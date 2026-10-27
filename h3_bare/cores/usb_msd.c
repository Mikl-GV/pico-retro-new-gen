// usb_msd.c — USB Mass Storage BOT поверх EHCI (HS) / OHCI (FS-LS).
// r788: порт из эталона Bare_linux (src/usb_msd.c), адаптация под проект.
//
// CBW/CSW протокол + READ(10)/WRITE(10). Данные ходят через bounce-буфер
// .coherent (usb_msd_block_read/write копируют в/из caller-буфера): буфер
// вызывающего может быть в обычном кэшируемом .bss/стеке, а EHCI/OHCI DMA
// пишет в физическую DRAM без ведома D-cache.
//
// Интерфейс ищется по классу (08/06/50), EP — только внутри интерфейса.
// SET_CONFIGURATION и GET_MAX_LUN делает вызывающий (usb_hc.c / main).
#include <stdint.h>
#include "usb_msd.h"
#include "h3.h"
#include "uart.h"

extern int printf(const char* fmt, ...);

// Диагностика CSW/ошибок — всегда (это редкие сбои).
#ifndef MSD_VERBOSE_LOG
#define MSD_VERBOSE_LOG 1
#endif

// CBW/CSW буферы и bounce-сектор — uncached для DMA.
static struct usb_msd_cbw g_cbw __attribute__((section(".coherent"), aligned(64)));
static uint8_t g_csw_buf[13] __attribute__((section(".coherent"), aligned(64)));
static uint8_t g_bounce[4096] __attribute__((section(".coherent"), aligned(64)));

static struct usb_msd_dev g_dev_s;
static struct usb_msd_dev* g_dev = &g_dev_s;
static int g_dev_valid = 0;

// r788: доступ к EP активного накопителя (для BOT-recovery из usb_hc).
uint8_t usb_msd_get_eps(uint8_t* in, uint8_t* out) {
    if (!g_dev_valid) return 1;
    if (in)  *in  = g_dev_s.ep_in;
    if (out) *out = g_dev_s.ep_out;
    return 0;
}

// Общие bulk-примитивы поставляет usb_hc (EHCI) / наш usb_ohci (OHCI).
// Выбираются глобально: usb_hc ставит флаг, какой путь активен.
extern int usb_bulk_out(uint8_t ep, uint16_t mps, const uint8_t* data, uint32_t len);
extern int usb_bulk_in(uint8_t ep, uint16_t mps, uint8_t* data, uint32_t len);

static int usb_msd_bulk_out(const uint8_t* data, uint32_t len) {
    if (!g_dev_valid) return -1;
    return usb_bulk_out(g_dev->ep_out, g_dev->ep_out_mps, data, len);
}
static int usb_msd_bulk_in(uint8_t* data, uint32_t len) {
    if (!g_dev_valid) return -1;
    return usb_bulk_in(g_dev->ep_in, g_dev->ep_in_mps, data, len);
}

// Найти MSD-интерфейс в config-дескрипторе и заполнить dev.
int usb_msd_init(struct usb_msd_dev* dev, const uint8_t* cfg, uint32_t cfg_len) {
    if (!dev || !cfg) return -1;
    dev->ep_in = dev->ep_out = 0;
    dev->ep_in_mps = dev->ep_out_mps = 0;
    dev->max_lun = 0;
    dev->block_count = 0;
    dev->block_size = 512;

    uint32_t pos = 0;
    int in_msd = 0;
    while (pos + 2 <= cfg_len) {
        uint8_t bLen = cfg[pos];
        uint8_t bType = cfg[pos + 1];
        if (bLen == 0) break;
        if (pos + bLen > cfg_len) break;
        if (bType == 0x04) {   /* interface */
            uint8_t iclass = cfg[pos + 5];
            uint8_t isub   = cfg[pos + 6];
            uint8_t iprot  = cfg[pos + 7];
            in_msd = (iclass == 0x08 && isub == 0x06 && iprot == 0x50);
        } else if (bType == 0x05 && in_msd) {   /* endpoint внутри MSD */
            uint8_t addr = cfg[pos + 2];
            uint8_t attr = cfg[pos + 3];
            uint16_t mps = (uint16_t)(cfg[pos + 4] | (cfg[pos + 5] << 8));
            mps &= 0x07FFu;
            if (attr == 0x02) {   /* bulk */
                if (addr & 0x80) { dev->ep_in  = addr & 0x0F; dev->ep_in_mps  = mps; }
                else             { dev->ep_out = addr & 0x0F; dev->ep_out_mps = mps; }
            }
        }
        pos += bLen;
    }
    if (!dev->ep_in || !dev->ep_out) return -2;
    return 0;
}

// Выполнить CBW и прочитать CSW. data==0/data_len==0 — без данных.
static int usb_msd_cmd_once(struct usb_msd_dev* dev, const uint8_t* cdb, uint8_t cb_len,
                            uint32_t dir_in, uint32_t xlen,
                            uint8_t* data, uint32_t data_len) {
    if (!dev) return -1;
    g_cbw.signature         = USB_MSD_CBW_SIG;
    g_cbw.tag               = 0x11223344u + (uint32_t)dev->ep_in;
    g_cbw.data_transfer_len = xlen;
    g_cbw.flags             = dir_in ? USB_MSD_CBW_DIR_IN : USB_MSD_CBW_DIR_OUT;
    g_cbw.lun               = 0;
    g_cbw.cb_len            = cb_len;
    for (int i = 0; i < 16; i++) g_cbw.cb[i] = (i < cb_len) ? cdb[i] : 0;
    __asm volatile("dmb sy" ::: "memory");

    if (usb_msd_bulk_out((const uint8_t*)&g_cbw, 31) != 0) { printf("msd: op=0x%02X CBW fail\n", cdb[0]); return -2; }
    if (xlen > 0) {
        if (dir_in) {
            if (usb_msd_bulk_in(data, data_len) != 0) { printf("msd: op=0x%02X DATA-IN fail len=%u\n", cdb[0], (unsigned)data_len); return -3; }
        } else {
            if (usb_msd_bulk_out(data, data_len) != 0) { printf("msd: op=0x%02X DATA-OUT fail len=%u\n", cdb[0], (unsigned)data_len); return -3; }
        }
    }
    if (usb_msd_bulk_in(g_csw_buf, 13) != 0) { printf("msd: op=0x%02X CSW fail\n", cdb[0]); return -4; }
    struct usb_msd_csw* csw = (struct usb_msd_csw*)g_csw_buf;
    if (csw->signature != USB_MSD_CSW_SIG || csw->status != 0) {
        if (MSD_VERBOSE_LOG || cdb[0] != 0x00) {
            printf("msd: csw op=0x%02X sig=0x%08X tag=0x%08X st=%d res=%u\n",
                   cdb[0], csw->signature, csw->tag, csw->status, (unsigned)csw->residue);
        }
        return -5;
    }
    return 0;
}

// Обёртка: при обрыве фазы один раз восстанавливаем BOT-канал и повторяем.
static int usb_msd_cmd(struct usb_msd_dev* dev, const uint8_t* cdb, uint8_t cb_len,
                       uint32_t dir_in, uint32_t xlen,
                       uint8_t* data, uint32_t data_len) {
    int rc = usb_msd_cmd_once(dev, cdb, cb_len, dir_in, xlen, data, data_len);
    if (rc == -3 || rc == -4) {
        usb_msd_bot_recover();
        rc = usb_msd_cmd_once(dev, cdb, cb_len, dir_in, xlen, data, data_len);
    }
    return rc;
}

int usb_msd_read10(struct usb_msd_dev* dev, uint32_t lba, uint32_t count, uint8_t* data) {
    if (!dev || !data || count == 0) return -1;
    uint32_t bs = dev->block_size ? dev->block_size : 512;
    uint8_t cdb[10] = {
        0x28, 0,
        (uint8_t)(lba >> 24), (uint8_t)(lba >> 16),
        (uint8_t)(lba >> 8),  (uint8_t)(lba),
        0,
        (uint8_t)(count >> 8), (uint8_t)(count), 0
    };
    return usb_msd_cmd(dev, cdb, 10, 1, count * bs, data, count * bs);
}

int usb_msd_write10(struct usb_msd_dev* dev, uint32_t lba, uint32_t count, const uint8_t* data) {
    if (!dev || !data || count == 0) return -1;
    uint32_t bs = dev->block_size ? dev->block_size : 512;
    uint8_t cdb[10] = {
        0x2A, 0,
        (uint8_t)(lba >> 24), (uint8_t)(lba >> 16),
        (uint8_t)(lba >> 8),  (uint8_t)(lba),
        0,
        (uint8_t)(count >> 8), (uint8_t)(count), 0
    };
    return usb_msd_cmd(dev, cdb, 10, 0, count * bs, (uint8_t*)data, count * bs);
}

int usb_msd_inquiry(struct usb_msd_dev* dev, uint8_t* buf) {
    if (!dev || !buf) return -1;
    uint8_t cdb[6] = { 0x12, 0, 0, 0, 36, 0 };
    return usb_msd_cmd(dev, cdb, 6, 1, 36, buf, 36);
}

int usb_msd_test_unit_ready(struct usb_msd_dev* dev) {
    uint8_t cdb[6] = { 0x00, 0, 0, 0, 0, 0 };
    return usb_msd_cmd(dev, cdb, 6, 0, 0, 0, 0);
}

int usb_msd_wait_ready(struct usb_msd_dev* dev, int tries) {
    if (!dev) return -1;
    if (tries < 1) tries = 1;
    for (int i = 0; i < tries; i++) {
        if (usb_msd_test_unit_ready(dev) == 0) return 0;
        udelay(100000);
    }
    return -1;
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int usb_msd_read_capacity(struct usb_msd_dev* dev, uint32_t* last_lba, uint32_t* block_len) {
    if (!dev) return -1;
    uint8_t cdb[10] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int rc = usb_msd_cmd(dev, cdb, 10, 1, 8, g_bounce, 8);
    if (rc != 0) return rc;
    uint32_t last = be32(g_bounce);
    uint32_t blk  = be32(g_bounce + 4);
    if (last_lba) *last_lba = last;
    if (block_len) *block_len = blk;
    if (blk >= 512 && blk <= sizeof(g_bounce)) dev->block_size = blk;
    dev->block_count = last + 1;
    return 0;
}

int usb_msd_activate(struct usb_msd_dev* dev) {
    if (!dev || !dev->ep_in || !dev->ep_out) { g_dev_valid = 0; return -1; }
    g_dev_s = *dev;
    g_dev = &g_dev_s;
    g_dev_valid = 1;
    return 0;
}

int usb_msd_active(void) {
    return g_dev_valid;
}

// ---- Блочные обёртки для FAT (fat_set_block_source) ----
// Накопитель может иметь блок 4096 (4Kn): читаем блок устройства и вырезаем
// нужные 512 байт; для записи — read-modify-write.
int usb_msd_block_read(uint32_t lba, void* buf) {
    if (!g_dev_valid || !buf) return -1;
    uint32_t bs = g_dev_s.block_size ? g_dev_s.block_size : 512;
    uint32_t ratio = bs / 512;
    if (ratio == 0) ratio = 1;
    uint32_t dlba = lba / ratio;
    uint32_t off  = (lba % ratio) * 512;
    for (int attempt = 0; attempt < 3; attempt++) {
        int rc = usb_msd_read10(&g_dev_s, dlba, 1, g_bounce);
        if (rc == 0) {
            uint8_t* d = (uint8_t*)buf;
            for (uint32_t i = 0; i < 512; i++) d[i] = g_bounce[off + i];
            return 0;
        }
        usb_msd_bot_recover();
        udelay(50000);
    }
    return -1;
}

int usb_msd_block_write(uint32_t lba, const void* buf) {
    if (!g_dev_valid || !buf) return -1;
    uint32_t bs = g_dev_s.block_size ? g_dev_s.block_size : 512;
    uint32_t ratio = bs / 512;
    if (ratio == 0) ratio = 1;
    uint32_t dlba = lba / ratio;
    uint32_t off  = (lba % ratio) * 512;
    if (ratio > 1) {
        if (usb_msd_read10(&g_dev_s, dlba, 1, g_bounce) != 0) return -1;
    }
    const uint8_t* s = (const uint8_t*)buf;
    for (uint32_t i = 0; i < 512; i++) g_bounce[off + i] = s[i];
    return usb_msd_write10(&g_dev_s, dlba, 1, g_bounce);
}

// BOT Reset Recovery: Bulk-Only Reset + CLEAR_FEATURE(HALT) на оба bulk-EP.
// Реализация аппаратно-зависимая — в usb_hc.c (EHCI/OHCI).
void usb_msd_bot_recover(void) {
    if (!g_dev_valid) return;
    extern void usb_hc_bot_recover(void);
    usb_hc_bot_recover();
}