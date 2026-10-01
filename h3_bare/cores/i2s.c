// i2s.c — I2S0 H3 (0x01C22000) для MAX98357A, 48 кГц стерео 16-бит.
//
// Схема (обработка потока эмулятора, как у консолей):
//   - i2s_push_sample(): эмулятор кладёт сэмплы в РАМ-кольцо МГНОВЕННО,
//     без ожидания (не тормозит кадр). Громкость — здесь.
//   - i2s_flush(): переносит кольцо → аппаратный TX-FIFO с реальным темпом
//     48 кГц. Если кольцо пусто — пишет тишину (0), чтобы FIFO не проседал
//     до нуля (нет шума).
//   r503: i2s_init() вызывается из main() при старте (железо готово к
//   тестам). Звук эмуляторов пока НЕ подключён — emu_throttle() не зовёт
//   i2s_flush(), включается пошагово.
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

// PA10 — SD усилителя MAX98357A (HIGH = работа, микширует L+R).
#define SD_PIN 10

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

// DC-blocker (r508/r518/r519): эмуляторы/blip дают постоянную составляющую и
// её перепады между кадрами («фон»). HPF: K=8 (~30 Гц — фон слышен), K=6
// (~120 Гц — лучше), r519: K=5 (~240 Гц — фон почти ушёл; выше режем музыку).
static int32_t dc_l = 0;
static int32_t dc_r = 0;
#define DC_SHIFT 5
// Лёгкий сглаживающий FIR (r520): (x[n]+2*x[n-1]+x[n-2])/4 — убирает резкие
// ступеньки blip (треск), срез ~0.38*fs (48к → ~18 кГц), музыку не режет.
static int32_t sm_l1 = 0, sm_l2 = 0, sm_r1 = 0, sm_r2 = 0;

// ---- кольцо потока эмулятора (объявлено раньше геттеров — их использует) ----
#define AUDIO_RING_SIZE 8192
static int16_t g_ring_l[AUDIO_RING_SIZE];
static int16_t g_ring_r[AUDIO_RING_SIZE];
static volatile uint32_t g_ring_wr = 0;
static volatile uint32_t g_ring_rd = 0;   // r124: volatile — читается в нескольких местах

void i2s_volume(int p) {
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    g_volume_pct = p;
    printf("audio: volume=%d%%\n", p);
}
int i2s_volume_pct(void) { return g_volume_pct; }
int i2s_ready(void) { return g_i2s_ready; }
void i2s_ring_reset(void) { g_ring_wr = 0; g_ring_rd = 0; dc_l = 0; dc_r = 0; sm_l1 = 0; sm_l2 = 0; sm_r1 = 0; sm_r2 = 0; }
void i2s_mute(int m) { g_muted = m; if (m) H3_PIO_PORTA->DAT &= ~(1u<<SD_PIN); else H3_PIO_PORTA->DAT |= (1u<<SD_PIN); }

int i2s_init(void) {
    pll_audio_enable();

    H3_CCU->BUS_CLK_GATING2 |= (1u << 12);
    CCU_RST_I2S0 |= (1u << 12);

    volatile uint32_t* clk = &H3_CCU->I2SPCM0_CLK;
    *clk = (*clk & ~((3u << 16) | (1u << 31))) | (3u << 16) | (1u << 31);
    udelay(2000);

    // r0.413: пины по СХЕМЕ пользователя (HARDWARE.md «Звук»):
    //   PA18 = PCM0_SYNC (LRC), PA19 = PCM0_CLK (BCLK),
    //   PA20 = PCM0_DOUT (DIN),   PA10 = SD (мьют).
    // ВАЖНО: не путать с предыдущим вариантом PA10-12 — те пины заняты
    // Sega-падом (TWI0) PA11/12; PA10 при этом свободен.
    // r503: PA16..PA23 живут в CFG2 (не CFG3!) — до r503 настройка шла в
    // CFG3 (регистр PA24..31), баг не проявлялся: i2s_init() не вызывался.
    uint32_t c2 = H3_PIO_PORTA->CFG2;
    c2 &= ~((0xFu << 8) | (0xFu << 12) | (0xFu << 16));
    c2 |=  (2u << 8) | (2u << 12) | (2u << 16);   // функция 2 = PCM0
    H3_PIO_PORTA->CFG2 = c2;

    // PA10 — SD усилителя, output=1 (HIGH = работа). CFG1 = PA8..PA15.
    H3_PIO_PORTA->CFG1 = (H3_PIO_PORTA->CFG1 & ~(0xFu << ((SD_PIN - 8) * 4)))
                         | (1u << ((SD_PIN - 8) * 4));
    H3_PIO_PORTA->DAT |= (1u << SD_PIN);

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

// ---- приём/перенос потока эмулятора ----
static inline uint32_t ring_count(void) { return (uint32_t)(g_ring_wr - g_ring_rd); }
int i2s_ring_level(void) { return (int)ring_count(); }   // r522: для диагностики

// Приём сэмпла: НЕБЛОКИРУЮЩИЙ. Громкость здесь.
void i2s_push_sample(int16_t left, int16_t right) {
    if (!g_i2s_ready) return;
    if (g_muted) { g_muted = 0; H3_PIO_PORTA->DAT |= (1u << SD_PIN); }
    if (ring_count() >= AUDIO_RING_SIZE) return;

    // убираем постоянную составляющую (и подбасовый гул)
    int32_t fl = (int32_t)left  - dc_l;  dc_l += fl >> DC_SHIFT;
    int32_t fr = (int32_t)right - dc_r;  dc_r += fr >> DC_SHIFT;

    // r520: лёгкое сглаживание ступенек
    int32_t f2l = (fl + 2 * sm_l1 + sm_l2) >> 2;  sm_l2 = sm_l1; sm_l1 = fl;
    int32_t f2r = (fr + 2 * sm_r1 + sm_r2) >> 2;  sm_r2 = sm_r1; sm_r1 = fr;

    int32_t v = (g_volume_pct * 32) / 100;
    int32_t L = f2l * v / 32;
    int32_t R = f2r * v / 32;
    if (L > 32767) L = 32767 + (L - 32767) / 4;   // r508: мягкий лимит
    if (L < -32768) L = -32768 + (L + 32768) / 4;
    if (R > 32767) R = 32767 + (R - 32767) / 4;
    if (R < -32768) R = -32768 + (R + 32768) / 4;
    // r546: после сжатия перегруз всё ещё может превышать диапазон s16
    // (напр. 98301 → 32767+16383=49150 → обёртка при касте в int16).
    // Повторный кламп — гарантирует валидный сэмпл.
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

// Синус-таблица 1/4 периода (общая для тест-тона и меню-клика).
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

void i2s_test_tone(int freq, int msec) {
    if (!g_i2s_ready) return;
    if (freq < 20) freq = 20;
    if (msec <= 0) msec = 100;

    uint32_t step = (uint32_t)(((uint64_t)freq << 16) / 48000u);
    uint32_t ph = 0;
    int total = 48000 * msec / 1000;
    // r546: не гасим усилитель, если звук уже шёл (пример: клик/тон поверх
    // игры) — запоминаем прежнее состояние и восстанавливаем в конце.
    int was_muted = g_muted;
    if (was_muted) { g_muted = 0; H3_PIO_PORTA->DAT |= (1u << SD_PIN); }

    // Генерируем ровно msec миллисекунд звука в РЕАЛЬНОМ темпе 48 кГц:
    // каждый сэмпл сразу уходит через i2s_flush_max(1) (~20.8 мкс на пару),
    // иначе кольцо (8192) переполняется за мгновение и получается «щелчок».
    for (int d = 0; d < total; d++) {
        uint32_t idx = (ph >> 8) & 0xFF; ph += step;
        int32_t s = (int32_t)sin_tab[idx] * 5 / 10;   // r504: 50% — тон не оглушает
        i2s_push_sample((int16_t)s, (int16_t)s);
        i2s_flush_max(1);
    }
    i2s_flush();
    if (was_muted) { g_muted = 1; H3_PIO_PORTA->DAT &= ~(1u << SD_PIN); }
}

// Короткий тихий щелчок при навигации в меню (~5 мс, 1.5 кГц, ~25% амплитуды).
// Неблокирующим не делаем: 5 мс на смену пункта незаметно, зато код прост.
// Если I2S не готов — no-op (меню не должно тормозить из-за звука).
void i2s_click(void) {
    if (!g_i2s_ready) return;
    uint32_t step = (uint32_t)(((uint64_t)1500u << 16) / 48000u);
    uint32_t ph = 0;
    int total = 48000 * 5 / 1000;
    // r546: как в i2s_test_tone — восстанавливаем прежнее состояние мьюта.
    int was_muted = g_muted;
    if (was_muted) { g_muted = 0; H3_PIO_PORTA->DAT |= (1u << SD_PIN); }
    for (int d = 0; d < total; d++) {
        uint32_t idx = (ph >> 8) & 0xFF; ph += step;
        int32_t s = (int32_t)sin_tab[idx] >> 2;   // ~25%
        i2s_push_sample((int16_t)s, (int16_t)s);
        i2s_flush_max(1);
    }
    i2s_flush();
    if (was_muted) { g_muted = 1; H3_PIO_PORTA->DAT &= ~(1u << SD_PIN); }
}