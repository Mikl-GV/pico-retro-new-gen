// fat.c — минимальный FAT32-ридер/писатель (без кэша, PIO-чтение секторов).
// Поддерживает: корневую директорию и подпапки, 8.3 + LFN (UTF-16), чтение
// файлов кластерами.
//
// r788: блочный источник АБСТРАГИРОВАН (fat_set_block_source). По умолчанию —
// SD (sd_read/sd_write_sector); кастомный источник (USB-флешка) ставится из
// main.c. Все чтения идут через blk_read/blk_write — SD и USB неразличимы
// для FAT. Паттерн — как в эталоне Bare_linux/src/fat.c.
#include <string.h>
#include "fat.h"
#include "sd.h"
#include "uart.h"

extern int printf(const char* fmt, ...);

/* r835: пошаговая отладка записи FAT (wr1..wr4) выключена — при копировании
   папки она давала сотни строк в UART (115200) и заметно тормозила.
   Включить: FAT_DBG 1. */
#define FAT_DBG 0
#if FAT_DBG
extern int uart0_printf(const char* fmt, ...);
#define FATLOG(...) uart0_printf(__VA_ARGS__)
#else
#define FATLOG(...) do {} while (0)
#endif

// forward: создание записи директории, сравнение имён (определены ниже)
static void make_dir_entry(uint8_t* e, const char* name, uint32_t cluster, int is_dot);
static int name_eq(const char* a, const char* b);

// ---- Блочный источник (r788): SD по умолчанию, USB через fat_set_block_source ----
// blk_read/blk_write — единственные точки доступа FAT к носителю. Если
// кастомный источник не назначен (или дёрнут до main.c), читаем SD —
// g_blk_rd/g_blk_wr по умолчанию указывают на SD-обёртки, рекурсии нет.
static fat_blk_read_t  g_blk_rd = sd_read_sector;
static fat_blk_write_t g_blk_wr = sd_write_sector;

static int blk_read(uint32_t lba, void* buf) {
    return g_blk_rd ? g_blk_rd(lba, buf) : sd_read_sector(lba, buf);
}
static int blk_write(uint32_t lba, const void* buf) {
    return g_blk_wr ? g_blk_wr(lba, buf) : sd_write_sector(lba, buf);
}

void fat_set_block_source(fat_blk_read_t rd, fat_blk_write_t wr) {
    g_blk_rd = rd;
    g_blk_wr = wr;
}

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
static void wr32(uint8_t* p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

/* r828: подсказка свободного кластера. Было: find_free_cluster всегда
   начинал с кластера 2 и линейно перебирал FAT — при STOR на флешке 30 ГБ
   это десятки тысяч USB-чтений (запись 8 КБ шла секунды-десятки секунд).
   Теперь старт с последнего выделенного (FSInfo.next_free, как в Windows),
   полученное значение сохраняется обратно в FSInfo. */
static uint32_t g_free_hint = 2;
static uint32_t g_free_count = 0xFFFFFFFFu;
static uint32_t g_fsinfo_sec = 0;   /* номер сектора FSInfo внутри раздела */

/* Загрузить next_free/free_count из FSInfo (FAT32). Невалидный — остаёмся
   на дефолте (поиск с кластера 2). */
static void fsinfo_load(void) {
    if (!g_fsinfo_sec) return;
    uint8_t b[512];
    if (blk_read(g_part_lba + g_fsinfo_sec, b) < 0) return;
    if (le32(b) != 0x41615252u || le32(b + 484) != 0x61417272u) return;
    uint32_t nf = le32(b + 492);
    if (nf >= 2 && nf < 2 + g_total_clusters) g_free_hint = nf;
    g_free_count = le32(b + 488);
}

/* Сохранить next_free/free_count в FSInfo.
   r903: если сигнатуры FSInfo не сошлись (свежая разметка / битый FSInfo) —
   инициализируем сектор штатно (как mkfs.fat: lead/trail сигнатуры,
   free_count, next_free, 0x55AA). Сектор зарезервирован BPB под FSInfo,
   перезапись безопасна; без этого подсказка свободного кластера терялась
   при каждом перезапуске (линейный поиск с кластера 2). */
static void fsinfo_store(void) {
    if (!g_fsinfo_sec) return;
    uint8_t b[512];
    uint32_t sec = g_part_lba + g_fsinfo_sec;
    if (blk_read(sec, b) < 0) return;
    if (le32(b) != 0x41615252u || le32(b + 484) != 0x61417272u) {
        memset(b, 0, sizeof(b));
        wr32(b, 0x41615252u);
        wr32(b + 484, 0x61417272u);
        wr32(b + 488, 0xFFFFFFFFu);
        wr32(b + 492, g_free_hint);
        b[510] = 0x55; b[511] = 0xAA;
    }
    wr32(b + 488, g_free_count);
    wr32(b + 492, g_free_hint);
    blk_write(sec, b);
}

// ---- кластер -> сектор (с учётом сдвига раздела) ----
static uint32_t cluster_to_sector(uint32_t cl) {
    return g_data_start + (cl - 2) * g_sec_per_cluster;
}

// ---- чтение следующего кластера по FAT ----
/* r827: кэш ОДНОГО сектора FAT. Было: find_free_cluster/fat_next_cluster
   читали сектор FAT через USB на КАЖДЫЙ кластер (128 кластеров на сектор ->
   128 одинаковых USB-чтений подряд). На флешке 1.87 млн кластеров поиск
   свободного кластера при STOR растягивался на минуты и выглядел как
   зависание прошивки. Кэш сбрасывается после записи в FAT (fat_set_cluster). */
static uint32_t fat_cache_sec = 0xFFFFFFFFu;
static uint8_t  fat_cache[512];

static int fat_sector_cached(uint32_t sec) {
    if (sec == fat_cache_sec) return 0;
    if (blk_read(sec, fat_cache) < 0) { fat_cache_sec = 0xFFFFFFFFu; return -1; }
    fat_cache_sec = sec;
    return 0;
}

static uint32_t fat_next_cluster(uint32_t cl) {
    // P30: кластеры начинаются с 2; cl<2 = служебные записи FAT (0/1) —
    // не читаем их как кластеры, сразу обрыв цепочки.
    // r853: и не выходим за пределы тома — иначе битая запись каталога даёт
    // чтение/запись произвольного сектора (cl*4 переполняется).
    if (cl < 2 || cl >= 2 + g_total_clusters) return 0x0FFFFFFF;
    uint32_t fat_off = cl * 4;
    uint32_t sec = g_part_lba + g_reserved + (fat_off / 512);
    if (fat_sector_cached(sec) < 0) return 0x0FFFFFFF;
    uint32_t v = le32(fat_cache + (fat_off % 512)) & 0x0FFFFFFF;
    return v;
}

// r0.417 (H1 аудита): верхняя граница итераций цепочки кластеров —
// защита от бесконечного обхода при цикле в цепочке на битой FAT.
static uint32_t chain_guard_limit(void) {
    // r853: цепочка не может быть длиннее числа кластеров тома. Прежний
    // минимум 100000 на битой FAT давал минуты чтения секторов.
    return (g_total_clusters > 2) ? g_total_clusters : 2;
}

// ---- чтение по байтам из цепочки кластеров ----
static uint32_t read_chain(uint32_t cl, uint32_t offset, uint8_t* buf, uint32_t len) {
    if (g_sec_per_cluster == 0) return 0;
    if (cl < 2 || cl >= 2 + g_total_clusters) return 0;   /* r853: кластер вне тома */
    uint32_t cl_size = g_sec_per_cluster * 512;
    uint32_t skip = offset;
    uint32_t guard = 0;
    uint32_t clim = chain_guard_limit();
    while (skip >= cl_size) {
        if (guard++ >= clim) return 0;
        cl = fat_next_cluster(cl);
        if (cl >= 0x0FFFFFF8) return 0;
        skip -= cl_size;
    }
    uint32_t done = 0;
    guard = 0;
    while (done < len && guard++ < clim) {
        if (cl == 0 || cl >= 0x0FFFFFF8) break;
        uint32_t sec_off = cluster_to_sector(cl) + skip / 512;
        uint32_t in_sec  = skip % 512;
        uint32_t avail   = cl_size - skip;
        uint32_t want    = len - done;
        if (want > avail) want = avail;
        while (want > 0) {
            if (blk_read(sec_off, g_sector) < 0) return done;
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
    out->attr = short_attr;      // r818: сохраняем атрибут (0x10 = каталог)
    lfn[0] = 0;
    *lfn_len = 0;
    return 1;
}

// ---- чтение директории (кластеры) ----
static int read_dir(uint32_t cl, fat_entry_t* out, int max) {
    if (cl < 2 || cl >= 2 + g_total_clusters) return 0;   /* r853: кластер вне тома */
    int count = 0;
    char lfn[FAT_NAME_LEN] = {0};
    int lfn_len = 0;
    uint32_t guard = 0;
    uint32_t clim = chain_guard_limit();

    while (cl && cl < 0x0FFFFFF8 && guard++ < clim) {
        uint32_t sec = cluster_to_sector(cl);
        for (uint32_t s = 0; s < g_sec_per_cluster; s++) {
            if (blk_read(sec + s, g_sector) < 0) return count;
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

    if (blk_read(0, g_sector) < 0) return -1;
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
        if (blk_read(1, gpthdr) == 0 &&
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
            // r0.417 (M7 аудита): запись читается до en[39] (48-бит LBA на
            // offset 32). При битом GPT с esize>473 чтение выходит за конец
            // стекового буфера tbl[512] — обрываем цикл.
            if ((uint32_t)off + 40 > 512) break;
            if (off == 0) {
                if (blk_read(sec, tbl) < 0) break;
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
        if (blk_read(lba, g_sector) < 0) continue;
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
    if (blk_read(part_lba, g_sector) < 0) return -1;
    if (le16(g_sector + 510) != 0xAA55) return -1;
    if (le16(g_sector + 11) != 512) return -1;

    g_part_lba      = part_lba;
    g_sec_per_cluster = g_sector[13];
    g_reserved        = le16(g_sector + 14);
    g_num_fats        = g_sector[16];
    g_fat_size        = le32(g_sector + 36);
    g_root_cluster    = le32(g_sector + 44);

    if (!g_fat_size || g_fat_size == 0xFFFFFFFF) return -1;
    // r0.390 (S3): битый BPB (sec/clu=0) или data_start за пределами тома
    // → деление на 0 / unsigned-underflow → Data Abort. Отвергаем раздел.
    if (g_sec_per_cluster == 0) return -1;
    if (g_num_fats == 0) return -1;

    g_data_start = part_lba + g_reserved + g_num_fats * g_fat_size;
    uint32_t total_sectors = le32(g_sector + 32);
    if (g_data_start >= total_sectors) return -1;
    g_total_clusters = (total_sectors - g_data_start) / g_sec_per_cluster;
    /* r853: границы FAT32 по спецификации: кластер ≤ 128 секторов (64 КиБ),
       минимум два кластера, корневой кластер внутри тома. */
    if (g_sec_per_cluster > 128) return -1;
    if (g_total_clusters < 2) return -1;
    if (g_root_cluster < 2 || g_root_cluster >= 2 + g_total_clusters) return -1;

    /* r828: FSInfo — подсказка next_free для быстрого поиска свободного
       кластера (без неё запись начинала линейный обход с кластера 2). */
    g_fsinfo_sec = le16(g_sector + 48);
    if (g_fsinfo_sec == 0 || g_fsinfo_sec == 0xFFFF) g_fsinfo_sec = 1;
    fsinfo_load();

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
    /* r827: чтение сектора FAT через кэш. r828: старт с подсказки FSInfo —
       первый же свободный кластер обычно рядом с последним выделенным. */
    uint32_t start = g_free_hint;
    if (start < 2 || start >= 2 + g_total_clusters) start = 2;
    for (uint32_t pass = 0; pass < 2; pass++) {
        uint32_t from = (pass == 0) ? start : 2;
        uint32_t to   = (pass == 0) ? (2 + g_total_clusters) : start;
        for (uint32_t cl = from; cl < to; cl++) {
            uint32_t fat_off = cl * 4;
            uint32_t sec = g_part_lba + g_reserved + (fat_off / 512);
            if (fat_sector_cached(sec) < 0) return 0;
            uint32_t v = le32(fat_cache + (fat_off % 512)) & 0x0FFFFFFF;
            if (v == 0) {
                g_free_hint = cl + 1;
                if (g_free_count != 0 && g_free_count != 0xFFFFFFFFu) g_free_count--;
                return cl;
            }
        }
    }
    return 0;
}

// записать значение в FAT (обе копии)
static int fat_set_cluster(uint32_t cl, uint32_t val) {
    if (cl < 2 || cl >= 2 + g_total_clusters) return -1;   /* r853: за пределы FAT */
    uint32_t fat_off = cl * 4;
    uint32_t sec = g_part_lba + g_reserved + (fat_off / 512);
    uint32_t off = fat_off % 512;
    for (uint32_t copy = 0; copy < g_num_fats; copy++) {
        uint32_t s = sec + copy * g_fat_size;
        if (blk_read(s, g_sector) < 0) return -1;
        uint32_t v = le32(g_sector + off) & 0xF0000000; // сохранить старшие 4 бита
        v |= (val & 0x0FFFFFFF);
        g_sector[off] = v & 0xFF;
        g_sector[off+1] = (v >> 8) & 0xFF;
        g_sector[off+2] = (v >> 16) & 0xFF;
        g_sector[off+3] = (v >> 24) & 0xFF;
        if (blk_write(s, g_sector) < 0) return -1;
    }
    /* r827: FAT изменена — кэш сектора устарел. */
    fat_cache_sec = 0xFFFFFFFFu;
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
    if (cl < 2 || cl >= 2 + g_total_clusters) return -1;   /* r853 */
    uint32_t base = cluster_to_sector(cl);
    for (uint32_t s = 0; s < g_sec_per_cluster; s++) {
        if (blk_read(base + s, g_sector) < 0) return -1;
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
    if (cl < 2 || cl >= 2 + g_total_clusters) return -1;   /* r853 */
    uint32_t guard = 0;
    uint32_t clim = chain_guard_limit();
    while (cl && cl < 0x0FFFFFF8 && guard++ < clim) {
        uint32_t base = cluster_to_sector(cl);
        for (uint32_t s = 0; s < g_sec_per_cluster; s++) {
            if (blk_read(base + s, g_sector) < 0) return -1;
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
    if (blk_write(new_sec, zero) < 0) return -1;
    // остальные секторы кластера — ЧИСТЫЕ нули. Раньше сюда писался тот же
    // `zero` с ".", "..", из-за чего каждый сектор кластера выглядел как
    // начало директории и на ПК/в парсерах появлялись мусорные записи.
    uint8_t empty[512];
    memset(empty, 0, 512);
    for (uint32_t s = 1; s < g_sec_per_cluster; s++) {
        if (blk_write(new_sec + s, empty) < 0) return -1;
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
    if (blk_read(dummy_sec, g_sector) < 0) return -1;

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
    if (blk_write(dummy_sec, g_sector) < 0) return -1;

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
    uint32_t walk_guard = 0;
    uint32_t clim = chain_guard_limit();

    while (cl && cl < 0x0FFFFFF8 && !found && walk_guard++ < clim) {
        uint32_t base = cluster_to_sector(cl);
        for (uint32_t s = 0; s < g_sec_per_cluster && !found; s++) {
            if (blk_read(base + s, g_sector) < 0) return -1;
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
                    /* r831: собираем 8.3 ТАК ЖЕ, как read_dir (LIST): пробелы
                       внутри имени сохраняются, обрезаются только хвостовые.
                       Раньше здесь стоял обрыв на первом пробеле: файл
                       "ARCADE A.A26" (из длинного имени) показывался в LIST
                       как "ARCADE A.A26", а DELE искал "ARCADE.A26" ->
                       не находил -> 550 delete fail. */
                    char n8[9], x3[4];
                    memcpy(n8, g_sector + i, 8); n8[8] = 0;
                    memcpy(x3, g_sector + i + 8, 3); x3[3] = 0;
                    for (int k = 7; k >= 0 && n8[k] == ' '; k--) n8[k] = 0;
                    for (int k = 2; k >= 0 && x3[k] == ' '; k--) x3[k] = 0;
                    int pi = 0;
                    for (int k = 0; n8[k] && pi < 12; k++) full[pi++] = n8[k];
                    if (attr != 0x10 && x3[0]) {
                        full[pi++] = '.';
                        for (int k = 0; x3[k] && pi < 12; k++) full[pi++] = x3[k];
                    }
                    full[pi] = 0;
                    flen = pi;
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
        if (blk_read(lfn_lba[k], g_sector) < 0) continue;
        g_sector[lfn_off[k]] = 0xE5;
        if (blk_write(lfn_lba[k], g_sector) < 0) return -1;
    }

    // Теперь пометить 0xE5 саму 8.3 запись (уже после LFN, чтобы не перезатереть)
    if (blk_read(entry_sec, g_sector) < 0) return -1;
    g_sector[entry_off] = 0xE5;
    if (blk_write(entry_sec, g_sector) < 0) return -1;

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
        out->attr = 0x10;            // r818: корень — каталог
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

// Записать файл в существующую директорию (dir_path — "/" или "/roms/...", name — имя файла).
// Если файл с таким именем уже есть — перезаписывает (удаляет старую запись, берёт свежие кластеры).
// Данные пишутся байтами в цепочку кластеров (размер округляется до кластера, остаток — 0xFF? нет, нулями).
// Возвращает: len при успехе, -1 при ошибке. Имя должно быть 8.3-совместимым (без пробелов, <=8 символов + опц. расширение <=3).
int fat_write_file(const char* dir_path, const char* name, const uint8_t* data, uint32_t len) {
    if (!dir_path || !name || !name[0]) return -1;
    if (!data && len) return -1;
    /* r829: пустой файл (len==0) теперь СОЗДАЁТСЯ: запись каталога с size=0
       и first_cluster=0. Раньше возвращали успех, но файла на диске не было —
       FTP отвечал 426 (клиент не мог сохранить 0-байтный файл). */
    /* r826: пошаговая диагностика записи — STOR виснет после приёма данных,
       нужно видеть, на каком этапе (поиск каталога / удаление старого /
       выделение кластеров / данные / запись dirent). */
    FATLOG("  fat: wr1 start len=%u\r\n", len);

    // 1. Найти родительскую директорию
    char pbuf[FAT_NAME_LEN];
    fat_entry_t parent;
    if (!path_lookup(dir_path, &parent, pbuf, FAT_NAME_LEN)) return -1;
    if (parent.size != 0) return -1;
    FATLOG("  fat: wr1a dir ok cl=%u\r\n", parent.first_cluster);

    // 2. Если файл уже есть — удалить (освободить старые кластеры и запись)
    fat_entry_t old;
    if (fat_find(dir_path, name, &old)) {
        FATLOG("  fat: wr1b delete old size=%u\r\n", old.size);
        fat_delete_file(dir_path, name);
    }
    FATLOG("  fat: wr1c alloc %u cl\r\n", (len + (g_sec_per_cluster*512) - 1) / (g_sec_per_cluster*512));

    // 3. Выделить цепочку кластеров (для пустого файла — ни одного)
    uint32_t cl_size = g_sec_per_cluster * 512;
    uint32_t need_cl = (len + cl_size - 1) / cl_size;   /* len==0 -> 0 */

    uint32_t first_cl = 0, prev_cl = 0;
    for (uint32_t i = 0; i < need_cl; i++) {
        uint32_t cl = find_free_cluster();
        if (cl == 0) { if (first_cl) fat_set_cluster(prev_cl, 0x0FFFFFFF); return -1; }
        if (!first_cl) first_cl = cl;
        if (i > 0) fat_set_cluster(prev_cl, cl);   // prev -> next
        prev_cl = cl;
    }
    if (need_cl) {
        fat_set_cluster(prev_cl, 0x0FFFFFFF);      // конец цепочки
        fsinfo_store();                            /* r828: сохранить next_free */
    }
    FATLOG("  fat: wr2 cl first=%u need=%u\r\n", first_cl, need_cl);

    // 4. Записать данные по кластерам
    uint32_t pos = 0;
    uint32_t cl = first_cl;
    uint8_t buf[512];
    while (pos < len && cl >= 2 && cl < 0x0FFFFFF8) {
        uint32_t sec = cluster_to_sector(cl);
        for (uint32_t s = 0; s < g_sec_per_cluster && pos < len; s++) {
            uint32_t chunk = len - pos;
            if (chunk > 512) chunk = 512;
            if (chunk < 512) {
                // последний сектор — читаем, правим хвост, пишем (сохранить остаток сектора)
                if (blk_read(sec + s, buf) < 0) return -1;
            }
            memcpy(buf, data + pos, chunk);
            if (blk_write(sec + s, buf) < 0) return -1;
            pos += chunk;
        }
        cl = fat_next_cluster(cl);
    }
    FATLOG("  fat: wr3 data ok pos=%u\r\n", pos);

    // 5. Запись в директорию (спрашиваем свободную запись: 1 = 8.3, без LFN)
    uint32_t dentry_sec; int dentry_off;
    if (find_free_entry(parent.first_cluster, &dentry_sec, &dentry_off) < 0) return -1;
    if (blk_read(dentry_sec, g_sector) < 0) return -1;
    FATLOG("  fat: wr4 dentry sec=%u off=%d\r\n", dentry_sec, dentry_off);

    // 8.3 имя: до 8 символов + '.' + до 3 расширение, в верхнем регистре
    uint8_t short83[11];
    memset(short83, ' ', 11);
    const char* dot = 0;
    for (const char* p = name; *p; p++) if (*p == '.') dot = p;
    int nlen = dot ? (int)(dot - name) : (int)strlen(name);
    int xlen = dot ? (int)strlen(dot + 1) : 0;
    if (nlen > 8) nlen = 8;
    if (xlen > 3) xlen = 3;
    for (int i = 0; i < nlen; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        /* r831: в 8.3 недопустимы пробел, точка, скобки и т.п. (пробел — только
           padding в конце). Заменяем на '_', иначе получались имена с пробелом
           внутри («ARCADE A»), и удаление/поиск расходились с LIST. */
        int ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 (c == '$' || c == '%' || c == '\'' || c == '-' || c == '_' ||
                  c == '@' || c == '~' || c == '`' || c == '!' || c == '#' ||
                  c == '&' || c == '(' || c == ')' || c == '{' || c == '}' ||
                  c == '^');
        short83[i] = (uint8_t)(ok ? c : '_');
    }
    for (int i = 0; i < xlen; i++) {
        char c = dot[i + 1];
        if (c >= 'a' && c <= 'z') c -= 32;
        int ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 (c == '$' || c == '%' || c == '\'' || c == '-' || c == '_' ||
                  c == '@' || c == '~' || c == '`' || c == '!' || c == '#' ||
                  c == '&' || c == '(' || c == ')' || c == '{' || c == '}' ||
                  c == '^');
        short83[8 + i] = (uint8_t)(ok ? c : '_');
    }
    // атрибут: архивный (0x20) — файл
    g_sector[dentry_off + 11] = 0x20;
    // кластер
    g_sector[dentry_off + 20] = (first_cl >> 16) & 0xFF;
    g_sector[dentry_off + 21] = (first_cl >> 24) & 0xFF;
    g_sector[dentry_off + 26] = first_cl & 0xFF;
    g_sector[dentry_off + 27] = (first_cl >> 8) & 0xFF;
    // размер
    g_sector[dentry_off + 28] = len & 0xFF;
    g_sector[dentry_off + 29] = (len >> 8) & 0xFF;
    g_sector[dentry_off + 30] = (len >> 16) & 0xFF;
    g_sector[dentry_off + 31] = (len >> 24) & 0xFF;
    memcpy(g_sector + dentry_off, short83, 11);

    if (blk_write(dentry_sec, g_sector) < 0) return -1;

    return (int)len;
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

/* ---- r832: ПОТОКОВАЯ запись файла (FTP STOR больших файлов) ----------
   Раньше STOR копил файл в g_stor[64 КБ] и писал целиком: файлы больше
   64 КБ молча обрезались (в логе "STOR OVERFLOW ... total=64564",
   файл 128 КБ записывался как 64 КБ). Теперь принятые сегменты пишутся
   в кластеры сразу, кластеры выделяются по мере заполнения. */

/* Символ, допустимый в 8.3 (остальные -> '_'). */
static char conv83(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    int ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             (c == '$' || c == '%' || c == '\'' || c == '-' || c == '_' ||
              c == '@' || c == '~' || c == '`' || c == '!' || c == '#' ||
              c == '&' || c == '(' || c == ')' || c == '{' || c == '}' ||
              c == '^');
    return ok ? c : '_';
}

/* r836: нужно ли писать LFN для этого имени (длинное или с символами,
   которых нет в 8.3 — строчные буквы, пробел, скобки, '[' и т.п.). */
static int name_needs_lfn(const char* name) {
    int n = (int)strlen(name);
    if (n > 12) return 1;
    const char* dot = 0;
    for (const char* p = name; *p; p++) if (*p == '.') dot = p;
    int nlen = dot ? (int)(dot - name) : n;
    int xlen = dot ? (int)strlen(dot + 1) : 0;
    if (nlen > 8 || xlen > 3) return 1;
    for (int i = 0; i < n; i++) {
        char c = name[i];
        if (c == '.') continue;
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '$' || c == '%' || c == '\'' || c == '-' || c == '_' ||
              c == '@' || c == '~' || c == '`' || c == '!' || c == '#' ||
              c == '&' || c == '(' || c == ')' || c == '{' || c == '}' ||
              c == '^')) return 1;
    }
    return 0;
}

/* r836: 8.3-имя файла; при need_tilde основа усекается до 6 символов и
   добавляется "~1" (псевдоним для LFN). */
static void build_short_file(const char* name, uint8_t* short83, int need_tilde) {
    memset(short83, ' ', 11);
    const char* dot = 0;
    for (const char* p = name; *p; p++) if (*p == '.') dot = p;
    int nlen = dot ? (int)(dot - name) : (int)strlen(name);
    int xlen = dot ? (int)strlen(dot + 1) : 0;
    if (xlen > 3) xlen = 3;
    for (int i = 0; i < xlen; i++) short83[8 + i] = (uint8_t)conv83(dot[i + 1]);
    int keep = need_tilde ? (nlen > 6 ? 6 : nlen) : (nlen > 8 ? 8 : nlen);
    for (int i = 0; i < keep; i++) short83[i] = (uint8_t)conv83(name[i]);
    if (need_tilde) { short83[6] = '~'; short83[7] = '1'; }
}

static void build_83(const char* name, uint8_t* short83) {
    memset(short83, ' ', 11);
    const char* dot = 0;
    for (const char* p = name; *p; p++) if (*p == '.') dot = p;
    int nlen = dot ? (int)(dot - name) : (int)strlen(name);
    int xlen = dot ? (int)strlen(dot + 1) : 0;
    if (nlen > 8) nlen = 8;
    if (xlen > 3) xlen = 3;
    for (int i = 0; i < nlen; i++) short83[i] = (uint8_t)conv83(name[i]);
    for (int i = 0; i < xlen; i++) short83[8 + i] = (uint8_t)conv83(dot[i + 1]);
}

static struct {
    int      active;
    uint32_t dentry_sec;
    int      dentry_off;
    uint32_t first_cluster;
    uint32_t last_cluster;
    uint32_t cur_cluster;   /* 0 = нужно выделить следующий */
    uint32_t size;
    uint32_t cl_off;        /* смещение внутри текущего кластера */
    uint8_t  secbuf[512];
    uint32_t sec_no;
    int      sec_dirty;
} g_wr;

static int wr_flush(void) {
    if (!g_wr.sec_dirty) return 0;
    if (blk_write(g_wr.sec_no, g_wr.secbuf) < 0) return -1;
    g_wr.sec_dirty = 0;
    return 0;
}

int fat_write_begin(const char* dir_path, const char* name) {
    /* r833: если предыдущая потоковая запись не закрылась (обрыв связи),
       фиксируем её и продолжаем — иначе все последующие STOR получали 550. */
    if (g_wr.active) fat_write_end();
    if (!dir_path || !name || !name[0]) return -1;

    fat_entry_t parent;
    char pbuf[FAT_NAME_LEN];
    if (!path_lookup(dir_path, &parent, pbuf, FAT_NAME_LEN)) return -1;
    if (parent.size != 0) return -1;

    fat_entry_t old;
    if (fat_find(dir_path, name, &old)) fat_delete_file(dir_path, name);

    uint32_t dsec; int doff;
    /* r836: LFN — длинные имена (и имена с недопустимыми для 8.3 символами)
       сохраняются полностью: перед 8.3-записью пишутся LFN-фрагменты, а
       8.3 получает псевдоним с '~1'. Раньше имя обрезалось до 8.3
       («COMMANDO_.A78»), и клиент не мог вернуть исходное имя. */
    int need_lfn = name_needs_lfn(name);
    uint8_t s83[11];
    build_short_file(name, s83, need_lfn);
    int lfn_count = need_lfn ? (int)((strlen(name) + 12) / 13) : 0;
    if (lfn_count > 15) lfn_count = 15;       /* 16 записей в сектор */

    if (lfn_count > 0) {
        if (find_free_entries(parent.first_cluster, lfn_count + 1, &dsec, &doff) < 0) return -1;
    } else {
        if (find_free_entry(parent.first_cluster, &dsec, &doff) < 0) return -1;
    }
    if (blk_read(dsec, g_sector) < 0) return -1;

    if (lfn_count > 0) {
        uint8_t chk = lfn_checksum(s83);
        int off = doff;
        /* Фрагменты на диске идут от последнего (seq|0x40) к первому,
           непосредственно ПЕРЕД 8.3-записью. */
        for (int i = lfn_count - 1; i >= 0; i--) {
            int seq = i + 1;
            if (i == lfn_count - 1) seq |= 0x40;
            make_lfn_fragment(g_sector + off, seq, name, i * 13, chk);
            off += 32;
        }
        doff = off;
    }
    memcpy(g_sector + doff, s83, 11);
    g_sector[doff + 11] = 0x20;               /* архивный файл */
    for (int k = 12; k < 32; k++) g_sector[doff + k] = 0;
    if (blk_write(dsec, g_sector) < 0) return -1;

    memset(&g_wr, 0, sizeof(g_wr));
    g_wr.active = 1;
    g_wr.dentry_sec = dsec;
    g_wr.dentry_off = doff;
    return 0;
}

int fat_write_data(const uint8_t* d, uint32_t n) {
    if (!g_wr.active) return -1;
    uint32_t cl_size = g_sec_per_cluster * 512;
    if (cl_size == 0) return -1;
    while (n) {
        if (g_wr.cur_cluster == 0) {
            uint32_t cl = find_free_cluster();
            if (cl == 0) return -1;
            if (g_wr.first_cluster == 0) g_wr.first_cluster = cl;
            else fat_set_cluster(g_wr.last_cluster, cl);
            g_wr.last_cluster = cl;
            g_wr.cur_cluster = cl;
            g_wr.cl_off = 0;
        }
        uint32_t sec_in_cl = g_wr.cl_off / 512;
        uint32_t in_sec = g_wr.cl_off % 512;
        uint32_t abs_sec = cluster_to_sector(g_wr.cur_cluster) + sec_in_cl;
        uint32_t take = 512 - in_sec;
        if (take > n) take = n;
        /* r833: полный сектор пишем напрямую — без blk_read ради хвоста.
           Раньше каждый сектор читался+писался (2 USB-операции на 512 б),
           из-за чего приём STOR проседал и клиент рвал канал. */
        if (in_sec == 0 && take == 512) {
            if (wr_flush() < 0) return -1;
            memcpy(g_wr.secbuf, d, 512);
            if (blk_write(abs_sec, g_wr.secbuf) < 0) return -1;
            g_wr.sec_no = abs_sec;
            g_wr.sec_dirty = 0;
        } else {
            if (!g_wr.sec_dirty || g_wr.sec_no != abs_sec) {
                if (wr_flush() < 0) return -1;
                if (blk_read(abs_sec, g_wr.secbuf) < 0) return -1;
                g_wr.sec_no = abs_sec;
            }
            memcpy(g_wr.secbuf + in_sec, d, take);
            g_wr.sec_dirty = 1;
        }
        d += take; n -= take;
        g_wr.cl_off += take;
        g_wr.size += take;
        if (g_wr.cl_off >= cl_size) {
            if (wr_flush() < 0) return -1;
            g_wr.cur_cluster = 0;
        }
    }
    return 0;
}

int fat_write_end(void) {
    if (!g_wr.active) return -1;
    g_wr.active = 0;
    if (wr_flush() < 0) return -1;
    if (g_wr.last_cluster) {
        fat_set_cluster(g_wr.last_cluster, 0x0FFFFFFF);   /* конец цепочки */
        fsinfo_store();
    }
    if (blk_read(g_wr.dentry_sec, g_sector) < 0) return -1;
    int off = g_wr.dentry_off;
    uint32_t fc = g_wr.first_cluster, sz = g_wr.size;
    g_sector[off + 20] = (uint8_t)((fc >> 16) & 0xFF);
    g_sector[off + 21] = (uint8_t)((fc >> 24) & 0xFF);
    g_sector[off + 26] = (uint8_t)(fc & 0xFF);
    g_sector[off + 27] = (uint8_t)((fc >> 8) & 0xFF);
    g_sector[off + 28] = (uint8_t)(sz & 0xFF);
    g_sector[off + 29] = (uint8_t)((sz >> 8) & 0xFF);
    g_sector[off + 30] = (uint8_t)((sz >> 16) & 0xFF);
    g_sector[off + 31] = (uint8_t)((sz >> 24) & 0xFF);
    if (blk_write(g_wr.dentry_sec, g_sector) < 0) return -1;
    return (int)g_wr.size;
}

void fat_abort_write(void) {
    /* r845: неудачная потоковая запись — помечаем запись каталога как
       удалённую (0xE5). Кластеры в FAT остаются несвязанными (leak, но
       файл не «битый»); FAT32 это переживёт, а частичного файла на диске
       не остаётся. */
    if (!g_wr.active) return;
    g_wr.active = 0;
    if (g_wr.dentry_sec) {
        if (blk_read(g_wr.dentry_sec, g_sector) < 0) {
            memset(&g_wr, 0, sizeof(g_wr));
            return;
        }
        g_sector[g_wr.dentry_off] = 0xE5;
        blk_write(g_wr.dentry_sec, g_sector);
    }
    memset(&g_wr, 0, sizeof(g_wr));
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
    // r0.365 (H4 аудита): read_chain при ошибке SD возвращает «сколько успели»
    // без признака обрыва — недопрочитанный файл выглядел как успех. Теперь
    // неполное чтение — ошибка: -1, вызывающие могут не запускать ROM.
    int r = read_chain(f->first_cluster, offset, buf, len);
    return (r == (int)len) ? r : -1;
}