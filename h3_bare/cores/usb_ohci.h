#ifndef USB_OHCI_H
#define USB_OHCI_H

#include <stdint.h>

#define OHCI1_BASE 0x01C1B400
#define OHCI2_BASE 0x01C1C400

int  usb_ohci_init(uint32_t base);
int  usb_ohci_ctrl_transfer(uint32_t base, uint8_t addr, uint8_t ep_in,
                            const uint8_t* setup, uint8_t setup_len,
                            uint8_t* data, uint32_t data_len, int dir_in,
                            uint32_t timeout_ms);
void usb_ohci_set_mps(uint16_t mps);
int  usb_ohci_root_port_connected(uint32_t base, int port);
int  usb_ohci_port_reset(uint32_t base, int port);
int  usb_ohci_port_low_speed(uint32_t base, int port);
uint32_t usb_ohci_port_status(uint32_t base, int port);

#endif