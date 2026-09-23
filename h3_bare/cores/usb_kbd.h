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

// ---- Тачпад (boot mouse, напр. I8 Pro/Novatek) ----
void usb_pad_poll(void);   // накопление курсора из dx/dy
int  usb_pad_get_pos(int* x, int* y);     // позиция курсора, 0=нет тачпада

// ---- Sega-геймпад: помощь для подменю ----
// Фронт нажатия (биты, нажатые только что), без автоповтора
uint16_t usb_pad_just_pressed(void);
// Ждать полного отпускания геймпада (чтобы зажатая кнопка не «доехала» в подменю)
void usb_pad_wait_release(void);
// Ждать отпускания клавиатуры (чтобы зажатый Enter не «доехал» в подменю)
void usb_kbd_wait_release(void);

// ---- Sega-геймпад: СВОЙ слой ввода (не через клавиатурные скан-коды) ----
// Один аппаратный скан (sega_pad_scan) на вызов, с антидребезгом. Вызывается
// один раз за кадр любым потребителем — повторные вызовы в том же миллисекунде
// возвращают закэшированное состояние (не дёргают чип пачками импульсов).
void usb_pad_update(void);
// Текущее стабильное состояние (биты как в sega_pad.h: UP=0x01...MODE=0x800)
uint16_t usb_pad_get(void);
// Биты, нажатые ТОЛЬКО что (фронт 0→1 после стабилизации)
uint16_t usb_pad_edge(void);

// ---- Sega-геймпад: константы автоповтора ----
#define PAD_REPEAT_DELAY_US 400000   // 0,4 с до первого повтора
#define PAD_REPEAT_RATE_US  200000   // 5 шагов/с при удержании

#endif