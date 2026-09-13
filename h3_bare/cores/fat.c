// fat.c — минимальный FAT32-ридер для SD-карты (без кэша, PIO-чтение секторов).
// Поддерживает: корневую директорию и подпапки 1 уровня, 8.3 + LFN (UTF-16),
// чтение файлов кластерами.
#include <string.h>
#include "fat.h"
#include "sd.h"
#include "uart.h"

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
    return g_part_lba + g_data_start + (cl - 2) * g_sec_per_cluster;
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
// LFN-записи копятся в lfn_buf. lfn_len обновляется через указатель.
static int parse_dir_entry(const uint8_t* e, fat_entry_t* out, char* lfn, int* lfn_len) {
    uint8_t attr = e[11];
    // LFN запись
    if (attr == 0x0F) {
        uint8_t seq = e[0];
        int idx = (seq & 0x0F) - 1;              // 0..19, идём сверху вниз (seq 0x41,0x42..)
        if (idx >= 0 && idx < 20) {
            // берём по 13 символов с этой LFN-записи
            char tmp[14];
            int n = 0;
            for (int i = 0; i < 10 && n < 13; i += 2) {
                uint16_t c = e[1 + i] | ((uint16_t)e[2 + i] << 8);
                if (c == 0) break;
                tmp[n++] = (c >= 0x80) ? '?' : (char)c;
            }
            for (int i = 0; i < 12 && n < 13; i += 2) {
                uint16_t c = e[14 + i] | ((uint16_t)e[15 + i] << 8);
                if (c == 0) break;
                tmp[n++] = (c >= 0x80) ? '?' : (char)c;
            }
            for (int i = 0; i < 4 && n < 13; i += 2) {
                uint16_t c = e[28 + i] | ((uint16_t)e[29 + i] << 8);
                if (c == 0) break;
                tmp[n++] = (c >= 0x80) ? '?' : (char)c;
            }
            tmp[n] = 0;
            // записываем на своё место: seq 0x41 = последний фрагмент (индекс 0 в LFN)
            int pos = (seq & 0x0F) - 1;
            memcpy(lfn + pos * 13, tmp, n + 1);
            *lfn_len += n;
            // если это последний (0x40), сдвигаем в начало
            if (seq & 0x40) {
                memmove(lfn, lfn + pos * 13, n + 1);
                *lfn_len = n;
            }
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
    // Карта с MBR: FAT32 живёт в разделе 1 (обычно LBA 2048).
    // Сначала читаем MBR (сектор 0), находим начало раздела.
    uint32_t part_lba = 0;

    if (sd_read_sector(0, g_sector) >= 0) {
        if (le16(g_sector + 510) == 0x55AA && (g_sector[446 + 4] == 0x0B ||
                                                g_sector[446 + 4] == 0x0C ||
                                                g_sector[446 + 4] == 0x06)) {
            // тип: FAT32 / FAT32 LBA / FAT16
            part_lba = le32(g_sector + 446 + 8);   // LBA начала раздела
            g_part_lba = part_lba;
            if (part_lba > 0) uart_puts("fat: MBR part1 @ lba\n");
        }
    }

    if (sd_read_sector(part_lba, g_sector) < 0) return -1;

    // проверка сигнатуры FAT32
    if (le16(g_sector + 510) != 0x55AA) { uart_puts("fat: no 55AA\n"); return -1; }
    // BPB
    uint16_t bps = le16(g_sector + 11);
    if (bps != 512) { uart_puts("fat: bps!=512\n"); return -1; }

    g_sec_per_cluster = g_sector[13];
    g_reserved        = le16(g_sector + 14);
    g_num_fats        = g_sector[16];
    g_fat_size        = le32(g_sector + 36);
    g_root_cluster    = le32(g_sector + 44);

    if (!g_fat_size || g_fat_size == 0xFFFFFFFF) {
        // FAT16/FAT12?
        uart_puts("fat: not FAT32\n");
        return -1;
    }

    g_data_start = part_lba + g_reserved + g_num_fats * g_fat_size;

    uint32_t total_sectors = le32(g_sector + 32);
    g_total_clusters = (total_sectors - g_data_start) / g_sec_per_cluster;

    uart_puts("fat: ok\n");
    return 0;
}

// имя без ведущих пробелов (для сравнения)
static int name_eq(const char* a, const char* b) {
    while (*a == ' ' || *a == '\t') a++;
    while (*b == ' ' || *b == '\t') b++;
    return strcmp(a, b) == 0;
}

int fat_list(const char* dir, fat_entry_t* out, int max) {
    if (!dir || dir[0] == 0 || strcmp(dir, "/") == 0) {
        return read_dir(g_root_cluster, out, max);
    }
    // ищем подпапку с этим именем в корне
    fat_entry_t root[FAT_MAX_ENTRIES];
    int n = read_dir(g_root_cluster, root, FAT_MAX_ENTRIES);
    for (int i = 0; i < n; i++) {
        if (name_eq(root[i].name, dir)) {
            // читаем содержимое подпапки
            return read_dir(root[i].first_cluster, out, max);
        }
    }
    return 0;
}

int fat_find(const char* dir, const char* name, fat_entry_t* out) {
    fat_entry_t list[FAT_MAX_ENTRIES];
    int n = fat_list(dir, list, FAT_MAX_ENTRIES);
    for (int i = 0; i < n; i++) {
        if (name_eq(list[i].name, name)) {
            *out = list[i];
            return 1;
        }
    }
    return 0;
}

int fat_read_file(const fat_entry_t* f, uint32_t offset, uint8_t* buf, uint32_t len) {
    if (f->size == 0 || offset >= f->size) return 0;
    if (offset + len > f->size) len = f->size - offset;
    return read_chain(f->first_cluster, offset, buf, len);
}