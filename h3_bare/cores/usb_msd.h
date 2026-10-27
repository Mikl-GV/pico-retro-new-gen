#ifndef USB_MSD_H3_H
#define USB_MSD_H3_H

#include <stdint.h>

// r788: USB Mass Storage (BOT — Bulk-Only Transport) для H3.
// Работает поверх EHCI (HS-флешка) или OHCI-bulk (FS/LS-флешка).
// Взято из эталона Bare_linux (src/usb_msd.c), адаптировано под проект:
//  - данные ходят через bounce-буфер .coherent (EHCI-OHCI DMA пишет в DRAM
//    напрямую, без ведома D-cache);
//  - после полного enum вызывается usb_msd_activate() — с этого момента
//    активен блочный источник usb_msd_block_read/write;
//  - fat_set_block_source(usb_msd_block_read, usb_msd_block_write) подключает
//    флешку к FAT.

#define USB_MSD_CBW_SIG   0x43425355u   /* 'USBC' */
#define USB_MSD_CSW_SIG   0x53425355u   /* 'USBS' */
#define USB_MSD_CBW_DIR_IN  0x80u
#define USB_MSD_CBW_DIR_OUT 0x00u

#pragma pack(push, 1)
struct usb_msd_cbw {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_transfer_len;
    uint8_t  flags;
    uint8_t  lun;
    uint8_t  cb_len;
    uint8_t  cb[16];
};

struct usb_msd_csw {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t  status;   /* 0 = success */
};
#pragma pack(pop)

struct usb_msd_dev {
    uint8_t  ep_in;
    uint8_t  ep_out;
    uint16_t ep_in_mps;
    uint16_t ep_out_mps;
    uint8_t  max_lun;
    uint32_t block_count;
    uint32_t block_size;   /* 512 для FAT */
};

// Найти первый MSD-интерфейс (класс 08/06/50) в config-дескрипторе,
// заполнить dev (bulk-EP только внутри интерфейса). 0 = успех.
int  usb_msd_init(struct usb_msd_dev* dev, const uint8_t* cfg, uint32_t cfg_len);

// READ/WRITE(10): count секторов (512) с lba. Возвращает 0 при успехе.
int  usb_msd_read10(struct usb_msd_dev* dev, uint32_t lba, uint32_t count, uint8_t* data);
int  usb_msd_write10(struct usb_msd_dev* dev, uint32_t lba, uint32_t count, const uint8_t* data);

int  usb_msd_inquiry(struct usb_msd_dev* dev, uint8_t* buf);
int  usb_msd_test_unit_ready(struct usb_msd_dev* dev);
int  usb_msd_wait_ready(struct usb_msd_dev* dev, int tries);
int  usb_msd_read_capacity(struct usb_msd_dev* dev, uint32_t* last_lba, uint32_t* block_len);

// Зафиксировать активный накопитель (после enum+SET_CONFIGURATION).
int  usb_msd_activate(struct usb_msd_dev* dev);
int  usb_msd_active(void);

// BOT Reset Recovery (Stall/десинк после короткого приёма блока).
void usb_msd_bot_recover(void);

// Блочные обёртки для fat_set_block_source. 0 = успех.
int  usb_msd_block_read(uint32_t lba, void* buf);
int  usb_msd_block_write(uint32_t lba, const void* buf);

#endif /* USB_MSD_H3_H */