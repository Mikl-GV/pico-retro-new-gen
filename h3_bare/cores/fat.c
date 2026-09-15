// fat.c — минимальный FAT32-ридер для SD-карты (без кэша, PIO-чтение секторов).
// Поддерживает: корневую директорию и подпапки 1 уровня, 8.3 + LFN (UTF-16),
// чтение файлов кластерами.
#include <string.h>
#include "fat.h"
#include "sd.h"
#include "uart.h"

extern int printf(const char* fmt, ...);

// helpers для записи little-endian
static void le16_enc(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
static void le32_enc(uint8_t* p, uint32_t v) { p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF; }

// forward: создание записи директории (определена ниже)
static void make_dir_entry(uint8_t* e, const char* name, uint32_t cluster, int is_dot);

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
    // пропуск до offset
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
        if (seq == 0xE5) return 0;
        int pos = (seq & 0x0F) - 1;
        if (pos >= 0 && pos < 20) {
            int off = pos * 13;
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

int fat_init(void) {
    uint32_t part_lba = 0; // найденный FAT32-раздел
    int part_idx = -1;

    if (sd_read_sector(0, g_sector) < 0) return -1;
    if (le16(g_sector + 510) != 0xAA55) return -1;

    // Сначала ищем второй раздел (partition 1, MBR-слот 446+16) с FAT32,
    // в корне которого есть папка /roms. Если нет — первый раздел.
    // partition 0 подробные данные? mbr_partition_offset + 16 * n
    int entries[4] = { 0, 1, 2, 3 };
    int priority[4];
    // приоритет: второй раздел → первый → третий → четвёртый
    priority[0] = 1; priority[1] = 0; priority[2] = 2; priority[3] = 3;

    for (int pi = 0; pi < 4; pi++) {
        int n = priority[pi];
        int off = 446 + n * 16;
        uint8_t type = g_sector[off + 4];
        if (type != 0x0B && type != 0x0C && type != 0x06) continue;
        part_lba = le32(g_sector + off + 8);
        if (part_lba == 0) continue;

        // Пробуем инициализировать этот раздел
        if (sd_read_sector(part_lba, g_sector) < 0) continue;
        if (le16(g_sector + 510) != 0xAA55) continue;
        uint16_t bps = le16(g_sector + 11);
        if (bps != 512) continue;
        uint32_t sec_per_cluster = g_sector[13];
        uint32_t reserved = le16(g_sector + 14);
        uint32_t num_fats = g_sector[16];
        uint32_t fat_size = le32(g_sector + 36);
        uint32_t root_cluster = le32(g_sector + 44);
        if (!fat_size || fat_size == 0xFFFFFFFF) continue;
        uint32_t data_start = part_lba + reserved + num_fats * fat_size;

        // Сохраняем временно
        g_part_lba = part_lba;
        g_sec_per_cluster = sec_per_cluster;
        g_reserved = reserved;
        g_num_fats = num_fats;
        g_fat_size = fat_size;
        g_root_cluster = root_cluster;
        g_data_start = data_start;

        // Проверяем наличие /roms в корне этого раздела
        char buf[8];
        fat_entry_t e;
        int found = 0;
        int cnt = read_dir(root_cluster, &e, 1);
        // перебираем корень в поисках папки roms
        fat_entry_t dirs[FAT_MAX_ENTRIES];
        int dn = read_dir(root_cluster, dirs, FAT_MAX_ENTRIES);
        for (int di = 0; di < dn; di++) {
            if (dirs[di].size == 0 && strcmp(dirs[di].name, "roms") == 0) { found = 1; break; }
        }

        if (found) {
            part_idx = n;
            break; // нашли раздел с ROMs!
        }
    }

    // Если не нашли со /roms — берём первый FAT32 (старое поведение)
    part_lba = 0;
    if (part_idx < 0) {
        for (int n = 0; n < 4; n++) {
            int off = 446 + n * 16;
            uint8_t type = g_sector[off + 4];
            if (type != 0x0B && type != 0x0C && type != 0x06) continue;
            part_lba = le32(g_sector + off + 8);
            if (part_lba != 0) { part_idx = n; break; }
        }
        if (part_idx < 0) return -1;
    }

    // Окончательно инициализируем выбранный раздел
    if (sd_read_sector(part_lba, g_sector) < 0) return -1;
    if (le16(g_sector + 510) != 0xAA55) return -1;
    uint16_t bps = le16(g_sector + 11);
    if (bps != 512) return -1;

    g_sec_per_cluster = g_sector[13];
    g_reserved        = le16(g_sector + 14);
    g_num_fats        = g_sector[16];
    g_fat_size        = le32(g_sector + 36);
    g_root_cluster    = le32(g_sector + 44);

    if (!g_fat_size || g_fat_size == 0xFFFFFFFF) return -1;

    g_data_start = part_lba + g_reserved + g_num_fats * g_fat_size;
    uint32_t total_sectors = le32(g_sector + 32);
    g_total_clusters = (total_sectors - g_data_start) / g_sec_per_cluster;

    return 0;
}

// ---- вспомогательные для записи ----

// forward declaration (определена ниже)
static int path_lookup(const char* path, fat_entry_t* out, char* buf, int buflen);

// 8.3 имя из строки (верхний регистр, без расширения если папка)
static void make_short_name(const char* name, uint8_t* out83) {
    for (int i = 0; i < 11; i++) out83[i] = ' ';
    int n = 0;
    for (int i = 0; name[i] && n < 8; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out83[n++] = (uint8_t)c;
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

// ---- Форматирование нового раздела и создание /roms с подпапками ----
// Вызывается из Settings → 1. Создаёт второй FAT32-раздел (если нет) или
// пересоздаёт его, форматирует, пишет /roms со всеми системными папками.
// ВНИМАНИЕ: запись в MBR — критическая операция.
static uint32_t calc_lba_end(void) {
    return g_part_lba + g_reserved + g_num_fats * g_fat_size + g_total_clusters * g_sec_per_cluster;
}

int fat_create_rom_partition(void) {
    uint8_t mbr[512];
    if (sd_read_sector(0, mbr) < 0) return -1;
    if (le16(mbr + 510) != 0xAA55) return -1;

    // Проверяем: есть ли уже раздел с /roms
    int existing = 0;
    for (int n = 0; n < 4; n++) {
        int off = 446 + n * 16;
        uint8_t type = mbr[off + 4];
        if (type != 0x0B && type != 0x0C && type != 0x06) continue;
        uint32_t lba = le32(mbr + off + 8);
        if (lba == 0) continue;
        // Проверяем BPB на /roms
        uint8_t sec[512];
        if (sd_read_sector(lba, sec) < 0) continue;
        if (le16(sec + 510) != 0xAA55) continue;
        uint32_t rc = le32(sec + 44);
        uint32_t ds = lba + le16(sec + 14) + sec[16] * le32(sec + 36);
        fat_entry_t dirs[FAT_MAX_ENTRIES];
        // временно переключаем глобалы
        uint32_t old_ds = g_data_start; int ok = 0;
        g_data_start = ds;
        g_root_cluster = rc;
        int dn = read_dir(rc, dirs, FAT_MAX_ENTRIES);
        for (int d = 0; d < dn; d++)
            if (dirs[d].size == 0 && strcmp(dirs[d].name, "roms") == 0) { ok = 1; break; }
        g_data_start = old_ds;
        if (ok) { existing = 1; break; }
    }
    if (existing) return 0;  // уже есть

    // --- Создаём второй FAT32-раздел ---
    // Конец первого раздела (всего LBA после data area)
    uint32_t part1_lba = 0, part1_end = 0;
    int slot = -1;
    for (int n = 0; n < 4; n++) {
        int off = 446 + n * 16;
        uint8_t type = mbr[off + 4];
        if (type != 0x0B && type != 0x0C && type != 0x06) continue;
        uint32_t lba = le32(mbr + off + 8);
        if (lba == 0 && slot < 0) { slot = n; continue; }  // пустой слот
        uint32_t sz = le32(mbr + off + 12);
        if (lba + sz > part1_end) { part1_lba = lba; part1_end = lba + sz; }
    }
    // Найти свободный слот
    if (slot < 0) for (int n = 0; n < 4; n++) if (le32(mbr + 446 + n * 16 + 8) == 0) { slot = n; break; }
    if (slot < 0) { printf("FAT: no free MBR slot\n"); return -1; }

    // Ёмкость карты для нового раздела
    int total_mb = sd_get_capacity_mb();
    if (total_mb < 256) { printf("FAT: card too small (%d MB)\n", total_mb); return -1; }
    uint32_t total_sectors = (uint32_t)total_mb * 2048;

    // Новый раздел: от part1_end до total_sectors - 1 (оставляем 4 МБ на всякий)
    if (part1_end < 64) part1_end = 64;  // min offset
    uint32_t part_lba = (part1_end + 63) & ~63;  // выравнивание на 64 сектора
    uint32_t part_sectors = total_sectors - part_lba - 8192;  // минус 4 МБ хвоста
    if (part_sectors < 65536) { printf("FAT: no space for 2nd partition\n"); return -1; }
    // FAT32 с 256-секторным кластером (~128 КБ)
    uint32_t sec_per_cluster = 64;  // 32K для карт > 32ГБ, 64 для 16ГБ
    uint32_t reserved = 32;
    uint32_t num_fats = 2;
    // FAT size: sectors = clusters * 4 / 512 = fat_sectors
    // clusters = part_sectors / sec_per_cluster + 1
    uint32_t approx_clusters = part_sectors / sec_per_cluster;
    uint32_t fat_sectors = (approx_clusters * 4 + 511) / 512;
    // пересчёт
    uint32_t data_start = part_lba + reserved + num_fats * fat_sectors;
    uint32_t data_sectors = part_sectors - reserved - num_fats * fat_sectors;
    uint32_t clusters = data_sectors / sec_per_cluster;

    // --- Запись MBR ---
    int off = 446 + slot * 16;
    mbr[off + 0] = 0x00;          // boot flag
    mbr[off + 1] = 0x01; mbr[off + 2] = 0x01; mbr[off + 3] = 0x00;  // CHS start
    mbr[off + 4] = 0x0C;          // FAT32 LBA
    // CHS end — заглушка
    mbr[off + 5] = 0xFE; mbr[off + 6] = 0xFF; mbr[off + 7] = 0xFF;
    mbr[off + 8] = part_lba & 0xFF; mbr[off + 9] = (part_lba >> 8) & 0xFF;
    mbr[off + 10] = (part_lba >> 16) & 0xFF; mbr[off + 11] = (part_lba >> 24) & 0xFF;
    mbr[off + 12] = part_sectors & 0xFF; mbr[off + 13] = (part_sectors >> 8) & 0xFF;
    mbr[off + 14] = (part_sectors >> 16) & 0xFF; mbr[off + 15] = (part_sectors >> 24) & 0xFF;

    if (sd_write_sector(0, mbr) < 0) { printf("FAT: MBR write failed\n"); return -1; }

    // --- Форматирование FAT32 ---
    uint8_t zero[512];
    memset(zero, 0, sizeof(zero));

    // BPB (sec 0 of partition)
    zero[0] = 0xEB; zero[1] = 0x58; zero[2] = 0x90;  // boot jump
    memcpy(zero + 3, "H3RETRO ", 8);                   // OEM
    le16_enc(zero + 11, 512);                           // bytes per sector
    zero[13] = (uint8_t)sec_per_cluster;                // sectors per cluster
    le16_enc(zero + 14, reserved);                      // reserved sectors
    zero[16] = num_fats;                                 // num FATs
    le16_enc(zero + 17, 0);                              // root entries (FAT32=0)
    le16_enc(zero + 19, 0);                              // total sectors16
    zero[21] = 0xF8;                                     // media
    le16_enc(zero + 22, 0);                              // FAT size16 (FAT32=0)
    le16_enc(zero + 24, 0);                              // sectors per track
    le16_enc(zero + 26, 0);                              // heads
    le32_enc(zero + 28, 0);                              // hidden sectors
    le32_enc(zero + 32, part_sectors);                    // total sectors32
    le32_enc(zero + 36, fat_sectors);                     // FAT size32
    le16_enc(zero + 40, 0);                              // ext flags
    le16_enc(zero + 42, 0);                              // FS version
    le32_enc(zero + 44, 2);                              // root cluster
    le16_enc(zero + 48, 1);                              // FSInfo sector
    le16_enc(zero + 50, 6);                              // backup boot sector
    for (int i = 52; i < 90; i++) zero[i] = 0;
    zero[82] = 0;  // drive number
    zero[83] = 0;  // reserved
    zero[84] = 0x29;  // extended boot signature
    le32_enc(zero + 87, 0x12345678);                     // volume serial
    memcpy(zero + 91, "H3 RETRO   ", 11);                // volume label
    memcpy(zero + 102, "FAT32   ", 8);                   // FS type
    zero[510] = 0x55; zero[511] = 0xAA;
    if (sd_write_sector(part_lba, zero) < 0) return -1;

    // FSInfo sector (sector 1)
    memset(zero, 0, 512);
    le32_enc(zero + 0, 0x41615252);
    le32_enc(zero + 484, 0x61417272);
    le32_enc(zero + 488, clusters + 2);  // free clusters
    le32_enc(zero + 492, 3);              // next free cluster hint
    le32_enc(zero + 508, 0xAA550000);
    if (sd_write_sector(part_lba + 1, zero) < 0) return -1;

    // FAT table (sectors)
    uint8_t fat_sec[512];
    memset(fat_sec, 0, 512);
    fat_sec[0] = 0xF8; fat_sec[1] = 0xFF; fat_sec[2] = 0xFF; fat_sec[3] = 0x0F;
    fat_sec[4] = 0xFF; fat_sec[5] = 0xFF; fat_sec[6] = 0xFF; fat_sec[7] = 0x0F;
    fat_sec[8] = 0xFF; fat_sec[9] = 0xFF; fat_sec[10] = 0xFF; fat_sec[11] = 0x0F;
    for (uint32_t f = 0; f < num_fats; f++) {
        uint32_t flba = part_lba + reserved + f * fat_sectors;
        if (sd_write_sector(flba, fat_sec) < 0) return -1;
        for (uint32_t s = 1; s < fat_sectors; s++) {
            uint8_t empty[512];
            memset(empty, 0, 512);
            if (sd_write_sector(flba + s, empty) < 0) return -1;
        }
    }

    // Root directory (cluster 2) + кластер 3 (папка /roms)
    // инициализируем через уже проверенные функции, временно переключая глобалы
    // на новый раздел.
    uint32_t old_gpl = g_part_lba, old_gsc = g_sec_per_cluster;
    uint32_t old_gr = g_reserved, old_gnf = g_num_fats;
    uint32_t old_gfs = g_fat_size, old_grc = g_root_cluster;
    uint32_t old_gds = g_data_start;

    g_part_lba = part_lba;
    g_sec_per_cluster = sec_per_cluster;
    g_reserved = reserved;
    g_num_fats = num_fats;
    g_fat_size = fat_sectors;
    g_data_start = data_start;

    // FAT для кластера 2 (корень) и 3 (roms) — на всякий случай затираем
    // но BPB уже записан, read_dir будет работать.
    // Создаём /roms через готовую fat_mkdir
    // Для этого нужна root-директория с . и ..
    // Записываем root-директорию через make_dir_entry прямо в кластер 2
    uint8_t rsec[512];
    memset(rsec, 0, 512);
    make_dir_entry(rsec, ".", 2, 1);
    make_dir_entry(rsec + 32, "..", 2, 2);
    if (sd_write_sector(data_start, rsec) < 0) return -1;
    for (uint32_t s = 1; s < g_sec_per_cluster; s++) {
        memset(rsec, 0, 512);
        if (sd_write_sector(data_start + s, rsec) < 0) return -1;
    }
    g_root_cluster = 2;

    // Помечаем кластер 2 (root) как EOC
    {
        uint32_t flba = g_part_lba + g_reserved;
        memset(rsec, 0, 512);
        rsec[0] = 0xF8; rsec[1] = 0xFF; rsec[2] = 0xFF; rsec[3] = 0x0F;
        rsec[4] = 0xFF; rsec[5] = 0xFF; rsec[6] = 0xFF; rsec[7] = 0x0F;
        rsec[8] = 0xFF; rsec[9] = 0xFF; rsec[10] = 0xFF; rsec[11] = 0x0F;
        if (sd_write_sector(flba, rsec) < 0) return -1;
    }

    // Создаём /roms через fat_mkdir
    int mk = fat_mkdir("/", "roms");
    if (mk < 0) { printf("FAT: mkdir /roms failed\n"); return -1; }

    // Создаём подпапки систем в /roms/
    static const char* sys_dirs[] = {
        "a2600", "a5200", "a7800", "nes", "sms", "gameboy",
        "portfolio", "gamegear", "pce", "snes", "megadrive"
    };
    int ok = 0, fail = 0;
    for (unsigned i = 0; i < sizeof(sys_dirs)/sizeof(sys_dirs[0]); i++) {
        if (fat_mkdir("/roms", sys_dirs[i]) >= 0) ok++; else fail++;
    }
    printf("FAT: /roms system folders: %d ok, %d fail\n", ok, fail);

    printf("FAT: partition %d created at LBA %u, %u sectors\n", slot, part_lba, part_sectors);
    printf("FAT: /roms created. Rebooting to use...\n");

    // Восстанавливаем старые глобалы
    g_part_lba = old_gpl; g_sec_per_cluster = old_gsc;
    g_reserved = old_gr; g_num_fats = old_gnf;
    g_fat_size = old_gfs; g_root_cluster = old_grc;
    g_data_start = old_gds;

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
    // читаем сектор (может содержать мусор), пишем заново
    uint8_t zero[512];
    memset(zero, 0, 512);
    // первая запись "."
    make_dir_entry(zero, ".", new_cl, 1);
    // вторая ".."
    make_dir_entry(zero + 32, "..", parent.first_cluster, 2);
    if (sd_write_sector(new_sec, zero) < 0) return -1;
    // остальные секторы кластера обнуляем
    for (uint32_t s = 1; s < g_sec_per_cluster; s++) {
        if (sd_write_sector(new_sec + s, zero) < 0) return -1;
    }

    // 4. Отметить в FAT: новый кластер = END (0x0FFFFFFF)
    if (fat_set_cluster(new_cl, 0x0FFFFFFF) < 0) return -1;

    // 5. Создать запись в родительской папке
    uint32_t dummy_sec;
    int off;
    if (find_free_entry(parent.first_cluster, &dummy_sec, &off) < 0) {
        // нет места — не получится (упрощённо)
        return -1;
    }
    if (sd_read_sector(dummy_sec, g_sector) < 0) return -1;
    make_dir_entry(g_sector + off, name, new_cl, 0);
    if (sd_write_sector(dummy_sec, g_sector) < 0) return -1;

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
        fat_entry_t entries[FAT_MAX_ENTRIES];
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
    fat_entry_t list[FAT_MAX_ENTRIES];
    int n = fat_list(dir, list, FAT_MAX_ENTRIES);
    for (int i = 0; i < n; i++)
        if (name_eq(list[i].name, name)) { *out = list[i]; return 1; }
    return 0;
}

int fat_read_file(const fat_entry_t* f, uint32_t offset, uint8_t* buf, uint32_t len) {
    if (f->size == 0 || offset >= f->size) return 0;
    if (offset + len > f->size) len = f->size - offset;
    return read_chain(f->first_cluster, offset, buf, len);
}