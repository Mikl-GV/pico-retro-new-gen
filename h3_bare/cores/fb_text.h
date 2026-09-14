// fb_text.h — растровый текст на HDMI-фреймбуфере
#ifndef FB_TEXT_H
#define FB_TEXT_H

#include <stdint.h>

void fb_putchar(int x, int y, char c, uint32_t color);
void fb_puts(int x, int y, const char* s, uint32_t color);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void fb_pixel(int x, int y, uint32_t color);
void fb_draw_stars(void);
int  fb_puts_s(int x, int y, const char* s, int scale, uint32_t color);
void fb_text_center(const char* s, int y, int scale, uint32_t color);
void fb_clear(void);
void fb_flush(void);

#endif