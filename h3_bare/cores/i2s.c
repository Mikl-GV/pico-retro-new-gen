// i2s.c — I2S0 H3 (0x01C22000) для MAX98357A, 48 кГц стерео 16-бит.
//
// АРХИТЕКТУРА (r773): звук выводит CPU2 поллингом TX FIFO, без прерываний
// и без DMA.
//   core0 (эмулятор) --i2s_push_sample--> кольцо (.coherent)
//   CPU2 (audio_core) --i2s_poll_fill-->  I2S0_TX_FIFO (0x01C22020)
//
// Почему не DMA (r740..r771): 30 версий DMA-пути (LLI/DRQ/ISR) не дали
// стабильного звука — DMA замирал на ~34 пакетах (pending), звук рвался.
// Почему не прерывание I2S (r772): GIC-доставка IRQ на CPU2 не работает
// (INTID не доходит), любая ISR-схема мертва. Добавлено: CPU2 пишет в полный
// FIFO невозможность исключена проверкой места перед записью (TXE_CNT>2).
// Эталон uli/allwinner-bare-metal (audio_i2s.c, тот же H3) — поллинг FIFO.
// Задержка = FIFO (~1.3 мс), единый такт = аппаратный I2S (48 кГц).
//
// Хост-API (i2s_push_sample и т.д.) не изменился — эмуляторы не знают, где
// именно крутится звук. Тон/клик меню — через кольцо, как эмуляторы.
//
// Референс: H3 Datasheet V1.2 §8.6.7 (I2S/PCM) + uli/allwinner-bare-metal.
#include <stdint.h>
#include <string.h>
#include "h3.h"
#include "h3_ccu.h"
#include "h3_hs_timer.h"
#include "i2s.h"
#include "led.h"
#include "emu.h"

extern int printf(const char* fmt, ...);

#define I2S_BASE 0x01C22000u

#define I2S_CTRL     (*(volatile uint32_t*)(I2S_BASE + 0x00))
#define I2S_FMT0     (*(volatile uint32_t*)(I2S_BASE + 0x04))
#define I2S_FMT1     (*(volatile uint32_t*)(I2S_BASE + 0x08))
#define I2S_ISTA     (*(volatile uint32_t*)(I2S_BASE + 0x0C)) /* статус: TXU/TXO/TXE (write-1-clear) */
#define I2S_RX_FIFO  (*(volatile uint32_t*)(I2S_BASE + 0x10))
#define I2S_FIFO_CTL (*(volatile uint32_t*)(I2S_BASE + 0x14))
#define I2S_FIFO_STA (*(volatile uint32_t*)(I2S_BASE + 0x18))
#define I2S_FIFO_TX  (*(volatile uint32_t*)(I2S_BASE + 0x20))
#define I2S_CLK_DIV  (*(volatile uint32_t*)(I2S_BASE + 0x24))
#define I2S_TX_CNT   (*(volatile uint32_t*)(I2S_BASE + 0x28))
#define I2S_CHAN_CFG (*(volatile uint32_t*)(I2S_BASE + 0x30))
#define I2S_TX_CSEL  (*(volatile uint32_t*)(I2S_BASE + 0x34))
#define I2S_INT      (*(volatile uint32_t*)(I2S_BASE + 0x1C))
#define I2S_TX_CMAP  (*(volatile uint32_t*)(I2S_BASE + 0x44))

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

// r773 (КОРЕНЬ тишины/трелей): g_i2s_ready обязан лежать в .coherent
// (uncached). Раньше был в обычном BSS — core0 писал 1, но запись оседала
// в D-cache core0, а CPU2 (MMU выключен) читал физическую DRAM и видел 0
// (тишина) либо 0/1 случайно при вытеснении строки кэша (трели/метроном).
static int g_i2s_ready __attribute__((section(".coherent"), aligned(64))) = 0;
static int g_volume_pct = 20;
static int g_muted = 1;

void i2s_dc_shift_set(int shift) {
    // r734: DC-блокер удалён (r642) — API сохранён для совместимости хостов.
    (void)shift;
}

// ---- Громкость/мьют (PA10 — SD усилителя, прямой писатель core0) ----
void i2s_volume(int p) {
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    g_volume_pct = p;
    // r706: аппаратный мьют при 0 громкости — PA10 в 0, усилитель выключен.
    if (g_volume_pct == 0)
        pa_dat_set(SD_PIN, 0);
    else if (!g_muted)
        pa_dat_set(SD_PIN, 1);
    printf("audio: volume=%d%%\n", p);
}
int i2s_volume_pct(void) { return g_volume_pct; }
int i2s_ready(void) { return g_i2s_ready; }

void i2s_mute(int m) {
    g_muted = m;
    pa_dat_set(SD_PIN, m ? 0 : 1);
}

// ---- Кольцо потока эмулятора (core0 → CPU2), в .coherent (uncached) ----
// r584/r715: индексы в .coherent; r727: SPSC — продюсер НЕ пишет rd.
#define AUDIO_RING_SIZE 8192
static int16_t g_ring_l[AUDIO_RING_SIZE] __attribute__((section(".coherent"), aligned(8)));
static int16_t g_ring_r[AUDIO_RING_SIZE] __attribute__((section(".coherent"), aligned(8)));
static volatile uint32_t g_ring_wr __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_ring_rd __attribute__((section(".coherent"), aligned(64))) = 0;

// r593: барьер ДО инкремента rd (сэмплы видны раньше сдвига).
static inline void ring_rd_advance(void) {
    __asm volatile("dmb sy" ::: "memory");
    g_ring_rd++;
    __asm volatile("dmb sy" ::: "memory");
}

// ---- Почта core0↔CPU2 (аудио-ядро) в .coherent ----
static volatile uint32_t g_audio_state  __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_cmd    __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_seq    __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_ack    __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_beat   __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_paused_f __attribute__((section(".coherent"), aligned(64))) = 0;

// r772/r773: DMA-буфер и весь DMA-слой УДАЛЕНЫ. Вывод — поллинг TX FIFO на
// CPU2 (i2s_poll_fill): единый такт = аппаратный I2S (48 кГц), задержка =
// FIFO (~1.3 мс), никакого DMA/LLI/прерываний/поллинга регистров DMA.

// Диагностика: дропнутые пары кольца (продюсер быстрее CPU2).
static uint32_t g_drop_cnt = 0;
void i2s_drop_cnt_reset(void) { g_drop_cnt = 0; }
uint32_t i2s_drop_cnt(void) { return g_drop_cnt; }

// ---- Fade-out (r745, возврат r591-593): плавное затухание к 0 ----
// При пустом кольце (пауза между кадровыми пачками) НЕ пишем резкий 0 —
// ступенька на стыке «пачка→тишина→пачка» давала щелчки/шум. Вместо этого
// последняя пара затухает к 0 за ~1 мс (I2S_HOLD_FADE_PAIRS пар). hold/fade
// пишет CPU2 (долив), сбрасывает core0 (ring_reset) → в .coherent.
#define I2S_HOLD_FADE_PAIRS 48   // ~1 мс затухания при 48 кГц
static int16_t hold_l __attribute__((section(".coherent"), aligned(64))) = 0;
static int16_t hold_r __attribute__((section(".coherent"), aligned(64))) = 0;
static int fade_left __attribute__((section(".coherent"), aligned(64))) = 0;
// r755: fade-in/атака ВОЗВРАЩЕНЫ. Атака: плавный вход из тишины — первый
// ГРОМКИЙ сэмпл нарастает от 0 (нет ступеньки 0→громко).
#define I2S_SILENCE_LEVEL 128
#define I2S_ATTACK_SILENCE 48
static int attack_left __attribute__((section(".coherent"), aligned(64))) = 0;
static int silent_run __attribute__((section(".coherent"), aligned(64))) = 0;

// r776 (P0-5 аудита): владелец fade-полей — ОДИН. hold/fade/attack/silent_run
// пишет либо CPU2 (в i2s_poll_fill), либо core0 (в i2s_ring_reset), но НЕ оба
// одновременно. Флаг выставляет ТОЛЬКО core0 перед сбросом ring_reset и
// снимает после; пока флаг установлен, CPU2 в i2s_poll_fill долив не делает
// и fade-поля не трогает. Устраняет формальный data race (RMW одного
// uncached-слова с двух ядер без владельца).
static volatile uint32_t g_audio_reset __attribute__((section(".coherent"), aligned(64))) = 0;

// r754: диагностика CPU2. Пишутся в доливе (CPU2), читаются из SLT (core0).
// В .coherent (uncached) — чтобы core0 не видел stale из D-кэша.
static volatile uint32_t g_cpu2_stage     __attribute__((section(".coherent"), aligned(64))) = 0; // 0=вне долива 2=внутри
static volatile uint32_t g_flush_enter    __attribute__((section(".coherent"), aligned(64))) = 0; // входов в долив
static volatile uint32_t g_flush_exit     __attribute__((section(".coherent"), aligned(64))) = 0; // выходов из долива
static volatile uint32_t g_flush_written  __attribute__((section(".coherent"), aligned(64))) = 0; // пар записано в TX FIFO
static volatile uint32_t g_flush_skip_full __attribute__((section(".coherent"), aligned(64))) = 0; // выходов по полному FIFO

// r773: ДОЛИВ БЕЗ ПРЕРЫВАНИЙ — поллинг TX FIFO в цикле CPU2 (эталон
// uli/allwinner-bare-metal audio_i2s.c). Вызывается из audio_core (CPU2):
// пишем пары из кольца в I2S TX FIFO, пока там есть место (TXE_CNT > 2).
// Никакого GIC/DMA/LLI/когерентности. Если кольцо пусто — плавный fade к 0.
int i2s_poll_fill(void) {
    if (!g_i2s_ready) return 0;
    // r776 (P0-5): во время i2s_ring_reset (core0) fade-поля и кольцо
    // принадлежат core0 — CPU2 не доливает и не трогает их (иначе RMW-гонка).
    if (g_audio_reset) return 0;
    g_cpu2_stage = 2;
    g_flush_enter++;
    // Сколько пар можно записать, пока в FIFO есть место (> 2 слов).
    uint32_t room = (I2S_FIFO_STA >> 16) & 0xFF;   // TXE_CNT [23:16]
    if (room <= 2) {
        // FIFO полон — I2S не освобождает место (нет вывода/такта).
        g_flush_skip_full++;   // диагностика: выход по полному FIFO
    }
    int n = 0;
    while (n < 24 && room > 2) {
        if ((uint32_t)(g_ring_wr - g_ring_rd) > 0) {
            // r776 (P0-4 аудита): барьер между чтением wr и данными слота.
            // Продюсер (core0) публикует: store g_ring_l/r; dmb; store g_ring_wr++.
            // Потребитель (CPU2) обязан упорядочить свои load — иначе на слабой
            // модели Cortex-A7 store-сторона могла бы быть переупорядочена
            // относительно wr, и CPU2 прочёл бы данные раньше их публикации.
            __asm volatile("dmb sy" ::: "memory");
            uint32_t r = g_ring_rd & (AUDIO_RING_SIZE - 1);
            int32_t L = g_ring_l[r];
            int32_t R = g_ring_r[r];
            hold_l = (int16_t)L; hold_r = (int16_t)R;
            fade_left = I2S_HOLD_FADE_PAIRS;
            int lvl_l = L < 0 ? -L : L;
            int lvl_r = R < 0 ? -R : R;
            if (lvl_l >= I2S_SILENCE_LEVEL || lvl_r >= I2S_SILENCE_LEVEL) {
                silent_run = 0;
                if (attack_left > 0) {
                    attack_left--;
                    uint32_t g = I2S_HOLD_FADE_PAIRS - (uint32_t)attack_left;
                    L = (int32_t)hold_l * (int32_t)g / I2S_HOLD_FADE_PAIRS;
                    R = (int32_t)hold_r * (int32_t)g / I2S_HOLD_FADE_PAIRS;
                }
            } else {
                if (++silent_run >= I2S_ATTACK_SILENCE)
                    attack_left = I2S_HOLD_FADE_PAIRS;
            }
            // TX FIFO, формат 32 бита: L в старших 16 (TXIM=0).
            I2S_FIFO_TX = ((uint32_t)(uint16_t)L << 16) | (uint32_t)(uint16_t)R;
            g_flush_written++;
            ring_rd_advance();
        } else {
            // Кольцо пусто: плавно гасим (fade) — иначе ступенька на стыке.
            if (fade_left > 0) {
                fade_left--;
                int32_t l = (int32_t)hold_l * fade_left / I2S_HOLD_FADE_PAIRS;
                int32_t r = (int32_t)hold_r * fade_left / I2S_HOLD_FADE_PAIRS;
                I2S_FIFO_TX = ((uint32_t)(uint16_t)l << 16) | (uint32_t)(uint16_t)r;
            } else {
                I2S_FIFO_TX = 0;   // тишина
            }
        }
        n++;
        room = (I2S_FIFO_STA >> 16) & 0xFF;
    }
g_flush_exit++;
    g_cpu2_stage = 0;
    return n;
}

// ---- Аудио-почта core0↔CPU2 (N1: cmd+ack+seq) ----
int i2s_audio_core_active(void) { return g_audio_state ? 1 : 0; }
uint32_t i2s_audio_beat(void)   { return g_audio_beat; }
void i2s_audio_set_state(int on) { g_audio_state = on ? 1 : 0; }
void i2s_audio_set_beat(uint32_t b) { g_audio_beat = b; }
int i2s_audio_paused(void) { return g_audio_paused_f ? 1 : 0; }

void i2s_audio_cmd(uint32_t cmd) {
    if (!g_audio_state) return;
    g_audio_cmd = cmd;
    __asm volatile("dmb sy" ::: "memory");
    uint32_t nseq = g_audio_seq + 1;
    if (nseq == 0) nseq = 1;
    g_audio_seq = nseq;
    if (cmd == AUDIO_CMD_PAUSE) {
        for (uint32_t t = 0; t < 100000 && g_audio_ack != nseq; t++) {
#if defined(__GNUC__)
            __asm__ volatile("nop; nop; nop; nop");
#endif
        }
    }
}

void i2s_audio_poll_cmd(void) {
    uint32_t seq = g_audio_seq;
    uint32_t ack = g_audio_ack;
    if (seq == ack) return;
    __asm volatile("dmb sy" ::: "memory");
    uint32_t cmd = g_audio_cmd;
    // r766 (D3): ветка AUDIO_CMD_RING_RESET удалена — команду никто не шлёт
    // (сброс делает i2s_ring_reset() на core0). CPU2 обрабатывает только
    // PAUSE/RESUME, которые реально используются (тон/клик/меню).
    if (cmd == AUDIO_CMD_PAUSE) {
        g_audio_paused_f = 1;
    } else if (cmd == AUDIO_CMD_RESUME) {
        g_audio_paused_f = 0;
    }
    __asm volatile("dmb sy" ::: "memory");
    g_audio_cmd = 0;
    g_audio_ack = seq;
}

// ---- Продюсер (core0): эмулятор кладёт пару в кольцо ----
void i2s_push_sample(int16_t left, int16_t right) {
    if (!g_i2s_ready) return;
    if (g_muted) {
        g_muted = 0;
        pa_dat_set(SD_PIN, g_volume_pct > 0 ? 1 : 0);
    }
    // SPSC: при полном буфере дропаем (как r727) — не двигаем rd.
    if ((uint32_t)(g_ring_wr - g_ring_rd) >= (AUDIO_RING_SIZE - 1)) {
        g_drop_cnt++;
        return;
    }
    int32_t v = (g_volume_pct * 32) / 100;
    int32_t L = (int32_t)left  * v / 32;
    int32_t R = (int32_t)right * v / 32;
    if (L > 32767) L = 32767;
    if (L < -32768) L = -32768;
    if (R > 32767) R = 32767;
    if (R < -32768) R = -32768;
    uint32_t w = g_ring_wr & (AUDIO_RING_SIZE - 1);
    g_ring_l[w] = (int16_t)L;
    g_ring_r[w] = (int16_t)R;
    __asm volatile("dmb sy" ::: "memory");
    g_ring_wr++;
}

// ---- Полный сброс звука (смена игры / выход в меню / перед кликом-тоном) ----
// r773: DMA/ISR слоёв нет — остаётся flush TX FIFO + сброс кольца. Долив
// делает CPU2 (i2s_poll_fill) всегда, так что после сброса звук продолжится
// с тишины (кольцо пусто) автоматически.
void i2s_ring_reset(void) {
    if (!g_i2s_ready) return;
    // r776 (P0-5): core0 забирает владение fade-полями на время сброса.
    // CPU2 (i2s_poll_fill) проверяет флаг и не доливает, пока он установлен.
    g_audio_reset = 1;
    __asm volatile("dmb sy" ::: "memory");
    // r748 (СИНХРОНИЗАЦИЯ): возвращаем CPU2 в RESUME — долив не зависит
    // от паузы, но почта должна остаться консистентной (клики/тоны ждут ack).
    if (g_audio_state) {
        i2s_audio_cmd(AUDIO_CMD_PAUSE);   // CPU2 отметит паузу (почта)
    }
    // r764: жёсткий перезапуск TX-блока I2S. Если контроллер «залип» в
    // underflow (ISTA=0x40) — простой flush FIFO не выводит из ступора;
    // обязателен TX_EN off → flush+статус → TX_EN on.
    I2S_CTRL &= ~I2S_CTRL_TX_EN;
    __asm volatile("dsb" ::: "memory");
    udelay(2);
    // hold/fade-сброс (обновляются в доливе CPU2, совм. с core0)
    hold_l = 0; hold_r = 0; fade_left = 0;
    // r755: атака (fade-in) взводится на входе — первый сэмпл после reset
    // нарастает от 0 (нет щелчка при старте игры/клика/тона).
    attack_left = I2S_HOLD_FADE_PAIRS;
    silent_run = 0;
    g_ring_wr = 0; g_ring_rd = 0;
    g_drop_cnt = 0;
    // Аппаратный flush TX FIFO — хвост предыдущей системы не играется.
    I2S_FIFO_CTL |= (1u << 25);
    udelay(2);
    I2S_FIFO_CTL &= ~(1u << 25);
    I2S_TX_CNT = 0;
    // r757: сброс статусных флагов I2S (TXU/TXO/TXE и др.) write-1-to-clear.
    I2S_ISTA = 0x7F;
    udelay(10);
    // r768: TX_EN вернуть ОБРАТНО (после перезапуска TX-блока).
    I2S_CTRL |= I2S_CTRL_TX_EN;
    __asm volatile("dsb" ::: "memory");
    // r748: ВОЗВРАТ CPU2 В РАБОТУ (почта консистентна).
    if (g_audio_state) {
        i2s_audio_cmd(AUDIO_CMD_RESUME);
    }
    // r776 (P0-5): core0 отдаёт владение fade-полями обратно CPU2. dmb ДО
    // снятия флага гарантирует, что записи fade-полей/кольца видны CPU2
    // раньше, чем он снова начнёт долив (публикация до снятия замка).
    __asm volatile("dmb sy" ::: "memory");
    g_audio_reset = 0;
}

// ---- Долив кольца → TX FIFO (вызывает ТОЛЬКО CPU2, audio_core.c) ----
// r772: i2s_flush_max/i2s_flush (DMA-эпоха) УДАЛЕНЫ — долив теперь
// единственный: i2s_poll_fill в цикле CPU2. Эмуляторы зовут i2s_push_sample
// (в кольцо), CPU2 сам выпивает кольцо в TX FIFO.

// ---- Инициализация I2S + DMA ----
int i2s_init(void) {
    pll_audio_enable();

    H3_CCU->BUS_CLK_GATING2 |= (1u << 12);
    CCU_RST_I2S0 |= (1u << 12);

    volatile uint32_t* clk = &H3_CCU->I2SPCM0_CLK;
    *clk = (*clk & ~((3u << 16) | (1u << 31))) | (3u << 16) | (1u << 31);
    udelay(2000);

    // Пины: PA18=PCM0_SYNC(LRC), PA19=PCM0_CLK(BCLK), PA20=PCM0_DOUT(DIN) (r0.413).
    // PA16..PA23 живут в CFG2 (r503). PA10 — SD усилителя (CFG1).
    uint32_t c2 = H3_PIO_PORTA->CFG2;
    c2 &= ~((0xFu << 8) | (0xFu << 12) | (0xFu << 16));
    c2 |=  (2u << 8) | (2u << 12) | (2u << 16);   // функция 2 = PCM0
    H3_PIO_PORTA->CFG2 = c2;

    H3_PIO_PORTA->CFG1 = (H3_PIO_PORTA->CFG1 & ~(0xFu << ((SD_PIN - 8) * 4)))
                         | (1u << ((SD_PIN - 8) * 4));
    pa_dat_set(SD_PIN, 1);

    I2S_CTRL = 0;
    I2S_FIFO_CTL |= (1u << 24) | (1u << 25);   // flush RX+TX FIFO
    udelay(100);
    // r753: ЯВНЫЙ сброс FTX/FRX (биты 25/24). Даже при self-clear не полагаемся
    // на него: если хотя бы один бит останется в 1 после включения TX_EN,
    // FIFO вечно флашится -> звука нет. (ring_reset снимает бит 25 так же.)
    I2S_FIFO_CTL &= ~((1u << 25) | (1u << 24));
    // r757: сброс статусных флагов I2S_ISTA (0x0C) — TXU(6)/TXO(5)/TXE(4)
    // пишутся write-1-to-clear. Если они остались от прошлого зависания
    // (underrun/overrun), DRQ-запрос к DMA заперт навсегда — отсюда «DMA
    // прошёл 17 пакетов и встал», «шум-стык при входе», «ритмичные щелчки».
    // Сбрасываем все биты ISTA (write 1 во все читаемые).
    I2S_ISTA = 0x7F;   // TXU|TXO|TXE|RXU|RXO|RXA (+ мусор старших)
    udelay(10);
    for (int i = 0; i < 256; i++) { volatile uint32_t d = I2S_RX_FIFO; (void)d; }
    I2S_TX_CNT = 0;

// 48к: SR=3 (16-бит), SW=7 (32-бит слот), LRCKPER=32, BCLK 3.072M
    // r759 (ЭТАЛОН linux sun8i-h3-i2s): LRCK_PERIOD[17:8] = slot_width = 32
    // (в I2S mode это «число BCLK на канал»). При BCLK=3.072M: period=32 →
    // LRCK = 3.072M/64 = 48кГц. Раньше стояло 64 → LRCK = 3.072M/128 = 24кГц:
    // железо играло на 24к, мы лили 48к → переполнение DMA-буфера/кольца,
    // периодический FIFO-underrun (ISTA=0x40) → «тиканье часов», затыки DMA.
    I2S_FMT0 = (7u << 0) | (3u << 4) | (0u << 7) | I2S_FMT0_LRCKPER(32) | (0u << 19);
    I2S_FMT1 = 0;
    // BCLK 3.072M из PLL_AUDIO (24.576M): BCLKDIV[7:4]=5 = «Divide by 8»
    // (по даташиту CLKD: 3=÷4 → 6.144M → LRCK 96к — НЕВЕРНО для 48к).
    // MCLK на пин = PLL/2 (MCLKDIV=2); MCLKO_EN=1 — MCLK выводится.
    // r754: BCLKDIV был 3 (÷4 → 96к) — отсюда «рычание/пульс» до фикса;
    // r755: строка восстановлена после временной потери при правке FMT0.
    I2S_CLK_DIV = I2S_CLK_MCLK_EN | I2S_CLK_BCLK(5) | I2S_CLK_MCLK(2);

    I2S_TX_CMAP = 0x76543210;
    I2S_TX_CSEL = (3u << 4) | I2S_TX_CHAN_OFF(1) | 1;
    I2S_CHAN_CFG = I2S_CHAN_TXSLOT(2) | I2S_CHAN_RXSLOT(2);

    // r772/r773: прерывания НЕ используем (GIC не работает на CPU2).
    // TXEI_EN/DRQ не включаем — долив поллингом (i2s_poll_fill). TXTL=64
    // дефолт: порог для поллинга не критичен, оставляем как есть.

    I2S_FIFO_CTL = (I2S_FIFO_CTL & ~((0x7Fu) << 12)) | (0x40u << 12);   // TXTL=64
    I2S_INT = 0;   // без прерываний и DRQ — чистый поллинг

    I2S_CTRL = I2S_CTRL_BCLK_OUT | I2S_CTRL_LRCK_OUT | (1u << 4)
             | I2S_CTRL_TX_EN | I2S_CTRL_SDO_EN0 | I2S_CTRL_GL_EN;
    udelay(1000);

    g_i2s_ready = 1;
    printf("I2S: ready (48000 Hz, poll-fill path, vol=%d%%)\n", g_volume_pct);
    return 0;
}

// ---- Тон/клик меню: через кольцо (как эмуляторы) → CPU2 → DMA ----
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

void i2s_click(void) {
    if (!g_i2s_ready) return;
    int was_muted = g_muted;
    // r746: очистить очередь ДО клика — иначе клик слышится через ~170 мс
    // задержки буфера («размазанные клики»). ring_reset: flush FIFO + сброс
    // кольца — клик прозвучит сразу и чётко.
    i2s_ring_reset();
    if (was_muted) i2s_mute(0);
    uint32_t step = (uint32_t)(((uint64_t)1500u << 16) / 48000u);
    uint32_t ph = 0;
    int total = 256;   // 8 полных периодов 1500 Гц
    for (int d = 0; d < total; d++) {
        int32_t s = (int32_t)sin_tab[(ph >> 8) & 0xFF] >> 2;
        ph += step;
        s = (int32_t)((int64_t)s * (total - d) / total);
        i2s_push_sample((int16_t)s, (int16_t)s);
    }
    if (was_muted) i2s_mute(1);
}

// ---- Диагностика (SLT-тест) ----
// r772/r773: DMA-слой удалён — возвращаем живые счётчики poll-долива:
//   flushed = входов в i2s_poll_fill (сколько раз CPU2 долил)
//   written = пар реально записано в TX FIFO
//   skipfull = выходов по полному FIFO (нет места)

// r772: живые счётчики поллинг-долива:

void i2s_cpu2_diag(uint32_t* flush, uint32_t* written, uint32_t* skipfull, uint32_t* dummy) {
    (void)dummy;
    if (flush)    *flush    = g_flush_enter;
    if (written)  *written  = g_flush_written;
    if (skipfull) *skipfull = g_flush_skip_full;
}
void i2s_cpu2_pairs_written_get(uint32_t* v) { if (v) *v = g_flush_written; }
void i2s_cpu2_stage_get(uint32_t* st, uint32_t* en, uint32_t* ex) {
    if (st) *st = g_cpu2_stage;
    if (en) *en = g_flush_enter;
    if (ex) *ex = g_flush_exit;
}
void i2s_ring_wr_rd_get(uint32_t* w, uint32_t* r) { if(w)*w=g_ring_wr; if(r)*r=g_ring_rd; }
void i2s_flush_diag_get(uint32_t* nempty, uint32_t* nempty_full) {
    if (nempty)      *nempty      = g_flush_enter;
    if (nempty_full) *nempty_full = g_flush_skip_full;
}