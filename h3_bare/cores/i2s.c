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
#include "i2s.h"

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

// r555: заполнение TX FIFO — из I2S/PCM_FSTA (0x18): bit28 TXE (место ≥1 слова),
// bits23:16 TXE_CNT (число СВОБОДНЫХ слов). r588: вынесено НАВЕРХ — используется
// и i2s_flush_max, и i2s_write_pair_direct (прямой вывод тона на паузе CPU2).
#define I2S_FSTA_TXE_CNT(st) (((st) >> 16) & 0xFFu)   // свободных слов
#define I2S_TXE_MIN 8                                 // стоп, если свободно меньше 4 пар
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

// r558: DC-блокер (HPF) — убирает постоянную составляющую/перепады фона от
// blip. r560 поднял срез до ~480 Гц (DC_SHIFT=4) — но на стенде звук стал
// «режущим» (бедный бас) по сравнению с оригиналом (retrogpSP без такого
// фильтра). r572: возвращён DC_SHIFT=5 (~240 Гц) — низ восстановлен.
// r585 (Ф1): dc_l/dc_r — в .coherent: их пишет core0 (i2s_push_sample) и
// обнуляет CPU2 (i2s_audio_poll_cmd при RING_RESET); без uncached core0
// читал бы своё старое значение из write-back кэша после сброса.
static int32_t dc_l __attribute__((section(".coherent"), aligned(4))) = 0;
static int32_t dc_r __attribute__((section(".coherent"), aligned(4))) = 0;
// r590: DC_SHIFT — переменная (не #define): для разных систем свой срез.
//   GBA (пачки с паузами тишины) — 5 (~240 Гц): медленный блокер (6=120 Гц)
//   давал щелчок/посторонний шум на каждом стыке «тишина→пачка» (восстановление
//   ~1.3 мс) → «слабый/неразборчивый».
//   Lynx (непрерывный поток) — 6 (~120 Гц): по стенду ровнее, ближе к оригиналу.
// Задают хосты через i2s_dc_shift_set(); только core0 пишет (push_sample),
// в .coherent не нужно.
static int g_dc_shift = 5;   // по умолчанию ~240 Гц (безопасно для всех)

// r635: hold/fade из i2s_flush_max вынесены в файловые статики (сбрасываются в
// i2s_ring_reset/AUDIO_CMD_RING_RESET) — иначе при смене игры flush_max «затухал»
// от старого hold-пика → рваный старт/накопительный треск.
static int16_t hold_l = 0, hold_r = 0;   // последняя выведенная пара
static int fade_left = 0;                // пар до конца затухания

void i2s_dc_shift_set(int shift) {
    if (shift < 1) shift = 1;
    if (shift > 12) shift = 12;
    g_dc_shift = shift;
    dc_l = 0; dc_r = 0;   // сброс истории — иначе блокер «помнит» старый срез
}

// ---- кольцо потока эмулятора (объявлено раньше геттеров — их использует) ----
#define AUDIO_RING_SIZE 8192
// r584 (Ф0): кольцо и индексы — в .coherent (uncached, 1МБ область). Продюсер
// (core0, i2s_push_sample) и потребитель (core2, audio_core_main) — РАЗНЫЕ
// ядра, у core0 D-cache write-back: без uncached CPU2 читал бы stale-линии.
// Секция .coherent обнуляется стартом (startup.S) и помечена non-cacheable
// через mmu_mark_uncached (main.c). Размер: 2×8192×2 + 8 = 32776 б.
static int16_t g_ring_l[AUDIO_RING_SIZE] __attribute__((section(".coherent"), aligned(8)));
static int16_t g_ring_r[AUDIO_RING_SIZE] __attribute__((section(".coherent"), aligned(8)));
static volatile uint32_t g_ring_wr __attribute__((section(".coherent"), aligned(4))) = 0;
static volatile uint32_t g_ring_rd __attribute__((section(".coherent"), aligned(4))) = 0;

// ---- Почта core0↔CPU2 (аудио-ядро) в .coherent (uncached) ----
// core0 (эмулятор) пишет команды и читает состояние; CPU2 (долив) обрабатывает
// команды и тикает heartbeat. CPU2 с выключенным MMU/кэшами читает/пишет
// напрямую в DRAM; core0 — через uncached-маппинг .coherent (оба ядра видят
// одну физическую память, кэш-когерентность обеспечена свойством секции).
// (enum команд — в i2s.h: AUDIO_CMD_NONE/RING_RESET/PAUSE/RESUME.)
static volatile uint32_t g_audio_state  __attribute__((section(".coherent"), aligned(4))) = 0; // 1 = CPU2 в цикле
static volatile uint32_t g_audio_cmd    __attribute__((section(".coherent"), aligned(4))) = 0;
static volatile uint32_t g_audio_beat   __attribute__((section(".coherent"), aligned(4))) = 0; // инкремент CPU2 (heartbeat)
static volatile uint32_t g_audio_pairs  __attribute__((section(".coherent"), aligned(4))) = 0; // пар вывел CPU2 (диагностика)
static volatile uint32_t g_audio_ring   __attribute__((section(".coherent"), aligned(4))) = 0; // уровень кольца от CPU2
static volatile uint32_t g_audio_paused_f __attribute__((section(".coherent"), aligned(4))) = 0; // 1 = CPU2 подтвердил паузу

// core0: ядро 2 живо? (heartbeat не замер — сравнивается в emu_throttle)
int i2s_audio_core_active(void) { return g_audio_state ? 1 : 0; }
uint32_t i2s_audio_beat(void)   { return g_audio_beat; }
uint32_t i2s_audio_pairs(void)  { return g_audio_pairs; }
uint32_t i2s_audio_ring(void)   { return g_audio_ring; }

// CPU2 (audio_core.c): пометить себя активным/неактивным, тикать heartbeat,
// класть диагностику (кольцо/пары) — сеттеры, т.к. поля static в i2s.c.
void i2s_audio_set_state(int on) { g_audio_state = on ? 1 : 0; }
void i2s_audio_set_beat(uint32_t b) { g_audio_beat = b; }
void i2s_audio_set_ring(uint32_t r) { g_audio_ring = r; }
void i2s_audio_set_pairs(uint32_t p) { g_audio_pairs = p; }

// core0: послать команду CPU2. Неблокирующая (диагностика), heartbeat-контроль
// в emu_throttle решает, оффлоадить ли звук.
void i2s_audio_cmd(uint32_t cmd) {
    if (!g_audio_state) return;   // ядро не поднято — нечего слать
    g_audio_cmd = cmd;
    // Для PAUSE ждём, пока CPU2 подтвердит (пауза) — иначе core0 начнёт
    // писать в FIFO, а CPU2 ещё не встал → двойной вывод.
    if (cmd == AUDIO_CMD_PAUSE) {
        uint32_t t = 0;
        while (!g_audio_paused_f && ++t < 100000) {}
    }
}

int i2s_audio_paused(void) { return g_audio_paused_f ? 1 : 0; }

// CPU2: обработать одну команду (вызывается в цикле аудио-ядра).
void i2s_audio_poll_cmd(void) {
    uint32_t cmd = g_audio_cmd;
    if (cmd == AUDIO_CMD_RING_RESET) {
        g_ring_wr = 0; g_ring_rd = 0; dc_l = 0; dc_r = 0;
        hold_l = 0; hold_r = 0; fade_left = 0;   // r635: сброс hold/fade (накопительный треск)
        g_audio_cmd = 0;
    } else if (cmd == AUDIO_CMD_PAUSE) {
        g_audio_paused_f = 1;   // подтвердить паузу
        g_audio_cmd = 0;
    } else if (cmd == AUDIO_CMD_RESUME) {
        g_audio_paused_f = 0;
        g_audio_cmd = 0;
    } else if (cmd != 0) {
        g_audio_cmd = 0;   // неизвестная — сбросить
    }
}

// r639: пакетная запись. v (масштаб громкости) считается ОДИН раз вне цикла —
// деление на 100 на каждый семпл было узким местом real-time (кряхтение на 48к).
// Клик/тон вызывают быстрый вариант с заранее посчитанным v.
static void i2s_write_pair_direct_v(int16_t l, int16_t r, int32_t v) {
    if (!g_i2s_ready) return;
    int32_t L = (int32_t)l * v / 32;
    int32_t R = (int32_t)r * v / 32;
    if (L > 32767) L = 32767;
    if (L < -32768) L = -32768;
    if (R > 32767) R = 32767;
    if (R < -32768) R = -32768;
    I2S_FIFO_TX = (uint32_t)(uint16_t)L << 16;
    I2S_FIFO_TX = (uint32_t)(uint16_t)R << 16;
}

void i2s_write_pair_direct(int16_t l, int16_t r) {
    int32_t v = (g_volume_pct * 32) / 100;
    i2s_write_pair_direct_v(l, r, v);
}

// Ждать, пока в TX FIFO освободится место для минимум ОДНОЙ ПОЛНОЙ пары
// (2 слова = I2S_TXE_MIN). Контроллер выводит с темпом 48 кГц, поэтому
// спин даёт «железный» темп записи без потери пар. Таймаут страхует от
// зависшего контроллера. Возвращает 1, если место есть.
static int i2s_wait_tx_room(void) {
    volatile uint32_t t = 0;
    while (I2S_FSTA_TXE_CNT(I2S_FIFO_STA) < I2S_TXE_MIN && ++t < 200000u) {}
    return I2S_FSTA_TXE_CNT(I2S_FIFO_STA) >= I2S_TXE_MIN;
}

void i2s_volume(int p) {
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    g_volume_pct = p;
    printf("audio: volume=%d%%\n", p);
}
int i2s_volume_pct(void) { return g_volume_pct; }
int i2s_ready(void) { return g_i2s_ready; }
void i2s_ring_reset(void) {
    // r585 (Ф1): если долив делает CPU2 — сброс кольца/DC через почту,
    // чтобы CPU2 не читал обнуляемое кольцо одновременно с нами (гонка).
    // Если CPU2 не поднят — обнуляем напрямую (прежнее поведение).
    if (g_audio_state) {
        i2s_audio_cmd(AUDIO_CMD_RING_RESET);
    } else {
        g_ring_wr = 0; g_ring_rd = 0; dc_l = 0; dc_r = 0;
        hold_l = 0; hold_r = 0; fade_left = 0;   // r635: сброс hold/fade (fallback)
    }
}
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
    // ВАЖНО: не путать с предыдущим вариантом PA10-12. PA11/12 больше НЕ
    // заняты Sega-падом — с r614 пад на PG9/PG8 (порт G); PA10 — SD усилка.
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
    I2S_TX_CNT = 0;   // r553: сброс счётчика TX FIFO (как Linux sun4i_i2s_start_playback)
                      // — иначе TX_CNT не отражает реальный уровень и pump считает FIFO «полным»

    // 48к: WSS=7 (32-бит слот), SR=3 (16-бит), LRCKPER=64, BCLK 3.072M
    I2S_FMT0 = (7u << 0) | (3u << 4) | (0u << 7) | I2S_FMT0_LRCKPER(64) | (0u << 19);
    I2S_FMT1 = 0;
    // r556: BCLK val 5 → 3. На стенде txcnt рос ~23.6 к/с (вдвое медленнее
    // 48 к) — H3 похоже использует sun4i-шкалу делителей (val=5 → div=16 →
    // BCLK 1.536 М → LRCK 24 кГц), а не sun8i. val=3 → div=8 → BCLK 3.072 М
    // → настоящие 48 кГц. Проверить тест-тоном 1000 Гц (должен звенеть ровно).
    I2S_CLK_DIV = I2S_CLK_MCLK_EN | I2S_CLK_BCLK(3) | I2S_CLK_MCLK(2);

    I2S_TX_CMAP = 0x76543210;
    I2S_TX_CSEL = (3u << 4) | I2S_TX_CHAN_OFF(1) | 1;
    I2S_CHAN_CFG = I2S_CHAN_TXSLOT(2) | I2S_CHAN_RXSLOT(2);

    I2S_CTRL = I2S_CTRL_BCLK_OUT | I2S_CTRL_LRCK_OUT | (1u << 4)
             | I2S_CTRL_TX_EN | I2S_CTRL_SDO_EN0 | I2S_CTRL_GL_EN;
    udelay(1000);

    g_i2s_ready = 1;
    printf("I2S: ready (48000 Hz, vol=%d%%)\n", g_volume_pct);
    return 0;
}

// ---- приём/перенос потока эмулятора ----
static inline uint32_t ring_count(void) { return (uint32_t)(g_ring_wr - g_ring_rd); }
int i2s_ring_level(void) { return (int)ring_count(); }   // r522: для диагностики

// r557: срезать накопленную историю — оставить только keep_pairs самых
// СВЕЖИХ (двигаем rd к wr, самые старые пар дропаются). Нужно, когда темп
// железа чуть ниже производства и кольцо «застряло» полным (задержка ~170мс
// + дропы) — мгновенно убирает отставание, дальше период стабилизируется.
void i2s_ring_trim(uint32_t keep_pairs) {
    if (keep_pairs >= AUDIO_RING_SIZE) keep_pairs = AUDIO_RING_SIZE - 1;
    if (ring_count() > keep_pairs) {
        g_ring_rd = g_ring_wr - keep_pairs;
        // r575 (Д-4): дропнутые старые пары не должны влиять на DC-оценку —
        // иначе после среза блокер «помнит» уровень сброшенной истории и
        // выдаёт короткий щелчок/перепад. Сброс = мгновенный, как в
        // i2s_ring_reset().
        dc_l = 0;
        dc_r = 0;
    }
}

// Приём сэмпла: НЕБЛОКИРУЮЩИЙ. r558: DC-блокер (~240 Гц) на сыром сигнале,
// затем громкость и жёсткий кламп s16. FIR/лимитер не возвращаем.
void i2s_push_sample(int16_t left, int16_t right) {
    if (!g_i2s_ready) return;
    if (g_muted) { g_muted = 0; H3_PIO_PORTA->DAT |= (1u << SD_PIN); }
    if (ring_count() >= AUDIO_RING_SIZE) return;   // drop-on-full

    int32_t fl = (int32_t)left  - dc_l;  dc_l += fl >> g_dc_shift;
    int32_t fr = (int32_t)right - dc_r;  dc_r += fr >> g_dc_shift;

    int32_t v = (g_volume_pct * 32) / 100;
    int32_t L = fl * v / 32;
    int32_t R = fr * v / 32;
    if (L > 32767) L = 32767;
    if (L < -32768) L = -32768;
    if (R > 32767) R = 32767;
    if (R < -32768) R = -32768;

    uint32_t w = g_ring_wr & (AUDIO_RING_SIZE - 1);
    g_ring_l[w] = (int16_t)L;
    g_ring_r[w] = (int16_t)R;
    // r593: БАРЬЕР перед публикацией индекса. Кольцо в .coherent (uncached),
    // но на ARM две обычные записи к Normal-памяти могут переупорядочиться:
    // g_ring_wr++ мог дойти до DRAM РАНЬШЕ сэмплов → CPU2 читал по новому
    // индексу ещё НЕ записанные данные = «ритмичные громкие щелчки с
    // динамикой игры». dmb упорядочивает: сэмплы видны раньше индекса.
    __asm volatile("dmb sy" ::: "memory");
    g_ring_wr++;
}

// Ритм: 21 мкс на пару ≈ 20.8 мкс = 48000 Гц
// (таймер 24 МГц, lo_us() даёт настоящие микросекунды: 48кГц → пара за 20.8 мкс).
#define I2S_PACE_UNITS 21

// Порция неблокирующего долива (~0.5 мс звука): CPU не ждёт темп.
#define I2S_FLUSH_BURST 24

// Перенос кольцо → FIFO. НЕБЛОКИРУЮЩИЙ — пишем пары, пока в TX FIFO есть
// свободные слова (контроллер играет сам с темпом 48 кГц). Больше никакого
// busy-wait по 21 мкс/пара: CPU освобождается, звук «fire-and-forget».
// r591 (Д-56): при пустом кольце — HOLD последней пары (не тишина 0).
// Раньше писали 0: на микроголоде (кадр эмуляции чуть дольше периода)
// ступенька в 0 на непрерывной мелодии давала «треск как песок» у Lynx.
// r593 (Д-58): HOLD заменён на ЗАТУХАНИЕ к 0 (~1 мс). Держать пик пачки
// до следующей пачки = «ритмичные громкие щелчки с динамикой игры» (при
// отставании эмуляции кольцо пустеет между пачками; на стыке скачок от
// удержанного пика к новому сэмплу). Затухание убирает и громкий hold-тон,
// и стыковочный скачок; «песок» (скачок в 0) не возвращается.
#define I2S_HOLD_FADE_PAIRS 48   // ~1 мс затухания при 48 кГц

// r635: hold/fade — файловые статики (см. объявление выше): сбрасываются при
// смене системы/перезаходе (i2s_ring_reset/AUDIO_CMD_RING_RESET).
void i2s_flush_max(int max_pairs) {
    if (!g_i2s_ready) return;
    if (max_pairs > I2S_FLUSH_BURST) max_pairs = I2S_FLUSH_BURST;
    int n = 0;
    while (n < max_pairs) {
        if (I2S_FSTA_TXE_CNT(I2S_FIFO_STA) < I2S_TXE_MIN) break;   // мало места — вернёмся позже
        if (ring_count() > 0) {
            uint32_t r = g_ring_rd & (AUDIO_RING_SIZE - 1);
            hold_l = g_ring_l[r];
            hold_r = g_ring_r[r];
            fade_left = I2S_HOLD_FADE_PAIRS;   // следующий голод начнёт затухать от нового уровня
            I2S_FIFO_TX = (uint32_t)(uint16_t)hold_l << 16;
            I2S_FIFO_TX = (uint32_t)(uint16_t)hold_r << 16;
            g_ring_rd++;
        } else {
            // кольцо пусто (пауза между пачками): плавно гасим уровень до 0.
            // Держать пик (старый HOLD) давало громкие щелчки на стыке пачек.
            if (fade_left > 0) {
                fade_left--;
                int32_t l = (int32_t)hold_l * fade_left / I2S_HOLD_FADE_PAIRS;
                int32_t r = (int32_t)hold_r * fade_left / I2S_HOLD_FADE_PAIRS;
                I2S_FIFO_TX = (uint32_t)(uint16_t)l << 16;
                I2S_FIFO_TX = (uint32_t)(uint16_t)r << 16;
            } else {
                I2S_FIFO_TX = 0;
                I2S_FIFO_TX = 0;
            }
        }
        n++;
    }
}

void i2s_flush(void) { i2s_flush_max(24); }

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

// Общий вход для тона/клика: изолировать TX FIFO от CPU2-долива на время
// вывода, обеспечить единственного писателя (core0). Возвращает 1, если
// можно выводить.
static int i2s_menu_begin(void) {
    if (!g_i2s_ready) return 0;
    // CPU2 (долив) на паузу — ждём подтверждения; пустое кольцо и DC.
    // Если CPU2 не поднят — i2s_ring_reset() сбросит напрямую.
    if (i2s_audio_core_active())
        i2s_audio_cmd(AUDIO_CMD_PAUSE);
    i2s_ring_reset();
    return 1;
}

static void i2s_menu_end(void) {
    // Возвращаем долив (если CPU2 жив) — FIFO снова обслуживает кольцо.
    if (i2s_audio_core_active())
        i2s_audio_cmd(AUDIO_CMD_RESUME);
}

// Включить усилитель на время вывода, восстановить прежнее состояние в конце.
static int i2s_mute_push(void) {
    int was = g_muted;
    if (was) { g_muted = 0; H3_PIO_PORTA->DAT |= (1u << SD_PIN); }
    return was;
}
static void i2s_mute_pop(int was) {
    if (was) { g_muted = 1; H3_PIO_PORTA->DAT &= ~(1u << SD_PIN); }
}

void i2s_test_tone(int freq, int msec) {
    if (!i2s_menu_begin()) return;
    if (freq < 20) freq = 20;
    if (msec <= 0) msec = 100;

    uint32_t step = (uint32_t)(((uint64_t)freq << 16) / 48000u);
    uint32_t ph = 0;
    int total = 48000 * msec / 1000;
    int was = i2s_mute_push();
    int32_t v = (g_volume_pct * 32) / 100;   // r639: один раз, вне цикла

    // r639: пакетная запись. Читаем свободные слова TX FIFO, вливаем столько пар,
    // сколько помещается, за один проход — без посемпльного ожидания (кряхтение/underrun).
    int d = 0;
    while (d < total) {
        uint32_t free_words = I2S_FSTA_TXE_CNT(I2S_FIFO_STA);
        if (free_words < I2S_TXE_MIN) {        // FIFO почти полон — ждём место
            if (!i2s_wait_tx_room()) break;
            free_words = I2S_FSTA_TXE_CNT(I2S_FIFO_STA);
        }
        int free_pairs = (int)(free_words / 2);
        int batch = free_pairs;
        if (d + batch > total) batch = total - d;
        for (int p = 0; p < batch; p++, d++) {
            int32_t s = (int32_t)sin_tab[(ph >> 8) & 0xFF] / 2;   // 50% — тон не оглушает
            ph += step;
            int tail = total - d;
            if (tail <= 32)
                s = (int32_t)((int64_t)s * tail / 32);   // fade-out — краевого щелчка нет
            i2s_write_pair_direct_v((int16_t)s, (int16_t)s, v);
        }
    }

    i2s_mute_pop(was);
    i2s_menu_end();
}

// Короткий тихий щелчок при навигации в меню. 1.5 кГц = ровно 32 пары/период,
// берём 8 полных периодов = 256 пар (~5.3 мс) — волна заканчивается строго в 0,
// без щелчка-обрыва. Амплитуда ~25% (sin_tab кратен 4 → >>2 чистое деление).
// r638: длина из 240 (5.0 мс, нецелое число периодов) → 256 (8 полных периодов).
void i2s_click(void) {
    if (!i2s_menu_begin()) return;
    uint32_t step = (uint32_t)(((uint64_t)1500u << 16) / 48000u);
    uint32_t ph = 0;
    int total = 256;   // 8 полных периодов 1500 Гц (48000/1500=32 ×8)
    int was = i2s_mute_push();
    int32_t v = (g_volume_pct * 32) / 100;   // r639: один раз, вне цикла

    // r639: пакетная запись (как в i2s_test_tone).
    int d = 0;
    while (d < total) {
        uint32_t free_words = I2S_FSTA_TXE_CNT(I2S_FIFO_STA);
        if (free_words < I2S_TXE_MIN) {
            if (!i2s_wait_tx_room()) break;
            free_words = I2S_FSTA_TXE_CNT(I2S_FIFO_STA);
        }
        int batch = (int)(free_words / 2);
        if (d + batch > total) batch = total - d;
        for (int p = 0; p < batch; p++, d++) {
            int32_t s = (int32_t)sin_tab[(ph >> 8) & 0xFF] >> 2;   // ~25%
            ph += step;
            i2s_write_pair_direct_v((int16_t)s, (int16_t)s, v);
        }
    }

    i2s_mute_pop(was);
    i2s_menu_end();
}
