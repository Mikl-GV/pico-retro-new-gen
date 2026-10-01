// i2s.h — аудио-выход I2S (MAX98357A), 48000 Гц 16-bit стерео.
#ifndef I2S_H
#define I2S_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация I2S0 (H3) + MAX98357A (SD=PA10 HIGH). Вызывается в main().
int  i2s_init(void);

// Готов ли I2S (init прошёл) — эмуляторы проверяют перед доливом.
int  i2s_ready(void);

// Полный сброс звукового кольца (между разными системами).
void i2s_ring_reset(void);

// r557: срезать накопленную историю кольца — оставить keep_pairs самых
// свежих пар (дроп самых старых). Убирает «застрявшую» задержку при
// переполнении (когда темп железа чуть ниже производства).
void i2s_ring_trim(uint32_t keep_pairs);

// Громкость 0..100 (%). Старт 20%.
void i2s_volume(int percent);
int  i2s_volume_pct(void);

// Мьют (0/1). При 1 — SD=0 (полная тишина без шума).
void i2s_mute(int mute);

// Запись одного стерео-сэмпла в кольцевой буфер (неблокирующая).
void i2s_push_sample(int16_t left, int16_t right);

// Вытолкнуть накопленные сэмплы из кольцевого буфера в I2S FIFO.
// Вызывать раз в кадр (из emu_throttle или после run_frame).
void i2s_flush(void);

// Вытолкнуть НЕ БОЛЕЕ max_pairs пар (лимит за один вызов — не блокирует
// эмуляцию). Используется в emu_throttle для долива звука.
void i2s_flush_max(int max_pairs);

// Тест звука: синусоида на частоте freq (440/1000/2000 Гц),
// длительность msec (мс), стерео, с текущей громкостью.
void i2s_test_tone(int freq, int msec);

// Короткий тихий «щелчок» при навигации по меню (5 мс, 1.5 кГц).
void i2s_click(void);

#ifdef __cplusplus
}
#endif

#endif