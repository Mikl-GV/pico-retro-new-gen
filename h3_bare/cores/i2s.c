// i2s.c — звуковой слой I2S0 (H3, PCM0) для MAX98357A, 48 кГц стерео 16-бит.
//
// АРХИТЕКТУРА (спроектировано заново по железу H3 — даташит + sun4i-i2s.c):
//   core0 (эмулятор) --i2s_push_sample--> ring (.coherent, uncached)
//   CPU2 (аудио-ядро) --i2s_poll_fill-->  I2S0 TX FIFO (guard TXE_CNT)
//
// Принципы (не повторяют старые эксперименты):
//   1. Темп задаёт АППАРАТНЫЙ TX FIFO (48 кГц из делителей MCLK/BCLK) —
//      CPU2 не считает время и не полиится по таймеру.
//   2. CPU2 пишет в FIFO ТОЛЬКО когда TXE_CNT (свободных слов) >= порога —
//      запись в полный FIFO (корень старого AHB-зависания) исключена.
//   3. Без DMA и без GIC — оба в истории не работали.
//   4. Ring и почта — в .coherent (uncached через mmu_mark_uncached) —
//      видны и core0, и CPU2 без кэш-когерентности.
//
// Тактовые параметры (из драйвера sun4i-i2s.c, sun8i-h3):
//   mod_clk = PLL_AUDIO = 24.576 МГц (для 48 кГц);
//   BCLK = 24.576М / (48к * 2 * 16) = 16 → div=7 (sun8i кодировка);
//   LRCK_PERIOD = slot_width = 16 (стерио 16-бит, I2S mode);
//   SR=3, WSS=3 (sun8i кодировка для 16-бит);
//   FIFO_TX = 0x20, TX_CNT = 0x28, FIFO_CTRL = 0x14 (FLUSH_TX bit25).
//
// CCU (H3): gate bus-i2s0 = BUS_CLK_GATING1 bit12 (0x068); mod_clk =
//   I2SPCM0_CLK (0x0B0) — mux pll-audio (bits 17:16 = pll-audio), gate bit31.
#include <stdint.h>
#include <string.h>
#include "h3.h"
#include "h3_ccu.h"
#include "i2s.h"
#include "led.h"
#include "h3_hs_timer.h"

extern int printf(const char* fmt, ...);

// ---- I2S0 база и регистры (H3, PCM0) ----
#define I2S_BASE 0x01C22000u

#define I2S_CTRL     (*(volatile uint32_t*)(I2S_BASE + 0x00))
#define I2S_FMT0     (*(volatile uint32_t*)(I2S_BASE + 0x04))
#define I2S_FMT1     (*(volatile uint32_t*)(I2S_BASE + 0x08))
#define I2S_ISTA     (*(volatile uint32_t*)(I2S_BASE + 0x0C)) /* статус (write-1-clear) */
#define I2S_RX_FIFO  (*(volatile uint32_t*)(I2S_BASE + 0x10))
#define I2S_FIFO_CTL (*(volatile uint32_t*)(I2S_BASE + 0x14))
#define I2S_FIFO_STA (*(volatile uint32_t*)(I2S_BASE + 0x18))
#define I2S_INT      (*(volatile uint32_t*)(I2S_BASE + 0x1C)) /* DMA_INT_CTRL */
#define I2S_FIFO_TX  (*(volatile uint32_t*)(I2S_BASE + 0x20))
#define I2S_CLK_DIV  (*(volatile uint32_t*)(I2S_BASE + 0x24))
#define I2S_TX_CNT   (*(volatile uint32_t*)(I2S_BASE + 0x28))
#define I2S_CHAN_CFG (*(volatile uint32_t*)(I2S_BASE + 0x30))
#define I2S_TX_CSEL  (*(volatile uint32_t*)(I2S_BASE + 0x34))
#define I2S_TX_CMAP  (*(volatile uint32_t*)(I2S_BASE + 0x44))

// Каналы (sun8i, эталон): CHCFG TXSLOT(n-1) bits3:0, RXSLOT(n-1)<<4.
#define I2S_CHCFG_TXSLOT(v)  ((v) - 1)
#define I2S_CHCFG_RXSLOT(v)  (((v) - 1) << 4)

// ---- Биты (sun8i-H3, из sun4i-i2s.c) ----
#define I2S_CTRL_GL_EN     (1u << 0)
#define I2S_CTRL_TX_EN     (1u << 2)
#define I2S_CTRL_MODE_MASK (3u << 4)   // sun8i: MODE bits 5:4
#define I2S_CTRL_MODE_LEFT (1u << 4)   // left-justified / I2S
#define I2S_CTRL_SDO_EN0   (1u << 8)
#define I2S_CTRL_LRCK_OUT  (1u << 17)
#define I2S_CTRL_BCLK_OUT  (1u << 18)

#define I2S_FMT0_LRCK_PERIOD_MASK (0x3FFu << 8)
#define I2S_FMT0_LRCK_PERIOD(v)   (((v)-1) << 8)
#define I2S_FMT0_WSS_MASK   (7u << 0)
#define I2S_FMT0_WSS(v)     ((v) << 0)
#define I2S_FMT0_SR_MASK    (7u << 4)
#define I2S_FMT0_SR(v)      ((v) << 4)
#define I2S_FMT0_BCLK_POL_NORMAL   (0u << 7)
#define I2S_FMT0_LRCLK_POL_START_LOW (0u << 19)

#define I2S_FIFO_CTL_FLUSH_TX (1u << 25)
#define I2S_FIFO_CTL_TXIM     (1u << 2)   /* TXIM: 0=данные в СТАРШИХ битах (MSB), 1=LSB */
#define I2S_FIFO_CTL_TXTL(v)  ((v) << 12) /* уровень срабатывания TX empty */
#define I2S_TX_CSEL_OFFSET(v)  ((v) << 12) /* offset в слотах (для I2S = 1) */
#define I2S_TX_CHAN_EN(n)      ((((1u << (n)) - 1) << 4))  /* enable каналов */
#define I2S_TX_CHSEL(n)        ((n) - 1)   /* число каналов */

#define I2S_CLK_DIV_MCLK_EN  (1u << 8)   /* sun8i: bit8 (не 7!) */
#define I2S_CLK_DIV_BCLK(v)  ((v) << 4)
#define I2S_CLK_DIV_MCLK(v)  ((v) << 0)

// Свободные слова TX FIFO (FIFO_STA bits 23:16), порог — не лезть на границу.
#define I2S_FSTA_TXE_CNT(st) (((st) >> 16) & 0xFFu)
#define I2S_TXE_MIN 8                     /* пишем, только если свободно >= 4 пар */

// ---- CCU H3 ----
// r907 (ПРИЧИНА ТИШИНЫ): сброс I2S0 — регистр BUS_SOFT_RST3 0x2D0, бит12
// (mainline ccu-sun8i-h3.c: `[RST_BUS_I2S0] = { 0x2d0, BIT(12) }`, эталон
// Bare_linux h3_ccu.h: H3_CCU_RST3=0x2D0). Прежний код писал сброс в 0x2C4 —
// это RST1, чей бит12 = сброс DE (дисплея); I2S0 при этом НЕ выходил из сброса
// → блок не работал → тишина во всех эмуляторах и тест-тоне (тракт общий).
#define CCU_BUS_GATING2 (*(volatile uint32_t*)(H3_CCU_BASE + 0x068))  /* gate i2s0 = bit12 */
#define CCU_BUS_RST3    (*(volatile uint32_t*)(H3_CCU_BASE + 0x2D0))  /* reset i2s0 = bit12 */
#define CCU_I2SPCM0_CLK (*(volatile uint32_t*)(H3_CCU_BASE + 0x0B0))
#define PLL_AUDIO_CTRL  (*(volatile uint32_t*)(H3_CCU_BASE + 0x008))
#define PLL_AUDIO_PAT   (*(volatile uint32_t*)(H3_CCU_BASE + 0x284))

// PA10 — SD усилителя MAX98357A (HIGH = работа).
#define SD_PIN 10

// r928: SRAM A1 (uncached, вне DRAM) — дубль готовности I2S для CPU2 (без MMU).
// CPU2 видит SRAM всегда, как статус 0x90/0xA2 (доказан живым); если
// .coherent-видимость DRAM между ядрами нарушена — сработает SRAM.
#define I2S_SRAM_READY_ADDR 0x94u

// r929: МЕЖЪЯДЕРНАЯ синхронизация кольца — ТОЛЬКО SRAM A1 (НЕ .coherent).
// Стенд-лог r928: CPU2 снял g_ring_sync (`sync-cleared`), а core0 продолжал
// видеть 1 (`done=0`) → направление CPU2→core0 через DRAM НЕ работает.
// Адреса SRAM A1 (свободны, вне 0x18/0x20/0x24/0x64/0x88..0x8A8/0x90/0x94):
#define SRAM_RING_WR    (*(volatile uint32_t*)0x00000098u)  /* core0 пишет, CPU2 читает */
#define SRAM_RING_RD    (*(volatile uint32_t*)0x0000009Cu)  /* CPU2 пишет, core0 читает */
#define SRAM_RING_SYNC  (*(volatile uint32_t*)0x000000A0u)  /* двунаправленный */
#define SRAM_AUDIO_KEY  0x5A5Au                              /* защита от мусора U-Boot */

// ---- Кольцо потока эмулятора (core0 → CPU2) ----
// ДАННЫЕ ring (L/R) — в .coherent (uncached через mmu_mark_uncached, только
// чтение CPU2 по адресам DRAM); ИНДЕКСЫ wr/rd и флаг sync — в SRAM A1 (r929:
// стенд-лог r928 показал, что направление CPU2→core0 через DRAM НЕ работает
// — CPU2 снял sync, core0 не увидел; SRAM A1 двунаправленно доказан живым
// статусами 0x90/0x94/beat).
#define AUDIO_RING_SIZE 8192              /* пар; ~170 мс при 48 кГц */
static int16_t g_ring_l[AUDIO_RING_SIZE] __attribute__((section(".coherent"), aligned(8)));
static int16_t g_ring_r[AUDIO_RING_SIZE] __attribute__((section(".coherent"), aligned(8)));

// ---- Почта core0↔CPU2 (аудио-ядро) в .coherent ----
static volatile uint32_t g_audio_state  __attribute__((section(".coherent"), aligned(64))) = 0; /* 1 = CPU2 в цикле */
static volatile uint32_t g_audio_cmd    __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_seq    __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_ack    __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_beat   __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_audio_paused_f __attribute__((section(".coherent"), aligned(64))) = 0;
// r916: 1 = активная эмуляция (ставит core0 в emu_prepare, снимает в
// emu_audio_stop). По нему CPU2 доливает ТИШИНУ при пустом кольце ТОЛЬКО в
// играх; в меню флаг =0 → FIFO не трогает (r914-зависание меню исключено).
static volatile uint8_t g_audio_active __attribute__((section(".coherent"), aligned(32))) = 0;

// Диагностика CPU2 (в .coherent, uncached — пишет CPU2, читает core0).
static volatile uint32_t g_cpu2_stage     __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_flush_enter    __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_flush_exit     __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_flush_written  __attribute__((section(".coherent"), aligned(64))) = 0;
static volatile uint32_t g_flush_skip_full __attribute__((section(".coherent"), aligned(64))) = 0;
static uint32_t g_drop_cnt = 0;            /* дропы продюсера (полное кольцо) — core0 */

// r904: g_volume_pct вынесен из .coherent — секция NOLOAD, startup.S её зануляет,
// и инициализатор `= 10` терялся → усилитель стартовал с 0% (нет звука до входа
// в настройки). Громкость читает ТОЛЬКО core0 (i2s_volume_pct/i2s_volume); CPU2
// (i2s_poll_fill) её не использует — поэтому обычный BSS с сохраняемым стартовым
// значением корректен. Оставлен в .coherent только g_i2s_ready (его CPU2 читает).
static int g_i2s_ready  __attribute__((section(".coherent"), aligned(64))) = 0;
static int g_volume_pct = 10;  /* r898/r904: 10% по умолчанию (задано владельцем) */
static int g_muted = 1;                    /* усилитель выключен до init */

// ---- Фаза 0 (r910): диагностика согласования темпа продюсер↔48 кГц ----
// Водяные знаки заполнения кольца, дропы (переполнение), недоливы (underrun),
// суммарно поданные пары. 32-бит aligned — атомарно на ARM. CPU2 пишет
// underrun, core0 — остальное; читает core0 для UART-дампа.
volatile uint32_t g_stat_fill_max = 0;
volatile uint32_t g_stat_fill_min = AUDIO_RING_SIZE;
volatile uint32_t g_stat_underrun = 0;
volatile uint32_t g_stat_pairs    = 0;     // всего пар подано (core0)
volatile uint8_t  g_stat_active   = 0;     // замер только во время эмуляции

// ---- Такты: PLL audio = 24.576 МГц (для 48 кГц) ----
static int pll_audio_lock_wait(void) {
    uint32_t t = 0;
    while (!(PLL_AUDIO_CTRL & (1u << 28)) && t++ < 1000000) ;
    return (PLL_AUDIO_CTRL & (1u << 28)) ? 0 : -1;
}

static int pll_audio_enable(void) {
    // r914 (КОРЕНЬ ~73% дропа и «запаздывания» во ВСЕХ системах): по даташиту
    // H3 (стр.93) PLL_AUDIO = (24MHz*N)/(M*P). Прежний код ставил N=14,M=14 при
    // сохранённом P=4 → 24*14/(14*4) = 6 МГц (в 4 раза ниже!). Тогда BCLK=/8 от
    // 6 МГц → LRCK ≈ 11.7 кГц вместо 48 → продюсер переполнял кольцо на ~75%.
    // Правильно — проверенное эталоном значение 0x80035514: EN + N=86,M=21,P=4
    // → 24*86/(21*4) = 24.571 МГц (≈24.576). r917(D1 аудита): таймаут лоска
    // теперь РЕАЛЬНО возвращает ошибку — инициализация не идёт дальше.
    PLL_AUDIO_PAT = 0xc000ac02u;
    PLL_AUDIO_CTRL = 0x80035514u;   /* EN=1, N=86, M=21, P=4 → 24.571 МГц */
    udelay(3000);
    if (pll_audio_lock_wait() == 0) { printf("I2S: PLL audio 24.571 MHz locked\n"); return 0; }
    printf("I2S: PLL audio LOCK TIMEOUT\n");
    return -1;
}

// ---- Громкость/мьют (PA10 — SD усилителя, прямой писатель core0) ----
// r924: Volume 0% = аппаратный мьют — PA10 (SD) опускается в 0, усилитель
// MAX98357A выключен. ЛЮБОЕ ненулевое значение при !g_muted поднимает SD.
// r922 (всегда PA10=1) отменён: усилитель реально выключается на громкости 0
// и при mute — не жжёт класс-D без полезного сигнала.
// g_volume_pct читает только core0 (i2s_volume_pct/i2s_push_sample); CPU2 его
// не использует — поэтому обычный BSS, не .coherent.
static void i2s_apply_amp(void) {
    // Усилитель включён, только если громкость >0 И не замьючено.
    pa_dat_set(SD_PIN, (g_volume_pct > 0 && !g_muted) ? 1 : 0);
}
void i2s_volume(int p) {
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    g_volume_pct = p;
    i2s_apply_amp();
    printf("audio: volume=%d%%\n", p);
}
int i2s_volume_pct(void) { return g_volume_pct; }
int i2s_ready(void) { return g_i2s_ready; }

void i2s_mute(int m) {
    g_muted = m ? 1 : 0;
    i2s_apply_amp();
}

// DC-блокер не нужен (макрос совместимости — API сохранён).
void i2s_dc_shift_set(int shift) { (void)shift; }

// ---- Ring: продюсер (core0) ----
void i2s_push_sample(int16_t left, int16_t right) {
    if (!g_i2s_ready) return;
    uint32_t wr = SRAM_RING_WR;
    uint32_t next = (wr + 1) & (AUDIO_RING_SIZE - 1);
    if (next == SRAM_RING_RD) {
        g_drop_cnt++;                       // кольцо полно — продюсер теряет лишнее
        return;
    }
    // r914: реальная громкость — масштабируем сэмплы (прежде громкость только
    // включала/выключала PA10, поэтому «10%» звучало как максимум).
    // g_volume_pct читает core0 (продюсер), CPU2 не участвует.
    if (g_volume_pct != 100) {
        left  = (int16_t)((int32_t)left  * g_volume_pct / 100);
        right = (int16_t)((int32_t)right * g_volume_pct / 100);
    }
    g_ring_l[wr] = left;
    g_ring_r[wr] = right;
    // r920: dsb (полный системный барьер) — НЕ dmb. Переключатель wr→CPU2
    // должен гарантированно быть виден другому ядру даже для uncached-memory;
    // dmb sy на этом кремнии давал рассинхрон (avl=0 для CPU2 после первого
    // прохода кольца → «тишина» при живом beat).
    __asm volatile("dsb" ::: "memory");
    SRAM_RING_WR = next;
    // Фаза 0 (r910): учёт поданных пар и водяных знаков заполнения кольца.
    g_stat_pairs++;
    {
        uint32_t fill = (SRAM_RING_WR - SRAM_RING_RD) & (AUDIO_RING_SIZE - 1);
        if (fill > g_stat_fill_max) g_stat_fill_max = fill;
        if (fill < g_stat_fill_min) g_stat_fill_min = fill;
    }
}

// r919 (Баг 1/2 аудита): пакетная подача блока пар ОДНИМ dmb вместо dsb на
// каждый сэмпл. Для SPSC producer/consumer достаточно release-барьера перед
// обновлением wr. Возвращает число реально записанных пар (остаток дропается
// при переполнении — как перегруженный host-слой).
uint32_t i2s_push_block(const int16_t* pairs, uint32_t n) {
    if (!g_i2s_ready || !pairs) return 0;
    uint32_t written = 0;
    uint32_t wr = SRAM_RING_WR;
    while (written < n) {
        uint32_t next = (wr + 1) & (AUDIO_RING_SIZE - 1);
        if (next == SRAM_RING_RD) { g_drop_cnt += (n - written); break; }
        int16_t l = pairs[2 * written], r = pairs[2 * written + 1];
        if (g_volume_pct != 100) {
            l = (int16_t)((int32_t)l * g_volume_pct / 100);
            r = (int16_t)((int32_t)r * g_volume_pct / 100);
        }
        g_ring_l[wr] = l;
        g_ring_r[wr] = r;
        wr = next;
        written++;
    }
    if (written) {
        g_stat_pairs += written;
        {
            uint32_t fill = (wr - SRAM_RING_RD) & (AUDIO_RING_SIZE - 1);
            if (fill > g_stat_fill_max) g_stat_fill_max = fill;
            if (fill < g_stat_fill_min) g_stat_fill_min = fill;
        }
        __asm volatile("dsb" ::: "memory");        // r920: dsb, не dmb — см. push_sample
        SRAM_RING_WR = wr;
    }
    return written;
}

void i2s_ring_reset(void) {
    // r929: синхронный сброс кольца ЧЕРЕЗ SRAM A1 (не .coherent!). Стенд-лог
    // r928: CPU2 снял g_ring_sync, а core0 в .coherent не увидел (направление
    // CPU2→core0 через DRAM не работает) → вечный wait, зависание теста меню.
    // SRAM A1 двунаправленно доказан (0x90/0x94/beat). rd пишет ТОЛЬКО CPU2:
    // ставим sync в SRAM, CPU2 подтягивает rd=wr и снимает sync, core0 ждёт
    // снятия (с потолком — зависание невозможно), затем пишет wr=rd.
    uint32_t w0 = SRAM_RING_WR, r0 = SRAM_RING_RD;
    printf("DBG-RST: in wr=%u rd=%u beat=%u\n",
           (unsigned)w0, (unsigned)r0, (unsigned)i2s_audio_beat());
    SRAM_RING_SYNC = 1;
    __asm volatile("dsb" ::: "memory");
    uint32_t t = 0;
    uint32_t spins = 0;
    while (SRAM_RING_SYNC && t < 100000u) { t++; spins++; }   // ~потолок; CPU2 снимает флаг
    printf("DBG-RST: sync spins=%u done=%d rd=%u\n",
           (unsigned)spins, SRAM_RING_SYNC ? 0 : 1, (unsigned)SRAM_RING_RD);
    __asm volatile("dsb" ::: "memory");
    SRAM_RING_WR = SRAM_RING_RD;
    __asm volatile("dsb" ::: "memory");
    g_drop_cnt = 0;
}

// Ринг-свободные пары (для CPU2).
static inline uint32_t ring_count(void) {
    return (uint32_t)(SRAM_RING_WR - SRAM_RING_RD) & (AUDIO_RING_SIZE - 1);
}
static inline void ring_consume(uint32_t n) {
    SRAM_RING_RD = (SRAM_RING_RD + n) & (AUDIO_RING_SIZE - 1);
}

// ---- CPU2: долив ring → TX FIFO ----
// Переносит пары, пока в FIFO есть место (TXE_CNT >= порог) и кольцо не
// пусто. Возвращает число перенесенных пар. Темп — аппаратный FIFO.
#define I2S_FLUSH_BURST 24                  /* макс. пар за один вызов */
static int s_was_empty = 0;                 /* Фаза 0 (r910): переход в пустоту */
int i2s_poll_fill(void) {
    if (!g_i2s_ready) return 0;
    int n = 0;
    g_cpu2_stage = 2;
    g_flush_enter++;
    while (n < I2S_FLUSH_BURST) {
        // Свободных слов в TX FIFO. По даташиту H3 (I2S/PCM_FSTA, стр.534):
        // [23:16] = TXE_CNT «Empty Space Word Counter», [28] = TXE (есть место).
        // При пустом FIFO = 128 (глубина). Это ВЕРНОЕ поле (r912-сдвиг — ошибка).
        uint32_t free_words = I2S_FSTA_TXE_CNT(I2S_FIFO_STA);
        if (free_words < I2S_TXE_MIN * 2) {   // < 4 пары — место есть, но не лезем на границу
            if (free_words < 2) {             // совсем нет места — подождать
                g_flush_skip_full++;
                break;
            }
            free_words -= (free_words & 1);   // чётное число слов
        }
        uint32_t avail = ring_count();
        if (!avail) {
            // Фаза 0 (r910): недолив = FIFO хочет данных, а кольцо пусто.
            if (g_stat_active && !s_was_empty) { g_stat_underrun++; s_was_empty = 1; }
            // r924: silence-fill (r916, g_audio_active) УБРАН — CPU2 больше НЕ
            // подмешивает тишину сам. Причины: (1) меню-тон при пустом кольце
            // и g_audio_active=0 получал тишину перемежаемую с данными
            // (ob-щипки); (2) игровые паузы в тихих местах (r916) приводили к
            // заливу FIFO нулями, маскируя реальные проблемы оси; (3) с
            // синхронным reset и держанием кольца заполненным (блочные пуши)
            // fake-тишина не нужна — кольцо просто не должно пустеть. Если
            // кольцо пусто — это настоящий underrun, и он виден в статистике.
            break;
        }
        s_was_empty = 0;
        uint32_t take = avail;
        uint32_t maxpairs_by_fifo = free_words / 2;
        if (take > maxpairs_by_fifo) take = maxpairs_by_fifo;
        if (take <= 0) break;
        if ((uint32_t)take > (uint32_t)(I2S_FLUSH_BURST - n)) take = I2S_FLUSH_BURST - n;

        uint32_t rd = SRAM_RING_RD;
        for (uint32_t k = 0; k < take; k++) {
            uint32_t idx = (rd + k) & (AUDIO_RING_SIZE - 1);
            // Формат FIFO (TXIM=0): пара = ДВА 32-бит слова [L][R], сэмпл в
            // СТАРШИХ битах (16-бит << 16), младшие 0 — как эталон (r735).
            I2S_FIFO_TX = (uint32_t)(uint16_t)(int16_t)(g_ring_l[idx]) << 16;
            I2S_FIFO_TX = (uint32_t)(uint16_t)(int16_t)(g_ring_r[idx]) << 16;
        }
        ring_consume(take);
        n += take;
        g_flush_written += take;
    }
    g_flush_exit++;
    g_cpu2_stage = 0;
    return n;
}

void i2s_flush(void) { i2s_poll_fill(); }

// ---- Почта CPU2 (команды core0) ----
void i2s_audio_cmd(uint32_t cmd) { g_audio_cmd = cmd; __asm volatile("dsb" ::: "memory"); }
void i2s_audio_poll_cmd(void) {
    uint32_t cmd = g_audio_cmd;
    if (cmd == AUDIO_CMD_NONE) return;
    if (cmd == AUDIO_CMD_PAUSE) g_audio_paused_f = 1;
    // r925: RESUME НЕ вызывает i2s_ring_reset — тот ставит g_ring_sync=1 и ждёт
    // снятия от CPU2, а CPU2 сейчас внутри poll_cmd (до main-цикла не дойдёт)
    // → взаимная блокировка (r924-зависание на старте после «I2S: ready»).
    // RESUME только снимает паузу; сброс кольца делает core0 напрямую (в
    // i2s_test_tone*/emu_prepare), где он и должен быть.
    else if (cmd == AUDIO_CMD_RESUME) { g_audio_paused_f = 0; }
    g_audio_cmd = AUDIO_CMD_NONE;
    g_audio_ack = cmd;
}
void i2s_audio_set_state(int on) { g_audio_state = on ? 1 : 0; }
void i2s_audio_set_active(int on) { g_audio_active = on ? 1 : 0; }  /* r916: флаг «в игре» для CPU2 */
void i2s_audio_set_beat(uint32_t b) { g_audio_beat = b; }
int  i2s_audio_core_active(void) { return (int)g_audio_state; }
uint32_t i2s_audio_beat(void) { return g_audio_beat; }
int  i2s_audio_paused(void) { return (int)g_audio_paused_f; }

// ---- Прочее API (сохранено из совместимости) ----
void i2s_click(void) {
    // Короткий клик меню — 10 пар 1 кГц через ring (не блокирует).
    if (!g_i2s_ready) return;
    for (int i = 0; i < 10; i++) {
        int16_t s = (int16_t)(((i & 1) ? 6000 : -6000));
        i2s_push_sample(s, s);
    }
}

// Подача тона в кольцо блоками с контролем свободного места.
#define TONE_BLOCK 512                              /* пар за порцию */
static void i2s_tone_pour(const int16_t* blk, int n, uint32_t* pushed);  /* r924: определение ниже */

// Тест-тон на старте: ~200 мс 1 кГц — проверить, что I2S0 играет ровно 48 кГц
// и PA10-SD поднят. Вызывается из main.c после i2s_init().
// r924: как меню-тон — блочная подача с гейтом g_audio_active=1 (CPU2 считает
// это «игрой» и не подмешивает ничего, кольцо держится заполненным пачками),
// без вечного wait (i2s_tone_pour имеет потолок по wall-clock).
void i2s_test_tone(void) {
    if (!g_i2s_ready) return;
    printf("DBG-TONE: begin act=%d\n", (int)g_audio_active);
    i2s_audio_cmd(AUDIO_CMD_RESUME);
    i2s_ring_reset();
    printf("DBG-TONE: reset-ok beat=%u paused=%d\n",
           (unsigned)i2s_audio_beat(), (int)i2s_audio_paused());
    int32_t phase = 0;
    int32_t step = (int32_t)((1000LL << 16) / 48000);  // инкремент фазы (16.16)
    const uint32_t total = 48000 / 5;                   // 9600 пар = 200 мс
    uint32_t pushed = 0;
    static int16_t blk[TONE_BLOCK * 2];
    i2s_audio_set_active(1);
    uint32_t tone_t0 = h3_hs_timer_lo_us();
    while (pushed < total) {
        // r927: общий дедлайн тона (~3× длительности: boot-тон 200 мс → ~1.6 с).
        // Если CPU2 мёртв — кольцо забивается, pushed застревает; раньше цикл
        // крутился ВЕЧНО → «висит». Теперь выходим с диагнозом, не зависая.
        if (h3_hs_timer_lo_us() - tone_t0 > 1600000u) {
            printf("DBG-TONE: DEADLINE pushed=%u/%u beat=%u\n",
                   (unsigned)pushed, (unsigned)total, (unsigned)i2s_audio_beat());
            break;
        }
        uint32_t nb = total - pushed;
        if (nb > TONE_BLOCK) nb = TONE_BLOCK;
        for (uint32_t j = 0; j < nb; j++) {
            int16_t s = (int16_t)((phase & 0x8000) ? 8000 : -8000);  // меандр 1 кГц
            blk[2 * j] = s;
            blk[2 * j + 1] = s;
            phase += step;
        }
        // r926: поштучно-ограниченный вывод прогресса (12 раз — не спам)
        uint32_t pre = pushed;
        i2s_tone_pour(blk, (int)nb, &pushed);
        if (pushed == 0 || pre / 800 != pushed / 800)
            printf("DBG-TONE: it pre=%u post=%u\n", (unsigned)pre, (unsigned)pushed);
        udelay(16000);                     // «кадр» ~16.7 мс
    }
    i2s_audio_set_active(0);
    printf("TONE-START: pushed=%u/%u beat=%u\n",
           (unsigned)pushed, (unsigned)total, (unsigned)i2s_audio_beat());
}

// Подача тона в кольцо блоками с контролем свободного места.
// r923: СТАРОЕ поведение (r919) — `i2s_push_sample` + `udelay(21)` на пару —
// подавало тон РОВНО в темп расхода TX FIFO (48 кГц). Если CPU2 хоть немного
// отставал, кольцо пустело, а так как в меню `g_audio_active=0`, CPU2 при
// пустом кольце НЕ доливает тишину (i2s.c i2s_poll_fill) → FIFO недополучает
// → тон обрывался/молчал. Решение: тон пишется ПАЧКАМИ (i2s_push_block), при
// этом перед порцией ждём, пока в кольце хватит места, — кольцо держится
// заполненным с запасом, CPU2 успевает, дропов нет, обрывов нет. Это схема
// игры (пачка в кольцо), применимая и к меню, где долив тишины запрещён.
static void i2s_tone_pour(const int16_t* blk, int n, uint32_t* pushed) {
    // Ждём место в кольце, но с ПРЕДЕЛОМ по wall-clock: если CPU2 не сливает
    // (мёртв / стоит), бесконечный wait (r923) вешал меню навсегда. Потолок
    // ~200 мс; если место так и не пришло — дропаем порцию (тест всё равно
    // покажет drop), но не зависаем.
    uint32_t t = h3_hs_timer_lo_us();
    while ((uint32_t)n > (AUDIO_RING_SIZE - 1 - (uint32_t)ring_count())) {
        if (h3_hs_timer_lo_us() - t > 200000u) break;
        udelay(100);
    }
    *pushed += i2s_push_block(blk, (uint32_t)n);
}

// Тон заданной частоты (Гц) и длительности (мс) — синус 8-бит таблицей.
// Запускается из меню настроек «Audio test tone» (по команде, не на старте).
// r924: (1) НЕТ бесконечного ожидания места (см. i2s_tone_pour); (2) на время
// тона ставится g_audio_active=1, чтобы CPU2 считал тон «игрой» (в прошлой
// схеме при пустом кольце и g_audio_active=0 CPU2 не доливал — тон обрывался,
// а последующая версия с вечным wait висла); (3) вся длительность тона
// разбита на порции, между порциями — профильная пауза (как игра), чтобы
// кольцо держалось заполненным, но без «забивания» шины.
void i2s_test_tone_freq(int hz, int ms) {
    if (!g_i2s_ready || hz <= 0) return;
    i2s_audio_cmd(AUDIO_CMD_RESUME);   // снять возможную паузу (синхронный reset)
    i2s_ring_reset();
    // 8-битная синус-таблица (1/4 периода достаточно, но полная проще)
    static const uint8_t sin8[256] = {
        128,131,134,137,140,143,146,149,152,155,158,162,165,167,170,173,
        176,179,182,184,187,190,192,195,197,200,202,205,207,209,211,213,
        216,218,220,222,224,226,227,229,231,232,234,235,237,238,239,241,
        242,243,244,245,246,247,248,248,249,250,250,251,251,252,252,252,
        253,253,253,253,253,253,253,253,252,252,252,251,251,250,250,249,
        248,248,247,246,245,244,243,242,241,239,238,237,235,234,232,231,
        229,227,226,224,222,220,218,216,213,211,209,207,205,202,200,197,
        195,192,190,187,184,182,179,176,173,170,167,165,162,158,155,152,
        149,146,143,140,137,134,131,128,124,121,118,115,112,109,106,103,
        100,97,93,90,87,85,82,79,76,73,70,67,65,62,60,58,
        55,53,50,48,46,43,41,39,37,35,33,31,29,27,26,24,
        22,20,19,17,15,14,12,11,9,8,7,5,4,3,2,1,
        0,0,0,0,0,0,0,0,0,1,1,1,2,2,3,3,
        4,5,5,6,7,8,9,10,11,12,13,14,15,16,17,18,
        19,21,22,23,24,26,27,28,30,31,33,34,36,37,39,40,
        42,44,45,47,49,50,52,54,55,57,59,61,62,64,66,67
    };
    uint32_t phase = 0;
    uint32_t step = (uint32_t)(((uint64_t)hz << 32) / 48000ull);  // инкремент фазы на пару
    uint32_t total = (uint32_t)ms * 48;                            // пар
    uint32_t pushed = 0;
    static int16_t blk[TONE_BLOCK * 2];
    i2s_audio_set_active(1);       // тон = «игра» для CPU2 (на время тона)
    while (pushed < total) {
        uint32_t nb = total - pushed;
        if (nb > TONE_BLOCK) nb = TONE_BLOCK;
        for (uint32_t j = 0; j < nb; j++) {
            int16_t s = (int16_t)(((int32_t)sin8[(phase >> 24) & 0xFF] - 128) << 7);
            blk[2 * j] = s;
            blk[2 * j + 1] = s;
            phase += step;
        }
        i2s_tone_pour(blk, (int)nb, &pushed);
        // между «кадрами» тона — пауза ~1 кадра (16.7 мс), как игровой кадр
        udelay(16000);
    }
    i2s_audio_set_active(0);       // тон закончился — вернуть «меню»
    printf("TONE: hz=%d ms=%d pushed=%u/%u beat=%u fw=%u skip=%u\n",
           hz, ms, (unsigned)pushed, (unsigned)total,
           (unsigned)i2s_audio_beat(), (unsigned)g_flush_written, (unsigned)g_flush_skip_full);
}

// ---- Диагностика ----
void i2s_cpu2_diag(uint32_t* flush, uint32_t* written, uint32_t* skipfull, uint32_t* dummy) {
    if (flush) *flush = g_flush_enter;
    if (written) *written = g_flush_written;
    if (skipfull) *skipfull = g_flush_skip_full;
    if (dummy) *dummy = g_drop_cnt;
}
void i2s_cpu2_pairs_written_get(uint32_t* v) { if (v) *v = g_flush_written; }
void i2s_cpu2_stage_get(uint32_t* st, uint32_t* en, uint32_t* ex) {
    if (st) *st = g_cpu2_stage;
    if (en) *en = g_flush_enter;
    if (ex) *ex = g_flush_exit;
}
void i2s_ring_wr_rd_get(uint32_t* w, uint32_t* r) {
    if (w) *w = SRAM_RING_WR;
    if (r) *r = SRAM_RING_RD;
}
void i2s_flush_diag_get(uint32_t* nempty, uint32_t* nempty_full) {
    if (nempty) *nempty = g_flush_enter;
    if (nempty_full) *nempty_full = g_flush_skip_full;
}
void i2s_drop_cnt_reset(void) { g_drop_cnt = 0; }
uint32_t i2s_drop_cnt(void) { return g_drop_cnt; }

// ---- Фаза 0 (r910): API диагностики согласования темпа ----
void i2s_stats_reset(void) {
    g_stat_fill_max = 0;
    g_stat_fill_min = AUDIO_RING_SIZE;
    g_stat_underrun = 0;
    g_stat_pairs    = 0;
    g_drop_cnt      = 0;
}
void i2s_stats_active(int on) { g_stat_active = on ? 1 : 0; }
void i2s_stats_get(uint32_t* fmax, uint32_t* fmin, uint32_t* drop,
                   uint32_t* under, uint32_t* pairs) {
    if (fmax)  *fmax  = g_stat_fill_max;
    if (fmin)  *fmin  = (g_stat_fill_min == AUDIO_RING_SIZE) ? 0u : g_stat_fill_min;
    if (drop)  *drop  = g_drop_cnt;
    if (under) *under = g_stat_underrun;
    if (pairs) *pairs = g_stat_pairs;
}

// ---- Инициализация I2S0 ----
int i2s_init(void) {
    if (g_i2s_ready) return 0;

    // r917 (D1 аудита): если PLL не залочился — не продолжаем с «мёртвыми» тактами.
    if (pll_audio_enable() != 0) return -1;

    // CCU: gate + reset i2s0 (r907). Reset — RST3 0x2D0 bit12 по mainline
    // ccu-sun8i-h3.c. Правильная последовательность как в эталоне Bare_linux:
    // gate on → assert reset (&~bit) → deassert (|bit).
    CCU_BUS_GATING2 |= (1u << 12);
    __asm volatile("dsb" ::: "memory");
    udelay(10);
    CCU_BUS_RST3 &= ~(1u << 12);      /* assert: удерживаем I2S0 в сбросе */
    __asm volatile("dsb" ::: "memory");
    udelay(10);
    CCU_BUS_RST3 |= (1u << 12);       /* deassert: отпускаем сброс */
    __asm volatile("dsb" ::: "memory");
    udelay(10);

    // mod_clk: I2SPCM0_CLK mux → pll-audio (bits 17:16 = 3), gate bit31.
    // У r735 (работает на железе): (3u << 16) — родитель pll-audio, НЕ 0.
    CCU_I2SPCM0_CLK = (CCU_I2SPCM0_CLK & ~((3u << 16) | (1u << 31))) | (3u << 16) | (1u << 31);
    udelay(20);

    // Пины PCM0 (схема железа, r0.413/r735): PA18 = PCM0_SYNC (LRC),
    // PA19 = PCM0_CLK (BCLK), PA20 = PCM0_DOUT (DIN) — функция 2 = PCM0.
    // PA16..PA23 живут в CFG2 (не CFG3!). Без mux на этих пинах I2S0 не
    // выводит такты/данные на MAX98357A.
    H3_PIO_PORTA->CFG2 = (H3_PIO_PORTA->CFG2
                          & ~((0xFu << 8) | (0xFu << 12) | (0xFu << 16)))
                         | (2u << 8) | (2u << 12) | (2u << 16);   // функция 2 = PCM0

    // Сброс контроллера
    I2S_CTRL = 0;
    I2S_FIFO_CTL |= (1u << 24) | (1u << 25);   // flush RX+TX
    udelay(100);
    for (int i = 0; i < 256; i++) { volatile uint32_t d = I2S_RX_FIFO; (void)d; }
    I2S_TX_CNT = 0;

    // Формат (рабочий эталон Bare_linux, стенд 48 кГц): LRCK_PERIOD=32,
    // SR=3 (16-бит), SW=7 (32-бит слот), полярности по умолчанию.
    I2S_FMT0 = I2S_FMT0_SR(3) | (7u << 0)   // SW=7 (32-бит слот)
             | I2S_FMT0_LRCK_PERIOD(32)
             | I2S_FMT0_BCLK_POL_NORMAL | I2S_FMT0_LRCLK_POL_START_LOW;
    I2S_FMT1 = 0;                             // sign-extension pad 0 (SEXT=0)

    // FIFO_CTL: TXTL=64 (уровень empty), TXIM=0 (данные в СТАРШИХ битах).
    // КРИТИЧНО (эталон): при TXIM=1 FIFO берёт младшие биты (0) → тишина.
    I2S_FIFO_CTL = (I2S_FIFO_CTL & ~((0x7Fu << 12) | I2S_FIFO_CTL_TXIM))
                 | (0x40u << 12);             // TXTL=64, TXIM=0
    // r913 (КОРЕНЬ «кольцо не сливается»): ЯВНО снимаем биты FLUSH RX/TX (24,25).
    // Если они не self-clearing, RMW выше их сохранял → FIFO заперт в flush → не
    // сливается (CPU2 жив, но долив стоит). Если self-clearing — no-op, вреда нет.
    I2S_FIFO_CTL &= ~((1u << 24) | (1u << 25));

    // Каналы: 2 (стерео) — как эталон: TXCHSEL OFF=1, CHEN=2, CHSEL=2;
    // CHCFG TXSLOT=2, RXSLOT=2; TXCHMAP.
    I2S_CHAN_CFG = I2S_CHCFG_TXSLOT(2) | (I2S_CHCFG_RXSLOT(2));  // TX=1, RX=1<<4
    I2S_TX_CSEL  = I2S_TX_CSEL_OFFSET(1) | I2S_TX_CHAN_EN(2) | I2S_TX_CHSEL(2);
    I2S_TX_CMAP  = 0x76543210;

    // Такты (эталон, 48 кГц): BCLK val 5 (=/8 → 3.072М), MCLK val 2 (=/2),
    // MCLK_EN bit8.
    I2S_CLK_DIV = I2S_CLK_DIV_MCLK_EN | I2S_CLK_DIV_BCLK(5) | I2S_CLK_DIV_MCLK(2);

    // Включение: master (BCLK/LRCK out), режим I2S (MODE_LEFT), TX, SDO0.
    I2S_CTRL = I2S_CTRL_BCLK_OUT | I2S_CTRL_LRCK_OUT
             | I2S_CTRL_MODE_LEFT
             | I2S_CTRL_SDO_EN0 | I2S_CTRL_TX_EN | I2S_CTRL_GL_EN;
    udelay(1000);

    // Сброс статусов (underrun/overrun) — write-1-clear.
    I2S_ISTA = 0x7F;

    // r913 ДИАГ (безопасно): дамп регистров I2S0 + тест слива TX FIFO.
    // Пишем не больше реально свободного (free0) — переполнение невозможно.
    // Смысл: free_50ms ≈ 128 → FIFO сливается (аппарат ок, искать софт/темп);
    //        free_50ms ≈ free_now → FIFO НЕ сливается (такт/включение/FLUSH).
    {
        uint32_t fsta0 = I2S_FIFO_STA;
        uint32_t free0 = (fsta0 >> 16) & 0xFFu;
        uint32_t nw = free0 > 64u ? 64u : free0;
        for (uint32_t i = 0; i < nw; i++) I2S_FIFO_TX = 0;   // тишина
        __asm volatile("dsb" ::: "memory");
        uint32_t free1 = (I2S_FIFO_STA >> 16) & 0xFFu;
        udelay(50000);                                       // 50 мс
        uint32_t free2 = (I2S_FIFO_STA >> 16) & 0xFFu;
        printf("I2S DIAG: CTRL=0x%08x FMT0=0x%08x FCTL=0x%08x CLKD=0x%08x\n",
               (unsigned)I2S_CTRL, (unsigned)I2S_FMT0, (unsigned)I2S_FIFO_CTL, (unsigned)I2S_CLK_DIV);
        printf("I2S DIAG: MODCLK=0x%08x GATE=0x%08x RST3=0x%08x\n",
               (unsigned)CCU_I2SPCM0_CLK, (unsigned)CCU_BUS_GATING2, (unsigned)CCU_BUS_RST3);
        printf("I2S DIAG: FSTA=0x%08x free_pre=%u wrote=%u free_now=%u free_50ms=%u TXCNT=%u\n",
               (unsigned)fsta0, (unsigned)free0, (unsigned)nw, (unsigned)free1,
               (unsigned)free2, (unsigned)(I2S_TX_CNT & 0xFFFFFFu));
        // r917 (B2 аудита): явная сверка LRCK = PLL/(BCLKDIV*2*LRCK_PERIOD) ≈ 48 кГц.
        {
            uint32_t pll = 24571000u;          // 0x80035514 → 24.571 МГц
            uint32_t bd  = 8u;                 // BCLKDIV=5 → «Divide by 8» (даташит стр.536)
            uint32_t lr  = 32u;                // LRCK_PERIOD 32 BCLK/канал → 64 BCLK/кадр
            printf("I2S DIAG: LRCK_calc=%u Hz (24.571M / %u / 2 / %u)\n",
                   (unsigned)(pll / (bd * 2u * lr)), (unsigned)bd, (unsigned)lr);
        }
    }

    g_i2s_ready = 1;
    g_muted = 0;
    SRAM_RING_WR = 0;
    SRAM_RING_RD = 0;
    SRAM_RING_SYNC = 0;
    __asm volatile("dsb" ::: "memory");
    // r928: SRAM-дубль готовности I2S — CPU2 (без MMU) видит SRAM A1 всегда,
    // как статус 0x90/0xA2 (доказан живым). Если .coherent-видимость DRAM
    // между ядрами нарушена, CPU2 всё равно войдёт в цикл.
    *(volatile uint32_t*)I2S_SRAM_READY_ADDR = 1u;
    __asm volatile("dsb" ::: "memory");
    // r924: PA10 (SD MAX98357A) — по громкости/мьюту (i2s_apply_amp), НЕ всегда
    // в 1 (r922 отменён): громкость 0 или mute → PA10=0, усилитель реально
    // выключен (не жжёт класс-D без полезного сигнала).
    i2s_apply_amp();

    printf("I2S: ready (48k stereo, PLL 24.576M)\n");
    return 0;
}

// ---- CPU2: точка входа аудио-ядра ----
// Имя cpu2_entry — из startup.S (ассемблерный пролог, затем bx cpu2_audio_entry).

// r927: сырая печать с CPU2 (UART0 THR напрямую, как uart_dbg в startup.S).
// printf с CPU2 не используется (общая блокировка/стек — риск). Только
// диагностика, вызывается тем же физическим UART0 (115200).
static void cpu2_uart_puts(const char* s) {
    volatile uint32_t* usr = (volatile uint32_t*)0x01C2807Cu;
    volatile uint32_t* thr = (volatile uint32_t*)0x01C28000u;
    while (*s) {
        int t = 0xFFFF;
        while (!(*usr & 2u) && --t) ;
        *thr = (uint32_t)(uint8_t)*s++;
    }
}

void cpu2_audio_entry(void) {
    i2s_audio_set_state(1);
    i2s_audio_set_beat(0);
    cpu2_uart_puts("cpu2: enter\r\n");

    uint32_t beat = 0;
    int once = 0;
    for (;;) {
        i2s_audio_poll_cmd();
        // r918: r917-`wfi` ОТМЕНЁН — у CPU2 нет источника пробуждения (GIC для
        // CPU2 не настроен): из `wfi` ядро не выходит никогда → beat=0, долив
        // мёртв (лог r917: beat=0 fw=0, полная тишина). Читаем флаг активным
        // циклом (стоимость ~капля, ядро своё).
        // r928: готовность = .coherent ИЛИ SRAM-дубль. Если core0 не виден
        // через DRAM (проблема когерентности) — сработает SRAM.
        if (!g_i2s_ready && !*(volatile uint32_t*)I2S_SRAM_READY_ADDR) continue;
        if (SRAM_RING_SYNC) {
            // r929: синхронный сброс кольца через SRAM A1. rd — собственность
            // ТОЛЬКО CPU2: по запросу core0 подтягиваем rd к wr (кольцо
            // пустеет), снимаем sync (core0 ждёт снятия) — после этого core0
            // пишет wr=rd и гонки consume нет.
            SRAM_RING_RD = SRAM_RING_WR;
            __asm volatile("dsb" ::: "memory");
            SRAM_RING_SYNC = 0;
            if (!once) { cpu2_uart_puts("cpu2: sync-cleared\r\n"); once = 1; }
            continue;
        }
        if (i2s_audio_paused())
            continue;
        if (!once) { cpu2_uart_puts("cpu2: before-poll-fill\r\n"); }
        int wrote = i2s_poll_fill();
        // r929: маркеры before/after — ОДИН раз (первый проход). В r928 они
        // печатались на КАЖДОМ витке (once ставился только в sync-ветке) и
        // забивали UART (склейки в логе), тормозя сам долив.
        if (!once) { cpu2_uart_puts("cpu2: after-poll-fill\r\n"); once = 1; }
        if (wrote == 0) {
            // FIFO на границе / кольцо пусто — NOP-backoff (не долбить регистр).
            __asm volatile("nop; nop; nop; nop; nop; nop; nop; nop");
        }
        beat++;
        i2s_audio_set_beat(beat);
        if (beat == 1)
            cpu2_uart_puts("cpu2: loop beat=1\r\n");
        if ((beat & 0xFFFFFFu) == 0)
            cpu2_uart_puts("cpu2: beat-wrap\r\n");
    }
}