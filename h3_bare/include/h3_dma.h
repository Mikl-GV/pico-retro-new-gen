// h3_dma.h — минимальный драйвер sunxi-DMA (H3, sun8i) для звука.
//
// Задача (r740, решение владельца): I2S0 TX обслуживает ЖЕЛЕЗНЫЙ DMA, а не
// CPU2. Многодневная отладка CPU-долива упёрлась в то, что запись в
// полностью заполненный TX FIFO вешает AHB-шину (CPU2 залипает внутри
// flush_max без исключений, ST:2 EN=EX+1, ABT:0). DMA пишет в FIFO только по
// аппаратному DRQ (когда в FIFO есть место) — залипание на полном FIFO
// невозможно в принципе. Поэтому звук переводится на циклическое DMA:
//   RAM-буфер (uncached .dma_buf) → I2S0_TX_FIFO (0x01C22020, IO-mode, DRQ=3)
// Продюсер (core0) пишет пары в буфер, отслеживая текущую позицию DMA по
// DMA_CHAN_CUR_SRC (поллинг, GIC не используется).
//
// Референс форматов: linux-драйвер drivers/dma/sun6i-dma.c (конфиг
// sun8i_h3_dma_cfg) + Allwinner_H3_Datasheet V1.2 (раздел 4.11 DMA).
// Базовый адрес DMAC: 0x01C02000. Канал N: 0x100 + N*0x40.

#ifndef H3_DMA_H_
#define H3_DMA_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация DMAC (CCU-gate/reset, autogate) и канала ch для звука.
// Буфер buf (физический адрес, uncached) длиной buf_bytes передаётся DMA
// циклически в I2S0_TX_FIFO. Возвращает 0 при успехе, -1 при ошибке.
int  h3_dma_audio_init(int ch, uint32_t buf_phys, uint32_t buf_bytes);

// Запустить/остановить циклическую передачу.
void h3_dma_audio_start(void);
void h3_dma_audio_stop(void);

// Текущий адрес источника, который DMA читает (CUR_SRC). Если DMA стоит —
// последняя позиция. None 0 при неактивном канале.
uint32_t h3_dma_audio_cur_pos(void);

// r764 (прерывания): INTID аудио-DMA (SPI 82 → 114) для регистрации в GIC.
uint32_t h3_dma_audio_intid(void);

// r764: вызывается из ISR (CPU2) при каждом PKG — сброс pending + инкремент
// счётчика завершённых пакетов. Возвращает новый счётчик.
uint32_t h3_dma_audio_pkg_isr(void);

// r764: сброс счётчика PKG (вызывает core0 при ring_reset/init).
void h3_dma_audio_pkg_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* H3_DMA_H_ */