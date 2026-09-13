#include <stdint.h>
#include <string.h>
#include "fb_text.h"
#include "fat.h"
#include "uart.h"
#include "usb_kbd.h"

#define PHYS_W 1024
#define PHYS_H 600
#define ROW_H 18
#define FOOTER_Y (PHYS_H - 30)

static void sort_entries(fat_entry_t *list, int n) {
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(list[i].name, list[j].name) > 0) {
                fat_entry_t t = list[i]; list[i] = list[j]; list[j] = t;
            }
}

static int input_wait(void) {
    for (;;) {
        if (uart_rx_ready()) return uart_getc();
        int k = usb_kbd_poll();
        if (k) return k;
    }
}

void rom_browser_run(const char *sys_id, const char *sys_name) {
    char path[32];
    strcpy(path, "/roms/");
    strcat(path, sys_id);

    fat_entry_t list[FAT_MAX_ENTRIES];
    int n = fat_list(path, list, FAT_MAX_ENTRIES);
    sort_entries(list, n);

    int cursor = 0;
    int scroll = 0;
    int max_rows = (FOOTER_Y - 100) / ROW_H;

    for (;;) {
        fb_clear();
        fb_puts_s(60, 30, sys_name, 2, 0x00FF0000);
        fb_fill_rect(60, 60, 200, 2, 0x00FFFFFF);

        if (n <= 0) {
            fb_puts_s(60, 120, "No ROMs found on SD card", 1, 0x00FF4444);
            fb_puts_s(60, 140, "Put ROM files in:", 1, 0x00AAAAAA);
            fb_puts_s(60, 158, path, 1, 0x00AAAAAA);
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
                fb_clear();
                fb_text_center("Loading emulator...", 200, 2, 0x0000FF00);
                fb_text_center("Not yet implemented", 250, 1, 0x00FFFFFF);
                fb_text_center("Press any key to return", 300, 1, 0x00888888);
                fb_flush();
                while (input_wait()) {}
                return;
            }
        } else if (k == 41 || k == 27 || k == 'q') {
            return;
        }
    }
}