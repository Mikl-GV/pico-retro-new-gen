// sd.c — чтение SD/MMC для Allwinner H3 (MMC0, Orange Pi Lite).
// U-Boot уже инициализировал MMC0 и настроил GPIO/клок/карту.
// Мы НЕ переинициализируем — только CMD17 + PIO через STATUS-поллинг (как U-Boot).
#include <string.h>
#include "h3.h"
#include "uart.h"
#include "sd.h"
#include "led.h"

typedef struct {
    volatile uint32_t gctrl;
    volatile uint32_t clkcr;
    volatile uint32_t timeout;
    volatile uint32_t width;
    volatile uint32_t blksz;
    volatile uint32_t bytecnt;
    volatile uint32_t cmd;
    volatile uint32_t arg;
    volatile uint32_t resp0;
    volatile uint32_t resp1;
    volatile uint32_t resp2;
    volatile uint32_t resp3;
    volatile uint32_t imask;
    volatile uint32_t mint;
    volatile uint32_t rint;
    volatile uint32_t status;
    volatile uint32_t ftrglevel;
    volatile uint32_t funcsel;
    volatile uint32_t res0[6];
    volatile uint32_t res1[6];
    volatile uint32_t hwrst;
    volatile uint32_t res2;
    volatile uint32_t dmac;
    volatile uint32_t dlba;
    volatile uint32_t idst;
    volatile uint32_t idie;
    volatile uint32_t res3[28];
    volatile uint32_t thldc;
    volatile uint32_t res4[63];
    volatile uint32_t fifo;
} mmc_regs_t;

#define MMC ((mmc_regs_t*)0x01C0F000)

#define CMD_START          (1u << 31)
#define CMD_CHECK_CRC      (1u << 8)
#define CMD_RESP_EXPIRE    (1u << 6)
#define CMD_WRITE           (1u << 10)
#define CMD_DATA_EXPIRE    (1u << 9)
#define CMD_WAIT_PRE       (1u << 13)

#define RINT_CMD_DONE      (1u << 2)
#define RINT_DATA_OVER     (1u << 3)
#define RINT_RESP_CRC_ERR  (1u << 6)
#define RINT_DATA_CRC_ERR  (1u << 7)
#define RINT_RESP_TIMEOUT  (1u << 8)
#define RINT_DATA_TIMEOUT  (1u << 9)
#define RINT_FIFO_RUN_ERR  (1u << 11)
#define RINT_ERR_MASK (RINT_RESP_CRC_ERR | RINT_DATA_CRC_ERR | \
                       RINT_RESP_TIMEOUT | RINT_DATA_TIMEOUT | RINT_FIFO_RUN_ERR)

#define STATUS_FIFO_EMPTY       (1u << 2)
#define STATUS_FIFO_FULL        (1u << 3)
#define STATUS_FIFO_LEVEL(s)    (((s) >> 17) & 0x3FFF)

static int g_sdhc = 1;
static uint8_t g_sec[512];

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

// Poll STATUS FIFO level, читаем слова — как U-Boot mmc_trans_data_by_cpu
static int mmc_read_data(uint8_t* buf, uint32_t bytes) {
    uint32_t word_cnt = bytes >> 2;
    uint32_t* dst = (uint32_t*)buf;
    uint32_t i = 0;

    while (i < word_cnt) {
        // ждём пока STATUS скажет, что данные в FIFO есть (не EMPTY)
        uint32_t st;
        uint32_t t = 0;
        do {
            st = MMC->status;
            if (t++ > 5000000) return -1;
        } while (st & STATUS_FIFO_EMPTY);

        // сколько слов в FIFO
        uint32_t n = STATUS_FIFO_LEVEL(st);
        if (n == 0 && (st & STATUS_FIFO_FULL)) n = 32;
        if (n == 0) n = 1;  // хотя бы одно

        for (uint32_t j = 0; j < n && i < word_cnt; j++)
            dst[i++] = MMC->fifo;
    }

    // ждём DATA_OVER — сигнал, что передача завершена
    return mmc_wait_rint(RINT_DATA_OVER);
}

int sd_init(void) {
    // Ничего не сбрасываем — наследуем настройки U-Boot.
    MMC->blksz = 512;
    MMC->bytecnt = 512;

    if (mmc_send_cmd(17, 0, CMD_RESP_EXPIRE | CMD_CHECK_CRC | CMD_DATA_EXPIRE | CMD_WAIT_PRE) < 0) {
        uart_puts("sd: CMD17 fail\n");
        return -1;
    }
    if (mmc_read_data(g_sec, 512) < 0) {
        uart_puts("sd: CMD17 read fail\n");
        return -1;
    }

    uart_puts("sd: ready (from U-Boot)\n");
    return 0;
}

int sd_read_sector(uint32_t lba, void* buf) {
    led_sd_on();
    uint32_t addr = g_sdhc ? lba : (lba << 9);
    mmc_clr_rint();
    MMC->blksz = 512;
    MMC->bytecnt = 512;
    if (mmc_send_cmd(17, addr, CMD_RESP_EXPIRE | CMD_CHECK_CRC | CMD_DATA_EXPIRE | CMD_WAIT_PRE) < 0)
        { led_sd_off(); return -1; }
    if (mmc_read_data((uint8_t*)buf, 512) < 0) { led_sd_off(); return -1; }
    led_sd_off();
    return 0;
}

// Запись данных через FIFO (TX): ожидаем FIFO не FULL, пишем слова.
static int mmc_write_data(const uint8_t* buf, uint32_t bytes) {
    uint32_t word_cnt = bytes >> 2;
    const uint32_t* src = (const uint32_t*)buf;
    uint32_t i = 0;

    while (i < word_cnt) {
        // ждём пока FIFO не полон (есть место для записи)
        uint32_t st;
        uint32_t t = 0;
        do {
            st = MMC->status;
            if (t++ > 5000000) return -1;
        } while (st & STATUS_FIFO_FULL);

        uint32_t n = 32 - STATUS_FIFO_LEVEL(st);
        if (n == 0) n = 1;
        if (n > word_cnt - i) n = word_cnt - i;

        for (uint32_t j = 0; j < n; j++)
            MMC->fifo = src[i++];
    }

    // ждём DATA_OVER — передача завершена
    return mmc_wait_rint(RINT_DATA_OVER);
}

// Ожидание завершения занятости карты (из U-Boot): после записи ждём
// пока STATUS_CARD_DATA_BUSY не сбросится.
static int mmc_wait_not_busy(void) {
    // BIT9 = CARD_DATA_BUSY
    uint32_t t = 0;
    while (MMC->status & (1u << 9)) {
        if (t++ > 5000000) return -1;
    }
    return 0;
}

int sd_write_sector(uint32_t lba, const void* buf) {
    uint32_t addr = g_sdhc ? lba : (lba << 9);
    mmc_clr_rint();
    MMC->blksz = 512;
    MMC->bytecnt = 512;
    // CMD24 (WRITE_BLOCK) + CMD_WRITE
    if (mmc_send_cmd(24, addr, CMD_RESP_EXPIRE | CMD_CHECK_CRC | CMD_DATA_EXPIRE | CMD_WAIT_PRE | CMD_WRITE) < 0)
        return -1;
    if (mmc_write_data((const uint8_t*)buf, 512) < 0) return -1;
    if (mmc_wait_not_busy() < 0) return -1;
    return 0;
}