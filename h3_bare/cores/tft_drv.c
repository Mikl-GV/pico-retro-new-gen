// tft_drv.c — DFR0428 (ILI9486, 480×320 RGB565) на SPI0 H3 (PC0-PC3).
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
#define SPI0_TXD8  (*(volatile uint8_t*)0x01C68200u)
#define SPI0_RXD8  (*(volatile uint8_t*)0x01C68204u)
#define SPI0_TCR_XCH (1u << 31)
#define SPI0_FSR_TF_CNT_MASK (0xFFu << 16)

#define PA_BASE  0x01C20800u
#define PA_CFG0  (*(volatile uint32_t*)(PA_BASE + 0x00u))
#define PA_CFG1  (*(volatile uint32_t*)(PA_BASE + 0x04u))
#define PA_DAT   (*(volatile uint32_t*)(PA_BASE + 0x10u))
#define PC_BASE  0x01C20848u
#define PC_CFG0  (*(volatile uint32_t*)(PC_BASE + 0x00u))
#define PC_DAT   (*(volatile uint32_t*)(PC_BASE + 0x10u))

#define PIN_CS  3   // PC3
#define PIN_DC  7   // PC7
#define PIN_RST 2   // PA2

#define FB_W  1024
#define FB_H  600
#define TFT_W 480
#define TFT_H 320

#define TFT_MENU_FB 0x5FC00000u
static uint16_t* tft_fb = (uint16_t*)TFT_MENU_FB;
static int g_mode = 1;
static int g_tft_ready = 0;

// Флаг «кадр HDMI готов» для TFT-ядра (CPU1). Лежит в .coherent —
// секция uncached через MMU, поэтому запись с core0 видна core1 сразу.
volatile uint32_t g_tft_frame_ready __attribute__((section(".coherent"), aligned(4)));

static void cs_low(void)  { PC_DAT &= ~(1u << PIN_CS); }
static void cs_high(void) { PC_DAT |=  (1u << PIN_CS); }
static void dc_cmd(void)  { PC_DAT &= ~(1u << PIN_DC); }
static void dc_data(void) { PC_DAT |=  (1u << PIN_DC); }

// Таймауты ограничены: при отсутствии дисплея/неисправном SPI драйвер
// не должен висеть (иначе блокирует загрузку).
static int wait_tx_room(void) {
    for (uint32_t t = 0; t < 20000; t++)
        if (((SPI0_FSR & SPI0_FSR_TF_CNT_MASK) >> 16) < 64u) return 1;
    return 0;
}

static int wait_done(void) {
    for (uint32_t t = 0; t < 200000; t++)
        if (!(SPI0_TCR & SPI0_TCR_XCH)) return 1;
    return 0;
}

// Каждый байт — отдельный burst с XCH (как в рабочей инициализации).
// CS остаётся низким между байтами. Это медленно (~50 мс кадр), но надёжно.
// Возвращает 0 = ок, -1 = SPI не отвечает.
static int spi0_tx8(uint8_t b) {
    if (!wait_tx_room()) return -1;
    SPI0_TXD8 = b;
    SPI0_MBC = 1;
    SPI0_BCC = 1;
    SPI0_TCR |= SPI0_TCR_XCH;
    return wait_done() ? 0 : -1;
}

// Байтовая передача с приёмом MISO (нужно для чтения ID дисплея).
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

// Чтение N байт по команде (DC низкий только на команде).
static int tft_read_bytes(uint8_t cmd, uint8_t* out, int n) {
    cs_low();
    dc_cmd();
    if (spi0_txrx8(cmd, 0) < 0) { cs_high(); return -1; }
    dc_data();
    for (int i = 0; i < n; i++)
        if (spi0_txrx8(0x00, &out[i]) < 0) { cs_high(); return -1; }
    cs_high();
    return 0;
}

static int xfer_cmd(uint8_t cmd) {
    cs_low();
    dc_cmd();
    int r = spi0_tx8(cmd);
    dc_data();
    cs_high();
    return r;
}

static int xfer_data(uint8_t d) {
    cs_low();
    int r = spi0_tx8(d);
    cs_high();
    return r;
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

    // PC0=SPI0_MOSI, PC1=SPI0_MISO, PC2=SPI0_CLK (func3), PC3=output(CS), PC7=output(DC)
    PC_CFG0 = (3u << 0) | (3u << 4) | (3u << 8) | (1u << 12) | (1u << 28);
    PA_CFG0 &= ~(0xFu << 8);
    PA_CFG0 |= (1u << 8);   // PA2 = output (RESET)

    PC_DAT |= (1u << PIN_CS);
    PC_DAT &= ~(1u << PIN_DC);
    PA_DAT |= (1u << PIN_RST);

    SPI0_GCR = (1u << 0) | (1u << 1);
    udelay(100);
    SPI0_TCR = 0;
    SPI0_CCR = (5u << 8);
    udelay(100);
}

// Универсальная инициализация 480x320 (перекрывает ILI9486 и ILI9488).
// ILI9488 без регистров F7/B0/B1/B6/B4/C0/C1/C5 не включает вывод
// (белый экран); ILI9486 эти команды игнорирует — последовательность
// безопасна для обоих.
static int tft_ili_init(void) {
    PA_DAT &= ~(1u << PIN_RST);
    delay_ms(20);
    PA_DAT |= (1u << PIN_RST);
    delay_ms(150);
    if (xfer_cmd(CMD_SWRESET) < 0) { printf("TFT: SPI fail @SWRESET\n"); return -1; }
    delay_ms(150);

    if (xfer_cmd(CMD_SLPOUT) < 0) { printf("TFT: SPI fail @SLPOUT\n"); return -1; }
    delay_ms(150);

    if (xfer_cmd(0xB0) < 0 || xfer_data(0x00) < 0)                      { printf("TFT: SPI fail B0\n"); return -1; }
    if (xfer_cmd(0xB1) < 0 || xfer_data(0x00) < 0 || xfer_data(0x11) < 0){ printf("TFT: SPI fail B1\n"); return -1; }
    if (xfer_cmd(0xB4) < 0 || xfer_data(0x02) < 0)                      { printf("TFT: SPI fail B4\n"); return -1; }
    if (xfer_cmd(0xB6) < 0 || xfer_data(0x02) < 0 || xfer_data(0x02) < 0 ||
        xfer_data(0x3B) < 0)                                           { printf("TFT: SPI fail B6\n"); return -1; }
    if (xfer_cmd(0xC0) < 0 || xfer_data(0x0B) < 0)                      { printf("TFT: SPI fail C0\n"); return -1; }
    if (xfer_cmd(0xC1) < 0 || xfer_data(0x41) < 0)                      { printf("TFT: SPI fail C1\n"); return -1; }
    if (xfer_cmd(0xC5) < 0 || xfer_data(0x00) < 0 || xfer_data(0x12) < 0){ printf("TFT: SPI fail C5\n"); return -1; }
    if (xfer_cmd(0xF7) < 0 || xfer_data(0xA9) < 0 || xfer_data(0x51) < 0 ||
        xfer_data(0x2C) < 0 || xfer_data(0x82) < 0)                     { printf("TFT: SPI fail F7\n"); return -1; }

    if (xfer_cmd(CMD_MADCTL) < 0 || xfer_data(0xC8) < 0) { printf("TFT: SPI fail MADCTL\n"); return -1; }
    if (xfer_cmd(CMD_COLMOD) < 0 || xfer_data(0x55) < 0) { printf("TFT: SPI fail COLMOD\n"); return -1; }

    {
        static const uint8_t gp[15] = {0x00,0x07,0x10,0x09,0x17,0x0B,0x41,0x89,
                                       0x43,0x08,0x12,0x08,0x17,0x14,0x0F};
        static const uint8_t gn[15] = {0x00,0x17,0x1D,0x04,0x0B,0x04,0x47,0x33,
                                       0x44,0x0A,0x0C,0x08,0x12,0x14,0x0F};
        if (xfer_cmd(0xE0) < 0) { printf("TFT: SPI fail E0\n"); return -1; }
        for (int i = 0; i < 15; i++) if (xfer_data(gp[i]) < 0) { printf("TFT: SPI fail E0\n"); return -1; }
        if (xfer_cmd(0xE1) < 0) { printf("TFT: SPI fail E1\n"); return -1; }
        for (int i = 0; i < 15; i++) if (xfer_data(gn[i]) < 0) { printf("TFT: SPI fail E1\n"); return -1; }
    }

    if (xfer_cmd(CMD_INVON) < 0) { printf("TFT: SPI fail INVON (0x%X)\n", (unsigned)SPI0_TCR); return -1; }
    if (xfer_cmd(CMD_DISPON) < 0) { printf("TFT: SPI fail DISPON (0x%X TCR=0x%X FSR=0x%X)\n",
        (unsigned)CMD_DISPON, (unsigned)SPI0_TCR, (unsigned)SPI0_FSR); return -1; }
    delay_ms(50);
    printf("TFT: init done\n");
    return 0;
}

static int set_window(void) {
    int r = 0;
    if (xfer_cmd(CMD_CASET) < 0) r = -1;
    if (xfer_data(0) < 0 || xfer_data(0) < 0 ||
        xfer_data((TFT_W-1)>>8) < 0 || xfer_data((TFT_W-1)&0xFF) < 0) r = -1;
    if (xfer_cmd(CMD_RASET) < 0) r = -1;
    if (xfer_data(0) < 0 || xfer_data(0) < 0 ||
        xfer_data((TFT_H-1)>>8) < 0 || xfer_data((TFT_H-1)&0xFF) < 0) r = -1;
    return r;
}

// Возвращает 0 = готово, -1 = дисплей не отвечает (не блокируем загрузку).
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

// Полный кадр — каждый байт отдельным burst (медленно, но надёжно)
void tft_flush(void) {
    if (!g_tft_ready) return;
    if (set_window() < 0) return;
    cs_low();
    dc_cmd();
    if (spi0_tx8(CMD_RAMWR) < 0) { cs_high(); return; }
    dc_data();
    for (int ty = 0; ty < TFT_H; ty++) {
        if (g_mode) {
            const uint16_t* row = tft_fb + (uint32_t)ty * TFT_W;
            for (int tx = 0; tx < TFT_W; tx++) {
                uint16_t p = row[tx];
                if (spi0_tx8((uint8_t)(p >> 8)) < 0) goto abort;
                if (spi0_tx8((uint8_t)(p & 0xFF)) < 0) goto abort;
            }
        } else {
            const uint32_t* src = (const uint32_t*)0x5F900000;
            int sy = (ty * 15) / 8;
            const uint32_t* row = src + (uint32_t)sy * FB_W;
            for (int tx = 0; tx < TFT_W; tx++) {
                int sx = (tx * 32) / 15;
                uint32_t c = row[sx];
                uint16_t p = (uint16_t)(((c>>3)&0x1F)<<11)|(uint16_t)(((c>>10)&0x3F)<<5)|(uint16_t)((c>>19)&0x1F);
                if (spi0_tx8((uint8_t)(p >> 8)) < 0) goto abort;
                if (spi0_tx8((uint8_t)(p & 0xFF)) < 0) goto abort;
            }
        }
    }
    cs_high();
    return;
abort:
    cs_high();
}

void tft_tick(void) { tft_flush(); }
void tft_set_menu_mode(void) { g_mode = 1; }
void tft_set_dup_mode(void)  { g_mode = 0; }

void tft_render_begin(void) { memset(tft_fb, 0, TFT_W * TFT_H * 2); }

void tft_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
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

// ---- TFT core: исполняется на CPU1 (startup.S cpu1_entry) ----
// Самопроверка панели: сплошные заливки + цветовые полосы. К HDMI не
// привязано — проверяем весь путь: init -> RAMWR -> 595-шина -> панель.
void tft_core_main(void) {
    uart_puts("TFT-CORE: enter\n");
    if (tft_init() < 0) {
        uart_puts("TFT-CORE: no display, idle\n");
        for (;;) __asm volatile("wfi");
    }
    uart_puts("TFT-CORE: init ok\n");
    tft_set_menu_mode();
    uart_puts("TFT-CORE: starting test loop\n");
    for (;;) {
        uart_puts("TFT-TEST: RED\n");       tft_fill_screen(0xF800); delay_ms(1000);
        uart_puts("TFT-TEST: GREEN\n");     tft_fill_screen(0x07E0); delay_ms(1000);
        uart_puts("TFT-TEST: BLUE\n");      tft_fill_screen(0x001F); delay_ms(1000);
        uart_puts("TFT-TEST: WHITE\n");     tft_fill_screen(0xFFFF); delay_ms(1000);
        uart_puts("TFT-TEST: BLACK\n");     tft_fill_screen(0x0000); delay_ms(1000);
        uart_puts("TFT-TEST: BARS\n");
        tft_render_begin();
        tft_fill_rect(0,          0, TFT_W / 3, TFT_H, 0xF800);
        tft_fill_rect(TFT_W / 3,  0, TFT_W / 3, TFT_H, 0x07E0);
        tft_fill_rect(2 * TFT_W / 3, 0, TFT_W / 3, TFT_H, 0x001F);
        tft_flush();
        delay_ms(1000);
    }
}