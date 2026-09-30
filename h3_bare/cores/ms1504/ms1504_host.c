/* ms1504_host.c — «Электроника МС 1504» (8086/PC-XT, КР1834ВМ86) на ядре
   Fake86 для bare-metal H3.
   Порядок: rom_browser отдаёт файл ПЗУ (дамп BIOS ≤ 64 КБ) → кладём в верх
   RAM (0x100000-n), грузим «железо» (8253/8259/8237, video, диск), reset86().
   Цикл: exec86(execloops) → USB-клавиатура (HID → PC-сканкод, doirq(1))
   → рендер текстового кадра (CGA 80x25, шрифт 8x16) → EMU_FB 320x200.
   Выход: emu_esc_hold. Звук: off (манифест). */

#include <stdint.h>
#include <string.h>

#include "fake86_src/config.h"
#include "fake86_src/cpu.h"
#include "fake86_src/ports.h"
#include "fake86_src/i8253.h"
#include "fake86_src/i8259.h"
#include "fake86_src/i8237.h"
#include "fake86_src/video.h"
#include "fake86_src/bios.h"
#include "fake86_src/disk.h"
#include "fake86_src/sermouse.h"
#include "fake86_src/timing.h"
#include "fake86_src/input.h"

#include "emu.h"
#include "usb_kbd.h"
#include "fb_text.h"
#include "h3_hs_timer.h"

extern int printf(const char* fmt, ...);

#define EMU_FB      ((volatile uint16_t*)0x5F800000u)
#define EMU_FB_W    320
#define EMU_FB_H    240
#define MS1504_SCR_W 320
#define MS1504_SCR_H 200

// ---- HID-usage → PC set-1 сканкод (для МС1504) ----
// (0x04 = 'a' → 0x1E, 0x1E = '1' → 0x02, и т.д.)
static int ms1504_hid_to_pc(uint8_t h) {
    switch (h) {
    case 0x04: return 0x1E; case 0x05: return 0x30; case 0x06: return 0x2E;
    case 0x07: return 0x20; case 0x08: return 0x12; case 0x09: return 0x21;
    case 0x0A: return 0x22; case 0x0B: return 0x23; case 0x0C: return 0x17;
    case 0x0D: return 0x24; case 0x0E: return 0x25; case 0x0F: return 0x26;
    case 0x10: return 0x32; case 0x11: return 0x31; case 0x12: return 0x18;
    case 0x13: return 0x19; case 0x14: return 0x10; case 0x15: return 0x13;
    case 0x16: return 0x1F; case 0x17: return 0x14; case 0x18: return 0x16;
    case 0x19: return 0x2F; case 0x1A: return 0x11; case 0x1B: return 0x2D;
    case 0x1C: return 0x15; case 0x1D: return 0x2C;
    case 0x1E: return 0x02; case 0x1F: return 0x03; case 0x20: return 0x04;
    case 0x21: return 0x05; case 0x22: return 0x06; case 0x23: return 0x07;
    case 0x24: return 0x08; case 0x25: return 0x09; case 0x26: return 0x0A;
    case 0x27: return 0x0B;
    case 0x28: return 0x1C;   /* Enter */
    case 0x29: return 0x01;   /* Esc */
    case 0x2A: return 0x0E;   /* Backspace */
    case 0x2B: return 0x0F;   /* Tab */
    case 0x2C: return 0x39;   /* Space */
    case 0x2D: return 0x0C;   /* - */
    case 0x2E: return 0x0D;   /* = */
    case 0x2F: return 0x1A;   /* [ */
    case 0x30: return 0x1B;   /* ] */
    case 0x31: return 0x2B;   /* \ */
    case 0x33: return 0x27;   /* ; */
    case 0x34: return 0x28;   /* ' */
    case 0x35: return 0x29;   /* ` */
    case 0x36: return 0x33;   /* , */
    case 0x37: return 0x34;   /* . */
    case 0x38: return 0x35;   /* / */
    case 0x39: return 0x3A;   /* CapsLock */
    case 0x3A: return 0x3B; case 0x3B: return 0x3C; case 0x3C: return 0x3D;
    case 0x3D: return 0x3E; case 0x3E: return 0x3F; case 0x3F: return 0x40;
    case 0x40: return 0x41; case 0x41: return 0x42; case 0x42: return 0x43;
    case 0x43: return 0x44; case 0x44: return 0x57; case 0x45: return 0x58;
    case 0x46: return 0x37;   /* PrintScreen */
    case 0x47: return 0x46;   /* ScrollLock */
    case 0x49: return 0x52;   /* Insert */
    case 0x4A: return 0x47;   /* Home */
    case 0x4B: return 0x49;   /* PageUp */
    case 0x4C: return 0x53;   /* Delete */
    case 0x4D: return 0x4F;   /* End */
    case 0x4E: return 0x51;   /* PageDown */
    case 0x4F: return 0x4D;   /* Right */
    case 0x50: return 0x4B;   /* Left */
    case 0x51: return 0x50;   /* Down */
    case 0x52: return 0x48;   /* Up */
    /* Keypad (NumLock on): 54=/ 55=* 56=- 57=+ 58=Enter 59-61=1-3 ... */
    case 0x54: return 0x35;
    case 0x55: return 0x37;
    case 0x56: return 0x4A;
    case 0x57: return 0x4E;
    case 0x58: return 0x1C;
    case 0x59: return 0x4F; case 0x5A: return 0x50; case 0x5B: return 0x51;
    case 0x5C: return 0x4B; case 0x5D: return 0x4C; case 0x5E: return 0x4D;
    case 0x5F: return 0x47; case 0x60: return 0x48; case 0x61: return 0x49;
    case 0x62: return 0x52; case 0x63: return 0x53;
    /* Mods: Ctrl/Shift/Alt — только фронт? нет: подаём как сканкоды */
    case 0xE0: case 0xE4: return 0x1D;   /* Ctrl */
    case 0xE1: return 0x2A;  case 0xE5: return 0x36;   /* Shift */
    case 0xE2: case 0xE6: return 0x38;   /* Alt */
    default: return -1;
    }
}

static uint8_t g_kbd_prev[256];

// Подать сканкод в «клавиатуру» ПК (как input.c): порт 0x60 + doirq(1).
static void feed_pc_key(uint8_t scan, int down) {
    portram[0x60] = (uint8_t)(scan | (down ? 0 : 0x80));
    portram[0x64] |= 2;                 // input buffer full
    doirq(1);
}

static void ms1504_kbd_update(void) {
    uint8_t raw[8];
    int n = usb_kbd_get_raw(raw, 8);
    for (int i = 0; i < n; i++) {
        int pc = ms1504_hid_to_pc(raw[i]);
        if (pc < 0) continue;
        if (!g_kbd_prev[pc]) {
            g_kbd_prev[pc] = 1;
            feed_pc_key((uint8_t)pc, 1);
        }
    }
    // отпущенные: сканкод, которого нет в текущем отчёте
    for (int pc2 = 1; pc2 < 0x59; pc2++) {
        if (g_kbd_prev[pc2]) {
            int still = 0;
            for (int i = 0; i < n; i++)
                if (ms1504_hid_to_pc(raw[i]) == pc2) { still = 1; break; }
            if (!still) {
                g_kbd_prev[pc2] = 0;
                // автоповтор USB не переопрашиваем: отпускаем сразу
                feed_pc_key((uint8_t)pc2, 0);
            }
        }
    }
}

// ---- рендер CGA/текст в EMU_FB 320x200 (RGB565) ----
// Пишем напрямую в EMU_FB (r0.410: статический backbuffer ms1504_scr
// был мёртвым кодом — 128 КБ неиспользуемого BSS, удалён).
static uint16_t ms1504_rgb565(uint32_t rgb888) {
    uint8_t r = (uint8_t)(rgb888 >> 16), g = (uint8_t)(rgb888 >> 8), b = (uint8_t)rgb888;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void ms1504_render_into(uint16_t* dst, int dw, int dh) {
    // r0.410 (S12): при смене видеорежима BIOS/myBIOS может выставить
    // videobase в конец RAM — читаем только текстовую зону 0xB8000:
    // чтение до +4096 (25×80×2) за пределы RAM[0x100000] = OOB-read.
    uint32_t vb = videobase;
    if (vb > RAM_SIZE - 4096) vb = 0xB8000;
    const uint8_t* text = RAM + vb;
    uint16_t ncols = cols ? cols : 80;
    for (int y = 0; y < dh; y++) {
        int sy = y * 2;          // 640x400 → 320x200 (×2)
        int cy = sy >> 4;        // 16 строк шрифта на ряд
        int fy = sy & 15;
        const uint8_t* row = text + ((size_t)cy * ncols) * 2;
        for (int x = 0; x < dw; x++) {
            int sx = x * 2;
            int cx = sx >> 3;
            int fx = sx & 7;
            uint8_t ch = row[cx * 2];
            uint8_t at = row[cx * 2 + 1];
            uint32_t fg = palettecga[at & 15];
            uint32_t bg = palettecga[(at >> 4) & 7];
            uint8_t dat = fontcga ? fontcga[(size_t)ch * 16 + fy] : 0;
            uint32_t c = (dat & (0x80 >> fx)) ? fg : bg;
            dst[y * dw + x] = ms1504_rgb565(c);
        }
    }
    (void)EMU_FB;
}

// показать в EMU_FB 320x240 (кадр 320x200 сверху) + эмуляционное изм.
static void ms1504_video_flush(void) {
    ms1504_render_into((uint16_t*)EMU_FB, MS1504_SCR_W, MS1504_SCR_H);
}

// ---- init ----
static int ms1504_inited = 0;

static void ms1504_hardware_init(void) {
    if (ms1504_inited) return;
    memset(RAM, 0, RAM_SIZE);
    memset(readonly, 0, RAM_SIZE);
    ports_init();
    init8253();
    init8259();
    init8237();
    initVideoPorts();
    initcga();
    inittiming();
    ms1504_inited = 1;
}

// ---- собственный BIOS (вшит) для режима «Встроенное ПО» ----
extern const unsigned char ms1504_bios_pk300[];
extern const unsigned int  ms1504_bios_pk300_len;
extern const unsigned char ms1504_hdd_bios[];
extern const unsigned int  ms1504_hdd_bios_len;

int ms1504_init_game(const uint8_t* rom, uint32_t size) {
    printf("MS1504: init size=%u\n", (unsigned)size);
    if (!rom || size == 0) {
        // r0.399: режим «Встроенное ПО» — родной BIOS (PK300) вшит в прошивку
        rom = ms1504_bios_pk300;
        size = ms1504_bios_pk300_len;
        printf("MS1504: builtin BIOS (PK300, вшит, %u Б)\n", (unsigned)size);
    } else if (size > 0x10000) {
        printf("MS1504: файл %u Б слишком большой — это не образ ПЗУ МС-1504 (нужен ≤64К)\n",
               (unsigned)size);
        return 0;
    }

    ms1504_hardware_init();
    memset(RAM, 0, RAM_SIZE);
    memset(readonly, 0, RAM_SIZE);

    // BIOS вверх RAM: F0000-FFFFF (как loadbios в main.c)
    memcpy(RAM + 0x100000 - size, rom, size);
    memset(readonly + 0x100000 - size, 1, size);
    printf("MS1504: BIOS %u Б -> 0x%05X, readonly\n", (unsigned)size,
           (unsigned)(0x100000 - size));

    running = 1;
    reset86();
    printf("MS1504: cpu reset (4.77 МГц КР1834ВМ86), boot\n");
    return 1;
}

// ---- точка входа из rom_browser (emu.h) ----
void emu_run_ms1504(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    snd_manifest("ms1504", "none");
    if (!ms1504_init_game(rom, size)) return;
    emu_set_border_color(0x00000000);
    emu_throttle_reset();
    emu_esc_hold_reset();
    memset(g_kbd_prev, 0, sizeof(g_kbd_prev));

    printf("MS1504: \"%s\" (%u Б) — execute\n", rom_name ? rom_name : "?", (unsigned)size);
    for (;;) {
        ms1504_kbd_update();
        exec86(120000);
        // INT10 (видеопорти/спецрежимы) обрабатывает сам загруженный BIOS
        ms1504_video_flush();
        emu_scale(MS1504_SCR_W, MS1504_SCR_H);   // EMU_FB -> HDMI FB 1024x600
        fb_flush();
        emu_throttle();
        if (emu_esc_hold()) break;
    }

    running = 0;
    fb_clear(); fb_flush();
    printf("MS1504: exit\n");
}