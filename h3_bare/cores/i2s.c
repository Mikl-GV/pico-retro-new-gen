// i2s.c — I2S-контроллер H3 (sun8i-i2s, 0x01C22000) для MAX98357A.
// Master, 16-bit stereo, 48000 Гц. Регистры по sun4i-i2s.c (ядро Linux).
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
// MAX98357A подключается:
//   PA19 (PCM0_CLK) → BCLK
//   PA18 (PCM0_SYNC) → LRC
//   PA20 (PCM0_DOUT) → DIN
//   PG7 → SD (output=1, HIGH = работает, LOW = мьют)
//   GAIN — float (9 дБ)
#include <stdint.h>
#include "h3.h"
#include "h3_ccu.h"
#include "h3_hs_timer.h"

extern int printf(const char* fmt, ...);

// ---- I2S0 @ 0x01C22000 (sun8i H3) ----
// Регистры: CTRL=0x00, FMT0=0x04, FMT1=0x08, FIFO_TX=0x20,
// FIFO_CTL=0x14, FIFO_STA=0x18, CLK_DIV=0x24,
// TX_CNT=0x28 (пишется 0 при старте), RX_CNT=0x2C
// TX_CHAN_SEL=0x34, TX_CHAN_MAP=0x44, CHAN_CFG=0x30 (sun8i)
#define I2S_BASE 0x01C22000u

#define I2S_CTRL     (*(volatile uint32_t*)(I2S_BASE + 0x00))
#define I2S_FMT0     (*(volatile uint32_t*)(I2S_BASE + 0x04))
#define I2S_FMT1     (*(volatile uint32_t*)(I2S_BASE + 0x08))
#define I2S_RX_FIFO  (*(volatile uint32_t*)(I2S_BASE + 0x10))  // читать для drain
#define I2S_FIFO_CTL (*(volatile uint32_t*)(I2S_BASE + 0x14))
#define I2S_FIFO_STA (*(volatile uint32_t*)(I2S_BASE + 0x18))
#define I2S_FIFO_TX  (*(volatile uint32_t*)(I2S_BASE + 0x20))
#define I2S_CLK_DIV  (*(volatile uint32_t*)(I2S_BASE + 0x24))
#define I2S_TX_CNT   (*(volatile uint32_t*)(I2S_BASE + 0x28))  // сброс: запись 0
#define I2S_TX_CSEL  (*(volatile uint32_t*)(I2S_BASE + 0x34))  // SUN8I
#define I2S_TX_CMAP  (*(volatile uint32_t*)(I2S_BASE + 0x44))  // SUN8I
#define I2S_CHAN_CFG (*(volatile uint32_t*)(I2S_BASE + 0x30))  // SUN8I

// Статус FIFO_STA: TX статус в битах 15:8, RX статус в 7:0.
// Значение > 0 = есть данные в FIFO (но для sun8i в документации H3
// точных битов нет; используем любые ненулевые биты как «FIFO не пуст»).

// CTRL биты (SUN8I)
#define I2S_CTRL_GL_EN     (1u << 0)
#define I2S_CTRL_TX_EN     (1u << 2)
#define I2S_CTRL_MODE_MASK (3u << 4)
#define I2S_CTRL_MODE_I2S  (0u << 4)
#define I2S_CTRL_MODE_LJ   (1u << 4)
#define I2S_CTRL_LRCK_OUT  (1u << 17)
#define I2S_CTRL_BCLK_OUT  (1u << 18)

// FMT0 (SUN8I): WSS биты 2:0, SR биты 6:4
#define I2S_FMT0_WSS_MASK  (7u << 0)
#define I2S_FMT0_SR_MASK   (7u << 4)
#define I2S_FMT0_BCLPOL    (1u << 7)
#define I2S_FMT0_BCLPOL_NORM (0u << 7)
#define I2S_FMT0_LRCKPER_MASK (0x3FFu << 8)
#define I2S_FMT0_LRCKPER(v)  (((v)-1) << 8)
#define I2S_FMT0_LRCKPOL     (1u << 19)

// CLK_DIV: MCLK_EN для H3 = бит 8
#define I2S_CLK_MCLK_EN  (1u << 8)
#define I2S_CLK_BCLK_MASK (7u << 4)
#define I2S_CLK_BCLK(v)  ((v) << 4)
#define I2S_CLK_MCLK_MASK (0xFu << 0)
#define I2S_CLK_MCLK(v)  ((v) << 0)

// FIFO статус: биты TX_CNT (сколько слов можно записать)
#define I2S_FIFO_STA_TXCNT_MASK (0x3Fu << 0)

// TX_CSEL (SUN8I): CHAN_EN = биты 11:4
#define I2S_TX_CHAN_EN       (0xFFu << 4)  // все каналы
#define I2S_TX_CHAN_OFF(v)   ((v) << 12)   // offset для I2S = 1

// CHAN CFG
#define I2S_CHAN_TXSLOT_MASK (0xFu << 0)
#define I2S_CHAN_TXSLOT(v)   ((v)-1)
#define I2S_CHAN_RXSLOT_MASK (0xFu << 4)
#define I2S_CHAN_RXSLOT(v)   (((v)-1) << 4)

// ---- PG7 (SD) ----
// Порт G: PORTA offset 0, PORTG = 6*0x24 = 0xD8
// Смещения внутри порта: CFG0=0x00, CFG1=0x04, DAT=0x10
// PG0..7 лежат в CFG0, PG7 биты 28..31 (7*4=28)
#define PG_BASE    (H3_PIO_BASE + 0xD8u)
#define PG_CFG0    (*(volatile uint32_t*)(PG_BASE + 0x00u))
#define PG_DAT     (*(volatile uint32_t*)(PG_BASE + 0x10u))

// ---- CCU: soft-reset I2S0 (RST_BUS_I2S0 = 0x2D0 bit12) ----
// В H3 (sun8i-h3) по ядру Linux: drivers/clk/sunxi-ng/ccu-sun8i-h3.c
//   [RST_BUS_I2S0] = { 0x2d0, BIT(12) }
// Без снятия soft-reset модуль не тактируется — полная тишина.
#define CCU_RST_I2S0 (*(volatile uint32_t*)(H3_CCU_BASE + 0x2D0u))

// ---- PLL_AUDIO (0x008) — обязателен для I2S0 ----
// U-Boot на H3 его НЕ включает (I2S ему не нужен) → модуль I2S0 без такта,
// FIFO_STA всегда 0 → тишина. Включаем вручную как в sunxi-ng (ccu-sun8i-h3.c):
//   base = 24М * N / M c SDM. Для 48 кГц-семейства берём 24.576 МГц:
//     pattern (0x284) = 0xc000ac02, N=14 (reg=13), M=14 (reg=13),
//     SDM_EN = BIT(24), gate/EN = BIT(31), lock = BIT(28).
#define PLL_AUDIO_CTRL (*(volatile uint32_t*)(H3_CCU_BASE + 0x008u))
#define PLL_AUDIO_PAT  (*(volatile uint32_t*)(H3_CCU_BASE + 0x284u))

static int pll_audio_lock_wait(void) {
    uint32_t t = 0;
    while (!(PLL_AUDIO_CTRL & (1u << 28)) && t++ < 1000000)
        ;
    return (PLL_AUDIO_CTRL & (1u << 28)) ? 0 : -1;
}

static void pll_audio_enable(void) {
    // Если уже включен с нужной частотой — не трогаем
    if ((PLL_AUDIO_CTRL & (1u << 31)) && (PLL_AUDIO_CTRL & (1u << 28))) {
        printf("I2S: PLL_AUDIO already on (0x%08X)\n", PLL_AUDIO_CTRL);
        return;
    }
    // 24.576 МГц: SDM pattern, N=14 (reg 13), M=14 (reg 13), SDM_EN=bit24
    PLL_AUDIO_PAT = 0xc000ac02u;
    PLL_AUDIO_CTRL = (1u << 31) | (1u << 24) | ((14 - 1) << 8) | (14 - 1);
    udelay(3000);
    if (pll_audio_lock_wait() == 0)
        printf("I2S: PLL_AUDIO 24.576 MHz locked\n");
    else
        printf("I2S: PLL_AUDIO LOCK TIMEOUT (0x%08X)\n", PLL_AUDIO_CTRL);
}

static int g_i2s_ready = 0;
static uint16_t g_volume = 204;  // 20% от 1023 (10-bit, макс 1023)
static int g_muted = 1;          // старт: усилитель ВЫКЛЮЧЕН (SD=0) — нет треска
static int g_sample_rate = 48000;

void i2s_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    g_volume = (uint16_t)(percent * 1023u / 100u);
    printf("audio: volume=%d%% (%d/1023)\n", percent, (int)g_volume);
}

int i2s_volume_pct(void) {
    return (int)(g_volume * 100u / 1023u);
}

void i2s_mute(int mute) {
    g_muted = mute;
    if (mute) {
        PG_DAT &= ~(1u << 7);   // SD = 0 (мьют)
    } else {
        PG_DAT |= (1u << 7);    // SD = 1 (звук)
    }
}

int i2s_init(void) {
    // 1. PLL_AUDIO — такт для I2S0. U-Boot не включает (I2S ему не нужен).
    pll_audio_enable();

    // 2. BUS gate + снять soft-reset
    H3_CCU->BUS_CLK_GATING2 |= (1u << 12);   // gate I2S0 (0x068 bit12)
    CCU_RST_I2S0 |= (1u << 12);              // de-assert soft reset (0x2D0 bit12)
    // модульный клок I2S0: I2SPCM0_CLK (0x0B0), mux [17:16]=3 → pll-audio
    // (i2s_parents = {8x=00, 4x=01, 2x=10, pll-audio=11}; pll-audio = 24.576М
    // — base PLL, без множителя). Это даёт mod_clk=24.576М, из которого
    // bclk_div=8 → BCLK 3.072М = 48к*32*2 (правильно для стерео 32-бит).
    volatile uint32_t* clkreg = &H3_CCU->I2SPCM0_CLK;
    *clkreg = (*clkreg & ~((3u << 16) | (1u << 31))) | (3u << 16) | (1u << 31);
    udelay(2000);

    // 2. PIO: PA18/19/20 → функция 2 (PCM0/I2S0) — ВАЖНО: пины PA16..23
    //    лежат в CFG2 (offset 0x08), а не в CFG0/CFG1! Раньше писали
    //    в CFG0(PA0-7)/CFG1(PA8-15) — пины I2S были выходами GPIO по
    //    дефолту и шина молчала. Ядро: SUNXI_FUNCTION(0x2, "i2s0").
    uint32_t cfg2 = H3_PIO_PORTA->CFG2;
    cfg2 &= ~((0xFu <<  8) | (0xFu << 12) | (0xFu << 16) | (0xFu << 20)); // PA18-21
    cfg2 |=  (2u <<  8);   // PA18 = PCM0_SYNC (LRCK)
    cfg2 |=  (2u << 12);   // PA19 = PCM0_CLK  (BCLK)
    cfg2 |=  (2u << 16);   // PA20 = PCM0_DOUT (DIN => MAX98357A DIN)
    // PA21 (PCM0_DIN) НЕ трогаем — это вход (микрофон), не нужен.
    H3_PIO_PORTA->CFG2 = cfg2;

    // 3. PG7 → output, HIGH (SD=1). PG7 биты 28..31 в CFG0.
    PG_CFG0 = (PG_CFG0 & ~(0xFu << 28)) | (1u << 28);  // output
    PG_DAT &= ~(1u << 7);                                // LOW = SD выключен (без треска)
                                                         // включается при первом сэмпле

    // 4. Сброс I2S-контроллера + очистка RX FIFO от мусора
    // (SPL/U-Boot мог оставить мусор после тестов I2S по JTAG или DMA).
    // Делаем ДО настройки тактов (чтобы чипы-мусор не ушли в DOUT).
    I2S_CTRL = 0;
    I2S_FIFO_CTL |= (1u << 24);  // flush RX
    I2S_FIFO_CTL |= (1u << 25);  // flush TX
    udelay(100);

    // Принудительно читаем RX FIFO пока ненулевые значения (если мусор был)
    uint32_t dummy;
    for (int di = 0; di < 256; di++) { dummy = I2S_RX_FIFO; (void)dummy; }

// 5. Конфигурация I2S master (sun8i H3), 48000 Гц
    // Геометрия кадра: BCLK = 3.072 МГц = 48к × 64 = 2×32-бит слота.
    // По ядру Linux sun8i (quirk sun8i_h3):
    //   WSS bits[2:0]=7 (32-бит слот)
    //   SR  bits[6:4]=3 (16-бит данные, старшие биты)
    //   bit7   = BCLK polarity (0 = normal)
    //   bit19  = LRCLK polarity (0 = start LOW для I2S)
    //   bits[17:8] = LRCK период = 64 BCLK (ровно 48 кГц при BCLK 3.072М)
    I2S_FMT0 = (7u << 0)                  // WSS=7
             | (3u << 4)                  // SR=3
             | (0u << 7)                  // BCLK normal
             | I2S_FMT0_LRCKPER(64)        // LRCK период=64
             | (0u << 19);                 // LRCLK start LOW (I2S)
    I2S_FMT1 = 0;                         // SEXT=0

    // CLK_DIV: bclk val=5 (div=8 → 24.576М/8=3.072М), mclk val=2
    I2S_CLK_DIV = I2S_CLK_MCLK_EN
                | I2S_CLK_BCLK(5)
                | I2S_CLK_MCLK(2);

    // Устанавливаем реальную частоту (для отладки/лога)
    g_sample_rate = 48000;

    // Каналы: стерео, CHAN_EN(2)=3<<4, CHAN_SEL=1 (2 канала), offset=1 (I2S delay)
    I2S_TX_CMAP = 0x76543210;           // маппинг (то же что дефолт)
    I2S_TX_CSEL = (3u << 4)             // CHAN_EN(2): биты 5:4
                | I2S_TX_CHAN_OFF(1)    // offset=1 (I2S задержка 1 BCLK)
                | 1;                     // CHAN_SEL(num_chan)=1 → 2 канала
    I2S_CHAN_CFG = I2S_CHAN_TXSLOT(2)  // 2 слота TX
                 | I2S_CHAN_RXSLOT(2); // 2 слота RX

    // 6. Включение I2S master (H3 sun8i: MODE=LEFT_J для I2S)
    I2S_CTRL = I2S_CTRL_BCLK_OUT | I2S_CTRL_LRCK_OUT
             | (1u << 4)                    // MODE=LEFT_J (I2S for sun8i)
             | I2S_CTRL_TX_EN | I2S_CTRL_GL_EN;
    udelay(1000);

    g_i2s_ready = 1;

    // ---- Диагностика в UART: все ключевые регистры ----
    printf("I2S: === diagnostic ===\n");
    printf("I2S: PLL_AUDIO_CTRL=0x%08X PAT=0x%08X\n",
           (unsigned)(*(volatile uint32_t*)(H3_CCU_BASE + 0x008)),
           (unsigned)(*(volatile uint32_t*)(H3_CCU_BASE + 0x284)));
    printf("I2S: I2SPCM0_CLK=0x%08X\n", (unsigned)H3_CCU->I2SPCM0_CLK);
    printf("I2S: GATING2=0x%08X RST=0x%08X\n",
           (unsigned)H3_CCU->BUS_CLK_GATING2,
           (unsigned)(*(volatile uint32_t*)(H3_CCU_BASE + 0x2D0)));
    printf("I2S: CTRL=0x%08X FMT0=0x%08X FMT1=0x%08X\n",
           (unsigned)I2S_CTRL, (unsigned)I2S_FMT0, (unsigned)I2S_FMT1);
    printf("I2S: CLKDIV=0x%08X FIFOCTL=0x%08X TXCNT=0x%08X\n",
           (unsigned)I2S_CLK_DIV, (unsigned)I2S_FIFO_CTL, (unsigned)I2S_TX_CNT);
    printf("I2S: CHANCFG=0x%08X TXSEL=0x%08X TXMAP=0x%08X\n",
           (unsigned)I2S_CHAN_CFG, (unsigned)I2S_TX_CSEL, (unsigned)I2S_TX_CMAP);
    printf("I2S: PIO_A_CFG0=0x%08X CFG1=0x%08X CFG2=0x%08X\n",
           (unsigned)H3_PIO_PORTA->CFG0, (unsigned)H3_PIO_PORTA->CFG1,
           (unsigned)H3_PIO_PORTA->CFG2);
    printf("I2S: PG_CFG0=0x%08X PG_DAT=0x%08X\n",
           (unsigned)PG_CFG0, (unsigned)PG_DAT);
    printf("I2S: ready (%d Hz, volume=%d%%)\n",
           g_sample_rate, (int)(g_volume * 100u / 1023u));
    return 0;
}

// Запись одного стерео-сэмпла (L/R в 16-bit signed) в I2S FIFO.
// Формат sun8i H3: слова MSB-aligned (value << 16).
// Ровный ритм 48 кГц: пара слов (L+R) за ~20.83 мкс по HS-таймеру.
// Это гарантирует, что FIFO не переполняется и не пустеет —
// и в тесте, и в эмуляторах (нет треска-мусора).
void i2s_push_sample(int16_t left, int16_t right) {
    if (!g_i2s_ready) return;

    if (g_muted) {
        g_muted = 0;
        PG_DAT |= (1u << 7);   // SD=1 — усилитель включён
    }

    // ритм 48 кГц (период 20.83 мкс)
    static uint32_t last_us = 0;
    uint32_t now = h3_hs_timer_lo_us();
    if (last_us) {
        uint32_t want = last_us + 20;
        while ((int32_t)(h3_hs_timer_lo_us() - want) < 0)
            ;
    }
    last_us = now;

    I2S_FIFO_TX = (uint32_t)(uint16_t)(left * (int32_t)g_volume / 1023) << 16;
    I2S_FIFO_TX = (uint32_t)(uint16_t)(right * (int32_t)g_volume / 1023) << 16;
}

// ---- тест звука: синусоида на частоте freq (Гц), msec (мс) ----
// Генерим стерео-тон. Тайминг 48 кГц — внутри i2s_push_sample.
void i2s_test_tone(int freq, int msec) {
    if (!g_i2s_ready) return;
    if (freq < 20) freq = 20;
    if (msec <= 0) msec = 100;

    uint32_t step = (uint32_t)(((uint64_t)freq << 16) / 48000u);
    uint32_t phase = 0;
    int total = 48000 * msec / 1000;          // ~48000*0.1=4800 для 100 мс

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

        i2s_push_sample((int16_t)(s >> 8), (int16_t)(s >> 8));
    }
    // После теста: отключаем усилитель (SD=0), mute возвращаем
    g_muted = 1;
    PG_DAT &= ~(1u << 7);
    printf("I2S: test %d Hz finished, SD=0\n", freq);
}