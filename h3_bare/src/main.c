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

int h3_de2_init(struct display_timing *timing, uint32_t fbbase);
void h3_hs_timer_init(void);
void udelay(uint32_t d);

#define FB_ADDR 0x5F900000

void main(void) {
    int sd_ok = 0;

    uart_init();
    uart_puts("\nMultiTool Retro boot\n");

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
    if (sd_ok) uart_puts("SD ready\n");
    else uart_puts("SD not available\n");

    for (;;) {
        int sel = menu_run();
        if (sel < 0) continue;

        if (systems[sel].status != STATUS_READY) {
            fb_clear();
            fb_text_center("In development", 200, 2, 0x00FFAA00);
            fb_text_center("Press any key to return", 280, 1, 0x00AAAAAA);
            fb_flush();
            for (;;) {
                if (uart_rx_ready()) { uart_getc(); break; }
                if (usb_kbd_poll()) break;
                udelay(10000);
            }
            continue;
        }

        if (!sd_ok) {
            fb_clear();
            fb_text_center("SD card not ready", 200, 2, 0x00FF0000);
            fb_text_center("Insert SD card with /roms/<sys>/ ROMs", 250, 1, 0x00FFFFFF);
            fb_flush();
            for (;;) {
                if (uart_rx_ready()) { uart_getc(); break; }
                if (usb_kbd_poll()) break;
                udelay(10000);
            }
            continue;
        }

        rom_browser_run(systems[sel].id, systems[sel].name);
    }
}