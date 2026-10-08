// i2s.h — ЗВУКОВОЙ СЛОЙ УДАЛЁН (r777). only-no-op API для host-слоёв.
#ifndef I2S_H
#define I2S_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- no-op стабы (host-слои эмуляторов и меню продолжают их звать) ----
int  i2s_init(void);                    // -1: звук не инициализируется
int  i2s_ready(void);                   // 0: всегда «не готов»
void i2s_ring_reset(void);
void i2s_push_sample(int16_t left, int16_t right);
void i2s_dc_shift_set(int shift);
void i2s_volume(int percent);
int  i2s_volume_pct(void);
void i2s_mute(int mute);
void i2s_click(void);

// ---- аудио-ядро CPU2 — упразднено ----
int      i2s_audio_core_active(void);
uint32_t i2s_audio_beat(void);
void     i2s_audio_cmd(uint32_t cmd);
void     i2s_audio_poll_cmd(void);
void     i2s_audio_set_state(int on);
void     i2s_audio_set_beat(uint32_t b);
int      i2s_audio_paused(void);
int      i2s_poll_fill(void);

enum {
    AUDIO_CMD_NONE = 0,
    AUDIO_CMD_PAUSE = 1,
    AUDIO_CMD_RESUME = 2,
};

// ---- диагностика (SLT удалён, стабы для совместимости) ----
void     i2s_cpu2_diag(uint32_t* flush, uint32_t* written, uint32_t* skipfull, uint32_t* dummy);
void     i2s_cpu2_pairs_written_get(uint32_t* v);
void     i2s_cpu2_stage_get(uint32_t* st, uint32_t* en, uint32_t* ex);
void     i2s_ring_wr_rd_get(uint32_t* wr, uint32_t* rd);
void     i2s_flush_diag_get(uint32_t* nempty, uint32_t* nempty_full);
void     i2s_drop_cnt_reset(void);
uint32_t i2s_drop_cnt(void);

#ifdef __cplusplus
}
#endif

#endif /* I2S_H */