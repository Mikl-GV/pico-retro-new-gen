#ifndef TOUCH_XPT2046_H
#define TOUCH_XPT2046_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// XPT2046 на SPI0 (H3 Port C): CS=PC3, MOSI=PC0, MISO=PC1, SCLK=PC2
// CS=1 — отключено (SPI idle)
void touch_init(void);

// Чтение позиции касания (12 бит, 0..4095).
// Возвращает 1 если палец нажат, 0 если нет.
// Координаты: X = [0..4095] (ширина экрана), Y = [0..4095] (высота)
// Для 1024x600: масштабирование будет снаружи.
int touch_read(int* x, int* y);

// Калибровка (пока пустая — просто используем raw)
void touch_set_cal(int x_off, int y_off, float x_scale, float y_scale);

#ifdef __cplusplus
}
#endif

#endif