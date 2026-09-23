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
#include "h3_hs_timer.h"
#include "led.h"
#include "sega_pad.h"
#include "remap.h"
#include "tft_drv.h"

extern int printf(const char* fmt, ...);

int h3_de2_init(struct display_timing *timing, uint32_t fbbase);
void h3_hs_timer_init(void);
void udelay(uint32_t d);

#define FB_ADDR 0x5F900000

// ---- MSX: экран выбора «BASIC / Load cartridge» ----
// Возвращает 0 = ESC/назад, 1 = Start BASIC, 2 = Load cartridge.
static int msx_launch_dialog(int has_dir) {
    int sel = 0;   // 0 = BASIC, 1 = Load cartridge
    int dirty = 1;
    int nopts = has_dir ? 2 : 1;   // вне цикла — используется и в рендере, и в вводе
    for (;;) {
        // Рендер ТОЛЬКО при изменении — иначе экран мерцает (перерисовка
        // со звёздами затирает/мельтешит подложку).
        if (dirty) {
            fb_clear();
            fb_draw_stars();
            fb_puts_s(60, 80, "MSX / Yamaha YIS-503II", 2, 0x00FFAA00);
            fb_fill_rect(60, 120, 300, 2, 0x00FFFFFF);

            const char* opts[2];
            opts[0] = "1. Start BASIC";
            opts[1] = has_dir ? "2. Load cartridge from SD" : "2. (no /roms/msx folder)";

            int y = 180;
            for (int i = 0; i < nopts; i++) {
                uint32_t clr = (i == sel) ? 0x00FFFF00 : 0x00AAAAAA;
                if (i == sel) fb_fill_rect(50, y - 4, 450, 26, 0x00222222);
                fb_puts_s(70, y, opts[i], 1, clr);
                y += 36;
            }

            fb_puts(60, 520, "  ^v: select   Enter: OK   ESC: back", 0x00888888);
            fb_flush();
            dirty = 0;
        }

        int k = usb_input_poll();
        if (!k) { udelay(16000); continue; }
        if (k == 82) { if (nopts > 1 && sel != 0) { sel = 0; dirty = 1; } }        // Up → BASIC
        else if (k == 81) { if (nopts > 1 && sel != 1) { sel = 1; dirty = 1; } }   // Down → cartridge
        else if (k == 40) {
            if (sel == 0) return 1;
            if (has_dir) return 2;
            return 1;   // если папки нет — Enter = BASIC
        }
        else if (k == 41 || k == 27) return 0;
        // анти-автоповтор: если курсор не менялся, даём паузу
        udelay(50000);
    }
}

void main(void) {
    int sd_ok = 0;

    uart_init();
    uart_rx_flush();
    uart_puts("\nMultiTool Retro boot\n");
    uart_puts("build: TFT self-test r27 (mode0-fix)\n");

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
        fat_entry_t* dirs = fat_scratch();
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

    // Пользовательский ремап клавиатуры (из /retro.cfg) — после fat_init
    if (sd_ok) { remap_load(); uart_puts("remap: loaded\n"); }

    // I2C (TWI0 PA11/PA12) + Sega-геймпад через PCF8574@0x20
    if (sega_pad_init()) uart_puts("sega_pad: PCF8574 OK\n");
    else uart_puts("sega_pad: PCF8574 not found\n");

    // Вторичное ядро CPU1: SPI-дисплей на своём ядре — core0 не нагружается.
    extern int h3_cpu_start(int cpu, void (*entry)(void));
    extern void cpu1_entry(void);
    if (h3_cpu_start(1, cpu1_entry) == 1)
        uart_puts("smp: CPU1 started (TFT core)\n");
    else
        uart_puts("smp: CPU1 FAILED to start\n");

    // Диагностика: статус CPU1 из SRAM (0x24). 1=вошёл, 2=init ok, 3=тест,
    // 9=не отвечает. Печатаем несколько раз — видно переходы даже если
    // терминал рвёт вывод.
    {
        volatile uint32_t* st  = (volatile uint32_t*)0x24u;
        volatile uint32_t* tx  = (volatile uint32_t*)0x28u;
        volatile uint32_t* ty  = (volatile uint32_t*)0x2Cu;
        volatile uint32_t* pc3 = (volatile uint32_t*)0x30u;
        for (int i = 0; i < 10; i++) {
            udelay(300000);
            printf("TFT: st=%u tX=0x%04X tY=0x%04X pc3=0x%04X\n",
                   (unsigned)*st, (unsigned)*tx, (unsigned)*ty, (unsigned)*pc3);
        }
    }

    // Тест-режим: core0 в idle, весь SPI-тест панели на CPU1 (без меню).
    for (;;) udelay(1000000);

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

        // MSX: BIOS и BASIC вшиты. Показываем экран выбора:
        //   1. Start BASIC
        //   2. Load cartridge from SD (если /roms/msx есть)
        if (strcmp(id, "msx") == 0) {
            extern void emu_run_msx(const uint8_t*, uint32_t, const char*);
            // проверяем наличие папки /roms/msx
            int has_dir = 0;
            fat_entry_t* list = fat_scratch();
            int n = fat_list("/roms", list, FAT_MAX_ENTRIES);
            for (int i = 0; i < n; i++) {
                if (list[i].size != 0) continue;
                const char* dn = list[i].name;
                if ((dn[0] == 'm' || dn[0] == 'M') &&
                    (dn[1] == 's' || dn[1] == 'S') &&
                    (dn[2] == 'x' || dn[2] == 'X')) { has_dir = 1; break; }
            }

            int choice = msx_launch_dialog(has_dir);
            if (choice == 1)
                emu_run_msx(NULL, 0, name);    // BASIC
            else if (choice == 2)
                rom_browser_run("msx", name, "msx");
            continue;
        }

        rom_browser_run(id, name, dir);
    }
}