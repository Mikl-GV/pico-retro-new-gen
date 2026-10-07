// i2s.h — аудио-выход I2S (MAX98357A), 48000 Гц 16-bit стерео.
// r773: долив поллингом на CPU2, без прерываний и DMA (см. i2s.c).
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

// r772/r773: долив делает CPU2 поллингом (i2s_poll_fill) — без прерываний
    // и без DMA. i2s_flush*/i2s_flush удалены как мёртвые (эмуляторы зовут
    // только i2s_push_sample → кольцо, CPU2 сам выпивает его в TX FIFO).

    // r773: долив кольца → I2S TX FIFO. Вызывает ТОЛЬКО CPU2 (audio_core.c)
    // в цикле: пишет пары, пока в FIFO есть место (TXE_CNT > 2). Возвращает
    // число записанных пар. Без GIC/IRQ/DMA — чистый поллинг MMIO.
    int  i2s_poll_fill(void);

// Короткий тихий «щелчок» при навигации по меню (5 мс, 1.5 кГц).
void i2s_click(void);

// ---- Аудио-ядро CPU2 (r773: долив поллингом, без ISR/DMA) ----
// CPU2 (audio_core.c) — вечный цикл: почта + i2s_poll_fill + heartbeat.
// core0 только синтезирует и кладёт в кольцо (i2s_push_sample). Почта — в
// .coherent. GIC/прерывания НЕ используются.
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
    AUDIO_CMD_PAUSE = 1,        // CPU2 отмечает паузу (почта) — для тона/клика
    AUDIO_CMD_RESUME = 2,       // CPU2 снимает паузу
};

// r739: i2s_write_pair_direct УДАЛЁН — прямого вывода в TX FIFO мимо кольца
// больше нет. В TX FIFO пишет ТОЛЬКО CPU2 (i2s_poll_fill, долив из кольца).
// core0 — только i2s_push_sample. Клик/тон меню тоже идут через кольцо.

// Пауза CPU2 активна? (геттер для core0, чтобы знать, встал ли CPU2)
int i2s_audio_paused(void);

// r734: DC-блокер УДАЛЁН (r642). API сохранён как no-op — хосты
// (GBA/Lynx/NGP/GPGX/GB/MSX/...) продолжают его звать. Параметр игнорируется.
void i2s_dc_shift_set(int shift);

// r735: счётчик дропнутых пар кольца (диагностика слоя; продюсер дропает
// новые пары, когда кольцо полное — CPU2 не успевает доливать).
void     i2s_drop_cnt_reset(void);
uint32_t i2s_drop_cnt(void);

// ---- Диагностика (r772/r773: счётчики poll-долива) ----
// i2s_cpu2_diag: flushed (входов в i2s_poll_fill), written (пар реально
// записано в TX FIFO), skipfull (выходов по полному FIFO).
void i2s_cpu2_diag(uint32_t* flush, uint32_t* written, uint32_t* skipfull, uint32_t* dummy);
void i2s_cpu2_pairs_written_get(uint32_t* v);
// стадия CPU2 (0=idle, 2=внутри poll-долива) + входов/выходов долива.
void i2s_cpu2_stage_get(uint32_t* st, uint32_t* en, uint32_t* ex);
// снимок индексов кольца (wr — продюсер core0, rd — потребитель CPU2).
void i2s_ring_wr_rd_get(uint32_t* wr, uint32_t* rd);
void i2s_flush_diag_get(uint32_t* nempty, uint32_t* nempty_full);
// r776: i2s_dma_diag_get удалён вместе с DMA-путём (r773). Свободное место в
// TX FIFO (слова) — читается напрямую из FSTA[23:16] (см. SLT).

#ifdef __cplusplus
}
#endif

#endif