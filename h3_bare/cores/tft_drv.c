// tft_drv.c — TFT-ядро DFR0428 (ILI9486, 480×320 RGB565)
// через 74HC4094×2 + 74HC4040 → параллельная шина D0–D15.
// SPI0 H3 (PC0=MOSI, PC1=MISO, PC2=SCLK, PC3=CS дисплея, PC7=DC),
// PA21=CS тача **TSC2046I** (аналог XPT2046), PA2=RST. CPU1, MMU off.
//
// Ключевые находки по ходу отладки (r1..r28):
//   1) SPI_RXD на 0x300, а не 0x204 — иначе MISO читается как 0.
//   2) TF_CNT (H3 SPI0) НЕ УБЫВАЕТ после XCH — монотонный счётчик.
//      При 64 навсегда встаёт wait_tx_room. Фикс: FCR TX_FIFO_RST
//      ПОСЛЕ каждого байта (уже отправлен — wait_done).
//   3) DFR0428: два 74HC4094 каскадом: первый байт SPI → D8–D15,
//      второй → D0–D7. 16 тактов под CS, подъём CS = лэтч.
//      Команда ILI9486 (RS=0) читается с D0–D7 → вторым байтом.
//      Пиксель на шине: [старший байт, младший] (hi первым).
//      Mode 0 (CPOL=0, CPHA=0).
//   4) Тач = TSC2046I на PA21, MISO на PC1, протокол XPT2046 Mode 1.

#include <stdint.h>
#include <string.h>
#include "h3.h"
#include "h3_ccu.h"
#include "h3_hs_timer.h"
#include "tft_drv.h"
#include "uart.h"

extern int printf(const char*, ...);

// Локальная копия шрифта 8x8 (ASCII 0x20..0x7F) — не зависим от
// font8x8 из fb_text.c и его секции/линковки.
static const uint8_t tft_font[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x18,0x18,0x18,0x00,0x18,0x00,0x00},
    {0x6C,0x6C,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},
    {0x00,0x66,0xAC,0x18,0x34,0x6A,0x00,0x00},
    {0x38,0x6C,0x68,0x76,0xDC,0xCE,0x7A,0x00},
    {0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    {0x18,0x7E,0x3C,0xFF,0x3C,0x7E,0x18,0x00},
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x18,0x18,0x30,0x00},
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00},
    {0x02,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},
    {0x3C,0x66,0x76,0x7E,0x6E,0x66,0x3C,0x00},
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00},
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    {0x0C,0x1C,0x2C,0x4C,0x7E,0x0C,0x0C,0x00},
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    {0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    {0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00},
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00},
    {0x00,0x00,0x18,0x00,0x18,0x18,0x30,0x00},
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00},
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00},
    {0x3C,0x66,0x0C,0x18,0x00,0x18,0x00,0x00},
    {0x3C,0x42,0x99,0xBD,0xB5,0x99,0x42,0x3C},
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00},
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00},
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x66,0x66,0x66,0x6E,0x3E,0x00},
    {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00},
    {0x3C,0x66,0x70,0x3C,0x0E,0x66,0x3C,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},
    {0xC6,0xC6,0xC6,0xD6,0x7E,0x6C,0x44,0x00},
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    {0x40,0x60,0x30,0x18,0x0C,0x06,0x02,0x00},
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x18,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00},
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00},
    {0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00},
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00},
    {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00},
    {0x1C,0x30,0x7C,0x30,0x30,0x30,0x30,0x00},
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x7C},
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00},
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    {0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x70},
    {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00},
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0x00,0x00,0xEC,0xFE,0xD6,0xC6,0xC6,0x00},
    {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00},
    {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00},
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60},
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06},
    {0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00},
    {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00},
    {0x30,0x30,0x7C,0x30,0x30,0x30,0x1C,0x00},
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00},
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00},
    {0x00,0x00,0xC6,0xC6,0xD6,0x7E,0x6C,0x00},
    {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00},
    {0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x7C},
    {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00},
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},
    {0x00,0x00,0x62,0x92,0x8C,0x00,0x00,0x00},
};

#define CMD_SWRESET  0x01
#define CMD_SLPOUT   0x11
#define CMD_INVON    0x21
#define CMD_COLMOD   0x3A
#define CMD_DISPON   0x29
#define CMD_CASET    0x2A
#define CMD_RASET    0x2B
#define CMD_RAMWR    0x2C
#define CMD_MADCTL   0x36

#define SPI0_GCR   (*(volatile uint32_t*)0x01C68004u)
#define SPI0_TCR   (*(volatile uint32_t*)0x01C68008u)
#define SPI0_FSR   (*(volatile uint32_t*)0x01C6801Cu)
#define SPI0_CCR   (*(volatile uint32_t*)0x01C68024u)
#define SPI0_MBC   (*(volatile uint32_t*)0x01C68030u)
#define SPI0_BCC   (*(volatile uint32_t*)0x01C68038u)
#define SPI0_FCR   (*(volatile uint32_t*)0x01C68018u)
#define SPI0_ISR   (*(volatile uint32_t*)0x01C68014u)
#define SPI0_TXD8  (*(volatile uint8_t*)0x01C68200u)
#define SPI0_RXD8  (*(volatile uint8_t*)0x01C68300u)
#define SPI0_TCR_XCH (1u << 31)
#define SPI0_FSR_TF_CNT_MASK (0xFFu << 16)

#define PA_BASE  0x01C20800u
#define PA_CFG0  (*(volatile uint32_t*)(PA_BASE + 0x00u))
#define PA_PULL0 (*(volatile uint32_t*)(PA_BASE + 0x1Cu))
#define PA_CFG1  (*(volatile uint32_t*)(PA_BASE + 0x04u))
#define PA_CFG2  (*(volatile uint32_t*)(PA_BASE + 0x08u))
#define PA_DAT   (*(volatile uint32_t*)(PA_BASE + 0x10u))
#define PC_BASE  0x01C20848u
#define PC_CFG0  (*(volatile uint32_t*)(PC_BASE + 0x00u))
#define PC_DRV0  (*(volatile uint32_t*)(PC_BASE + 0x14u))
#define PC_DAT   (*(volatile uint32_t*)(PC_BASE + 0x10u))

#define PIN_CS  3   // PC3 — CS дисплея (CE0)
#define PIN_CS2 21  // PA21 — CS тача XPT2046 (CE1)
#define PIN_DC  7   // PC7
#define PIN_RST 2   // PA2
#define PIN_PEN 1   // PA1 — PENIRQ TSC2046: 0 = касание (активный низкий)

// MADCTL=0xE0 → панель работает в BGR: красный и синий каналы поменяны местами.
// RGB565: R=биты[15:11], G=[10:5], B=[4:0]. Меняем R↔B, зелёный не трогаем.
// Белый (0xFFFF), чёрный и серые (R==B) не изменятся; красный↔синий — да.
static inline uint16_t tft_swap_rb(uint16_t c) {
    return (uint16_t)(((c & 0x001Fu) << 11) | (c & 0x07E0u) | ((c >> 11) & 0x001Fu));
}

// ============================================================================
// ВНИМАНИЕ (гонка PA_DAT, 0x01C20810): CPU1 пишет PA_DAT для тача (PA21) и
// RST (PA2); CPU0 — для Sega-пада (PA11=SCL, PA12=SDA, sega_pad.c) и SD-LED
// (PA15). Битовые |= / &= НЕ атомарны между ядрами → редкая RMW-гонка может
// «потерять» бит I2C и сорвать скан пада. Меры (r155/r157): мигалку «alive»
// увели на PL10 (R_PIO, led.c), кэш скана пада 12 мс, ре-инициализация пада на
// переходах. НЕ добавлять новых частых записей в PA_DAT с CPU0; полное решение —
// аппаратный TWI0 под пад или сериализация доступа.
// ============================================================================

// SRAM A1 почта (вне кэшей, MMU-disabled на CPU1)
#define TFT_STAT  (*(volatile uint32_t*)0x24u)  // st=1..3,9,0x11..0x16 — статус CPU1
#define TFT_PROBE  (*(volatile uint32_t*)0x28u) // PA21 X
#define TFT_PROBE2 (*(volatile uint32_t*)0x2Cu) // PA21 Y
#define TFT_PROBE3 (*(volatile uint32_t*)0x30u) // PC3 контроль
// r118: команды/результат калибровки через SRAM-почту. .coherent между
// ядрами на этой плате не работает (mmu_mark_uncached не зовётся, если
// USB не инициализировался) — SRAM A1 не кэшируется ни одним ядром.
#define TFT_CMD    (*(volatile uint32_t*)0x34u) // core0→CPU1: 1 = калибровка
#define TFT_CALOK  (*(volatile uint32_t*)0x38u) // CPU1→core0: 1 = OK
#define TFT_CALRX  ((volatile uint32_t*)0x3Cu)  // CPU1→core0: rx4[5]
#define TFT_CALRY  ((volatile uint32_t*)0x50u)  // CPU1→core0: ry4[5]
// r120: кнопки/справка — тоже через SRAM (не .coherent): калибровка заняла
// 0x34-0x5F, берём следующие адреса.
#define TFT_BTN      (*(volatile int32_t*)0x64u) // CPU1→core0: -2 Settings, -4 About
#define TFT_HELP_ID  (*(volatile int32_t*)0x68u) // core0→CPU1: страница справки
#define TFT_HELP_EPOCH (*(volatile int32_t*)0x6Cu)// core0→CPU1: эпоха (перерисовать)
// r121: значения переключателей F1/F2 — пишет core0, читает CPU1 (иначе
// CPU1 видит stale-значения из кэша core0 и на TFT всегда старые строки).
#define TFT_DIFF     (*(volatile int32_t*)0x70u) // A2600 diff: 1=Expert
// r158: TFT_PERIOD (0x74) удалён — настройка частоты кадра убрана из меню.
// r135: меню настроек на TFT (режим CPU1, аналогично калибровке)
#define TFT_SET_CMD   (*(volatile int32_t*)0x78u) // core0→CPU1: 1 = режим настроек
#define TFT_SET_SEL   (*(volatile int32_t*)0x7Cu) // core0→CPU1: выбранный пункт 0..7 (подсветка)
#define TFT_SET_EV    (*(volatile int32_t*)0x80u) // CPU1→core0: тап (0..7, 8=Back)
#define TFT_SET_EPOCH (*(volatile int32_t*)0x84u) // core0→CPU1: перерисовать
#define TFT_SET_MODE  (*(volatile int32_t*)0x88u) // core0→CPU1: подрежим (0=список,1=partition,2=confirm,3=result)
#define TFT_SET_INFO  (*(volatile int32_t*)0x8Cu) // core0→CPU1: результат подрежима (create-folder счётчики)

#define FB_W  1024
#define FB_H  600
#define TFT_W 480
#define TFT_H 320

static uint16_t tft_fb[TFT_H * TFT_W] __attribute__((aligned(16)));
static int g_tft_ready = 0;

// r130: g_tft_frame_ready/g_help_id/g_tft_help_epoch/g_tft_request удалены —
// вся межъядерная связь в SRAM-почте (0x34..0x70). Флаг «кадр готов» не
// имел потребителя (зеркало HDMI отложено).

// --- Буфер тача: CPU1 пишет, core0 печатает (драки за UART нет) ---
volatile uint16_t g_ts_rx __attribute__((section(".coherent"), aligned(4)));   // сырой X (0x90)
volatile uint16_t g_ts_ry __attribute__((section(".coherent"), aligned(4)));   // сырой Y (0xD0)
volatile int16_t  g_ts_px __attribute__((section(".coherent"), aligned(4)));   // масштабированный x
volatile int16_t  g_ts_py __attribute__((section(".coherent"), aligned(4)));   // масштабированный y
volatile uint32_t g_ts_seq __attribute__((section(".coherent"), aligned(4)));  // растёт на каждом событии
volatile uint8_t  g_ts_pressed __attribute__((section(".coherent"), aligned(4))); // 1 = сейчас нажат
volatile uint32_t g_ts_dbg_flag __attribute__((section(".coherent"), aligned(4)));
volatile uint32_t g_ts_dbg_data[8] __attribute__((section(".coherent"), aligned(4)));

// r115: применяемые границы (сканер использует их). Значения НЕ
// инициализировать в объявлении: .coherent не копируется из .data,
// статики остаются нулями → деление на 0 даёт pos=0/319. Дефолты
// выставляются в tft_core_main при старте; калибровка обновляет.
volatile int32_t g_cal_rx[5] __attribute__((section(".coherent"), aligned(4)));
volatile int32_t g_cal_ry[5] __attribute__((section(".coherent"), aligned(4)));
volatile int32_t g_cal_ok   __attribute__((section(".coherent"), aligned(4))); // 1=OK, 0=NOT OK
volatile int32_t g_cal_xmin __attribute__((section(".coherent"), aligned(4)));
volatile int32_t g_cal_xmax __attribute__((section(".coherent"), aligned(4)));
volatile int32_t g_cal_ymin __attribute__((section(".coherent"), aligned(4)));
volatile int32_t g_cal_ymax __attribute__((section(".coherent"), aligned(4)));
volatile int32_t g_touch_xmin __attribute__((section(".coherent"), aligned(4)));
volatile int32_t g_touch_xmax __attribute__((section(".coherent"), aligned(4)));
volatile int32_t g_touch_ymin __attribute__((section(".coherent"), aligned(4)));
volatile int32_t g_touch_ymax __attribute__((section(".coherent"), aligned(4)));

static void cs_low(void)  {
    PC_DAT &= ~(1u << PIN_CS);
    PA_DAT &= ~(1u << PIN_CS2);
}
static void cs_high(void) {
    PC_DAT |=  (1u << PIN_CS);
    PA_DAT |=  (1u << PIN_CS2);
}
// r126: подъём ТОЛЬКО CS дисплея (PC3) — не дёргает PA21 (тач).
// Используется в xfer*/tft_flush* вместо cs_high(); спайки на CS тача
// исключены (см. P5: cs_low/cs_high трогали оба CS).
static void cs_disp_high(void) {
    PC_DAT |=  (1u << PIN_CS);
}
static void dc_cmd(void)  { PC_DAT &= ~(1u << PIN_DC); }
static void dc_data(void) { PC_DAT |=  (1u << PIN_DC); }

static int wait_tx_room(void) {
    for (uint32_t t = 0; t < 200000; t++)
        if (((SPI0_FSR & SPI0_FSR_TF_CNT_MASK) >> 16) < 64u) return 1;
    return 0;
}
static int wait_done(void) {
    for (uint32_t t = 0; t < 200000; t++)
        if (!(SPI0_TCR & SPI0_TCR_XCH)) return 1;
    return 0;
}

// Передача одного байта + принудительный сброс TX/RX FIFO.
// TF_CNT монотонен — без сброса через 64 навсегда блокируется.
static int spi0_tx8(uint8_t b) {
    if (!wait_tx_room()) return -2;
    SPI0_TXD8 = b;
    SPI0_MBC = 1;
    SPI0_BCC = 1;
    SPI0_TCR |= SPI0_TCR_XCH;
    if (!wait_done()) return -1;
    (void)SPI0_RXD8;
    SPI0_FCR = (1u << 31) | (1u << 15);
    return 0;
}

// Тот же сброс TX/RX FIFO, что и в spi0_tx8(): TF_CNT монотонен, без
// SPI0_FCR после каждого байта тач-скан упрётся в 64 и «зависнет» навсегда.
// Тут ещё и читаем принятый байт ДО сброса (RX FIFO дёргает MISO XPT2046).
// r59: данные появляются в RX FIFO с задержкой относительно XCH — ждём
// RF_CNT>0 (иначе читаем пустоту и теряем байт), эталон — sun6i-драйвер.
static int spi0_txrx8(uint8_t tx, uint8_t* rx, uint32_t* rf_out) {
    uint32_t rf_cnt = 0;
    if (!wait_tx_room()) return -1;
    SPI0_TXD8 = tx;
    SPI0_MBC = 1;
    SPI0_BCC = 1;
    SPI0_TCR |= SPI0_TCR_XCH;
    if (!wait_done()) return -1;
    for (int t = 0; t < 2000; t++) {
        rf_cnt = SPI0_FSR & 0xFF;
        if (rf_cnt) break;
    }
    if (rx) *rx = rf_cnt ? SPI0_RXD8 : 0;
    if (rf_out) *rf_out = rf_cnt;
    SPI0_FCR = (1u << 31) | (1u << 15);
    return 0;
}

static void cs_select(int sel) {
    PC_DAT |= (1u << PIN_CS);
    PA_DAT |= (1u << PIN_CS2);
    if      (sel == 0) PC_DAT &= ~(1u << PIN_CS);
    else               PA_DAT &= ~(1u << PIN_CS2);
}

static uint16_t tft_touch_probe_cs(int sel, uint8_t cmd) {
    uint16_t v = 0;
    // r89: TSC2046 читается в SPI Mode 1 (CPHA=1, SDM=1); дисплей Mode 0,
    // поэтому TCR переключаем на время чтения тача и возвращаем.
    uint32_t tcr_save = SPI0_TCR;
    SPI0_TCR = (SPI0_TCR & ~((1u << 1) | (1u << 0))) | (1u << 0) | (1u << 13);
    __asm volatile("dsb" ::: "memory");
    for (int pass = 0; pass < 2; pass++) {
        uint8_t d0, d1, d2;
        cs_select(sel);
        __asm volatile("dsb" ::: "memory");   // CS зажат — барьер ДО SPI
        if (spi0_txrx8(cmd, &d0, NULL) < 0 ||
            spi0_txrx8(0x00, &d1, NULL) < 0 ||
            spi0_txrx8(0x00, &d2, NULL) < 0) { PA_DAT |= (1u << PIN_CS2); SPI0_TCR = tcr_save; return 0xFFFF; }
        PA_DAT |= (1u << PIN_CS2);   // поднимаем ТОЛЬКО PA21 (cs_high после P5 не трогает тач)
        v = (uint16_t)((d1 << 8) | d2);
    }
    SPI0_TCR = tcr_save;
    __asm volatile("dsb" ::: "memory");
    return v;
}

// ---- Протокол 16-битной шины (74HC4094×2 + 4040) ----
// Каждый обмен = 16 тактов SPI под одним CS:
//   [0x00, cmd] для команд (cmd на D0-D7)
//   [0x00, d]   для 8-битных параметров
//   [hi,  lo]   для 16-битных пикселей (hi в D8-D15)
// Подъём CS = лэтч в 4094 + WR.

static int xfer_cmd(uint8_t cmd) {
    cs_select(0);
    dc_cmd();
    int r = spi0_tx8(0x00);
    if (r == 0) r = spi0_tx8(cmd);
    dc_data();
    cs_disp_high();
    return r < 0 ? r : 0;
}

static int xfer_data(uint8_t d) {
    cs_select(0);
    int r = spi0_tx8(0x00);
    if (r == 0) r = spi0_tx8(d);
    cs_disp_high();
    return r < 0 ? r : 0;
}

static void delay_ms(int ms) {
    uint32_t start = h3_hs_timer_lo_us();
    while ((int32_t)(h3_hs_timer_lo_us() - start) < (int32_t)((uint32_t)ms * 1000u)) ;
}

static void spi0_init(void) {
    H3_CCU->BUS_CLK_GATING0 |= CCU_BUS_CLK_GATING0_SPI0;
    H3_CCU->BUS_SOFT_RESET0  |= CCU_BUS_SOFT_RESET0_SPI0;
    udelay(1000);
    volatile uint32_t* clk = &H3_CCU->SPI0_CLK;
    *clk = (1u << 31) | (1u << 24);
    udelay(1000);

    PC_CFG0 = (3u << 0) | (3u << 4) | (3u << 8) | (1u << 12) | (1u << 28);
    // r103: PA1 = PENIRQ тача. Вход + внутренняя ПОДТЯЖКА проца:
    // на рабочем таче внешней подтяжки нет, без неё PA1 плавает и
    // детект по значению сыпет мусор 0/2048/4095 без нажатия.
    PA_CFG0 &= ~(0xFu << 4);              // PA1 = input
    PA_PULL0 &= ~(0x3u << 2);             // биты [3:2] для PA1
    PA_PULL0 |=  (0x1u << 2);             // 01 = pull-up
    // Максимальная сила драйвера (8 мА) на MOSI/SCLK/CS — быстрое нарастание
    // фронтов; слабый драйвер по умолчанию режет 8+ МГц на ёмкости шлейфа.
    PC_DRV0 |= (3u << 0) | (3u << 2) | (3u << 4) | (3u << 6);   /* PC0..PC3 */
    PA_CFG0 &= ~(0xFu << 8);
    PA_CFG0 |= (1u << 8);
    PA_CFG2 &= ~(0xFu << 20);
    PA_CFG2 |= (1u << 20);

    PC_DAT |= (1u << PIN_CS);
    PA_DAT |= (1u << PIN_CS2);
    PC_DAT &= ~(1u << PIN_DC);
    PA_DAT |= (1u << PIN_RST);

    SPI0_GCR = (1u << 0) | (1u << 1) | (1u << 7);
    udelay(100);
    SPI0_TCR = 0;
    SPI0_FCR = (1u << 31) | (1u << 15);
    udelay(100);
    SPI0_CCR = 0x1005;  /* CDR2=5 → 8 МГц (96/12) — с усиленным драйвером */
    udelay(100);
}

static void spi_dump(void) {
    printf("DMP GCR=0x%08X TCR=0x%08X FSR=0x%08X ISR=0x%08X CCR=0x%08X MBC=0x%08X BCC=0x%08X\n",
           (unsigned)SPI0_GCR, (unsigned)SPI0_TCR, (unsigned)SPI0_FSR,
           (unsigned)SPI0_ISR, (unsigned)SPI0_CCR,
           (unsigned)SPI0_MBC, (unsigned)SPI0_BCC);
}

// Последовательность инициализации ILI9486 — из waveshare35a-overlay.dtb.
// B0=00, 3A=55, 36=28, C2=44, C5=6×00, E0/E1/E2 гаммы, 36=28, 11, 29.
// Все 16-битные обмены через xfer_cmd/xfer_data.
static int tft_ili_init(void) {
    TFT_STAT = 0x11;
    PA_DAT &= ~(1u << PIN_RST);  delay_ms(20);
    PA_DAT |=  (1u << PIN_RST);  delay_ms(150);
    if (xfer_cmd(CMD_SWRESET) < 0) { printf("TFT: SPI fail @SWRESET\n"); spi_dump(); return -1; }
    delay_ms(150);
    TFT_STAT = 0x12;

    if (xfer_cmd(CMD_SLPOUT) < 0) { printf("TFT: SPI fail @SLPOUT\n"); spi_dump(); return -1; }
    delay_ms(150);
    TFT_STAT = 0x13;

    if (xfer_cmd(0xB0) < 0 || xfer_data(0x00) < 0)            { printf("TFT: SPI fail B0\n"); spi_dump(); return -1; }
    if (xfer_cmd(CMD_COLMOD) < 0 || xfer_data(0x55) < 0)      { printf("TFT: SPI fail COLMOD\n"); spi_dump(); return -1; }
    if (xfer_cmd(CMD_MADCTL) < 0 || xfer_data(0xE0) < 0)      { printf("TFT: SPI fail MADCTL\n"); spi_dump(); return -1; }
    if (xfer_cmd(0xC2) < 0 || xfer_data(0x44) < 0)            { printf("TFT: SPI fail C2\n"); spi_dump(); return -1; }
    if (xfer_cmd(0xC5) < 0)                                   { printf("TFT: SPI fail C5\n"); spi_dump(); return -1; }
    for (int z = 0; z < 6; z++)
        if (xfer_data(0x00) < 0)                              { printf("TFT: SPI fail C5\n"); spi_dump(); return -1; }
    TFT_STAT = 0x14;

    {
        static const uint8_t gp[15] = {0x0F,0x1F,0x1C,0x0C,0x0F,0x08,0x48,0x98,
                                       0x37,0x0A,0x13,0x04,0x11,0x0D,0x00};
        static const uint8_t gn[15] = {0x0F,0x32,0x2E,0x0B,0x0D,0x05,0x47,0x75,
                                       0x37,0x06,0x10,0x03,0x24,0x20,0x00};
        if (xfer_cmd(0xE0) < 0)                { printf("TFT: SPI fail E0\n"); spi_dump(); return -1; }
        for (int i = 0; i < 15; i++)
            if (xfer_data(gp[i]) < 0)          { printf("TFT: SPI fail E0-d[%d]\n", i); spi_dump(); return -1; }
        if (xfer_cmd(0xE1) < 0)                { printf("TFT: SPI fail E1\n"); spi_dump(); return -1; }
        for (int i = 0; i < 15; i++)
            if (xfer_data(gn[i]) < 0)          { printf("TFT: SPI fail E1-d[%d]\n", i); spi_dump(); return -1; }
        if (xfer_cmd(0xE2) < 0)                { printf("TFT: SPI fail E2\n"); spi_dump(); return -1; }
        for (int i = 0; i < 15; i++)
            if (xfer_data(gn[i]) < 0)          { printf("TFT: SPI fail E2-d[%d]\n", i); spi_dump(); return -1; }
    }
    TFT_STAT = 0x15;
    if (xfer_cmd(CMD_MADCTL) < 0 || xfer_data(0xE0) < 0) { printf("TFT: SPI fail MADCTL2\n"); spi_dump(); return -1; }
    if (xfer_cmd(CMD_SLPOUT) < 0) { printf("TFT: SPI fail SLPOUT2\n"); spi_dump(); return -1; }
    delay_ms(50);
    if (xfer_cmd(CMD_DISPON) < 0) { printf("TFT: SPI fail DISPON (0x%X TCR=0x%X FSR=0x%X)\n",
        (unsigned)CMD_DISPON, (unsigned)SPI0_TCR, (unsigned)SPI0_FSR); spi_dump(); return -1; }
    TFT_STAT = 0x16;
    delay_ms(50);
    printf("TFT: init done\n");
    return 0;
}

static int set_window(void) {
    int r = 0;
    if (xfer_cmd(CMD_CASET) < 0) r = -1;
    xfer_data(0); xfer_data(0); xfer_data((TFT_W-1)>>8); xfer_data((TFT_W-1)&0xFF);
    if (xfer_cmd(CMD_RASET) < 0) r = -1;
    xfer_data(0); xfer_data(0); xfer_data((TFT_H-1)>>8); xfer_data((TFT_H-1)&0xFF);
    return r;
}

int tft_init(void) {
    spi0_init();
    if (tft_ili_init() == 0) {
        g_tft_ready = 1;
        printf("TFT DFR0428: ready (480x320, dup HDMI)\n");
        return 0;
    }
    g_tft_ready = 0;
    return -1;
}

// r130: tft_tick/set_menu_mode/set_dup_mode удалены (мёртвый код, P6-P8)

void tft_render_begin(void) { memset(tft_fb, 0, TFT_W * TFT_H * 2); }

// Полный кадр — по-байтовый путь, лэтч по CS↑ (рабочий вариант на 8 МГц).
// Пакет с непрерывным CS (один XCH на кадр) давал серый экран: 74HC4094
// на этой плате лэтчит только фронтом CS, непрерывный низкий CS ничего
// не защёлкивает.
void tft_flush(void) {
    if (!g_tft_ready) return;
    if (set_window() < 0) return;
    cs_select(0);
    dc_cmd();
    if (spi0_tx8(0x00) < 0 || spi0_tx8(CMD_RAMWR) < 0) { cs_disp_high(); return; }
    dc_data();
    for (int ty = 0; ty < TFT_H; ty++) {
        const uint16_t* row = tft_fb + (uint32_t)ty * TFT_W;
        for (int tx = 0; tx < TFT_W; tx++) {
            uint16_t p = row[tx];
            cs_select(0);
            if (spi0_tx8((uint8_t)(p >> 8)) < 0 ||
                spi0_tx8((uint8_t)(p & 0xFF)) < 0) { cs_disp_high(); goto bye; }
            __asm volatile("nop; nop; nop; nop; nop"); /* ~50ns на стабильность шины */
            cs_disp_high();
        }
    }
bye:
    ;
}

// Частичная отрисовка прямоугольника окна: только CASET/RASET по области,
// пиксели из tft_fb. Обновление маленького UI-элемента в сотни раз быстрее
// полного кадра (десяти байт вместо 614400).
void tft_flush_rect(int x, int y, int w, int h) {
    if (!g_tft_ready) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= TFT_W || y >= TFT_H) return;
    if (x + w > TFT_W) w = TFT_W - x;
    if (y + h > TFT_H) h = TFT_H - y;
    if (w <= 0 || h <= 0) return;

    if (xfer_cmd(CMD_CASET) < 0) return;
    xfer_data((uint8_t)(x >> 8)); xfer_data((uint8_t)(x & 0xFF));
    xfer_data((uint8_t)((x + w - 1) >> 8)); xfer_data((uint8_t)((x + w - 1) & 0xFF));
    if (xfer_cmd(CMD_RASET) < 0) return;
    xfer_data((uint8_t)(y >> 8)); xfer_data((uint8_t)(y & 0xFF));
    xfer_data((uint8_t)((y + h - 1) >> 8)); xfer_data((uint8_t)((y + h - 1) & 0xFF));

    cs_select(0);
    dc_cmd();
    if (spi0_tx8(0x00) < 0 || spi0_tx8(CMD_RAMWR) < 0) { cs_disp_high(); return; }
    dc_data();
    for (int yy = y; yy < y + h; yy++) {
        const uint16_t* row = tft_fb + (uint32_t)yy * TFT_W + x;
        for (int xx = 0; xx < w; xx++) {
            uint16_t p = row[xx];
            cs_select(0);
            if (spi0_tx8((uint8_t)(p >> 8)) < 0 ||
                spi0_tx8((uint8_t)(p & 0xFF)) < 0) { cs_disp_high(); return; }
            __asm volatile("nop; nop; nop; nop; nop");
            cs_disp_high();
        }
    }
}

void tft_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) x = 0;  if (y < 0) y = 0;
    if (x >= TFT_W || y >= TFT_H) return;
    if (x + w > TFT_W) w = TFT_W - x;
    if (y + h > TFT_H) h = TFT_H - y;
    color = tft_swap_rb(color);   // MADCTL=0xE0: BGR → RGB
    for (int yy = y; yy < y + h; yy++) {
        uint16_t* row = tft_fb + (uint32_t)yy * TFT_W;
        for (int xx = x; xx < x + w; xx++) row[xx] = color;
    }
}

void tft_puts(int x, int y, const char* s, uint16_t color) {
    color = tft_swap_rb(color);   // MADCTL=0xE0: BGR → RGB
    while (*s) {
        char ch = *s++;
        if (ch < 0x20 || ch > 0x7F) { x += 8; continue; }
        const uint8_t* gl = tft_font[ch - 0x20];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = gl[row];
            if (y + row >= TFT_H) break;
            uint16_t* line = tft_fb + (uint32_t)(y + row) * TFT_W + x;
            for (int col = 0; col < 8; col++) {
                if (x + col >= TFT_W) break;
                if (bits & (0x80 >> col)) line[col] = color;
            }
        }
        x += 8;
    }
}

// Текст с масштабом 2x (заголовки). Аккуратно: каждый глиф рисуется 16x16.
static void tft_puts2(int x, int y, const char* s, uint16_t color) {
    color = tft_swap_rb(color);   // MADCTL=0xE0: BGR → RGB
    while (*s) {
        char ch = *s++;
        if (ch < 0x20 || ch > 0x7F) { x += 16; continue; }
        const uint8_t* gl = tft_font[ch - 0x20];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = gl[row];
            for (int dy = 0; dy < 2; dy++) {
                int py = y + row*2 + dy;
                if (py >= TFT_H) break;
                uint16_t* line = tft_fb + (uint32_t)py * TFT_W + x;
                for (int col = 0; col < 8; col++) {
                    if (x + col*2 + 1 >= TFT_W) break;
                    if (bits & (0x80 >> col)) {
                        line[col*2]   = color;
                        line[col*2+1] = color;
                    }
                }
            }
        }
        x += 16;
    }
}

// ---- Справка по управлению на TFT ----
// Таблица: id системы (NULL=меню), заголовок, строки до NULL.
static const struct {
    const char* id;
    const char* title;
    const char* lines[12];
} g_help_list[] = {
{ NULL,
      "MultiTool Retro",
      {"Up / Down: navigate","Enter: open system / ROM",
       "ESC: back",
       "Settings / About: right columns",
       "","",
       NULL}
    },
    { "gameboy",
      "Game Boy / Game Boy Color",
      {"Z = B    X = A","S = Select    Enter = Start","ESC hold = exit",NULL}
    },
    { "gamegear",
      "Sega Game Gear",
      {"Z = Button 1    X = Button 2","S = Pause","Enter = Start    ESC hold = exit",NULL}
    },
    { "gba",
      "Game Boy Advance",
      {"Z = B    X = A","S = Select    Enter = Start","ESC hold = exit","",NULL}
    },
    { "lynx",
      "Atari Lynx",
      {"Arrows = D-Pad    Z = A","X = B    S = Opt1","Enter = Opt2","ESC hold = exit",NULL}
    },
    { "ngp",
      "Neo Geo Pocket",
      {"Arrows = D-Pad","Z = A    X = B","S = Select    Enter = Start","ESC hold = exit",NULL}
    },
    { "a2600",
      "Atari 2600",
      {"Arrows = D-Pad","Z = Fire (button)","S = Select    Enter = Reset","Diff. = Settings 4","ESC hold = exit",NULL}
    },
    { "a5200",
      "Atari 5200",
      {"Arrows = D-Pad","Z = Fire    X = Pause","S = Start    Enter = Key3","ESC hold = exit",NULL}
    },
    { "a7800",
      "Atari 7800",
      {"Arrows = D-Pad","Z = B1(A)    X = B2(B)","S = Select    Enter = Start","ESC hold = exit",NULL}
    },
    { "sms",
      "Sega Master System",
      {"Arrows = D-Pad","Z = Button 1    X = Button 2","S = Pause","Enter = Start    ESC hold = exit",NULL}
    },
    { "coleco",
      "ColecoVision",
      {"Arrows = D-Pad","Z = Fire 1    X = Fire 2","Enter = Start    ESC hold = exit",NULL}
    },
    { "nes",
      "NES / Famicom (Dendy)",
      {"Arrows = D-Pad","Z = A    X = B","S = Select    Enter = Start","ESC hold = exit",NULL}
    },
    { "snes",
      "SNES / Super Famicom",
      {"Arrows = D-Pad","Z = B    X = Y    A = A","S = X    Q = L    W = R","Space = Select    Enter = Start","ESC hold = exit",NULL}
    },
    { "megadrive",
      "Mega Drive / Genesis",
      {"Arrows = D-Pad","Z = A    X = B    C = C","A = X    S = Y    D = Z","Q = Mode    Enter = Start","ESC hold = exit",NULL}
    },
    { "vectrex",
      "GCE Vectrex",
      {"Arrows = D-Pad","Z = Button 1    X = Button 2","Enter = Start","ESC hold = exit",NULL}
    },
    { "msx",
      "MSX / Yamaha YIS-503II",
      {"Full keyboard emulation","(P)ortfolio style","Enter = Start BASIC","ESC hold = exit","",NULL}
    },
    { "portfolio",
      "Atari Portfolio",
      {"Full keyboard emulation","INS = on-screen kbd","ESC hold = exit to menu","",NULL}
    },
    // r122: About по тап-иконке на TFT (не трогает HDMI)
    { "about",
      "MultiTool Retro",
      {"Allwinner H3 4x Cortex-A7","bare-metal multiboot","SPI TFT 480x320 (ILI9486)","touch TSC2046I","ESC - back to menu","",NULL}
    },
};
#define TFT_HELP_COUNT (sizeof(g_help_list)/sizeof(g_help_list[0]))

// Ставит справку для системы sys_id (NULL = меню). Вызывается с core0.
// r120: пишем в SRAM-почту (не .coherent) — между ядрами видно сразу.
void tft_help_show(const char* sys_id) {
    if (!sys_id) { TFT_HELP_ID = 0; TFT_HELP_EPOCH++; return; }   // меню
    int id = 0;
    for (int i = 1; i < (int)TFT_HELP_COUNT; i++) {
        if (g_help_list[i].id && strcmp(g_help_list[i].id, sys_id) == 0) { id = i; break; }
    }
    TFT_HELP_ID = id;
    TFT_HELP_EPOCH++;
}

// Сборка строки вручную (нет snprintf в bare-metal): число 0..999 + текст.
static void tft_itoa(char** pp, unsigned v) {
    char tmp[8];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n > 0) *(*pp)++ = tmp[--n];
}
static void tft_strcat(char** pp, const char* s) {
    while (*s) *(*pp)++ = *s++;
}

// Рендер справки по индексу id в tft_fb (выполняется на CPU1).
// Верстка: слева столбиком подсказки по кнопкам, справа — колонка иконок
// Settings/About (тап по ним = меню-пункты, код в SRAM TFT_BTN).
#define ICON_X  384
#define ICON_W  (TFT_W - ICON_X - 4)   // ~92px
#define ICON_H  34
static void tft_help_render(int id, int full) {
    if (id < 0 || id >= (int)TFT_HELP_COUNT) id = 0;
    const char* title = g_help_list[id].title;
    const char* const* lines = g_help_list[id].lines;

    tft_render_begin();
    tft_fill_rect(0, 0, TFT_W, TFT_H, 0x0000);

    // Заголовок — крупный, красный (шрифт 2x: глиф 16px, от y=2 до y≈18)
    tft_puts2(4, 2, title, 0xF800);
    // Разделитель НИЖЕ заголовка (y=24), не пересекается с ним
    tft_fill_rect(0, 24, TFT_W, 2, 0xFFFF);

    // Строки справки — столбиком, НАЧИНАЯ ПОСЛЕ разделителя (y=30)
    int y = 30;
    for (int i = 0; lines[i] && i < 12; i++) {
        tft_puts(4, y, lines[i], 0xFFFF);
        y += 14;
    }

    // Динамическая строка переключателя (страница меню, id=0): F1 — A2600 diff.
    // Частоту кадра убрали из меню (r158) — строка F2 больше не рисуется.
    // r121: значения читаем из SRAM-почты — core0 пишет туда при F1,
    // иначе CPU1 видит stale-значения из кэша core0.
    if (id == 0) {
        int diff = TFT_DIFF;
        tft_fill_rect(0, 116, TFT_W, 30, 0x0000);   // затирка зоны строки
        char dynbuf[64];
        char* p = dynbuf;

        tft_strcat(&p, "F1: A2600 diff: ");
        tft_strcat(&p, diff ? "Expert" : "Novice");
        *p = 0;
        tft_puts(4, 124, dynbuf, 0xFFFF);
    }

    // Иконки справа (Settings / About)
    int sx = ICON_X, sy = 30;
    tft_fill_rect(sx, sy, ICON_W, ICON_H, 0x0018);
    tft_fill_rect(sx, sy, ICON_W, 1, 0xFFFF);
    tft_fill_rect(sx, sy + ICON_H - 1, ICON_W, 1, 0xFFFF);
    tft_fill_rect(sx, sy, 1, ICON_H, 0xFFFF);
    tft_fill_rect(sx + ICON_W - 1, sy, 1, ICON_H, 0xFFFF);
    tft_puts(sx + 6, sy + 13, "Settings", 0xFFFF);

    sy += ICON_H + 10;
    tft_fill_rect(sx, sy, ICON_W, ICON_H, 0x0018);
    tft_fill_rect(sx, sy, ICON_W, 1, 0xFFFF);
    tft_fill_rect(sx, sy + ICON_H - 1, ICON_W, 1, 0xFFFF);
    tft_fill_rect(sx, sy, 1, ICON_H, 0xFFFF);
    tft_fill_rect(sx + ICON_W - 1, sy, 1, ICON_H, 0xFFFF);
    tft_puts(sx + 6, sy + 13, "About", 0xFFFF);

tft_puts(4, TFT_H - 12, "ESC hold = exit emulator", 0x7BEF);
    // r140: версия прошивки в правом нижнем углу TFT (r142: прижата вправо целиком)
    extern const char g_fw_version[];
    int fw_w = 8 * (int)strlen(g_fw_version);   // шрифт 8x8
    if (fw_w > TFT_W - 8) fw_w = TFT_W - 8;
    tft_puts(TFT_W - fw_w - 4, TFT_H - 12, g_fw_version, 0x7BEF);

    // Эхо в UART: что рисуем на TFT + сырые значения тача (отладка)
    // r113: TXT-эхо убрано — калибровка рисует результат на экран,
    // печатает CAL-строки; TXT спамили UART при каждой перерисовке меню.
    (void)title; (void)lines;

    // r121: полный флаш только при смене страницы; refresh (F1/F2) отправляем
    // только зону динамических строк (y 116..159) — быстрее и без мельканий.
    if (full || id != 0) tft_flush();
    else                 tft_flush_rect(0, 116, TFT_W, 44);
}

// ---- Сканирование тача TSC2046I (CPU1) — протокол XPT2046 ----
// Калибровка грубая (waveshare35a-overlay: xmin=200,xmax=3900, swapxy=1).
// Маппинг под ориентацию 0xE0: скан X-канала (0x90) идёт в ВЕРТИКАЛЬ (sy),
// скан Y-канала (0xD0) — в ГОРИЗОНТАЛЬ (sx). Если координаты зеркально
// неверны — менять знаки/места по отладочным raw (печатаются в UART).
// 0 = отпущен, 1 = нажат; координаты 0..TFT_W/H; raw отдаются для калибровки.
static int tft_touch_scan(int* px, int* py, uint16_t* rawx, uint16_t* rawy, uint16_t* rawz) {
    // r105: детект касания по Z1-каналу (0xB0, давление) — как в заводском
    // драйвере xpt2046. X/Y в отпущенном таче плавают (0/2048/4095) и не
    // дают надёжного порога, а Z1 при нажатии уходит из крайних значений.
    // Читаем X, Y и Z1; нажатие: Z1 в среднем диапазоне (не 0 и не 4095).
    // r129: watchdog — если SPI-шина залипла, скан не должен вешать CPU1
    // навсегда (симптом: PL10 замолчала, дисплей замер). После 100 мс
    // сбрасываем FIFO/TCR и выходим, главный цикл продолжит работу.
    uint32_t t0 = h3_hs_timer_lo_us();
    uint32_t save = SPI0_CCR;
    SPI0_CCR = (6u << 8);            // 1,5 МГц (96/2^6)
    uint16_t rx = tft_touch_probe_cs(1, 0x90);  // X-канал
    if (h3_hs_timer_lo_us() - t0 > 100000) goto ts_wd;
    uint16_t ry = tft_touch_probe_cs(1, 0xD0);  // Y-канал
    if (h3_hs_timer_lo_us() - t0 > 100000) goto ts_wd;
    uint16_t rz = tft_touch_probe_cs(1, 0xB0);  // Z1-канал (давление)
    if (h3_hs_timer_lo_us() - t0 > 100000) goto ts_wd;
    SPI0_CCR = save;
    if (rawx) *rawx = rx;
    if (rawy) *rawy = ry;
    if (rawz) *rawz = rz;
int rx4 = (int)(rx >> 4), ry4 = (int)(ry >> 4), rz4 = (int)(rz >> 4);
    // r110: калибровка по факту (тапны 1-4-C):
    //   rx4 2401(лево)..3920(право) — горизонталь  sx
    //   ry4 3664(верх)..2496(низ)    — вертикаль инвертирована: sy = 319 - ...
    // Диапазоны узкие: X 2350..3950, Y 2450..3700. Z1-порог 2140.
    if (rz4 < 2140) { g_ts_pressed = 0; return 0; }
    int sx = (rx4 - (int)g_touch_xmin) * TFT_W / ((int)g_touch_xmax - (int)g_touch_xmin);
    int sy = 319 - (ry4 - (int)g_touch_ymin) * TFT_H / ((int)g_touch_ymax - (int)g_touch_ymin);
    if (sx < 0) sx = 0; if (sx >= TFT_W) sx = TFT_W - 1;
    if (sy < 0) sy = 0; if (sy >= TFT_H) sy = TFT_H - 1;
    *px = sx; *py = sy;
    g_ts_pressed = 1;
    return 1;
ts_wd:
    printf("TSTOUCH watchdog: reset SPI\n");   // r129: единственный printf из тач-цикла
    SPI0_FCR = (1u << 31) | (1u << 15);   // сброс TX/RX FIFO
    SPI0_TCR = 0x0;                         // вернуть Mode 0 (дисплей)
    SPI0_CCR = save;
    g_ts_pressed = 0;
    return 0;
}

// ---- Калибровка тача: мишени в 4 углах + центр (r97) ----
// Рисуем квадраты с цифрами, по которым надо тапнуть. После снятия
// угловых значений (rx4/ry4 строками 1..4,C) вернуть tft_help_render(0)
// и подогнать 200/3900 в tft_touch_scan под фактические значения.
static void tft_calib_draw_markers(void) {
    const int M = 24;       // размер мишени
    const int OFF = 30;     // отступ от края
    const struct { int x, y; const char* label; } tg[5] = {
        { OFF,              OFF,              "1" },  // верхний-левый
        { TFT_W - OFF - M,  OFF,              "2" },  // верхний-правый
        { OFF,              TFT_H - OFF - M,  "3" },  // нижний-левый
        { TFT_W - OFF - M,  TFT_H - OFF - M,  "4" },  // нижний-правый
        { TFT_W/2 - M/2,    TFT_H/2 - M/2,    "C" },  // центр
    };
    for (int i = 0; i < 5; i++) {
        int x = tg[i].x, y = tg[i].y;
        tft_puts2(x + 4, y - 28, tg[i].label, 0xFFFF);   // подпись НАД мишенью
        tft_fill_rect(x - 1, y - 1, M + 2, M + 2, 0xFFFF); // рамка
        tft_fill_rect(x,     y,     M,     M,     0x07E0); // зелёная
        tft_fill_rect(x + 4, y + 4, M - 8, M - 8, 0x0000); // ядро
    }
}

// r112: режим калибровки тача — вызывается из tft_core_main когда
// core0 (Settings → Touch Calibration) ставит TFT_CMD=1 (SRAM-почта).
// CPU1 рисует мишени, ждёт 5 ФРОНТОВ касания (1,2,3,4,C), собирает
// rx4/ry4 в g_cal_rx/g_cal_ry, вычисляет границы и выводит OK/NOT OK.
static void tft_calib_mode(void) {
    TFT_STAT = 0x2E;   // r117 debug: CPU1 вошёл в калибровку
    tft_fill_rect(0, 0, TFT_W, TFT_H, 0x0000);
    tft_calib_draw_markers();
    tft_puts(4, TFT_H - 12, "tap 1 2 3 4 C", 0xFFFF);
    tft_flush();

    int got = 0, prev = 0, calok = 0;
    for (int i = 0; i < 5; i++) { g_cal_rx[i] = -1; g_cal_ry[i] = -1; }
    g_cal_ok = 0;

    // Без таймаута: выход по 5 тапам либо по отмене с core0 (ESC в Settings).
    while (got < 5 && TFT_CMD == 1) {
        extern void led_heartbeat_cpu1(void);
        led_heartbeat_cpu1();   // r127: PL10 мигает и в калибровке (лайв-индикатор)
        int px, py;
        uint16_t rawx = 0, rawy = 0, rawz = 0;
        int p = tft_touch_scan(&px, &py, &rawx, &rawy, &rawz);
        if (p && !prev) {
            int rx4 = (int)(rawx >> 4), ry4 = (int)(rawy >> 4);
            g_cal_rx[got] = rx4; g_cal_ry[got] = ry4;
            got++;
            // подсветка прогресса: рисуем номер принятой мишени
            char tmp[4]; tmp[0] = (char)('1' + got - 1); tmp[1] = 0;
            if (got == 5) tmp[0] = 'C';
            tft_puts2(300 + got * 20, 300, tmp, 0x07E0);
            tft_flush();
            // ждём отпускания (анти-дребезг: повторяющийся фронт не тап)
            while (tft_touch_scan(&px, &py, &rawx, &rawy, &rawz) && TFT_CMD == 1)
                { for (int d = 0; d < 1000; d++) udelay(30); }
        }
        prev = p;
        for (int d = 0; d < 1000; d++) udelay(30);   // ~30 мс между опросами
    }

    if (got == 5) {
        // границы: 1 и 3 — левые, 2 и 4 — правые; 1 и 2 — верх, 3 и 4 — низ
        int xmin = (g_cal_rx[0] < g_cal_rx[2]) ? g_cal_rx[0] : g_cal_rx[2];
        int xmax = (g_cal_rx[1] > g_cal_rx[3]) ? g_cal_rx[1] : g_cal_rx[3];
        int ymin = (g_cal_ry[2] < g_cal_ry[3]) ? g_cal_ry[2] : g_cal_ry[3];
        int ymax = (g_cal_ry[0] > g_cal_ry[1]) ? g_cal_ry[0] : g_cal_ry[1];

        // грубый допуск: размах по каждой оси не менее 300, границы не в краях
        int ok = (xmax - xmin > 300) && (ymax - ymin > 300) &&
                 xmin > 100 && xmax < 4095 - 100 &&
                 ymin > 100 && ymax < 4095 - 100;

        if (ok) {
            g_cal_xmin = xmin; g_cal_xmax = xmax;
            g_cal_ymin = ymin; g_cal_ymax = ymax;
            g_touch_xmin = xmin; g_touch_xmax = xmax;
            g_touch_ymin = ymin; g_touch_ymax = ymax;
        }
        g_cal_ok = ok;
        calok = ok;
        tft_fill_rect(0, 0, TFT_W, TFT_H, 0x0000);
        tft_puts2(20, 140, ok ? "OK" : "NOT OK", ok ? 0x07E0 : 0xF800);
        tft_puts(20, 200, ok ? "calibration saved" : "repeat calibration", 0xFFFF);
        tft_flush();
    }

    // возврат в меню: результат через SRAM-почту (core0 читает оттуда)
    TFT_STAT = 0x2F;   // r117 debug: калибровка завершена
    for (int i = 0; i < 5; i++) {
        TFT_CALRX[i] = (uint32_t)(int32_t)g_cal_rx[i];
        TFT_CALRY[i] = (uint32_t)(int32_t)g_cal_ry[i];
    }
    TFT_CALOK = (uint32_t)calok;
    TFT_CMD = 0;       // r118: снять команду — core0 выйдет из ожидания
    TFT_HELP_ID = 0;   // r120: на TFT снова меню-справка
    TFT_HELP_EPOCH++;
    for (int i = 0; i < 5; i++) printf("CAL %d rx=%d ry=%d\n",
        i, (int)g_cal_rx[i], (int)g_cal_ry[i]);
}

// r135: меню настроек на TFT — вызывается из главного цикла при TFT_SET_CMD==1.
// Рисует кнопки-строки (порядок = SET_* в settings.c), тап строит TFT_SET_EV.
// ВАЖНО: CPU1 НЕ ждёт ответа core0 вечно — после записи EV крутит цикл дальше
// (событие останется в слоте, core0 прочитает когда сможет). Это исключает
// взаимоблокировку ядер и «мертвый» экран.
static void tft_settings_mode(void) {
    static const char* const items[7] = {
        "Create ROM folders",
        "Input test (NES/A2600)",
        "A2600 difficulty",
        "Sega 6-button pad",
        "Keyboard remap",
        "Touch calibration",
        "ROM partition info",
    };
    TFT_STAT = 0x3E;

    int prev = 0;
    int last_ep = -1, last_mode = -1;

    // r139: ждём, что палец, которым тапнули кнопку Settings, УЖЕ ОТПУЩЕН.
    // Без этого тот же тап засчитывается за выбор 1-го пункта («сразу в меню
    // записи»). Таймаут 3 с на случай залипшего тача.
    {
        int dpx, dpy; uint16_t drx = 0, dry = 0, drz = 0;
        uint32_t dbg0 = h3_hs_timer_lo_us();
        while (tft_touch_scan(&dpx, &dpy, &drx, &dry, &drz) &&
               (h3_hs_timer_lo_us() - dbg0 < 3000000)) {
            extern void led_heartbeat_cpu1(void);
            led_heartbeat_cpu1();
            for (int d = 0; d < 500; d++) udelay(30);
        }
    }

    while (TFT_SET_CMD == 1) {
        extern void led_heartbeat_cpu1(void);
        led_heartbeat_cpu1();   // PL10 жив и в меню настроек

        int ep = TFT_SET_EPOCH;
        int mode = TFT_SET_MODE;
        if (ep != last_ep || mode != last_mode) {
            last_ep = ep; last_mode = mode;
            tft_fill_rect(0, 0, TFT_W, TFT_H, 0x0000);
            if (mode == 1) {                              // partition info
                tft_puts2(4, 2, "ROM partition setup", 0xF800);
                tft_puts(4, 32,  "1. Create FAT32 partition", 0xFFFF);
                tft_puts(4, 48,  "   (Win: DiskPart/GUI)", 0xAAAA);
                tft_puts(4, 72,  "2. Create folder 'roms'", 0xFFFF);
                tft_puts(4, 88,  "3. Put ROMs in /roms/<sys>/", 0xFFFF);
                tft_puts(4, 112, "4. Reboot the console", 0xFFFF);
                tft_puts(4, 136, "Then Settings -> Create folders", 0xFFE0);
                tft_puts2(4, TFT_H - 24, "Back", 0x07E0);
            } else if (mode == 2) {                       // create folders confirm #1
                tft_puts2(4, 2, "Create ROM folders?", 0xF800);
                tft_puts(4, 36, "Creates /roms/<system>/", 0xFFFF);
                tft_puts(4, 52, "for ALL registered systems.", 0xFFFF);
                tft_puts(4, 68, "No data will be deleted.", 0xAAAA);
                tft_puts2(4, TFT_H - 76, "Continue  (tap here)", 0x07E0);
                tft_puts2(4, TFT_H - 24, "Back", 0x07E0);
            } else if (mode == 4) {                       // SURE — второе подтверждение
                tft_puts2(4, 2, "Are you SURE?", 0xF800);
                tft_puts(4, 40, "This writes folders to SD", 0xFFFF);
                tft_puts(4, 56, "and is not reversible.", 0xFFFF);
                tft_puts2(4, TFT_H - 76, "CREATE  (tap here)", 0xF800);
                tft_puts2(4, TFT_H - 24, "Back", 0x07E0);
            } else if (mode == 3) {                       // create folders result
                uint32_t info = (uint32_t)TFT_SET_INFO;
                tft_puts2(4, 2, "Folders", 0xF800);
                char row[48];
                char* p = row;
                tft_strcat(&p, "Created: "); tft_itoa(&p, (info >> 0) & 0xFF); *p = 0;
                tft_puts(4, 36, row, 0x07E0);
                p = row;
                tft_strcat(&p, "Already: "); tft_itoa(&p, (info >> 8) & 0xFF); *p = 0;
                tft_puts(4, 56, row, 0xFFFF);
                p = row;
                tft_strcat(&p, "Failed: ");  tft_itoa(&p, (info >> 16) & 0xFF); *p = 0;
                if ((info >> 16) & 0xFF) tft_puts(4, 76, row, 0xF800);
                tft_puts2(4, TFT_H - 24, "Back", 0x07E0);
            } else {                                      // mode 0: список пунктов — КНОПКИ
                static const char icons[7] = {'F','T','D','S','K','C','I'};
                static const int  BTN_SP = 32, BTN_H = 28;
                tft_puts2(4, 2, "Settings (TFT)", 0xF800);
                for (int i = 0; i < 7; i++) {
                    int y = 24 + i * BTN_SP;
                    int on = (i == TFT_SET_SEL);
                    uint16_t frame = on ? 0xFFE0 : 0xFFFF;
                    tft_fill_rect(4, y, TFT_W - 8, BTN_H, on ? 0x1010 : 0x0200);
                    tft_fill_rect(4, y, TFT_W - 8, 1, frame);         // рамка
                    tft_fill_rect(4, y + BTN_H - 1, TFT_W - 8, 1, frame);
                    tft_fill_rect(4, y, 1, BTN_H, frame);
                    tft_fill_rect(TFT_W - 5, y, 1, BTN_H, frame);
                    // иконка слева
                    tft_fill_rect(9, y + 4, 20, BTN_H - 8, 0x0018);
                    char ic[2] = { icons[i], 0 };
                    tft_puts(13, y + (BTN_H - 8) / 2, ic, 0xFFFF);
                    // текст пункта (+ текущее значение для A2600 difficulty)
                    char row[48];
                    char* p = row;
                    tft_strcat(&p, items[i]);
                    if (i == 2) {
                        tft_strcat(&p, ": ");
                        tft_strcat(&p, TFT_DIFF ? "Expert" : "Novice");
                    }
                    *p = 0;
                    tft_puts(36, y + (BTN_H - 8) / 2, row, 0xFFFF);
                }
                tft_puts2(4, TFT_H - 24, "Back", 0x07E0);
            }
            tft_flush();
        }

        int px, py;
        uint16_t rawx = 0, rawy = 0, rawz = 0;
        int pressed = tft_touch_scan(&px, &py, &rawx, &rawy, &rawz);
        if (pressed && !prev) {
            if (mode == 0) {
                if (py >= TFT_H - 28) TFT_SET_EV = 8;              // Back
                else if (py >= 24) {
                    int i = (py - 24) / 32;                        // кнопки: старт 24, шаг 32
                    if (i < 8) TFT_SET_EV = i;                     // пункт 0..7
                }
            } else if (mode == 2 || mode == 4) {
                if (py >= TFT_H - 76) TFT_SET_EV = 1;              // Continue/CREATE
                if (py >= TFT_H - 28) TFT_SET_EV = 8;              // Back (приоритетнее)
            } else {
                if (py >= TFT_H - 28) TFT_SET_EV = 8;              // Back
            }
            // ждём ОТЖАТИЯ пальца: чтобы переход в следующий подрежим не
            // «дожимался» тем же нажатием (r137). Ограничено таймаутом 3 с —
            // если тач залип/дребезжит, CPU1 не зависнет навсегда.
            uint32_t rel_t0 = h3_hs_timer_lo_us();
            while (tft_touch_scan(&px, &py, &rawx, &rawy, &rawz) &&
                   (h3_hs_timer_lo_us() - rel_t0 < 3000000)) {
                led_heartbeat_cpu1();
                for (int d = 0; d < 500; d++) udelay(30);
            }
        }
        prev = pressed;
        for (int d = 0; d < 200; d++) udelay(30);   // ~6 мс
    }
    TFT_STAT = 0x3F;
}

// ---- CPU1 entry: probes, init, help display + touch ----
void tft_core_main(void) {
    TFT_STAT = 0x0A;
    // r115: .coherent не копируется из .data — статики приходят нулями.
    // Явно выставляем границы-по-умолчанию (r110, рабочий модуль).
    g_touch_xmin = 2350; g_touch_xmax = 3950;
    g_touch_ymin = 2450; g_touch_ymax = 3700;
    g_cal_ok = 0;
    spi0_init();
    SPI0_CCR = (6u << 8);          // 1,5 МГц — стартовые пробы тача
    TFT_STAT = 0x0B;
    TFT_PROBE  = tft_touch_probe_cs(1, 0x90);
    TFT_STAT = 0x0C;
    TFT_PROBE2 = tft_touch_probe_cs(1, 0xD0);
    TFT_PROBE3 = tft_touch_probe_cs(0, 0x90);
    TFT_STAT = 0x0D;

    if (tft_init() < 0) { TFT_STAT = 9; for (;;) __asm volatile("wfi"); }
    TFT_STAT = 2;
    // r120: SRAM-почта не zero-инициализируется — чистим при старте
    TFT_BTN = 0;
    TFT_HELP_ID = 0;
    TFT_HELP_EPOCH = 0;
    TFT_DIFF = 0;
    TFT_CMD = 0;   // r127: без этого мусор 0x34 (==1) сразу запускал бы калибровку
    TFT_SET_CMD = 0;   // r135: меню настроек на TFT
    TFT_SET_SEL = 0;
    TFT_SET_EV = (int32_t)-1;   // r141: «нет события» = -1 (0 = пункт Create folders!)
    TFT_SET_EPOCH = 0;
    TFT_SET_MODE = 0;
    TFT_SET_INFO = 0;
    SPI0_TCR = 0x0;
    SPI0_CCR = 0x1005;             // 8 MHz

// Обычное поведение: меню по умолчанию. Калибровочный режим — это
    // ОТДЕЛЬНЫЙ режим CPU1: core0 ставит TFT_CMD=1 (SRAM-почта) из Settings.
    int last = -1;
    int last_epoch = -1;
    tft_help_render(0, 1);
    last = 0;
    last_epoch = 0;

    int prev_pressed = 0;
    for (;;) {
        // r129: stall-детектор — если итерация цикла занимает >1.5 с, CPU1 где-то
        // застрял (SPI, flush). Печатаем, какой шаг стал долгим — иначе зависание
        // PL10 останется немой (UART без единой строки).
        uint32_t loop_t0 = h3_hs_timer_lo_us();
        // Индикатор «проц жив»: PL10 мигает с CPU1 (R_PIO, не трогает PA_DAT —
        // нет RMW-гонки с тачем/Sega-падом). Если это ядро крутится — проц
        // жив, даже если CPU0/эмулятор завис. При зависании CPU1 мигание
        // прекращается.
        extern void led_heartbeat_cpu1(void);
        led_heartbeat_cpu1();

        // Смена страницы справки по команде core0 (id или эпоха = перерисовать)
        // r120: читаем из SRAM-почты — .coherent между ядрами не работает.
        int cur = TFT_HELP_ID;
        int cur_e = TFT_HELP_EPOCH;
        // r118: калибровку запускаем по SRAM-почте TFT_CMD.
        if (TFT_CMD == 1) {
            last = cur; last_epoch = cur_e;   // проглотить висящие help-запросы
            tft_calib_mode();
        } else if (TFT_SET_CMD == 1) {        // r135: меню настроек на TFT
            last = cur; last_epoch = cur_e;
            tft_settings_mode();
        } else if (cur != last || cur_e != last_epoch) {
            int full = (cur != last);   // смена страницы = полный флаш, иначе refresh
            last = cur; last_epoch = cur_e;
            tft_help_render(cur, full);
        }

        // Сканирование тача; по фронту нажатия — попадание в иконку -> меню-код.
        // СЫРЫЕ rx/ry печатаются по каждому нажатию И удержанию (для калибровки),
        // событие в меню — только по фронту (0→1), чтобы не «зациклить» экран.
        int px, py;
        uint16_t rawx = 0, rawy = 0, rawz = 0;
        int pressed = tft_touch_scan(&px, &py, &rawx, &rawy, &rawz);
        if (h3_hs_timer_lo_us() - loop_t0 > 1500000)   // r129
            printf("TFT: slow after scan %u us\n", (unsigned)(h3_hs_timer_lo_us() - loop_t0));
        // r113: TCH-печать убрана (калибровка рисует результат на экран,
        // печатает CAL-строки). Здесь только иконки Settings/About.
        if (pressed && !prev_pressed) {
            if (px >= ICON_X && px < ICON_X + ICON_W) {
                // r122 debug: координаты тапа по иконкам (для отладки попадания)
                printf("TBTN: px=%d py=%d rx4=%d ry4=%d\n", px, py,
                       (int)(rawx >> 4), (int)(rawy >> 4));
                // r123: расширенные зоны — тач по дефолт-калибровке бьёт на ~10px
                // выше отрисованной иконки. Settings [16..74), About [74..130).
                if (py >= 16 && py < 74)                TFT_BTN = -2;  // Settings
                else if (py >= 74 && py < 130)           TFT_BTN = -4; // About
            }
        }
        prev_pressed = pressed;
        delay_ms(30);
        if (h3_hs_timer_lo_us() - loop_t0 > 1500000)   // r129
            printf("TFT: slow after delay %u us\n", (unsigned)(h3_hs_timer_lo_us() - loop_t0));
    }
}