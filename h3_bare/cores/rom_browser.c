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

// запуск эмулятора по sys_id (общая логика для Enter и меню читов)
static void run_emulator(const char* sys_id, uint8_t* rom, uint32_t size,
                         const char* sel_name) {
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
    else {
        fb_clear();
        fb_text_center("System not implemented yet", 200, 2, 0x00FFAA00);
        fb_text_center("Press any key", 250, 1, 0x00FFFFFF);
        fb_flush();
        input_wait();
    }
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
    }
}

// ---- карта sys_id -> папка в /cheats/ (имена как в базе libretro-database) ----
static const char* cheat_folder_for_id(const char* sys_id) {
    if (strcmp(sys_id, "a2600") == 0)   return "Atari - 2600";
    if (strcmp(sys_id, "a5200") == 0)   return "Atari - 5200";
    if (strcmp(sys_id, "a7800") == 0)   return "Atari - 7800";
    if (strcmp(sys_id, "jaguar") == 0)  return "Atari - Jaguar";
    if (strcmp(sys_id, "lynx") == 0)    return "Atari - Lynx";
    if (strcmp(sys_id, "coleco") == 0)  return "Coleco - ColecoVision";
    if (strcmp(sys_id, "gameboy") == 0 || strcmp(sys_id, "gbc") == 0)
        return "Nintendo - Game Boy";
    if (strcmp(sys_id, "gba") == 0)     return "Nintendo - Game Boy Advance";
    if (strcmp(sys_id, "gbc") == 0)     return "Nintendo - Game Boy";   // в базе GBC нет отдельно? есть "Nintendo - Game Boy Color"
    if (strcmp(sys_id, "nes") == 0)     return "Nintendo - Nintendo Entertainment System";
    if (strcmp(sys_id, "snes") == 0)    return "Nintendo - Super Nintendo Entertainment System";
    if (strcmp(sys_id, "gamegear") == 0) return "Sega - Game Gear";
    if (strcmp(sys_id, "sms") == 0)     return "Sega - Master System - Mark III";
    if (strcmp(sys_id, "megadrive") == 0) return "Sega - Mega Drive - Genesis";
    if (strcmp(sys_id, "ngp") == 0)     return "SNK - Neo Geo Pocket";
    if (strcmp(sys_id, "msx") == 0)     return "Microsoft - MSX (fMSX)";
    if (strcmp(sys_id, "ms1504") == 0)  return "Microsoft - MSX (fMSX)";
    return 0;
}

// ---- Экранный ввод кода чита (как на картридже Game Genie) ----
// Крестовина геймпада: ВВЕРХ/ВНИЗ — выбор слота, ВЛЕВО/ВПРАВО — выбор
// символа, A — вставить символ в слот, Start/Mode — подтвердить (ввести),
// B — отмена. Клавиатура тоже работает (A-Z 0-9 - :, Enter, Backspace, ESC).
// Возвращает 1 если код добавлен, 0 если отменено.
static int cheat_code_input(void) {
    char buf[CHEAT_CODE_LEN] = {0};
    static const char syms[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-:";
    const int n_syms = (int)sizeof(syms) - 1;
    const int SLOTS = 12;      // GG 11 символов / Raw до ~11 — хватает
    int bl = 0;                // число заполненных слотов
    int pos = 0;               // выбранный слот
    int sym = 0;               // выбранный символ
    int dirty = 1;

    usb_pad_wait_release();    // удержанная кнопка не должна «доехать» в этот экран

    for (;;) {
        if (dirty) {
            fb_draw_stars();
            fb_puts_s(60, 55, "Enter cheat code (Gamepad/Keyboard)", 2, 0x00FFAA00);
            fb_fill_rect(60, 90, 560, 2, 0x00FFFFFF);

            // ---- строка слотов ----
            const int slot_w = 34, slot_h = 40;
            const int x0 = 60, y0 = 130;
            for (int i = 0; i < SLOTS; i++) {
                int sx = x0 + i * (slot_w + 4);
                uint32_t filled = (i < bl);
                uint32_t bg = (i == pos) ? 0x00303030 : (filled ? 0x00181818 : 0x000C0C0C);
                fb_fill_rect(sx, y0, slot_w, slot_h, bg);
                if (i == pos) {
                    fb_fill_rect(sx - 2, y0 - 2, slot_w + 4, 2, 0x00FFFF00);
                    fb_fill_rect(sx - 2, y0 + slot_h, slot_w + 4, 2, 0x00FFFF00);
                    fb_fill_rect(sx - 2, y0, 2, slot_h, 0x00FFFF00);
                    fb_fill_rect(sx + slot_w, y0, 2, slot_h, 0x00FFFF00);
                }
                if (filled) {
                    char sc[2] = { buf[i], 0 };
                    fb_puts_s(sx + (slot_w - 16) / 2, y0 + (slot_h - 16) / 2, sc, 2, 0x00FFFFFF);
                } else {
                    fb_puts_s(sx + (slot_w - 16) / 2, y0 + (slot_h - 16) / 2, "_", 2, 0x00666666);
                }
            }
            fb_puts_s(x0, y0 - 24, "A:set  Up/Dn:slot  L/R:char  Start:OK  B:back", 1, 0x00888888);

            // ---- палитра символов ----
            const int py = y0 + slot_h + 34;
            const int cw = 24, chh = 26, per_row = 13;
            for (int i = 0; i < n_syms; i++) {
                int sx = 60 + (i % per_row) * (cw + 4);
                int sy = py + (i / per_row) * (chh + 4);
                fb_fill_rect(sx, sy, cw, chh, (i == sym) ? 0x00505000 : 0x00181818);
                if (i == sym) {
                    fb_fill_rect(sx - 1, sy - 1, cw + 2, 2, 0x00FFFF00);
                    fb_fill_rect(sx - 1, sy + chh, cw + 2, 2, 0x00FFFF00);
                }
                char sc[2] = { syms[i], 0 };
                fb_puts_s(sx + (cw - 16) / 2, sy + (chh - 16) / 2, sc, 2,
                          (i == sym) ? 0x00FFFF00 : 0x00FFFFFF);
            }
            char sinfo[64];
            snprintf(sinfo, sizeof(sinfo), "Symbol: %c   Slot: %d/%d", syms[sym], pos + 1, SLOTS);
            fb_puts_s(60, py + ((n_syms + per_row - 1) / per_row) * (chh + 4) + 8, sinfo, 1, 0x00AAAAAA);

            fb_flush();
            dirty = 0;
        }

        // ---- геймпад ----
        uint16_t pr = usb_pad_just_pressed();
        if (pr & 0x0001) { if (pos > 0) pos--; dirty = 1; continue; }        // Up
        if (pr & 0x0002) { if (pos < SLOTS - 1) pos++; dirty = 1; continue; } // Down
        if (pr & 0x0004) { sym = (sym + n_syms - 1) % n_syms; dirty = 1; continue; } // Left
        if (pr & 0x0008) { sym = (sym + 1) % n_syms; dirty = 1; continue; }         // Right
        if (pr & 0x0010) { // A — вставить символ в текущий слот
            buf[pos] = syms[sym];
            if (pos >= bl) bl = pos + 1;
            if (pos < SLOTS - 1) pos++;
            dirty = 1;
            continue;
        }
        if (pr & 0x0080 || pr & 0x0800) { // Start / Mode — подтвердить
            if (bl > 0) {
                buf[bl] = 0;
                int ok = cheats_manual_add(buf, "Manual");
                printf("cheat: manual add '%s' %s\n", buf, ok ? "OK" : "list full");
                return ok;
            }
            dirty = 1;
            continue;
        }
        if (pr & 0x0020) return 0;        // B — отмена

        // ---- клавиатура ----
        int k = usb_kbd_poll();
        if (!k) { h3_hs_timer_delay(16000); continue; }
        if (k == 41) return 0;                                // ESC — отмена
        if (k == 40 || k == '\n' || k == '\r') {              // Enter — подтвердить
            if (bl > 0) {
                buf[bl] = 0;
                int ok = cheats_manual_add(buf, "Manual");
                printf("cheat: manual add '%s' %s\n", buf, ok ? "OK" : "list full");
                return ok;
            }
            continue;
        }
        if (k == 42) { if (bl > 0) { bl--; buf[bl] = 0; if (pos > bl) pos = bl; } dirty = 1; continue; } // Backspace
        if (k == 82) { if (pos > 0) pos--; dirty = 1; continue; }   // Up
        if (k == 81) { if (pos < SLOTS - 1) pos++; dirty = 1; continue; } // Down
        if (k == 80) { sym = (sym + n_syms - 1) % n_syms; dirty = 1; continue; }
        if (k == 79) { sym = (sym + 1) % n_syms; dirty = 1; continue; }
        if (bl >= CHEAT_CODE_LEN - 1) continue;
        char ch = 0;
        if (k >= 4 && k <= 29)       ch = 'A' + (k - 4);
        else if (k >= 30 && k <= 39) ch = '0' + (k - 30);
        else if (k == 45)            ch = '-';
        else if (k == 51)            ch = ':';
        if (ch) {
            buf[pos] = ch;
            if (pos >= bl) bl = pos + 1;
            if (pos < SLOTS - 1) pos++;
            dirty = 1;
        }
    }
}

// ---- меню читов для выбранной игры ----
// Показывает список читов из базы, Enter — отметить/снять, стрелки — выбор.
// Последней строкой — ручной ввод кода. Возвращает 1 = запустить игру с
// читами (Start/Mode), 0 = отмена (ESC, назад в список).
static int cheat_menu_run(const char* sys_id, const char* rom_name) {
    const char* folder = cheat_folder_for_id(sys_id);
    if (!folder) return 0;  // для системы читов нет — просто назад в список

    // грузим читы из базы. Если .cht-файла для этой игры нет — cnt=0,
    // меню всё равно покажем: останется строка ручного ввода кода.
    int cnt = cheats_load(folder, rom_name);

    // Вход в меню читов: ждём полного отпускания геймпада, чтобы
    // зажатая кнопка (Mode, которой открыли меню) не сработала
    // мгновенно на первом пункте и не запустила игру.
    usb_pad_wait_release();

    int cursor = 0;
    int scroll = 0;
    int max_rows = (FOOTER_Y - 140) / ROW_H;
    int dirty = 1;   // перерисовать при входе и при изменении

    for (;;) {
        // Рендер только когда что-то изменилось — иначе мерцание
        // (перерисовка вхолостую быстрее кадра 60 Гц).
        if (dirty) {
            fb_draw_stars();
            fb_puts_s(60, 30, "Cheats", 2, 0x00FFAA00);
            fb_fill_rect(60, 60, 200, 2, 0x00FFFFFF);
            // подпись: имя игры
            char cap[64];
            int cl = strlen(rom_name);
            if (cl > 52) cl = 52;
            memcpy(cap, rom_name, cl); cap[cl] = 0;
            fb_puts_s(60, 68, cap, 1, 0x00666666);

            int y = 90;
            int total_rows = cnt + 1;   // +1 строка ручного ввода
            for (int i = scroll; i < total_rows && i < scroll + max_rows; i++) {
                if (i == cnt) {
                    // строка ручного ввода кода
                    int sel = (i == cursor);
                    uint32_t clr = sel ? 0x00FFFF00 : 0x00777777;
                    if (sel) fb_fill_rect(50, y - 2, PHYS_W - 100, ROW_H, 0x00181818);
                    fb_puts_s(80, y, "== Manual code entry ==", 1, clr);
                    fb_puts_s(560, y, "Enter", 1, 0x00888888);
                    y += ROW_H;
                    continue;
                }
                int sel = (i == cursor);
                uint32_t clr = sel ? 0x00FFFF00 : 0x00FFFFFF;
                if (cheats_enabled(i) && !sel) clr = 0x0000FF66;
                if (cheats_enabled(i) && sel)  clr = 0x0000FF66;
                if (sel) fb_fill_rect(50, y - 2, PHYS_W - 100, ROW_H, 0x00181818);
                // маркер включено
                char line[64];
                line[0] = cheats_enabled(i) ? '*' : ' ';
                line[1] = ' ';
                // Описание режем до 44 символов (44*10px=440, x=80 -> конец 520,
                // поле кода начинается на 560 — не перекрывается)
                int dl = strlen(cheats_desc(i) ? cheats_desc(i) : "");
                if (dl > 44) dl = 44;
                memcpy(line + 2, cheats_desc(i) ? cheats_desc(i) : "", dl);
                line[2 + dl] = 0;
                fb_puts_s(80, y, line, 1, clr);
                // код мелким серым (хватает на все форматы: GG/PAR/Raw)
                char cc[40];
                int ccl = strlen(cheats_code(i) ? cheats_code(i) : "");
                if (ccl > 36) ccl = 36;
                memcpy(cc, cheats_code(i) ? cheats_code(i) : "", ccl); cc[ccl] = 0;
                fb_puts_s(560, y, cc, 1, 0x00888888);
                y += ROW_H;
            }

            char foot[128];
            snprintf(foot, sizeof(foot), "  ^v:sel  Enter:toggle  C:all  X:none  Start/Mode:launch  ESC:back");
            fb_puts(60, FOOTER_Y, foot, 0x00888888);
            fb_flush();
            dirty = 0;
        }

        // Геймпад: стрелки = навигация, A/Mode = отметить/запуск, Start = запуск
        // (только фронт нажатия — зажатая кнопка повторно не срабатывает)
        uint16_t pr = usb_pad_just_pressed();
        if (pr & 0x0800) return 1;                  // Mode -> запустить игру с читами
        if (pr & 0x0080) return 1;                  // Start -> запустить игру с читами
        if (pr & 0x0010) {
            if (cursor < cnt) cheats_toggle(cursor);
            else if (cheat_code_input()) { cnt = cheats_count(); if (cursor > cnt) cursor = cnt; }
            dirty = 1;
            continue;
        }
        if (pr & 0x0001 && cursor > 0) { cursor--; if (cursor < scroll) scroll = cursor; dirty = 1; continue; }
        if (pr & 0x0002 && cursor < cnt) { cursor++; if (cursor >= scroll + max_rows) scroll = cursor - max_rows + 1; dirty = 1; continue; }
        if (pr & 0x0004 && cursor > 0) { cursor--; if (cursor < scroll) scroll = cursor; dirty = 1; continue; }
        if (pr & 0x0008 && cursor < cnt) { cursor++; if (cursor >= scroll + max_rows) scroll = cursor - max_rows + 1; dirty = 1; continue; }

        // Клавиатура — ТОЛЬКО usb_kbd_poll (без usb_input_poll, который сам
        // мапит геймпад и конфликтует с pad_just_pressed выше)
        int k = usb_kbd_poll();
        if (k == 82 || k == 'w') {
            if (cursor > 0) cursor--;
            if (cursor < scroll) scroll = cursor;
            dirty = 1;
        } else if (k == 81 || k == 's') {
            if (cursor < cnt) cursor++;
            if (cursor >= scroll + max_rows) scroll = cursor - max_rows + 1;
            dirty = 1;
        } else if (k == 40 || k == '\n' || k == '\r' || k == 44) {
            // Enter или Space: на читах — вкл/выкл, на строке ввода — ввод кода
            if (cursor < cnt) cheats_toggle(cursor);
            else if (cheat_code_input()) { cnt = cheats_count(); if (cursor > cnt) cursor = cnt; }
            dirty = 1;
        } else if (k == 6) {  // C
            for (int i = 0; i < cnt; i++) cheats_set_enabled(i, 1);
            dirty = 1;
        } else if (k == 27) { // X
            for (int i = 0; i < cnt; i++) cheats_set_enabled(i, 0);
            dirty = 1;
        } else if (k == 41) {
            return 0;  // ESC — отмена, назад в список
        } else if (k == 42) {
            return 0;  // Backspace — тоже назад (не запуск)
        } else {
            // нет нажатия — ждём ~1 кадр, чтобы не мерцало
            h3_hs_timer_delay(16000);
        }
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
    for (int i = 0; i < n; i++)
        printf("  [%d] '%s' size=%u\n", i, list[i].name, (unsigned)list[i].size);

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
                char ext[48] = "Supports: .bin .rom .sms .ngp .ngc .npc";
                fb_puts_s(60, 185, ext, 1, 0x00666666);
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

            fb_puts(60, FOOTER_Y, "  ^v : select    Enter : load    S/Mode : cheats    ESC : back", 0x00888888);
            fb_flush();
            dirty = 0;
        }

        int k = usb_input_poll();   // неблокирующий: клавиатура/геймпад с автоповтором
        if (!k) {
            // нет нажатия — ждём ~1 кадр (16 мс) и НЕ перерисовываем
            h3_hs_timer_delay(16000);
            continue;
        }
        if (k == 82 || k == 'w') {
            if (cursor > 0) cursor--;
            if (cursor < scroll) scroll = cursor;
            dirty = 1;
        } else if (k == 81 || k == 's') {
            if (cursor < n - 1) cursor++;
            if (cursor >= scroll + max_rows) scroll = cursor - max_rows + 1;
            dirty = 1;
} else if (k == 22) {
            // S — меню читов для выбранной игры.
            // Возврат 1 = запустить игру с отмеченными читами
            // (Start/A/Mode/ESC в чит-меню), 0 = назад в список.
            if (n > 0) {
                char sel_name[FAT_NAME_LEN];
                int sl = strlen(list[cursor].name);
                if (sl >= FAT_NAME_LEN) sl = FAT_NAME_LEN - 1;
                memcpy(sel_name, list[cursor].name, sl);
                sel_name[sl] = 0;
                int saved_cursor = cursor;
                int launch = cheat_menu_run(sys_id, sel_name);
                // чит-меню могло перетереть g_scratch_dir (fat_list для /cheats) —
                // перечитываем список ROM заново (кроме случая, когда игра запущена)
                if (launch) {
                    fb_clear(); fb_flush();
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
                n = fat_list(path, list, FAT_MAX_ENTRIES);
                sort_entries(list, n);
                if (saved_cursor < n) cursor = saved_cursor;
                else cursor = (n > 0) ? n - 1 : 0;
                if (cursor < scroll) scroll = cursor;
                if (scroll > cursor) scroll = cursor;
                dirty = 1;
            }
        } else if (k == 40 || k == '\n' || k == '\r') {
 if (n > 0) {
                // Копируем имя ДО load_rom — fat_find внутри перезатрёт g_scratch_dir
                char sel_name[FAT_NAME_LEN];
                int sl = strlen(list[cursor].name);
                if (sl >= FAT_NAME_LEN) sl = FAT_NAME_LEN - 1;
                memcpy(sel_name, list[cursor].name, sl);
                sel_name[sl] = 0;

                // Обычный запуск (не через меню читов): сбрасываем читы
                // прошлой сессии, чтобы RAW-коды/отметки не протекли в чужую игру.
                cheats_reset();

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
        } else if (k == 42 || k == 76 || k == 49) {
            // Backspace / Delete / или "D" — удалить ROM (двойное подтверждение)
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
                if (confirm != 40 && confirm != '\n' && confirm != '\r') { dirty = 1; continue; }

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
                if (confirm != 40 && confirm != '\n' && confirm != '\r') { dirty = 1; continue; }

                int r = fat_delete_file(path, del_name);
                if (r == 0) {
                    // перечитываем список
                    n = fat_list(path, list, FAT_MAX_ENTRIES);
                    sort_entries(list, n);
                    if (cursor >= n) cursor = n - 1;
                    if (cursor < 0) cursor = 0;
                } else {
                    // Буфер g_scratch_dir испорчен — выходим в меню
                    return;
                }
                dirty = 1;
            }
        }
    }
}