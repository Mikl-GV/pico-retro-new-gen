/* cw_sound.h — YM2149 (AY8910) для ядра Castaway (r1566)
 *
 * AY8910.c взят из EMULib (Marat Fayzullin) и оставлен как есть, кроме
 * подмены Sound.h: ему нужен только Sound() — установка параметров канала.
 * Генерацию сэмплов и вывод в I2S делает этот модуль.
 */
#ifndef CW_SOUND_H
#define CW_SOUND_H

#include <stdint.h>

/* Установка параметров канала (EMULib API, зовёт AY8910.c). */
void Sound(int n, int freq, int vol);

/* AY8910.c ждёт MSX-интерфейс микшера: тип канала/шум на ST не нужны. */
#define SND_MELODIC 0
#define SND_NOISE   1
static inline void SetSound(int n, int type) { (void)n; (void)type; }
static inline void SetNoise(unsigned int freq, unsigned int lfsr, unsigned int period)
{
    (void)freq; (void)lfsr; (void)period;
}

/* Порты PSG ST: 0xFF8800 (выбор регистра) и 0xFF8802 (данные). */
void cw_psg_ctrl(uint8_t v);
void cw_psg_data(uint8_t v);

/* Сброс и генерация одного кадра (50 Гц) прямо в I2S-кольцо. */
void cw_sound_reset(void);
void cw_sound_render(void);

#endif /* CW_SOUND_H */
