#include <stdint.h>
#include <string.h>
#include "emu.h"
#include "a2600.h"
#include "fb_text.h"
#include "uart.h"
#include "usb_kbd.h"
#include "h3_de2_scaler.h"
#include "h3_hs_timer.h"

extern int printf(const char* fmt, ...);

#define EMU_FB  ((uint16_t*)0x5F800000)
#define FB_ADDR 0x5F900000
#define PHYS_W  1024
#define PHYS_H  600
#define EMU_SCR_W 160
#define EMU_SCR_H 192

void emu_clear_fb(void) {
    memset((void*)EMU_FB, 0, EMU_SCR_W * EMU_SCR_H * 2);
}

static void blit_emu_fb(void) {
    uint16_t* src = EMU_FB;
    volatile uint32_t* dst = (volatile uint32_t*)FB_ADDR;
    int sx = (PHYS_W - EMU_SCR_W) / 2;
    int sy = (PHYS_H - EMU_SCR_H) / 2;
    for (int y = 0; y < EMU_SCR_H; y++) {
        for (int x = 0; x < EMU_SCR_W; x++) {
            uint16_t p = src[y * EMU_SCR_W + x];
            uint32_t r = ((p >> 11) & 0x1F) << 3;
            uint32_t g = ((p >> 5) & 0x3F) << 2;
            uint32_t b = ((p >> 0) & 0x1F) << 3;
            dst[(sy + y) * PHYS_W + (sx + x)] = (r << 16) | (g << 8) | b;
        }
    }
    fb_flush();
}

extern void atari2600_init(const uint8_t* rom, uint32_t size);
extern void atari2600_run_frame(void);
extern int a7800_init_game(const uint8_t* rom, uint32_t size);
extern void a7800_run_frame(void);
extern int a5200_init_game(const uint8_t* rom, uint32_t size);
extern void a5200_run_frame(void);
extern int sms_init_game(const uint8_t* rom, uint32_t size);
extern void sms_run_frame(void);
extern void sms_render_frame(void);

static void emu_wait_key(void) {
    for (;;) {
        if (uart_rx_ready()) { uart_getc(); return; }
        uint8_t keys[6];
        if (usb_kbd_get_raw(keys, 6) > 0) return;
    }
}

// Throttle 60 FPS: ждать до 16667 мкс от начала кадра
static uint32_t emu_ts0 = 0;
static void emu_throttle(void) {
    uint32_t now = h3_hs_timer_lo_us();
    if (!emu_ts0) emu_ts0 = now;
    uint32_t elapsed = now - emu_ts0;
    if (elapsed < 16667)
        h3_hs_timer_delay(16667 - elapsed);
    emu_ts0 = h3_hs_timer_lo_us();
}

static void emu_enter_scale(int src_w, int src_h, int dst_w, int dst_h, uint32_t fb) {
    int dst_x = (1024 - dst_w) / 2;
    int dst_y = (600 - dst_h) / 2;
    de2_set_emu_mode(src_w, src_h, dst_w, dst_h, dst_x, dst_y, fb, 1);
}
static void emu_exit_scale(void) {
    de2_set_ui_mode(0x5F900000);
    fb_clear();
    fb_flush();
}

// Вспомогательный макрос: общий цикл кадров для всех эмуляторов
#define EMU_LOOP(frame_call, render_call) \
    emu_ts0 = 0; \
    for (;;) { \
        frame_call; \
        emu_throttle(); \
        render_call; \
        fb_flush(); \
        if ((fc % 60) == 0) printf("f=%u\n", (unsigned)fc); \
        fc++; \
        int nk = usb_kbd_get_raw(raw_keys, 6); \
        for (int i = 0; i < nk; i++) \
            if (raw_keys[i] == 41) goto exit; \
        if (uart_rx_ready()) break; \
    }

void emu_run_a7800(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb();
    fb_clear();
    volatile uint32_t* d = (volatile uint32_t*)FB_ADDR;
    for (int i = 0; i < 1024 * 600; i++) d[i] = 0x000000FF;
    fb_flush();

    if (a7800_init_game(rom, size) != 1) {
        printf("A7800: init failed\n");
        fb_clear();
        fb_text_center("A7800 init failed", 200, 2, 0x00FF4444);
        fb_flush();
        emu_wait_key();
        return;
    }
    printf("A7800: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);

    uint8_t raw_keys[6];
    uint32_t fc = 0;
    EMU_LOOP(a7800_run_frame(), (void)0)
exit:
    fb_clear();
    fb_flush();
}

void emu_run_a5200(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb();
    fb_clear();
    volatile uint32_t* d = (volatile uint32_t*)FB_ADDR;
    for (int i = 0; i < 1024 * 600; i++) d[i] = 0x000000FF;
    fb_flush();

    if (a5200_init_game(rom, size) != 1) {
        printf("A5200: init failed\n");
        fb_clear();
        fb_text_center("A5200 init failed", 200, 2, 0x00FF4444);
        fb_flush();
        emu_wait_key();
        return;
    }
    printf("A5200: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);

    uint8_t raw_keys[6];
    uint32_t fc = 0;
    EMU_LOOP(a5200_run_frame(), (void)0)
exit:
    fb_clear();
    fb_flush();
}

void emu_run_sms(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb();
    fb_clear();
    volatile uint32_t* d = (volatile uint32_t*)FB_ADDR;
    for (int i = 0; i < 1024 * 600; i++) d[i] = 0x000000FF;
    fb_flush();

    if (sms_init_game(rom, size) != 1) {
        printf("SMS: init failed\n");
        fb_clear();
        fb_text_center("SMS init failed", 200, 2, 0x00FF4444);
        fb_flush();
        emu_wait_key();
        return;
    }
    printf("SMS: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);

    uint8_t raw_keys[6];
    uint32_t fc = 0;
    emu_ts0 = 0;
    for (;;) {
        sms_run_frame();
        sms_render_frame();
        emu_throttle();
        fb_flush();
        if ((fc % 60) == 0) printf("sms f=%u\n", (unsigned)fc);
        fc++;
        int nk = usb_kbd_get_raw(raw_keys, 6);
        for (int i = 0; i < nk; i++)
            if (raw_keys[i] == 41) goto exit;
        if (uart_rx_ready()) break;
    }
exit:
    fb_clear();
    fb_flush();
}

void emu_run_a2600_mcume(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb();
    fb_clear();
    volatile uint32_t* d = (volatile uint32_t*)FB_ADDR;
    for (int i = 0; i < 1024 * 600; i++) d[i] = 0x000000FF;
    fb_flush();

    atari2600_init(rom, size);
    printf("MCUME: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);

    uint8_t raw_keys[6];
    uint32_t fc = 0;
    EMU_LOOP(atari2600_run_frame(), blit_emu_fb())
exit:
    emu_clear_fb();
    fb_clear();
    fb_flush();
}

void emu_run_a2600(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_clear_fb();
    fb_clear();
    // залить фон синим (будет видно до первого блита эмулятора)
    volatile uint32_t* d = (volatile uint32_t*)FB_ADDR;
    for (int i = 0; i < 1024 * 600; i++) d[i] = 0x000000FF;
    fb_flush();
    a2600_t a;
    a2600_init(&a, rom, size, EMU_FB);

    if (rom_name)
        printf("ROM: \"%s\"  size=%d  mapper=%s  pc=%04X\n",
               rom_name, (int)size, a.mapper_label, a.cpu.cpu.pc);
    else
        printf("ROM: size=%d  mapper=%s  pc=%04X\n",
               (int)size, a.mapper_label, a.cpu.cpu.pc);

    uint8_t joy_dir = 0;
    uint8_t joy_fire = 0;
    uint8_t raw_keys[6];
    uint32_t fc = 0;

    for (;;) {
        a2600_frame(&a);
        blit_emu_fb();

        if ((fc % 60) == 0) {
            uint32_t npix = 0;
            uint16_t* p = (uint16_t*)EMU_FB;
            for (int i = 0; i < EMU_SCR_W * EMU_SCR_H; i++)
                if (p[i]) npix++;
            extern uint32_t g_frame_insns, g_frame_lines, g_frame_vbl, g_frame_vsync;
            printf("f=%u pc=%04X pix=%lu insn=%lu lines=%lu vbl=%lu vsync=%lu\n",
                   (unsigned)fc, (unsigned)a.cpu.cpu.pc, (unsigned long)npix,
                   (unsigned long)g_frame_insns, (unsigned long)g_frame_lines,
                   (unsigned long)g_frame_vbl, (unsigned long)g_frame_vsync);
        }
        fc++;

        int nk = usb_kbd_get_raw(raw_keys, 6);
        joy_dir = 0;
        joy_fire = 0;
        for (int i = 0; i < nk; i++) {
            uint8_t sc = raw_keys[i];
            if (sc == 41) goto exit;
            if (sc == 82 || sc == 26) joy_dir |= 1;
            if (sc == 81 || sc == 22) joy_dir |= 2;
            if (sc == 80 || sc == 4)  joy_dir |= 4;
            if (sc == 79 || sc == 7)  joy_dir |= 8;
            if (sc == 44) joy_fire = 1;
            if (sc == 40) joy_fire = 1;
        }
        // тач как джойстик (временно отключён)
        a2600_set_input(joy_dir, joy_fire);
        if (uart_rx_ready()) break;
    }
exit:
    a2600_set_input(0, 0);
    emu_clear_fb();
    fb_clear();
    fb_flush();
}