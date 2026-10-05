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
#include "led.h"
#include "emu.h"

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
// r715: ВАЖНО — поля, которые в горячем пути пишут РАЗНЫЕ ядра (core0 —
// g_ring_wr/dc_l/dc_r; CPU2 — g_ring_rd/hold_l/hold_r/fade_left/beat), разнесены
// по ОТДЕЛЬНЫМ 64-байтным кэш-линиям (aligned(64)). Причина: wr, rd, dc, hold,
// fade, beat лежали в одной 32-байтной uncached-линии .coherent; два ядра
// долбили её десятками тысяч раз/с (core0 → wr 48к/с, CPU2 → rd/beat) → гонка
// за захват линии на шине H3 → CPU2 замирал (не аборт — потому F1..F6 не
// печатались). Теперь каждое поле — в своей линии, конкуренции нет.
// dc_l/dc_r — пишет core0 (push_sample) и обнуляет CPU2 (RING_RESET) — держим
// их вместе, но в ОТДЕЛЬНОЙ линии от wr/rd (переписываются редко, не в потоке).
static int32_t dc_l __attribute__((section(".coherent"), aligned(64))) = 0;
static int32_t dc_r __attribute__((section(".coherent"), aligned(64))) = 0;
// r590: DC_SHIFT — переменная (не #define): для разных систем свой срез.
//   GBA (пачки с паузами тишины) — 6 (~120 Гц): r591 вернул низ после того, как
//   5 (~240 Гц) резал бас/давал «провал»; щелчки на стыках пачек уходят
//   мягким лимитером ниже. (Хосты GBA вызывают i2s_dc_shift_set(6) — см. gba_host.c.)
//   Lynx (непрерывный поток) — 6 (~120 Гц): по стенду ровнее, ближе к оригиналу.
// Задают хосты через i2s_dc_shift_set(); только core0 пишет (push_sample),
// в .coherent не нужно.
static int g_dc_shift = 5;   // по умолчанию ~240 Гц (безопасно для всех)

// r635: hold/fade из i2s_flush_max вынесены в файловые статики (сбрасываются в
// i2s_ring_reset/AUDIO_CMD_RING_RESET) — иначе при смене игры flush_max «затухал»
// от старого hold-пика → рваный старт/накопительный треск.
// r703: hold/fade в .coherent (uncached) — их пишет CPU2 (i2s_flush_max) и core0
// (i2s_ring_reset / i2s_audio_poll_cmd). Обычная static-переменная кэшировалась
// бы в D-cache core0: после RING_RESET сброс не доходил до DRAM, и CPU2 при
// RESUME читал старый пик удержания/счётчик затухания → «треск на старте» игры.
// r715: hold/fade — свои отдельные 64-байтные линии (пишет CPU2 в flush_max,
// обнуляет core0 в ring_reset; не мешать с wr/rd/beat).
static int16_t hold_l __attribute__((section(".coherent"), aligned(64))) = 0;
static int16_t hold_r __attribute__((section(".coherent"), aligned(64))) = 0;
static int fade_left __attribute__((section(".coherent"), aligned(64))) = 0;

void i2s_dc_shift_set(int shift) {
    if (shift < 1) shift = 1;
    if (shift > 12) shift = 12;
    g_dc_shift = shift;
    dc_l = 0; dc_r = 0;   // сброс истории — иначе блокер «помнит» старый срез
}

// ---- кольцо потока эмулятора (объявлено раньше геттеров — их использует) ----
#define AUDIO_RING_SIZE 8192
// D-audio: мягкий целевой уровень кольца (пар), до которого продюсер срезает
// при переполнении (i2s_push_sample → i2s_ring_trim при достижении HIGH).
// 1600 пар ≈ 33 мс по 48 кГц — больше, чем способен накопить один кадр
// (~800), поэтому срез срабатывает только на устойчивый дрифт производства
// выше железа, а не вырезает пачку целиком.
#define AUDIO_RING_TRIM_HIGH 7000u
#define AUDIO_RING_TRIM_TARGET 1600u
// r584 (Ф0): кольцо и индексы — в .coherent (uncached, 1МБ область). Продюсер
// (core0, i2s_push_sample) и потребитель (core2, audio_core_main) — РАЗНЫЕ
// ядра, у core0 D-cache write-back: без uncached CPU2 читал бы stale-линии.
// Секция .coherent обнуляется стартом (startup.S) и помечена non-cacheable
// через mmu_mark_uncached (main.c). Размер: 2×8192×2 + 8 = 32776 б.
// r715: индексы кольца — РАЗНЫЕ 64-байтные линии (wr пишет core0 48к/с, rd —
// CPU2; до r715 в одной линии → гонка за кэш-линию → CPU2 замирал).
static int16_t g_ring_l[AUDIO_RING_SIZE] __attribute__((section(".coherent"), aligned(8)));
static int16_t g_ring_r[AUDIO_RING_SIZE] __attribute__((section(".coherent"), aligned(8)));
static volatile uint32_t g_ring_wr __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_ring_rd __attribute__((section(".coherent"), aligned(64))) = 0;
// W: фаза watermark-предбуферизации. Пишет сброс (core0, i2s_ring_reset) и
// потребитель (CPU2, flush_max) — оба ядра, поэтому в .coherent (uncached).
static volatile uint32_t g_ring_watermark_active __attribute__((section(".coherent"), aligned(4))) = 0;

// ---- Почта core0↔CPU2 (аудио-ядро) в .coherent (uncached) ----
// core0 (эмулятор) пишет команды и читает состояние; CPU2 (долив) обрабатывает
// команды и тикает heartbeat. CPU2 с выключенным MMU/кэшами читает/пишет
// напрямую в DRAM; core0 — через uncached-маппинг .coherent (оба ядра видят
// одну физическую память, кэш-когерентность обеспечена свойством секции).
// (enum команд — в i2s.h: AUDIO_CMD_NONE/RING_RESET/PAUSE/RESUME.)
static volatile uint32_t g_audio_state  __attribute__((section(".coherent"), aligned(64))) = 0; // 1 = CPU2 в цикле
static volatile uint32_t g_audio_cmd    __attribute__((section(".coherent"), aligned(64))) = 0; // команда (0=нет)
static volatile uint32_t g_audio_seq    __attribute__((section(".coherent"), aligned(64))) = 0; // монотон. № команды (core0)
static volatile uint32_t g_audio_ack    __attribute__((section(".coherent"), aligned(64))) = 0; // послед.подтв. seq (CPU2)
// r715: beat (пишет CPU2 в горячем цикле) — отдельная 64-байтная линия.
static volatile uint32_t g_audio_beat   __attribute__((section(".coherent"), aligned(64))) = 0; // инкремент CPU2 (heartbeat, liveness C4)
static volatile uint32_t g_audio_paused_f __attribute__((section(".coherent"), aligned(64))) = 0; // 1 = CPU2 подтвердил паузу

// r722: диагностика (r716-r721: sticky-маркеры в .coherent, SRAM-зеркала
// 0x5000/0x5004 из hot-цикла CPU2, LT-лог) ПОЛНОСТЬЮ УДАЛЕНА. Причина:
// CPU2 писал в произвольные SRAM-адреса 0x5000/0x5004 в каждой итерации —
// это могло пересекаться с зонами U-Boot/почты/флага exc_once (0x18) и
// давать РАНДОМНЫЕ зависания на загрузке/в меню/в эмуляторе. Кольцо и почта
// (i2s.c) вернулись к чистому состоянию; канал .coherent подтверждён живым
// (r720: sram_b растёт синхронно с beat → CPU2 реально доливал все это время).

// core0: ядро 2 живо? (heartbeat не замер — сравнивается в emu_throttle)
int i2s_audio_core_active(void) { return g_audio_state ? 1 : 0; }
uint32_t i2s_audio_beat(void)   { return g_audio_beat; }

// CPU2 (audio_core.c): пометить себя активным/неактивным, тикать heartbeat.
// Сеттеры, т.к. поля static в i2s.c. r705: диагностика (trace/live/pairs/ring)
// удалена — её uncached-записи на каждой итерации долива грузили шину.
void i2s_audio_set_state(int on) { g_audio_state = on ? 1 : 0; }
void i2s_audio_set_beat(uint32_t b) { g_audio_beat = b; }

// N1: протокол почты cmd+ack+seq. Оба ядра читают/пишут однословные поля
// .coherent (uncached): 32-битные store/load на ARMv7 атомарны сами по себе,
// поэтому «послать команду + получить подтверждение» не требует LDREX/STREX —
// хватает монотонного seq, который однозначно отличает новую команду от
// «ещё не дочитанной старой». Это закрывает гонку (раньше двуштапная
// запись g_audio_cmd, потом сброс в 0 могли пересечься с новой командой).
// core0: послать команду CPU2. Для PAUSE/RING_RESET — ждать подтверждения
// (с NOP-backoff, без горячего спина по uncached-флагам). RESUME — неблокирующ.
void i2s_audio_cmd(uint32_t cmd) {
    if (!g_audio_state) return;   // ядро не поднято — нечего слать
    // Новая команда: публикуем СНАЧАЛА cmd, ПОТОМ seq (строго монотонный).
    // Порядок важен: когда CPU2 увидит НОВЫЙ seq, команда в g_audio_cmd уже
    // гарантированно новая (dmb упорядочивает запись cmd до seq). Раньше seq
    // шёл первым, и CPU2 мог прочитать новый seq при ещё СТАРОЙ cmd (=0 от
    // предыдущей обработки) → подтвердить ack, НЕ выполнив команду, а команда
    // навсегда оставалась при seq==ack (poll_cmd возвращает). Это давало
    // «зависание» RING_RESET/PAUSE при входе в эмулятор.
    g_audio_cmd = cmd;
    __asm volatile("dmb sy" ::: "memory");
    uint32_t nseq = g_audio_seq + 1;
    if (nseq == 0) nseq = 1;   // не перечёркиваем во врапе
    g_audio_seq = nseq;
    if (cmd == AUDIO_CMD_PAUSE || cmd == AUDIO_CMD_RING_RESET) {
        // Дождаться, пока CPU2 подтвердит именно этот seq (ack==nseq) —
        // r640: RING_RESET/PAUSE обязаны завершиться до продолжения core0
        // (иначе «хвост старой игры»/двойной вывод). NOP-backoff, чтобы не
        // забивать шину синхронными DRAM-чтениями (uncached).
        for (uint32_t t = 0; t < 100000 && g_audio_ack != nseq; t++) {
#if defined(__GNUC__)
            __asm__ volatile("nop; nop; nop; nop");
#endif
        }
    }
}

int i2s_audio_paused(void) { return g_audio_paused_f ? 1 : 0; }

// CPU2: обработать команду (вызывается в цикле аудио-ядра). Берёт только
// НОВЫЕ команды: seq должен отличаться от последнего исполненного (ack).
void i2s_audio_poll_cmd(void) {
    uint32_t seq = g_audio_seq;
    uint32_t ack = g_audio_ack;
    if (seq == ack) return;              // новой команды нет
    uint32_t cmd = g_audio_cmd;
    if (cmd == AUDIO_CMD_RING_RESET) {
        g_ring_wr = 0; g_ring_rd = 0; dc_l = 0; dc_r = 0;
        hold_l = 0; hold_r = 0; fade_left = 0;   // r635: сброс hold/fade (накопительный треск)
        g_ring_watermark_active = 0;             // W: рестарт watermark (новая игра)
    } else if (cmd == AUDIO_CMD_PAUSE) {
        g_audio_paused_f = 1;   // подтвердить паузу
    } else if (cmd == AUDIO_CMD_RESUME) {
        g_audio_paused_f = 0;
        // остальное — неизвестная команда: игнорируем payload, но seq всё равно
        // подтверждаем, чтобы core0 не завис на ожидании ack.
    }
    __asm volatile("dmb sy" ::: "memory");
    g_audio_cmd = 0;                 // освободить слот
    g_audio_ack = seq;               // подтвердить: команда seq ИСПОЛНЕНА
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
    // r706: аппаратный мьют при 0 громкости — PA10 (SD) в 0, усилитель
    // выключен (тишина без шума/дрейфа). При >0 — PA10 в 1 (если не muted).
    if (g_volume_pct == 0)
        pa_dat_set(SD_PIN, 0);
    else if (!g_muted)
        pa_dat_set(SD_PIN, 1);
    printf("audio: volume=%d%%\n", p);
}
int i2s_volume_pct(void) { return g_volume_pct; }
int i2s_ready(void) { return g_i2s_ready; }
void i2s_ring_reset(void) {
    // Сброс кольца/DC/FIFO. КОРНЕВАЯ ПРАВКА (r702-слой): нельзя писать в
    // аппаратный TX FIFO (I2S_FIFO_CTL bit25, I2S_TX_CNT) конкурентно с
    // CPU2, который в этот же момент доливает → гонка за регистры может
    // оставить контроллер в нерабочем состоянии → ТИШИНА/рваный старт.
    // Старые тест-тон/клик работали потому, что СНАЧАЛА ставят CPU2 на
    // PAUSE (i2s_menu_begin); эмуляторы входят через emu_prepare→
    // i2s_ring_reset при живом CPU2 — и тихо ломали FIFO. Поэтому здесь:
    //   если CPU2 жив — ПАУЗА (CPU2 встаёт, FIFO никто не пишет) →
    //   весь сброс на core0 (кольцо, dc, hold, FIFO, TX_CNT).
    //   RESUME НЕ делаем — решает вызывающий: тест-тон/клик после ring_reset
    //   пишут напрямую в FIFO и обязаны держать CPU2 в паузе до i2s_menu_end;
    //   эмулятор после ring_reset сам возвращает CPU2 в долив (emu_prepare).
    //   Иначе (auto-RESUME) получили бы двойного писателя FIFO на тон/клике.
    if (g_audio_state) {
        i2s_audio_cmd(AUDIO_CMD_PAUSE);      // CPU2 встал, подтвердил ack
    }
    g_ring_wr = 0; g_ring_rd = 0; dc_l = 0; dc_r = 0;
    hold_l = 0; hold_r = 0; fade_left = 0;   // r635: сброс hold/fade
    g_ring_watermark_active = 0;             // W: рестарт watermark (новая игра)
    // r644: очистка аппаратного TX FIFO — иначе после выхода из игры хвост
    // из FIFO ещё играется в меню (программный сброс кольца его не трогает).
    I2S_FIFO_CTL |= (1u << 25);
    udelay(2);
    I2S_FIFO_CTL &= ~(1u << 25);
    I2S_TX_CNT = 0;                          // сброс счётчика TX (как в init)
}
void i2s_mute(int m) {
    g_muted = m;
    // r706: PA10 (SD-усилка) пишет core0 напрямую (pa_dat_set) — см. led.c.
    pa_dat_set(SD_PIN, m ? 0 : 1);
}

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
    // r706: PA10 пишет core0 напрямую (раньше через coherent-shadow+CPU2 —
    // после клика в меню мог остаться на земле, усилитель молчал).
    pa_dat_set(SD_PIN, 1);

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
// D-audio: раньше trim вызывался на CPU2 (consummer) конкурентно с push/rd,
// обновляя g_ring_rd неатомарной записью и сбрасывая dc (который пишет core0
// и читает CPU2) → data-race и «хвосты/наложения». Теперь:
//   - вызов ТОЛЬКО из продюсера (i2s_push_sample / i2s_ring_trim), где кольцо
//     и dc принадлежат этому же потоку — гонки нет;
//   - индексы двигаются атомарно (LDREX/STREX), чтобы CPU2-читатель никогда
//     не увидел «рваный» rd/yровень = срезу посреди чтения.
// r707 (C2-fix): срез продюсера — ПРОСТОЙ store, без LDREX/STREX.
// r703 перевёл trim на CAS «чтобы CPU2 не увидел рваный rd». Это дало
// live-lock CPU2 (см. ring_rd_advance): эксклюзивный монитор сбрасывается
// непрерывными обычными store core0 в ту же кэш-линию (wr/rd соседние),
// CPU2 бесконечно retry'ит CAS → кольцо не доливается.
// SPSC: trim вызывается только продюсером (тот же поток, что wr). Гонка
// «trim пишет rd, пока CPU2 читает» — редкая (только при переполнении),
// последствия: пара лишних/пропущенных сэмплов на срезе — незаметно.
static int ring_trim(uint32_t keep_pairs) {
    if (keep_pairs >= AUDIO_RING_SIZE) keep_pairs = AUDIO_RING_SIZE - 1;
    uint32_t wr = g_ring_wr;
    uint32_t rd = g_ring_rd;
    if (wr - rd <= keep_pairs) return 0;
    g_ring_rd = wr - keep_pairs;
    // dmb: публикация нового rd (освободившихся слотов) для CPU2-читателя.
    __asm volatile("dmb sy" ::: "memory");
    return 1;
}

void i2s_ring_trim(uint32_t keep_pairs) {
    if (ring_trim(keep_pairs)) {
        // r575 (Д-4): дропнутые старые пары не должны влиять на DC-оценку.
        // Здесь мы на продюсере тредом, владеющим dc — сброс без гонки.
        dc_l = 0;
        dc_r = 0;
    }
}

// Приём сэмпла: НЕБЛОКИРУЮЩИЙ. r642: DC-блокер УБРАН из этого слоя.
// Эмуляторы (NGP fast, SNES, Lynx, GB...) уже центрируют сигнал на своём слое
// (у NGP есть DC-блокер в neopopsound.c). Повторное центрирование здесь — двойной
// DC = шум при появлении/динамике звука (симптом владельца). Оставляем громкость+кламп.
void i2s_push_sample(int16_t left, int16_t right) {
    if (!g_i2s_ready) return;
    // r706: PA10 (SD-усилка) пишет core0 напрямую (pa_dat_set). При нулевой
    // громкости — аппаратный мьют (PA10=0, шум/дрейф не слышны) — см. i2s_volume.
    if (g_muted) {
        g_muted = 0;
        pa_dat_set(SD_PIN, g_volume_pct > 0 ? 1 : 0);
    }
    // D-audio: drop-on-high. Уровень кольца контролируется у продюсера
    // (единственного потока, которому безопасно срезать и сбрасывать dc).
    // При достижении высокого порога срезаем хвост до целевого — кольцо не
    // растёт в бесконечность, срез идёт в этом же потоке без гонки с CPU2.
    uint32_t lvl = ring_count();
    if (lvl >= AUDIO_RING_TRIM_HIGH) { i2s_ring_trim(AUDIO_RING_TRIM_TARGET); return; }

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

// r707 (C2-fix): инкремент g_ring_rd потребителем (CPU2) — ПРОСТОЙ store, без CAS.
// r703 пытался делать CAS: продюсер (core0) и потребитель (CPU2) двигали бы
// g_ring_rd эксклюзивно. НО g_ring_wr (0x..698) и g_ring_rd (0x..69C) лежат в
// ОДНОЙ 32-байтной кэш-линии, а core0 пишет g_ring_wr++ КАЖДЫЙ сэмпл (48к/с)
// обычным store → это сбрасывает exclusive-монитор линии → STREX CPU2
// постоянно падает → CPU2 вечно крутит CAS внутри flush_max → beat не тикает,
// кольцо не доливается (симптом на стенде: beat застыл во время теста, ring
// растёт, звук идёт только через core0-fallback «импульсами»).
// SPSC: продюсер пишет ТОЛЬКО wr, потребитель ТОЛЬКО rd — эксклюзив не нужен.
// Единственный «чужой» писатель rd — trim продюсера (редкий, при переполнении);
// гонка «trim пишет rd = несколько пар скипнулись/продублировались» в момент
// переполнения — незаметна на слух, зато CPU2 больше не live-lock'ится.
static inline void ring_rd_advance(void) {
    g_ring_rd++;
    // Публикуем сдвиг индекса: продюсер читает уровень (wr-rd) и может писать
    // в освободившийся слот — dmb упорядочивает чтение сэмплов ДО сдвига.
    __asm volatile("dmb sy" ::: "memory");
}

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
// D-audio: возвращает число записанных пар — CPU2 по нему решает, делать ли
// NOP-backoff (не долбить регистр I2S_FSTA вплотную), см. audio_core.c.
// r702-DIAG(+ИСПРАВЛЕНИЕ): watermark-gate (CEILING/FLOOR) УБРАН — по данным
// стенда CPU2 залипал ВНУТРИ flush_max с этим gate (tr=flush-in, beat стоит,
// wm=0 при lvl≥CEILING). Причины две:
//   1) gate «не опускаться ниже FLOOR» + «не играть до CEILING» создавал
//      состояние, где CPU2 уходил в недостижимый выход;
//   2) мой прежний break вместо fade при пустом кольце давал резкие обрывы
//      FIFO = «помехи на тишине» в играх.
// Возвращён ПРОВЕРЕННЫЙ каркас (как было до watermark): долив до предела,
// при пустом кольце — плавное затухание к 0 (~1 мс), никаких блокирующих
// «ждать накопления». Анти-«тишина между пачками» будет добавлена позже и
// НЕ через блокировку потребителя.
int i2s_flush_max(int max_pairs) {
    if (!g_i2s_ready) return 0;
    if (max_pairs > I2S_FLUSH_BURST) max_pairs = I2S_FLUSH_BURST;
    int n = 0;

    while (n < max_pairs) {
        if (I2S_FSTA_TXE_CNT(I2S_FIFO_STA) < I2S_TXE_MIN)
            break;   // мало места — вернёмся позже (FIFO сам играет 48 кГц)
        if (ring_count() > 0) {
            uint32_t r = g_ring_rd & (AUDIO_RING_SIZE - 1);
            hold_l = g_ring_l[r];
            hold_r = g_ring_r[r];
            fade_left = I2S_HOLD_FADE_PAIRS;   // следующий голод начнёт затухать от нового уровня
            I2S_FIFO_TX = (uint32_t)(uint16_t)hold_l << 16;
            I2S_FIFO_TX = (uint32_t)(uint16_t)hold_r << 16;
            // r707 (C2-fix): простой инкремент (без CAS — см. ring_rd_advance).
            // Барьер ДО инкремента: прочитанные сэмплы и записи в FIFO
            // завершились, прежде чем пинок индекса освобождает слот
            // продюсеру (SPSC-упорядочивание).
            __asm volatile("dmb sy" ::: "memory");
            ring_rd_advance();
        } else {
            // Кольцо пусто (пауза между пачками): плавно гасим уровень до 0.
            // Резкий break/тишина дают «помехи на тишине» (обрыв FIFO).
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
    return n;
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
    if (was) {
        g_muted = 0;
        // r706: PA10 напрямую (core0). При 0% громкости усилитель не включаем
        // вовсе — тон/клик молчат (аппаратный мьют).
        pa_dat_set(SD_PIN, g_volume_pct > 0 ? 1 : 0);
    }
    return was;
}
static void i2s_mute_pop(int was) {
    if (was) {
        g_muted = 1;
        pa_dat_set(SD_PIN, 0);   // r706: PA10 напрямую
    }
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
// Тестовый «эмулятор» звука: тон, лиющийся в КОЛЬЦО пачками за кадр, как
// настоящий эмулятор (GBA ~549/кадр, Lynx ~640/кадр, ...). Использует ШТАТНЫЙ
// путь продюсер→кольцо→CPU2→FIFO (без паузы CPU2, без прямого доступа к TX
// FIFO) — чтобы проверить именно слой на ровном сигнале: если при кадровом
// пачечном производстве слой рвётся (паузы, тон/гул, клики), это будет слышно
// на непрерывном тоне. pairs_per_frame — размер «пачки» эмулятора, frames —
// сколько кадров играть.
// Возврат: без паузы CPU2, генерим пачками и ждём кадровую границу, как ядро.
void i2s_tone_burst_test(int freq, int pairs_per_frame, int frames) {
    if (!g_i2s_ready) return;
    if (freq < 20) freq = 20;
    if (pairs_per_frame < 16) pairs_per_frame = 16;
    if (frames <= 0) frames = 60;

    // r715: тест — ЧИСТЫЙ продюсер (пачка → udelay до кадра), как эмулятор.
    // Доливает ТОЛЬКО CPU2 (без core0-fallback, без emu_throttle) — проверяем
    // ровно тот слой, что работает в играх. r722: диагностика удалена.
    i2s_ring_reset();
    if (g_audio_state) i2s_audio_cmd(AUDIO_CMD_RESUME);
    if (g_muted) {
        g_muted = 0;
        // r706: PA10 напрямую (core0); при 0% громкости усилитель не включаем.
        pa_dat_set(SD_PIN, g_volume_pct > 0 ? 1 : 0);
    }

    const uint32_t step = (uint32_t)(((uint64_t)freq << 16) / 48000u);
    uint32_t ph = 0;
    uint32_t t0 = 0;

    for (int f = 0; f < frames; f++) {
        // Пачка тона (моно → стерео), как sound_read_samples→i2s_push_sample.
        for (int p = 0; p < pairs_per_frame; p++) {
            int32_t s = (int32_t)sin_tab[(ph >> 8) & 0xFF] / 2;   // 50%
            ph += step;
            i2s_push_sample((int16_t)s, (int16_t)s);
        }
        // Кадровая пауза (продюсер не ждёт вывода — как эмулятор).
        if (!t0) t0 = h3_hs_timer_lo_us();
        else {
            uint32_t el = (uint32_t)(h3_hs_timer_lo_us() - t0);
            if (el < 16667u) udelay(16667u - el);
            t0 = h3_hs_timer_lo_us();
        }
    }
    // Доиграть хвост (что успело накопиться) — короткая пауза.
    udelay(30000);
    i2s_ring_reset();
    if (g_audio_state) i2s_audio_cmd(AUDIO_CMD_RESUME);   // вернуть меню в норму
}

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
