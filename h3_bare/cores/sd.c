// sd.c — SD/MMC драйвер для Allwinner H3 (MMC0, Orange Pi Lite).
// Регистры из sunxi_mmc (u-boot). FIFO @ 0x200.
#include <string.h>
#include "h3.h"
#include "h3_ccu.h"
#include "uart.h"
#include "sd.h"

// ---- sunxi_mmc register block ----
typedef struct {
    volatile uint32_t gctrl;    // 0x00
    volatile uint32_t clkcr;    // 0x04
    volatile uint32_t timeout;  // 0x08
    volatile uint32_t width;    // 0x0C
    volatile uint32_t blksz;    // 0x10
    volatile uint32_t bytecnt;  // 0x14
    volatile uint32_t cmd;      // 0x18
    volatile uint32_t arg;      // 0x1C
    volatile uint32_t resp0;    // 0x20
    volatile uint32_t resp1;    // 0x24
    volatile uint32_t resp2;    // 0x28
    volatile uint32_t resp3;    // 0x2C
    volatile uint32_t imask;    // 0x30
    volatile uint32_t mint;     // 0x34
    volatile uint32_t rint;     // 0x38
    volatile uint32_t status;   // 0x3C
    volatile uint32_t ftrglevel;// 0x40
    volatile uint32_t funcsel;  // 0x44
    volatile uint32_t res0[6];  // 0x48-0x5C
    volatile uint32_t res1[6];  // 0x60-0x74
    volatile uint32_t hwrst;    // 0x78
    volatile uint32_t res2;     // 0x7C
    volatile uint32_t dmac;     // 0x80
    volatile uint32_t dlba;     // 0x84
    volatile uint32_t idst;     // 0x88
    volatile uint32_t idie;     // 0x8C
    volatile uint32_t res3[28]; // 0x90-0xFC
    volatile uint32_t thldc;    // 0x100
    volatile uint32_t res4[63]; // 0x104-0x1FC
    volatile uint32_t fifo;     // 0x200
} mmc_regs_t;

#define MMC ((mmc_regs_t*)0x01C0F000)

// ---- GCTRL bits ----
#define GCTRL_SOFT_RESET   (1u << 0)
#define GCTRL_FIFO_RESET   (1u << 1)
#define GCTRL_DMA_RESET    (1u << 2)
#define GCTRL_RESET_ALL    (GCTRL_SOFT_RESET | GCTRL_FIFO_RESET | GCTRL_DMA_RESET)
#define GCTRL_ACCESS_AHB   (1u << 31)

// ---- CLKCR bits ----
#define CLKCR_ENABLE       (1u << 16)
#define CLKCR_DIV_MASK     0xFF

// ---- CMD bits ----
#define CMD_START          (1u << 31)
#define CMD_UPCLK_ONLY     (1u << 21)
#define CMD_INIT_SEQ       (1u << 15)
#define CMD_WAIT_PRE       (1u << 13)
#define CMD_AUTO_STOP      (1u << 12)
#define CMD_WRITE          (1u << 10)
#define CMD_DATA_EXPIRE    (1u << 9)
#define CMD_CHECK_CRC      (1u << 8)
#define CMD_LONG_RESP      (1u << 7)
#define CMD_RESP_EXPIRE    (1u << 6)

// ---- RINT bits ----
#define RINT_RESP_ERR      (1u << 1)
#define RINT_CMD_DONE      (1u << 2)
#define RINT_DATA_OVER     (1u << 3)
#define RINT_TX_REQ        (1u << 4)
#define RINT_RX_REQ        (1u << 5)
#define RINT_RESP_CRC_ERR  (1u << 6)
#define RINT_DATA_CRC_ERR  (1u << 7)
#define RINT_RESP_TIMEOUT  (1u << 8)
#define RINT_DATA_TIMEOUT  (1u << 9)
#define RINT_FIFO_RUN_ERR  (1u << 11)
#define RINT_HW_LOCK       (1u << 12)
#define RINT_AUTO_DONE     (1u << 14)
#define RINT_END_BIT_ERR   (1u << 15)

#define RINT_ERR_MASK (RINT_RESP_ERR | RINT_RESP_CRC_ERR | RINT_DATA_CRC_ERR | \
                       RINT_RESP_TIMEOUT | RINT_DATA_TIMEOUT | \
                       RINT_FIFO_RUN_ERR | RINT_HW_LOCK | RINT_END_BIT_ERR)

#define STATUS_FIFO_LEVEL(s) (((s) >> 17) & 0x3FFF)
#define STATUS_FIFO_EMPTY    (1u << 2)
#define STATUS_FIFO_FULL     (1u << 3)

static int g_sdhc = 0;

static void mmc_clr_rint(void) { MMC->rint = 0xFFFFFFFF; }

static int mmc_wait_rint(uint32_t want) {
    for (uint32_t t = 0; t < 5000000; t++) {
        uint32_t r = MMC->rint;
        if (r & RINT_ERR_MASK) { MMC->rint = r; return -1; }
        if (r & want)          { MMC->rint = r; return 0;  }
    }
    return -1;
}

static int mmc_send_cmd(uint32_t idx, uint32_t arg, uint32_t flags) {
    mmc_clr_rint();
    MMC->arg = arg;
    MMC->cmd = (idx & 0x3F) | CMD_START | flags;
    return mmc_wait_rint(RINT_CMD_DONE);
}

static int mmc_read_fifo(uint8_t* buf, uint32_t bytes) {
    uint32_t done = 0;
    while (done < bytes) {
        int r = mmc_wait_rint(RINT_RX_REQ | RINT_DATA_OVER);
        if (r < 0) return -1;
        uint32_t rv = MMC->rint;
        uint32_t stat = MMC->status;
        if (rv & RINT_RX_REQ) {
            uint32_t cnt = STATUS_FIFO_LEVEL(stat);
            if (cnt == 0 && (stat & STATUS_FIFO_FULL)) cnt = 32;
            uint32_t* dst = (uint32_t*)(buf + done);
            for (uint32_t i = 0; i < cnt && done < bytes; i++) {
                dst[i] = MMC->fifo;
                done += 4;
            }
            MMC->rint = rv & ~RINT_RX_REQ;
        } else if (rv & RINT_DATA_OVER) {
            MMC->rint = rv & ~RINT_DATA_OVER;
            break;
        }
    }
    return (done >= bytes) ? 0 : -1;
}

int sd_init(void) {
    // 1. CCU: BUS_CLK_GATING0 bit 8 = MMC0 gate
    H3_CCU->BUS_CLK_GATING0 |= (1u << 8);
    //  BUS_SOFT_RST0 bit 8 = MMC0 reset
    H3_CCU->BUS_SOFT_RESET0 |= (1u << 8);
    udelay(1000);

    // 2. GPIO: PF0-PF5, alt 2 (mmc0)
    {
        uint32_t cfg = H3_PIO_PORTF->CFG0;
        for (int i = 0; i < 6; i++) {
            cfg &= ~(0xF << (i * 4));
            cfg |= (2u << (i * 4));
        }
        H3_PIO_PORTF->CFG0 = cfg;
        // Pull-up на CMD и DAT
        H3_PIO_PORTF->PUL0 = (H3_PIO_PORTF->PUL0 & ~0xFFF) | 0x555;
    }

    // 3. SDMMC0_CLK (0x088): source=osc24M (0), M=0, enable
    H3_CCU->SDMMC0_CLK = (1u << 31);
    udelay(1000);

    // 4. Reset controller
    MMC->gctrl = GCTRL_RESET_ALL;
    for (int i = 0; i < 100000 && (MMC->gctrl & GCTRL_SOFT_RESET); i++) {}
    MMC->gctrl = GCTRL_ACCESS_AHB;
    MMC->timeout = 0xFFFFFFFF;

    // 5. Clock ~400kHz: clkcr = 0x1D | CLK_ENABLE -> divide by 30
    //    module_clk = 24MHz, card_clk = 24M / (2*(30+1)) = 387kHz
    MMC->clkcr = 0x1D | CLKCR_ENABLE;
    udelay(5000);

    // 6. CMD0: GO_IDLE
    if (mmc_send_cmd(0, 0, CMD_INIT_SEQ) < 0) { uart_puts("sd: CMD0 fail\n"); return -1; }

    // 7. CMD8: SEND_IF_COND (SDHC/SDXC check)
    if (mmc_send_cmd(8, 0x1AA, CMD_RESP_EXPIRE | CMD_CHECK_CRC) < 0) {
        // SDSC (older card)
    } else {
        if ((MMC->resp0 & 0xFFF) != 0x1AA) { uart_puts("sd: CMD8 bad\n"); return -1; }
    }

    // 8. ACMD41: init + SDHC detect
    for (uint32_t i = 0; i < 2000; i++) {
        if (mmc_send_cmd(55, 0, CMD_RESP_EXPIRE | CMD_CHECK_CRC) < 0) continue;
        if (mmc_send_cmd(41, 0x40FF8000 | (1u << 30), CMD_RESP_EXPIRE) < 0) {
            udelay(1000);
            continue;
        }
        if (MMC->resp0 & (1u << 31)) {
            g_sdhc = (MMC->resp0 & (1u << 30)) ? 1 : 0;
            uart_puts("sd: "); uart_puts(g_sdhc ? "SDHC" : "SDSC"); uart_puts("\n");
            break;
        }
        udelay(1000);
    }
    if (!(MMC->resp0 & (1u << 31))) { uart_puts("sd: ACMD41 timeout\n"); return -1; }

    // 9. CMD2: ALL_SEND_CID
    if (mmc_send_cmd(2, 0, CMD_LONG_RESP | CMD_CHECK_CRC) < 0) { uart_puts("sd: CMD2 fail\n"); return -1; }

    // 10. CMD3: RELATIVE_ADDR
    if (mmc_send_cmd(3, 0, CMD_RESP_EXPIRE | CMD_CHECK_CRC) < 0) { uart_puts("sd: CMD3 fail\n"); return -1; }
    uint32_t rca = (MMC->resp0 >> 16) & 0xFFFF;

    // 11. CMD7: SELECT_CARD
    if (mmc_send_cmd(7, rca << 16, CMD_RESP_EXPIRE | CMD_CHECK_CRC) < 0) { uart_puts("sd: CMD7 fail\n"); return -1; }

    // 12. Clock ~12MHz: clkcr = 0 | CLK_ENABLE
    MMC->clkcr = CLKCR_ENABLE;
    udelay(1000);

    MMC->blksz = 512;
    MMC->bytecnt = 512;

    uart_puts("sd: ready\n");
    return 0;
}

int sd_read_sector(uint32_t lba, void* buf) {
    uint32_t addr = g_sdhc ? lba : (lba << 9);
    MMC->blksz = 512;
    MMC->bytecnt = 512;
    if (mmc_send_cmd(17, addr, CMD_RESP_EXPIRE | CMD_CHECK_CRC | CMD_DATA_EXPIRE) < 0)
        return -1;
    if (mmc_read_fifo((uint8_t*)buf, 512) < 0) return -1;
    return 0;
}