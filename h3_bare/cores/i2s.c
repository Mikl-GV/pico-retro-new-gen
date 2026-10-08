// i2s.c — ЗВУКОВОЙ СЛОЙ ВЫВОДА УДАЛЁН (r777).
//
// Решение владельца: звуковой вывод (I2S0/MAX98357A) отключён полностью.
// Полинг TX FIFO на CPU2 (r773..r776) — мёртвый путь, пройден на стенде:
// CPU2 зависал на записи в TX FIFO, контроллер уходил в плохое состояние
// после i2s_ring_reset, SLT-тесты и повторные входы в игры давали тишину
// и свист до снятия питания. DMA (r740..r771) и GIC (r772) тоже не заработали.
//
// В ЭМУЛЯТОРАХ логика синтеза звука СОХРАНЕНА (правило владельца) — host-слои
// продолжают вызывать i2s_* API. Здесь остаются ТОЛЬКО no-op стабы, чтобы
// host-слои и меню линковались без изменений. Аппаратный I2S не инициализируется,
// в .coherent ничего не кладётся, выходов на PA10/MAX98357A нет.
//
// CPU2 по-прежнему стартует (не мешает), но его точка входа cpu2_entry() — no-op
// вечный цикл (wfi). Никакого «аудио-ядра» нет.
#include <stdint.h>
#include "i2s.h"

// ---- no-op стабы API (вызываются host-слоями эмуляторов и меню) ----

int i2s_init(void) { return -1; }                    // звук не инициализируется
int i2s_ready(void) { return 0; }                    // всегда «не готов»
void i2s_ring_reset(void) {}                         // сброс — no-op
void i2s_push_sample(int16_t left, int16_t right) { (void)left; (void)right; }
void i2s_dc_shift_set(int shift) { (void)shift; }

void i2s_volume(int p) { (void)p; }
int  i2s_volume_pct(void) { return 0; }
void i2s_mute(int mute) { (void)mute; }

void i2s_click(void) {}

// ---- аудио-ядро CPU2 — упразднено (r777): только no-op ----
int  i2s_audio_core_active(void) { return 0; }
uint32_t i2s_audio_beat(void) { return 0; }
void i2s_audio_set_state(int on) { (void)on; }
void i2s_audio_set_beat(uint32_t b) { (void)b; }
void i2s_audio_cmd(uint32_t cmd) { (void)cmd; }
void i2s_audio_poll_cmd(void) {}
int  i2s_audio_paused(void) { return 0; }
int  i2s_poll_fill(void) { return 0; }

// ---- диагностика — no-op ----
void i2s_cpu2_diag(uint32_t* flush, uint32_t* written, uint32_t* skipfull, uint32_t* dummy) {
    (void)flush; (void)written; (void)skipfull; (void)dummy;
}
void i2s_cpu2_pairs_written_get(uint32_t* v) { if (v) *v = 0; }
void i2s_cpu2_stage_get(uint32_t* st, uint32_t* en, uint32_t* ex) {
    if (st) *st = 0;
    if (en) *en = 0;
    if (ex) *ex = 0;
}
void i2s_ring_wr_rd_get(uint32_t* w, uint32_t* r) {
    if (w) *w = 0;
    if (r) *r = 0;
}
void i2s_flush_diag_get(uint32_t* nempty, uint32_t* nempty_full) {
    if (nempty) *nempty = 0;
    if (nempty_full) *nempty_full = 0;
}

void i2s_drop_cnt_reset(void) {}
uint32_t i2s_drop_cnt(void) { return 0; }

// ---- CPU2 entry — no-op вечный цикл (проц стартует, ничего не делает) ----
// Имя cpu2_audio_entry — как зашито в startup.S (`.global cpu2_entry` —
// ассемблерный пролог, затем `bx cpu2_audio_entry`). Не переименовывать.
void cpu2_audio_entry(void) {
    for (;;) __asm volatile("wfi");
}