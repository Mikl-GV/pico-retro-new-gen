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
#define I2S_FIFO_DEPTH 128                /* глубина TX FIFO, слов (FSTA=128 при пустом) */

// Синус 8-бит (256 точек, 0..255, центр 128) — генератор тест-тона.
// Используется с линейной интерполяцией (256 точек + дробная фаза дают
// подавление гармоник ниже −70 дБ: тон «чистый», без слышимой ступеньки).
static const uint8_t k_sin8[256] = {
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
// S.0 (стабилизация CPU2): управляющие флаги, которые CPU2 читает в горячем
// цикле, — тоже в SRAM A1 (r929: DRAM-видимость между ядрами ненадёжна, лог:
// CPU2 «флапал» — тон шёл рывками/шумом). 0xA4/0xA8 свободны (см. карту SRAM:
// 0x18..0x74, 0x88..0x8AC, 0x90, 0x94, 0x98..0xA0).
#define SRAM_AUDIO_PAUSED (*(volatile uint32_t*)0x000000A4u)  /* core0→CPU2: 1 = пауза/не писать FIFO */
#define SRAM_AUDIO_ACTIVE (*(volatile uint32_t*)0x000000A8u)  /* core0→CPU2: 1 = идёт игра/тон */
#define SRAM_AUDIO_KEY  0x5A5Au                              /* защита от мусора U-Boot */

// ---- Кольцо потока эмулятора (core0 → CPU2) ----
// ДАННЫЕ ring (L/R) — в .coherent (uncached через mmu_mark_uncached, только
// чтение CPU2 по адресам DRAM); ИНДЕКСЫ wr/rd и флаг sync — в SRAM A1 (r929:
// стенд-лог r928 показал, что направление CPU2→core0 через DRAM НЕ работает
// — CPU2 снял sync, core0 не увидел; SRAM A1 двунаправленно доказан живым
// статусами 0x90/0x94/beat).
// r932: буфер 65536 пар = 2^16 (1.37 с при 48 кГц) — по решению владельца
// «≥1 секунда». Расчёт реального максимума эмулятора: все системы 48к дают
// 800 пар/кадр × 60 = 48000 пар/с; GBA (32768 Гц + ресемпл) ~804 пар/кадр.
// Пик продюсера ≈ 48000 пар/с → 65536 = 1.37 с буфера (запас на синкопы
// загрузки ROM с USB/SD). Маска & (SIZE-1) корректна (2^16). Память: 65536
// пар × 2 кан × 2 байта = 256 КБ в .ring_buf 0x4A418000..0x4A500000 (~950 КБ —
// влезает; ASSERT в linker.ld проверит).
#define AUDIO_RING_SIZE 4096               /* пар; 2^16, ~1.37 с при 48 кГц */
#define SRAM_RING_L_ADDR 0x00001000u
#define SRAM_RING_R_ADDR 0x00003000u
static inline int16_t* ring_l(void) { return (int16_t*)SRAM_RING_L_ADDR; }
static inline int16_t* ring_r(void) { return (int16_t*)SRAM_RING_R_ADDR; }
// r957: ������ ������ � � SRAM A1: ����� ������� ����� DRAM ����� ������ ����� ����/������.

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

// r941 (аудит r918, P2): состояние DC-блокера CPU2 (однополюсный high-pass).
// Вынесено из локальных static i2s_poll_fill в .coherent, чтобы core0 мог
// сбрасывать его в i2s_ring_reset (иначе хвост прошлой игры протекал в новый
// контекст). CPU2 — единственный писатель, core0 — только reset, атомарность
// 32-бит записей достаточна для SPSC-обмена.
static int32_t s_dc_lp_l  __attribute__((section(".coherent"), aligned(32))) = 0;
static int32_t s_dc_lp_r  __attribute__((section(".coherent"), aligned(32))) = 0;
// Коэффициент DC-фильтра — битовый сдвиг α=2^-shift (однополюсный high-pass).
// fc = f_s / (2^shift · 2π): shift=6 → 48000/(64·2π) ≈ 119 Гц.
// i2s_dc_shift_set() переопределяет порог (используется тонами/настройками);
// дефолт 6 — единый «120 Гц» для всех хостов. Читается CPU2 один раз за
// вызов i2s_poll_fill.
static volatile int32_t s_dc_shift __attribute__((section(".coherent"), aligned(32))) = 6;
// r956: bypass DC-фильтра для ТЕСТА. 1 = в FIFO идёт «сырой» сэмпл (без ФВЧ) —
// так слышно именно тракт, без косметики фильтра. 0 (после zero-init .coherent)
// = штатный ФВЧ. Переключается только на время теста, из меню.
static volatile uint32_t s_dc_bypass __attribute__((section(".coherent"), aligned(32)));

// Диагностика CPU2 — СЧЁТЧИКИ в SRAM A1 (0xAC..0xB8, свободно после 0xA8).
// S.2: раньше были в .coherent (DRAM), а направление CPU2→core0 через DRAM
// НЕ работает (r928) → core0 печатал «замороженные» fw/skip/beat, лог врал.
// r956: стадия CPU2 — в SRAM A1, а не в .coherent (DRAM). Направление
// CPU2→core0 через DRAM недостоверно (r928), поэтому диагностика показывала
// «замороженное» значение. 0xC0 свободен (почта занята до 0xBC).
#define SRAM_AUDIO_STAGE (*(volatile uint32_t*)0x000000C0u)
#define SRAM_AUDIO_FW      (*(volatile uint32_t*)0x000000ACu)  /* CPU2→core0: пар записано в FIFO */
#define SRAM_AUDIO_SKIP    (*(volatile uint32_t*)0x000000B0u)  /* CPU2→core0: раз «FIFO полон» */
#define SRAM_AUDIO_BEAT    (*(volatile uint32_t*)0x000000B4u)  /* CPU2→core0: виток цикла */

static uint32_t g_drop_cnt = 0;            /* дропы продюсера (полное кольцо) — core0 */

// r904: g_volume_pct вынесен из .coherent — секция NOLOAD, startup.S её зануляет,
// и инициализатор `= 10` терялся → усилитель стартовал с 0% (нет звука до входа
// в настройки). Громкость читает ТОЛЬКО core0 (i2s_volume_pct/i2s_volume); CPU2
// (i2s_poll_fill) её не использует — поэтому обычный BSS с сохраняемым стартовым
// значением корректен. Оставлен в .coherent только g_i2s_ready (его CPU2 читает).
static int g_i2s_ready  __attribute__((section(".coherent"), aligned(64))) = 0;
// r959: дефолт громкости — 10% (по решению владельца; тракт проверен на 100%
// в тест-тоне, а в играх стартуем тихо и поднимаем в Settings → Volume).
static int g_volume_pct = 10;
static int g_muted = 1;                    /* усилитель выключен до init */
// S.0: гейт вывода эмуляторов. 0 = host-слои не пишут в ring (тон меню —
// через i2s_push_block — работает всегда). Включается после приёмки
// тест-тона (i2s_emulators_audio). core0-only, обычный BSS.
static int g_emulators_audio = 0;
void i2s_emulators_audio(int on) { g_emulators_audio = on ? 1 : 0; }
// r943: целевая высота кольца (пар) — уровень, до которого держит кольцо
// throttle, чтобы FIFO не пустел между кадрами. Индивидуален для эмулятора:
// ≈2 кадра его звука (напр. GBA 804 пар/кадр → 1600). core0-only, читается
// emu_throttle; в .coherent не нужен (CPU2 его не трогает).
static uint32_t s_ring_target = 1600;

// r949: счётчик ПОДРЯД идущих таймаутов ожидания места в кольце. Если CPU2 не
// выгребает (мёртв/в паузе), ждать 25-50 мс на КАЖДОМ сэмпле нельзя: кадр
// эмулятора (640-800 пар) превратился бы в минуты. После серии неудач переходим
// в «мягкий» режим: пишем только если место есть СРАЗУ, иначе дропаем сэмпл.
#define I2S_WAIT_FAIL_STREAK_MAX 8
static uint32_t s_wait_fail_streak = 0;

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

// r941 (аудит r918, C1): DC-блокер/фильтр настраивается через битовый сдвиг
// α=2^-shift. Хосты передают shift (6 → ~119 Гц на 48 кГц). Ранее это был
// no-op, а реальный фильтр в CPU2 всегда работал с α=1/1024 — заявленные
// «120 Гц» в коде не существовали. Теперь параметр влияет на высокочастотный
// фильтр CPU2 (читается тем же .coherent-словом).
void i2s_dc_shift_set(int shift) {
    if (shift < 1) shift = 1;
    if (shift > 30) shift = 30;
    s_dc_shift = shift;
    __asm volatile("dsb" ::: "memory");
}

// r956: bypass DC-фильтра (только для теста тракта). 1 = сырой сэмпл в FIFO,
// 0 = штатный ФВЧ. Флаг в .coherent читает CPU2; пишет только core0.
void i2s_dc_bypass(int on) {
    s_dc_bypass = on ? 1u : 0u;
    __asm volatile("dsb" ::: "memory");
}

// ---- Ring: продюсер (core0) ----
// r942 (аудит r918, P1): бэкпрессура. Продюсер (host) за кадр пушит пачку без
// учёта темпа CPU2; если средний темп чуть выше 48000 пар/с, ring накапливается
// до 65535 (1.36 с) → «эхо ~1 с» у Lynx/GG/SNES/NGP (fill=1..65535 в AUDIO-END).
// Решение: перед записью ждать освобождения места до порога заполнения, с
// таймаутом ~50 мс — при мёртвом CPU2/паузе не висеть вечно, а дропнуть
// (старое поведение). Связывает продюсера с темпом 48к естественно.
// Порог НИЗКИЙ (4800 пар ≈ 100 мс) — иначе кольцо держалось бы на 75% ёмкости
// (= 1 с латентности), а это то самое «эхо», которое мы убираем. 4800 согласовано
// с целевым хедрумом emu_throttle (r940).
#define I2S_RING_MAX_FILL 3200
#define I2S_BACKPRESSURE_TIMEOUT_US 50000u
static int i2s_ring_has_space(uint32_t need) {
    uint32_t wr = SRAM_RING_WR;
    uint32_t rd = SRAM_RING_RD;
    uint32_t fill = (wr - rd) & (AUDIO_RING_SIZE - 1);
    return fill <= (I2S_RING_MAX_FILL - need);
}
// Ожидает, что в кольцо влезет `need` пар (не более MAX_FILL). Возвращает 1,
// если место есть (или освободилось в таймаут), 0 — если таймаут (дроп).
// r943 (P1): защита от зависания CPU2. Следим за прогрессом SRAM_RING_RD —
// это SRAM A1, направление CPU2→core0 доказано рабочим (r929 sync). Если rd
// не двигается за половину таймаута — CPU2 не выгребает кольцо (завис/в паузе),
// дропаем раньше, не крутя 50 мс на каждом сэмпле (для блока 72000 пар это
// часы «зависания» — выглядело как «система встала через 2 мин»).
static int i2s_ring_wait(uint32_t need) {
    if (i2s_ring_has_space(need)) { s_wait_fail_streak = 0; return 1; }
    // r949: CPU2 уже показал, что не выгребает (серия таймаутов) — не ждём
    // вообще. Иначе каждый сэмпл кадра стоил бы 25-50 мс и эмулятор «вставал».
    if (s_wait_fail_streak >= I2S_WAIT_FAIL_STREAK_MAX) return 0;
    uint32_t t0 = h3_hs_timer_lo_us();
    uint32_t rd0 = SRAM_RING_RD;
    // Busy-wait: CPU2 освобождает место, пока продюсер ждёт (SPSC — без
    // гонок на индекс: пишет только core0, читает CPU2).
    while (!i2s_ring_has_space(need)) {
        if ((h3_hs_timer_lo_us() - t0) >= I2S_BACKPRESSURE_TIMEOUT_US) {
            if (s_wait_fail_streak < 0xFFFFFFFFu) s_wait_fail_streak++;
            return 0;
        }
        // rd не продвинулся за полтаймаута — CPU2 не выгребает (завис/пауза).
        if (SRAM_RING_RD == rd0 &&
            (h3_hs_timer_lo_us() - t0) >= (I2S_BACKPRESSURE_TIMEOUT_US / 2)) {
            if (s_wait_fail_streak < 0xFFFFFFFFu) s_wait_fail_streak++;
            return 0;
        }
        __asm volatile("nop; nop; nop; nop; nop; nop; nop; nop");
    }
    s_wait_fail_streak = 0;
    return 1;
}
void i2s_push_sample(int16_t left, int16_t right) {
    if (!g_i2s_ready) return;
    // S.0: вывод эмуляторов ОТКЛЮЧЁН (новый этап звука). Все host-слои идут
    // через i2s_push_sample; тон меню — через i2s_push_block (не затронут).
    // Включается i2s_emulators_audio(1) после приёмки тест-тона.
    if (!g_emulators_audio) return;
    if (!i2s_ring_wait(1)) { g_drop_cnt++; return; }   // таймаут — дроп (старое)
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
    ring_l()[wr] = left;
    ring_r()[wr] = right;
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
// r942 (P1): backpressure — ждём освобождения места под весь блок (до порога
// 75%), с таймаутом; иначе продюсер, пушащий пачки быстрее CPU2, накапливает
// ring до 65535 (эхо ~1 с). По таймауту — дроп остатка (старое поведение).
uint32_t i2s_push_block(const int16_t* pairs, uint32_t n) {
    if (!g_i2s_ready || !pairs) return 0;
    // Для очень больших n (n >= ёмкости) ждать весь блок нереально — ждём
    // максимум 75% ёмкости, избыток осядет в цикле записи (дроп по таймауту
    // не допустим: i2s_ring_wait в таком случае вернёт ложно сразу).
    uint32_t want = (n > I2S_RING_MAX_FILL) ? I2S_RING_MAX_FILL : n;
    // r955: таймаут здесь НЕ считается дропом. Цикл ниже сам учтёт все
    // непоместившиеся пары по next == SRAM_RING_RD; прежний `g_drop_cnt += n-want`
    // учитывал их дважды и завышал статистику при блокировках CPU2.
    (void)i2s_ring_wait(want);
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
        ring_l()[wr] = l;
        ring_r()[wr] = r;
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
    // r938: сброс TX FIFO (уничтожает «доезды»/хвост из FIFO после выхода).
    // По эталону r909 FLUSH_TX допустим ТОЛЬКО при выключенном TX_EN, иначе
    // FIFO клинит (TXE_CNT=0, долив встаёт). Здесь reset вызывается при
    // выходе из игры/тона — аппарат не играет, выключаем TX_EN → flush →
    // включаем. Безопасно в моменте смены контекста.
    // r941 (аудит r918, P1): поднимаем паузу ДО отключения TX_EN,
    // чтобы CPU2 (i2s_poll_fill) гарантированно не писал I2S_FIFO_TX между
    // снятием TX_EN и FLUSH_TX — иначе запись после flush «доезжает» старым
    // сэмплом в начало паузы/следующей игры (гонка двух ядер на один MMIO).
    SRAM_AUDIO_PAUSED = 1;
    __asm volatile("dsb" ::: "memory");
    {
        volatile uint32_t* ctrl = &I2S_CTRL;
        uint32_t c = *ctrl & ~(1u << 2);   // TX_EN off
        *ctrl = c;
        __asm volatile("dsb" ::: "memory");
        I2S_FIFO_CTL |= (1u << 25);         // FLUSH_TX
        __asm volatile("dsb" ::: "memory");
        I2S_FIFO_CTL &= ~(1u << 25);
        *ctrl = c | (1u << 2);              // TX_EN back on
        __asm volatile("dsb" ::: "memory");
    }
    // r937: СТРОГИЙ handshake. Устанавливаем g_audio_paused_f=1 (CPU2 точно
    // не делает ring_consume), ставим sync; CPU2 подтягивает rd=wr и снимает
    // sync; затем core0 пишет wr=rd и снимает pause. Это исключает «мусор
    // после нагрузок»: раньше sync снимался, а CPU2 между чтением rd и записью
    // wr успевал ring_consume → после wr=rd в «пустом» кольце оставались
    // старые данные, которые CPU2 проигрывал как звук.
    // r941 (аудит r918, P2): обнуляем DC-состояние CPU2 — детерминированная
    // тишина в начале нового контекста (игры/тона), без хвоста прошлого
    // сигнала. Пока pause=1 CPU2 не пишет FIFO, гонки на сброс нет.
    s_dc_lp_l = 0;
    s_dc_lp_r = 0;
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
    if (SRAM_RING_SYNC) {
        // r954: таймаут — CPU2 не ответил (мёртв/занят). Флаг обязан быть снят
        // САМИМ core0: иначе CPU2, ожив позже, увидит SYNC и молча подтянет
        // RD=WR, уничтожив уже поданные после сброса данные (щелчок/обрыв).
        // WR при этом НЕ трогаем — поток, начатый после сброса, остаётся целым.
        SRAM_RING_SYNC = 0;
        __asm volatile("dsb" ::: "memory");
    } else {
        SRAM_RING_WR = SRAM_RING_RD;
    }
    __asm volatile("dsb" ::: "memory");
    // r934: НЕ заполняем всё кольцо тишиной (1.37 с мёртвого звука). r937:
    // благодаря pause CPU2 не расходует кольцо во время сброса — данные того,
    // что было, не проигрываются. Кольцо «пустое» (wr=rd), silence-fill
    // подольёт тишину при реальной паузе продюсера.
    SRAM_AUDIO_PAUSED = 0;
    __asm volatile("dsb" ::: "memory");
    g_drop_cnt = 0;
    // r955: сбросить серию таймаутов ожидания места. Иначе после сессии, где
    // CPU2 стоял в паузе, streak остаётся >= 8 и СЛЕДУЮЩИЙ запуск сразу идёт
    // в «мягком» режиме (пишем только при мгновенно свободном месте, остальное
    // дропаем) — игра стартует с дырками в звуке.
    s_wait_fail_streak = 0;
}

// Ринг-свободные пары (для CPU2).
static inline uint32_t ring_count(void) {
    return (uint32_t)(SRAM_RING_WR - SRAM_RING_RD) & (AUDIO_RING_SIZE - 1);
}
static inline void ring_consume(uint32_t n) {
    SRAM_RING_RD = (SRAM_RING_RD + n) & (AUDIO_RING_SIZE - 1);
}

// r940: текущее заполнение кольца (пар) — для адаптивного throttle продюсера.
uint32_t i2s_ring_fill(void) {
    return (uint32_t)(SRAM_RING_WR - SRAM_RING_RD) & (AUDIO_RING_SIZE - 1);
}

// r943: целевая высота кольца (пар) для throttle — per-system (≈2 кадра звука
// эмулятора). Если эмулятор пушит пачку за кадр (NES/Dandy 800 пар/кадр) и
// кольцо держать на этой высоте, FIFO не пустеет между кадрами (нет under-
// мусора). Клаmp к верхнему порогу заполнения (не дать латентность >эха).
void i2s_ring_set_target(uint32_t pairs) {
    if (pairs < 64) pairs = 64;
    if (pairs > I2S_RING_MAX_FILL / 2) pairs = I2S_RING_MAX_FILL / 2;
    s_ring_target = pairs;
}
uint32_t i2s_ring_target(void) {
    return s_ring_target;
}

// r944 (адаптивный target): хост сообщает фактическое число пар, поданных за
// последний кадр. Из него ведём EMA среднего темпа потока → целевая высота
// кольца = 2 кадра этого темпа. ЭТО автоматически учитывает региональную
// частоту кадра: NTSC 60 Гц (~800 пар), PAL/Dendy 50 Гц (~960 пар), A2600 nt/pal
// — не нужно захардкоживать частоту на эмулятор. Кольцо держится на 2-кадровом
// запасе → FIFO не пустеет между кадрами (меньше under-мусора), без накопления
// многокадровой латентности.
// EMA-α: берём ~32 кадра сходимости. Кламп к I2S_RING_MAX_FILL/2.
static uint32_t s_ring_ema = 0;     // EMA пар/кадр
void i2s_ring_observe_frame(uint32_t pairs) {
    if (pairs == 0) return;         // кадр без звука не «сбрасывает» темп
    if (s_ring_ema == 0) { s_ring_ema = pairs; }
    else { s_ring_ema = (s_ring_ema * 31 + pairs) / 32; }
    uint32_t t = s_ring_ema * 2;     // 2 кадра запаса
    if (t < 128) t = 128;
    if (t > I2S_RING_MAX_FILL / 2) t = I2S_RING_MAX_FILL / 2;
    s_ring_target = t;
}

// ---- CPU2: долив ring → TX FIFO ----
// Переносит пары, пока в FIFO есть место (TXE_CNT >= порог) и кольцо не
// пусто. Возвращает число перенесенных пар. Темп — аппаратный FIFO.
#define I2S_FLUSH_BURST 24                  /* макс. пар за один вызов */
static int s_was_empty = 0;                 /* Фаза 0 (r910): переход в пустоту */
int i2s_poll_fill(void) {
    // r953 (КОРЕНЬ «ядро 2 не забирает»): g_i2s_ready живёт в .coherent (DRAM),
    // а видимость DRAM core0→CPU2 на этой плате НЕ гарантирована (r928: то же
    // самое для обратного направления). Стенд-лог r952: `fw_delta=0` при живом
    // `beat` — CPU2 выходил ИМЕННО здесь. Готовность проверяем по SRAM-дублю
    // (0x94), который пишет i2s_init и который CPU2 видит всегда.
    if (!g_i2s_ready && !*(volatile uint32_t*)I2S_SRAM_READY_ADDR) return 0;
    // r941 (аудит r918, P1): пока идёт i2s_ring_reset (g_audio_paused_f=1,
    // TX_EN off → FLUSH_TX → TX_EN on), НЕ пишем в TX FIFO вообще — иначе
    // запись между снятием TX_EN и FLUSH съедается флешем либо «доезжает»
    // старым сэмплом. Это второй рубеж: первый — pause уже стоит ДО снятия
    // TX_EN в i2s_ring_reset.
    if (i2s_audio_paused()) return 0;
    int n = 0;
    SRAM_AUDIO_STAGE = 2;
    while (n < I2S_FLUSH_BURST) {
        // r955: пауза могла подняться ПОСРЕДИ burst — core0 готовит прямой
        // тест-тон и сейчас выключит TX_EN + FLUSH_TX. Проверяем на каждой
        // итерации: два писца FIFO_TX не должны пересечься ни на одной паре.
        if (i2s_audio_paused()) break;
        // Свободных слов в TX FIFO. По даташиту H3 (I2S/PCM_FSTA, стр.534):
        // [23:16] = TXE_CNT «Empty Space Word Counter», [28] = TXE (есть место).
        // При пустом FIFO = 128 (глубина). Это ВЕРНОЕ поле (r912-сдвиг — ошибка).
        uint32_t free_words = I2S_FSTA_TXE_CNT(I2S_FIFO_STA);
        if (free_words < I2S_TXE_MIN * 2) {   // < 4 пары — место есть, но не лезем на границу
            if (free_words < 2) {             // совсем нет места — подождать
                SRAM_AUDIO_SKIP++;
                break;
            }
            free_words -= (free_words & 1);   // чётное число слов
        }
        uint32_t avail = ring_count();
        if (!avail) {
            // r940: СИЛЬНЫЙ silence-fill ОТКЛЮЧЁН. Стенд-лог r938: тест-тон
            // звучал 40-50 Гц «рывками» — silence-fill (r936) заливал TX FIFO
            // тишиной (до 66 слов), и когда продюсер (тон) начинал подавать
            // данные, FIFO был ПОЛОН тишины → CPU2 стоял в `skip` (8М), данные
            // кольца тонули, звук «рычал» паузами. Убираем подмес тишины ВООБЩЕ:
            // underrun лечится бо́льшим кольцом (65536) и точным темпом, а не
            // силие-заполнением. Если кольцо пусто и FIFO пуст — FIFO сам
            // отдаст тишину (аппаратно), паузы маскируются DC-блокером.
            if (g_stat_active && !s_was_empty) { g_stat_underrun++; s_was_empty = 1; }
            break;
        }
        s_was_empty = 0;
        uint32_t take = avail;
        uint32_t maxpairs_by_fifo = free_words / 2;
        if (take > maxpairs_by_fifo) take = maxpairs_by_fifo;
        if (take <= 0) break;
        if ((uint32_t)take > (uint32_t)(I2S_FLUSH_BURST - n)) take = I2S_FLUSH_BURST - n;

        // r953: барьер перед чтением данных кольца. Индексы — SRAM, данные —
        // DRAM; без dmb чтение данных может опередить чтение индекса, и CPU2
        // берёт устаревшие сэмплы — слышно как хрип/дубль.
        __asm volatile("dmb" ::: "memory");
        uint32_t rd = SRAM_RING_RD;
        // DC-блокер (high-pass, однополюсный) — гасит гул/фон тишины
        // (DC-смещение от микса/усиления). Состояние — CPU2 ведёт фильтр
        // непрерывно, один вызов на пару. Коэффициент α=2^-shift настраивается
        // i2s_dc_shift_set (дефолт 6 → 48000/64/2π ≈ 119 Гц). Не влияет на
        // темп/синхронизацию (только на значение).
        // DC-блокер (однополюсный high-pass): из входа вычитается оценка
    // низкочастотной/постоянной составляющей (low-pass). Раньше в FIFO уходил
    // САМ low-pass (`s_dc_lp_*`) — при fc≈119 Гц (shift=6) 440/1000/4000 Гц
    // резались на −12…−30 дБ, тон «бубнил» и был тихим. Теперь:
    //   out = in − lp   (истинный high-pass, DC/инфразвук срезаются, тон почти
    //   не ослабляется выше fc).
        // r952: кламп. s_dc_shift живёт в .coherent (NOLOAD), startup.S её
        // зануляет — инициализатор `= 6` не применяется, и без клампа
        // dcsh=0 даёт `out = l - l = 0`: CPU2 «выводит» чистые нули (лог:
        // fw_delta=57600 при полной тишине). Мусор/0 → дефолт 6, не тишина.
        int32_t dcsh = s_dc_shift;
        uint32_t bypass = s_dc_bypass;   /* r956: тест — без ФВЧ */
        if (dcsh < 1) dcsh = 6;
        if (dcsh > 30) dcsh = 30;
        for (uint32_t k = 0; k < take; k++) {
            uint32_t idx = (rd + k) & (AUDIO_RING_SIZE - 1);
            // Формат FIFO (TXIM=0): пара = ДВА 32-бит слова [L][R], сэмпл в
            // СТАРШИХ битах (16-бит << 16), младшие 0 — как эталон (r735).
            // r935: ЯВНЫЙ порядок L-stwo, R-stwo (независимо от компилятора):
            // (uint16_t)(int16_t) → sign-extend, <<16 → MSB-выровнен. Оба слова
            // 32-бит → каждый сэмпл в отдельном FIFO-слоте: сдвига бит нет.
            int32_t l = (int32_t)(int16_t)ring_l()[idx];
            int32_t r = (int32_t)(int16_t)ring_r()[idx];
            s_dc_lp_l += bypass ? 0 : ((l - s_dc_lp_l) >> dcsh);   // оценка DC/низов (low-pass)
            s_dc_lp_r += ((r - s_dc_lp_r) >> dcsh);
            int32_t out_l = bypass ? l : (l - s_dc_lp_l);            // high-pass: in − lp
            int32_t out_r = bypass ? r : (r - s_dc_lp_r);
            I2S_FIFO_TX = (uint32_t)(uint16_t)(int16_t)out_l << 16;
            I2S_FIFO_TX = (uint32_t)(uint16_t)(int16_t)out_r << 16;
        }
        ring_consume(take);
        n += take;
        SRAM_AUDIO_FW += take;   // S.2: чётчик — в SRAM (виден core0, направление CPU2→core0 надёжно)
    }
    SRAM_AUDIO_STAGE = 0;
    return n;
}

void i2s_flush(void) { i2s_poll_fill(); }

// ---- Почта CPU2 (команды core0) — в SRAM A1 (надёжная межьядерная видимость).
#define SRAM_AUDIO_CMD (*(volatile uint32_t*)0x000000B8u)  /* core0→CPU2: команда */
#define SRAM_AUDIO_ACK (*(volatile uint32_t*)0x000000BCu)  /* CPU2→core0: подтверждение */
void i2s_audio_cmd(uint32_t cmd) { SRAM_AUDIO_CMD = cmd; __asm volatile("dsb" ::: "memory"); }
void i2s_audio_poll_cmd(void) {
    uint32_t cmd = SRAM_AUDIO_CMD;
    if (cmd == AUDIO_CMD_NONE) return;
    if (cmd == AUDIO_CMD_PAUSE) SRAM_AUDIO_PAUSED = 1;
    // r925: RESUME НЕ вызывает i2s_ring_reset — тот ставит SRAM_RING_SYNC=1 и
    // ждёт снятия от CPU2, а CPU2 сейчас внутри poll_cmd (до main-цикла не
    // дойдёт) → взаимная блокировка (r924-зависание на старте после «I2S:
    // ready»). RESUME только снимает паузу; сброс кольца делает core0 напрямую
    // (в i2s_test_tone*/emu_prepare), где он и должен быть.
    else if (cmd == AUDIO_CMD_RESUME) { SRAM_AUDIO_PAUSED = 0; }
    SRAM_AUDIO_CMD = AUDIO_CMD_NONE;
    SRAM_AUDIO_ACK = cmd;
}
void i2s_audio_set_state(int on) { g_audio_state = on ? 1 : 0; }
void i2s_audio_set_active(int on) { SRAM_AUDIO_ACTIVE = on ? 1 : 0; g_audio_active = on ? 1 : 0; }  /* r916: флаг «в игре» для CPU2 — SRAM (надёжно) */
void i2s_audio_set_beat(uint32_t b) { SRAM_AUDIO_BEAT = b; }
int  i2s_audio_core_active(void) { return (int)g_audio_state; }
uint32_t i2s_audio_beat(void) { return SRAM_AUDIO_BEAT; }
int  i2s_audio_paused(void) { return (int)SRAM_AUDIO_PAUSED; }

// ---- Прочее API (сохранено из совместимости) ----
void i2s_click(void) {
    // Короткий клик меню — 10 пар 1 кГц через ring (не блокирует).
    if (!g_i2s_ready) return;
    for (int i = 0; i < 10; i++) {
        int16_t s = (int16_t)(((i & 1) ? 6000 : -6000));
        i2s_push_sample(s, s);
    }
}

// ================= ПРЯМОЙ ТЕСТ-ТОН (r948) =================
// Тон для меню и для старта выводится НАПРЯМУЮ в TX FIFO с core0 — без кольца
// и без CPU2. Смысл: тест проверяет тракт «I2S0 + усилитель», а не
// межъядерный канал. Раньше тон шёл ring → CPU2, поэтому тишина в тесте
// означала одну из трёх разных неисправностей (мёртвый CPU2 / пустое кольцо /
// неверный темп подачи) с одинаковым симптомом.
//
// Принцип: темп задаёт АППАРАТНЫЙ FIFO. Пара пишется, только когда в FIFO есть
// 2 свободных слова (TXE_CNT >= 2). FIFO дренируется ровно на 48 кГц, поэтому
// цикл сам идёт в темпе вывода: ни udelay, ни таймеров, ни недоливов — тон
// сплошной. На время тона CPU2 ставится на паузу, единственный писатель
// FIFO = core0 (гонки двух ядер на один регистр нет).

static int i2s_fifo_free_words(void) {
    return (int)I2S_FSTA_TXE_CNT(I2S_FIFO_STA);
}

// 1 = в FIFO не меньше words свободных слов (дождались), 0 = таймаут.
static int i2s_fifo_wait_free(int words, uint32_t timeout_us) {
    uint32_t t0 = h3_hs_timer_lo_us();
    for (;;) {
        if (i2s_fifo_free_words() >= words) return 1;
        if ((h3_hs_timer_lo_us() - t0) > timeout_us) return 0;
    }
}

// Сброс TX FIFO. FLUSH_TX допустим только при выключенном TX_EN (иначе FIFO
// клинит), поэтому выключаем передачу на время флеша и включаем обратно.
static void i2s_fifo_flush_tx(void) {
    uint32_t c = I2S_CTRL & ~I2S_CTRL_TX_EN;
    I2S_CTRL = c;
    __asm volatile("dsb" ::: "memory");
    I2S_FIFO_CTL |= I2S_FIFO_CTL_FLUSH_TX;
    __asm volatile("dsb" ::: "memory");
    I2S_FIFO_CTL &= ~I2S_FIFO_CTL_FLUSH_TX;
    I2S_CTRL = c | I2S_CTRL_TX_EN;
    __asm volatile("dsb" ::: "memory");
}

// Проверка, что FIFO РЕАЛЬНО сливается: пустой FIFO отдаёт TXE_CNT = глубина,
// после записи 8 слов счётчик должен вернуться к глубине за 50 мс.
// 1 = такт есть и FIFO выгребается, 0 = блок не тактируется/заперт.
int i2s_selftest(void) {
    if (!g_i2s_ready) return 0;
    uint32_t t0 = h3_hs_timer_lo_us();
    while (i2s_fifo_free_words() < I2S_FIFO_DEPTH) {
        if ((h3_hs_timer_lo_us() - t0) > 50000u) return 0;
    }
    for (int i = 0; i < 8; i++) I2S_FIFO_TX = 0;
    __asm volatile("dsb" ::: "memory");
    t0 = h3_hs_timer_lo_us();
    while (i2s_fifo_free_words() < I2S_FIFO_DEPTH) {
        if ((h3_hs_timer_lo_us() - t0) > 50000u) return 0;
    }
    return 1;
}

// Прямой вывод синуса hz Гц длительностью ms мс.
// 0 = проигран, -1 = I2S не готов (init/PLL), -2 = FIFO не сливается.
// Громкость на время теста принудительно 100% (проверяем тракт, а не
// настройку), усилитель — включён; после теста состояние восстанавливается.
static int i2s_tone_direct(int hz, int ms) {
    if (!g_i2s_ready) return -1;
    if (hz <= 0 || ms <= 0) return -1;

    i2s_audio_cmd(AUDIO_CMD_PAUSE);
    udelay(3000);
    SRAM_AUDIO_PAUSED = 1;                 // гарантия, даже если CPU2 не ответил
    __asm volatile("dsb" ::: "memory");

    int old_muted = g_muted, old_vol = g_volume_pct;
    g_muted = 0;
    g_volume_pct = 100;
    i2s_apply_amp();
    udelay(3000);                          // MAX98357A выходит из shutdown

    uint32_t written = 0;
    int rc = 0;
    if (!i2s_selftest()) {
        rc = -2;
    } else {
        i2s_fifo_flush_tx();
        uint32_t step  = (uint32_t)(((uint64_t)hz << 32) / 48000ull);
        uint32_t total = (uint32_t)ms * 48u;          // пар @48 кГц
        uint32_t phase = 0;
        uint32_t t0 = h3_hs_timer_lo_us();
        uint32_t deadline = (uint32_t)ms * 2500u + 100000u;   // ~2.5× длительности
        while (written < total) {
            if ((h3_hs_timer_lo_us() - t0) > deadline) { rc = -2; break; }
            if (!i2s_fifo_wait_free(2, 50000u)) { rc = -2; break; }
            // Синус с линейной интерполяцией по 8-битной таблице: ступенька
            // таблицы размывается, гармоники уходят ниже −70 дБ.
            uint32_t idx  = (phase >> 24) & 0xFFu;
            uint32_t frac = (phase >> 16) & 0xFFu;
            int32_t a = (int32_t)k_sin8[idx] - 128;
            int32_t b = (int32_t)k_sin8[(idx + 1u) & 0xFFu] - 128;
            int32_t v = a + (((b - a) * (int32_t)frac) >> 8);
            int16_t s = (int16_t)(v << 7);            // −16384..16256, полушкала
            I2S_FIFO_TX = (uint32_t)(uint16_t)s << 16;
            I2S_FIFO_TX = (uint32_t)(uint16_t)s << 16;
            phase += step;
            written++;
        }
        // Хвост: 64 пары тишины (1.3 мс) — без щелчка на срезе тона.
        if (rc == 0) {
            for (int i = 0; i < 64; i++) {
                if (!i2s_fifo_wait_free(2, 50000u)) break;
                I2S_FIFO_TX = 0;
                I2S_FIFO_TX = 0;
            }
        }
        // Дать доиграть остаток FIFO, затем очистить: хвост прошлого тона не
        // должен «доехать» в начало следующего.
        {
            uint32_t tw = h3_hs_timer_lo_us();
            while (i2s_fifo_free_words() < I2S_FIFO_DEPTH &&
                   (h3_hs_timer_lo_us() - tw) < 5000u) ;
        }
        i2s_fifo_flush_tx();
    }

    g_muted = old_muted;
    g_volume_pct = old_vol;
    i2s_apply_amp();
    SRAM_AUDIO_PAUSED = 0;
    __asm volatile("dsb" ::: "memory");
    i2s_audio_cmd(AUDIO_CMD_RESUME);

    printf("TONE: hz=%d ms=%d rc=%d wrote=%u/%u free=%d beat=%u\n",
           hz, ms, rc, (unsigned)written, (unsigned)((uint32_t)ms * 48u),
           i2s_fifo_free_words(), (unsigned)i2s_audio_beat());
    return rc;
}

// Тест-тон на старте: 200 мс 1 кГц — проверка тракта до входа в меню
// (вызывается из main.c после i2s_init()).
void i2s_test_tone(void) { (void)i2s_tone_direct(1000, 200); }

// API меню настроек «Audio test tone»: тон заданной частоты и длительности.
// Возвращает код i2s_tone_direct (0 = проигран, -1 = I2S не готов,
// -2 = FIFO не сливается) — меню показывает результат на экране.
int i2s_test_tone_freq(int hz, int ms) {
    return i2s_tone_direct(hz, ms);
}

// ---- r951: тот же тон, но выводит CPU2 (кольцо → аудио-ядро → FIFO) ----
// Нужен, чтобы отличать «тракт I2S+усилитель» (прямой тест) от «межъядерного
// канала» (этот тест). Монополист TX FIFO здесь строго CPU2: core0 пишет
// только в кольцо, а пары в FIFO переносит i2s_poll_fill на CPU2.
// Tempo задаёт железо; core0 лишь держит кольцо заполненным (≤ половины
// I2S_RING_MAX_FILL), поэтому недолива по построению нет.
// Возврат: 0 = проигран, -1 = I2S не готов, -2 = нет прогресса (CPU2/кольцо).
int i2s_test_tone_cpu2(int hz, int ms) {
    if (!g_i2s_ready) return -1;
    if (hz <= 0 || ms <= 0) return -1;
    if (!i2s_selftest()) return -2;      // нет такта — и через CPU2 не заиграет

    // r952: DC-фильтр CPU2 — на дефолт (119 Гц). Без этого в тесте до первого
    // входа в игру s_dc_shift = 0 (.coherent обнуляется) → в FIFO уходят нули.
    i2s_dc_shift_set(6);

    // 1) Снять паузу: FIFO доливает CPU2.
    SRAM_AUDIO_PAUSED = 0;
    __asm volatile("dsb" ::: "memory");
    i2s_audio_cmd(AUDIO_CMD_RESUME);
    udelay(2000);

    // 2) Опустошить кольцо. RD принадлежит CPU2, поэтому на миг ставим паузу,
    //    выравниваем WR по RD и сразу снимаем — без SYNC-хендшейка (он и был
    //    источником дедлоков при мёртвом CPU2).
    SRAM_AUDIO_PAUSED = 1;
    __asm volatile("dsb" ::: "memory");
    udelay(1000);
    SRAM_RING_WR = SRAM_RING_RD;
    __asm volatile("dsb" ::: "memory");
    SRAM_AUDIO_PAUSED = 0;
    __asm volatile("dsb" ::: "memory");

    // 3) Усилитель на 100% на время теста (после — вернуть как было).
    int old_muted = g_muted, old_vol = g_volume_pct;
    g_muted = 0;
    g_volume_pct = 100;
    i2s_apply_amp();
    udelay(3000);

    // r956: тест тракта — без DC-фильтра (никакой «косметики»): слышно ровно
    // то, что отдаёт кольцо. После теста ФВЧ возвращается.
    i2s_dc_bypass(1);
    uint32_t fw0  = SRAM_AUDIO_FW;             // пар, выведенных CPU2 до теста
    uint32_t total = (uint32_t)ms * 48u;       // пар @48 кГц
    uint32_t written = 0;
    uint32_t step  = (uint32_t)(((uint64_t)hz << 32) / 48000ull);
    uint32_t phase = 0;
    uint32_t t0 = h3_hs_timer_lo_us();
    uint32_t deadline = (uint32_t)ms * 3000u + 300000u;   // ~3× длительности
    int rc = 0;
    static int16_t blk[256 * 2];

    while (written < total) {
        if ((h3_hs_timer_lo_us() - t0) > deadline) { rc = -2; break; }
        // Держим кольцо не выше половины верхнего порога: CPU2 успевает,
        // задержка не растёт, а FIFO не пустеет.
        if (i2s_ring_fill() > (I2S_RING_MAX_FILL / 2)) {
            __asm volatile("nop; nop; nop; nop; nop; nop; nop; nop");
            continue;
        }
        uint32_t nb = total - written;
        if (nb > 256u) nb = 256u;
        for (uint32_t j = 0; j < nb; j++) {
            uint32_t idx  = (phase >> 24) & 0xFFu;
            uint32_t frac = (phase >> 16) & 0xFFu;
            int32_t a = (int32_t)k_sin8[idx] - 128;
            int32_t b = (int32_t)k_sin8[(idx + 1u) & 0xFFu] - 128;
            int32_t v = a + (((b - a) * (int32_t)frac) >> 8);
            int16_t s = (int16_t)(v << 7);
            blk[2 * j] = s;
            blk[2 * j + 1] = s;
            phase += step;
        }
        uint32_t got = i2s_push_block(blk, nb);
        written += got;
        // r957: если в кольцо влезло меньше nb (backpressure/дроп), фазу надо
        // откатить на неподанные пары — иначе синус «скачет» вперёд и тон
        // трещит даже после того, как данные доходят целиком.
        if (got < nb) phase -= step * (nb - got);
        if (got == 0) udelay(200);             // CPU2 не берёт — не долбить шину
    }

    // 4) Дать CPU2 доиграть остаток кольца (≤ ~100 мс), затем хвост FIFO.
    {
        uint32_t tw = h3_hs_timer_lo_us();
        while (i2s_ring_fill() && (h3_hs_timer_lo_us() - tw) < 200000u) ;
    }
    udelay(20000);
    uint32_t fw1 = SRAM_AUDIO_FW;
    i2s_dc_bypass(0);                          // вернуть штатный ФВЧ

    g_muted = old_muted;
    g_volume_pct = old_vol;
    i2s_apply_amp();

    // fw_delta — сколько пар реально вывел CPU2. Если он меньше written
    // заметно, значит аудио-ядро не выгребает кольцо (и это НЕ вина тракта).
    printf("TONE-CPU2: hz=%d ms=%d wrote=%u/%u fw_delta=%u fill=%u fsta=%08x beat=%u rc=%d\n",
           hz, ms, (unsigned)written, (unsigned)total,
           (unsigned)(fw1 - fw0), (unsigned)i2s_ring_fill(),
           (unsigned)I2S_FIFO_STA, (unsigned)i2s_audio_beat(), rc);
    return rc;
}

// ---- Диагностика ----
// S.2: счётчики CPU2 читаем из SRAM (направление CPU2→core0 через DRAM не
// работает, r928) — здесь только тонкие обёртки, чтобы внешний API не менялся.
void i2s_cpu2_diag(uint32_t* flush, uint32_t* written, uint32_t* skipfull, uint32_t* dummy) {
    if (flush) *flush = 0;                       // g_flush_enter считали счётчиком входов — не используется
    if (written) *written = SRAM_AUDIO_FW;
    if (skipfull) *skipfull = SRAM_AUDIO_SKIP;
    if (dummy) *dummy = g_drop_cnt;
}
void i2s_cpu2_pairs_written_get(uint32_t* v) { if (v) *v = SRAM_AUDIO_FW; }
void i2s_cpu2_stage_get(uint32_t* st, uint32_t* en, uint32_t* ex) {
    if (st) *st = SRAM_AUDIO_STAGE;
    if (en) *en = 0;
    if (ex) *ex = 0;
}
void i2s_ring_wr_rd_get(uint32_t* w, uint32_t* r) {
    if (w) *w = SRAM_RING_WR;
    if (r) *r = SRAM_RING_RD;
}
void i2s_flush_diag_get(uint32_t* nempty, uint32_t* nempty_full) {
    if (nempty) *nempty = 0;
    if (nempty_full) *nempty_full = SRAM_AUDIO_SKIP;
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
    // r952: .coherent — NOLOAD, startup.S её зануляет, поэтому инициализатор
    // `s_dc_shift = 6` в памяти не появляется. Ставим дефолт явно, иначе
    // DC-фильтр CPU2 с shift=0 превращает любой звук в нули.
    s_dc_shift = 6;
    s_dc_lp_l = 0;
    s_dc_lp_r = 0;
    SRAM_RING_WR = 0;
    SRAM_RING_RD = 0;
    SRAM_RING_SYNC = 0;
    SRAM_AUDIO_PAUSED = 0;   // S.0: флаги CPU2 в SRAM — обнулить при старте (SRAM не zero-init)
    SRAM_AUDIO_ACTIVE = 0;
    SRAM_AUDIO_FW = 0;     // S.2: счётчики CPU2 — старт с нуля
    SRAM_AUDIO_SKIP = 0;
    SRAM_AUDIO_BEAT = 0;
    SRAM_AUDIO_CMD = AUDIO_CMD_NONE;
    SRAM_AUDIO_ACK = AUDIO_CMD_NONE;
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

    // r949: вывод звука эмуляторов включён ГЛОБАЛЬНО. Гейт g_emulators_audio
    // введён на этапе тест-тона (S.0), чтобы изолировать тракт; тон доказан
    // на стенде (r948), поэтому host-слои снова пишут в кольцо. Тест-тон это
    // не затрагивает: он идёт прямым доливом FIFO, мимо гейта.
    i2s_emulators_audio(1);

    printf("I2S: ready (48k stereo, PLL 24.576M), emu audio ON\n");
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