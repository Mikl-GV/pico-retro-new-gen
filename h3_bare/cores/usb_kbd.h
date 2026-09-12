#ifndef USB_KBD_H
#define USB_KBD_H

#include <stdint.h>

int  usb_kbd_init(void);
int  usb_kbd_poll(void);       // читает нажатые клавиши, возвращает scancode или 0

#endif