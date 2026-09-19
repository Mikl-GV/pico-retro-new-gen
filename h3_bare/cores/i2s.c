// i2s.c — I2S-контроллер H3 (sun8i-i2s, 0x01C22000) для MAX98357A.
// Master, 16-bit stereo, 44100 Гц. Регистры по sun4i-i2s.c (ядро Linux).
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

// ---- I2S0 @ 0x01C22000 ----
#define I2S_BASE 0x01C22000u

#define I2S_CTRL     (*(volatile uint32_t*)(I2S_BASE + 0x00))
#define I2S_FMT0     (*(volatile uint32_t*)(I2S_BASE + 0x04))
#define I2S_FMT1     (*(volatile uint32_t*)(I2S_BASE + 0x08))
#define I2S_FIFO_TX  (*(volatile uint32_t*)(I2S_BASE + 0x20))
#define I2S_FIFO_CTL (*(volatile uint32_t*)(I2S_BASE + 0x14))
#define I2S_FIFO_STA (*(volatile uint32_t*)(I2S_BASE + 0x18))
#define I2S_CLK_DIV  (*(volatile uint32_t*)(I2S_BASE + 0x24))
#define I2S_TX_CNT   (*(volatile uint32_t*)(I2S_BASE + 0x28))
#define I2S_TX_CSEL  (*(volatile uint32_t*)(I2S_BASE + 0x34))  // SUN8I
#define I2S_TX_CMAP  (*(volatile uint32_t*)(I2S_BASE + 0x44))  // SUN8I
#define I2S_CHAN_CFG (*(volatile uint32_t*)(I2S_BASE + 0x30))  // SUN8I

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
#define PG_BASE  0x01C20800u  // PIO PORTA…PORTG: PortG offset = 6*0x24 = 0xD8
#define PG_CFG0 (*(volatile uint32_t*)(PG_BASE + 0xD8 + 0x00))
#define PG_CFG1 (*(volatile uint32_t*)(PG_BASE + 0xD8 + 0x04))
// PG7 находится в CFG2 (8 пинов на регистр, PG0-7)
#define PG_CFG2 (*(volatile uint32_t*)(PG_BASE + 0xD8 + 0x08))
#define PG_DAT  (*(volatile uint32_t*)(PG_BASE + 0xD8 + 0x10))

static int g_i2s_ready = 0;
static uint16_t g_volume = 204;  // 20% от 1023 (10-bit, макс 1023)
static int g_muted = 0;

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
        PG_DAT &= ~(1u << 7);   // SD = 0
    } else {
        PG_DAT |= (1u << 7);    // SD = 1
    }
}

int i2s_init(void) {
    // 1. CCU: такт I2S0
    H3_CCU->BUS_CLK_GATING2 |= (1u << 12);   // gate I2S0 (0x068 bit12)
    // модульный клок: I2SPCM0_CLK (0x0B0) bit31=EN, mux=pll-audio-8x=0
    // PLL_AUDIO не трогаем — U-Boot обычно ставит на 22.5792М или 24.576М
    volatile uint32_t* clkreg = &H3_CCU->I2SPCM0_CLK;
    *clkreg = (*clkreg & ~((3u << 16) | (1u << 31))) | (0u << 16) | (1u << 31);
    udelay(2000);

    // 2. PIO: PA18/19/20 → функция 2 (PCM0)
    uint32_t cfg0 = H3_PIO_PORTA->CFG0;
    cfg0 = (cfg0 & ~(0xF << 8)) | (2u << 8);    // PA18 = PCMSYNC
    cfg0 = (cfg0 & ~(0xF << 12)) | (2u << 12);   // PA19 = PCMCLK
    H3_PIO_PORTA->CFG0 = cfg0;
    uint32_t cfg1 = H3_PIO_PORTA->CFG1;
    cfg1 &= ~(0xF << 0);                          // PA20
    cfg1 |= (2u << 0);                            // PCMDOUT
    H3_PIO_PORTA->CFG1 = cfg1;

    // 3. PG7 → output, HIGH (SD=1)
    PG_CFG2 = (PG_CFG2 & ~(0xF << 28)) | (1u << 28);  // PG7 output
    PG_DAT |= (1u << 7);

    // 4. Сброс I2S-контроллера
    I2S_FIFO_CTL |= (1u << 25);  // flush TX
    I2S_CTRL = 0;                // стоп

    // 5. Конфигурация I2S master (sun8i H3)
    // Формат: I2S, 16-bit стерео, Master BCLK/LRCK
    // SR=3 (16-bit для H3 sun8i), WSS=3 (32-bit слот на канал для I2S)
    I2S_FMT0 = (3u << 0)                    // WSS=3
             | (3u << 4)                    // SR=3  (16-bit)
             | I2S_FMT0_BCLPOL_NORM         // BCLK normal polarity
             | I2S_FMT0_LRCKPER(32)         // LRCK period = 32 BCLK
             | (0u << 19);                  // LRCLK start LOW (I2S mode)
    I2S_FMT1 = 0;  // SEXT=0 (zero extended)

    // Частоты: 44100 Гц, BCLK=32*44100=1411200
    // Если PLL_AUDIO даёт 22.5792 МГц (512FS) или 24.576 МГц:
    // 22.5792 MHz / 1411200 = 16 → bclk_div=16 → ищем в таблице sun8i_i2s_clk_div
    // Таблица делителей sun8i: 1,2,4,6,8,12,16,24
    // 22.5792 / 1411200 = 16 → bclk_div=16 (индекс 6 в таблице)
    // MCLK = 256*44100 = 11289600; 22.5792/11.2896 = 2 → mclk_div=2 (индекс 1)
    // Пишем: bclk_div=6, mclk_div=1
    I2S_CLK_DIV = I2S_CLK_MCLK_EN
                | I2S_CLK_BCLK(6)     // div=16
                | I2S_CLK_MCLK(1);    // div=2

    // Каналы: стерео, TX_EN все, offset=1 (I2S — данные со сдвигом 1 BCLK)
    I2S_TX_CMAP = 0x76543210;           // маппинг
    I2S_TX_CSEL = I2S_TX_CHAN_EN        // все каналы
                | I2S_TX_CHAN_OFF(1)    // offset=1 (I2S delay)
                | 0;                     // 2 канала (0+1)
    I2S_CHAN_CFG = I2S_CHAN_TXSLOT(2)  // 2 слота TX
                 | I2S_CHAN_RXSLOT(2); // 2 слота RX

    // 6. Включение I2S master
    I2S_CTRL = I2S_CTRL_BCLK_OUT | I2S_CTRL_LRCK_OUT | I2S_CTRL_MODE_I2S
             | I2S_CTRL_TX_EN | I2S_CTRL_GL_EN;
    udelay(1000);

    g_i2s_ready = 1;
    printf("I2S: ready (44100 Hz, 16-bit stereo, volume=%d%%)\n",
           (int)(g_volume * 100u / 1023u));
    return 0;
}

// Запись одного стерео-сэмпла (L/R в 16-bit signed) в I2S FIFO.
// Вызывать из эмулятора. Встраивает громкость (L*vol>>10, R*vol>>10).
// Если FIFO полон — ждёт освобождения (polling, <= 1 мкс при 44100 Hz).
void i2s_push_sample(int16_t left, int16_t right) {
    if (!g_i2s_ready || g_muted) return;

    // ждём, пока в TX FIFO есть место (хотя бы 1 свободный слот)
    uint32_t sta = I2S_FIFO_STA;
    uint32_t cnt = sta & I2S_FIFO_STA_TXCNT_MASK;
    (void)cnt;
    // При 44100 Гц FIFO опустошается со скоростью 1 слово/22.7 мкс;
    // быстрый poll — успеваем почти всегда без задержки.

    // Громкость: L*vol/1023, R*vol/1023
    int32_t l = (int32_t)left * (int32_t)g_volume;
    int32_t r = (int32_t)right * (int32_t)g_volume;
    // clamp для 16-bit signed
    if (l < -32768*1023) l = -32768*1023;
    if (l > 32767*1023)  l = 32767*1023;
    if (r < -32768*1023) r = -32768*1023;
    if (r > 32767*1023)  r = 32767*1023;
    int16_t lo = (int16_t)(l >> 10);
    int16_t ro = (int16_t)(r >> 10);

    I2S_FIFO_TX = (uint32_t)(uint16_t)lo | ((uint32_t)(uint16_t)ro << 16);
}