// audio_core.c — аудио-ядро на CPU2: долив кольца I2S в аппаратный TX FIFO.
//
// Ф1 (r585), одна задача: долив/темп звука I2S обслуживает CPU2, а не core0.
// core0 (эмулятор) по-прежнему синтезирует звук в ядрах и кладёт сэмплы в
// кольцо (i2s_push_sample). CPU2 крутит вечный цикл долива кольца в TX FIFO.
//
// TEMP (r588): темп задаёт АППАРАТНЫЙ TX FIFO, а не таймер. i2s_flush_max
// останавливается, когда FIFO полон (I2S_FSTA TXE_CNT < 8), и пишет снова,
// как только контроллер освободил место (он сам играет ровно 48 кГц). Так
// CPU2 не зависит от h3_hs_timer_lo_us() — на ядре с выключенным MMU таймер
// не давал корректной задержки, из-за чего CPU2 высасывал кольцо на
// максимальной скорости: «звук за 0.1 с проигрывал то, что должно звучать
// дольше» + разрывы/тишина, тест-тон становился тихим с щелчками.
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
#include "i2s.h"
#include "led.h"

// Страховка кольца (D-audio, C1/C2): ТЕМП задаёт АППАРАТНЫЙ TX FIFO (см. ниже).
// Переполнение/срез кольца делает ТОЛЬКО продюсер (core0, i2s_push_sample) —
// сброс g_ring_rd и dc конкурентным потребителю потоком был бы data-race.
// Пороги среза жили тут (AUDIO_TRIM_HI/LO) — перенесены в i2s.c как
// AUDIO_RING_TRIM_TARGET (мягкий уровень при drop-on-full).

// Точка входа CPU2 (из startup.S cpu2_entry — там включён VFP/NEON и стек).
void cpu2_audio_entry(void) {
    // Сердце: пометить себя активным, затем вечный цикл долива.
    i2s_audio_set_state(1);
    i2s_audio_set_beat(0);

    uint32_t beat = 0;
    uint32_t live = 0;
    for (;;) {
        // r702-DIAG: маркер «я на входе цикла» — если застыл здесь, CPU2 жив
        // и крутится, но не может выйти из poll_cmd/flush (см. trace ниже по
        // стадиям). trace=1 = дошли до начала итерации.
        i2s_audio_set_trace(1);

        // 1) команда от core0 (сброс кольца/DC, пауза для тона/клика)
        i2s_audio_set_trace(2);   // перед poll_cmd
        i2s_audio_poll_cmd();
        i2s_audio_set_trace(3);   // после poll_cmd

        // r703: PA_DAT (рантайм-биты PA10/PA15) — применяем здесь, раз за цикл
        // долива. core0 ставит флаг в coherent-shadow (led.c pa_dat_request),
        // единый писатель РЕГИСТРА — CPU2, без прерываний и RMW-гонок.
        pa_dat_apply_desired();

        // 2) если core0 поставил паузу (тест-тон/клик выводит сам напрямую
        //    в FIFO) — НЕ доливаем, чтобы не было двойного потребителя.
        if (i2s_audio_paused())
            continue;

        // 3) долив: пишем пары, пока в TX FIFO есть место. Когда FIFO полон
        //    — flush_max сам выходит (TXE_CNT < 8); освободится на 48 кГц —
        //    цикл снова допишет. ТЕМП = ЖЕЛЕЗО, без таймера.
        //    D-audio (C1): НЕ долбить I2S_FIFO_STA вплотную на почти-полном
        //    FIFO. Если за проход записалось меньше, чем запрошено (FIFO
        //    «висит на границе»), выдыхаем несколько NOP — иначе будем
        //    снова и снова читать регистр периферии, забивая шину (AHB/CP15)
        //    и отбирая полосу у core0/CPU1. FIFO сам освободится за ~20 мкс.
        i2s_audio_set_trace(4);   // перед flush_max
        int wrote = i2s_flush_max(24);
        i2s_audio_set_trace(5);   // после flush_max
        if (wrote == 0) {
            // r702-DIAG: если flush_max вернул 0 (watermark/FIFO) — помечаем
            // NOP-backoff; если trace застыл на 6 при beat стоящем — CPU2
            // жив, но не может выйти из flush_max (залип в watermark-gate
            // или wait_tx_room).
            i2s_audio_set_trace(6);
#if defined(__GNUC__)
            __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop");
#endif
        }

        // 4) самообслуживание кольца: переполнение срезает ПРОДЮСЕР
        //    (i2s_push_sample), а не CPU2 — раньше trim здесь конкурентно
        //    обновлял g_ring_rd/сбрасывал dc (данные core0) = data-race и
        //    «хвосты». CPU2 НЕ трогает trim вообще.

        // 5) heartbeat + локальный счётчик — оба должны расти, если CPU2
        //    реально крутит цикл (r702-DIAG: beat — «тик», live — «прогон»).
        beat++;
        live++;
        i2s_audio_set_beat(beat);
        i2s_audio_set_live(live);
        i2s_audio_set_trace(7);   // конец итерации — все стадии пройдены
    }
}