#include <stdint.h>
#include <string.h>
#include "fb_text.h"
#include "fat.h"
#include "uart.h"
#include "usb_kbd.h"
#include "emu.h"

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

void rom_browser_run(const char *sys_id, const char *sys_name, const char *rom_dir) {
    (void)sys_id;   // диспетчер эмуляторов по sys_id появится позже
    // rom_dir — имя папки с SD (FAT_NAME_LEN до 127 символов)
    char path[FAT_NAME_LEN + 16];
    int pl = 0;
    const char* pfx = "/roms/";
    while (*pfx && pl < (int)sizeof(path) - 1) path[pl++] = *pfx++;
    for (const char* s = rom_dir; *s && pl < (int)sizeof(path) - 1; s++) path[pl++] = *s;
    path[pl] = 0;

    fat_entry_t list[FAT_MAX_ENTRIES];
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
            char ext[24] = "Supports: .bin .rom .sms";
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
        } else if (k == 40 || k == '\n' || k == '\r') {
if (n > 0) {
                uint8_t* rom = 0;
                uint32_t size = 0;
                if (load_rom(path, list[cursor].name, &rom, &size) == 0) {
                    emu_clear_fb();
                    if (strcmp(sys_id, "a2600") == 0)
                        emu_run_a2600_mcume(rom, size, list[cursor].name);
                    else if (strcmp(sys_id, "a7800") == 0)
                        emu_run_a7800(rom, size, list[cursor].name);
                    else if (strcmp(sys_id, "a5200") == 0)
                        emu_run_a5200(rom, size, list[cursor].name);
                    else if (strcmp(sys_id, "sms") == 0)
                        emu_run_sms(rom, size, list[cursor].name);
                    else if (strcmp(sys_id, "nes") == 0)
                        emu_run_nes(rom, size, list[cursor].name);
                    else if (strcmp(sys_id, "snes") == 0)
                        emu_run_snes(rom, size, list[cursor].name);
                    else if (strcmp(sys_id, "portfolio") == 0)
                        emu_run_portfolio(rom, size, list[cursor].name);
                    else if (strcmp(sys_id, "gameboy") == 0)
                        emu_run_gameboy(rom, size, list[cursor].name);
                    else if (strcmp(sys_id, "lynx") == 0)
                        emu_run_lynx(rom, size, list[cursor].name);
                    else if (strcmp(sys_id, "megadrive") == 0)
                        emu_run_megadrive(rom, size, list[cursor].name);
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
                // Подтверждение 1: намерение
                fb_clear();
                fb_puts_s(60, 100, "Delete this ROM?", 2, 0x00FFAA00);
                fb_puts_s(60, 140, list[cursor].name, 1, 0x00FFFFFF);
                fb_puts_s(60, 180, "", 1, 0x00FFFFFF);
                fb_puts_s(80, 220, "  Enter: continue    ESC: cancel", 1, 0x00888888);
                fb_flush();

                int confirm = input_wait();
                if (confirm == 41) continue;   // ESC
                if (confirm != 40 && confirm != '\n' && confirm != '\r') continue;

                // Подтверждение 2: финальное
                fb_clear();
                fb_puts_s(60, 100, "Are you SURE?", 2, 0x00FF4444);
                fb_puts_s(60, 140, list[cursor].name, 1, 0x00FFFFFF);
                fb_puts_s(60, 180, "This will delete the file from SD", 1, 0x00FFFFFF);
                fb_puts_s(60, 200, "and is not reversible.", 1, 0x00FFFFFF);
                fb_puts_s(80, 240, "  Enter: DELETE    ESC: cancel", 1, 0x00888888);
                fb_flush();

                confirm = input_wait();
                if (confirm == 41) continue;   // ESC
                if (confirm != 40 && confirm != '\n' && confirm != '\r') continue;

                int r = fat_delete_file(path, list[cursor].name);
                if (r == 0) {
                    // перечитываем список
                    n = fat_list(path, list, FAT_MAX_ENTRIES);
                    sort_entries(list, n);
                    if (cursor >= n) cursor = n - 1;
                    if (cursor < 0) cursor = 0;
                } else {
                    fb_clear();
                    fb_text_center("Delete failed!", 200, 2, 0x00FF4444);
                    fb_flush();
                    input_wait();
                }
            }
        }
    }
}