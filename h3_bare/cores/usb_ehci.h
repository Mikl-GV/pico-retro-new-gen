// usb_ehci.h — EHCI host controller для H3 (1м порт).
#ifndef USB_EHCI_H
#define USB_EHCI_H

#include <stdint.h>

#define EHCI1_BASE 0x01C1B000
#define EHCI2_BASE 0x01C1C000

int  usb_ehci_init(uint32_t base);
int  usb_ehci_ctrl(uint32_t base, uint8_t addr, uint8_t ep,
                   const uint8_t* setup, uint8_t setup_len,
                   uint8_t* data, uint32_t data_len, int dir_in,
                   uint32_t timeout_ms);
int  usb_ehci_dev_connected(uint32_t base);
void usb_ehci_intr_poll(uint32_t base, uint8_t addr, uint8_t ep_in,
                         uint8_t* buf, int len);

#endif