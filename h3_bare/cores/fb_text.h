// fb_text.h — растровый текст на HDMI-фреймбуфере
#ifndef FB_TEXT_H
#define FB_TEXT_H

#include <stdint.h>

void fb_putchar(int x, int y, char c, uint32_t color);
void fb_puts(int x, int y, const char* s, uint32_t color);
void fb_clear(void);
void fb_flush(void);

#endif