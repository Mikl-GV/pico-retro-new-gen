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

// r727: i2s_ring_trim УДАЛЁН (продюсер больше не пишет g_ring_rd — data race
// с ring_rd_advance() CPU2 рвал долив). Переполнение обрабатывает сам
// продюсер: i2s_push_sample дропает новые пары при полном буфере.
// i2s_ring_level удалён вместе с ним (см. ring_count в i2s.c).

// Громкость 0..100 (%). Старт 20%.
void i2s_volume(int percent);
int  i2s_volume_pct(void);

// Мьют (0/1). При 1 — SD=0 (полная тишина без шума).
void i2s_mute(int mute);

// Запись одного стерео-сэмпла в кольцевой буфер (неблокирующая).
void i2s_push_sample(int16_t left, int16_t right);

// Вытолкнуть накопленные сэмплы из кольцевого буфера через CPU2 в DMA-буфер
// (фактически — вызов i2s_flush_max(24)).
// Вызывается раз в кадр (из emu_throttle или после run_frame).
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

// ---- Аудио-ядро CPU2 (Ф1, r585; r740 — вывод через DMA) ----
// Долив кольца I2S обслуживает CPU2 (audio_core.c). core0 синтезирует и
// кладёт в кольцо (i2s_push_sample); CPU2 переносит кольцо в DMA-буфер
// (i2s_flush_max), а в I2S TX FIFO пишет ЖЕЛЕЗНЫЙ DMA (h3_dma.c).
// Почта core0↔CPU2 — в .coherent.
int      i2s_audio_core_active(void);  // 1 = CPU2 в цикле
uint32_t i2s_audio_beat(void);         // heartbeat CPU2 (инкремент; liveness)
void     i2s_audio_cmd(uint32_t cmd);  // послать команду CPU2 (0=нет)
// CPU2 (audio_core.c): обработать одну команду из почты (вызывается в цикле).
void i2s_audio_poll_cmd(void);
// Сеттеры — вызывает ТОЛЬКО CPU2 (audio_core.c): активность и heartbeat.
// Поля static в i2s.c (секция .coherent).
void i2s_audio_set_state(int on);
void i2s_audio_set_beat(uint32_t b);

// Команды аудио-ядра (шлёт core0 через i2s_audio_cmd).
enum {
    AUDIO_CMD_NONE = 0,
    AUDIO_CMD_PAUSE = 1,        // CPU2 замирает (не трогает DMA-буфер) — для тона/клика
    AUDIO_CMD_RESUME = 2,       // CPU2 снова доливает
};

// r739: i2s_write_pair_direct УДАЛЁН — прямого вывода в TX FIFO мимо кольца
// больше нет. После r740 в TX FIFO пишет ТОЛЬКО железный DMA; CPU2 льёт
// кольцо → DMA-буфер. core0 — только i2s_push_sample. Клик/тон меню тоже
// идут через кольцо.

// Пауза CPU2 активна? (геттер для core0, чтобы знать, встал ли CPU2)
int i2s_audio_paused(void);

// r734: DC-блокер УДАЛЁН (r642). API сохранён как no-op — хосты
// (GBA/Lynx/NGP/GPGX/GB/MSX/...) продолжают его звать. Параметр игнорируется.
void i2s_dc_shift_set(int shift);

// r735: счётчик дропнутых пар кольца (диагностика слоя; продюсер дропает
// новые пары, когда кольцо полное — CPU2 не успевает доливать).
void     i2s_drop_cnt_reset(void);
uint32_t i2s_drop_cnt(void);

// ---- Диагностика (r754: оживлена после r740/DMA) ----
// r735-ДИАГ: счётчики CPU2 (пишет flush_max на CPU2, читает core0 из SLT):
//   flush    — число входов в i2s_flush_max (сколько раз CPU2 промотал кольцо→DMA);
//   written  — пар реально записано в DMA-буфер;
//   skipfull — выходов из flush_max по полному DMA-буферу (CPU2 обогнал DMA).
void i2s_cpu2_diag(uint32_t* flush, uint32_t* written, uint32_t* skipfull, uint32_t* dummy);
// r735-ДИАГ: сколько пар CPU2 реально записал в DMA-буфер.
void i2s_cpu2_pairs_written_get(uint32_t* v);
// r737-ДИАГ: стадия цикла CPU2 (0=idle, 1=poll, 2=внутри flush_max) +
// счётчики входов/выходов flush_max. Пишет flush_max на CPU2.
void i2s_cpu2_stage_get(uint32_t* st, uint32_t* en, uint32_t* ex);
// r738-ДИАГ: снимок индексов кольца (wr — продюсер core0, rd — потребитель CPU2).
// Если wr растёт, а rd стоит — CPU2 не потребляет кольцо (не видит его / завис).
void i2s_ring_wr_rd_get(uint32_t* wr, uint32_t* rd);
// r738-ДИАГ: диагностика flush_max (nempty = всего входов;
// nempty_full = выходов по полному DMA-буферу). Растёт nempty_full при
// живом CPU2 → DMA не освобождает буфер (не играет 48к).
void i2s_flush_diag_get(uint32_t* nempty, uint32_t* nempty_full);

// r740: диагностика DMA-звука — позиция DMA (пар сыграно) и свободно пар
// в DMA-буфере (CPU2 не должен обгонять DMA). Читает core0 из SLT-теста.
void i2s_dma_diag_get(uint32_t* played, uint32_t* free_pairs);

#ifdef __cplusplus
}
#endif

#endif