// i2s.c — I2S-контроллер H3 (sun8i-i2s, 0x01C22000) для MAX98357A.
// Master, 16-bit stereo, 44100 Гц. Регистры по sun4i-i2s.c (ядро Linux).
//
// MAX98357A подключается:
//   PA19 (PCM0_CLK) → BCLK
//   PA18 (PCM0_SYNC) → LRC
//   PA20 (PCM0_DOUT) → DIN
//   PG7 → SD (output=1, HIGH = работает, LOW = мьют)
//   GAIN — float (9 дБ)
//
// ВАЖНО: CCU soft-reset I2S0 (0x2D0 bit12) ОБЯЗАТЕЛЬНО снять — иначе
// модуль не тактируется (тишина). U-Boot его не трогает (I2S не использует).
//
// ПРАВКИ v2 (фикс полной тишины и треска):
//   1. SDO_EN(0) = бит 8 CTRL — включение DOUT (без него MAX98357A молчит)
//   2. PLL_AUDIO N=14, M=14 (raw — не -1) + SDM pattern для 24.576 МГц
//   3. i2s_push_sample: поллинг TX_FIFO_FULL вместо кривого таймера
//   4. TX_CHAN_EN: 0x30 (2 канала) вместо 0xFF0 (255)
//   5. BCLK=8, MCLK_EN: mod_clk 24.576M/8=3.072M, LRCK_PERIOD=64 → 48k
//      (если PLL залочится на 24M — BCLK=24M/8=3M, ~46.875k — приемлемо)
#include <stdint.h>
#include "h3.h"
#include "h3_ccu.h"
#include "h3_hs_timer.h"

extern int printf(const char* fmt, ...);

// ---- I2S0 @ 0x01C22000 (sun8i H3) ----
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

// CTRL биты (SUN8I)
#define I2S_CTRL_GL_EN     (1u << 0)
#define I2S_CTRL_TX_EN     (1u << 2)
#define I2S_CTRL_MODE_MASK (3u << 4)
#define I2S_CTRL_MODE_I2S  (0u << 4)
#define I2S_CTRL_MODE_LJ   (1u << 4)
#define I2S_CTRL_SDO_EN0   (1u << 8)
#define I2S_CTRL_LRCK_OUT  (1u << 17)
#define I2S_CTRL_BCLK_OUT  (1u << 18)

// FMT0 (SUN8I)
#define I2S_FMT0_WSS_MASK        (7u << 0)
#define I2S_FMT0_SR_MASK         (7u << 4)
#define I2S_FMT0_BCLPOL          (1u << 7)
#define I2S_FMT0_LRCKPER_MASK    (0x3FFu << 8)
#define I2S_FMT0_LRCKPER(v)      (((v)-1) << 8)
#define I2S_FMT0_LRCLKPOL        (1u << 19)

// CLK_DIV
#define I2S_CLK_MCLK_EN  (1u << 8)
#define I2S_CLK_BCLK_MASK (7u << 4)
#define I2S_CLK_BCLK(v)  ((v) << 4)
#define I2S_CLK_MCLK_MASK (0xFu << 0)
#define I2S_CLK_MCLK(v)  ((v) << 0)

// FIFO статус (sun8i): TX_CNT биты 13:8 (число слов в TX FIFO, глубина 64)
#define I2S_FIFO_STA_TXCNT_MASK (0x3Fu << 8)

// TX_CSEL
#define I2S_TX_CHAN_EN       (0xFFu << 4)
#define I2S_TX_CHAN_OFF(v)   ((v) << 12)

// CHAN CFG
#define I2S_CHAN_TXSLOT_MASK (0xFu << 0)
#define I2S_CHAN_TXSLOT(v)   ((v)-1)
#define I2S_CHAN_RXSLOT_MASK (0xFu << 4)
#define I2S_CHAN_RXSLOT(v)   (((v)-1) << 4)

// ---- PG7 (SD) ----
#define PG_BASE    (H3_PIO_BASE + 0xD8u)
#define PG_CFG0    (*(volatile uint32_t*)(PG_BASE + 0x00u))
#define PG_DAT     (*(volatile uint32_t*)(PG_BASE + 0x10u))

// ---- CCU ----
#define CCU_RST_I2S0 (*(volatile uint32_t*)(H3_CCU_BASE + 0x2D0u))

// PLL_AUDIO (0x008): N[14:8] width7, M[4:0] width5, SDM_EN[24], EN[31], LOCK[28]
#define PLL_AUDIO_CTRL (*(volatile uint32_t*)(H3_CCU_BASE + 0x008u))
#define PLL_AUDIO_PAT  (*(volatile uint32_t*)(H3_CCU_BASE + 0x284u))

static int pll_audio_lock_wait(void) {
    uint32_t t = 0;
    while (!(PLL_AUDIO_CTRL & (1u << 28)) && t++ < 1000000)
        ;
    return (PLL_AUDIO_CTRL & (1u << 28)) ? 0 : -1;
}

static void pll_audio_enable(void) {
    // Уже включён и залочен — не трогаем
    if ((PLL_AUDIO_CTRL & (1u << 31)) && (PLL_AUDIO_CTRL & (1u << 28))) {
        printf("I2S: PLL_AUDIO already on (0x%08X)\n", PLL_AUDIO_CTRL);
        return;
    }
    // 24.576 МГц: SDM pattern, N=14, M=14. Регистр хранит (n-1),(m-1):
    // по ccu-sun8i-h3.c table {rate=24576000, pattern=0xc000ac02, n=14, m=14}
    // и ccu_nm.c записывает (n-1)<<8 | (m-1). (13<<8|13 = N14/M14)
    PLL_AUDIO_PAT = 0xc000ac02u;
    PLL_AUDIO_CTRL = (1u << 31) | (1u << 24) | ((14 - 1) << 8) | (14 - 1);
    udelay(3000);
    if (pll_audio_lock_wait() == 0)
        printf("I2S: PLL_AUDIO 24.576 MHz locked\n");
    else
        printf("I2S: PLL_AUDIO LOCK TIMEOUT (0x%08X)\n", PLL_AUDIO_CTRL);
}

static int g_i2s_ready = 0;
static int g_volume_pct = 20;   // громкость 0..100 (точная, без ошибок округления)
static int g_muted = 1;
static int g_sample_rate = 48000;

// ---- кольцевой буфер (неблокирующий push) ----
// Эмуляторы генерируют звук пачками внутри run_frame; блокировать их
// поллингом I2S FIFO нельзя — тормозит эмуляцию (SNES/NES лагают).
// push пишет в RAM-кольцо мгновенно, i2s_flush() выталкивает в FIFO
// раз в кадр из emu_throttle.
#define AUDIO_RING_SIZE 8192          // пар стерео (≈ 0.17 с)
static int16_t g_ring_l[AUDIO_RING_SIZE];
static int16_t g_ring_r[AUDIO_RING_SIZE];
static volatile uint32_t g_ring_wr = 0;   // индекс записи
static uint32_t g_ring_rd = 0;            // индекс чтения (только flush)

static inline uint32_t ring_count(void) {
    return (uint32_t)(g_ring_wr - g_ring_rd);
}

void i2s_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    g_volume_pct = percent;
    printf("audio: volume=%d%%\n", percent);
}

int i2s_volume_pct(void) {
    return g_volume_pct;
}

void i2s_mute(int mute) {
    g_muted = mute;
    if (mute) {
        PG_DAT &= ~(1u << 7);
    } else {
        PG_DAT |= (1u << 7);
    }
}

int i2s_init(void) {
    // 1. PLL_AUDIO — такт для I2S0
    pll_audio_enable();

    // 2. BUS gate + снять soft-reset
    H3_CCU->BUS_CLK_GATING2 |= (1u << 12);
    CCU_RST_I2S0 |= (1u << 12);

    // 3. Модульный клок I2S0: mux=3 (pll-audio), gate=1
    volatile uint32_t* clkreg = &H3_CCU->I2SPCM0_CLK;
    *clkreg = (*clkreg & ~((3u << 16) | (1u << 31))) | (3u << 16) | (1u << 31);
    udelay(2000);

    // 4. PIO: PA18/19/20 → функция 2 (I2S0)
    uint32_t cfg2 = H3_PIO_PORTA->CFG2;
    cfg2 &= ~((0xFu <<  8) | (0xFu << 12) | (0xFu << 16) | (0xFu << 20));
    cfg2 |=  (2u <<  8);   // PA18 = PCM0_SYNC (LRCK)
    cfg2 |=  (2u << 12);   // PA19 = PCM0_CLK  (BCLK)
    cfg2 |=  (2u << 16);   // PA20 = PCM0_DOUT (DIN)
    H3_PIO_PORTA->CFG2 = cfg2;

    // 5. PG7 → output, LOW (SD=0 — усилитель выкл до первого звука)
    PG_CFG0 = (PG_CFG0 & ~(0xFu << 28)) | (1u << 28);
    PG_DAT &= ~(1u << 7);

    // 6. Сброс I2S + flush FIFO
    I2S_CTRL = 0;
    I2S_FIFO_CTL |= (1u << 24) | (1u << 25);  // flush RX+TX
    udelay(100);
    // Drain RX мусора
    for (int di = 0; di < 256; di++) { volatile uint32_t dummy = I2S_RX_FIFO; (void)dummy; }
    // ВНИМАНИЕ: НЕ трогаем TX_MODE/RX_MODE (биты 2:0 FIFO_CTL) — оставляем
    // дефолт из регистра (0). Установка 0x5 (16-бит значимых) ломала вывод:
    // после неё тест-тон замолчал. Рабочая конфигурация — дефолт.

    // 7. Конфигурация: I2S master, 48 кГц, 16-бит стерео
    // sun8i: WSS=SR=16bit → 3, LRCK_PERIOD = slot_width (32 для стерео 16-бит)
    // FMT0: WSS[2:0]=7 (32-бит слот), SR[6:4]=3 (16-бит), LRCLK START_LOW (I2S)
    I2S_FMT0 = (7u << 0) | (3u << 4) | (0u << 7)
             | I2S_FMT0_LRCKPER(32) | (0u << 19);
    I2S_FMT1 = 0;

    // CLK_DIV: bclk=5(div8), mclk=2, MCLK_EN
    // mod_clk 24.576M / 8 = 3.072M BCLK → 48k при LRCK_PERIOD=32 (2×32=64 BCLK/фрейм)
    I2S_CLK_DIV = I2S_CLK_MCLK_EN | I2S_CLK_BCLK(5) | I2S_CLK_MCLK(2);
    g_sample_rate = 48000;

    // Каналы: стерео, CHAN_EN=0x30 (2 канала), offset=1 (I2S delay)
    I2S_TX_CMAP = 0x76543210;
    I2S_TX_CSEL = (0x3u << 4)            // CHAN_EN(2)
                | I2S_TX_CHAN_OFF(1)     // offset=1
                | 1;                      // CHAN_SEL(num_chan)=1 → 2 канала
    I2S_CHAN_CFG = I2S_CHAN_TXSLOT(2) | I2S_CHAN_RXSLOT(2);

    // 8. Включение: BCLK_OUT|LRCK_OUT, MODE=LEFT_J, TX_EN, SDO_EN(0), GL_EN
    I2S_CTRL = I2S_CTRL_BCLK_OUT | I2S_CTRL_LRCK_OUT
             | (1u << 4)                    // MODE=LEFT_J
             | I2S_CTRL_TX_EN
             | I2S_CTRL_SDO_EN0            // обязательно: включает DOUT!
             | I2S_CTRL_GL_EN;
    udelay(1000);

    g_i2s_ready = 1;

    // Диагностика
    printf("I2S: === diagnostic ===\n");
    printf("I2S: PLL_AUDIO_CTRL=0x%08X PAT=0x%08X\n",
           (unsigned)PLL_AUDIO_CTRL, (unsigned)PLL_AUDIO_PAT);
    printf("I2S: I2SPCM0_CLK=0x%08X\n", (unsigned)H3_CCU->I2SPCM0_CLK);
    printf("I2S: CTRL=0x%08X FMT0=0x%08X\n",
           (unsigned)I2S_CTRL, (unsigned)I2S_FMT0);
    printf("I2S: CLKDIV=0x%08X FIFOSTA=0x%08X\n",
           (unsigned)I2S_CLK_DIV, (unsigned)I2S_FIFO_STA);
    printf("I2S: TXSEL=0x%08X TXMAP=0x%08X\n",
           (unsigned)I2S_TX_CSEL, (unsigned)I2S_TX_CMAP);
    printf("I2S: PG_CFG0=0x%08X PG_DAT=0x%08X\n",
           (unsigned)PG_CFG0, (unsigned)PG_DAT);
    printf("I2S: ready (%d Hz, volume=%d%%)\n",
           g_sample_rate, g_volume_pct);
    return 0;
}

// Запись одного стерео-сэмпла (L/R 16-bit signed) в кольцевой буфер.
// НЕБЛОКИРУЮЩАЯ: если буфер полон — сэмпл отбрасывается (защита от
// переполнения; реально flush опорожняет его 60 Гц, места хватает).
void i2s_push_sample(int16_t left, int16_t right) {
    if (!g_i2s_ready) return;

    if (g_muted) {
        g_muted = 0;
        PG_DAT |= (1u << 7);
    }

    if (ring_count() >= AUDIO_RING_SIZE) return;   // переполнение — пропуск

    uint32_t w = g_ring_wr & (AUDIO_RING_SIZE - 1);
    g_ring_l[w] = left;
    g_ring_r[w] = right;
    g_ring_wr++;
}

// Вытолкнуть накопленное из кольца в I2S FIFO. НЕБЛОКИРУЮЩАЯ:
// пишет, пока в аппаратном FIFO есть место (TX_CNT < 60), затем выходит.
// Вызывать раз в кадр (emu_throttle).
void i2s_flush(void) {
    if (!g_i2s_ready) return;

    while (ring_count() > 0) {
        // ждём место в аппаратном TX FIFO (глубина 64 слова = 32 стерео-пары)
        if ((I2S_FIFO_STA & I2S_FIFO_STA_TXCNT_MASK) >= (60u << 8))
            return;   // FIFO занят — продолжим в следующем кадре

        uint32_t r = g_ring_rd & (AUDIO_RING_SIZE - 1);
        // Громкость 0..100 → целочисленный множитель (10 бит, без округл. прыжков)
        int32_t vol = (g_volume_pct * 32) / 100;   // 0..32 (×32 = старое 0..1023/32)
        int32_t l32 = (int32_t)g_ring_l[r] * vol / 32;
        int32_t r32 = (int32_t)g_ring_r[r] * vol / 32;
        if (l32 > 32767) l32 = 32767; if (l32 < -32768) l32 = -32768;
        if (r32 > 32767) r32 = 32767; if (r32 < -32768) r32 = -32768;
        uint32_t l = (uint32_t)(uint16_t)l32 << 16;
        uint32_t rt = (uint32_t)(uint16_t)r32 << 16;
        I2S_FIFO_TX = l;
        I2S_FIFO_TX = rt;
        g_ring_rd++;
    }
}

// Тест звука: синусоида на частоте freq (Гц), msec (мс), стерео.
// Пишем прямо в аппаратный FIFO с блокировкой (поллинг места) — как в
// первой рабочей версии. Кольцо тут не годится: тон генерится быстро,
// кольцо переполняется и реально звучит только начало. Здесь важно
// выдерживать реальное время — I2S сам диктует темп.
void i2s_test_tone(int freq, int msec) {
    if (!g_i2s_ready) return;
    if (freq < 20) freq = 20;
    if (msec <= 0) msec = 100;

    uint32_t step = (uint32_t)(((uint64_t)freq << 16) / 48000u);
    uint32_t phase = 0;
    int total = 48000 * msec / 1000;

    // Короткий служебный тон: СД=1 (включить усилитель)
    if (g_muted) {
        g_muted = 0;
        PG_DAT |= (1u << 7);
    }

    for (int done = 0; done < total; done++) {
        uint32_t idx = (phase >> 8) & 0xFFu;
        phase += step;

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
        int32_t s = (int32_t)sin_tab[idx] * 9 / 10;
        int32_t vol = (g_volume_pct * 32) / 100;
        int32_t sv = (int32_t)(s >> 8) * vol / 32;
        if (sv > 32767) sv = 32767;
        if (sv < -32768) sv = -32768;
        uint32_t w = (uint32_t)(uint16_t)sv << 16;

        // ждём место в аппаратном TX FIFO — блокирующе (тест живёт в своём цикле)
        while ((I2S_FIFO_STA & I2S_FIFO_STA_TXCNT_MASK) >= (60u << 8))
            ;
        I2S_FIFO_TX = w;
        while ((I2S_FIFO_STA & I2S_FIFO_STA_TXCNT_MASK) >= (60u << 8))
            ;
        I2S_FIFO_TX = w;
    }
    g_muted = 1;
    PG_DAT &= ~(1u << 7);
    printf("I2S: test %d Hz finished, SD=0\n", freq);
}