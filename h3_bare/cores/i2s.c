// i2s.c — I2S0 H3 (0x01C22000) для MAX98357A, 48 кГц стерео 16-бит.
//
// Схема (обработка потока эмулятора, как у консолей):
//   - i2s_push_sample(): эмулятор кладёт сэмплы в РАМ-кольцо МГНОВЕННО,
//     без ожидания (не тормозит кадр). Громкость — здесь.
//   - i2s_flush(): вызывается из emu_throttle много раз в течение кадра,
//     переносит кольцо → аппаратный TX-FIFO с РИТМОМ 48 кГц. Если кольцо
//     пусто — пишет тишину (0), чтобы FIFO не проседал до нуля (нет шума).
//
// ТАЙМЕР: HSTMR тактируется от PLL (~96.8 МГц), h3_hs_timer_lo_us() даёт
// настоящие микросекунды (делитель 97, см. h3_hs_timer.h r127).
// Для 48 кГц нужна пара за 20.8 мкс → I2S_PACE_UNITS=21.
// (До r127 единица была 0.248 мкс и PACE=21 давал ~190 кГц — звук ускорялся.)
#include <stdint.h>
#include "h3.h"
#include "h3_ccu.h"
#include "h3_hs_timer.h"

extern int printf(const char* fmt, ...);

#define I2S_BASE 0x01C22000u

#define I2S_CTRL     (*(volatile uint32_t*)(I2S_BASE + 0x00))
#define I2S_FMT0     (*(volatile uint32_t*)(I2S_BASE + 0x04))
#define I2S_FMT1     (*(volatile uint32_t*)(I2S_BASE + 0x08))
#define I2S_RX_FIFO  (*(volatile uint32_t*)(I2S_BASE + 0x10))
#define I2S_FIFO_CTL (*(volatile uint32_t*)(I2S_BASE + 0x14))
#define I2S_FIFO_STA (*(volatile uint32_t*)(I2S_BASE + 0x18))
#define I2S_FIFO_TX  (*(volatile uint32_t*)(I2S_BASE + 0x20))
#define I2S_CLK_DIV  (*(volatile uint32_t*)(I2S_BASE + 0x24))
#define I2S_TX_CNT   (*(volatile uint32_t*)(I2S_BASE + 0x28))
#define I2S_TX_CSEL  (*(volatile uint32_t*)(I2S_BASE + 0x34))
#define I2S_TX_CMAP  (*(volatile uint32_t*)(I2S_BASE + 0x44))
#define I2S_CHAN_CFG (*(volatile uint32_t*)(I2S_BASE + 0x30))

#define I2S_CTRL_GL_EN     (1u << 0)
#define I2S_CTRL_TX_EN     (1u << 2)
#define I2S_CTRL_SDO_EN0   (1u << 8)
#define I2S_CTRL_LRCK_OUT  (1u << 17)
#define I2S_CTRL_BCLK_OUT  (1u << 18)

#define I2S_FMT0_LRCKPER(v)  (((v)-1) << 8)
#define I2S_CLK_MCLK_EN  (1u << 8)
#define I2S_CLK_BCLK(v)  ((v) << 4)
#define I2S_CLK_MCLK(v)  ((v) << 0)

#define I2S_TX_CHAN_OFF(v)   ((v) << 12)
#define I2S_CHAN_TXSLOT(v)   ((v)-1)
#define I2S_CHAN_RXSLOT(v)   (((v)-1) << 4)

#define PG_BASE    (H3_PIO_BASE + 0xD8u)
#define PG_CFG0    (*(volatile uint32_t*)(PG_BASE + 0x00u))
#define PG_DAT     (*(volatile uint32_t*)(PG_BASE + 0x10u))

#define CCU_RST_I2S0 (*(volatile uint32_t*)(H3_CCU_BASE + 0x2D0u))
#define PLL_AUDIO_CTRL (*(volatile uint32_t*)(H3_CCU_BASE + 0x008u))
#define PLL_AUDIO_PAT  (*(volatile uint32_t*)(H3_CCU_BASE + 0x284u))

static int pll_audio_lock_wait(void) {
    uint32_t t = 0;
    while (!(PLL_AUDIO_CTRL & (1u << 28)) && t++ < 1000000) ;
    return (PLL_AUDIO_CTRL & (1u << 28)) ? 0 : -1;
}

static void pll_audio_enable(void) {
    if ((PLL_AUDIO_CTRL & (1u << 31)) && (PLL_AUDIO_CTRL & (1u << 28))) {
        printf("I2S: PLL 24.576M already on\n");
        return;
    }
    PLL_AUDIO_PAT = 0xc000ac02u;
    PLL_AUDIO_CTRL = (1u << 31) | (1u << 24) | ((14 - 1) << 8) | (14 - 1);
    udelay(3000);
    if (pll_audio_lock_wait() == 0) printf("I2S: PLL 24.576 MHz locked\n");
    else printf("I2S: PLL LOCK TIMEOUT\n");
}

static int g_i2s_ready = 0;
static int g_volume_pct = 20;
static int g_muted = 1;
static uint32_t g_i2s_next = 0;   // r124: момент следующей пары (для повторного init)

void i2s_volume(int p) {
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    g_volume_pct = p;
    printf("audio: volume=%d%%\n", p);
}
int i2s_volume_pct(void) { return g_volume_pct; }
void i2s_mute(int m) { g_muted = m; if (m) PG_DAT &= ~(1u<<7); else PG_DAT |= (1u<<7); }

int i2s_init(void) {
    pll_audio_enable();

    H3_CCU->BUS_CLK_GATING2 |= (1u << 12);
    CCU_RST_I2S0 |= (1u << 12);

    volatile uint32_t* clk = &H3_CCU->I2SPCM0_CLK;
    *clk = (*clk & ~((3u << 16) | (1u << 31))) | (3u << 16) | (1u << 31);
    udelay(2000);

    uint32_t c2 = H3_PIO_PORTA->CFG2;
    c2 &= ~((0xFu << 8) | (0xFu << 12) | (0xFu << 16) | (0xFu << 20));
    c2 |= (2u << 8) | (2u << 12) | (2u << 16);
    H3_PIO_PORTA->CFG2 = c2;

    PG_CFG0 = (PG_CFG0 & ~(0xFu << 28)) | (1u << 28);
    PG_DAT &= ~(1u << 7);

    I2S_CTRL = 0;
    I2S_FIFO_CTL |= (1u << 24) | (1u << 25);
    udelay(100);
    for (int i = 0; i < 256; i++) { volatile uint32_t d = I2S_RX_FIFO; (void)d; }

    // 48к: WSS=7 (32-бит слот), SR=3 (16-бит), LRCKPER=64, BCLK 3.072M
    I2S_FMT0 = (7u << 0) | (3u << 4) | (0u << 7) | I2S_FMT0_LRCKPER(64) | (0u << 19);
    I2S_FMT1 = 0;
    I2S_CLK_DIV = I2S_CLK_MCLK_EN | I2S_CLK_BCLK(5) | I2S_CLK_MCLK(2);

    I2S_TX_CMAP = 0x76543210;
    I2S_TX_CSEL = (3u << 4) | I2S_TX_CHAN_OFF(1) | 1;
    I2S_CHAN_CFG = I2S_CHAN_TXSLOT(2) | I2S_CHAN_RXSLOT(2);

    I2S_CTRL = I2S_CTRL_BCLK_OUT | I2S_CTRL_LRCK_OUT | (1u << 4)
             | I2S_CTRL_TX_EN | I2S_CTRL_SDO_EN0 | I2S_CTRL_GL_EN;
    udelay(1000);

    g_i2s_next = 0;   // r124: сброс темпа при (повторном) init
    g_i2s_ready = 1;
    printf("I2S: ready (48000 Hz, vol=%d%%)\n", g_volume_pct);
    return 0;
}

// ---- кольцо потока эмулятора ----
#define AUDIO_RING_SIZE 8192
static int16_t g_ring_l[AUDIO_RING_SIZE];
static int16_t g_ring_r[AUDIO_RING_SIZE];
static volatile uint32_t g_ring_wr = 0;
static volatile uint32_t g_ring_rd = 0;   // r124: volatile — читается в нескольких местах
static inline uint32_t ring_count(void) { return (uint32_t)(g_ring_wr - g_ring_rd); }

// Приём сэмпла: НЕБЛОКИРУЮЩИЙ. Громкость здесь.
void i2s_push_sample(int16_t left, int16_t right) {
    if (!g_i2s_ready) return;
    if (g_muted) { g_muted = 0; PG_DAT |= (1u << 7); }
    if (ring_count() >= AUDIO_RING_SIZE) return;

    int32_t v = (g_volume_pct * 32) / 100;
    int32_t L = (int32_t)left * v / 32;
    int32_t R = (int32_t)right * v / 32;
    if (L > 32767) L = 32767;
    if (L < -32768) L = -32768;
    if (R > 32767) R = 32767;
    if (R < -32768) R = -32768;

    uint32_t w = g_ring_wr & (AUDIO_RING_SIZE - 1);
    g_ring_l[w] = (int16_t)L;
    g_ring_r[w] = (int16_t)R;
    g_ring_wr++;
}

// Ритм: 21 мкс на пару ≈ 20.8 мкс = 48000 Гц
// (таймер 24 МГц, lo_us() даёт настоящие микросекунды: 48кГц → пара за 20.8 мкс).
#define I2S_PACE_UNITS 21

// Перенос кольцо → FIFO. Малый лимит пар, чтобы вызов был коротким.
// Если кольцо пусто — доливаем тишину (FIFO не уходит в ноль).
void i2s_flush_max(int max_pairs) {
    if (!g_i2s_ready) return;
    int n = 0;
    while (n < max_pairs) {
        if (g_i2s_next) { while ((int32_t)(h3_hs_timer_lo_us() - g_i2s_next) < 0); }
        g_i2s_next = h3_hs_timer_lo_us() + I2S_PACE_UNITS;

        if (ring_count() > 0) {
            uint32_t r = g_ring_rd & (AUDIO_RING_SIZE - 1);
            I2S_FIFO_TX = (uint32_t)(uint16_t)g_ring_l[r] << 16;
            I2S_FIFO_TX = (uint32_t)(uint16_t)g_ring_r[r] << 16;
            g_ring_rd++;
        } else {
            // тишина, чтобы FIFO не проседал в ноль
            I2S_FIFO_TX = 0;
            I2S_FIFO_TX = 0;
        }
        n++;
    }
}

void i2s_flush(void) { i2s_flush_max(64); }

void i2s_test_tone(int freq, int msec) {
    if (!g_i2s_ready) return;
    if (freq < 20) freq = 20;
    if (msec <= 0) msec = 100;
    printf("BEEP %d Hz %d ms\n", freq, msec);

    uint32_t step = (uint32_t)(((uint64_t)freq << 16) / 48000u);
    uint32_t ph = 0;
    int total = 48000 * msec / 1000;
    if (g_muted) { g_muted = 0; PG_DAT |= (1u << 7); }

    static const int16_t sin_tab[256] = {
        0,804,1607,2410,3211,4011,4807,5601,6392,7179,7961,8739,9511,10278,11038,11792,
        12539,13278,14009,14732,15446,16150,16845,17530,18204,18867,19519,20159,20787,21402,22004,22594,
        23169,23731,24278,24811,25329,25831,26318,26789,27244,27683,28105,28510,28898,29268,29621,29955,
        30272,30571,30851,31113,31356,31580,31785,31970,32137,32284,32412,32520,32609,32678,32728,32757,
        32767,32757,32728,32678,32609,32520,32412,32284,32137,31970,31785,31580,31356,31113,30851,30571,
        30272,29955,29621,29268,28898,28510,28105,27683,27244,26789,26318,25831,25329,24811,24278,23731,
        23169,22594,22004,21402,20787,20159,19519,18867,18204,17530,16845,16150,15446,14732,14009,13278,
        12539,11792,11038,10278,9511,8739,7961,7179,6392,5601,4807,4011,3211,2410,1607,804,0,
        -804,-1607,-2410,-3211,-4011,-4807,-5601,-6392,-7179,-7961,-8739,-9511,-10278,-11038,-11792,-12539,
        -13278,-14009,-14732,-15446,-16150,-16845,-17530,-18204,-18867,-19519,-20159,-20787,-21402,-22004,-22594,-23169,
        -23731,-24278,-24811,-25329,-25831,-26318,-26789,-27244,-27683,-28105,-28510,-28898,-29268,-29621,-29955,-30272,
        -30571,-30851,-31113,-31356,-31580,-31785,-31970,-32137,-32284,-32412,-32520,-32609,-32678,-32728,-32757,-32767,
        -32757,-32728,-32678,-32609,-32520,-32412,-32284,-32137,-31970,-31785,-31580,-31356,-31113,-30851,-30571,-30272,
        -29955,-29621,-29268,-28898,-28510,-28105,-27683,-27244,-26789,-26318,-25831,-25329,-24811,-24278,-23731,-23169,
        -22594,-22004,-21402,-20787,-20159,-19519,-18867,-18204,-17530,-16845,-16150,-15446,-14732,-14009,-13278,-12539,
        -11792,-11038,-10278,-9511,-8739,-7961,-7179,-6392,-5601,-4807,-4011,-3211,-2410,-1607,-804
    };
    // Генерируем ровно msec миллисекунд звука в РЕАЛЬНОМ темпе 48 кГц:
    // каждый сэмпл сразу уходит через i2s_flush_max(1) (~20.8 мкс на пару),
    // иначе кольцо (8192) переполняется за мгновение и получается «щелчок».
    for (int d = 0; d < total; d++) {
        uint32_t idx = (ph >> 8) & 0xFF; ph += step;
        int32_t s = (int32_t)sin_tab[idx] * 9 / 10;
        i2s_push_sample((int16_t)s, (int16_t)s);
        i2s_flush_max(1);
    }
    i2s_flush();
    g_muted = 1; PG_DAT &= ~(1u << 7);
    printf("BEEP done\n");
}