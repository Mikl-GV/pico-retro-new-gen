// audio_core.c — аудио-ядро на CPU2: долив кольца → I2S TX FIFO поллингом.
//
// АРХИТЕКТУРА (r773, решение владельца): БЕЗ прерываний и БЕЗ DMA. GIC-доставка
// на CPU2 не работает (INTID не доходит), DMA вставал на 34 пакетах из-за
// нечищенного pending — обе проблемы убраны из уравнения выбором пути.
//   core0 (эмулятор) --i2s_push_sample--> кольцо (.coherent)
//   CPU2 (audio_core) --i2s_poll_fill--> I2S0_TX_FIFO (0x01C22020)
//
// Это эталон uli/allwinner-bare-metal (audio_i2s.c, тот же H3): CPU2 крутит
// цикл, пишет пары в TX FIFO, пока TXE_CNT > 2. Запись в полный FIFO
// невозможна (проверка места), задержка = FIFO (~1.3 мс), единый такт = I2S.
//
// Детали:
//   - кольцо и индексы — в .coherent (uncached, r584): CPU2 видит актуальное
//     состояние без кэш-когерентности (MMU у CPU2 выключен);
//   - почта (cmd/ack/seq) — для PAUSE/RESUME; heartbeat — liveness;
//   - UART: CPU2 НЕ пишет — состояние числом кладёт в .coherent;
//   - GIC/IRQ не используются: cpsie i НЕ выполняется, вектор 0x18 не активен.
#include <stdint.h>
#include "h3.h"
#include "i2s.h"
#include "led.h"

void cpu2_audio_entry(void) {
    // Прерывания НЕ включаем (GIC не инициализирован, IRQ на CPU2 не нужны —
    // долив поллингом). Просто помечаем себя активным и крутим цикл.
    i2s_audio_set_state(1);
    i2s_audio_set_beat(0);

    uint32_t beat = 0;
    for (;;) {
        // 1) команда от core0 (пауза для тона/клика/сброса)
        i2s_audio_poll_cmd();

        // 2) если core0 поставил паузу — не доливаем (тон/клик пишут сами)
        if (!i2s_audio_paused())
            i2s_poll_fill();

        // 3) heartbeat — liveness CPU2 (мониторинг, не fallback)
        beat++;
        i2s_audio_set_beat(beat);
    }
}