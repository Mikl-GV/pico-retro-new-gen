// fat.c — минимальный FAT32-ридер для SD-карты (без кэша, PIO-чтение секторов).
// Поддерживает: корневую директорию и подпапки 1 уровня, 8.3 + LFN (UTF-16),
// чтение файлов кластерами.
#include <string.h>
#include "fat.h"
#include "sd.h"
#include "uart.h"

extern int printf(const char* fmt, ...);

// forward: создание записи директории, сравнение имён (определены ниже)
static void make_dir_entry(uint8_t* e, const char* name, uint32_t cluster, int is_dot);
static int name_eq(const char* a, const char* b);

// ---- BPB (загрузочный сектор) ----
typedef struct {
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;      // FAT32 = 0
    uint16_t total_sectors16;
    uint8_t  media;
    uint16_t fat_size16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;
    uint32_t fat_size32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
} fat_bpb_t;

static uint8_t  g_sector[512];
static uint32_t g_sec_per_cluster;
static uint32_t g_reserved;
static uint32_t g_num_fats;
static uint32_t g_fat_size;
static uint32_t g_root_cluster;
static uint32_t g_data_start;   // первый сектор data region
static uint32_t g_total_clusters;
static uint32_t g_part_lba;     // LBA начала FAT-раздела (сдвиг для чтения)

static uint32_t le16(const uint8_t* p) { return p[0] | ((uint32_t)p[1] << 8); }
static uint32_t le32(const uint8_t* p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// ---- кластер -> сектор (с учётом сдвига раздела) ----
static uint32_t cluster_to_sector(uint32_t cl) {
    return g_data_start + (cl - 2) * g_sec_per_cluster;
}

// ---- чтение следующего кластера по FAT ----
static uint32_t fat_next_cluster(uint32_t cl) {
    uint32_t fat_off = cl * 4;
    uint32_t sec = g_part_lba + g_reserved + (fat_off / 512);
    if (sd_read_sector(sec, g_sector) < 0) return 0x0FFFFFFF;
    uint32_t v = le32(g_sector + (fat_off % 512)) & 0x0FFFFFFF;
    return v;
}

// ---- чтение по байтам из цепочки кластеров ----
static uint32_t read_chain(uint32_t cl, uint32_t offset, uint8_t* buf, uint32_t len) {
    if (g_sec_per_cluster == 0) return 0;
    uint32_t cl_size = g_sec_per_cluster * 512;
    uint32_t skip = offset;
    while (skip >= cl_size) {
        cl = fat_next_cluster(cl);
        if (cl >= 0x0FFFFFF8) return 0;
        skip -= cl_size;
    }
    uint32_t done = 0;
    while (done < len) {
        if (cl == 0 || cl >= 0x0FFFFFF8) break;
        uint32_t sec_off = cluster_to_sector(cl) + skip / 512;
        uint32_t in_sec  = skip % 512;
        uint32_t avail   = cl_size - skip;
        uint32_t want    = len - done;
        if (want > avail) want = avail;
        while (want > 0) {
            if (sd_read_sector(sec_off, g_sector) < 0) return done;
            uint32_t n = 512 - in_sec;
            if (n > want) n = want;
            memcpy(buf + done, g_sector + in_sec, n);
            done += n; want -= n;
            in_sec = 0;
            sec_off++;
        }
        cl = fat_next_cluster(cl);
        skip = 0;
    }
    return done;
}

// ---- обработка одной записи директории ----
// Возвращает 1 если запись обработана (файл/папка), 0 если пустая, -1 конец.
// LFN-записи копятся в lfn_buf.
// Порядок на диске: последний фрагмент (0x40|n) идёт ПЕРВЫМ, за ним
// фрагменты в обратном порядке, затем 8.3-запись.
// Пример: "Adventure (Color Scrolling Hack).a26" (>13 символов):
//   seq=0x43: "ng Hack).a26"    (pos=2, последний фрагмент)
//   seq=0x02: "r Scrolling Ha"  (pos=1)
//   seq=0x01: "Adventure (Co"   (pos=0, ближайший к 8.3)
// После всех фрагментов собираем: [pos0][pos1][pos2] → null-terminate.
static int parse_dir_entry(const uint8_t* e, fat_entry_t* out, char* lfn, int* lfn_len) {
    uint8_t attr = e[11];
    // LFN запись
    if (attr == 0x0F) {
        uint8_t seq = e[0];
        if (seq == 0xE5) { lfn[0] = 0; *lfn_len = 0; return 0; }   // удалённая LFN — сбросить буфер
        int pos = (seq & 0x0F) - 1;
        if (pos >= 0 && pos < 20) {
            int off = pos * 13;
            if (off + 13 > FAT_NAME_LEN - 1) return 0;   // имя длиннее нашего буфера — не читаем
            int idx = 0;
            for (int i = 0; i < 10; i += 2) {
                uint16_t c = e[1 + i] | ((uint16_t)e[2 + i] << 8);
                if (c == 0 || c == 0xFFFF) break;
                lfn[off + idx++] = (char)c;
            }
            for (int i = 0; i < 12; i += 2) {
                uint16_t c = e[14 + i] | ((uint16_t)e[15 + i] << 8);
                if (c == 0 || c == 0xFFFF) break;
                lfn[off + idx++] = (char)c;
            }
            for (int i = 0; i < 4; i += 2) {
                uint16_t c = e[28 + i] | ((uint16_t)e[29 + i] << 8);
                if (c == 0 || c == 0xFFFF) break;
                lfn[off + idx++] = (char)c;
            }
            int end = off + idx;
            if (end > *lfn_len) *lfn_len = end;
        }
        return 0;
    }

    uint8_t first = e[0];
    if (first == 0x00) return -1;    // конец директории
    if (first == 0xE5) { lfn[0] = 0; *lfn_len = 0; return 0; } // удалённая

    uint8_t short_attr = attr;
    // системные/volume — пропускаем (кроме директорий)
    if (short_attr & (0x08 | 0x20)) { // volume label / archive
        if ((short_attr & 0x08) && !(short_attr & 0x10)) { lfn[0] = 0; *lfn_len = 0; return 0; }
    }

    // имя: LFN если есть, иначе 8.3
    if (lfn[0]) {
        lfn[*lfn_len] = 0;   // null-terminate LFN
        strncpy(out->name, lfn, FAT_NAME_LEN - 1);
        out->name[FAT_NAME_LEN - 1] = 0;
    } else {
        char n[9], x[4];
        memcpy(n, e, 8); n[8] = 0;
        memcpy(x, e + 8, 3); x[3] = 0;
        for (int i = 7; i >= 0 && n[i] == ' '; i--) n[i] = 0;
        for (int i = 2; i >= 0 && x[i] == ' '; i--) x[i] = 0;
        char* d = out->name;
        int pi = 0;
        for (int i = 0; n[i] && pi < FAT_NAME_LEN - 1; i++) d[pi++] = n[i];
        if (x[0]) {
            d[pi++] = '.';
            for (int i = 0; x[i] && pi < FAT_NAME_LEN - 1; i++) d[pi++] = x[i];
        }
        d[pi] = 0;
    }

    out->first_cluster = le16(e + 26) | (le16(e + 20) << 16);
    out->size = (short_attr & 0x10) ? 0 : le32(e + 28);
    lfn[0] = 0;
    *lfn_len = 0;
    return 1;
}

// ---- чтение директории (кластеры) ----
static int read_dir(uint32_t cl, fat_entry_t* out, int max) {
    int count = 0;
    char lfn[FAT_NAME_LEN] = {0};
    int lfn_len = 0;

    while (cl && cl < 0x0FFFFFF8) {
        uint32_t sec = cluster_to_sector(cl);
        for (uint32_t s = 0; s < g_sec_per_cluster; s++) {
            if (sd_read_sector(sec + s, g_sector) < 0) return count;
            for (int i = 0; i < 512; i += 32) {
                const uint8_t* e = g_sector + i;
                int r = parse_dir_entry(e, &out[count], lfn, &lfn_len);
                if (r < 0) return count;
                if (r > 0) {
                    // пропускаем служебные . и ..
                    if (out[count].name[0] == '.' &&
                        (out[count].name[1] == 0 || (out[count].name[1] == '.' && out[count].name[2] == 0)))
                        continue;
                    count++;
                    if (count >= max) return count;
                }
            }
        }
        cl = fat_next_cluster(cl);
    }
return count;
}

static fat_entry_t g_scratch_dir[FAT_MAX_ENTRIES];

fat_entry_t* fat_scratch(void) {
    return g_scratch_dir;
}

int fat_init(void) {
    uint32_t part_lba = 0;
    int part_idx = -1;

    if (sd_read_sector(0, g_sector) < 0) return -1;
    if (le16(g_sector + 510) != 0xAA55) return -1;

    // --- Сбор всех FAT32-разделов (MBR + GPT) в список ---
    // Каждый: lba начала.
    enum { MAX_PART = 16 };
    uint32_t plt_start[MAX_PART];
    int part_count = 0;

    // MBR-разделы
    for (int n = 0; n < 4; n++) {
        int off = 446 + n * 16;
        uint8_t type = g_sector[off + 4];
        if (type == 0x0B || type == 0x0C || type == 0x06) {
            uint32_t lba = le32(g_sector + off + 8);
            if (lba != 0 && part_count < MAX_PART) {
                plt_start[part_count] = lba;
                part_count++;
            }
        }
    }

    // GPT-разделы (если MBR не дал результата или защитный 0xEE)
    int mbr_is_protective = 0;
    for (int n = 0; n < 4; n++) {
        if (g_sector[446 + n * 16 + 4] == 0xEE) { mbr_is_protective = 1; break; }
    }
    if (mbr_is_protective) {
        uint8_t gpthdr[512];
        // GPT header в LBA 1
        if (sd_read_sector(1, gpthdr) == 0 &&
            memcmp(gpthdr, "EFI PART", 8) == 0) {
            uint32_t tbl_lba = le32(gpthdr + 72); // partition entry array LBA
            uint32_t nentries = le32(gpthdr + 80);
            uint32_t esize = le32(gpthdr + 84);
            if (esize < 128) esize = 128;
            if (nentries > 512) nentries = 512;
            // Читаем таблицу: nentries * esize, читаем по секторам
            uint8_t tbl[512];
            uint32_t base = tbl_lba;
            for (uint32_t e = 0; e < nentries && part_count < MAX_PART; e++) {
                uint32_t sec = base + (e * esize) / 512;
                uint32_t off = (e * esize) % 512;
                if (off == 0) {
                    if (sd_read_sector(sec, tbl) < 0) break;
                }
                // Тип GUID — FAT32 / basic data: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
                // LBA первое (48 бит) на offset 32 в запись
                const uint8_t* en = tbl + off;
                // проверяем GUID на валидность (не нули)
                int nonzero = 0;
                for (int b = 0; b < 16; b++) if (en[b]) { nonzero = 1; break; }
                if (!nonzero) continue;
                uint64_t first = (uint64_t)en[32] | ((uint64_t)en[33] << 8) |
                                 ((uint64_t)en[34] << 16) | ((uint64_t)en[35] << 24) |
                                 ((uint64_t)en[36] << 32) | ((uint64_t)en[37] << 40) |
                                 ((uint64_t)en[38] << 48) | ((uint64_t)en[39] << 56);
                if (first == 0) continue;
                plt_start[part_count] = (uint32_t)first;
                part_count++;
            }
            printf("FAT: GPT found, %u entries usable\n", part_count);
        }
    }
    if (part_count == 0) return -1;

    // --- 1. Ищем раздел с /roms. Системный (первый) в поиске НЕ участвует. ---
    // Приоритет: сначала ищем в не-system, потом если только один — он.
    // Сначала проверяем все кроме первого (системный = первый по счёту)
    for (int pi = 1; pi < part_count; pi++) {
        uint32_t lba = plt_start[pi];
        if (sd_read_sector(lba, g_sector) < 0) continue;
        if (le16(g_sector + 510) != 0xAA55) continue;
        if (le16(g_sector + 11) != 512) continue;

        uint32_t rc = le32(g_sector + 44);
        uint32_t ds = lba + le16(g_sector + 14) + g_sector[16] * le32(g_sector + 36);
        uint32_t old_pl = g_part_lba, old_sc = g_sec_per_cluster;
        uint32_t old_rs = g_reserved, old_nf = g_num_fats;
        uint32_t old_fs = g_fat_size;
        g_part_lba = lba;
        g_sec_per_cluster = g_sector[13];
        g_reserved = le16(g_sector + 14);
        g_num_fats = g_sector[16];
        g_fat_size = le32(g_sector + 36);
        g_root_cluster = rc; g_data_start = ds;

        fat_entry_t* dirs = g_scratch_dir;
        int dn = read_dir(rc, dirs, FAT_MAX_ENTRIES);
        int found = 0;
        for (int d = 0; d < dn; d++)
            if (dirs[d].size == 0 && name_eq(dirs[d].name, "roms")) { found = 1; break; }

        g_part_lba = old_pl; g_sec_per_cluster = old_sc;
        g_reserved = old_rs; g_num_fats = old_nf;
        g_fat_size = old_fs;

        if (found) {
            part_lba = lba; part_idx = pi;
            break;
        }
    }

    // --- 2. Если с /roms не нашли — берём первый не-system раздел ---
    if (part_idx < 0 && part_count > 1) {
        part_lba = plt_start[1];
        part_idx = 1;
    }

    // --- 3. Если нет других разделов — падаем на первый (не создаём /roms) ---
    if (part_idx < 0) {
        part_lba = plt_start[0];
        part_idx = 0;
    }

    // Инициализируем выбранный раздел
    if (sd_read_sector(part_lba, g_sector) < 0) return -1;
    if (le16(g_sector + 510) != 0xAA55) return -1;
    if (le16(g_sector + 11) != 512) return -1;

    g_part_lba      = part_lba;
    g_sec_per_cluster = g_sector[13];
    g_reserved        = le16(g_sector + 14);
    g_num_fats        = g_sector[16];
    g_fat_size        = le32(g_sector + 36);
    g_root_cluster    = le32(g_sector + 44);

    if (!g_fat_size || g_fat_size == 0xFFFFFFFF) return -1;

    g_data_start = part_lba + g_reserved + g_num_fats * g_fat_size;
    uint32_t total_sectors = le32(g_sector + 32);
    g_total_clusters = (total_sectors - g_data_start) / g_sec_per_cluster;

    printf("FAT: part LBA=%u sec/clu=%u reserved=%u fats=%u fat_size=%u root_clu=%u data_start=%u total_clu=%u\n",
           part_lba, g_sec_per_cluster, g_reserved, g_num_fats, g_fat_size,
           g_root_cluster, g_data_start, g_total_clusters);

    return 0;
}

// forward declaration (определена ниже)
static int path_lookup(const char* path, fat_entry_t* out, char* buf, int buflen);

// 8.3 имя из строки (верхний регистр, без расширения если папка).
// Если имя длиннее 6 символов (и не умещается в 8), добавляем суффикс
// "~N" (FAT-стиль), чтобы избежать коллизий 8.3-имён для разных папок.
static void make_short_name(const char* name, uint8_t* out83) {
    for (int i = 0; i < 11; i++) out83[i] = ' ';
    int n = 0;
    int total = 0;
    while (name[total]) total++;
    int keep = (total > 8) ? 6 : total;
    for (int i = 0; name[i] && n < keep; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out83[n++] = (uint8_t)c;
    }
    if (total > 8) {
        // "~1".."~9": 6 символов + "~N" = 8
        out83[6] = '~';
        out83[7] = '1';
    }
    // расширение не заполняем — это папка
}

// поиск свободного кластера (по FAT), возвращает номер или 0
static uint32_t find_free_cluster(void) {
    for (uint32_t cl = 2; cl < 2 + g_total_clusters; cl++) {
        uint32_t fat_off = cl * 4;
        uint32_t sec = g_part_lba + g_reserved + (fat_off / 512);
        if (sd_read_sector(sec, g_sector) < 0) return 0;
        uint32_t v = le32(g_sector + (fat_off % 512)) & 0x0FFFFFFF;
        if (v == 0) return cl;
    }
    return 0;
}

// записать значение в FAT (обе копии)
static int fat_set_cluster(uint32_t cl, uint32_t val) {
    uint32_t fat_off = cl * 4;
    uint32_t sec = g_part_lba + g_reserved + (fat_off / 512);
    uint32_t off = fat_off % 512;
    for (uint32_t copy = 0; copy < g_num_fats; copy++) {
        uint32_t s = sec + copy * g_fat_size;
        if (sd_read_sector(s, g_sector) < 0) return -1;
        uint32_t v = le32(g_sector + off) & 0xF0000000; // сохранить старшие 4 бита
        v |= (val & 0x0FFFFFFF);
        g_sector[off] = v & 0xFF;
        g_sector[off+1] = (v >> 8) & 0xFF;
        g_sector[off+2] = (v >> 16) & 0xFF;
        g_sector[off+3] = (v >> 24) & 0xFF;
        if (sd_write_sector(s, g_sector) < 0) return -1;
    }
    return 0;
}

// создать пустую запись директории (32 байта) в g_sector+off
static void make_dir_entry(uint8_t* e, const char* name, uint32_t cluster, int is_dot) {
    memset(e, 0, 32);
    if (is_dot) {
        // ".", ".." — специальные
        make_short_name(is_dot == 1 ? "." : "..", e);
    } else {
        make_short_name(name, e);
    }
    e[11] = 0x10; // directory attr
    e[20] = (cluster >> 16) & 0xFF;
    e[21] = (cluster >> 24) & 0xFF;
    e[26] = cluster & 0xFF;
    e[27] = (cluster >> 8) & 0xFF;
}

// checksum по 8.3 имени (для LFN-записи)
static uint8_t lfn_checksum(const uint8_t* shortname11) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = ((sum & 1) << 7) + (sum >> 1) + shortname11[i];
    return sum;
}

// ОДИН LFN-фрагмент: seq = 1..N (0x40|N для последнего на диске),
// хранит 13 UTF-16 символов имени с позиции start.
static void make_lfn_fragment(uint8_t* e, int seq, const char* name, int start, uint8_t chksum) {
    memset(e, 0, 32);
    e[0] = (uint8_t)seq;
    e[11] = 0x0F;
    e[13] = chksum;
    int len = (int)strlen(name);
    for (int i = 0; i < 13; i++) {
        uint16_t c = 0xFFFF;
        int idx = start + i;
        if (idx < len) c = (uint16_t)(uint8_t)name[idx];
        int off = (i < 5) ? 1 + i * 2 : (i < 11) ? 14 + (i - 5) * 2 : 28 + (i - 11) * 2;
        e[off] = c & 0xFF;
        e[off + 1] = (c >> 8) & 0xFF;
    }
}

// Найти count подряд свободных записей ВНУТРИ одного сектора директории.
static int find_free_entries(uint32_t cl, int count, uint32_t* first_sec, int* first_off) {
    uint32_t base = cluster_to_sector(cl);
    for (uint32_t s = 0; s < g_sec_per_cluster; s++) {
        if (sd_read_sector(base + s, g_sector) < 0) return -1;
        for (int off = 0; off + 32 * count <= 512; off += 32) {
            int ok = 1;
            for (int j = 0; j < count; j++) {
                uint8_t b = g_sector[off + j * 32];
                if (b != 0x00 && b != 0xE5) { ok = 0; break; }
            }
            if (ok) { *first_sec = base + s; *first_off = off; return 0; }
        }
    }
    return -1;
}

// найти свободное место в директории (пустая/удалённая запись)
static int find_free_entry(uint32_t cl, uint32_t* sec_out, int* off_out) {
    while (cl && cl < 0x0FFFFFF8) {
        uint32_t base = cluster_to_sector(cl);
        for (uint32_t s = 0; s < g_sec_per_cluster; s++) {
            if (sd_read_sector(base + s, g_sector) < 0) return -1;
            for (int i = 0; i < 512; i += 32) {
                uint8_t first = g_sector[i];
                if (first == 0x00 || first == 0xE5) {
                    *sec_out = base + s;
                    *off_out = i;
                    return 0;
                }
            }
        }
        cl = fat_next_cluster(cl);
    }
    return -1;
}

// Создать папку name в parent_path (например "/roms").
// Возвращает 0 при успехе, -1 при ошибке.
int fat_mkdir(const char* parent_path, const char* name) {
    // 1. Найти родительскую директорию
    fat_entry_t parent;
    char buf[FAT_NAME_LEN];
    if (!path_lookup(parent_path, &parent, buf, FAT_NAME_LEN))
        return -1;
    if (parent.size != 0) return -1;

    // 2. Свободный кластер для новой папки
    uint32_t new_cl = find_free_cluster();
    if (new_cl == 0) return -1;

    // 3. Инициализировать кластер: ".", ".."
    uint32_t new_sec = cluster_to_sector(new_cl);
    uint8_t zero[512];
    memset(zero, 0, 512);
    // первая запись "."
    make_dir_entry(zero, ".", new_cl, 1);
    // вторая ".."
    make_dir_entry(zero + 32, "..", parent.first_cluster, 2);
    if (sd_write_sector(new_sec, zero) < 0) return -1;
    // остальные секторы кластера — ЧИСТЫЕ нули. Раньше сюда писался тот же
    // `zero` с ".", "..", из-за чего каждый сектор кластера выглядел как
    // начало директории и на ПК/в парсерах появлялись мусорные записи.
    uint8_t empty[512];
    memset(empty, 0, 512);
    for (uint32_t s = 1; s < g_sec_per_cluster; s++) {
        if (sd_write_sector(new_sec + s, empty) < 0) return -1;
    }

    // 4. Отметить в FAT: новый кластер = END (0x0FFFFFFF)
    if (fat_set_cluster(new_cl, 0x0FFFFFFF) < 0) return -1;

    // 5. Создать запись(и) в родительской папке: LFN-фрагменты + 8.3
    int namelen = (int)strlen(name);
    int lfn_count = 0;
    // LFN нужен, если имя длиннее 8 (капс 8.3 обрежет)
    {
        char upper[9];
        int u = 0;
        for (int i = 0; name[i] && i < 8; i++) {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            upper[u++] = c;
        }
        upper[u] = 0;
        (void)upper; // достаточно проверить длину ниже
    }
    if (namelen > 8) {
        lfn_count = (namelen + 12) / 13;   // фрагменты по 13 символов
    }

    uint32_t dummy_sec;
    int off;
    if (lfn_count > 0) {
        // Нужны lfn_count + 1 (8.3) подряд свободных записей
        if (find_free_entries(parent.first_cluster, lfn_count + 1, &dummy_sec, &off) < 0)
            return -1;
    } else {
        if (find_free_entry(parent.first_cluster, &dummy_sec, &off) < 0)
            return -1;
    }
    if (sd_read_sector(dummy_sec, g_sector) < 0) return -1;

    // 8.3 имя (заглавное, короткое) — им же считаем checksum
    uint8_t short83[11];
    make_short_name(name, short83);
    uint8_t chk = lfn_checksum(short83);

    if (lfn_count > 0) {
        // LFN-фрагменты идут от последнего (seq с 0x40) к первому,
        // непосредственно ПЕРЕД 8.3-записью (последний фрагмент сразу перед ней).
        for (int i = lfn_count - 1; i >= 0; i--) {
            int seq = i + 1;
            if (i == lfn_count - 1) seq |= 0x40;    // последний фрагмент — бит 0x40
            make_lfn_fragment(g_sector + off, seq, name, i * 13, chk);
            off += 32;
        }
        make_dir_entry(g_sector + off, name, new_cl, 0);
        // 8.3 запись уже считана в буфере; низкие байты кластера
        // make_dir_entry сама выставит.
    } else {
        make_dir_entry(g_sector + off, name, new_cl, 0);
    }
    if (sd_write_sector(dummy_sec, g_sector) < 0) return -1;

    return 0;
}

// Удалить файл: dir = например "/roms/nes", name = "game.nes".
// Имя сравнивается с ПОЛНЫМ именем (LFN если есть, иначе 8.3) — браузер
// отдаёт LFN-имена, поэтому 8.3-сравнение не находило файл.
// Помечает 0xE5 саму запись и ВСЕ её LFN-фрагменты (в т.ч. в предыдущих
// секторах директории), затем освобождает кластеры в обеих FAT.
int fat_delete_file(const char* dir, const char* name) {
    fat_entry_t d;
    char buf[FAT_NAME_LEN];
    if (!path_lookup(dir, &d, buf, FAT_NAME_LEN)) return -1;
    if (d.size != 0) return -1;  // не директория

    // LFN-фрагменты копятся при обходе ВПЕРЁД (они лежат ПЕРЕД своей 8.3
    // записью), вместе с адресами секторов — чтобы потом пометить 0xE5
    // даже те, что перешли на предыдущий сектор.
    char lfn[FAT_NAME_LEN] = {0};
    int lfn_max = 0;
    uint32_t lfn_lba[20];
    int lfn_off[20];
    int lfn_cnt = 0;

    uint32_t cl = d.first_cluster;
    uint32_t target_first = 0;
    uint32_t entry_sec = 0;
    int entry_off = 0;
    int found = 0;

    while (cl && cl < 0x0FFFFFF8 && !found) {
        uint32_t base = cluster_to_sector(cl);
        for (uint32_t s = 0; s < g_sec_per_cluster && !found; s++) {
            if (sd_read_sector(base + s, g_sector) < 0) return -1;
            for (int i = 0; i < 512 && !found; i += 32) {
                uint8_t first = g_sector[i];
                uint8_t attr = g_sector[i + 11];
                if (first == 0x00) return -1;    // конец директории — дальше нет
                if (first == 0xE5) { lfn_cnt = 0; lfn_max = 0; continue; } // удалённая

                if (attr == 0x0F) {
                    // LFN-фрагмент: собираем в буфер по позиции и запоминаем адрес
                    int pos = (first & 0x0F) - 1;
                    if (pos >= 0 && pos < 20 && lfn_cnt < 20) {
                        lfn_lba[lfn_cnt] = base + s;
                        lfn_off[lfn_cnt] = i;
                        lfn_cnt++;
                        int dst = pos * 13;
                        if (dst >= FAT_NAME_LEN - 1) continue;   // имя длиннее нашего буфера — не читаем
                        int idx = 0;
                        for (int k = 0; k < 10; k += 2) {
                            if (dst + idx >= FAT_NAME_LEN - 1) break;
                            uint16_t c = g_sector[i + 1 + k] | ((uint16_t)g_sector[i + 2 + k] << 8);
                            if (c == 0 || c == 0xFFFF) break;
                            lfn[dst + idx++] = (char)c;
                        }
                        for (int k = 0; k < 12; k += 2) {
                            if (dst + idx >= FAT_NAME_LEN - 1) break;
                            uint16_t c = g_sector[i + 14 + k] | ((uint16_t)g_sector[i + 15 + k] << 8);
                            if (c == 0 || c == 0xFFFF) break;
                            lfn[dst + idx++] = (char)c;
                        }
                        for (int k = 0; k < 4; k += 2) {
                            if (dst + idx >= FAT_NAME_LEN - 1) break;
                            uint16_t c = g_sector[i + 28 + k] | ((uint16_t)g_sector[i + 29 + k] << 8);
                            if (c == 0 || c == 0xFFFF) break;
                            lfn[dst + idx++] = (char)c;
                        }
                        int end = dst + idx;
                        if (end > lfn_max) lfn_max = end;
                    }
                    continue;
                }

                // обычная 8.3 запись: полное имя = LFN если есть, иначе 8.3
                char full[FAT_NAME_LEN];
                int flen;
                if (lfn_cnt > 0) {
                    lfn[lfn_max] = 0;
                    flen = lfn_max;
                    if (flen >= FAT_NAME_LEN) flen = FAT_NAME_LEN - 1;
                    memcpy(full, lfn, flen);
                    full[flen] = 0;
                } else {
                    char n[13];
                    int pi = 0;
                    for (int c = 0; c < 8 && g_sector[i + c] != ' ' && pi < 12; c++)
                        n[pi++] = (char)g_sector[i + c];
                    if (attr != 0x10 && g_sector[i + 8] != ' ') {
                        n[pi++] = '.';
                        for (int c = 0; c < 3 && g_sector[i + 8 + c] != ' ' && pi < 12; c++)
                            n[pi++] = (char)g_sector[i + 8 + c];
                    }
                    flen = pi;
                    memcpy(full, n, flen);
                    full[flen] = 0;
                }

                if (attr != 0x10 && name_eq(full, name)) {   // файл (не папка)
                    // Не пишем 0xE5 сразу — LFN-фрагменты могут быть
                    // в том же секторе и перезатрём. Откладываем.
                    target_first = le16(g_sector + i + 26) | (le16(g_sector + i + 20) << 16);
                    entry_sec = base + s;
                    entry_off = i;
                    found = 1;
                    break;
                }
                lfn_cnt = 0; lfn_max = 0;   // не наш файл или директория — сброс LFN
            }
        }
        if (!found) cl = fat_next_cluster(cl);
    }
    if (!found || target_first == 0) return -1;

    // Пометить 0xE5 все LFN-фрагменты найденного файла
    for (int k = 0; k < lfn_cnt; k++) {
        if (sd_read_sector(lfn_lba[k], g_sector) < 0) continue;
        g_sector[lfn_off[k]] = 0xE5;
        if (sd_write_sector(lfn_lba[k], g_sector) < 0) return -1;
    }

    // Теперь пометить 0xE5 саму 8.3 запись (уже после LFN, чтобы не перезатереть)
    if (sd_read_sector(entry_sec, g_sector) < 0) return -1;
    g_sector[entry_off] = 0xE5;
    if (sd_write_sector(entry_sec, g_sector) < 0) return -1;

    // Освободить кластеры файла в обеих FAT (с защитой от цикла в цепочке)
    uint32_t fc = target_first;
    uint32_t guard = 0;
    while (fc >= 2 && fc < 0x0FFFFFF8 && guard++ < 100000) {
        uint32_t next = fat_next_cluster(fc);
        if (next == 0) { fat_set_cluster(fc, 0); break; } // кластер 0 — стоп
        fat_set_cluster(fc, 0);
        if (next == fc || next >= 0x0FFFFFF8) break;
        fc = next;
    }
    return 0;
}

// имя без ведущих пробелов (для сравнения); FAT-имена регистронезависимы
static int name_eq(const char* a, const char* b) {
    while (*a == ' ' || *a == '\t') a++;
    while (*b == ' ' || *b == '\t') b++;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == 0 && *b == 0);
}

static int path_lookup(const char* path, fat_entry_t* out, char* buf, int buflen) {
    uint32_t cluster = g_root_cluster;
    const char* p = path;
    while (*p == '/') p++;
    if (*p == 0) {
        out->first_cluster = cluster;
        out->size = 0;
        strcpy(out->name, "/");
        return 1;
    }
    for (;;) {
        const char* slash = p;
        while (*slash && *slash != '/') slash++;
        int n = (int)(slash - p);
        if (n == 0 || n >= buflen) return 0;
        memcpy(buf, p, n); buf[n] = 0;
        fat_entry_t* entries = g_scratch_dir;
        int cnt = read_dir(cluster, entries, FAT_MAX_ENTRIES);
        int found = 0;
        for (int i = 0; i < cnt; i++) {
            if (name_eq(entries[i].name, buf)) { *out = entries[i]; found = 1; break; }
        }
        if (!found) return 0;
        if (*slash == 0) return 1;
        if (out->size != 0) return 0;
        cluster = out->first_cluster;
        p = slash + 1;
    }
}

int fat_list(const char* dir, fat_entry_t* out, int max) {
    if (!dir || dir[0] == 0 || strcmp(dir, "/") == 0)
        return read_dir(g_root_cluster, out, max);
    fat_entry_t d;
    char buf[FAT_NAME_LEN];
    if (!path_lookup(dir, &d, buf, FAT_NAME_LEN)) return 0;
    if (d.size != 0) return 0;
    return read_dir(d.first_cluster, out, max);
}

int fat_find(const char* dir, const char* name, fat_entry_t* out) {
    // Копируем имя ДО fat_list — name может указывать в g_scratch_dir,
    // который затрётся вызовом fat_list
    char target[FAT_NAME_LEN];
    if (!name) return 0;
    int tlen = strlen(name);
    if (tlen >= FAT_NAME_LEN) tlen = FAT_NAME_LEN - 1;
    memcpy(target, name, tlen);
    target[tlen] = 0;

    fat_entry_t* list = g_scratch_dir;
    int n = fat_list(dir, list, FAT_MAX_ENTRIES);
    for (int i = 0; i < n; i++)
        if (name_eq(list[i].name, target)) { *out = list[i]; return 1; }
    return 0;
}

int fat_read_file(const fat_entry_t* f, uint32_t offset, uint8_t* buf, uint32_t len) {
    if (f->size == 0 || offset >= f->size) return 0;
    if (offset + len > f->size) len = f->size - offset;
    return read_chain(f->first_cluster, offset, buf, len);
}