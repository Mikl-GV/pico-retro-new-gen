# План: аудио-подсистема pico-retro-new-gen (актуально r776)

## Итоговая архитектура (r773/r776, реализовано)

- **Звук работает** на I2S0 (MAX98357A, 48 кГц стерео 16-бит).
- Аудио-ядро **CPU2** (audio_core.c): вечный цикл — почта (PAUSE/RESUME) +
  `i2s_poll_fill()` + heartbeat. Долив кольца в TX FIFO — **поллингом**, без
  прерываний и без DMA.
- core0 (эмулятор) — только продюсер: `i2s_push_sample()` → кольцо в `.coherent`.
  Единственный писатель TX FIFO — CPU2.
- Кольцо SPSC (8 КБ пар), индексы и почта — в `.coherent` (uncached).
  Барьеры: продюсер публикует `store l/r; dmb; wr++`, потребитель читает с `dmb`
  (r776 P0-4). Владелец fade-полей — один (гейт `g_audio_reset`, r776 P0-5).
- DMA/GIC-путь (r740..r772) **упразднён** (r773) и **удалён** (r776):
  `h3_dma.c/h`, `audio_irq_test.c`, секция `.dma_buf` в linker.ld — убраны.
  Причина: DMA замирал на ~34 пакетах (pending), GIC-доставка IRQ на CPU2 не
  работала; поллинг — эталон uli/allwinner-bare-metal для H3.

```
core0 (эмулятор): синтез → i2s_push_sample() → кольцо (.coherent)
                                                    │
CPU2 (audio_core): i2s_poll_fill() ← кольцо → I2S0 TX FIFO → MAX98357A
```

## Подключённые системы (звук в I2S)

Все: Lynx, GBA, GB/GBC, NGP, MD/SMS/GG, NES, SNES, A2600, A5200, A7800, Coleco,
PCE, ZX Spectrum, MSX, BK-0010, Vectrex, Portfolio, MS1504 (PC-спикер), аркады
FBNeo. Ресемплеры (если синтез ≠ 48 кГц): GBA 32768→48к (полифаз), NGP 44100→48к,
A7800 31440→48к, MS1504 44100→48к и т.д.

## API

- `i2s_push_sample(l, r)` — продюсер (core0, эмуляторы).
- `i2s_ring_reset()` — сброс кольца/TX (core0, смена игры/клик/выход).
- `i2s_audio_cmd(PAUSE/RESUME)` + heartbeat — почта core0↔CPU2.
- `i2s_click()` — клик меню через кольцо.
- Диагностика: `i2s_cpu2_diag`, `i2s_ring_wr_rd_get`, `i2s_drop_cnt` — из
  Settings → Layer tests (SLT).

## Критерии приёма (проверено на стенде)

- Чистый тон 440/1000/3000 в SLT без «песка», `dropped≈0`.
- В эмуляторах нет щелчков/доезда/ухудшения при перезаходах.
- Единственный писатель TX FIFO — CPU2 (fallback на core0 удалён).

## Документы

- docs/HANDOVER.md — точка сохранения и история дельт аудио (r503..r776)
- docs/AUDIT_2026-10-05_r735.md — аудит