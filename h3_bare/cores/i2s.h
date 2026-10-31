// i2s.h — звуковой слой I2S0 (H3, PCM0) для эмуляторов.
//
// АРХИТЕКТУРА (этап 1, спроектировано по железу H3 — даташит + sun4i-i2s.c):
//   core0 (эмулятор) --i2s_push_sample--> ring (.coherent, uncached)
//   CPU2 (аудио-ядро) --долив-->          I2S0 TX FIFO (guard TXE_CNT)
//
// Темп задаёт АППАРАТНЫЙ TX FIFO (ровно 48 кГц из делителей MCLK/BCLK),
// CPU2 лишь переносит пары ring→FIFO, пока FIFO не полон. Запись в полный
// FIFO исключена guard'ом TXE_CNT (корень старого AHB-зависания).
//
// Без DMA и без GIC (оба в истории не работали). Ring и почта — в
// .coherent (uncached через mmu_mark_uncached) — видны и core0, и CPU2.
#ifndef I2S_H
#define I2S_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Хост-API (эмуляторы и меню) ----
int  i2s_init(void);                    // 0 = готово, -1 = ошибка init
int  i2s_ready(void);                   // 1 = I2S работает, тык пары принимает
void i2s_ring_reset(void);              // сброс ring+FIFO (смена игры/старт)
void i2s_push_sample(int16_t left, int16_t right); // продюсер: пара в ring
uint32_t i2s_push_block(const int16_t* pairs, uint32_t n); // r919: блок пар, 1 dmb на пакет
void i2s_test_tone(void);               // тест-тон 1 кГц на старте (этап 2)
void i2s_test_tone_freq(int hz, int ms); // тон по команде (меню настроек)
void i2s_dc_shift_set(int shift);       // no-op (DC-блокер не нужен, для совместимости)
void i2s_volume(int percent);           // громкость 0..100; 0% = PA10(SD)=0 (мьют усилителя)
int  i2s_volume_pct(void);
void i2s_mute(int mute);                // 1 = усилитель выключен (PA10=0)
void i2s_click(void);                   // короткий клик меню (через ring)

// ---- Аудио-ядро CPU2 (вызываются из cpu2_entry) ----
int      i2s_audio_core_active(void);
uint32_t i2s_audio_beat(void);
void     i2s_audio_cmd(uint32_t cmd);   // core0 → CPU2 (пауза/резюм)
void     i2s_audio_poll_cmd(void);      // CPU2: принять команды core0
void     i2s_audio_set_state(int on);
void     i2s_audio_set_active(int on); // r916: 1 = эмуляция идёт (CPU2 доливает тишину)
void     i2s_audio_set_beat(uint32_t b);
int      i2s_audio_paused(void);
int      i2s_poll_fill(void);           // CPU2: долить ring→FIFO (пачки)

enum {
    AUDIO_CMD_NONE = 0,
    AUDIO_CMD_PAUSE = 1,
    AUDIO_CMD_RESUME = 2,
};

// ---- Диагностика (SLT/стабы) ----
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