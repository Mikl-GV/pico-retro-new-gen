// audio_core.c — аудио-ядро на CPU2: долив кольца I2S в аппаратный TX FIFO.
//
// Ф1 (r585), одна задача: долив/темп звука I2S обслуживает CPU2, а не core0.
// core0 (эмулятор) по-прежнему синтезирует звук в ядрах и кладёт сэмплы в
// кольцо (i2s_push_sample). CPU2 крутит вечный цикл долива с честным темпом
// 48 кГц (порция 24 пары / 500 мкс — тот же проверенный ритм, что был в
// emu_throttle r549), независимо от загрузки core0.
//
// Синхронизация:
//   - кольцо и индексы — в .coherent (uncached, r584): CPU2 видит актуальное
//     состояние без кэш-когерентности (у CPU2 MMU выключен — прямые
//     физические адреса, как у CPU1/TFT);
//   - почта (i2s.c, .coherent): core0 пишет g_audio_cmd, CPU2 исполняет и
//     сбрасывает; i2s_audio_set_state(1) — CPU2 в цикле; set_beat — heartbeat;
//   - UART: CPU2 НЕ пишет в UART0 — диагностику числами кладёт в .coherent
//     (set_ring/set_pairs), печатает core0 (один писатель, без драк на PA4/PA5).

#include <stdint.h>
#include "h3.h"
#include "h3_hs_timer.h"
#include "i2s.h"

#define AUDIO_PACE_US 500   // 24 пары за 500 мкс = ровно 48 кГц (ритм r549)

// Точка входа CPU2 (из startup.S cpu2_entry — там включён VFP/NEON и стек).
void cpu2_audio_entry(void) {
    // Сердце: пометить себя активным, затем вечный цикл долива.
    i2s_audio_set_state(1);
    i2s_audio_set_beat(0);

    uint32_t beat = 0;
    for (;;) {
        // 1) команда от core0 (сброс кольца/DC при смене системы)
        i2s_audio_poll_cmd();

        // 2) долив: порция 24 пары (~0.5 мс звука), как emu_throttle r549.
        //    Если кольцо пусто — flush_max сам пишет тишину (FIFO не проседает).
        i2s_flush_max(24);

        // 3) темп: 24 пары за 500 мкс = ровно 48 кГц (не зависит от эмуляции).
        {
            uint32_t t0 = h3_hs_timer_lo_us();
            while (h3_hs_timer_lo_us() - t0 < AUDIO_PACE_US) {}
        }

        // 4) heartbeat + диагностика раз в ~1024 итераций (~0.5 с)
        beat++;
        if ((beat & 0x3FF) == 0) {
            i2s_audio_set_beat(beat);
            i2s_audio_set_ring((uint32_t)i2s_ring_level());
            i2s_audio_set_pairs(0);
        }
    }
}