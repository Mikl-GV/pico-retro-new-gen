/* System layer for the smsplus Sega Master System core on pico-retro RP2040.
 * Hybrid: pico-retro smsplus renderer (cacheStore 32K, sms_render_line) +
 * MCUME z80.c (read_rom for flash / direct for RAM pointers).
 * Provides: static memory pool (reusing NES buffers), ROM from flash (XIP),
 * line renderer to the ILI9341 and joypad input.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "display.h"
#include "joypad.h"
#include "audio.h"
#include "config.h"
}

extern "C" {
#include "shared.h"
#include "sms.h"
#include "vdp.h"
#include "render.h"
#include "system.h"
#include "z80.h"
}

/* ------------------------------------------------------------------ */
/* ROM access: cart.rom points into flash (XIP). MCUME z80.c uses       */
/* read_rom() only for cpu_readmap values < 0x80000 (offsets); our       */
/* sms.c uses real pointers (>= 0x10000000) so read_rom is never hit.    */
/* ------------------------------------------------------------------ */

extern "C" uint8 read_rom(int address)
{
    (void)address;
    return 0xFF;
}

extern "C" void write_rom(int address, uint8 val)
{
    (void)address; (void)val;
}

/* ------------------------------------------------------------------ */
/* Static memory pool — reuses NES-only buffers (SMS and NES never run   */
/* together). Regions: SCREEN(60K) + ChrBuf(32K) + RAM(8K) + SRAM(8K) +  */
/* PPURAM(16K) = 124K. SMS needs ~76K (cachePtr 4K + cacheStore 32K +    */
/* cacheStoreUsed + ram 8K + sram 32K).                                  */
/* ------------------------------------------------------------------ */

extern "C" uint8_t SCREEN[240][256];
extern "C" uint8_t ChrBuf[];
extern "C" uint8_t RAM[];
extern "C" uint8_t SRAM[];
extern "C" uint8_t PPURAM[];

static struct {
    uint8_t *base;
    int      size;
    int      used;
} sms_pool[] = {
    { (uint8_t *)&SCREEN[0][0], 240 * 256, 0 },
    { ChrBuf,                   256 * 2 * 8 * 8, 0 },
    { RAM,                      0x2000, 0 },
    { SRAM,                     0x2000, 0 },
    { PPURAM,                   0x4000, 0 },
};
#define SMS_POOL_REGS (sizeof(sms_pool) / sizeof(sms_pool[0]))

static void sms_pool_init(void)
{
    for (unsigned i = 0; i < SMS_POOL_REGS; i++) sms_pool[i].used = 0;
}

extern "C" void *frens_f_malloc(size_t size)
{
    for (unsigned i = 0; i < SMS_POOL_REGS; i++) {
        if (sms_pool[i].used + (int)size <= sms_pool[i].size) {
            void *p = sms_pool[i].base + sms_pool[i].used;
            sms_pool[i].used += (int)size;
            return p;
        }
    }
    printf("[sms] pool exhausted for %u bytes\n", (unsigned)size);
    return NULL;
}

extern "C" void frens_f_free(void *ptr) { (void)ptr; }

/* FatFS stubs — save-state functions in system.c are never used. */
extern "C" int f_write(FIL *fp, const void *buff, unsigned btw, unsigned *bw)
{ (void)fp; (void)buff; (void)btw; if (bw) *bw = 0; return FR_OK; }

extern "C" int f_read(FIL *fp, void *buff, unsigned btr, unsigned *br)
{ (void)fp; (void)buff; (void)btr; if (br) *br = 0; return FR_OK; }

extern "C" void system_load_sram(void) {}

/* ------------------------------------------------------------------ */
/* Renderer: smsplus draws one 256px line into linebuf and calls         */
/* sms_render_line(). We blit the line to the display.                   */
/* ------------------------------------------------------------------ */

static uint16_t sms_pal_rgb565[PALETTE_SIZE];
static uint16_t sms_line_rgb[256];

extern "C" void sms_palette_sync(int index)
{
    /* SMS CRAM: 2 bits per channel, R in bits 0-1, G in bits 2-3, B in
     * bits 4-5 (same layout as MCUME picosms). */
    uint8_t c = vdp.cram[index];
    int r = ((c >> 0) & 0x03) << 6;
    int g = ((c >> 2) & 0x03) << 6;
    int b = ((c >> 4) & 0x03) << 6;
    if (index < PALETTE_SIZE)
        sms_pal_rgb565[index] = RGB565(r >> 3, g >> 2, b >> 3);
}

extern "C" void sms_palette_syncGG(int index) { (void)index; }

/* SMS is natively 256x192. We render it 1:1 centred in the 320x240 window
 * (x=32, y=24) — same as NES — for crisp pixels and no scaling artifacts. */
extern "C" void sms_render_line(int line, const uint8_t *buffer)
{
    if (line < 0 || line >= 192) return;
    if (buffer == 0) return;   /* out-of-viewport blank line */
    for (int x = 0; x < 256; x++)
        sms_line_rgb[x] = sms_pal_rgb565[buffer[x] & 0x1F];
    display_stream_begin(32, 24 + line, 256, 1);
    display_stream_pixels16(sms_line_rgb, 256, 1);
    display_stream_end();
}

/* ------------------------------------------------------------------ */
/* Input                                                                */
/* ------------------------------------------------------------------ */

static void sms_poll_input(void)
{
    uint8_t pad = joypad_buttons();   /* 0 = pressed, NES bit order */
    int p = 0;
    if (!(pad & 0x10)) p |= INPUT_UP;
    if (!(pad & 0x20)) p |= INPUT_DOWN;
    if (!(pad & 0x40)) p |= INPUT_LEFT;
    if (!(pad & 0x80)) p |= INPUT_RIGHT;
    if (!(pad & 0x01)) p |= INPUT_BUTTON1;   /* A = button 1 */
    if (!(pad & 0x02)) p |= INPUT_BUTTON2;   /* B = button 2 */
    input.pad[0] = p;
    input.pad[1] = 0;
    /* Select = Pause button on the console body (like real SMS).
     * Start is NOT mapped to NMI — it's only used by main.cpp to exit. */
    input.system = 0;
    if (!(pad & 0x04)) input.system |= INPUT_PAUSE;  /* Select = Pause */
}

/* ------------------------------------------------------------------ */
/* Entry points used by main.cpp                                       */
/* ------------------------------------------------------------------ */

extern "C" int sms_init_game(const uint8_t *rom, uint32_t size)
{
    sms_pool_init();
    for (int i = 0; i < PALETTE_SIZE; i++) sms_pal_rgb565[i] = 0;

    /* loadrom.c points cart.rom at the flash (XIP) image. */
    if (!load_rom((uintptr_t)rom, (int)size, false)) {
        printf("[sms] load_rom failed\n");
        return 0;
    }

    system_init(AUDIO_SAMPLE_RATE);   /* enable SN76489 -> I2S */
    system_reset();
    z80_set_irq_callback(sms_irq_callback);
    return 1;
}

/* Смешиваем L/R каналы SN76489 в стерео-интерлив (L,R,L,R,...). */
static int16_t sms_mixbuf[735 * 2];

extern "C" void sms_run_frame(void)
{
    sms_poll_input();
    sms_frame(0);
    if (snd.enabled && snd.bufsize > 0) {
        int n = snd.bufsize;
        for (int i = 0; i < n; i++) {
            sms_mixbuf[i * 2]     = snd.buffer[0][i];
            sms_mixbuf[i * 2 + 1] = snd.buffer[1][i];
        }
        audio_play(sms_mixbuf, n);
    }
}

extern "C" void sms_render_frame(void)
{
    /* Lines are streamed inside sms_render_line() during sms_frame(). */
}
