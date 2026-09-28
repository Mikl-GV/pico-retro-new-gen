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

// ---- ZX Spectrum (Fuse): выбор модели, затем ROM / BASIC ----
static const char* const zx_models[] = {
    "Spectrum 48K", "Spectrum 128K", "Spectrum +2", "Spectrum +2A",
    "Spectrum +3", "Spectrum +3e", "Spectrum 48K (NTSC)", "Spectrum SE",
    "Timex TC2048", "Timex TC2068", "Timex TS2068", "Spectrum 16K",
};
#define ZX_MODEL_COUNT ((int)(sizeof(zx_models) / sizeof(zx_models[0])))

extern void fuse_set_model(const char* m);
extern void emu_run_fuse(const uint8_t* rom, uint32_t size, const char* rom_name);
extern void bk_set_model(const char* m);
extern void emu_run_bk(const uint8_t* rom, uint32_t size, const char* rom_name);

static int zx_model_dialog(void) {
    int sel = 0, scroll = 0, dirty = 1;
    const int per = 13;
    for (;;) {
        if (dirty) {
            fb_clear(); fb_draw_stars();
            fb_puts_s(60, 60, "ZX Spectrum", 2, 0x00FFAA00);
            fb_fill_rect(60, 100, 340, 2, 0x00FFFFFF);
            for (int i = 0; i < per && scroll + i < ZX_MODEL_COUNT; i++) {
                int idx = scroll + i, y = 120 + i * 24;
                uint32_t clr = (idx == sel) ? 0x00FFFF00 : 0x00AAAAAA;
                if (idx == sel) fb_fill_rect(50, y - 2, 500, 22, 0x00222222);
                fb_puts(70, y, zx_models[idx], clr);
            }
            fb_puts(60, 520, "  ^v: model   Enter: OK   ESC: back", 0x00888888);
            fb_flush(); dirty = 0;
        }
        int k = usb_input_poll();
        if (!k) { udelay(16000); continue; }
        if (k == 82) { if (sel > 0) sel--; if (sel < scroll) scroll = sel; dirty = 1; }
        else if (k == 81) { if (sel < ZX_MODEL_COUNT - 1) sel++; if (sel >= scroll + per) scroll = sel - per + 1; dirty = 1; }
        else if (k == 40) return sel;
        else if (k == 41 || k == 27) return -1;
        udelay(50000);
    }
}

// 1 = ROM (браузер /roms/zxspectrum), 2 = BASIC, 0 = назад
static int zx_source_dialog(void) {
    int sel = 0, dirty = 1;
    for (;;) {
        if (dirty) {
            fb_clear(); fb_draw_stars();
            fb_puts_s(60, 90, "ZX Spectrum: load", 2, 0x00FFAA00);
            const char* opts[2] = { "1. ROM (snapshot .z80/.sna)", "2. BASIC" };
            for (int i = 0; i < 2; i++) {
                int y = 160 + i * 40;
                uint32_t clr = (i == sel) ? 0x00FFFF00 : 0x00AAAAAA;
                if (i == sel) fb_fill_rect(50, y - 6, 600, 30, 0x00222222);
                fb_puts(70, y, opts[i], clr);
            }
            fb_puts(60, 520, "  ^v: select   Enter: OK   ESC: back", 0x00888888);
            fb_flush(); dirty = 0;
        }
        int k = usb_input_poll();
        if (!k) { udelay(16000); continue; }
        if (k == 82) { if (sel > 0) sel--; dirty = 1; }
        else if (k == 81) { if (sel < 1) sel++; dirty = 1; }
        else if (k == 40) return sel + 1;
        else if (k == 41 || k == 27) return 0;
        udelay(50000);
    }
}

// ---- BK-0010/0011M: выбор модели (опция bk_model ядра), затем ROM/BASIC ----
static const char* const bk_models[] = {
    "BK-0010", "BK-0010.01", "BK-0010.01 + FDD",
    "BK-0011M + FDD", "Terak 8510/a", "Slow BK-0011M",
};
#define BK_MODEL_COUNT ((int)(sizeof(bk_models) / sizeof(bk_models[0])))

static int bk_model_dialog(void) {
    int sel = 0, scroll = 0, dirty = 1;
    const int per = 13;
    for (;;) {
        if (dirty) {
            fb_clear(); fb_draw_stars();
            fb_puts_s(60, 60, "BK-0010/0011M", 2, 0x00FFAA00);
            fb_fill_rect(60, 100, 340, 2, 0x00FFFFFF);
            for (int i = 0; i < per && scroll + i < BK_MODEL_COUNT; i++) {
                int idx = scroll + i, y = 120 + i * 24;
                uint32_t clr = (idx == sel) ? 0x00FFFF00 : 0x00AAAAAA;
                if (idx == sel) fb_fill_rect(50, y - 2, 500, 22, 0x00222222);
                fb_puts(70, y, bk_models[idx], clr);
            }
            fb_puts(60, 520, "  ^v: model   Enter: OK   ESC: back", 0x00888888);
            fb_flush(); dirty = 0;
        }
        int k = usb_input_poll();
        if (!k) { udelay(16000); continue; }
        if (k == 82) { if (sel > 0) sel--; if (sel < scroll) scroll = sel; dirty = 1; }
        else if (k == 81) { if (sel < BK_MODEL_COUNT - 1) sel++; if (sel >= scroll + per) scroll = sel - per + 1; dirty = 1; }
        else if (k == 40) return sel;
        else if (k == 41 || k == 27) return -1;
        udelay(50000);
    }
}

// 1 = ROM (браузер /roms/bk0010), 2 = BASIC, 0 = назад
static int bk_source_dialog(void) {
    int sel = 0, dirty = 1;
    for (;;) {
        if (dirty) {
            fb_clear(); fb_draw_stars();
            fb_puts_s(60, 90, "BK: load", 2, 0x00FFAA00);
            const char* opts[2] = { "1. ROM (.bin / .img)", "2. BASIC" };
            for (int i = 0; i < 2; i++) {
                int y = 160 + i * 40;
                uint32_t clr = (i == sel) ? 0x00FFFF00 : 0x00AAAAAA;
                if (i == sel) fb_fill_rect(50, y - 6, 600, 30, 0x00222222);
                fb_puts(70, y, opts[i], clr);
            }
            fb_puts(60, 520, "  ^v: select   Enter: OK   ESC: back", 0x00888888);
            fb_flush(); dirty = 0;
        }
        int k = usb_input_poll();
        if (!k) { udelay(16000); continue; }
        if (k == 82) { if (sel > 0) sel--; dirty = 1; }
        else if (k == 81) { if (sel < 1) sel++; dirty = 1; }
        else if (k == 40) return sel + 1;
        else if (k == 41 || k == 27) return 0;
        udelay(50000);
    }
}

// Единая строка версии прошивки: показывается в About (HDMI) и на TFT в углу.
// Обновлять при каждой сборке (совпадает с баннером build:).
const char g_fw_version[] = "r0.257 (15.2.1)";

void main(void) {
    int sd_ok = 0;

    uart_init();
    uart_rx_flush();
    uart_puts("\nMultiTool Retro boot\n");
    uart_puts("build: r0.257 (15.2.1)\n");

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
    // r124: когерентность .coherent включаем ЯВНО, не полагаясь на USB —
    // mmu_mark_uncached() звался только из usb_ohci_init(), а если USB не
    // инициализирован (нет клавиатуры), межъядерная связь через .coherent
    // не работала. SRAM-почта (калибровка/кнопки/справка) не зависит от этого.
    extern void mmu_mark_uncached(uint32_t addr);
    // r0.198: адрес брали жёстко (0x43800000), но после r180 (*(.bss.*) в linker.ld)
    // .bss вырос, _bend1 перешёл через 0x42000000, и .coherent сдвинулся на 1 МБ
    // (nm: _coherent_start=0x43900000). Жёсткий адрес помечал ЧУЖУЮ секцию (хвост
    // gb-пула), оставляя реальный .coherent кэшируемым. Берём символ линкера —
    // он всегда совпадает с началом 1МБ-области (как H3_MEM_COHERENT_REGION).
    extern unsigned char libh3_coherent_region[];
    mmu_mark_uncached((uint32_t)libh3_coherent_region);
    extern int h3_cpu_start(int cpu, void (*entry)(void));
    extern void cpu1_entry(void);
    if (h3_cpu_start(1, cpu1_entry) == 1)
        uart_puts("smp: CPU1 started (TFT core)\n");
    else
        uart_puts("smp: CPU1 FAILED to start\n");

// Меню на HDMI — обычная работа core0; справка на TFT.
    // r123: SRAM-почта 0x64 (кнопки с TFT) не zero-инициализируется и может
    // содержать мусор, пока CPU1 ещё не стартовал — чистим заранее.
    *(volatile int32_t*)0x64u = 0;
    __asm volatile("dsb st" ::: "memory");   // r0.198: закрыть запись до чтения CPU1
    for (;;) {
        tft_help_show(NULL);
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
            sega_pad_init();             // чистый пад перед входом
            tft_help_show("portfolio");
            *(volatile uint32_t*)0x74u = 1;   // r162: игра — TFT (CPU1) заморожен
            __asm volatile("dsb st" ::: "memory");   // r0.198: флаг виден CPU1 до входа
            emu_run_portfolio(NULL, 0, name);
            usb_wait_release_all();                  // r0.204: кнопка выхода не «доезжает» в меню
            *(volatile uint32_t*)0x74u = 0;
            __asm volatile("dsb st" ::: "memory");
            sega_pad_init();             // и после выхода
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
            if (choice == 1) {
                sega_pad_init();         // чистый пад перед входом
                tft_help_show("msx");
                *(volatile uint32_t*)0x74u = 1;   // r162: игра — TFT (CPU1) заморожен
                __asm volatile("dsb st" ::: "memory");   // r0.198: флаг виден CPU1
                emu_run_msx(NULL, 0, name);    // BASIC
                usb_wait_release_all();        // r0.204: кнопка выхода не «доезжает» в меню
                *(volatile uint32_t*)0x74u = 0;
                __asm volatile("dsb st" ::: "memory");
                sega_pad_init();         // и после выхода
            } else if (choice == 2) {
                sega_pad_init();         // чистый пад перед браузером картриджей
                tft_help_show("msx");
                rom_browser_run("msx", name, "msx");
            }
            continue;
        }

        // ZX Spectrum: выбор модели -> ROM (.z80/.sna) или BASIC
        if (strcmp(id, "zxspectrum") == 0) {
            int m = zx_model_dialog();
            if (m < 0) continue;
            fuse_set_model(zx_models[m]);
            int src = zx_source_dialog();
            if (src == 0) continue;
            sega_pad_init();
            tft_help_show("zxspectrum");
            if (src == 2) {                  // BASIC
                *(volatile uint32_t*)0x74u = 1;
                __asm volatile("dsb st" ::: "memory");
                emu_run_fuse(NULL, 0, "basic");
                usb_wait_release_all();
                usb_kbd_restart_intr();
                *(volatile uint32_t*)0x74u = 0;
                __asm volatile("dsb st" ::: "memory");
            } else {                         // ROM через браузер (0x74 ставит run_emulator)
                rom_browser_run("zxspectrum", name, "zxspectrum");
            }
            sega_pad_init();
            continue;
        }

        // BK-0010/0011M: выбор модели -> ROM (.bin) или BASIC
        if (strcmp(id, "bk0010") == 0) {
            int m = bk_model_dialog();
            if (m < 0) continue;
            bk_set_model(bk_models[m]);
            int src = bk_source_dialog();   // 1=ROM(.bin/.img), 2=BASIC
            if (src == 0) continue;
            sega_pad_init();
            tft_help_show("bk0010");
            if (src == 2) {                  // BASIC
                *(volatile uint32_t*)0x74u = 1;
                __asm volatile("dsb st" ::: "memory");
                emu_run_bk(NULL, 0, "basic");
                usb_wait_release_all();
                usb_kbd_restart_intr();
                *(volatile uint32_t*)0x74u = 0;
                __asm volatile("dsb st" ::: "memory");
            } else {                         // ROM через браузер (0x74 ставит run_emulator)
                rom_browser_run("bk0010", name, "bk0010");
            }
            sega_pad_init();
            continue;
        }

        rom_browser_run(id, name, dir);
    }
}