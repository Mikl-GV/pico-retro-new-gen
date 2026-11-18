// r1427: меню PC/XT-286 (pico-286) — отдельным объектом, линкуется ПОСЛЕДНИМ.
// Вставка кода в main.c или ядро сдвигает .text и на стенде стабильно ломает
// USB-энумерацию (ctrl TIMEOUT cc=0/0/15, no keyboard found) — PITFALLS §10,
// HANDOVER r1378/r1426.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fb_text.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "rom_browser.h"
#include "emu.h"
#include "fat.h"
#include "emulog.h"

extern void udelay(uint32_t d);
extern void tft_help_show(const char* sys_id);
extern void pxt_mount_hdd(void);
extern void pxt_disk_set(uint8_t drive, const uint8_t* base, size_t len);
extern void emu_run_pxt(const uint8_t* rom, uint32_t size, const char* rom_name);

// 1 = ROM/образ через браузер, 2 = встроенное ПО (fdd0/fdd1/HDD), 0 = назад.
static int pxt_source_dialog(void) {
    int sel = 0, dirty = 1;
    for (;;) {
        if (dirty) {
            fb_clear(); fb_draw_stars();
            fb_puts_s(60, 90, "PC/XT-286: load", 2, 0x00FFAA00);
            const char* opts[2] = {
                "1. Load ROM / disk image (browser)",
                "2. Built-in boot (fdd0 + HDD)",
            };
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

// r1428: индикатор встроенной загрузки (как load_rom в rom_browser): на HDMI
// видно, что fdd0/fdd1 читаются с носителя, а не «зависло».
static void pxt_boot_begin(const char* label) {
    fb_clear();
    fb_puts_s(60, 200, "PC/XT-286: built-in boot", 2, 0x00FFFF00);
    fb_puts_s(60, 240, label, 1, 0x00FFFFFF);
    fb_fill_rect(60, 300, 904, 26, 0x00222222);
    fb_puts_s(60, 340, "0 %", 1, 0x00AAAAAA);
    fb_flush();
}

static void pxt_boot_update(uint32_t done, uint32_t total) {
    uint32_t pct = total ? (uint32_t)(((uint64_t)done * 100u) / total) : 100u;
    if (pct > 100) pct = 100;
    fb_fill_rect(60, 300, (904 * (int)pct) / 100, 26, 0x0000AA00);
    char pb[16];
    snprintf(pb, sizeof(pb), "%lu %%", (unsigned long)pct);
    fb_fill_rect(60, 340, 140, 18, 0x00000000);
    fb_puts_s(60, 340, pb, 1, 0x00AAAAAA);
    fb_flush();
}

static int pxt_load_image(const char* name, uint8_t* dst, fat_entry_t* e, uint32_t maxsz) {
    if (!fat_find("/roms/pxt", name, e) || !e->size || e->size > maxsz) return -1;
    pxt_boot_begin(name);
    uint32_t done = 0;
    const uint32_t chunk = 256u * 1024u;
    while (done < e->size) {
        uint32_t n = e->size - done;
        if (n > chunk) n = chunk;
        if (fat_read_file(e, done, dst + done, n) != (int)n) return -1;
        done += n;
        pxt_boot_update(done, e->size);
    }
    return 0;
}

// Встроенный запуск — как эталон pico-286 при включении: A: fdd0.img,
// B: fdd1.img (если есть), C:/D: из /roms/pxt/hdd, затем reset86/INT 19h.
static int pxt_boot_default(void) {
    fat_entry_t e0, e1;
    uint8_t* buf0 = (uint8_t*)0x50000000u;      // ROM_BUF, аркадный пул
    const uint32_t off1 = 4u * 1024u * 1024u;
    uint8_t* buf1 = buf0 + off1;
    if (pxt_load_image("fdd0.img", buf0, &e0, off1) != 0) {
        emulog_field("PXT", "boot", "fdd0.img not found");
        return -1;
    }
    pxt_boot_begin("HDD C:/D:");
    pxt_mount_hdd();
    pxt_boot_update(1, 1);
    if (pxt_load_image("fdd1.img", buf1, &e1, 16u * 1024u * 1024u) == 0) {
        pxt_disk_set(1, buf1, e1.size);
        emulog_field("PXT", "disk", "B: fdd1.img, %u B", (unsigned)e1.size);
    }
    emu_run_pxt(buf0, e0.size, "fdd0.img");
    return 0;
}

// Цикл меню: диалог → браузер или встроенный запуск. Живёт вне main.c.
void pxt_menu_run(const char* name) {
    for (;;) {
        int src = pxt_source_dialog();
        if (src == 0) return;
        sega_pad_init();
        tft_help_show("pxt");
        if (src == 2) {
            *(volatile uint32_t*)0x74u = 1;
            __asm volatile("dsb st" ::: "memory");
            int r = pxt_boot_default();
            usb_wait_release_all();
            usb_kbd_restart_intr();
            *(volatile uint32_t*)0x74u = 0;
            __asm volatile("dsb st" ::: "memory");
            if (r != 0) rom_browser_run("pxt", name, "pxt");
        } else {
            rom_browser_run("pxt", name, "pxt");
        }
        sega_pad_init();
    }
}
