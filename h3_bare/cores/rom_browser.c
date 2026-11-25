#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "fb_text.h"
#include "fat.h"
#include "uart.h"
#include "usb_kbd.h"
#include "emu.h"
#include "cheatdb.h"
#include "sega_pad.h"
#include "h3.h"
#include "h3_hs_timer.h"

extern int printf(const char* fmt, ...);

// forward: определён ниже
static int input_wait(void);

#define PHYS_W 1024
#define PHYS_H 600
#define ROW_H 18
#define FOOTER_Y (PHYS_H - 30)

#define ROM_BUF 0x50000000
#define ROM_MAX (24 * 1024 * 1024)

static int load_rom(const char* path, const char* name, uint8_t** rom, uint32_t* size) {
    fat_entry_t f;
    if (!fat_find(path, name, &f)) { printf("load_rom: not found %s/%s\n", path, name); return -1; }
    if (f.size == 0 || f.size > ROM_MAX) { printf("load_rom: bad size %lu\n", (unsigned long)f.size); return -1; }
    uint8_t* buf = (uint8_t*)ROM_BUF;
    printf("load_rom: %s/%s size=%lu cl=%lu\n", path, name, (unsigned long)f.size, (unsigned long)f.first_cluster);
    /* r836: индикатор загрузки. Чтение ROM с USB-флешки идёт секундами, и
       без прогресса это выглядит как зависание (владелец). Читаем блоками
       по 256 КБ и обновляем полосу — как у аркад. */
    const int bx = 60, by = 300, bw = 904, bh = 26;
    fb_clear();
    fb_puts_s(60, 200, "Loading ROM...", 2, 0x00FFFF00);
    fb_puts_s(60, 240, name, 1, 0x00FFFFFF);
    fb_fill_rect(bx, by, bw, bh, 0x00222222);
    fb_puts_s(bx, by + 40, "0 %", 1, 0x00AAAAAA);
    fb_flush();

    uint32_t done = 0;
    const uint32_t chunk = 256u * 1024u;
    int r = 0;
    while (done < f.size) {
        uint32_t n = f.size - done;
        if (n > chunk) n = chunk;
        r = fat_read_file(&f, done, buf + done, n);
        if (r < 0 || (uint32_t)r != n) { printf("load_rom: short read at %lu\n", (unsigned long)done); return -1; }
        done += n;
        uint32_t pct = (uint32_t)(((uint64_t)done * 100u) / f.size);
        fb_fill_rect(bx, by, (bw * (int)pct) / 100, bh, 0x0000AA00);
        char pb[16];
        snprintf(pb, sizeof(pb), "%lu %%", (unsigned long)pct);
        fb_fill_rect(bx, by + 40, 120, 18, 0x00000000);
        fb_puts_s(bx, by + 40, pb, 1, 0x00AAAAAA);
        fb_flush();
    }
    printf("load_rom: got %lu bytes\n", (unsigned long)done);
    // sd.c читает PIO: CPU пишет данные через обычные store, поэтому
    // dirty-линии D-cache (write-back) могут не дойти до DRAM. Инвалидация
    // (DCIMVAC) НЕ подходит — она отбросит грязные данные. Используем
    // clean+invalidate (DCCIMVAC): сливаем кэш в DRAM и делаем линии
    // невалидными, чтобы CPU читал настоящий ROM.
    // r581 (Д-47): end = округление ВВЕРХ до границы линии (32 б) — раньше
    // было `a + f.size + 32`, что чистило до 31 байта ЗА прочитанными данными
    // (при f.size%32==0 — ровно +32); внутри ROM_BUF безвредно, но формально
    // выход за границы буфера. Теперь чистим ровно линии, покрывающие ROM.
    uint32_t a = (uint32_t)buf & ~0x1Fu;
    uint32_t end = (a + f.size + 31) & ~0x1Fu;
    for (; a < end; a += 32)
        __asm volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"(a));
    __asm volatile("dsb" ::: "memory");
    // r1000: диагностика — что реально лежит в ROM_BUF сразу после чтения
    // и cache-операции (до входа в эмулятор).
    printf("load_rom: buf first=%02X %02X %02X %02X\n",
           buf[0], buf[1], buf[2], buf[3]);
    *rom = buf; *size = f.size;
    return 0;
}

// запуск эмулятора по sys_id (общая логика: вход/выход, справка TFT, 0x74)
static void run_emulator(const char* sys_id, uint8_t* rom, uint32_t size,
                         const char* sel_name) {
    // Показать справку на TFT по кнопкам этой системы.
    extern void tft_help_show(const char* sys_id);
    tft_help_show(sys_id);

    // r180: «игра активна» — CPU1 замораживает TFT (SRAM 0x74), чтобы не
    // трогать PA_DAT (RMW-гонка с Sega-падом). Выставляем ПОСЛЕ
    // tft_help_show (справка успевает отрисоваться), снимаем после выхода
    // из эмулятора. Покрывает ВСЕ пути запуска (раньше ставился только
    // из чит-меню-запуска).
    *(volatile uint32_t*)0x74u = 1;
    __asm volatile("dsb st" ::: "memory");   // r0.198: флаг виден CPU1 до эмулятора

    // r0.390 (S1): init и ожидание отпускания пада перенесены ПОСЛЕ
    // заморозки CPU1 — раньше (до tft_help_show и 0x74) они выполнялись
    // сотнями I2C-итераций (~500 мс) пока CPU1 ещё писал PA_DAT
    // (тач-скан/TFT) — окно RMW-гонки с падом. Теперь CPU1 заморожен и
    // скан пада идёт на чистой шине.
    // Переинициализация PCF8574 (0xFF → TH=1 idle) лечит «мёртвый» пад
    // после сбоя I2C — тот же приём, что в Sega 6-button test.
    sega_pad_init();

    // r155: ждём ОТПУСКАНИЯ пада (макс 500 мс), чтобы зажатая в браузере
    // кнопка (Enter/A/Start) не «доехала» в первый кадр игры как ложное
    // нажатие. См. usb_pad_wait_release — лимит 500 итераций × 1 мс.
    usb_pad_wait_release();

    emu_clear_fb();
    if (strcmp(sys_id, "a2600") == 0)
        emu_run_a2600(rom, size, sel_name);   // r1525: Stella вместо MCUME
    else if (strcmp(sys_id, "a7800") == 0)
        emu_run_a7800(rom, size, sel_name);
    else if (strcmp(sys_id, "a5200") == 0)
        emu_run_a5200(rom, size, sel_name);
    else if (strcmp(sys_id, "sms") == 0)
        emu_run_sms(rom, size, sel_name);
    else if (strcmp(sys_id, "gamegear") == 0)
        emu_run_gg(rom, size, sel_name);
    else if (strcmp(sys_id, "sg1000") == 0)
        emu_run_sg1000(rom, size, sel_name);   // r1190: SG-1000 /roms/sg1000
    else if (strcmp(sys_id, "nes") == 0)
        emu_run_nes(rom, size, sel_name);
    else if (strcmp(sys_id, "snes") == 0)
        emu_run_snes(rom, size, sel_name);
    else if (strcmp(sys_id, "portfolio") == 0)
        emu_run_portfolio(rom, size, sel_name);
    else if (strcmp(sys_id, "gameboy") == 0)
        emu_run_gameboy(rom, size, sel_name);
    else if (strcmp(sys_id, "gba") == 0)
        emu_run_gba(rom, size, sel_name);
    else if (strcmp(sys_id, "lynx") == 0)
        emu_run_lynx(rom, size, sel_name);
    else if (strcmp(sys_id, "ngp") == 0)
        emu_run_ngp(rom, size, sel_name);
    else if (strcmp(sys_id, "gw") == 0)
        emu_run_gw(rom, size, sel_name);   // r1544: Game & Watch / Электроника
    else if (strcmp(sys_id, "atarist") == 0)
        emu_run_atari_st_cw(rom, size, sel_name);   // r1567: Atari ST (Castaway)
    else if (strcmp(sys_id, "megadrive") == 0)
        emu_run_megadrive(rom, size, sel_name);
    else if (strcmp(sys_id, "32x") == 0)
        emu_run_32x(rom, size, sel_name);
    else if (strcmp(sys_id, "msx") == 0)
        emu_run_msx(rom, size, sel_name);
    else if (strcmp(sys_id, "vectrex") == 0)
        emu_run_vectrex(rom, size, sel_name);
    else if (strcmp(sys_id, "coleco") == 0)
        emu_run_coleco(rom, size, sel_name);
    else if (strcmp(sys_id, "pce") == 0)
        emu_run_pce(rom, size, sel_name);
    else if (strcmp(sys_id, "zxspectrum") == 0)
        emu_run_fuse(rom, size, sel_name);
    else if (strcmp(sys_id, "bk0010") == 0)
        emu_run_bk(rom, size, sel_name);
    else if (strcmp(sys_id, "ms1504") == 0)
        emu_run_ms1504(rom, size, sel_name);
    else if (strcmp(sys_id, "cps1") == 0)
        emu_run_cps1(rom, size, sel_name);   // ROM-сет читает сам host (папка/zip)
    else if (strcmp(sys_id, "cps2") == 0)
        emu_run_cps2(rom, size, sel_name);
    else if (strcmp(sys_id, "neogeo") == 0)
        emu_run_neogeo(rom, size, sel_name);
    else if (strcmp(sys_id, "toaplan") == 0)
        emu_run_toaplan(rom, size, sel_name);   // ROM-сет читает host (папка/zip)
    else if (strcmp(sys_id, "cave") == 0)
        emu_run_cave(rom, size, sel_name);      // Cave 68K — /roms/cave (r0.383)
    else if (strcmp(sys_id, "segasys") == 0)
        emu_run_segasys(rom, size, sel_name);   // Sega System 16 — /roms/segasys (r0.383)
    else if (strcmp(sys_id, "namco") == 0)
        emu_run_namco(rom, size, sel_name);     // Namco — /roms/namco (r1610)
    else if (strcmp(sys_id, "taito") == 0)
        emu_run_taito(rom, size, sel_name);     // Taito — /roms/taito (r1623)
    else if (strcmp(sys_id, "dataeast") == 0)
        emu_run_dataeast(rom, size, sel_name);  // Data East — /roms/dataeast (r1626)
    else if (strcmp(sys_id, "psikyo") == 0)
        emu_run_psikyo(rom, size, sel_name);    // Psikyo PS3 — /roms/psikyo (r1627)
    else if (strcmp(sys_id, "fbneo") == 0)
        emu_run_fbneo(rom, size, sel_name);     // общий FBNeo-корень
    else if (strcmp(sys_id, "atari800") == 0)
        emu_run_atari800(rom, size, sel_name);  // Atari 8-bit (400/800/XL/XE) — /roms/atari800 (r778)
    else if (strcmp(sys_id, "cpc") == 0)
        emu_run_cpc(rom, size, sel_name);       // Amstrad CPC — /roms/cpc (r963)
    else if (strcmp(sys_id, "c64") == 0)
        emu_run_frodo(rom, size, sel_name);     // Commodore 64 — /roms/c64 (r1284: ядро Frodo)
    else if (strcmp(sys_id, "amiga") == 0)
        emu_run_amiga(rom, size, sel_name);     // Commodore Amiga — /roms/amiga (r1191, PUAE)
    else if (strcmp(sys_id, "agat") == 0)
        emu_run_agat(rom, size, sel_name);      // Агат-9 — /roms/agat (r1297)
    else if (strcmp(sys_id, "apple2") == 0) {
        /* r1404: вход/выход в эмулятор отдельными строками — зависание
           между ними локализуется по последней строке UART. */
        printf("apple2: run '%s' size=%u\n", sel_name, (unsigned)size);
        emu_run_apple2(rom, size, sel_name);
        printf("apple2: run done\n");
    }
    else if (strcmp(sys_id, "radio86rk") == 0)
        emu_run_rk86(rom, size, sel_name);      // Радио-86РК — /roms/radio86rk (r1185)
    else if (strcmp(sys_id, "enterprise") == 0)
        emu_run_ep(rom, size, sel_name);        // Enterprise 64/128 — /roms/enterprise (r135x)
    else if (strcmp(sys_id, "pxt") == 0) {
        // r1413: HDD-образы PXT лежат в папке рома (/roms/pxt/hdd/hdd.img,
        // hdd2.img), как на флешке стенда; монтируем до запуска (C:/D:).
        extern void pxt_mount_hdd(void);
        pxt_mount_hdd();
        emu_run_pxt(rom, size, sel_name);       // IBM PC/XT-286 — /roms/pxt (r1412)
    }
    else {
        fb_clear();
        fb_text_center("System not implemented yet", 200, 2, 0x00FFAA00);
        fb_text_center("Press any key", 250, 1, 0x00FFFFFF);
        fb_flush();
        input_wait();
    }

    // r0.204: ждём отпускания ВСЕХ кнопок — зажатая кнопка выхода (ESC/Start+Mode)
    // не должна «доехать» в меню и прыгнуть курсор в первую строку.
    usb_wait_release_all();
    // r0.222: быстрые поллы в эмуляторе могли рассинхронизировать ED клавиатуры —
    // перезапускаем interrupt-IN цепочку, чтобы меню снова получало отчёты.
    usb_kbd_restart_intr();

    // Выход из эмулятора: снова переинициализация геймпада — возвращаемся
    // в меню с чистым падом (полный TFT-рендер меню мог снова сорвать I2C).
    sega_pad_init();

    // Игра окончилась — TFT снова жив (r162): снимаем «заморозку» CPU1.
    *(volatile uint32_t*)0x74u = 0;
    __asm volatile("dsb st" ::: "memory");   // r0.198
}

// r1354: каталоги всегда выше файлов, внутри группы — по имени. Раньше всё
// сортировалось вперемешку, и в /roms/enterprise (939 каталогов + 592 файла)
// файлы «размазывались» между играми.
static void sort_entries(fat_entry_t *list, int n) {
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++) {
            int di = (list[i].attr & 0x10) ? 0 : 1;
            int dj = (list[j].attr & 0x10) ? 0 : 1;
            int swap = (di != dj) ? (di > dj)
                                  : (strcmp(list[i].name, list[j].name) > 0);
            if (swap) {
                fat_entry_t t = list[i]; list[i] = list[j]; list[j] = t;
            }
        }
}

static int input_wait(void) {
    for (;;) {
        int k = usb_input_poll();
        if (k) return k;
        // r0.417 (M4 аудита): предыдущая версия крутила poll без паузы —
        // busy-loop давал RMW-гонку PA_DAT с TFT-ядром (срыв I2C-пада/джоёв).
        udelay(16000);
    }
}


// r1356: в каталоге игры показываем только то, что реально запускается.
// Для Enterprise это .com/.bas (EXOS-программы); .prg/.scr/.1/.2/.dat — данные
// игры, их host-слой подхватывает сам по каталогу (ep_load_dir_files), а в
// списке они только мешают выбрать нужный файл. Каталоги видны всегда.
static int ep_file_visible(const char* name) {
    const char* dot = 0;
    for (const char* p = name; *p; p++)
        if (*p == '.') dot = p;
    if (!dot) return 0;
    char e[4] = { 0 };
    for (int i = 0; i < 3 && dot[1 + i]; i++) {
        char c = dot[1 + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        e[i] = c;
    }
    return (strcmp(e, "com") == 0 || strcmp(e, "bas") == 0);
}

// r1360: Apple II+ — в папке показываем только образы дисков. В /roms/apple2
// лежат сотни .woz и .do; файлы ПЗУ (.rom) и прочий мусор выбирать нельзя —
// host их не монтирует, и машина молча уходит в BASIC.
static int apple2_file_visible(const char* name) {
    const char* dot = 0;
    for (const char* p = name; *p; p++)
        if (*p == '.') dot = p;
    if (!dot) return 0;
    char e[4] = { 0 };
    for (int i = 0; i < 3 && dot[1 + i]; i++) {
        char c = dot[1 + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        e[i] = c;
    }
    return (strcmp(e, "dsk") == 0 || strcmp(e, "do") == 0 ||
            strcmp(e, "po") == 0 || strcmp(e, "nib") == 0 ||
            strcmp(e, "woz") == 0);
}

// Единая точка листинга: fat_list + фильтр по системе (r1356).
static int list_dir_filtered(const char* path, const char* sys_id, fat_entry_t* list) {
    int n = fat_list(path, list, FAT_MAX_ENTRIES);
    int is_ep = (strcmp(sys_id, "enterprise") == 0);
    int is_a2 = (strcmp(sys_id, "apple2") == 0);
    if (!is_ep && !is_a2) return n;
    int w = 0;
    for (int i = 0; i < n; i++) {
        if ((list[i].attr & 0x10) ||
            (is_ep ? ep_file_visible(list[i].name)
                   : apple2_file_visible(list[i].name)))
            list[w++] = list[i];
    }
    return w;
}

// r1362: аркады (CPS/NeoGeo/Toaplan/Cave/Sega System 16/FBNeo) и Enterprise
// в браузере показывают папку как файл без расширения, а Enter на папке
// ЗАПУСКАЕТ игру (host сам читает папку целиком или zip), а не входит внутрь.
static int is_arcade_sys(const char* sys_id) {
    return strcmp(sys_id, "cps1") == 0 || strcmp(sys_id, "cps2") == 0 ||
           strcmp(sys_id, "neogeo") == 0 || strcmp(sys_id, "toaplan") == 0 ||
           strcmp(sys_id, "fbneo") == 0 || strcmp(sys_id, "cave") == 0 ||
           strcmp(sys_id, "segasys") == 0 || strcmp(sys_id, "namco") == 0 ||
           strcmp(sys_id, "taito") == 0 || strcmp(sys_id, "dataeast") == 0 ||
           strcmp(sys_id, "psikyo") == 0;
}

static char rom_lc(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int rom_ci_eq(const char* a, const char* b) {
    while (*a && *b) {
        if (rom_lc(*a) != rom_lc(*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static int rom_ci_prefix(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] || !b[i] || rom_lc(a[i]) != rom_lc(b[i])) return 0;
    }
    return 1;
}

// r1362: главный файл EP-игры в каталоге. Приоритет: start.com/start,
// файл с именем каталога, первый .com, первый .bas, любой файл.
// Результат копируется в out ДО load_rom (fat_scratch перезатрётся).
static int ep_find_main(const char* dir, const char* dirname,
                        char* out, int outsz) {
    fat_entry_t* list = fat_scratch();
    int n = fat_list(dir, list, FAT_MAX_ENTRIES);
    int start = -1, same = -1, com = -1, bas = -1, any = -1;
    if (n <= 0) return -1;
    int dl = (int)strlen(dirname);
    for (int i = 0; i < n; i++) {
        if (list[i].attr & 0x10) continue;
        if (list[i].size == 0) continue;
        const char* nm = list[i].name;
        if (rom_ci_eq(nm, "start.com") || rom_ci_eq(nm, "start")) {
            if (start < 0) start = i;
        }
        const char* dot = strrchr(nm, '.');
        int bl = dot ? (int)(dot - nm) : (int)strlen(nm);
        if (bl == dl && rom_ci_prefix(nm, dirname, dl)) {
            if (same < 0) same = i;
        }
        if (dot && rom_ci_eq(dot, ".com")) { if (com < 0) com = i; }
        if (dot && rom_ci_eq(dot, ".bas")) { if (bas < 0) bas = i; }
        if (any < 0) any = i;
    }
    int pick = (start >= 0) ? start : (same >= 0) ? same :
               (com >= 0) ? com : (bas >= 0) ? bas : any;
    if (pick < 0) return -1;
    int l = (int)strlen(list[pick].name);
    if (l >= outsz) l = outsz - 1;
    memcpy(out, list[pick].name, (size_t)l);
    out[l] = 0;
    return 0;
}


void rom_browser_run(const char *sys_id, const char *sys_name, const char *rom_dir) {
    (void)sys_id;   // диспетчер эмуляторов по sys_id появится позже
    // Вход сюда — только из menu_run, который уже дождался отпускания
    // Enter/геймпада (usb_kbd_wait_release/usb_pad_wait_release).
    // usb_input_clear() здесь НЕ вызываем: при зажатой кнопке он обнуляет
    // g_pad_prev и превращает удержание в «фантомный фронт» — первый пункт
    // списка активировался бы сам.
    // rom_dir — имя папки с SD (FAT_NAME_LEN до 127 символов).
    // Может быть NULL — система из таблицы без папки на SD (пункт меню
    // с present=0). Тогда показываем заглушку и выходим (не крашимся).
    if (!rom_dir) {
        fb_clear();
        fb_puts_s(60, 120, "No ROM folder on SD for this system", 2, 0x00FFAA00);
        fb_puts_s(60, 160, "Folder /roms/<id>/ not present", 1, 0x00FFFFFF);
        fb_flush();
        return;
    }
    char path[FAT_NAME_LEN + 16];
    int pl = 0;
    const char* pfx = "/roms/";
    while (*pfx && pl < (int)sizeof(path) - 1) path[pl++] = *pfx++;
    for (const char* s = rom_dir; *s && pl < (int)sizeof(path) - 1; s++) path[pl++] = *s;
    path[pl] = 0;

    fat_entry_t* list = fat_scratch();
    int n = list_dir_filtered(path, sys_id, list);
    sort_entries(list, n);
    if (g_fat_dir_truncated)
        printf("rom_browser: WARN %s list truncated at %d (files beyond limit hidden)\n",
               path, n);
    printf("rom_browser: %s n=%d\n", path, n);
    // (пофайловый список убран из отладки — спамил дерево при каждом входе)

    int cursor = 0;
    int scroll = 0;
    int depth = 0;     // r1354: глубина вложенности подпапок (для ESC-выхода)
    int max_rows = (FOOTER_Y - 100) / ROW_H;
    int dirty = 1;   // перерисовать сразу при входе

    for (;;) {
        // Рендер ТОЛЬКО когда что-то изменилось (вход/курсор/список) —
        // иначе экран не мерцает, т.к. не перерисовывается вхолостую.
        if (dirty) {
            fb_draw_stars();
            fb_puts_s(60, 30, sys_name, 2, 0x00FF0000);
            fb_fill_rect(60, 60, 200, 2, 0x00FFFFFF);

            if (n <= 0) {
                fb_puts_s(60, 120, "Folder is empty", 1, 0x00FFAA00);
                fb_puts_s(60, 145, "Put your ROM files here:", 1, 0x00AAAAAA);
                fb_puts_s(60, 163, path, 1, 0x00AAAAAA);
                // r0.391 (M14): список расширений зависит от системы и устаревал
                // (у ZX .z80/.sna, у PCE .pce и т.д.) — показан общий хинт.
                fb_puts_s(60, 185, "Formats depend on system", 1, 0x00666666);
            } else {
                int y = 85;
                for (int i = scroll; i < n && i < scroll + max_rows; i++) {
                    int sel = (i == cursor);
                    uint32_t clr = sel ? 0x00FFFF00 : 0x00FFFFFF;
                    if (sel)
                        fb_fill_rect(50, y - 2, PHYS_W - 100, ROW_H, 0x00181818);
                    char buf[64];
                    int len = strlen(list[i].name);
                    // r1354: каталог помечаем суффиксом '/'. r1362: у аркад и
                    // Enterprise папка — это игра, показываем её как файл без '/'.
                    int is_dir = (list[i].attr & 0x10) ? 1 : 0;
                    int slash = is_dir && !is_arcade_sys(sys_id) &&
                                strcmp(sys_id, "enterprise") != 0 &&
                                strcmp(sys_id, "ms1504") != 0;
                    if (len > (54 - (slash ? 1 : 0))) len = 54 - (slash ? 1 : 0);
                    memcpy(buf, list[i].name, len);
                    if (slash) buf[len++] = '/';
                    buf[len] = 0;
                    fb_puts_s(80, y, buf, 1, clr);
                    y += ROW_H;
                }
            }

            fb_puts(60, FOOTER_Y, "  ^v : select    Enter : load    ESC : back", 0x00888888);
            fb_flush();
            dirty = 0;
        }

        int k = usb_input_poll();   // неблокирующий: клавиатура/геймпад с автоповтором
        if (!k) {
            // нет нажатия — ждём ~1 кадр (16 мс) и НЕ перерисовываем
            udelay(16000);
            continue;
        }
        if (k == 82 || k == 26) {   // Up / W — по кольцу
            if (n <= 1) { dirty = 1; }
            else { cursor = (cursor == 0) ? n - 1 : cursor - 1; }
            // двусторонний скролл: курсор всегда в видимой области
            if (cursor < scroll) scroll = cursor;
            if (cursor >= scroll + max_rows) scroll = cursor - max_rows + 1;
            dirty = 1;
        } else if (k == 81) {   // Down — по кольцу
            if (n <= 1) { dirty = 1; }
            else { cursor = (cursor >= n - 1) ? 0 : cursor + 1; }
            // двусторонний скролл
            if (cursor < scroll) scroll = cursor;
            if (cursor >= scroll + max_rows) scroll = cursor - max_rows + 1;
            dirty = 1;
        } else if (k >= 4 && k <= 29) {
            // Поиск по первой букве (HID-сканкоды A=4..Z=29). Регистронезависимо.
            char c = (char)('a' + (k - 4));
            int found = -1;
            for (int i = 0; i < n; i++) {
                char ch = list[i].name[0];
                char l = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
                if (l == c) { found = i; break; }
            }
            if (found >= 0) {
                cursor = found;
                if (cursor < scroll) scroll = cursor;
                if (cursor >= scroll + max_rows) scroll = cursor - max_rows + 1;
            }
            dirty = 1;
        } else if (k == 40) {
            if (n > 0) {
                // r1362: имя копируем ДО fat_list/load_rom — scratch перезатрётся.
                char sel_name[FAT_NAME_LEN];
                int sl = (int)strlen(list[cursor].name);
                if (sl >= FAT_NAME_LEN) sl = FAT_NAME_LEN - 1;
                memcpy(sel_name, list[cursor].name, (size_t)sl);
                sel_name[sl] = 0;

                /* r1363: каталог текущего списка — источник файлов для F12
                   hot-swap; host заберёт его через emu_disk_poll(). */
                emu_disk_set_dir(path);

                int is_dir = (list[cursor].attr & 0x10) ? 1 : 0;
                int is_arc = is_arcade_sys(sys_id);
                int is_ep = (strcmp(sys_id, "enterprise") == 0);
                int is_ms = (strcmp(sys_id, "ms1504") == 0);   // папка = игра

                // Arkada: Enter на папке = запуск игры (host читает папку/zip).
                if (is_dir && is_arc) {
                    cheats_reset();
                    emu_clear_fb();
                    run_emulator(sys_id, NULL, 0, sel_name);
                    goto reload_list;
                }

                // Enterprise: Enter на папке = запуск игры из этой папки.
                if (is_dir && is_ep) {
                    char dirpath[256];
                    char main_name[FAT_NAME_LEN];
                    uint8_t* rom = 0;
                    uint32_t size = 0;
                    snprintf(dirpath, sizeof(dirpath), "%s/%s", path, sel_name);
                    if (ep_find_main(dirpath, sel_name, main_name,
                                     (int)sizeof(main_name)) == 0 &&
                        load_rom(dirpath, main_name, &rom, &size) == 0) {
                        cheats_reset();
                        ep_set_rom_dir(dirpath);
                        emu_clear_fb();
                        run_emulator(sys_id, rom, size, main_name);
                        goto reload_list;
                    }
                    // Нет главного файла — ниже войдём в каталог как раньше
                    // (доступ к коллекциям не теряем); sel_name уже скопирован.
                }

                // МС1504: папка = игра. Собираем игровой образ (дискета FAT12 или HDD
                // FAT16 0x80 для больших игр) и запускаем с вшитой BIOS PK300.
                if (is_dir && is_ms) {
                    char dirpath[256];
                    char main_name[FAT_NAME_LEN];
                    snprintf(dirpath, sizeof(dirpath), "%s/%s", path, sel_name);
                    if (m15_find_main(dirpath, sel_name, main_name,
                                      (int)sizeof(main_name)) == 0) {
                        int built = m15_build_floppy(dirpath, main_name);
                        if (built != 0) built = m15_build_hdd(dirpath, main_name);
                        if (built == 0) {
                            cheats_reset();
                            emu_clear_fb();
                            /* rom=NULL: вшитая BIOS PK300, образы дисков
                             * активны (host смонтирует A:/C:). */
                            run_emulator(sys_id, NULL, 0, main_name);
                            goto reload_list;
                        }
                    }
                    // Ошибка сборки — ниже войдём в каталог (как раньше).
                }

                // Enter на каталоге — входим в него (обновляем path)
                if (is_dir) {
                    int plen = (int)strlen(path);
                    if (sl > 0 && sl < 96 && plen + 1 + sl + 1 < (int)sizeof(path)) {
                        path[plen] = '/';
                        memcpy(path + plen + 1, sel_name, (size_t)sl);
                        path[plen + 1 + sl] = 0;
                        depth++;
                        n = list_dir_filtered(path, sys_id, list);
                        sort_entries(list, n);
                        cursor = 0; scroll = 0;
                        if (n <= 0) cursor = 0;
                        dirty = 1;
                        continue;
                    }
                }

                // Объявляем до goto (r625): C++ запрещает goto через
                // инициализацию переменной ("jump bypasses initialization").
                uint8_t* rom = 0;
                uint32_t size = 0;

                // Сброс чит-отметок прошлой сессии (raw-коды не
                // должны протекать в чужую игру).
                cheats_reset();

                if (is_arc) {
                    // Аркада (multi-file): элемент = папка игры или zip-файл;
                    // ROM-сет читает сам host по имени. Единый ROM не грузим.
                    emu_clear_fb();
                    run_emulator(sys_id, NULL, 0, sel_name);
                    // r625: после игры возвращаемся в браузер (не в главное меню).
                    goto reload_list;
                }

                if (load_rom(path, sel_name, &rom, &size) == 0) {
                    emu_clear_fb();
                    // r1354: Enterprise грузит и остальные файлы каталога игры
                    // (.com + .prg/.scr/...) — host-слою нужен путь каталога.
                    if (is_ep)
                        ep_set_rom_dir(path);
                    if (strcmp(sys_id, "atarist") == 0) {
                        /* r1577: Castaway читает образ диска с SD по пути, а
                         * sel_name — только имя файла (без каталога). */
                        char cw_img[FAT_NAME_LEN + 16];
                        snprintf(cw_img, sizeof(cw_img), "%s/%s", path, sel_name);
                        run_emulator(sys_id, rom, size, cw_img);
                    } else {
                        run_emulator(sys_id, rom, size, sel_name);
                    }
                    // r625: после игры возвращаемся в браузер (не в главное меню).
                    goto reload_list;
                } else {
                    fb_clear();
                    fb_text_center("Failed to load ROM", 200, 2, 0x00FF4444);
                    fb_flush();
                    input_wait();
                }
                // r625: неудача загрузки тоже возвращает в браузер (не выходим).
                goto reload_list;
            }
        } else if (k == 41) {
            // r1354: ESC в подпапке — выходим на уровень выше
            if (depth > 0) {
                char* slash = path + strlen(path);
                while (slash > path && *slash != '/') slash--;
                if (slash > path) *slash = 0;   // отрезать "/имя"
                depth--;
                n = list_dir_filtered(path, sys_id, list);
                sort_entries(list, n);
                cursor = 0; scroll = 0;
                if (n <= 0) cursor = 0;
                dirty = 1;
                continue;
            }
            return;
        } else if (k == 42 || k == 76) {
            // Backspace (42) / Delete (76) — удалить ROM (двойное подтверждение)
            if (n > 0) {
                // Копируем имя ДО удаления — fat_delete_file затрёт g_scratch_dir
                char del_name[FAT_NAME_LEN];
                int dl = strlen(list[cursor].name);
                if (dl >= FAT_NAME_LEN) dl = FAT_NAME_LEN - 1;
                memcpy(del_name, list[cursor].name, dl);
                del_name[dl] = 0;

                // Подтверждение 1: намерение
                fb_clear();
                fb_puts_s(60, 100, "Delete this ROM?", 2, 0x00FFAA00);
                fb_puts_s(60, 140, del_name, 1, 0x00FFFFFF);
                fb_puts_s(60, 180, "", 1, 0x00FFFFFF);
                fb_puts_s(80, 220, "  Enter: continue    ESC: cancel", 1, 0x00888888);
                fb_flush();

                int confirm = input_wait();
                if (confirm == 41) { dirty = 1; continue; }   // ESC
                if (confirm != 40) { dirty = 1; continue; }

                // Подтверждение 2: финальное
                fb_clear();
                fb_puts_s(60, 100, "Are you SURE?", 2, 0x00FF4444);
                fb_puts_s(60, 140, del_name, 1, 0x00FFFFFF);
                fb_puts_s(60, 180, "This will delete the file from SD", 1, 0x00FFFFFF);
                fb_puts_s(60, 200, "and is not reversible.", 1, 0x00FFFFFF);
                fb_puts_s(80, 240, "  Enter: DELETE    ESC: cancel", 1, 0x00888888);
                fb_flush();

                // r1271 (RT-01): отпускание Enter/пада ДО второго диалога. Иначе
                // автоповтор клавиатуры (350 мс) пробивает «Are you SURE?» и файл
                // удаляется одним удержанием клавиши.
                usb_wait_release_all();
                confirm = input_wait();
                if (confirm == 41) { dirty = 1; continue; }   // ESC
                if (confirm != 40) { dirty = 1; continue; }

                usb_wait_release_all();   // r1271: и перед самой записью на SD
                int r = fat_delete_file(path, del_name);
                if (r == 0) {
                    // перечитываем список
                    n = list_dir_filtered(path, sys_id, list);
                    sort_entries(list, n);
                    // r547: после удаления последнего файла n=0 давал
                    // cursor = n-1 = -1 (исправлялось следующей строкой,
                    // но хрупко) — теперь явная ветка для пустого списка.
                    if (n <= 0) cursor = 0;
                    else if (cursor >= n) cursor = n - 1;
                } else {
                    // r1644 (A-17): раньше браузер молча закрывался — теперь
                    // показываем ошибку и остаёмся в списке.
                    fb_clear();
                    fb_puts_s(60, 100, "Delete failed", 2, 0x00FF4444);
                    fb_puts_s(60, 140, del_name, 1, 0x00FFFFFF);
                    fb_puts_s(80, 220, "  Enter/ESC: back", 1, 0x00888888);
                    fb_flush();
                    input_wait();
                    dirty = 1;
                    continue;
                }
                dirty = 1;
            }
        }

reload_list:
        // r625: вернулись из эмулятора — перечитываем список ROM (файлы могли
        // измениться/удалиться) и остаёмся в браузере текущей системы.
        dirty = 1;
        if (n > 0) { cursor = (cursor >= n) ? n - 1 : cursor; }
        n = list_dir_filtered(path, sys_id, list);
        sort_entries(list, n);
        if (n <= 0) cursor = 0;
        else if (cursor >= n) cursor = n - 1;
        if (cursor < scroll) scroll = cursor;
        if (cursor >= scroll + max_rows) scroll = cursor - max_rows + 1;
    }
}
