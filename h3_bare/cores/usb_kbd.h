#ifndef USB_KBD_H
#define USB_KBD_H

#include <stdint.h>

int  usb_kbd_init(void);
int  usb_kbd_poll(void);       // клавиатура: scancode или 0
int  usb_kbd_get_raw(uint8_t* buf, int max_buf);
// модификаторы из первого байта boot-отчёта: LCtrl=1 LShift=2 LAlt=4 LGui=8 RCtrl=0x10 RShift=0x20 RAlt=0x40 RGui=0x80
uint8_t usb_kbd_get_mods(void);

// Тач GT911 (generic HID)
int  usb_touch_poll(int* x, int* y, int* pressed);
// Объединённый ввод для меню: клавиатура или тач(Enter)
int  usb_input_poll(void);
// Тач как джойстик для эмулятора
void usb_touch_joy(uint8_t* dir, uint8_t* fire);

#endif