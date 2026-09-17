#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "fb_text.h"
#include "fat.h"
#include "uart.h"
#include "usb_kbd.h"
#include "emu.h"
#include "cheatdb.h"

extern int printf(const char* fmt, ...);

#define PHYS_W 1024
#define PHYS_H 600
#define ROW_H 18
#define FOOTER_Y (PHYS_H - 30)

#define ROM_BUF 0x50000000
#define ROM_MAX (24 * 1024 * 1024)

static int load_rom(const char* path, const char* name, uint8_t** rom, uint32_t* size) {
    fat_entry_t f;
    if (!fat_find(path, name, &f)) { printf("load_rom: not found %s/%s\n", path, name); return -1; }
    if (f.size == 0 || f.size > ROM_MAX) { printf("load_rom: bad size %u\n", f.size); return -1; }
    uint8_t* buf = (uint8_t*)ROM_BUF;
    printf("load_rom: %s/%s size=%u cl=%u\n", path, name, f.size, f.first_cluster);
    int r = fat_read_file(&f, 0, buf, f.size);
    printf("load_rom: got %d bytes\n", r);
    // SD-DMA писала в DRAM, а D-cache (write-back, on) содержит старые
    // данные — надо инвалидировать, чтобы CPU читал настоящий ROM
    uint32_t a = (uint32_t)buf & ~0x1Fu;
    uint32_t end = a + f.size + 32;
    for (; a < end; a += 32)
        __asm volatile("mcr p15, 0, %0, c7, c6, 1" :: "r"(a));
    __asm volatile("dsb" ::: "memory");
    *rom = buf; *size = f.size;
    return 0;
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
    return NULL;
}

// ---- меню читов для выбранной игры ----
// Показывает список читов из базы, Enter — отметить/снять, стрелки — выбор.
// Возвращает 1 = применять читы (игру запускать), 0 = отмена (не запускать).
static int cheat_menu_run(const char* sys_id, const char* rom_name) {
    const char* folder = cheat_folder_for_id(sys_id);
    if (!folder) return 1;  // для системы читов нет — не мешаем запуску

    // грузим читы из базы
    int cnt = cheats_load(folder, rom_name);
    if (cnt <= 0)
        return 1;  // читов для этой игры нет — просто запускаем

    int cursor = 0;
    int scroll = 0;
    int max_rows = (FOOTER_Y - 140) / ROW_H;

    for (;;) {
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
        for (int i = scroll; i < cnt && i < scroll + max_rows; i++) {
            int sel = (i == cursor);
            uint32_t clr = sel ? 0x00FFFF00 : 0x00FFFFFF;
            if (cheats_enabled(i) && !sel) clr = 0x0000FF66;
            if (cheats_enabled(i) && sel)  clr = 0x0000FF66;
            if (sel) fb_fill_rect(50, y - 2, PHYS_W - 100, ROW_H, 0x00181818);
            // маркер включено
            char line[64];
            line[0] = cheats_enabled(i) ? '*' : ' ';
            line[1] = ' ';
            int dl = strlen(cheats_desc(i) ? cheats_desc(i) : "");
            if (dl > 51) dl = 51;
            memcpy(line + 2, cheats_desc(i) ? cheats_desc(i) : "", dl);
            line[2 + dl] = 0;
            fb_puts_s(80, y, line, 1, clr);
            // код мелким серым
            char cc[40];
            int ccl = strlen(cheats_code(i) ? cheats_code(i) : "");
            if (ccl > 36) ccl = 36;
            memcpy(cc, cheats_code(i) ? cheats_code(i) : "", ccl); cc[ccl] = 0;
            fb_puts_s(560, y, cc, 1, 0x00888888);
            y += ROW_H;
        }

        char foot[128];
        snprintf(foot, sizeof(foot), "  ^v:sel  Enter:toggle  C:all  X:none  ESC:launch  Backspace:back");
        fb_puts(60, FOOTER_Y, foot, 0x00888888);
        fb_flush();

        int k = input_wait();
        if (k == 82 || k == 'w') {
            if (cursor > 0) cursor--;
            if (cursor < scroll) scroll = cursor;
        } else if (k == 81 || k == 's') {
            if (cursor < cnt - 1) cursor++;
            if (cursor >= scroll + max_rows) scroll = cursor - max_rows + 1;
        } else if (k == 40 || k == '\n' || k == '\r' || k == 44) {
            cheats_toggle(cursor);   // Enter или Space
        } else if (k == 8 || k == 42 || k == 76) {
            // C — все, X — никакие (backspace удалить отдельно нет)
            // Используем: C = все вкл, X = все выкл
            // (C=HID 6, X=HID 27)
        } else if (k == 6) {  // C
            for (int i = 0; i < cnt; i++) cheats_set_enabled(i, 1);
        } else if (k == 27) { // X
            for (int i = 0; i < cnt; i++) cheats_set_enabled(i, 0);
        } else if (k == 41) {
            return 1;  // ESC — запускаем с текущими отметками
        } else if (k == 42) {
            return 1;  // Backspace = тоже запуск (как в списке ROM)
        }
    }
}

void rom_browser_run(const char *sys_id, const char *sys_name, const char *rom_dir) {
    (void)sys_id;   // диспетчер эмуляторов по sys_id появится позже
    // rom_dir — имя папки с SD (FAT_NAME_LEN до 127 символов)
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

    for (;;) {
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

        fb_puts(60, FOOTER_Y, "  ^v : select    Enter : load    ESC : back", 0x00888888);
        fb_flush();

        int k = input_wait();
        if (k == 82 || k == 'w') {
            if (cursor > 0) cursor--;
            if (cursor < scroll) scroll = cursor;
        } else if (k == 81 || k == 's') {
            if (cursor < n - 1) cursor++;
            if (cursor >= scroll + max_rows) scroll = cursor - max_rows + 1;
} else if (k == 22) {
            // S — меню читов для выбранной игры, потом назад в список
            if (n > 0) {
                char sel_name[FAT_NAME_LEN];
                int sl = strlen(list[cursor].name);
                if (sl >= FAT_NAME_LEN) sl = FAT_NAME_LEN - 1;
                memcpy(sel_name, list[cursor].name, sl);
                sel_name[sl] = 0;
                cheat_menu_run(sys_id, sel_name);
            }
        } else if (k == 40 || k == '\n' || k == '\r') {
 if (n > 0) {
                // Копируем имя ДО load_rom — fat_find внутри перезатрёт g_scratch_dir
                char sel_name[FAT_NAME_LEN];
                int sl = strlen(list[cursor].name);
                if (sl >= FAT_NAME_LEN) sl = FAT_NAME_LEN - 1;
                memcpy(sel_name, list[cursor].name, sl);
                sel_name[sl] = 0;

                uint8_t* rom = 0;
                uint32_t size = 0;
                if (load_rom(path, sel_name, &rom, &size) == 0) {
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
                    else if (strcmp(sys_id, "lynx") == 0)
                        emu_run_lynx(rom, size, sel_name);
                    else if (strcmp(sys_id, "ngp") == 0)
                        emu_run_ngp(rom, size, sel_name);
                    else if (strcmp(sys_id, "megadrive") == 0)
                        emu_run_megadrive(rom, size, sel_name);
                    else {
                        fb_clear();
                        fb_text_center("System not implemented yet", 200, 2, 0x00FFAA00);
                        fb_text_center("Press any key", 250, 1, 0x00FFFFFF);
                        fb_flush();
                        input_wait();
                    }
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
                if (confirm == 41) continue;   // ESC
                if (confirm != 40 && confirm != '\n' && confirm != '\r') continue;

                // Подтверждение 2: финальное
                fb_clear();
                fb_puts_s(60, 100, "Are you SURE?", 2, 0x00FF4444);
                fb_puts_s(60, 140, del_name, 1, 0x00FFFFFF);
                fb_puts_s(60, 180, "This will delete the file from SD", 1, 0x00FFFFFF);
                fb_puts_s(60, 200, "and is not reversible.", 1, 0x00FFFFFF);
                fb_puts_s(80, 240, "  Enter: DELETE    ESC: cancel", 1, 0x00888888);
                fb_flush();

                confirm = input_wait();
                if (confirm == 41) continue;   // ESC
                if (confirm != 40 && confirm != '\n' && confirm != '\r') continue;

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
            }
        }
    }
}