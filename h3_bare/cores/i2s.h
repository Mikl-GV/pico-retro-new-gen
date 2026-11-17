// i2s.h — звуковой слой I2S0 (H3, PCM0) для эмуляторов.
//
// АРХИТЕКТУРА (этап 1, спроектировано по железу H3 — даташит + sun4i-i2s.c):
//   core0 (эмулятор) --i2s_push_sample--> ring (SRAM A1, r957)
//   CPU2 (аудио-ядро) --долив-->          I2S0 TX FIFO (guard TXE_CNT)
//
// Темп задаёт АППАРАТНЫЙ TX FIFO (ровно 48 кГц из делителей MCLK/BCLK),
// CPU2 лишь переносит пары ring→FIFO, пока FIFO не полон. Запись в полный
// FIFO исключена guard'ом TXE_CNT (корень старого AHB-зависания).
//
// Без DMA и без GIC (оба в истории не работали). Кольцо, его индексы, почта
// команд и флаги паузы/активности — в SRAM A1; в .coherent (DRAM write-back,
// mmu_mark_uncached с r1206 — no-op) остались только g_i2s_ready и параметры
// DC-фильтра, видимость которых держат SRAM-дубль готовности (0x94) и клампы.
#ifndef I2S_H
#define I2S_H

#include <stdint.h>

// Фактический размер кольца звука (пар) в SRAM A1 (r957). r1357: 8192 пары
// @48 кГц ≈ 170 мс (было 4096 ≈ 85 мс — мало для джиттера кадра на тяжёлых
// ядрах: короткие провалы продюсера сразу давали underrun). SRAM A1 на H3 —
// 48 КБ (0x0000..0xBFFF), кольцо занимает 0x1000..0x9000 (L 16 КБ + R 16 КБ).
// Используется в логе AUDIO-END и в i2s.c.
#define AUDIO_RING_SIZE 8192

#ifdef __cplusplus
extern "C" {
#endif

// ---- Хост-API (эмуляторы и меню) ----
int  i2s_init(void);                    // 0 = готово, -1 = ошибка init
int  i2s_ready(void);                   // 1 = I2S работает, тык пары принимает
uint32_t i2s_ring_fill(void);           // r940: текущее заполнение кольца (пар), для throttle
uint32_t i2s_ring_target(void);          // r943: текущий target кольца
void i2s_ring_observe_frame(uint32_t pairs); // r944: адаптивный target из фактического потока (PAL/NTSC сам)
void i2s_ring_reset(void);              // сброс ring+FIFO (смена игры/старт)
void i2s_push_sample(int16_t left, int16_t right); // продюсер: пара в ring
uint32_t i2s_push_block(const int16_t* pairs, uint32_t n); // r919: блок пар, 1 dmb на пакет
void i2s_test_tone(void);               // тест-тон 1 кГц на старте (этап 2)
// r948: тон по команде (меню настроек) — ПРЯМОЙ долив TX FIFO с core0,
// без кольца и без CPU2. 0 = проигран, -1 = I2S не готов, -2 = FIFO не сливается.
// Монополист FIFO на время теста — core0 (CPU2 принудительно на паузе).
int  i2s_test_tone_freq(int hz, int ms);
// r951: тот же тон, но выводит CPU2: core0 только пишет в кольцо, FIFO
// доливает аудио-ядро. Монополист FIFO — CPU2. Возврат: 0 = проигран,
// -1 = I2S не готов, -2 = кольцо/CPU2 не выгребаются (нет прогресса).
int  i2s_test_tone_cpu2(int hz, int ms);
void i2s_dc_shift_set(int shift);       // α=2^-shift для ФВЧ CPU2 (дефолт 6)
void i2s_dc_bypass(int on);             // r956: 1 = тест без DC-фильтра, 0 = штатный ФВЧ
int  i2s_dc_shift(void);                // r1410: текущий сдвиг ФВЧ (для лога железа)
int  i2s_dc_is_bypassed(void);          // r1410: текущее состояние bypass DC
void i2s_volume(int percent);           // громкость 0..100; 0% = PA10(SD)=0 (мьют усилителя)
int  i2s_volume_pct(void);
void i2s_emulators_audio(int on);       // S.0: 1 = host-слои пишут в ring (по умолч. 0)

// ---- Аудио-диагностика (выключена по умолчанию) ----
// Разовые печати AUDIO-END/NES-AUD/LYNX-AUD/TONE/TONE-CPU2/DBG-RST и GBA-строки
// в кадре идут только при g_audio_diag=1. Накопление счётчиков — всегда.
// Включается из отладочного кода/меню; в обычном образе спама нет.
int  i2s_audio_diag(void);              // текущее значение флага
void i2s_audio_diag_set(int on);        // 1 = печатать диагностику

// ---- Аудио-ядро CPU2 (вызываются из cpu2_entry) ----
int      i2s_audio_core_active(void);
uint32_t i2s_audio_beat(void);
void     i2s_audio_cmd(uint32_t cmd);   // core0 → CPU2 (пауза/резюм)
void     i2s_audio_poll_cmd(void);      // CPU2: принять команды core0
void     i2s_audio_set_state(int on);
void     i2s_audio_set_active(int on); // r916: 1 = эмуляция идёт. Флаг пишется в SRAM A1, hot-path CPU2 его не читает; при пустом кольце доливаются 4 пары нулей (r1326), underrun считается в статистику
void     i2s_audio_set_beat(uint32_t b);
int      i2s_audio_paused(void);
int      i2s_poll_fill(void);           // CPU2: долить ring→FIFO (пачки)

enum {
    AUDIO_CMD_NONE = 0,
    AUDIO_CMD_PAUSE = 1,
    AUDIO_CMD_RESUME = 2,
};

// ---- Диагностика (SLT/стабы) ----
// r948: проверка, что TX FIFO реально сливается (такт I2S0 жив, блок не заперт
// во FLUSH). 1 = такт есть, 0 = блок не тактируется. Требует ПОЛНОСТЬЮ пустой
// FIFO (TXE_CNT == глубина), поэтому вызывать только при остановленном доливе:
// CPU2 подмешивает тишину при пустом кольце (r1326) и держит FIFO заполненным
// (r1367: в тест-тоне CPU2 на время проверки ставится на паузу).
int      i2s_selftest(void);
void     i2s_cpu2_diag(uint32_t* flush, uint32_t* written, uint32_t* skipfull, uint32_t* dummy);
void     i2s_cpu2_pairs_written_get(uint32_t* v);
void     i2s_cpu2_stage_get(uint32_t* st, uint32_t* en, uint32_t* ex);
void     i2s_ring_wr_rd_get(uint32_t* wr, uint32_t* rd);
void     i2s_flush_diag_get(uint32_t* nempty, uint32_t* nempty_full);
void     i2s_drop_cnt_reset(void);
uint32_t i2s_drop_cnt(void);

// ---- Фаза 0 (r910): диагностика согласования темпа (ring fill/underrun/pairs) ----
void i2s_stats_reset(void);
void i2s_stats_active(int on);
void i2s_stats_get(uint32_t* fmax, uint32_t* fmin, uint32_t* drop,
                   uint32_t* under, uint32_t* pairs);

#ifdef __cplusplus
}
#endif

#endif /* I2S_H */