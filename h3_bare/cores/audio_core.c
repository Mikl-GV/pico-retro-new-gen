// audio_core.c — аудио-ядро на CPU2: перенос кольца I2S → DMA-буфер.
//
// АРХИТЕКТУРА (решение владельца, r561 + r740): звук обслуживает CPU2,
// в TX FIFO пишет ЖЕЛЕЗНЫЙ DMA.
//   core0 (эмулятор) --i2s_push_sample--> кольцо (.coherent)
//   CPU2 (audio_core) --i2s_flush_max--> DMA-буфер (.dma_buf, обычная RAM)
//   DMA (h3_dma.c)    --по DRQ-->        I2S0_TX_FIFO (0x01C22020)
//
// Зачем это отличие от r585-r739 (когда CPU2 писал в TX FIFO руками):
// запись в полностью заполненный TX FIFO вешала AHB-шину — CPU2 залипал
// внутри flush_max без исключений (ST:2 EN=EX+1, ABT:0). Теперь CPU2 пишет в
// ОБЫЧНУЮ RAM (DMA-буфер) — такая запись не может залипнуть; в FIFO пишет
// только DMA по аппаратному DRQ, когда в FIFO есть место. CPU2 остаётся
// единственным хозяином потока: темп задаёт DMA (48 кГц железом), а CPU2
// просто следит за кольцом и DMA-буфером.
//
// Детали:
//   - кольцо и индексы — в .coherent (uncached, r584): CPU2 видит актуальное
//     состояние без кэш-когерентности (MMU у CPU2 выключен);
//   - почта (cmd/ack/seq) — для PAUSE/RING_RESET; heartbeat — liveness;
//   - UART: CPU2 НЕ пишет — состояние числом кладёт в .coherent.
//
// r722: CPU2 не пишет в SRAM A1 и не ставит маркеры — только heartbeat.
#include <stdint.h>
#include "h3.h"
#include "i2s.h"
#include "h3_dma.h"
#include "gic.h"
#include "led.h"

void cpu2_audio_entry(void) {
    // r764: GIC — аудио-DMA (INTID 114) будет приходить СЮДА (CPU2). Ядро
    // Secure (sec=0x0), регистры GIC доступны. Разрешаем IRQ (CPSR.I=0).
    gic_init();
    __asm volatile("cpsie i" ::: "memory");
    // Сердце: пометить себя активным, затем вечный цикл долива кольца в
    // DMA-буфер. Темп задаёт аппаратный DMA (48 кГц), CPU2 лишь следует за
    // позицией DMA (dma_free_pairs в flush_max) — залипание исключено.
    i2s_audio_set_state(1);
    i2s_audio_set_beat(0);

    uint32_t beat = 0;
    for (;;) {
        // 1) команда от core0 (сброс кольца/DMA-буфера, пауза для тона/клика)
        i2s_audio_poll_cmd();

        // 2) если core0 поставил паузу — не наполняем DMA-буфер
        //    (тест-тон/клик/ring_reset пишут через кольцо после RESUME).
        if (i2s_audio_paused())
            continue;

        // 3) долив: переносим пары из кольца в DMA-буфер порциями.
        //    flush_max сам выйдет, если DMA-буфер полон (CPU2 обогнал DMA)
        //    или кольцо пусто (тогда пишет тишину — поток непрерывен).
        //    r755: если flush_max вернул 0 (DMA-буфер полон / тишина —
        //    ничего не записано), выдыхаем NOP-backoff — иначе цикл вплотную
        //    читает CH_CUR_SRC регистра периферии, забивая AHB-шинy.
        {
            int wrote = i2s_flush_max(24);
            if (wrote == 0) {
#if defined(__GNUC__)
                __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop");
#endif
            }
        }

        // 4) heartbeat — liveness CPU2 (мониторинг, не fallback)
        beat++;
        i2s_audio_set_beat(beat);
    }
}