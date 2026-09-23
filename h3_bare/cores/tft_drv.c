// tft_drv.c — TFT-ядро DFR0428 (ILI9486, 480×320 RGB565)
// через 74HC4094×2 + 74HC4040 → параллельная шина D0–D15.
// SPI0 H3 (PC0=MOSI, PC1=MISO, PC2=SCLK, PC3=CS дисплея, PC7=DC),
// PA21=CS тача XPT2046, PA2=RST. CPU1, MMU off.
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
//   4) Тач = XPT2046 на PA21, MISO на PC1.

#include <stdint.h>
#include <string.h>
#include "h3.h"
#include "h3_ccu.h"
#include "h3_hs_timer.h"
#include "tft_drv.h"
#include "uart.h"

extern int printf(const char*, ...);
extern const uint8_t font8x8[96][8];

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

// SRAM A1 почта (вне кэшей, MMU-disabled на CPU1)
#define TFT_STAT  (*(volatile uint32_t*)0x24u)  // st=1..3,9,0x11..0x16 — статус CPU1
#define TFT_PROBE  (*(volatile uint32_t*)0x28u) // PA21 X
#define TFT_PROBE2 (*(volatile uint32_t*)0x2Cu) // PA21 Y
#define TFT_PROBE3 (*(volatile uint32_t*)0x30u) // PC3 контроль

#define FB_W  1024
#define FB_H  600
#define TFT_W 480
#define TFT_H 320

static uint16_t tft_fb[TFT_H * TFT_W] __attribute__((aligned(16)));
static int g_mode = 1;      // 1 = вывод tft_fb, 0 = зеркало HDMI (0x5F900000)
static int g_tft_ready = 0;

// Флаг «HDMI-кадр готов»: core0 (fb_text.c) ставит в 1 после каждого
// FB-flush; TFT-ядро CPU1 опрашивает его для зеркала. Живёт в .coherent
// (uncached через MMU) — запись с core0 видна CPU1 сразу, без чистки кэша.
volatile uint32_t g_tft_frame_ready __attribute__((section(".coherent"), aligned(4)));

static void cs_low(void)  {
    PC_DAT &= ~(1u << PIN_CS);
    PA_DAT &= ~(1u << PIN_CS2);
}
static void cs_high(void) {
    PC_DAT |=  (1u << PIN_CS);
    PA_DAT |=  (1u << PIN_CS2);
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

static int spi0_txrx8(uint8_t tx, uint8_t* rx) {
    if (!wait_tx_room()) return -1;
    SPI0_TXD8 = tx;
    SPI0_MBC = 1;
    SPI0_BCC = 1;
    SPI0_TCR |= SPI0_TCR_XCH;
    if (!wait_done()) return -1;
    if (rx) *rx = SPI0_RXD8;
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
    for (int pass = 0; pass < 2; pass++) {
        uint8_t d0, d1, d2;
        cs_select(sel);
        if (spi0_txrx8(cmd, &d0) < 0 ||
            spi0_txrx8(0x00, &d1) < 0 ||
            spi0_txrx8(0x00, &d2) < 0) { cs_high(); return 0xFFFF; }
        cs_high();
        v = (uint16_t)((d1 << 8) | d2);
    }
    return v;
}

// ---- Протокол 16-битной шины (74HC4094×2 + 4040) ----
// Каждый обмен = 16 тактов SPI под одним CS:
//   [0x00, cmd] для команд (cmd на D0-D7)
//   [0x00, d]   для 8-битных параметров
//   [hi,  lo]   для 16-битных пикселей (hi в D8-D15)
// Подъём CS = лэтч в 4094 + WR.

static int xfer_cmd(uint8_t cmd) {
    cs_low();
    dc_cmd();
    int r = spi0_tx8(0x00);
    if (r == 0) r = spi0_tx8(cmd);
    dc_data();
    cs_high();
    return r < 0 ? r : 0;
}

static int xfer_data(uint8_t d) {
    cs_low();
    int r = spi0_tx8(0x00);
    if (r == 0) r = spi0_tx8(d);
    cs_high();
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
    if (xfer_cmd(CMD_MADCTL) < 0 || xfer_data(0x20) < 0)      { printf("TFT: SPI fail MADCTL\n"); spi_dump(); return -1; }
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
    if (xfer_cmd(CMD_MADCTL) < 0 || xfer_data(0x20) < 0) { printf("TFT: SPI fail MADCTL2\n"); spi_dump(); return -1; }
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

// Полный кадр — пиксель парой по-байтовых XCH (под одним CS), лэтч по CS↑
// после NOP. Это проверенный рабочий вариант (r36, 6 МГц); попытка одним
// 16-битным бурстом (tx16) собла панель — движок шлёт такты иначе.
void tft_flush(void) {
    if (!g_tft_ready) return;
    if (set_window() < 0) return;
    cs_low();
    dc_cmd();
    if (spi0_tx8(0x00) < 0 || spi0_tx8(CMD_RAMWR) < 0) { cs_high(); return; }
    dc_data();
    for (int ty = 0; ty < TFT_H; ty++) {
        if (g_mode) {
            const uint16_t* row = tft_fb + (uint32_t)ty * TFT_W;
            for (int tx = 0; tx < TFT_W; tx++) {
                uint16_t p = row[tx];
                cs_low();
                if (spi0_tx8((uint8_t)(p >> 8)) < 0 ||
                    spi0_tx8((uint8_t)(p & 0xFF)) < 0) { cs_high(); goto abort; }
                __asm volatile("nop; nop; nop; nop; nop"); /* ~50ns на стабильность шины */
                cs_high();
            }
        } else {
            /* Зеркало HDMI: даунскейл 1024x600 XRGB8888 -> 480x320 RGB565 */
            const uint32_t* src = (const uint32_t*)0x5F900000;
            int sy = (ty * 15) / 8;
            const uint32_t* row0 = src + (uint32_t)sy * FB_W;
            for (int tx = 0; tx < TFT_W; tx++) {
                int sx = (tx * 32) / 15;
                uint32_t c = row0[sx];
                uint16_t p = (uint16_t)(((c>>3)&0x1F)<<11) |
                             (uint16_t)(((c>>10)&0x3F)<<5) |
                             (uint16_t)((c>>19)&0x1F);
                cs_low();
                if (spi0_tx8((uint8_t)(p >> 8)) < 0 ||
                    spi0_tx8((uint8_t)(p & 0xFF)) < 0) { cs_high(); goto abort; }
                __asm volatile("nop; nop; nop; nop; nop");
                cs_high();
            }
        }
    }
    return;
abort:
    ;
}

void tft_tick(void) { tft_flush(); }
void tft_set_menu_mode(void) { g_mode = 1; }
void tft_set_dup_mode(void)  { g_mode = 0; }

void tft_render_begin(void) { memset(tft_fb, 0, TFT_W * TFT_H * 2); }

void tft_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) x = 0;  if (y < 0) y = 0;
    if (x >= TFT_W || y >= TFT_H) return;
    if (x + w > TFT_W) w = TFT_W - x;
    if (y + h > TFT_H) h = TFT_H - y;
    for (int yy = y; yy < y + h; yy++) {
        uint16_t* row = tft_fb + (uint32_t)yy * TFT_W;
        for (int xx = x; xx < x + w; xx++) row[xx] = color;
    }
}

void tft_puts(int x, int y, const char* s, uint16_t color) {
    while (*s) {
        char ch = *s++;
        if (ch < 0x20 || ch > 0x7F) { x += 8; continue; }
        const uint8_t* gl = font8x8[ch - 0x20];
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

static void tft_fill_screen(uint16_t color) {
    tft_render_begin();
    tft_fill_rect(0, 0, TFT_W, TFT_H, color);
    tft_flush();
}

// ---- CPU1 entry: probes, init, test fills ----
void tft_core_main(void) {
    TFT_STAT = 0x0A;
    spi0_init();                     /* SPI0 — тактовая из spi0_init */

    // Пробы XPT2046 — ему надо ≤2МГц
    SPI0_CCR = (5u << 8);            /* 750 кГц — заведомо в норме для тача */
    TFT_STAT = 0x0B;
    TFT_PROBE  = tft_touch_probe_cs(1, 0x90);
    TFT_STAT = 0x0C;
    TFT_PROBE2 = tft_touch_probe_cs(1, 0xD0);
    TFT_PROBE3 = tft_touch_probe_cs(0, 0x90);
    TFT_STAT = 0x0D;

    // Замер реальной скорости SPI (uS на байт) — источник тактов заранее не известен.
    {
        uint32_t n = 2000, t1 = h3_hs_timer_lo_us();
        for (uint32_t i = 0; i < n; i++) spi0_tx8(0xAA);
        uint32_t dt = h3_hs_timer_lo_us() - t1;
        printf("SPI: %u uS/byte (~%u kHz)\n",
               (unsigned)(dt / n), (unsigned)(8000u / (dt / n)));
    }
    // Панель — скорость выставляет spi0_init внутри tft_init

    if (tft_init() < 0) {
        TFT_STAT = 9;
        for (;;) __asm volatile("wfi");
    }

    TFT_STAT = 2;
    tft_set_menu_mode();
    SPI0_TCR = 0x0;

    // Частота 8 МГц (CCR=0x1005) + усиленный драйвер PC0..PC3
    SPI0_CCR = 0x1005;
    tft_fill_screen(0xF800);
    delay_ms(1500);

    for (;;) {
        TFT_STAT = 3;
        uint32_t t0 = h3_hs_timer_lo_us();
        tft_fill_screen(0xF800);
        tft_fill_screen(0x07E0);
        tft_fill_screen(0x001F);
        uint32_t dt = h3_hs_timer_lo_us() - t0;
        printf("TFT: 3frames=%u ms\n", (unsigned)(dt / 1000));
        delay_ms(500);
        tft_fill_screen(0xFFFF); delay_ms(1000);
        tft_fill_screen(0x0000); delay_ms(1000);
        tft_render_begin();
        tft_fill_rect(0,          0, TFT_W / 3, TFT_H, 0xF800);
        tft_fill_rect(TFT_W / 3,  0, TFT_W / 3, TFT_H, 0x07E0);
        tft_fill_rect(2 * TFT_W / 3, 0, TFT_W / 3, TFT_H, 0x001F);
        tft_flush();
        delay_ms(1000);
    }
}