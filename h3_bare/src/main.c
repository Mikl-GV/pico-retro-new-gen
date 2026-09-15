#include <stdint.h>
#include <string.h>
#include "h3.h"
#include "uart.h"
#include "display_timing.h"
#include "sd.h"
#include "fat.h"
#include "usb_kbd.h"
#include "fb_text.h"
#include "systems.h"
#include "menu.h"
#include "rom_browser.h"
#include "settings.h"
#include "emu.h"
#include "led.h"

extern int printf(const char* fmt, ...);

int h3_de2_init(struct display_timing *timing, uint32_t fbbase);
void h3_hs_timer_init(void);
void udelay(uint32_t d);

#define FB_ADDR 0x5F900000

void main(void) {
    int sd_ok = 0;

    uart_init();
    uart_puts("\nMultiTool Retro boot\n");

    led_init();
    led_set(0);

    h3_hs_timer_init();

    struct display_timing timing;
    memset(&timing, 0, sizeof(timing));
    timing.hdmi_monitor = 0;
    timing.pixelclock.typ = 51200000;
    timing.hactive.typ = 1024; timing.hfront_porch.typ = 40;
    timing.hback_porch.typ = 248; timing.hsync_len.typ = 32;
    timing.vactive.typ = 600; timing.vfront_porch.typ = 1;
    timing.vback_porch.typ = 31; timing.vsync_len.typ = 3;
    timing.flags = (DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_HIGH);

    if (h3_de2_init(&timing, FB_ADDR) != 0) {
        uart_puts("HDMI FAILED\n");
        while (1) udelay(1000000);
    }
    uart_puts("HDMI ok\n");

    fb_clear();
    fb_flush();

    uart_puts("USB kbd init...\n");
    if (usb_kbd_init() == 0) uart_puts("USB kbd ready\n");
    else uart_puts("USB kbd not found\n");

    uart_puts("SD init...\n");
    sd_ok = (sd_init() == 0) && (fat_init() == 0);
    if (sd_ok) {
        uart_puts("SD ready\n");
        uart_puts("ROMs:\n");
        fat_entry_t dirs[FAT_MAX_ENTRIES];
        int n = fat_list("/roms", dirs, FAT_MAX_ENTRIES);
        for (int i = 0; i < n; i++)
            if (dirs[i].size == 0) {
                uart_puts("  ");
                uart_puts(dirs[i].name);
                uart_puts("\n");
            }
    } else {
        uart_puts("SD not available\n");
    }

    for (;;) {
        int sel = menu_run();
        if (sel == -2) {
            settings_run();
            continue;
        }
        if (sel == -3) {
            menu_help();
            continue;
        }
        if (sel == -4) {
            menu_about();
            continue;
        }
        if (sel < 0) continue;

        const char* id   = menu_get_id(sel);
        const char* name = menu_get_name(sel);
        const char* dir  = menu_get_dir(sel);

        // Самодостаточные системы (BIOS вшит, ROM с SD не нужен) —
        // запускаются напрямую из меню, минуя браузер ROM.
        if (strcmp(id, "portfolio") == 0) {
            emu_run_portfolio(NULL, 0, name);
            continue;
        }

        rom_browser_run(id, name, dir);
    }
}