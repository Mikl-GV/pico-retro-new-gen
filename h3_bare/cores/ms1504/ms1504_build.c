/* ms1504_build.c — сборка загрузочного образа МС1504 из папки игры в RAM.
 *
 * МС1504 — PC/XT-совместимый (Fake86, КР1834ВМ86). Игра — это папка с .exe
 * и файлами данных (напр. /roms/ms1504/grandprix/), которую реальная машина
 * грузит с загрузочной дискеты MS-DOS. Запуск требует собрать в памяти
 * образ диска:
 *   * малые игры (вмещаются в 720К) — FAT12-дискета на базе вшитой системной
 *     дискеты mc1504BOOT.IMA (TBIOS/TDOS/COMMAND) + файлы папки + свой
 *     AUTOEXEC.BAT;
 *   * большие игры (civrus 2.5 МБ, pacific_war 1.3 МБ) — FAT16-образ HDD
 *     (диск 0x80), на дискету A: ставятся системные файлы, игра копируется
 *     на C:, AUTOEXEC.BAT на A: переходит на C: и запускает главный файл.
 *
 * Буфер — пул аркад `.noinit_fbn` (0x54000000, 96 МБ). МС1504 и аркады
 * параллельно не запускаются (владелец времени один), поэтому перезанимать
 * пул безопасно. Готовый образ отдаётся hostfs через g_ms1504_active_img
 * (см. ms1504_shim.c) — шлюз обычных read/seek поверх RAM-буфера.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fat.h"

/* ---- буфер в пуле аркад (.noinit_fbn, владелец — МС1504) ---- */
#define M15_IMG_BASE     0x54000000u   /* начало (.fs_bss кончается здесь) */
#define M15_FLOPPY_SIZE  737280u       /* 720К FAT12 системная дискета */
#define M15_HDD_SIZE     (4u*1024u*1024u) /* до 4 МБ (civrus 2.5 МБ) */
#define M15_HDD_BASE     (M15_IMG_BASE + M15_FLOPPY_SIZE)

/* Активный образ (floppy или HDD), который отдаёт hostfs. Заполняет shim. */
extern const uint8_t* g_ms1504_active_img;
extern uint32_t       g_ms1504_active_img_len;

/* HDD-образ (диск 0x80) для больших игр. Определяем здесь, читает shim. */
const uint8_t* g_ms1504_hdd_img   = 0;
uint32_t       g_ms1504_hdd_img_len = 0;

/* Смонтирован ли HDD-образ (для host: монтировать ли диск 0x80). */
int m15_hdd_present(void)
{
    return g_ms1504_hdd_img && g_ms1504_hdd_img_len ? 1 : 0;
}

/* Вшитая системная дискета MS-DOS (ms1504_boot_ima.c). */
extern const unsigned char ms1504_boot_ima[];
extern const unsigned int  ms1504_boot_ima_len;

/* ---- FAT12-геометрия системного диска (mc1504BOOT.IMA, 720К) ----
 * BPB сектора 0: bps=512, spc=2, resv=1, nFAT=2, root=112, tot=1440, spf=3.
 * Свободно ~665 КБ в 712 кластерах, ~98 свободных корневых слотов. */
enum {
    M15_BPS     = 512,
    M15_SPC     = 2,       /* секторов в кластере */
    M15_RESV    = 1,
    M15_NFAT    = 2,
    M15_ROOTENT = 112,
    M15_SPF     = 3,
    M15_TOTSEC  = 1440,
    M15_ROOTSEC = (M15_ROOTENT * 32) / M15_BPS,   /* 7 */
    M15_FAT_OFF = M15_RESV * M15_BPS,
    M15_ROOT_OFF= (M15_RESV + M15_NFAT * M15_SPF) * M15_BPS,
    M15_DATA_OFF= (M15_RESV + M15_NFAT * M15_SPF + M15_ROOTSEC) * M15_BPS,
    M15_DATA_SEC= M15_TOTSEC - (M15_RESV + M15_NFAT * M15_SPF + M15_ROOTSEC),
    M15_CLUSTERS= M15_DATA_SEC / M15_SPC,         /* 712 */
    M15_CLUSZ   = M15_SPC * M15_BPS               /* 1024 */
};

/* ---- имя 8.3 из длинного имени SD (фатальные для DOS) ----
 * Старшие имена игр уже короткие (GPCGA.EXE, CAR1.PCS, HELI.EXE); для
 * длинных (old-games.nfo) усекаем: базовое до 8, нижний регистр → верхний. */
static void m15_name83(const char* sd, char* out)   /* out >= 11 */
{
    const char* dot = 0;
    const char* p;
    for (p = sd; *p; p++)
        if (*p == '.') dot = p;
    int base_len = dot ? (int)(dot - sd) : (int)strlen(sd);
    if (base_len > 8) base_len = 8;
    int ext_len = dot ? (int)strlen(dot + 1) : 0;
    if (ext_len > 3) ext_len = 3;
    char b[12];
    for (int i = 0; i < 8; i++)
        b[i] = (i < base_len && sd[i]) ? (char)((sd[i] >= 'a' && sd[i] <= 'z') ? sd[i] - 32 : sd[i]) : ' ';
    for (int i = 0; i < 3; i++)
        b[8 + i] = (dot && i < ext_len) ? (char)((dot[1 + i] >= 'a' && dot[1 + i] <= 'z') ? dot[1 + i] - 32 : dot[1 + i]) : ' ';
    memcpy(out, b, 11);
}

/* ---- работа с FAT12 в RAM-образе ----
 * base — указатель на образ (floppy 720К). geom фиксирована (M15_*). */

/* Прочитать запись FAT для кластера (12 бит). */
static uint32_t m15_fat_get(const uint8_t* fat, uint16_t clu)
{
    uint32_t o = (uint32_t)clu + (clu >> 1);   /* clu*1.5 */
    uint16_t w = (uint16_t)(fat[o] | ((uint16_t)fat[o + 1] << 8));
    if (clu & 1) return (w >> 4) & 0xFFF;
    return w & 0xFFF;
}

static void m15_fat_set(uint8_t* fat, uint16_t clu, uint32_t val)
{
    uint32_t o = (uint32_t)clu + (clu >> 1);
    uint16_t w = (uint16_t)(fat[o] | ((uint16_t)fat[o + 1] << 8));
    if (clu & 1) { w = (uint16_t)((w & 0x000F) | ((val & 0xFFF) << 4)); }
    else         { w = (uint16_t)((w & 0xF000) | (val & 0xFFF)); }
    fat[o] = (uint8_t)w;
    fat[o + 1] = (uint8_t)(w >> 8);
}

/* Найти первый свободный кластер, начиная с from. */
static int m15_fat_next_free(const uint8_t* fat, int from)
{
    for (int c = from; c < M15_CLUSTERS + 2; c++)
        if (m15_fat_get(fat, (uint16_t)c) == 0) return c;
    return -1;
}

/* Записать файл игры (с SD) в образ: прочитать через fat_read_file порциями,
 * разложить по свободным кластерам data-области, проставить цепочку в FAT1,
 * отразить в FAT2, добавить запись в корневой каталог. f — SD-запись.
 * Возвращает 0 при успехе, -1 при ошибке (мало места/нет файла). */
static int m15_write_file_into_floppy(uint8_t* img, const fat_entry_t* f,
                                      uint32_t* used_from)
{
    if (!f || f->size == 0 || f->size > (uint32_t)(M15_CLUSTERS * M15_CLUSZ))
        return -1;
    uint8_t* fat1 = img + M15_FAT_OFF;
    uint8_t* fat2 = fat1 + M15_SPF * M15_BPS;
    uint8_t* root = img + M15_ROOT_OFF;

    /* номер первого кластера */
    int first = m15_fat_next_free(fat1, (int)*used_from);
    if (first < 0) return -1;
    int cur = first;
    uint32_t left = f->size;
    uint32_t off = 0;
    while (left > 0) {
        if (cur >= M15_CLUSTERS + 2) return -1;
        uint8_t* dst = img + M15_DATA_OFF + (uint32_t)(cur - 2) * M15_CLUSZ;
        uint32_t chunk = left < M15_CLUSZ ? left : M15_CLUSZ;
        uint8_t buf[1024];
        if (fat_read_file(f, off, buf, chunk) != (int)chunk) return -1;
        memcpy(dst, buf, chunk);
        if (left > chunk) {
            int nxt = m15_fat_next_free(fat1, cur + 1);
            if (nxt < 0) return -1;
            m15_fat_set(fat1, (uint16_t)cur, (uint32_t)nxt);
            m15_fat_set(fat2, (uint16_t)cur, (uint32_t)nxt);
            cur = nxt;
        } else {
            m15_fat_set(fat1, (uint16_t)cur, 0xFFF);
            m15_fat_set(fat2, (uint16_t)cur, 0xFFF);
        }
        off += chunk;
        left -= chunk;
    }
    *used_from = (uint32_t)(first + 1);

    /* слот в корневом каталоге */
    int slot = -1;
    for (int i = 0; i < M15_ROOTENT; i++) {
        uint8_t c = root[i * 32];
        if (c == 0 || c == 0xE5) { slot = i; break; }
    }
    if (slot < 0) return -1;
    uint8_t* e = root + (size_t)slot * 32;
    memset(e, 0, 32);
    char n83[12];
    m15_name83(f->name, n83);
    // r1413: два длинных имени могут схлопнуться в одно 8.3 — не затираем
    // существующую запись, а сообщаем об ошибке сборки.
    for (int i = 0; i < M15_ROOTENT; i++) {
        uint8_t* e2 = root + (size_t)i * 32;
        if (e2[0] == 0) break;
        if (e2[0] == 0xE5) continue;
        if (memcmp(e2, n83, 11) == 0) return -1;
    }
    for (int i = 0; i < 11; i++) e[i] = (uint8_t)n83[i];
    e[11] = 0x20;                                  /* archive */
    e[26] = (uint8_t)(first & 0xFF);               /* first cluster LE */
    e[27] = (uint8_t)((first >> 8) & 0xFF);
    e[28] = (uint8_t)(f->size & 0xFF);             /* size LE */
    e[29] = (uint8_t)((f->size >> 8) & 0xFF);
    e[30] = (uint8_t)((f->size >> 16) & 0xFF);
    e[31] = (uint8_t)((f->size >> 24) & 0xFF);
    return 0;
}

/* Составить AUTOEXEC.BAT (текст) и записать поверх существующей записи
 * системного диска. clu — первый кластер AUTOEXEC.BAT (одна запись в корне),
 * sysboot-диск уже имеет её на кластере 70. Замена ин-плейс: файл мал. */
static int m15_rewrite_autoexec(uint8_t* img, uint16_t clu, const char* text)
{
    size_t len = strlen(text);
    if (len > M15_CLUSZ) return -1;
    uint8_t* fat1 = img + M15_FAT_OFF;
    uint8_t* root = img + M15_ROOT_OFF;
    // r1413: кластер AUTOEXEC.BAT берём из его корневой записи, а не хардкодом
    // 70 — иначе на нестандартном системном образе портится чужой кластер.
    uint8_t* ent = 0;
    for (int i = 0; i < M15_ROOTENT; i++) {
        uint8_t* e = root + (size_t)i * 32;
        if (e[0] == 0) break;
        if (memcmp(e, "AUTOEXECBAT", 11) == 0) { ent = e; break; }
    }
    if (ent) {
        uint16_t fc = (uint16_t)(ent[26] | ((uint16_t)ent[27] << 8));
        if (fc >= 2) clu = fc;
    }
    if (clu < 2 || m15_fat_get(fat1, clu) == 0xFFF || m15_fat_get(fat1, clu) == 0) {
        /* цепочки нет — выделяем один кластер */
        int c = m15_fat_next_free(fat1, 2);
        if (c < 0) return -1;
        clu = (uint16_t)c;
        m15_fat_set(fat1, clu, 0xFFF);
        memcpy(img + M15_FAT_OFF + M15_SPF * M15_BPS, fat1, M15_SPF * M15_BPS);
    }
    if (ent) {
        ent[26] = (uint8_t)(clu & 0xFF);
        ent[27] = (uint8_t)((clu >> 8) & 0xFF);
        ent[28] = (uint8_t)(len & 0xFF);
        ent[29] = (uint8_t)((len >> 8) & 0xFF);
        ent[30] = 0; ent[31] = 0;
    }
    uint8_t* dst = img + M15_DATA_OFF + (uint32_t)(clu - 2) * M15_CLUSZ;
    memset(dst, 0, M15_CLUSZ);
    memcpy(dst, text, len);
    return 0;
}

/* Мелкие регистр-независимые сравнения имён FAT (как в rom_browser.c, но
 * локальные, чтобы build-модуль был самодостаточным). FAT-имена 8.3. */
static char m15_lc(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}
static int m15_ci_eq(const char* a, const char* b) {
    while (*a && *b) {
        if (m15_lc(*a) != m15_lc(*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}
static int m15_ci_prefix(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] || !b[i] || m15_lc(a[i]) != m15_lc(b[i])) return 0;
    }
    return 1;
}

/* Выбрать главный запускаемый файл игры в папке. Приоритет:
 * 1) точный AUTOEXEC.BAT / RUNME.BAT / runme.bat — берём из него вызов
 *    (после @echo off / pause / cls / echo… находим первый `.exe`/`.com`/`gp`),
 * 2) файл, чьё имя совпадает с именем каталога (по регистру-независимо),
 * 3) первый .exe,
 * 4) первый .com,
 * 5) первый файл.
 * Имя копируется в out (без пути). Возвращает 0 при успехе. Использует
 * fat_scratch() — вызывать ДО повторного fat_list корня. */
int m15_find_main(const char* dir, const char* dirname,
                         char* out, int outsz)
{
    fat_entry_t* list = fat_scratch();
    int n = fat_list(dir, list, FAT_MAX_ENTRIES);
    if (n <= 0) return -1;

    int bat = -1, same = -1, exe = -1, com = -1, any = -1;
    int dl = (int)strlen(dirname);
    for (int i = 0; i < n; i++) {
        if (list[i].attr & 0x10) continue;
        if (list[i].size == 0) continue;
        const char* nm = list[i].name;
        if (m15_ci_eq(nm, "autoexec.bat") ||
            m15_ci_eq(nm, "runme.bat")   ||
            m15_ci_eq(nm, "run.bat")) { if (bat < 0) bat = i; }
        const char* dot = strrchr(nm, '.');
        int bl = dot ? (int)(dot - nm) : (int)strlen(nm);
        if (bl == dl && m15_ci_prefix(nm, dirname, dl)) { if (same < 0) same = i; }
        if (dot && m15_ci_eq(dot, ".exe")) { if (exe < 0) exe = i; }
        if (dot && m15_ci_eq(dot, ".com")) { if (com < 0) com = i; }
        if (any < 0) any = i;
    }

    /* Из .bat достаём первое «исполняемое слово» строки (после @echo off,
       pause, cls, echo). См. реальные игры: grandprix/RUNME.BAT -> gp,
       dos_pacific_war/PW.BAT -> pac.exe (после title.exe), 3dheli/runme.bat
       -> HELI.EXE. main = первое исполняемое слово. */
    char main_name[FAT_NAME_LEN];
    int have_main = 0;
    if (bat >= 0) {
        fat_entry_t* fb = &list[bat];
        char content[384];
        if (fb->size < (uint32_t)sizeof(content)) {
            int r = fat_read_file(fb, 0, (uint8_t*)content, (uint32_t)fb->size);
            if (r == (int)fb->size) {
                content[fb->size] = 0;
                /* первые слова: цепочка команд НЕ .BAT/echo/pause/cls/@ */
                const char* p = content;
                while (p && *p) {
                    while (*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
                    const char* w = p;
                    while (*p && *p!=' '&&*p!='\t'&&*p!='\r'&&*p!='\n') p++;
                    if (p == w) break;
                    int wl = (int)(p - w);
                    if (wl >= 2 && wl <= 11 &&
!(wl==4 && m15_ci_prefix(w,"echo",4)) &&
                        !(wl==5 && m15_ci_prefix(w,"pause",5)) &&
                        !(wl==3 && m15_ci_prefix(w,"cls",3)) &&
                        !(wl>=4 && m15_ci_prefix(w,"title",5)) ) {
                        /* слово — исполняемый файл или команда (gp) */
                        if (wl >= outsz) wl = outsz - 1;
                        memcpy(main_name, w, (size_t)wl);
                        main_name[wl] = 0;
                        have_main = 1;
                        break;
                    }
                    /* иначе переходим к след. слову; конец? */
                }
            }
        }
    }

    if (have_main) { memcpy(out, main_name, strlen(main_name)+1); return 0; }

    int pick = (exe>=0)?exe:(com>=0)?com:(same>=0)?same:(any>=0)?any:-1;
    if (pick < 0) return -1;
    int l = (int)strlen(list[pick].name);
    if (l >= outsz) l = outsz - 1;
    memcpy(out, list[pick].name, (size_t)l);
    out[l] = 0;
    return 0;
}

/* Собрать FAT12-дискету 720К: копия вшитой системной дискеты + файлы папки
 * игры + AUTOEXEC.BAT с запуском main. dir — каталог игры (абс. путь SD),
 * main — имя запускаемого файла (без пути). Возвращает 0 при успехе.
 * Образ кладётся в M15_IMG_BASE, активируется через g_ms1504_active_img. */
int m15_build_floppy(const char* dir, const char* main)
{
    uint8_t* img = (uint8_t*)M15_IMG_BASE;
    if (ms1504_boot_ima_len != M15_FLOPPY_SIZE) return -1;
    memcpy(img, ms1504_boot_ima, M15_FLOPPY_SIZE);

    fat_entry_t* list = fat_scratch();
    int n = fat_list(dir, list, FAT_MAX_ENTRIES);
    if (n <= 0) return -1;

    uint32_t used_from = 2;
    int wrote = 0;
    for (int i = 0; i < n; i++) {
        if (list[i].attr & 0x10) continue;
        if (list[i].size == 0) continue;
        // r1413: свой AUTOEXEC.BAT из папки не копируем — его пишем сами
        // (иначе коллизия 8.3 с системным AUTOEXEC.BAT).
        if (m15_ci_eq(list[i].name, "autoexec.bat")) continue;
        /* Строго: если файл не влез — это ошибка (вызывающий перейдёт на
         * HDD-путь), а не потерянный тихо файл игры. */
        if (m15_write_file_into_floppy(img, &list[i], &used_from) != 0)
            return -1;
        wrote++;
    }
    if (!wrote) return -1;

    /* AUTOEXEC.BAT: @echo off \n <main>\n */
    char bat[128];
    snprintf(bat, sizeof(bat), "@echo off\r\n%s\r\n", main);
    if (m15_rewrite_autoexec(img, 70, bat) != 0) return -1;

    /* повтор: распределение данных завершено; активен только флоппи-образ */
    g_ms1504_active_img = img;
    g_ms1504_active_img_len = M15_FLOPPY_SIZE;
    g_ms1504_hdd_img = 0;          /* HDD не используется для малой игры */
    g_ms1504_hdd_img_len = 0;
    return 0;
}

/* ---- FAT16 HDD-образ для больших игр (диск 0x80, до 4 МБ) ----
 * Грубый FAT16: boot-сектор + 2×FAT + корень + данные. Кластер 4 КБ.
 * Системные файлы НЕ нужны (MS-DOS грузится с A:, C: содержит только игру;
 * AUTOEXEC.BAT на A: переходит на C: и запускает главный файл).
 * Отдаём пару образов: флоппи A: (системная + AUTOEXEC «C:» + main) — через
 * g_ms1504_active_img, сам HDD C: — через g_ms1504_hdd_img. */

enum {
    M16_BPS     = 512,
    M16_SPC     = 8,          /* 4 КБ кластер */
    M16_RESV    = 1,
    M16_NFAT    = 2,
    M16_ROOTENT = 512,
    M16_TOTSEC  = M15_HDD_SIZE / M16_BPS,          /* 8192 */
    M16_ROOTSEC = (M16_ROOTENT * 32) / M16_BPS     /* 32 */
};

/* Собрать пару образов для большой игры: флоппи A: (системная MS-DOS с
 * AUTOEXEC, переходящим на C: и запускающим main) + FAT16 HDD C: с файлами
 * папки. Возвращает 0 при успехе. A: — в M15_IMG_BASE (floppy-буфер),
 * активируется через g_ms1504_active_img; C: — в M15_HDD_BASE через
 * g_ms1504_hdd_img. */
int m15_build_hdd(const char* dir, const char* main)
{
    /* FAT16 sectors-per-FAT: считаем из числа кластеров */
    uint32_t data_sec = M16_TOTSEC - M16_RESV - 2 * M16_ROOTSEC - M16_ROOTSEC;
    uint32_t clusters = data_sec / M16_SPC;
    uint32_t fat_sec  = ((clusters * 2 + M16_BPS - 1) / M16_BPS);
    if (fat_sec < 1) fat_sec = 1;
    uint32_t root_off = (M16_RESV + 2 * fat_sec) * M16_BPS;
    uint32_t data_off = (M16_RESV + 2 * fat_sec + M16_ROOTSEC) * M16_BPS;
    uint32_t total_sec = data_off / M16_BPS + data_sec;

    uint8_t* img = (uint8_t*)M15_HDD_BASE;
    memset(img, 0, M15_HDD_SIZE);

    /* Boot-сектор FAT16 с минимальным BPB */
    uint8_t* b = img;
    b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90;
    memcpy(b + 3, "MSDOS5.0", 8);
    b[11] = M16_BPS & 0xFF; b[12] = (M16_BPS >> 8) & 0xFF;
    b[13] = M16_SPC;
    b[14] = M16_RESV & 0xFF; b[15] = (M16_RESV >> 8) & 0xFF;
    b[16] = M16_NFAT;
    b[17] = M16_ROOTENT & 0xFF; b[18] = (M16_ROOTENT >> 8) & 0xFF;
    b[19] = total_sec & 0xFF;   b[20] = (total_sec >> 8) & 0xFF;
    b[21] = 0xF8;                                  /* media descriptor */
    b[22] = fat_sec & 0xFF;   b[23] = (fat_sec >> 8) & 0xFF;
    b[24] = 0x40; b[25] = 0x00;                    /* 64 sec/track */
    b[26] = 0x10; b[27] = 0x00;                    /* heads */
    b[28] = 0x3F; b[29] = 0x00; b[30] = 0x00; b[31] = 0x00; /* hidden */
    b[32] = total_sec & 0xFF; b[33] = (total_sec >> 8) & 0xFF;
    b[34] = (total_sec >> 16) & 0xFF; b[35] = (total_sec >> 24) & 0xFF;
    b[54] = 0x29;
    b[62] = 0x00; b[63] = 0x00; b[64] = 0x00; b[65] = 0x00; /* volume serial */
    b[66] = ' '; b[67] = ' '; b[68] = ' '; b[69] = ' '; b[70] = ' ';
    memcpy(b + 71, "FAT16   ", 8);
    b[510] = 0x55; b[511] = 0xAA;

    /* FAT: кластер 0 = 0xFFF8 (media), кластер 1 = 0xFFFF */
    uint8_t* fat1 = img + M16_RESV * M16_BPS;
    fat1[0] = 0xF8; fat1[1] = 0xFF; fat1[2] = 0xFF;
    fat1[3] = 0xFF;
    uint8_t* fat2 = fat1 + fat_sec * M16_BPS;
    memcpy(fat2, fat1, fat_sec * M16_BPS);

    /* копируем файлы папки в корень C: */
    fat_entry_t* list = fat_scratch();
    int n = fat_list(dir, list, FAT_MAX_ENTRIES);
    if (n <= 0) return -1;
    uint32_t cur = 2;
    int wrote = 0;
    int oom = 0;   // r1413: нехватка места не должна давать «успешный» обрезанный образ
    uint8_t* root = img + root_off;
    int slot = 0;
    for (int i = 0; i < n; i++) {
        if (list[i].attr & 0x10) continue;
        if (list[i].size == 0) continue;
        uint32_t need = (list[i].size + (M16_SPC * M16_BPS) - 1) / (M16_SPC * M16_BPS);
        if (cur + need > clusters + 2) { oom = 1; break; }
        /* записываем данные */
        uint32_t first = cur;
        uint32_t off = 0, left = list[i].size;
        while (left > 0) {
            uint8_t* dst = img + data_off + (uint32_t)(cur - 2) * M16_SPC * M16_BPS;
            uint32_t chunk = left < (M16_SPC * M16_BPS) ? left : (M16_SPC * M16_BPS);
            uint8_t buf[4096];
            if (fat_read_file(&list[i], off, buf, chunk) != (int)chunk)
                return -1;   // r1413: недопрочитанный файл — ошибка сборки
            memcpy(dst, buf, chunk);
            off += chunk; left -= chunk;
            /* проставить FAT16 */
            uint32_t fatn = (uint32_t)cur * 2;
            if (left > 0) {
                uint16_t nxt = (uint16_t)(cur + 1);
                fat1[fatn] = (uint8_t)nxt; fat1[fatn+1] = (uint8_t)(nxt >> 8);
                memcpy(fat2 + fatn, fat1 + fatn, 2);
            } else {
                fat1[fatn] = 0xFF; fat1[fatn+1] = 0xFF;
                memcpy(fat2 + fatn, fat1 + fatn, 2);
            }
            cur++;
        }
        /* запись каталога */
        uint8_t* e = root + (size_t)slot * 32;
        memset(e, 0, 32);
        char n83[12]; m15_name83(list[i].name, n83);
        for (int k = 0; k < 11; k++) e[k] = (uint8_t)n83[k];
        e[11] = 0x20;
        e[26] = (uint8_t)(first & 0xFF); e[27] = (uint8_t)((first >> 8) & 0xFF);
        e[28] = (uint8_t)(list[i].size & 0xFF);
        e[29] = (uint8_t)((list[i].size >> 8) & 0xFF);
        e[30] = (uint8_t)((list[i].size >> 16) & 0xFF);
        e[31] = (uint8_t)((list[i].size >> 24) & 0xFF);
        slot++;
        wrote++;
        if (slot >= M16_ROOTENT) break;
    }
    if (!wrote || oom) return -1;   // r1413: нехватка места — не «успех»

    /* Флоппи A: — системная дискета MS-DOS с AUTOEXEC: переход на C: и запуск
     * main. Берём из вшитой копии, перезаписываем AUTOEXEC.BAT. */
    uint8_t* flop = (uint8_t*)M15_IMG_BASE;
    if (ms1504_boot_ima_len != M15_FLOPPY_SIZE) return -1;
    memcpy(flop, ms1504_boot_ima, M15_FLOPPY_SIZE);
    char bat[128];
    snprintf(bat, sizeof(bat), "@echo off\r\nC:\r\n%s\r\n", main);
    if (m15_rewrite_autoexec(flop, 70, bat) != 0) return -1;

    /* C: (HDD) — собранный FAT16 образ; A: (флоппи) — системная. */
    g_ms1504_active_img = flop;
    g_ms1504_active_img_len = M15_FLOPPY_SIZE;
    g_ms1504_hdd_img = (const uint8_t*)img;
    g_ms1504_hdd_img_len = M15_HDD_SIZE;
    return 0;
}