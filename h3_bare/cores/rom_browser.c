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
    int r = fat_read_file(&f, 0, buf, f.size);
    printf("load_rom: got %d bytes\n", r);
    // r0.365 (H4 аудита): недопрочитанный ROM (ошибка SD) — не запускать:
    // эмулятор стартовал с обрезанной прошивкой и молча падал/глючил.
    if (r < 0 || (uint32_t)r != f.size) { printf("load_rom: short read\n"); return -1; }
    // sd.c читает PIO: CPU пишет данные через обычные store, поэтому
    // dirty-линии D-cache (write-back) могут не дойти до DRAM. Инвалидация
    // (DCIMVAC) НЕ подходит — она отбросит грязные данные. Используем
    // clean+invalidate (DCCIMVAC): сливаем кэш в DRAM и делаем линии
    // невалидными, чтобы CPU читал настоящий ROM.
    uint32_t a = (uint32_t)buf & ~0x1Fu;
    uint32_t end = a + f.size + 32;
    for (; a < end; a += 32)
        __asm volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"(a));
    __asm volatile("dsb" ::: "memory");
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
        emu_run_a2600_mcume(rom, size, sel_name);
    else if (strcmp(sys_id, "a7800") == 0)
        emu_run_a7800(rom, size, sel_name);
    else if (strcmp(sys_id, "a5200") == 0)
        emu_run_a5200(rom, size, sel_name);
    else if (strcmp(sys_id, "sms") == 0)
        emu_run_sms(rom, size, sel_name);
    else if (strcmp(sys_id, "gamegear") == 0)
        emu_run_gg(rom, size, sel_name);
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
    else if (strcmp(sys_id, "megadrive") == 0)
        emu_run_megadrive(rom, size, sel_name);
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
    else if (strcmp(sys_id, "fbneo") == 0)
        emu_run_fbneo(rom, size, sel_name);     // общий FBNeo-корень
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

static void sort_entries(fat_entry_t *list, int n) {
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(list[i].name, list[j].name) > 0) {
                fat_entry_t t = list[i]; list[i] = list[j]; list[j] = t;
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
    int n = fat_list(path, list, FAT_MAX_ENTRIES);
    sort_entries(list, n);
    printf("rom_browser: %s n=%d\n", path, n);
    // (пофайловый список убран из отладки — спамил дерево при каждом входе)

    int cursor = 0;
    int scroll = 0;
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
                    if (len > 54) len = 54;
                    memcpy(buf, list[i].name, len);
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
                // Копируем имя ДО load_rom — fat_find внутри перезатрёт g_scratch_dir
                char sel_name[FAT_NAME_LEN];
                int sl = strlen(list[cursor].name);
                if (sl >= FAT_NAME_LEN) sl = FAT_NAME_LEN - 1;
                memcpy(sel_name, list[cursor].name, sl);
                sel_name[sl] = 0;

                // Сброс чит-отметок прошлой сессии (raw-коды не
                // должны протекать в чужую игру).
                cheats_reset();

                if (strcmp(sys_id, "cps1") == 0 || strcmp(sys_id, "cps2") == 0 || strcmp(sys_id, "neogeo") == 0 || strcmp(sys_id, "toaplan") == 0 || strcmp(sys_id, "fbneo") == 0 || strcmp(sys_id, "cave") == 0 || strcmp(sys_id, "segasys") == 0) {
                    // Аркада (multi-file): элемент = папка игры или zip-файл;
                    // ROM-сет читает сам host по имени. Единый ROM не грузим.
                    emu_clear_fb();
                    run_emulator(sys_id, NULL, 0, sel_name);
                    return;
                }

                uint8_t* rom = 0;
                uint32_t size = 0;
                if (load_rom(path, sel_name, &rom, &size) == 0) {
                    emu_clear_fb();
                    run_emulator(sys_id, rom, size, sel_name);
                } else {
                    fb_clear();
                    fb_text_center("Failed to load ROM", 200, 2, 0x00FF4444);
                    fb_flush();
                    input_wait();
                }
                return;
            }
        } else if (k == 41) {
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

                confirm = input_wait();
                if (confirm == 41) { dirty = 1; continue; }   // ESC
                if (confirm != 40) { dirty = 1; continue; }

                int r = fat_delete_file(path, del_name);
                if (r == 0) {
                    // перечитываем список
                    n = fat_list(path, list, FAT_MAX_ENTRIES);
                    sort_entries(list, n);
                    // r547: после удаления последнего файла n=0 давал
                    // cursor = n-1 = -1 (исправлялось следующей строкой,
                    // но хрупко) — теперь явная ветка для пустого списка.
                    if (n <= 0) cursor = 0;
                    else if (cursor >= n) cursor = n - 1;
                } else {
                    // Буфер g_scratch_dir испорчен — выходим в меню
                    return;
                }
                dirty = 1;
            }
        }
    }
}
