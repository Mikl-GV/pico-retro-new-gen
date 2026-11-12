// fb_text.h — растровый текст на HDMI-фреймбуфере
#ifndef FB_TEXT_H
#define FB_TEXT_H

#include <stdint.h>

// Общий 8x8 шрифт (0x20..0x7F) — доступен другим модулям
extern const uint8_t font8x8[96][8];

void fb_putchar(int x, int y, char c, uint32_t color);
void fb_puts(int x, int y, const char* s, uint32_t color);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void fb_pixel(int x, int y, uint32_t color);
void fb_draw_stars(void);
int  fb_puts_s(int x, int y, const char* s, int scale, uint32_t color);
int  fb_puts_s15(int x, int y, const char* s, uint32_t color);   // 12px (1.5x)
void fb_text_center(const char* s, int y, int scale, uint32_t color);
void fb_clear(void);
void fb_flush(void);
// r1183: двойная буферизация HDMI-вывода (опция меню).
// fb_flip_set(1) — рисование уходит в back-буфер, fb_flush в конце кадра
// переключает DE2 на готовый кадр (+1 кадр задержки, разрыва нет).
// fb_flip_set(0) — штатный режим: всё рисуется в FB_ADDR. fb_draw_addr() —
// адрес буфера, в который надо писать прямым писателям.
uint32_t fb_draw_addr(void);
void fb_flip_set(int on);
// r1277 (E-01): включён ли пинг-понг буферов (нужно писателям, которые
// оптимизируют перерисовку полей по неизменной геометрии).
int  fb_flip_active(void);
// r1159 (E14): dirty-зоны — fb_flush чистит только отмеченную область
void fb_mark_dirty(int x, int y, int w, int h);
void fb_dirty_all(void);
void fb_frame_scaled(void);

#endif