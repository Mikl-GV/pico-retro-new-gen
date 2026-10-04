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

// r522: текущий уровень кольца (пар в буфере) — для страховочного среза
// в эмуляторах (emu.c) и диагностики.
int  i2s_ring_level(void);

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
// D-audio: возвращает число фактически записанных пар (0/частично = FIFO или
// кольцо были заняты в этот проход).
int i2s_flush_max(int max_pairs);

// Тест звука: синусоида на частоте freq (440/1000/2000 Гц),
// длительность msec (мс), стерео, с текущей громкостью.
void i2s_test_tone(int freq, int msec);

// Короткий тихий «щелчок» при навигации по меню (5 мс, 1.5 кГц).
void i2s_click(void);

// Тестовый «эмулятор» звука (проверка слоя): тон пачками за кадр через
// обычный путь кольцо→CPU2→FIFO, как настоящий эмулятор. freq — частота Гц,
// pairs_per_frame — размер пачки (GBA ~549, Lynx ~640), frames — число кадров.
void i2s_tone_burst_test(int freq, int pairs_per_frame, int frames);

// ---- Аудио-ядро CPU2 (Ф1, r585) ----
// Долив кольца I2S обслуживает CPU2 (audio_core.c), а не core0. core0
// синтезирует звук и кладёт в кольцо; CPU2 выводит кольцо в TX FIFO с
// честным темпом 48 кГц. Почта — в .coherent.
int      i2s_audio_core_active(void);  // 1 = CPU2 в цикле
uint32_t i2s_audio_beat(void);         // heartbeat CPU2 (инкремент)
uint32_t i2s_audio_pairs(void);         // пар вывел CPU2 (диагностика)
uint32_t i2s_audio_ring(void);          // уровень кольца от CPU2 (диагностика)
// r702-DIAG: маркер местонахождения CPU2 + локальный счётчик (для отлова
// зависания аудио-ядра). CPU2 пишет trace на стадиях цикла; core0 печатает.
uint32_t i2s_audio_trace(void);
uint32_t i2s_audio_live(void);
void     i2s_audio_cmd(uint32_t cmd);  // послать команду CPU2 (0=нет)
// CPU2 (audio_core.c): обработать одну команду из почты (вызывается в цикле).
void i2s_audio_poll_cmd(void);
// Сеттеры — вызывает ТОЛЬКО CPU2 (audio_core.c): отметить активность,
// heartbeat, диагностику. Поля static в i2s.c (секция .coherent).
void i2s_audio_set_state(int on);
void i2s_audio_set_beat(uint32_t b);
void i2s_audio_set_ring(uint32_t r);
void i2s_audio_set_pairs(uint32_t p);
// r702-DIAG: см. геттеры выше.
void i2s_audio_set_trace(uint32_t t);
void i2s_audio_set_live(uint32_t l);

// Команды аудио-ядра.
enum {
    AUDIO_CMD_NONE = 0,
    AUDIO_CMD_RING_RESET = 1,   // сбросить кольцо + DC (смена системы)
    AUDIO_CMD_PAUSE = 2,        // CPU2 замирает (не трогает FIFO) — для тона/клика
    AUDIO_CMD_RESUME = 3,       // CPU2 снова доливает
};
// Пауза CPU2 активна? (геттер для core0, чтобы знать, встал ли CPU2)
int i2s_audio_paused(void);
// r588: вывод одной пары НАПРЯМУЮ в TX FIFO (мимо кольца) — для тест-тона/
// клика, когда CPU2 на паузе. Уважает место в FIFO.
void i2s_write_pair_direct(int16_t l, int16_t r);

// r590: задать срез DC-блокера по системе (5 ≈ 240 Гц, 6 ≈ 120 Гц).
// GBA — 5 (пачки, медленный блокер даёт щелчки), Lynx — 6 (непрерывный поток).
void i2s_dc_shift_set(int shift);

#ifdef __cplusplus
}
#endif

#endif