#ifndef H3_DE2_SCALER_H
#define H3_DE2_SCALER_H

#include <stdint.h>

// Включить режим эмулятора с аппаратным скейлингом через DE2 VI+VSU.
// src_w/src_h — натуральное разрешение эмулятора
// dst_w/dst_h — окно вывода на экране (целый множитель src)
// dst_x/dst_y — позиция окна по центру
// fb_addr — физический адрес фреймбуфера в DRAM
// format_rgb565 — 1=RGB565, 0=XRGB8888
int de2_set_emu_mode(int src_w, int src_h, int dst_w, int dst_h,
                     int dst_x, int dst_y, uint32_t fb_addr,
                     int format_rgb565);

// Вернуться в UI-режим (меню, 1024×600 XRGB8888)
void de2_set_ui_mode(uint32_t fb_addr);

// Текущий режим: 1=emu, 0=UI
int de2_is_emu_mode(void);

#endif