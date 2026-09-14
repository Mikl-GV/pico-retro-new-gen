// system_sms_h3.cpp — host-слой SMS/GG для H3.
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "uart.h"
#include "usb_kbd.h"
}

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240
#define SCR_W   256
#define SCR_H   192

extern "C" {
#include "smsplus/types.h"
#include "smsplus/system.h"
#include "smsplus/sms.h"
#include "smsplus/vdp.h"
#include "smsplus/loadrom.h"
#include "smsplus/z80.h"
}

static uint16_t sms_pal_rgb565[PALETTE_SIZE];

extern "C" void* frens_f_malloc(size_t size) {
    static uint8_t sms_heap[128 * 1024];
    static int pos = 0;
    if (pos + (int)size > (int)sizeof(sms_heap)) return NULL;
    void* p = sms_heap + pos;
    pos += (int)size;
    return p;
}
extern "C" void frens_f_free(void*) {}

extern "C" uint8 read_rom(int) { return 0xFF; }
extern "C" void write_rom(int, uint8) {}
extern "C" int f_write(void*, const void*, unsigned, unsigned*) { return 0; }
extern "C" int f_read(void*, void*, unsigned, unsigned*) { return 0; }
extern "C" void system_load_sram(void) {}

extern "C" void sms_palette_sync(int index) {
    uint8 c = vdp.cram[index];
    int r = ((c >> 0) & 3) << 6;
    int g = ((c >> 2) & 3) << 6;
    int b = ((c >> 4) & 3) << 6;
    if (index < PALETTE_SIZE)
        sms_pal_rgb565[index] = (r >> 3 << 11) | (g >> 2 << 5) | (b >> 3);
}

extern "C" void sms_palette_syncGG(int) {}

static uint8_t last_fb[SCR_H][SCR_W];

extern "C" void sms_render_line(int line, const uint8_t* buffer) {
    if (line < 0 || line >= SCR_H) return;
    if (!buffer) return;
    for (int x = 0; x < SCR_W; x++)
        last_fb[line][x] = buffer[x] & 0x1F;
}

static void blit_fb(void) {
    for (int y = 0; y < SCR_H; y++)
        for (int x = 0; x < SCR_W; x++)
            EMU_FB[y * EMU_W + x] = sms_pal_rgb565[last_fb[y][x]];
}

static void sms_poll_input(void) {
    uint8_t keys[6];
    int n = usb_kbd_get_raw(keys, 6);
    int p = 0;
    for (int i = 0; i < n; i++) {
        uint8_t sc = keys[i];
        if (sc == 82) p |= INPUT_UP;
        if (sc == 81) p |= INPUT_DOWN;
        if (sc == 80) p |= INPUT_LEFT;
        if (sc == 79) p |= INPUT_RIGHT;
        if (sc == 29) p |= INPUT_BUTTON1;
        if (sc == 27) p |= INPUT_BUTTON2;
        if (sc == 22) p |= INPUT_PAUSE;
    }
    input.pad[0] = p;
    input.pad[1] = 0;
    input.system = 0;
}

extern "C" int sms_init_game(const uint8_t* rom, uint32_t size) {
    for (int i = 0; i < PALETTE_SIZE; i++) sms_pal_rgb565[i] = 0;
    memset(last_fb, 0, sizeof(last_fb));

    if (!load_rom((uintptr_t)rom, (int)size, false)) {
        printf("[sms] load_rom failed\n");
        return 0;
    }
    system_init(44100);
    system_reset();
    z80_set_irq_callback(sms_irq_callback);
    printf("[sms] loaded size=%d pages=%d\n", (int)size, cart.pages);
    return 1;
}

extern "C" void sms_run_frame(void) {
    sms_poll_input();
    sms_frame(0);
}

extern "C" void sms_render_frame(void) {
    blit_fb();
}