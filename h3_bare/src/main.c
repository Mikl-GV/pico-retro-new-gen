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

#define C_BG      0x00000000
#define C_CURSOR  0x00FFFF00
#define C_TITLE   0x00FF0000
#define C_WHITE   0x00FFFFFF
#define C_BAR     0x00FFFFFF
#define C_GREEN   0x0000FF00
#define C_GREY    0x00AAAAAA
#define C_HILITE  0x002C2C00
#define C_INFO    0x0080C0FF

static void blit_emu_fb_w(int w) {
    uint16_t* src = (uint16_t*)EMU_FB;
    volatile uint32_t* dst = (volatile uint32_t*)FB_ADDR;
    int sx = (PHYS_W - w * 3) / 2;
    int sy = (PHYS_H - 240 * 3) / 2;
    for (int y = 0; y < 240 && sy + 2 < PHYS_H; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t p = src[y * w + x];
            // RGB565 -> RGB888 + коррекция белой точки против желтизны H3
            // (чуть убрать R/G, добавить B — как в fb_text)
            uint32_t r = ((((p >> 11) & 0x1F) << 3) * 235) >> 8;
            uint32_t g = ((((p >> 5)  & 0x3F) << 2) * 235) >> 8;
            uint32_t b = ((((p >> 0)  & 0x1F) << 3) * 280) >> 8;
            if (b > 0xFF) b = 0xFF;
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

static void put_uint32(uint32_t v) {
    char b[12]; int i = 10; b[11] = 0;
    do { b[--i] = '0' + (v % 10); v /= 10; } while (v);
    uart_puts(b + i);
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
    uart_puts("loaded: "); uart_puts(name); uart_puts(" ("); put_uint32(load_size); uart_puts("B)\n");
    return 0;
}

static const char* rom_dir_for_plat(int plat) {
    if (plat == ROM_PLAT_A2600) return "/roms/a2600";
    if (plat == ROM_PLAT_A5200) return "/roms/a5200";
    if (plat == ROM_PLAT_A7800) return "/roms/a7800";
    return "/roms";
}

static int list_roms(int plat, fat_entry_t* out, int max) {
    return fat_list(rom_dir_for_plat(plat), out, max);
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
// UI helpers
// ============================================================

static void draw_header(const char* title) {
    fb_fill_rect(80, 8, PHYS_W - 160, 3, C_WHITE);
    fb_text_center(title, 16, 2, C_TITLE);
}

static void draw_footer(const char* msg) {
    fb_fill_rect(80, PHYS_H - 34, PHYS_W - 160, 2, C_BAR);
    fb_text_center(msg, PHYS_H - 24, 1, C_WHITE);
}

// Киноплёнка — левая и правая оранжевые панели с перфорацией как у плёнки
static void draw_film_strip(void) {
    volatile uint32_t* fb = (volatile uint32_t*)FB_ADDR;
    for (int y = 0; y < PHYS_H; y++) {
        int bright = 10 + (y * 12) / PHYS_H;
        uint32_t orange = (uint32_t)(bright << 16) | ((bright / 2) << 8);
        for (int x = 0; x < 60; x++) {
            fb[y * PHYS_W + x] = orange;
            fb[y * PHYS_W + PHYS_W - 1 - x] = orange;
        }
    }
    // Перфорация (sprocket holes) на внешнем крае панелей — как у киноплёнки
    for (int side = 0; side < 2; side++) {
        int x0 = side ? (PHYS_W - 60) : 0;
        for (int y = 10; y < PHYS_H - 20; y += 22) {
            for (int yy = 0; yy < 12; yy++)
                for (int xx = 0; xx < 10; xx++)
                    fb[(y + yy) * PHYS_W + x0 + 3 + xx] = C_BG;
        }
    }
}

static void wait_key(void) {
    for (;;) {
        if (uart_rx_ready()) { uart_getc(); return; }
        if (usb_kbd_poll() != 0) { while (usb_kbd_poll() != 0) udelay(10000); return; }
        udelay(10000);
    }
}

static int check_quit(uint32_t* fc) {
    if (uart_rx_ready()) { uart_getc(); return 1; }
    if ((*fc & 0x7F) == 0) { int k = usb_kbd_poll(); if (k == 41) return 1; }
    return 0;
}

// Ожидание нажатия Enter или пробела
static int kbd_get_action(void) {
    for (;;) {
        if (uart_rx_ready()) { return uart_getc(); }
        int k = usb_kbd_poll();
        if (k) return k;
        udelay(10000);
    }
}

// ============================================================
// Системное меню
// ============================================================

static const char* sys_names[] = {
    "Atari 2600", "Atari 5200", "Atari 7800",
};
static const char* sys_info[] = {
    "8-bit 6507 1.19MHz  160x192",
    "8-bit 6502 1.79MHz  320x192",
    "8-bit 6502C 1.79MHz  320x240",
};
#define N_SYS 3
#define MENU_SETTINGS 3
#define MENU_ABOUT    4
#define MENU_COUNT    5

static int sys_cursor = 0;
static int sys_scroll = 0;

static const char* sys_menu_name(int i) {
    if (i < N_SYS) return sys_names[i];
    if (i == MENU_SETTINGS) return "Settings";
    if (i == MENU_ABOUT) return "About";
    return "";
}

static void draw_system_menu(void) {
    fb_clear();
    draw_film_strip();
    draw_header("SELECT SYSTEM");
    const int row_h = 34, vis = 7, sy = 60;
    if (sys_cursor < sys_scroll) sys_scroll = sys_cursor;
    if (sys_cursor >= sys_scroll + vis) sys_scroll = sys_cursor - vis + 1;
    if (sys_scroll + vis > MENU_COUNT) sys_scroll = MENU_COUNT - vis;
    if (sys_scroll < 0) sys_scroll = 0;
    for (int i = sys_scroll; i < MENU_COUNT && i < sys_scroll + vis; i++) {
        int py = sy + (i - sys_scroll) * row_h;
        int sel = (i == sys_cursor);
        uint32_t clr = sel ? C_CURSOR : (i < N_SYS ? C_WHITE : C_GREY);
        if (sel) fb_fill_rect(140, py - 1, PHYS_W - 280, 26, C_HILITE);
        fb_puts_s(144, py, sel ? ">>" : "  ", 1, clr);
        fb_puts_s(170, py, sys_menu_name(i), 2, clr);
    }
    // Характеристики выбранной системы — отдельной строкой внизу (не поверх текста)
    if (sys_cursor < N_SYS) {
        fb_fill_rect(140, PHYS_H - 90, PHYS_W - 280, 2, C_BAR);
        fb_text_center(sys_info[sys_cursor], PHYS_H - 80, 1, C_INFO);
    }
    if (sys_scroll > 0) fb_puts_s(PHYS_W - 60, sy, "^", 2, C_WHITE);
    if (sys_scroll + vis < MENU_COUNT) fb_puts_s(PHYS_W - 60, sy + (vis-1)*row_h, "v", 2, C_WHITE);
    draw_footer("UP/DN  ENTER  ESC=back");
    fb_flush();
}

// ============================================================
// Игровой список
// ============================================================

static void draw_game_list(const char* title, const char** names, int n, int cursor, const char* footer) {
    fb_clear();
    draw_film_strip();
    draw_header(title);
    if (n <= 0) {
        fb_text_center("No ROMs found", 160, 2, C_TITLE);
        fb_text_center("Insert SD card or USB drive", 200, 1, C_WHITE);
    } else {
        int rows = 10, sy = 55;
        int top = cursor - rows/2;
        if (top < 0) top = 0;
        if (top + rows > n) top = n - rows;
        for (int i = 0; i < rows && top + i < n; i++) {
            int idx = top + i, py = sy + i * 32;
            int sel = (idx == cursor);
            uint32_t clr = sel ? C_CURSOR : C_WHITE;
            if (sel) fb_fill_rect(140, py - 1, PHYS_W - 280, 30, C_HILITE);
            fb_puts_s(144, py, ">", 1, clr);
            char buf[64];
            const char* src = names[idx];
            int maxc = (PHYS_W - 180) / 18;
            if (maxc > 63) maxc = 63;
            strncpy(buf, src, maxc); buf[maxc] = 0;
            fb_puts_s(160, py, buf, 2, clr);
        }
        if (n > rows) {
            if (top > 0) fb_puts_s(PHYS_W - 60, sy, "^", 2, C_WHITE);
            if (top + rows < n) fb_puts_s(PHYS_W - 60, sy + (rows-1)*32, "v", 2, C_WHITE);
        }
    }
    draw_footer(footer);
    fb_flush();
}

// ============================================================
// Цветовой тест
// ============================================================
static void color_test(void) {
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
        fb_puts_s(bx + 10, 220, names[i], 1, C_WHITE);
    }
    fb_text_center("press any key", 500, 1, C_GREY);
    fb_flush();
}

// ============================================================
// Эмуляторы
// ============================================================
static void run_a2600_kernel(void) {
    memset((void*)EMU_FB, 0, 160*240*2); fb_clear(); fb_flush();
    a2600_t a26; a2600_init(&a26, kernel_2600, 4096, (uint16_t*)EMU_FB); uint32_t fc = 0;
    while (1) { a2600_frame(&a26); blit_emu_fb(); if ((fc++&0x3F)==0) { uart_puts("a2600 pc="); dbg_pc(a26.cpu.pc); } if (check_quit(&fc)) break; }
}
static void run_a5200_kernel(void) {
    memset((void*)EMU_FB, 0, 160*240*2); fb_clear(); fb_flush();
    a5200_t a52; a5200_init(&a52, test_a5200_get_cart(), 16384, test_a5200_get_bios(), (uint16_t*)EMU_FB);
    test_a5200_setup_dl(&a52); uint32_t fc = 0;
    while (1) { a5200_frame(&a52); blit_emu_fb(); if ((fc++&0x3F)==0) { uart_puts("a5200 pc="); dbg_pc(a52.cpu.pc); } if (check_quit(&fc)) break; }
}
static void run_a7800_asteroids(void) {
    memset((void*)EMU_FB, 0, 160*240*2); fb_clear(); fb_flush();
    a7800_t a78; a7800_init(&a78, rom_a7800_asteroids, sizeof(rom_a7800_asteroids), NULL, 0, (uint16_t*)EMU_FB);
    uint32_t fc = 0;
    while (1) { a7800_frame(&a78); blit_emu_fb(); if ((fc++&0x3F)==0) { uart_puts("a7800 pc="); dbg_pc(a78.cpu.pc); } if (check_quit(&fc)) break; }
}

static void run_sd_game(int plat, int* sd_ok) {
    if (!*sd_ok) { *sd_ok = (sd_init() == 0) && (fat_init() == 0); }
    if (!*sd_ok) {
        fb_clear(); draw_film_strip();
        fb_text_center("SD not ready", 160, 2, C_TITLE);
        draw_footer("press any key"); fb_flush(); wait_key(); return;
    }
    fat_entry_t list[FAT_MAX_ENTRIES];
    int n = list_roms(plat, list, FAT_MAX_ENTRIES);
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++)
            if (strcmp(list[i].name, list[j].name) > 0) {
                fat_entry_t t = list[i]; list[i] = list[j]; list[j] = t;
            }
    if (n == 0) {
        fb_clear(); draw_film_strip();
        fb_text_center("No ROMs found", 160, 2, C_TITLE);
        draw_footer("press any key"); fb_flush(); wait_key(); return;
    }

    const char* title = (plat == ROM_PLAT_A2600) ? "Atari 2600" :
                        (plat == ROM_PLAT_A5200) ? "Atari 5200" : "Atari 7800";
    char gnames[FAT_MAX_ENTRIES][32];
    for (int i = 0; i < n && i < FAT_MAX_ENTRIES; i++) {
        strncpy(gnames[i], list[i].name, 31); gnames[i][31] = 0;
        // убираем расширение для красоты
        int dot = -1;
        for (int p = 0; gnames[i][p]; p++) if (gnames[i][p] == '.') dot = p;
        if (dot > 0) gnames[i][dot] = 0;
    }
    const char* dp[FAT_MAX_ENTRIES];
    for (int i = 0; i < n; i++) dp[i] = gnames[i];

    int cursor = 0;
    for (;;) {
        draw_game_list(title, dp, n, cursor, "UP/DN  ENTER=play  ESC=back");
        int done = 0;
        while (!done) {
            int c = 0;
            if (uart_rx_ready()) { c = uart_getc(); }
            else { int k = usb_kbd_poll(); if (k) c = k; }
            if (c == 82 || c == 'w') { cursor = (cursor - 1 + n) % n; break; }
            if (c == 81 || c == 's') { cursor = (cursor + 1) % n; break; }
            if (c == 40 || c == '\n' || c == '\r') { done = 2; break; }
            if (c == 41 || c == 'q') { uart_puts("back\n"); return; }
            udelay(10000);
        }
        if (done == 2) break;
    }

    uint8_t* rom = 0; uint32_t rom_size = 0;
    if (load_rom(rom_dir_for_plat(plat), list[cursor].name, &rom, &rom_size, (plat == ROM_PLAT_A7800)) != 0) {
        fb_clear(); draw_film_strip();
        fb_text_center("ROM load failed", 160, 2, C_TITLE);
        draw_footer("press any key"); fb_flush(); wait_key(); return;
    }
    memset((void*)EMU_FB, 0, 160*240*2); fb_clear(); fb_flush(); uint32_t fc = 0;
    if (plat == ROM_PLAT_A2600) {
        a2600_t a26; a2600_init(&a26, rom, rom_size, (uint16_t*)EMU_FB);
        while (1) { a2600_frame(&a26); blit_emu_fb(); if ((fc++&0x3F)==0) uart_puts("a2600 running\n"); if (check_quit(&fc)) break; }
    } else if (plat == ROM_PLAT_A5200) {
        a5200_t a52; a5200_init(&a52, rom, rom_size, test_a5200_get_bios(), (uint16_t*)EMU_FB);
        test_a5200_setup_dl(&a52);
        while (1) { a5200_frame(&a52); blit_emu_fb(); if ((fc++&0x3F)==0) uart_puts("a5200 running\n"); if (check_quit(&fc)) break; }
    } else {
        a7800_t a78; a7800_init(&a78, rom, rom_size, NULL, 0, (uint16_t*)EMU_FB);
        while (1) { a7800_frame(&a78); blit_emu_fb(); if ((fc++&0x3F)==0) uart_puts("a7800 running\n"); if (check_quit(&fc)) break; }
    }
}

// ============================================================
// main
// ============================================================
void main(void) {
    int sd_ok = 0;

    uart_init(); uart_puts("\nH3 emu boot\n");
    h3_hs_timer_init(); uart_puts("timer ok\n");

    struct display_timing timing;
    memset(&timing, 0, sizeof(timing));
    timing.hdmi_monitor = 1; timing.pixelclock.typ = 51200000;
    timing.hactive.typ = 1024; timing.hfront_porch.typ = 160;
    timing.hback_porch.typ = 88; timing.hsync_len.typ = 40;
    timing.vactive.typ = 600; timing.vfront_porch.typ = 12;
    timing.vback_porch.typ = 20; timing.vsync_len.typ = 3;
    timing.flags = (DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW);

    uart_puts("HDMI init...\n");
    if (h3_de2_init(&timing, FB_ADDR) != 0) {
        uart_puts("HDMI FAILED\n"); while (1) udelay(1000000);
    }
    uart_puts("HDMI ok\n");

    touch_init(); uart_puts("touch init ok\n");
    uart_puts("USB kbd init...\n");
    if (usb_kbd_init() == 0) uart_puts("USB kbd ready\n");
    else uart_puts("USB kbd not found\n");

    // Инициализируем SD при старте
    uart_puts("SD init...\n");
    sd_ok = (sd_init() == 0) && (fat_init() == 0);
    if (sd_ok) uart_puts("SD ready\n");

    for (;;) {
        sys_cursor = 0; sys_scroll = 0;
        draw_system_menu();
        for (;;) {
            int k = 0, c = 0;
            if (uart_rx_ready()) { c = uart_getc(); }
            else { k = usb_kbd_poll(); if (k) c = k; }
            if (c == 82 || c == 'w') { sys_cursor = (sys_cursor - 1 + MENU_COUNT) % MENU_COUNT; draw_system_menu(); }
            else if (c == 81 || c == 's') { sys_cursor = (sys_cursor + 1) % MENU_COUNT; draw_system_menu(); }
            else if (c == 40 || c == '\n' || c == '\r') {
                if (sys_cursor < N_SYS) {
                    int plat = (sys_cursor == 0) ? ROM_PLAT_A2600 :
                               (sys_cursor == 1) ? ROM_PLAT_A5200 : ROM_PLAT_A7800;
                    run_sd_game(plat, &sd_ok);
                    break;
                } else if (sys_cursor == MENU_SETTINGS) {
                    fb_clear(); draw_film_strip();
                    fb_text_center("Settings - press any key", 160, 2, C_TITLE);
                    fb_text_center("Pad test, brightness - not yet on H3", 200, 1, C_WHITE);
                    draw_footer("press any key"); fb_flush(); wait_key();
                    break;
                } else if (sys_cursor == MENU_ABOUT) {
                    fb_clear(); draw_film_strip();
                    fb_text_center("MultiTool Retro", 80, 2, C_CURSOR);
                    fb_text_center("the bare-metal emulator for Orange Pi Lite", 120, 1, C_WHITE);
                    fb_text_center("Atari 2600 / 5200 / 7800", 150, 1, C_GREEN);
                    fb_text_center("CPU: Allwinner H3 Cortex-A7", 180, 1, C_WHITE);
                    fb_text_center("HDMI 1024x600  USB keyboard", 200, 1, C_GREY);
                    fb_text_center("SD FAT32 /roms/<system>/", 230, 1, C_GREY);
                    draw_footer("press any key"); fb_flush(); wait_key();
                    break;
                }
            } else if (c == 41 || c == 'q') { break; }
            udelay(10000);
        }
    }
}