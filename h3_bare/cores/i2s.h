// i2s.h — аудио-выход I2S (MAX98357A), 44100 Гц 16-bit стерео.
#ifndef I2S_H
#define I2S_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация I2S0 (H3) + MAX98357A (SD=PG7 HIGH). Вызывать в main(). 
int  i2s_init(void);

// Громкость 0..100 (%). Старт 20%.
void i2s_volume(int percent);
int  i2s_volume_pct(void);

// Мьют (0/1). При 1 — SD=0 (полный тишина без шума).
void i2s_mute(int mute);

// Запись одного стерео-сэмпла во FIFO I2S (с громкостью).
void i2s_push_sample(int16_t left, int16_t right);

#ifdef __cplusplus
}
#endif

#endif