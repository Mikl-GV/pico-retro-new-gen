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

const uint8_t* test_a5200_get_bios(void);
const uint8_t* test_a5200_get_cart(void);
void test_a5200_setup_dl(a5200_t* m);

int h3_de2_init(struct display_timing *timing, uint32_t fbbase);
void h3_hs_timer_init(void);
void udelay(uint32_t d);

#define PHYS_W  1024
#define PHYS_H  600
// Orange Pi Lite: 512MB DRAM = 0x40000000..0x60000000
#define FB_ADDR 0x5F900000       // HDMI framebuffer (XRGB8888 1024x600)
#define EMU_FB  0x5F800000       // emu framebuffer (RGB565, 320x240)
#define ROM_BUF 0x50000000       // буфер ROM с SD (до 24MB)
#define ROM_MAX (24 * 1024 * 1024)

static void blit_emu_fb(void) {
    uint16_t* src = (uint16_t*)EMU_FB;
    volatile uint32_t* dst = (volatile uint32_t*)FB_ADDR;
    int sx = 32, sy = 0;
    for (int y = 0; y < 240 && sy + 2 < PHYS_H; y++) {
        for (int x = 0; x < 320; x++) {
            uint16_t p = src[y * 320 + x];
            uint32_t px = ((uint32_t)(p & 0x001F) << 3) |
                          ((uint32_t)(p & 0x07E0) << 5) |
                          ((uint32_t)(p & 0xF800) << 8);
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
        sx = 32; sy += 3;
    }
}

static void dbg_pc(uint16_t pc) {
    char b[8] = "0x0000\n";
    const char* hx = "0123456789ABCDEF";
    b[2] = hx[(pc >> 12) & 0xF];
    b[3] = hx[(pc >> 8) & 0xF];
    b[4] = hx[(pc >> 4) & 0xF];
    b[5] = hx[pc & 0xF];
    uart_puts(b);
}

typedef struct {
    const char* name;
    const char* ext;
    int plat;
} rom_map_t;

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
    *rom = buf;
    *size = load_size;
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
    0xA9, 0x14, 0x85, 0x08, 0x85, 0x02,
    0xA9, 0xAA, 0x85, 0x0E, 0x85, 0x0F, 0xCA, 0xD0, 0xF3,
    0xA9, 0x02, 0x85, 0x01, 0xA2, 0x1E, 0x85, 0x02, 0xCA, 0xD0, 0xFB,
    0xA9, 0x00, 0x85, 0x01, 0x4C, 0x05, 0xF0,
    [0xFFC] = 0x00, 0xF0,
    [0xFFE] = 0x00, 0xF0,
};

static void show_menu(void) {
    uart_puts("\n=== MultiTool Retro ===\n");
    uart_puts("1 - Atari 2600 (test)\n");
    uart_puts("2 - Atari 5200 (test)\n");
    uart_puts("3 - Atari 7800 (Asteroids)\n");
    uart_puts("4 - SD: Atari 2600\n");
    uart_puts("5 - SD: Atari 5200\n");
    uart_puts("6 - SD: Atari 7800\n");
    uart_puts("s - SD init\n");
    uart_puts("l - list ROMs\n");
    uart_puts("Выбор: ");
}

void main(void) {
    uint16_t* emu_fb = (uint16_t*)EMU_FB;
    uint32_t frames = 0;
    int prev_touch = 0;
    int sd_ok = 0;

    uart_init();
    uart_puts("\nH3 emu boot\n");   // [0] UART жив
    h3_hs_timer_init();
    uart_puts("timer ok\n");        // [1]

    struct display_timing timing;
    memset(&timing, 0, sizeof(timing));
    timing.hdmi_monitor = 1;
    timing.pixelclock.typ = 51200000;
    timing.hactive.typ = 1024;
    timing.hfront_porch.typ = 160;
    timing.hback_porch.typ = 88;
    timing.hsync_len.typ = 40;
    timing.vactive.typ = 600;
    timing.vfront_porch.typ = 12;
    timing.vback_porch.typ = 20;
    timing.vsync_len.typ = 3;
    timing.flags = (DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW);

    uart_puts("HDMI init...\n");    // [2]
    if (h3_de2_init(&timing, FB_ADDR) != 0) {
        uart_puts("HDMI FAILED\n");
        while (1) udelay(1000000);
    }
    uart_puts("HDMI ok\n");         // [3]

    touch_init();
    uart_puts("touch init ok\n");   // [4]

    // USB-клавиатура (не блокируем, если её нет — меню работает и так)
    uart_puts("USB kbd init...\n");
    if (usb_kbd_init() == 0) {
        uart_puts("USB kbd ready\n");
    } else {
        uart_puts("USB kbd not found (touch/UART ok)\n");
    }

    show_menu();
    char sel = 0;
    while (!sel) {
        char c = 0;
        if (uart_rx_ready()) {
            c = uart_getc();
            // Принимаем ТОЛЬКО валидные клавиши, мусор игнорируем
            if ((c >= '1' && c <= '9') || c == 's' || c == 'l')
                sel = c;
        } else {
            int k = usb_kbd_poll();
            char m = 0;
            if (k == 30) m = '1'; else if (k == 31) m = '2';
            else if (k == 32) m = '3'; else if (k == 33) m = '4';
            else if (k == 34) m = '5'; else if (k == 35) m = '6';
            else if (k == 22) m = 's'; else if (k == 15) m = 'l';
            else { int x, y;
                if (touch_read(&x, &y)) {
                    if (y < 1200 && !prev_touch) m = '1';
                    else if (y < 2400 && !prev_touch) m = '2';
                    else if (y < 3600 && !prev_touch) m = '3';
                    else if (y < 4800 && !prev_touch) m = '4';
                    else if (y < 6000 && !prev_touch) m = '5';
                    else if (!prev_touch) m = '6';
                }
            }
            if (m) sel = m;
        }
        prev_touch = 0;
        udelay(10000);

        if (sel == 's' || sel == 'l') {
            uart_puts("\n");
            if (!sd_ok) {
                sd_ok = (sd_init() == 0) && (fat_init() == 0);
            }
            if (sd_ok && sel == 'l') {
                fat_entry_t list[32];
                const char* dirs[3] = {"/roms/a2600", "/roms/a5200", "/roms/a7800"};
                const char* labels[3] = {" [2600]", " [5200]", " [7800]"};
                uart_puts("ROMs:\n");
                for (int d = 0; d < 3; d++) {
                    int n = fat_list(dirs[d], list, 32);
                    for (int i = 0; i < n; i++) {
                        uart_puts(list[i].name); uart_puts(labels[d]); uart_puts("\n");
                    }
                }
            }
            if (!sd_ok) uart_puts("SD not ready\n");
            sel = 0;
            show_menu();
        }
    }
    uart_puts("\n");

    // ================= 2600 (test kernel) =================
    if (sel == '1') {
        uart_puts("Running A2600\n");
        a2600_t a26;
        a2600_init(&a26, kernel_2600, 4096, emu_fb);
        while (1) {
            a2600_frame(&a26);
            blit_emu_fb();
            if ((frames++ & 0x3F) == 0) { uart_puts("a2600 pc="); dbg_pc(a26.cpu.pc); }
        }
    }
    // ================= 5200 (test) =================
    else if (sel == '2') {
        uart_puts("Running A5200 (mini BIOS)\n");
        a5200_t a52;
        a5200_init(&a52, test_a5200_get_cart(), 16384,
                   test_a5200_get_bios(), emu_fb);
        test_a5200_setup_dl(&a52);
        while (1) {
            a5200_frame(&a52);
            blit_emu_fb();
            if ((frames++ & 0x3F) == 0) { uart_puts("a5200 pc="); dbg_pc(a52.cpu.pc); }
        }
    }
    // ================= 7800 (Asteroids) =================
    else if (sel == '3') {
        uart_puts("Running A7800 Asteroids\n");
        a7800_t a78;
        a7800_init(&a78, rom_a7800_asteroids, sizeof(rom_a7800_asteroids),
                   NULL, 0, emu_fb);
        uart_puts("pc="); dbg_pc(a78.cpu.pc);
        while (1) {
            a7800_frame(&a78);
            blit_emu_fb();
            if ((frames++ & 0x3F) == 0) { uart_puts("a7800 pc="); dbg_pc(a78.cpu.pc); }
        }
    }
    // ================= SD ROMs (4/5/6) =================
    else if (sel >= '4' && sel <= '6') {
        uint8_t* rom = 0;
        uint32_t rom_size = 0;
        int plat = 0;
        if (sel == '4') plat = ROM_PLAT_A2600;
        else if (sel == '5') plat = ROM_PLAT_A5200;
        else if (sel == '6') plat = ROM_PLAT_A7800;

        if (plat && sd_ok) {
            fat_entry_t list[32];
            int n = list_roms(plat, list, 32);
            for (int i = 0; i < n && !rom; i++) {
                load_rom(rom_dir_for_plat(plat), list[i].name, &rom, &rom_size,
                         (plat == ROM_PLAT_A7800));
            }
        }

        if (!rom) {
            uart_puts("No ROM found\n");
            // БЕЗ автозапуска дефолтной игры: ждать выбора снова
            while (1) udelay(1000000);
        }

        if (rom) {
            uart_puts("Running from SD...\n");
            if (plat == ROM_PLAT_A2600) {
                a2600_t a26;
                a2600_init(&a26, rom, rom_size, emu_fb);
                while (1) {
                    a2600_frame(&a26);
                    blit_emu_fb();
                    if ((frames++ & 0x3F) == 0) { uart_puts("a2600 pc="); dbg_pc(a26.cpu.pc); }
                }
            } else if (plat == ROM_PLAT_A5200) {
                a5200_t a52;
                a5200_init(&a52, rom, rom_size, test_a5200_get_bios(), emu_fb);
                test_a5200_setup_dl(&a52);
                while (1) {
                    a5200_frame(&a52);
                    blit_emu_fb();
                    if ((frames++ & 0x3F) == 0) { uart_puts("a5200 pc="); dbg_pc(a52.cpu.pc); }
                }
            } else if (plat == ROM_PLAT_A7800) {
                a7800_t a78;
                a7800_init(&a78, rom, rom_size, NULL, 0, emu_fb);
                while (1) {
                    a7800_frame(&a78);
                    blit_emu_fb();
                    if ((frames++ & 0x3F) == 0) { uart_puts("a7800 pc="); dbg_pc(a78.cpu.pc); }
                }
            }
        }
    }
}