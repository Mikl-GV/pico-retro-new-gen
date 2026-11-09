#ifndef RK86_VIDEO_H
#define RK86_VIDEO_H

#include <stdint.h>

// r1185: отрисовка экрана Радио-86РК (ВГ75: 78x30 знакомест 6x8 + 2 строки
// межстрочного интервала) напрямую в HDMI-фреймбуфер 1024x600 XRGB8888.
// Кадр 468x300 растягивается ровно вдвое: 936x600, поля по 44 пикселя.
void rk86_render(uint32_t* fb, int blink_phase);

#endif
