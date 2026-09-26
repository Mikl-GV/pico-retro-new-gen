# FINDINGS.md — полный аудит кода pico-retro-new-gen (v2, актуализирован на r124)

Анализ: 2026-09-25, тулчейн arm-none-eabi 15.2.1, ветка main, r124.
Охват: `h3_bare/cores/`, `h3_bare/src/`, `h3_bare/platform/`, `h3_bare/include/`,
`platform/fb/`, `docs/`, Makefile, build.sh.

Типы: `[баг]` `[гонка]` `[мусор]` `[эффект]` `[косметика]` `[OK]` `[вопрос]`.
Статусы: `[fixed r..]` — исправлено, `[акт.]` — актуально в r124.

---

## 1. TFT-дисплей (tft_drv.c/h)

| № | Файл:строка | Проблема | Тип | Статус r124 |
|---|---|---|---|---|
| P1 | tft_flush/flush_rect | R/B swap при MADCTL=0xE0 | [баг] | [fixed r97+] `tft_swap_rb()` в fill_rect/puts/puts2 |
| P2 | tft_drv.c:736-746 | Верстка: строки на разделитель | [верстка] | [fixed] заголовок y=2, разделитель y=24, строки y=30 |
| P3 | tft_touch_scan | Калибровка под MADCTL 0xE0 | [баг] | [fixed r110] rx4→sx, ry4→sy=319-…, Z1-порог 2140 |
| P4 | — | Сырые rx/ry при удержании | [замечание] | [fixed r113] |
| P5 | cs_low/cs_high | Дёргают оба CS | [замечание] | [fixed r126] cs_select(0)/cs_disp_high — дисплей не дёргает PA21 |
| P6 | fb_text.c:246 | g_tft_frame_ready мёртв | [мусор] | [fixed r130] удалён |
| P7 | g_mode/setters | Мёртвые | [мусор] | [fixed r130] удалены |
| P8 | tft_tick() | Мёртвый | [мусор] | [fixed r130] удалён |
| P9 | tft_touch_probe_cs | Двойной probe | [OK] | [OK] прогрев TSC2046; добавить коммент |

**Находка r72/r73 (подтверждена):** TSC2046I, только Mode 1 (CPHA=1, SDM=1),
value<<4, холостой 2048, CS=PA21, ждать RF_CNT>0.

---

## 2. SMP / межъядерная связь

| № | Файл:строка | Проблема | Тип | Статус r124 |
|---|---|---|---|---|
| P10 | .coherent help_id/epoch/request | «uncached, гонок нет» | [OK] | [УСТАРЕЛ] переменные УДАЛЕНЫ в r124, связь на SRAM |
| P11 | fb_text.c:246 | g_tft_frame_ready | [мусор] | [fixed r130] см. P6 |
| P12 | startup.S 0x20 почта | OK | [OK] | [OK] |

**K1 (r124):** `mmu_mark_uncached` звался только из `usb_ohci_init` — без USB
`.coherent` оставался write-back и записи core0 не были видны CPU1.
Исправление: явный вызов в main.c + вся функциональная связь на SRAM A1
(0x34..0x74). Старые описания архитектуры (help_id/epoch/request) обновить в docs.

---

## 3. ТАЙМЕРЫ (новый раздел — математика из кода)

### 3.1. Корень — HSTMR

| № | Файл:строка | Код | Комментарий | Вывод |
|---|---|---|---|---|
| T1 | h3_hs_timer.c:46 | `CTRL \|= (1u<<4)` | (нет) | бит 4 = CLK_SRC? переключает источник с OSC24M — **нет коммент** |
| T2 | h3_hs_timer.h:38 | `~(CURNT_LO/24)` | «от OSC24M, 1 ед.=41.67 нс» | делитель 24 верен только при OSC24M; init переключил источник → **внутреннее противоречие** |

### 3.2. Производные (наследуют масштаб lo_us)

| № | Место | Код | Комментарий | При 24 МГц | При ~96 МГц | Статус |
|---|---|---|---|---|---|---|
| T3 | udelay.c:41 | 24*d | «24 тика/мкс» | ок | 0.25×d мкс | [баг] наследует T2 |
| T4 | tft_drv.c:366-368 | delay_ms=lo_us*1000 | «мс» | ок | 0.25×ms мс | [баг] init TFT 20/150/150/150→5/37/37/37 мс |
| T5 | i2s.c:161 | I2S_PACE_UNITS=21 | «20.8 мкс=48кГц» | 47.6 кГц ✓ | 190 кГц | [потенц.] |
| T6 | sega_pad.c:74-79 | t_half=130 | «~1.3 мкс, SCL ~370 кГц» | ✓ рабочая скорость детекта 6-btn | [fixed r147/r153] при 92 кГц чип не успевал |
| T7 | sega_pad.c:154-167 | th_* = 20 мкс через lo_us (64-бит) | «паузы фаз детекта» | ✓ скан ~1.4 мс < окна 1.6 мс | [fixed r152/r153] |
| T8 | emu.c:154-160 | throttle elapsed+udelay | «60 Гц» | ✓ | самосогласовано, абс. ~240 Гц | [вопрос] |
| T9 | emu.c:182 | ESC-hold 900000 | «0.9 с» | ✓ | 225 мс | [потенц.] |
| T10 | usb_kbd.c:563 | wait_release 5000 | «5 мс тишины» | ✓ | 1.25 мс теряет Enter/ESC | [баг] (P26) |
| T11 | usb_ohci.c:349,354 | udelay(50000/100000) | «50/100 мс PRS» | ✓ | 12/24 мс | [потенц.] |
| T12 | SPI0_CCR 0x1005 | «8 МГц» | — | — | формула не проверяема без CCU | [вопрос] |
| T13 | ngp_host.cpp:102 | SDL_GetTicks=lo_us/1000 | «мс» | ✓ | 0.25 мс | [потенц.] |

**Открытый вопрос V1 (блокер):** фактическая частота HSTMR (24 МГц или ~96/100).
Замер на железе: мигание PA15 N раз с lo_us-паузой + секундомер. От ответа зависят T3-T13.

---

## 4. СЛОИ ЭМУЛЯТОРОВ И HOST-СЛОЙ (новый раздел)

### 4.1. Контракт

| Слой | Файл | Run | Render | Stop |
|---|---|---|---|---|
| GB | gameboy_host.cpp | run_until PPU_FRAME_TICKS | fb→EMU_FB | нет |
| GBA | gba_host.c | execute_arm | gba_screen→EMU_FB | нет |
| Lynx | lynx_host.cpp | Update + safety 4M | lynx_fb→EMU_FB | delete |
| SNES | snes_host.cpp | S9xMainLoop | GFX.Screen→EMU_FB | S9xDeinit* |
| Coleco | coleco_host.cpp | RunToVBlank(EMU_FB) | ядро в EMU_FB | delete |
| A5200 | system_a5200_h3.cpp | at5_Step | a5_DrawLinePal16 | нет |
| A7800 | system_a7800_h3.cpp | run | maria_LineReady | нет |
| A2600 | system_atari_h3.cpp | run_frame | EMU_FB | нет |
| MD/SMS/GG | gpgx/system_gpgx_h3.c | system_frame_* | bitmap→EMU_FB | gp_cheats_clear |
| NES | fceumm/nes_host_fceumm.cpp | FCEUI_Emulate | XBuf палитра→EMU_FB | CloseGame |
| Vectrex | vecx_host.c | vecx_emu(30000) | свой 1024×600 | нет |
| NGP | ngp/ngp_host.cpp | ngp_run | blit→EMU_FB | нет |

### 4.2. Находки по слоям

| № | Файл:строка | Проблема | Тип | Решение |
|---|---|---|---|---|
| H1 | fceumm/nes_host_fceumm.cpp:237-241 | **deemp-OOB**: `nes_pal_rgb565[base + (src&0xFF)]`, base=256+(deemp&7)<<6 → при deemp=4..7 индекс >511 | [баг] | кламп deemp&0x03 или таблица 1024; проверить диапазон deemp ядра |
| H2 | lynx_host.cpp:128 + led.c:62-65 | **PA_DAT-гонка**: core0 `led_set` при запрете из led.c («НЕ писать с CPU0») | [гонка] | убрать led_set из lynx_run_frame |
| H3 | ngp_host.cpp:102 | SDL_GetTicks → lo_us/1000 | [потенц.] | после V1 |
| H4 | gba_host.c:136-139 | drain `sound_read_samples` каждый кадр | [эффект] | убрать/коммент |
| H5 | snes_host.cpp:60-88 | malloc/free LIFO, порядок free совпадает с выделением | [вопрос] | добавить коммент |
| H6 | system_gpgx_h3.c | два блока config (MD и GG) — дублирование | [косметика] | общая функция config |
| H7 | system_a5200_h3.cpp:130 | at5_Start("cart") — проверить ядро | [вопрос] | проверить a5200 |
| H8 | кол-во sega_pad_scan | 12 слоёв × кадр | [эффект] | при Sega=нет возвращает 0 — ок |

---

## 5. Остальные P

### Мёртвый код / сборка
| № | Проблема | Статус |
|---|---|---|
| P13 | emu_osd_apply() пуста, зовётся каждый кадр (fb_text.c:237) | [fixed r130] удалена |
| P14 | sega_pad_scan в host-слоях, все на core0 | [OK] |
| P15 | gba_buttons() безусловно читает геймпад | [OK] |
| P16 | геймпад+клавиатура параллельно | [OK] |
| P17 | баннер main.c | [fixed] каждый релиз |
| P18 | tft_tick() не вызывается | [fixed r130] удалён |
| P19 | g_mode/setters/g_tft_frame_ready | [fixed r130] удалены |
| P20 | emu_osd_apply пуста | [fixed r130] удалена |
| P21 | build.sh: нет h3_smp.o, tft_drv.o, i2s.o | [fixed r124] |
| P22 | Makefile cygpath | [вопрос] ждёт подтверждения |

### I2S / ввод / FB / меню / FAT
| № | Проблема | Статус |
|---|---|---|
| P23 | g_ring_rd не volatile | [fixed r124] |
| P24 | s_next без сброса | [fixed r124] → g_i2s_next |
| P25 | t_half переполнение 32-bit | [OK] не зацикливает |
| P26 | Enter/ESC теряются в wait_release | [акт.] см. T10 |
| P27 | fb_putchar/fb_putchar_s дублируют '_' | [OK] | идентичны, расхождение не грозит |
| P28 | menu_scroll_to hdr | [OK] | hdr виден при max_visible>=2 — по арифметике корректно |
| P29 | fat g_sector non-reentrant | [OK] |
| P30 | fat_next_cluster(0) | [fixed r131] защита cl<2 |

---

## 6. Карта памяти (сверена, r124)

| Область | Адрес | Статус |
|---|---|---|
| .text | 0x40000000 (3.4M) | OK |
| .data/.bss | 0x4048..0x41FB5C30 | OK |
| Куча _gb_heap | 0x41FB6D40 (24M) → 0x437B6D40 | OK |
| _hend/_sbrk | → SBRK_LIMIT 0x4F000000 | OK |
| .coherent | 0x43800000 (1M uncached) | OK |
| _menu_arena | 0x4F000000 | OK |
| ROM_BUF | 0x50000000 (24M) | OK |
| EMU_FB | 0x5F800000 | OK |
| HDMI FB | 0x5F900000 (конец 0x5FB58000) | OK |
| Стеки CPU1 | 0x5FDFD000..0x5FE01000 | OK |
| Стеки core0 | 0x5FF0x000 / 0x60000000 | OK |

Пересечений нет. IRQ не включаются (вектор→_hang), OHCI IRQ disabled.

---

## 7. КАРТА ТАЙМЕРА (кто задействован, r129)

**Единственный источник времени — HSTMR** (`h3_hs_timer`), 64-бит, ~96.8 МГц.
`h3_hs_timer_lo_us()` (r128, 64-битная) → настоящие микросекунды, делитель 97 (r127).

### Кто читает время (полный список)

| Модуль | Файл:строка | Что делает | Требование |
|---|---|---|---|
| udelay() | udelay.c:40 | задержки НА ВСЁМ проекте | d мкс (через HSTMR_MHZ*d) |
| throttle эмуляторов | emu.c:150-160 | кадровая частота | 60 Гц = 16667 мкс (из настроек, F2) |
| ESC-hold эмулятора | emu.c:173-187 | удержание ESC ~0.9 с | 900000 мкс |
| tft_init / delay_ms | tft_drv.c:366 | reset+init дисплея | 20/150/150/150 мс — теперь честные |
| heartbeat CPU1 | led.c:75-84 | мигание PA15 ~1 с | 500000 мкс (после r127 период 0.996 с — подтверждено) |
| I2S звук | i2s.c:161-172 | темп 48 кГц | I2S_PACE_UNITS=21 «мкс»/пара |
| sega-геймпад | sega_pad.c:74-79 | SCL ~370 кГц | t_half=130 тиков (r147/r153: рабочая) |
| sega-геймпад фазы | sega_pad.c:154-167 | TH-паузы 20 мкс | через lo_us (64-бит), 8 уровней TH из gen_hw |
| USB / OHCI | usb_ohci.c | PRS, дескрипторы, ресет | udelay — теперь честный |
| usb_kbd | usb_kbd.c | авто-повтор, wait_release 20 мс | lo_us — честный |
| SMP запуск | h3_smp.c | паузы опроса почты | udelay |
| HDMI | dw_hdmi.c/h3_hdmi.c | инициализация таймингов | udelay |
| Portfolio | portfolio/…cpp | pin-тайминги, ESC | lo_us |
| NGP SDL | ngp/ngp_host.cpp | SDL_GetTicks (мс) | lo_us/1000 |

### Регулировка эмуляторов (50/60 FPS)
- `emu_throttle` ждёт до `emu_period_us`, значение из настроек:
  - 16667 → 60 Гц, 20000 → 50 Гц, 22222 → 45, 25000 → 40, 28571 → 35, 33333 → 30 Гц
- **Больше 60 не бывает**: «скорость» = заниженный таймер (0.248 мкс) на старых прошивках → эмуляторы летали 240 fps. После r127 — честные мкс → 60 Гц.

### PA15 (лайв-индикатор CPU1)
- Моргает только из главного цикла CPU1 и калибровки (r127).
- r129: watchdog в tft_touch_scan (сброс SPI после 100 мс) + stall-детектор цикла
  («TFT: slow after scan/delay» в UART) — если PA15 снова замолчала, UART покажет, где застряло.

---

## 8. Приоритеты правок

| Приор | Пункт | Что | Файлы |
|---|---|---|---|
| 🔴 | H1 | FCEUmm deemp-OOB | fceumm/nes_host_fceumm.cpp |
| 🔴 | H2 | Lynx PA_DAT-гонка | lynx_host.cpp |
| 🔴 | T1/T2 | HSTMR CLK_SRC vs делитель 24 — замер + одна константа HSTMR_MHZ | h3_hs_timer.c/h, все производные |
| 🟠 | T10/P26 | Enter/ESC теряются | usb_kbd.c |
| 🟠 | P5 | cs_select вместо cs_low/high | tft_drv.c |
| 🟠 | P6-P8, P13, P20 | мёртвый код | tft_drv.c, fb_text.c, emu.c |
| 🟡 | T7 | джойстик: протокол/тайминги | [fixed r147-r153] SCL 370 кГц, паузы 20 мкс, 8 уровней TH, окно 1.6 мс |
| 🟡 | H4, H6, H8, P27-P30 | мелочи | по файлам |