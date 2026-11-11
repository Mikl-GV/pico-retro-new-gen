#ifndef USB_KBD_H
#define USB_KBD_H

#include <stdint.h>

// ============================================================================
//  USB HID ввод через OHCI: единый слой СОСТОЯНИЯ клавиатуры + очередь
//  событий. Один аппаратный опрос на кадр (KBD_FRAME_US внутри usb_kbd.c),
//  все публичные функции читают состояние слоя.
// ============================================================================

// Инициализация USB (OHCI1/OHCI2), энумерация клавиатуры/тача/тачпада.
// Возвращает 0 при успехе, -1 если клавиатура не найдена.
int  usb_kbd_init(void);

// Меню: HID-сканкод нажатия (PRESS из очереди) либо автоповтор удержанной
// клавиши; 0 = ничего. Неблокирующий.
int  usb_kbd_poll(void);

// Игры: текущие УДЕРЖИВАЕМЫЕ HID-коды (0..max_buf). Возвращает количество.
int  usb_kbd_get_raw(uint8_t* buf, int max_buf);

// Состояние БЕЗ опроса железа (для emu_esc_hold — не воровать отчёт/кадр).
int  usb_kbd_get_last(uint8_t* buf, int max_buf);

// Модификаторы последнего отчёта: 0x01=LCTRL 0x02=LSHIFT 0x04=LALT 0x08=LWIN
// 0x10=RCTRL 0x20=RSHIFT 0x40=RALT 0x80=RWIN.
uint8_t usb_kbd_get_mods(void);

// Обслуживание клавиатуры ИЗ длинных ожиданий OHCI (bulk/control).
// Безопасен: работает только для стартованного interrupt-IN (low-speed),
// не входит в GET_REPORT-рекурсию. Частота — раз в кадр (общий гейт).
void usb_kbd_poll_block(void);

// Выход эмулятора для Low-Speed донгла (I8 Pro): удержание на нём ненадёжно,
// поэтому ESC-выход — 3 подряд нажатия ESC в окне ~0.7 с. Для остальных
// клавиатур вызовы возвращают 0 и ничего не сбрасывают.
int  usb_kbd_esc3_pressed(void);
void usb_kbd_esc3_reset(void);

// Перезапуск interrupt-IN цепочки (после ошибок TD / выхода из эмулятора).
void usb_kbd_restart_intr(void);

// Дождаться отпускания клавиатуры (макс. ~800 мс) и очистить состояние —
// чтобы зажатая клавиша не «доехала» в подменю/эмулятор.
void usb_kbd_wait_release(void);
// То же для клавиатуры И Sega-геймпада (выход из эмулятора).
void usb_wait_release_all(void);

// ---- Событийный слой (внутренние константы, нужны потребителям) ----
#define KBD_EV_PRESS    1
#define KBD_EV_RELEASE  2

// ---- Тач GT911 ----
int usb_touch_poll(int* x, int* y, int* pressed);
int usb_input_poll(void);   // объединённый ввод для меню (клава/геймпад/тач)

// ---- Тачпад (boot mouse, I8 Pro/Novatek) ----
void usb_pad_poll(void);
int  usb_pad_get_pos(int* x, int* y);
int  usb_pad_buttons(void);   // r1237: бит0=ЛКМ, бит1=ПКМ, бит2=СКМ

// ---- Sega-геймпад ----
void usb_pad_update(void);
uint16_t usb_pad_get(void);
uint16_t usb_pad_edge(void);
void usb_pad_wait_release(void);

#define PAD_REPEAT_DELAY_US 400000
#define PAD_REPEAT_RATE_US  200000

#endif