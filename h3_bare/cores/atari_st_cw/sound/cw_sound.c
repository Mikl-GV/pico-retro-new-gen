/* cw_sound.c — генератор YM2149 для Castaway на H3 (r1566)
 *
 * AY8910.c (EMULib) считает частоты/громкости каналов и зовёт Sound().
 * Здесь — квадратная волна 3 тональных каналов + шум, 44100 Гц, и вывод
 * в I2S-кольцо по одному PAL-кадру (50 Гц, 882 пары).
 */
#include <stdint.h>
#include <string.h>

#include "AY8910.h"
#include "cw_sound.h"
#include "i2s.h"

#define CW_SR        44100u
#define CW_CH        6
#define CW_TONE_CH   3          /* первые 3 канала — тональные */
#define CW_FRAME_HZ  50u

static AY8910   g_psg;
static int      g_ready;

static int32_t  g_freq[CW_CH];
static int32_t  g_vol[CW_CH];
static uint32_t g_phase[CW_CH];

/* Установка параметров канала — вызывает AY8910.c. */
void Sound(int n, int freq, int vol)
{
    if (n < 0 || n >= CW_CH)
        return;
    g_freq[n] = freq;
    g_vol[n]  = vol;
}

void cw_psg_ctrl(uint8_t v)
{
    WrCtrl8910(&g_psg, v);
}

void cw_psg_data(uint8_t v)
{
    WrData8910(&g_psg, v);
}

void cw_sound_reset(void)
{
    memset(&g_psg, 0, sizeof(g_psg));
    Reset8910(&g_psg, 2000000, 0);      /* YM2149 на ST: 2 МГц */
    memset(g_freq, 0, sizeof(g_freq));
    memset(g_vol, 0, sizeof(g_vol));
    memset(g_phase, 0, sizeof(g_phase));
    g_ready = 1;
}

void cw_sound_render(void)
{
    static int16_t buf[2 * (CW_SR / 40)];   /* с запасом на 50 Гц */
    const uint32_t n = CW_SR / CW_FRAME_HZ; /* 882 пары */

    if (!g_ready)
        return;

    /* r1581: AY8910 сам считает период огибающей; без Loop8910 она не
     * шагает, и keyclick TOS застывает в постоянный писк. */
    Loop8910(&g_psg, (int)(1000000u / CW_FRAME_HZ));

    for (uint32_t i = 0; i < n; i++) {
        int32_t acc = 0;

        for (int c = 0; c < CW_TONE_CH; c++) {
            if (!g_vol[c] || !g_freq[c])
                continue;
            /* фаза в 1/65536 периода: inc = freq * 65536 / SR */
            g_phase[c] += ((uint32_t)g_freq[c] << 16) / CW_SR;
            acc += (g_phase[c] & 0x8000u) ? g_vol[c] : -g_vol[c];
        }

        /* Канал 3 — шум (упрощённый LFSR, амплитуда от канала 3). */
        if (g_vol[3] && g_freq[3]) {
            static uint32_t lfsr = 0x1D872B41u;
            static uint32_t ph;
            ph += ((uint32_t)g_freq[3] << 16) / CW_SR;
            if (ph & 0x8000u) {
                uint32_t bit = (lfsr ^ (lfsr >> 2)) & 1u;
                lfsr = (lfsr >> 1) | (bit << 31);
            }
            acc += (lfsr & 1u) ? g_vol[3] : -g_vol[3];
        }

        int32_t s = acc * 96;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        buf[2 * i]     = (int16_t)s;
        buf[2 * i + 1] = (int16_t)s;
    }

    i2s_push_block(buf, n);
}
