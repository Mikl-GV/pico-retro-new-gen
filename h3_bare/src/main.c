#include <stdint.h>
#include <string.h>
#include "h3.h"
#include "uart.h"
#include "display_timing.h"
#include "a2600.h"
#include "a5200.h"
#include "a7800.h"
#include "touch.h"
#include "rom_a7800_asteroids.h"
#include "sd.h"
#include "fat.h"
#include "usb_kbd.h"
#include "fb_text.h"

const uint8_t* test_a5200_get_bios(void);
const uint8_t* test_a5200_get_cart(void);
void test_a5200_setup_dl(a5200_t* m);

int h3_de2_init(struct display_timing *timing, uint32_t fbbase);
void h3_hs_timer_init(void);
void udelay(uint32_t d);

#define PHYS_W  1024
#define PHYS_H  600
#define FB_ADDR 0x5F900000
#define EMU_FB  0x5F800000
#define ROM_BUF 0x50000000
#define ROM_MAX (24 * 1024 * 1024)

static void blit_emu_fb_w(int w) {
    uint16_t* src = (uint16_t*)EMU_FB;
    volatile uint32_t* dst = (volatile uint32_t*)FB_ADDR;
    int sx = (PHYS_W - w * 3) / 2;
    int sy = (PHYS_H - 240 * 3) / 2;
    for (int y = 0; y < 240 && sy + 2 < PHYS_H; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t p = src[y * w + x];
            // RGB565 -> RGB888, стандартно, без коррекций
            uint32_t r = ((p >> 11) & 0x1F) << 3;
            uint32_t g = ((p >> 5)  & 0x3F) << 2;
            uint32_t b = ((p >> 0)  & 0x1F) << 3;
            uint32_t px = (r << 16) | (g << 8) | b;
            dst[(sy+0)*PHYS_W + sx+0] = px;
            dst[(sy+0)*PHYS_W + sx+1] = px;
            dst[(sy+0)*PHYS_W + sx+2] = px;
            dst[(sy+1)*PHYS_W + sx+0] = px;
            dst[(sy+1)*PHYS_W + sx+1] = px;
            dst[(sy+1)*PHYS_W + sx+2] = px;
            dst[(sy+2)*PHYS_W + sx+0] = px;
            dst[(sy+2)*PHYS_W + sx+1] = px;
            dst[(sy+2)*PHYS_W + sx+2] = px;
            sx += 3;
        }
        sx = (PHYS_W - w * 3) / 2; sy += 3;
    }
    fb_flush();
}

static void blit_emu_fb(void) { blit_emu_fb_w(160); }

static void dbg_pc(uint16_t pc) {
    char b[8] = "0x0000\n";
    const char* hx = "0123456789ABCDEF";
    b[2] = hx[(pc >> 12) & 0xF]; b[3] = hx[(pc >> 8) & 0xF];
    b[4] = hx[(pc >> 4) & 0xF]; b[5] = hx[pc & 0xF];
    uart_puts(b);
}

#define ROM_PLAT_A2600 1
#define ROM_PLAT_A5200 2
#define ROM_PLAT_A7800 3

static const char* rom_dir_for_plat(int plat) {
    if (plat == ROM_PLAT_A2600) return "/roms/a2600";
    if (plat == ROM_PLAT_A5200) return "/roms/a5200";
    if (plat == ROM_PLAT_A7800) return "/roms/a7800";
    return "/roms";
}

static int list_roms(int plat, fat_entry_t* out, int max) {
    return fat_list(rom_dir_for_plat(plat), out, max);
}

static int load_rom(const char* path, const char* name, uint8_t** rom, uint32_t* size, int strip_a78) {
    fat_entry_t f;
    if (!fat_find(path, name, &f)) return -1;
    if (f.size == 0 || f.size > ROM_MAX) return -1;
    uint8_t* buf = (uint8_t*)ROM_BUF;
    int offs = (strip_a78 && f.size >= 131072) ? 128 : 0;
    uint32_t load_size = f.size - offs;
    int r = fat_read_file(&f, offs, buf, load_size);
    if (r <= 0) return -1;
    *rom = buf; *size = load_size;
    return 0;
}

static uint8_t kernel_2600[4096] = {
    0x78, 0xD8, 0xA2, 0xFF, 0x9A, 0xE6, 0x80,
    0xA9, 0x02, 0x85, 0x00, 0xA2, 0x03, 0x85, 0x02, 0xCA, 0xD0, 0xFB,
    0xA9, 0x00, 0x85, 0x00, 0xA9, 0x02, 0x85, 0x01,
    0xA2, 0x25, 0x85, 0x02, 0xCA, 0xD0, 0xFB,
    0xA9, 0x00, 0x85, 0x01, 0xA2, 0xC0,
    0xA5, 0x80, 0x29, 0x7F, 0x85, 0x09,
    0xA9, 0xAA, 0x85, 0x0E, 0x85, 0x0F, 0xA9, 0x14, 0x85, 0x08, 0x85, 0x02, 0xCA, 0xD0, 0xF7,
    0xA9, 0x02, 0x85, 0x01, 0xA2, 0x1E, 0x85, 0x02, 0xCA, 0xD0, 0xFB,
    0xA9, 0x00, 0x85, 0x01, 0x4C, 0x05, 0xF0,
    [0xFFC] = 0x00, 0xF0, [0xFFE] = 0x00, 0xF0,
};

// ============================================================
// Простое меню: список систем
// ============================================================
static const char* sys_names[] = {
    "Atari 2600", "Atari 5200", "Atari 7800",
};
#define N_SYS 3
#define MENU_COUNT (N_SYS + 1)  // + Color test

static int cursor = 0;

static void show_menu(void) {
    fb_clear();
    fb_puts_s(60, 40, "MultiTool Retro", 2, 0x00FF0000);
    fb_fill_rect(60, 70, 200, 2, 0x00FFFFFF);
    for (int i = 0; i < MENU_COUNT; i++) {
        int y = 110 + i * 36;
        int sel = (i == cursor);
        uint32_t clr = sel ? 0x00FFFF00 : 0x00FFFFFF;
        // Подложку рисуем ДО текста, чтобы не затирать его
        if (sel) fb_fill_rect(60, y - 2, PHYS_W - 120, 30, 0x00101010);
        if (i < N_SYS)
            fb_puts_s(80, y, sys_names[i], 2, clr);
        else
            fb_puts_s(80, y, "Color test", 2, clr);
    }
    fb_puts(60, PHYS_H - 40, "Arrows+Enter: select   ESC: back", 0x00AAAAAA);
    fb_flush();
}

// ============================================================
// Список игр с SD
// ============================================================
static void sort_roms(fat_entry_t* list, int n) {
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(list[i].name, list[j].name) > 0) {
                fat_entry_t t = list[i]; list[i] = list[j]; list[j] = t;
            }
}

static int pick_rom_screen(const char* title, fat_entry_t* list, int n) {
    if (n <= 0) {
        fb_clear();
        fb_text_center("No ROMs found", 200, 2, 0x00FF0000);
        fb_text_center("Put .a26/.a52/.a78 in /roms/<sys>/", 240, 1, 0x00FFFFFF);
        fb_flush();
        return -1;
    }
    int cur = 0;
    const int rows = 12;
    for (;;) {
        fb_clear();
        fb_puts_s(60, 40, title, 2, 0x00FF0000);
        fb_fill_rect(60, 70, 200, 2, 0x00FFFFFF);
        int top = cur - rows / 2;
        if (top < 0) top = 0;
        if (top + rows > n) top = n - rows;
        for (int i = 0; i < rows && top + i < n; i++) {
            int idx = top + i, y = 100 + i * 34;
            int sel = (idx == cur);
            uint32_t clr = sel ? 0x00FFFF00 : 0x00FFFFFF;
            // Подложку рисуем ДО текста, чтобы не затирать его
            if (sel) fb_fill_rect(60, y - 2, PHYS_W - 120, 28, 0x00101010);
            char buf[64];
            strncpy(buf, list[idx].name, 60); buf[60] = 0;
            fb_puts_s(80, y, buf, 1, clr);
        }
        fb_puts(60, PHYS_H - 40, "Arrows: move   Enter: run   ESC: back", 0x00AAAAAA);
        fb_flush();

        int k = 0;
        for (;;) {
            if (uart_rx_ready()) { k = uart_getc(); }
            else { int kk = usb_kbd_poll(); if (kk) k = kk; }
            if (k) break;
            udelay(10000);
        }
        if (k == 82 || k == 'w') { cur = (cur - 1 + n) % n; }
        else if (k == 81 || k == 's') { cur = (cur + 1) % n; }
        else if (k == 40 || k == '\n' || k == '\r') { return cur; }
        else if (k == 41 || k == 27 || k == 'q') { return -1; }
    }
}

// ============================================================
// Эмуляторы
// ============================================================
static void run_a2600(const uint8_t* rom, uint32_t size) {
    memset((void*)EMU_FB, 0, 160*240*2); fb_clear(); fb_flush();
    a2600_t a; a2600_init(&a, rom, size, (uint16_t*)EMU_FB); uint32_t fc = 0;
    while (1) {
        a2600_frame(&a); blit_emu_fb();
        if ((fc++ & 0x3F) == 0) { uart_puts("a2600 pc="); dbg_pc(a.cpu.pc); }
        if (uart_rx_ready()) break;
        if ((fc & 0x7F) == 0 && usb_kbd_poll() == 41) break;
    }
}

static void run_a5200(const uint8_t* rom, uint32_t size) {
    memset((void*)EMU_FB, 0, 160*240*2); fb_clear(); fb_flush();
    a5200_t a; a5200_init(&a, rom, size, test_a5200_get_bios(), (uint16_t*)EMU_FB);
    test_a5200_setup_dl(&a); uint32_t fc = 0;
    while (1) {
        a5200_frame(&a); blit_emu_fb();
        if ((fc++ & 0x3F) == 0) { uart_puts("a5200 pc="); dbg_pc(a.cpu.pc); }
        if (uart_rx_ready()) break;
        if ((fc & 0x7F) == 0 && usb_kbd_poll() == 41) break;
    }
}

static void run_a7800(const uint8_t* rom, uint32_t size) {
    memset((void*)EMU_FB, 0, 160*240*2); fb_clear(); fb_flush();
    a7800_t a; a7800_init(&a, rom, size, NULL, 0, (uint16_t*)EMU_FB); uint32_t fc = 0;
    while (1) {
        a7800_frame(&a); blit_emu_fb();
        if ((fc++ & 0x3F) == 0) { uart_puts("a7800 pc="); dbg_pc(a.cpu.pc); }
        if (uart_rx_ready()) break;
        if ((fc & 0x7F) == 0 && usb_kbd_poll() == 41) break;
    }
}

static void run_system(int plat) {
    fat_entry_t list[FAT_MAX_ENTRIES];
    int n = list_roms(plat, list, FAT_MAX_ENTRIES);
    sort_roms(list, n);
    const char* title = (plat == ROM_PLAT_A2600) ? "Atari 2600 games" :
                        (plat == ROM_PLAT_A5200) ? "Atari 5200 games" : "Atari 7800 games";
    int pick = pick_rom_screen(title, list, n);
    if (pick < 0) return;

    uint8_t* rom = 0; uint32_t rom_size = 0;
    if (load_rom(rom_dir_for_plat(plat), list[pick].name, &rom, &rom_size,
                 (plat == ROM_PLAT_A7800)) != 0) return;

    if (plat == ROM_PLAT_A2600) run_a2600(rom, rom_size);
    else if (plat == ROM_PLAT_A5200) run_a5200(rom, rom_size);
    else run_a7800(rom, rom_size);
}

// ============================================================
// main
// ============================================================
void main(void) {
    int sd_ok = 0;

    uart_init(); uart_puts("\nH3 emu boot\n");
    h3_hs_timer_init();

    struct display_timing timing;
    memset(&timing, 0, sizeof(timing));
    timing.hdmi_monitor = 0;   // DVI mode: панель Waveshare ждёт чистый RGB 0-255
                               // без AVI-инфофреймов (иначе уводит в YUV/limited -> жёлтый)
    timing.pixelclock.typ = 51200000;
    timing.hactive.typ = 1024; timing.hfront_porch.typ = 160;
    timing.hback_porch.typ = 88; timing.hsync_len.typ = 40;
    timing.vactive.typ = 600; timing.vfront_porch.typ = 12;
    timing.vback_porch.typ = 20; timing.vsync_len.typ = 3;
    timing.flags = (DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW);

    if (h3_de2_init(&timing, FB_ADDR) != 0) { uart_puts("HDMI FAILED\n"); while (1) udelay(1000000); }
    uart_puts("HDMI ok\n");

    touch_init();
    uart_puts("USB kbd init...\n");
    if (usb_kbd_init() == 0) uart_puts("USB kbd ready\n");
    else uart_puts("USB kbd not found\n");

    uart_puts("SD init...\n");
    sd_ok = (sd_init() == 0) && (fat_init() == 0);
    if (sd_ok) uart_puts("SD ready\n");

    for (;;) {
        cursor = 0;
        show_menu();
        for (;;) {
            int k = 0;
            if (uart_rx_ready()) { k = uart_getc(); }
            else { int kk = usb_kbd_poll(); if (kk) k = kk; }
            if (k == 82 || k == 'w') { cursor = (cursor - 1 + MENU_COUNT) % MENU_COUNT; show_menu(); }
            else if (k == 81 || k == 's') { cursor = (cursor + 1) % MENU_COUNT; show_menu(); }
            else if (k == 40 || k == '\n' || k == '\r') {
                if (cursor < N_SYS) {
                    int plat = (cursor == 0) ? ROM_PLAT_A2600 :
                               (cursor == 1) ? ROM_PLAT_A5200 : ROM_PLAT_A7800;
                    if (!sd_ok) {
                        fb_clear();
                        fb_text_center("SD not ready", 200, 2, 0x00FF0000);
                        fb_text_center("No games in menu without SD", 240, 1, 0x00FFFFFF);
                        fb_flush();
                    } else {
                        run_system(plat);
                    }
                    break;
                } else {
                    // Color test
                    fb_clear();
                    volatile uint32_t* fb = (volatile uint32_t*)FB_ADDR;
                    uint32_t cols[8] = {0x00FF0000,0x0000FF00,0x000000FF,0x00FFFF00,
                                        0x0000FFFF,0x00FF00FF,0x00FFFFFF,0x00000000};
                    const char* names[8] = {"RED","GREEN","BLUE","YEL","CYAN","MAG","WHITE","BLK"};
                    for (int i = 0; i < 8; i++) {
                        int bx = 30 + i * 120;
                        for (int y = 250; y < 370; y++)
                            for (int x = bx; x < bx + 80; x++)
                                fb[y*PHYS_W+x] = cols[i];
                        fb_puts_s(bx + 10, 220, names[i], 1, 0x00FFFFFF);
                    }
                    fb_text_center("press any key", 500, 1, 0x00AAAAAA);
                    fb_flush();
                    while (1) {
                        if (uart_rx_ready()) { uart_getc(); break; }
                        if (usb_kbd_poll()) break;
                        udelay(10000);
                    }
                    break;
                }
            }
            udelay(10000);
        }
    }
}