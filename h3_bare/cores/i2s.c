// i2s.c — I2S0 H3 (0x01C22000) для MAX98357A, 48 кГц стерео 16-бит.
//
// АРХИТЕКТУРА (решение владельца): звук обслуживает CPU2 (аудио-ядро),
// в FIFO пишет ЖЕЛЕЗНЫЙ DMA.
//   core0 (эмулятор) --i2s_push_sample--> кольцо (.coherent)
//   CPU2 (audio_core) --i2s_flush_max--> DMA-буфер (.dma_buf)
//   DMA (h3_dma.c)    --по DRQ-->        I2S0_TX_FIFO (0x01C22020)
//
// Зачем DMA вместо ручного вывода CPU2 в FIFO (r740): запись в полностью
// заполненный TX FIFO вешала AHB-шину (CPU2 залипал внутри flush_max без
// исключений: ST:2 EN=EX+1, ABT:0). Теперь CPU2 пишет в ОБЫЧНУЮ RAM
// (DMA-буфер) — такая запись не может залипнуть; в FIFO пишет только DMA по
// аппаратному DRQ, когда в FIFO есть место. Залипание исключено, CPU2
// остаётся единственным хозяином потока.
//
// Хост-API (i2s_push_sample и т.д.) не изменился — эмуляторы не знают, где
// именно крутится звук. Тон/клик меню — через кольцо, как эмуляторы.
//
// Референс DMA: drivers/dma/sun6i-dma.c (sun8i_h3) + H3 Datasheet V1.2 4.11.
// Драйвер — platform/h3_dma.c, заголовок include/h3_dma.h.
#include <stdint.h>
#include <string.h>
#include "h3.h"
#include "h3_ccu.h"
#include "h3_hs_timer.h"
#include "i2s.h"
#include "led.h"
#include "emu.h"
#include "h3_dma.h"
#include "gic.h"

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

static int g_i2s_ready = 0;
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

// ---- Почта core0↔CPU2 (аудио-ядро) в .coherent ----
static volatile uint32_t g_audio_state  __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_cmd    __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_seq    __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_ack    __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_beat   __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_paused_f __attribute__((section(".coherent"), aligned(64))) = 0;

// ---- DMA-буфер звука (.dma_buf, uncached) — пишет CPU2, читает DMA ----
// ФОРМАТ = формат TX FIFO: 32-битные слова, сэмпл в СТАРШИХ битах (TXIM=0).
// Пара = ДВА слова: [L<<16][R<<16]. 64 КБ = 8192 пары ≈ 170 мс @48к.
#define DMA_BUF_SIZE_BYTES (64 * 1024)
#define DMA_BUF_WORDS      (DMA_BUF_SIZE_BYTES / 4)
#define DMA_BUF_PAIRS      (DMA_BUF_SIZE_BYTES / 8)
extern unsigned char _dma_buf_start[];
static volatile uint32_t* g_dma_buf = (volatile uint32_t*)(uintptr_t)_dma_buf_start;

// Диагностика: дропнутые пары кольца (продюсер быстрее CPU2).
static uint32_t g_drop_cnt = 0;
void i2s_drop_cnt_reset(void) { g_drop_cnt = 0; }
uint32_t i2s_drop_cnt(void) { return g_drop_cnt; }

// ---- Fade-out (r745, возврат r591-593): плавное затухание к 0 ----
// При пустом кольце (пауза между кадровыми пачками) НЕ пишем резкий 0 —
// ступенька на стыке «пачка→тишина→пачка» давала щелчки/шум. Вместо этого
// последняя пара затухает к 0 за ~1 мс (I2S_HOLD_FADE_PAIRS пар). hold/fade
// пишет CPU2 (flush_max), сбрасывает core0 (ring_reset) → в .coherent.
#define I2S_HOLD_FADE_PAIRS 48   // ~1 мс затухания при 48 кГц
static int16_t hold_l __attribute__((section(".coherent"), aligned(64))) = 0;
static int16_t hold_r __attribute__((section(".coherent"), aligned(64))) = 0;
static int fade_left __attribute__((section(".coherent"), aligned(64))) = 0;
// r755: fade-in/атака ВОЗВРАЩЕНЫ (потерялись при переходе на DMA в r740).
// Атака: плавный вход из тишины. Взводится ring_reset/командой RING_RESET и
// длительной тихой сценой (silent_run); первый ГРОМКИЙ сэмпл нарастает от 0 —
// нет ступеньки 0→громко (щелчок на входе в игру/после паузы).
#define I2S_SILENCE_LEVEL 128
#define I2S_ATTACK_SILENCE 48
static int attack_left __attribute__((section(".coherent"), aligned(64))) = 0;
static int silent_run __attribute__((section(".coherent"), aligned(64))) = 0;

// r740 (ДМА): счётчик дропнутых пар DMA-буфера (CPU2 быстрее DMA).
static uint32_t g_dma_drop_cnt = 0;

// r754: диагностика CPU2 (раньше — мёртвые заглушки). Пишутся в flush_max
// (CPU2), читаются геттерами из SLT (core0). ВАЖНО: в .coherent (uncached) —
// CPU2 пишет физически, core0 читает без кэша; в обычном BSS core0 видел бы
// stale из D-кэша (symptom: ex>en, мусор в flush/written).
static volatile uint32_t g_cpu2_stage     __attribute__((section(".coherent"), aligned(64))) = 0; // 0=вне flush_max 2=внутри
static volatile uint32_t g_flush_enter    __attribute__((section(".coherent"), aligned(64))) = 0; // входов в flush_max
static volatile uint32_t g_flush_exit     __attribute__((section(".coherent"), aligned(64))) = 0; // выходов из flush_max
static volatile uint32_t g_flush_written  __attribute__((section(".coherent"), aligned(64))) = 0; // пар записано в DMA-буфер
static volatile uint32_t g_flush_skip_full __attribute__((section(".coherent"), aligned(64))) = 0; // выходов по полному DMA-буферу
// r763: DMA_IRQ_PEND сбрасывается БЕЗУСЛОВНО в цикле CPU2 (audio_core.c),
// как linux sun8i-dma в ISR (writel(status, IRQ_STAT) на каждый пакет).
// Rate-limit был ошибкой: при затыке канал вставал быстрее, чем раз в 256,
// и снова замирал. Одна MMIO-запись на проход CPU2 — дёшево и всегда успевает.

// Индекс пары, куда CPU2 пишет в DMA-буфер [0, DMA_BUF_PAIRS). SPSC с DMA:
// fill = (dma_wr - played) & mask; DMA-позиция берётся из CUR_SRC (регистр).
// Пишет ТОЛЬКО CPU2 (flush_max) и core0-диагностика читает; поле в .coherent.
static volatile uint32_t g_dma_wr __attribute__((section(".coherent"), aligned(64))) = 0;

// r761: позиция DMA (в парах, [0, DMA_BUF_PAIRS)) для CPU2 в .coherent.
// ПИШЕТ core0 (i2s_init/i2s_ring_reset — там, где он и так трогает DMA;
// читает CH_CUR_SRC один раз). ЧИТАЕТ CPU2 (dma_played_pairs).
// Это убирает у CPU2 (MMU off) доступ к MMIO CH_CUR_SRC и к кэшируемой
// глобальной g_ch (h3_dma.c, обычный BSS) — оба могли давать мусор/0
// (отсюда «free=0 навсегда», deadlock кольца: rd=0, written не растёт).
static volatile uint32_t g_dma_curpos __attribute__((section(".coherent"), aligned(64))) = 0;

// Обновить g_dma_curpos из аппаратного регистра. Вызывает ТОЛЬКО core0
// (редко: инициализация/ring_reset), когда DMA остановлен/только стартовал.
static inline void dma_curpos_update(void) {
    uint32_t base = (uint32_t)(uintptr_t)_dma_buf_start;
    uint32_t pos  = h3_dma_audio_cur_pos();
    uint32_t cur  = 0;
    if (pos >= base && pos < base + DMA_BUF_SIZE_BYTES)
        cur = (pos - base) / 8;
    g_dma_curpos = cur;
    __asm volatile("dmb sy" ::: "memory");
}

// Позиция DMA (пар от начала) — [0, DMA_BUF_PAIRS). Чтение .coherent-копии,
// БЕЗ MMIO и БЕЗ g_ch. Для CPU2 и core0 одинаково.
static inline uint32_t dma_played_pairs(void) {
    return g_dma_curpos;
}

// Счётчик пришедших аудио-IRQ (диагностика, читает core0 из SLT).
static volatile uint32_t g_irq_cnt __attribute__((section(".coherent"), aligned(64))) = 0;
uint32_t i2s_audio_irq_cnt(void) { return g_irq_cnt; }

// r764: ISR аудио-DMA (CPU2, INTID 114). Вызывается из gic_dispatch при
// каждом PKG-прерывании (DMA завершил LLI-пакет и запросил обслуживание).
// ДЕЛАЕТ: сброс DMA_IRQ_PEND (иначе канал замирает) + обновление позиции
// из счётчика пакетов (БЕЗ чтения CUR_SRC — конвейер шины может отдать
// мусор). Это размораживает g_dma_curpos: CPU2 в flush_max видит, что DMA
// продвинулся, и продолжает писать (rd/written растут, нет deadlock-а).
static void i2s_dma_isr(uint32_t intid) {
    (void)intid;
    uint32_t n = h3_dma_audio_pkg_isr();   // сброс pending + счётчик
    // Позиция = (n пакетов × half). g_buf=64КБ → half=4096 пар, уже в парах.
    g_dma_curpos = (n * (DMA_BUF_PAIRS / 2)) & (DMA_BUF_PAIRS - 1);
    // Прирост счётчика (для диагностики SLT — «сколько IRQ пришло»).
    g_irq_cnt = n;
    __asm volatile("dmb sy" ::: "memory");
}
static inline uint32_t dma_fill(void) {
    return (g_dma_wr - dma_played_pairs()) & (DMA_BUF_PAIRS - 1);
}
static inline uint32_t dma_free_pairs(void) {
    return (DMA_BUF_PAIRS - 1) - dma_fill();
}

// Запись пары в DMA-буфер (2 слова: L<<16, R<<16). Только CPU2.
static inline void dma_write_pair(uint32_t idx, int16_t l, int16_t r) {
    uint32_t w = (idx & (DMA_BUF_PAIRS - 1)) * 2;
    g_dma_buf[w]     = (uint32_t)(uint16_t)l << 16;
    g_dma_buf[w + 1] = (uint32_t)(uint16_t)r << 16;
}

// Занулить DMA-буфер (тишина) — вызывается при сбросе, иначе «доезд».
static void dma_buf_clear(void) {
    for (uint32_t i = 0; i < DMA_BUF_WORDS; i++)
        g_dma_buf[i] = 0;
    __asm volatile("dsb" ::: "memory");
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

// r593: барьер ДО инкремента rd (сэмплы видны раньше сдвига).
static inline void ring_rd_advance(void) {
    __asm volatile("dmb sy" ::: "memory");
    g_ring_rd++;
    __asm volatile("dmb sy" ::: "memory");
}

// ---- Полный сброс звука (смена игры / выход в меню / перед кликом-тоном) ----
void i2s_ring_reset(void) {
    if (!g_i2s_ready) return;
    // r748 (СИНХРОНИЗАЦИЯ): ring_reset ОБЯЗАН вернуть CPU2 в RESUME. Раньше
    // (r745/r746) мы ставили PAUSE и НЕ снимали — CPU2 замирал, кольцо не
    // выпивалось (dropped=134729, тишина тестов, клики «песок»), а после
    // выхода из меню выплёскивал весь накопленный мусор («шум как ТВ»).
    // С DMA единственный писатель DMA-буфера — CPU2, поэтому он должен быть
    // активен ВСЕГДА (и в меню, и в тестах): прямого вывода в FIFO нет.
    if (g_audio_state) {
        i2s_audio_cmd(AUDIO_CMD_PAUSE);   // CPU2 встал (не пишет в буфер)
    }
    h3_dma_audio_stop();                  // DMA остановлен — старый поток рвётся
    // r764 (по рекомендации внешнего аудита): жёсткий перезапуск TX-блока I2S.
    // Если контроллер «залип» в underflow (ISTA=0x40) — простой flush FIFO не
    // выводит его из ступора; обязателен TX_EN off → flush+статус → TX_EN on.
    I2S_CTRL &= ~I2S_CTRL_TX_EN;
    __asm volatile("dsb" ::: "memory");
    udelay(2);
    // hold/fade-сброс (обновляются в flush_max, совм. с core0/CPU2)
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
    // Без этого зависший underrun/overrun запирает DRQ к DMA навсегда —
    // после выхода из игры «хвост», «пульсирующий шум», «стык шалит».
    I2S_ISTA = 0x7F;
    udelay(10);
    // r768 (ФИКС r764-бага): TX_EN вернуть ОБРАТНО! Выше (по рекомендации
    // внешнего аудита) сняли TX_EN для «перезапуска» TX-блока, но после
    // чистки его НЕ выставили — после первого же ring_reset передатчик
    // оставался выключен навсегда: TXCNT=0, ISTA=0, нет DRQ, DMA стоит,
    // полная тишина (r764-симптом). Включаем до старта DMA.
    I2S_CTRL |= I2S_CTRL_TX_EN;
    __asm volatile("dsb" ::: "memory");
    dma_buf_clear();                      // буфер в тишину
    g_dma_drop_cnt = 0;
    h3_dma_audio_start();                 // DMA играет тишину
    // r756 (SND-1): g_dma_wr = НАЧАЛО буфера, а не dma_played_pairs().
    // h3_dma_audio_start() ставит CH_LLI=&lli[0] → DMA после рестарта начинает
    // С НАЧАЛА буфера (0x4A410000), а не с CUR_SRC, который замерялся ДО
    // останова (в произвольной позиции внутри 32КБ LLI). Раньше g_dma_wr
    // указывал на середину, DMA играл с начала → «фиктивное заполнение»
    // (free=0), CPU2 вставал (st=2), кольцо забивалось, звук молчал в SLT,
    // при выходе из игры — хвост/пульсирующий шум/стык. dmb — чтобы запись
    // g_dma_wr=0 была видна CPU2 до его чтения в flush_max.
    g_dma_wr = 0;
    __asm volatile("dmb sy" ::: "memory");
    // r761: зафиксировать позицию DMA для CPU2 (.coherent) после рестарта.
    dma_curpos_update();
    // r766 (D1): сброс счётчика пакетов DMA и позиции в ноль при сбросе.
    // Без этого g_pkg_cnt (и, как следствие, g_dma_curpos из ISR) не
    // обнулялся между играми — следующая игра стартовала с «хвоста» позиции
    // (мусор/скачки при перезаходах).
    h3_dma_audio_pkg_reset();
    g_dma_curpos = 0;
    __asm volatile("dmb sy" ::: "memory");
    // r748: ВОЗВРАТ CPU2 В РАБОТУ — он должен сразу выпивать кольцо
    // (меню/клик/тон/тест/игра — всё через кольцо).
    if (g_audio_state) {
        i2s_audio_cmd(AUDIO_CMD_RESUME);
    }
}

// ---- CPU2 (audio_core.c): перенос кольца → DMA-буфер ----
// Громкость уже применена в push_sample (в кольце лежат готовые пары).
// Порция: 24 пары (как раньше). Если кольцо пусто — пишем тишину в буфер,
// чтобы DMA не играл старое (непрерывный поток 48к).
#define I2S_FLUSH_BURST 24
int i2s_flush_max(int max_pairs) {
    if (!g_i2s_ready) return 0;
    if (max_pairs > I2S_FLUSH_BURST) max_pairs = I2S_FLUSH_BURST;
    // r760: НЕ сбрасывать I2S_ISTA здесь! r759 ставил запись в каждый вход
    // flush_max — при затыке (кольцо полно, буфер полон) flush_max вызывается
    // МИЛЛИОНЫ раз/сек, и миллионы MMIO-записей в I2S по AHB (общая шина с
    // DMA) давали «трактор», писк ~2.5кГц, треск в тишине, хвосты. Сброс ISTA
    // делается только в i2s_init и i2s_ring_reset (редко, дёшево).
    g_cpu2_stage = 2;          // внутри flush_max (диагностика)
    g_flush_enter++;           // вход в flush_max
    int n = 0;
    while (n < max_pairs) {
        if (dma_free_pairs() == 0) {
            g_dma_drop_cnt++;   // CPU2 быстрее DMA (буфер полон)
            g_flush_skip_full++;
            break;
        }
        if ((uint32_t)(g_ring_wr - g_ring_rd) > 0) {
            uint32_t r = g_ring_rd & (AUDIO_RING_SIZE - 1);
            int32_t L = g_ring_l[r];
            int32_t R = g_ring_r[r];
            // Обновляем hold — следующий голод затухнет от нового уровня.
            hold_l = (int16_t)L; hold_r = (int16_t)R;
            fade_left = I2S_HOLD_FADE_PAIRS;
            // r755: fade-in/атака (как в r735): первый ГРОМКИЙ сэмпл после
            // тишины нарастает от 0 — нет щелчка на входе/после паузы.
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
            dma_write_pair(g_dma_wr, (int16_t)L, (int16_t)R);
            __asm volatile("dmb sy" ::: "memory");
            g_dma_wr++;
            g_flush_written++;   // реально записано пар в DMA-буфер
            ring_rd_advance();
        } else {
            // Кольцо пусто (пауза между пачками): плавно гасим уровень до 0.
            // Резкий break/0 давали «помехи на тишине» (ступенька на стыке).
            if (fade_left > 0) {
                fade_left--;
                int32_t l = (int32_t)hold_l * fade_left / I2S_HOLD_FADE_PAIRS;
                int32_t r = (int32_t)hold_r * fade_left / I2S_HOLD_FADE_PAIRS;
                dma_write_pair(g_dma_wr, (int16_t)l, (int16_t)r);
            } else {
                dma_write_pair(g_dma_wr, 0, 0);
            }
            __asm volatile("dmb sy" ::: "memory");
            g_dma_wr++;
        }
        n++;
    }
    g_flush_exit++;            // выход из flush_max (диагностика)
    g_cpu2_stage = 0;          // вне flush_max
    return n;
}
void i2s_flush(void) { i2s_flush_max(24); }

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

    // TX FIFO Empty DRQ Enable (I2S_INT bit7): разрешаем DMA-запросы в FIFO.
    // r759 (ЭТАЛОН linux sun8i-h3-i2s): TXTL (FCTL[18:12]) оставляем дефолтным
    // 0x40=64 (linux его не трогает). Даташит: «IRQ/DRQ Generated when
    // WLEVEL ≤ TXTL» — при TXTL=64 DMA-запрос держится почти всегда (FIFO
    // считается «пустым», пока в нём ≤64 слов), подлив непрерывный.
    // Раньше TXTL=16: DRQ молчал при 17..64 словах в FIFO → подлив порциями,
    // FIFO успевал уйти в 0 → underrun (ISTA=0x40) → «тиканье» и затыки.
    I2S_FIFO_CTL = (I2S_FIFO_CTL & ~((0x7Fu) << 12)) | (0x40u << 12);   // TXTL=64 (дефолт)
    I2S_INT = (1u << 7);   // TX_DRQ=1

    I2S_CTRL = I2S_CTRL_BCLK_OUT | I2S_CTRL_LRCK_OUT | (1u << 4)
             | I2S_CTRL_TX_EN | I2S_CTRL_SDO_EN0 | I2S_CTRL_GL_EN;
    udelay(1000);

    // DMA: циклически читает .dma_buf → I2S0_TX_FIFO. Стартуем сразу (буфер
    // пуст = тишина), CPU2 начнёт наполнять после подъёма. Канал 0 свободен.
    dma_buf_clear();
    g_dma_wr = 0;
    uint32_t buf = (uint32_t)(uintptr_t)_dma_buf_start;
    if (h3_dma_audio_init(0, buf, DMA_BUF_SIZE_BYTES) != 0) {
        printf("I2S: DMA init FAILED — звук отключён\n");
        return -1;
    }
    // r761: зафиксировать стартовую позицию DMA для CPU2 (.coherent).
    dma_curpos_update();
    // r766 (D1): сброс счётчика пакетов DMA и позиции при инициализации —
    // чтобы первый запуск после boot не начинался с «хвоста» позиции.
    h3_dma_audio_pkg_reset();
    g_dma_curpos = 0;
    __asm volatile("dmb sy" ::: "memory");
    // r764: регистрация ISR аудио-DMA (INTID 114) в GIC. Сам GIC инициализирует
    // audio_core (CPU2) — но ISR можно зарегистрировать и здесь (таблица в
    // .data, ядро Secure, видно обоим). Регистрируем на CPU2 в audio_core,
    // здесь — страховка, если таблица gic уже есть.
    gic_register_isr(h3_dma_audio_intid(), i2s_dma_isr);

    g_i2s_ready = 1;
    printf("I2S: ready (48000 Hz, DMA + CPU2, vol=%d%%)\n", g_volume_pct);
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

void i2s_test_tone(int freq, int msec) {
    if (!g_i2s_ready) return;
    if (freq < 20) freq = 20;
    if (msec <= 0) msec = 100;

    int was_muted = g_muted;
    i2s_ring_reset();        // r746: очистить очередь DMA-буфера/кольца, чтобы
                             // тон шёл СРАЗУ, а не после ~170 мс тишины (размаз)
    if (was_muted) i2s_mute(0);   // после ring_reset mute=1 — снять
    uint32_t step = (uint32_t)(((uint64_t)freq << 16) / 48000u);
    uint32_t ph = 0;
    int total = 48000 * msec / 1000;
    int d = 0;
    uint32_t t0 = 0;
    while (d < total) {
        int batch = total - d;
        if (batch > 800) batch = 800;
        for (int p = 0; p < batch; p++, d++) {
            int32_t s = (int32_t)sin_tab[(ph >> 8) & 0xFF] / 2;
            ph += step;
            int tail = total - d;
            if (tail <= 32)
                s = (int32_t)((int64_t)s * tail / 32);
            i2s_push_sample((int16_t)s, (int16_t)s);
        }
        // Кадровая пауза — CPU2/DMA выведут накопленное.
        if (!t0) t0 = h3_hs_timer_lo_us();
        else {
            uint32_t el = (uint32_t)(h3_hs_timer_lo_us() - t0);
            if (el < 16667u) udelay(16667u - el);
            t0 = h3_hs_timer_lo_us();
        }
    }
    udelay(30000);
    if (was_muted) i2s_mute(1);
}

void i2s_click(void) {
    if (!g_i2s_ready) return;
    int was_muted = g_muted;
    // r746: очистить очередь ДО клика — иначе клик слышится через ~170 мс
    // задержки DMA-буфера («размазанные клики»). ring_reset: стоп DMA +
    // flush FIFO + clear буфера + рестарт (тишина), позиции выровнены —
    // клик прозвучит сразу и чётко.
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

void i2s_tone_burst_test(int freq, int pairs_per_frame, int frames) {
    if (!g_i2s_ready) return;
    if (freq < 20) freq = 20;
    if (pairs_per_frame < 16) pairs_per_frame = 16;
    if (frames <= 0) frames = 60;

    i2s_ring_reset();
    i2s_drop_cnt_reset();
    if (g_muted) i2s_mute(0);

    const uint32_t step = (uint32_t)(((uint64_t)freq << 16) / 48000u);
    uint32_t ph = 0;
    for (int f = 0; f < frames; f++) {
        for (int p = 0; p < pairs_per_frame; p++) {
            int32_t s = (int32_t)sin_tab[(ph >> 8) & 0xFF] / 2;
            ph += step;
            i2s_push_sample((int16_t)s, (int16_t)s);
        }
        udelay(1667);
    }
    udelay(30000);
    printf("I2S layer test: pairs/frame=%d frames=%d dropped=%lu\n",
           pairs_per_frame, frames, (unsigned long)i2s_drop_cnt());
    i2s_ring_reset();
}

// ---- Диагностика (SLT-тест) ----
// ВНИМАНИЕ (r754): эти геттеры писались ДО внедрения DMA (r740), когда CPU2
// лил прямо в TX FIFO, и после перехода на DMA остались ЗАГЛУШКАМИ — в SLT
// уходили нули, отладка врала. Теперь возвращаем ЖИВЫЕ счётчики/стадии
// (объявлены выше, пишутся в flush_max на CPU2).

// Позиция DMA и заполненность DMA-буфера — для ДИАГНОСТИКИ (core0, вне
// горячего пути CPU2). Читаем ЖИВОЙ регистр CH_CUR_SRC напрямую, чтобы SLT
// показывал реальное движение DMA, а не замороженную g_dma_curpos.
void i2s_dma_diag_get(uint32_t* played, uint32_t* free_pairs) {
    // r767 (D8): позиция — из g_dma_curpos (обновляет ISR по счётчику PKG),
    // НЕ из MMIO CH_CUR_SRC: во время активной передачи чтение CUR_SRC может
    // вернуть мусор (конвейер шины). Так SLT видит ту же позицию, что CPU2.
    uint32_t cur = g_dma_curpos;
    if (played)     *played    = cur;
    if (free_pairs) *free_pairs = (DMA_BUF_PAIRS - 1) - ((g_dma_wr - cur) & (DMA_BUF_PAIRS - 1));
}
void i2s_cpu2_diag(uint32_t* flush, uint32_t* written, uint32_t* skipfull, uint32_t* dummy) {
    if (flush)    *flush    = g_flush_enter;
    if (written)  *written  = g_flush_written;
    if (skipfull) *skipfull = g_flush_skip_full;
    if (dummy)    *dummy    = 0;
}
void i2s_cpu2_pairs_written_get(uint32_t* v) { if (v) *v = g_flush_written; }
void i2s_cpu2_stage_get(uint32_t* st, uint32_t* en, uint32_t* ex) {
    if (st) *st = g_cpu2_stage;
    if (en) *en = g_flush_enter;
    if (ex) *ex = g_flush_exit;
}
void i2s_ring_wr_rd_get(uint32_t* w, uint32_t* r) { if(w)*w=g_ring_wr; if(r)*r=g_ring_rd; }
void i2s_flush_diag_get(uint32_t* nempty, uint32_t* nempty_full) {
    // r754: живые индикаторы: nempty = всего входов, nempty_full = выходов по
    // полному DMA-буферу (CPU2 быстрее DMA). Раньше были заглушки.
    if (nempty)      *nempty      = g_flush_enter;
    if (nempty_full) *nempty_full = g_flush_skip_full;
}