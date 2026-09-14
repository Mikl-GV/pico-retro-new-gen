# Архитектура

Bare-metal мультисистемный эмулятор для Allwinner H3 (Orange Pi Lite).
Без ОС, без фреймворков. HDMI-стек — из `lib-h3` (MIT).

## Слои

```
┌───────────────────────────────────────────────────────────────┐
│                         main.c                                │
│          меню → браузер ROM → диспетчер эмуляторов            │
│     (a2600 / a5200 / a7800 / nes / sms по sys_id)             │
├───────────────────────────────────────────────────────────────┤
│  mcume/    a5200/    a7800/    nes/    smsplus/               │
│  (A2600)   (A5200)   (A7800)   (NES)   (SMS/GG)              │
├───────────────────────────────────────────────────────────────┤
│  system_atari_h3.cpp  system_a5200_h3.cpp  system_a7800_h3.cpp │
│  nes_host.cpp          system_sms_h3.cpp  — host-слои         │
├───────────────────────────────────────────────────────────────┤
│  cpu6502.c (не используется — MCUME/Sally/InfoNES/K6502)     │
│  emu.c  menu.c  rom_browser.c  settings.c                     │
│  usb_kbd.c  usb_ohci.c  sd.c  fat.c                          │
│  fb_text.c  uart.c  printf.c  libc_min.c                      │
├───────────────────────────────────────────────────────────────┤
│  HDMI: h3_de2 + h3_hdmi + dw_hdmi + h3_lcd                    │
│  (1024×600, DE2 → TCON1 → HDMI PHY)                            │
├───────────────────────────────────────────────────────────────┤
│  startup.S  linker.ld  udelay  h3_hs_timer                     │
│  (ARM entry → SVC → BSS=0 → VFP/NEON → main())                │
└───────────────────────────────────────────────────────────────┘
```

## Эмуляторы

| Система | Ядро | Язык | Рендер | Ввод |
|---------|------|:----:|--------|------|
| Atari 2600 | MCUME (Virtual VCS) | C (gnu89) | 160×192 RGB565 → EMU_FB | usb_kbd_get_raw |
| Atari 5200 | pico5200 (Atari800) | C (gnu89) | 320×240 XRGB8888 → HDMI FB | usb_kbd_get_raw |
| Atari 7800 | ProSystem | C++17 | 320×240 через maria_LineReady | usb_kbd_get_raw |
| NES | InfoNES | C++ | 256×240 XRGB8888 → HDMI FB | usb_kbd_get_raw |
| SMS/GG | smsplus | C (gnu89) | 256×192 через sms_render_line | usb_kbd_get_raw |

Каждый эмулятор:
- `*_init_game(rom, size)` — загрузка, инициализация
- `*_run_frame()` — один кадр (CPU + видео + звук)
- опционально `*_render_frame()` — блит на HDMI если не built-in

### system_a7800_h3.cpp (A7800, ProSystem)

- Пул памяти: статический `a7_rom_data[1MB]` под ROM
- `maria_LineReady()` — построчный рендер с масштабированием visibleArea → 240 строк
- Кнопки: pad-маска из usb_kbd, маппинг в 17-байтовый input-массив RIOT

### system_a5200_h3.cpp (A5200, Atari800-derived)

- Пул памяти: `a5_memory_pool[68K]` (64K RAM + резерв)
- `a5_DrawLinePal16()` — строка 320px → HDMI FB
- Заглушки StateSav/SndSave/emuFile — ядро Atari800 тянет много legacy

### system_sms_h3.cpp (SMS/GG, smsplus)

- Пул: статический `sms_heap[128K]` через frens_f_malloc
- `sms_render_line()` → промежуточный `last_fb[192][256]` → `blit_fb()` в HDMI
- SN76489-звук заглушен (snd.enabled=1, буферы молчат)

### nes_host.cpp (NES, InfoNES)

- Рендер: SCREEN[240][256] → построчно через palette LUT → HDMI FB
- Throttle: udelay по h3_hs_timer, 60 FPS

### system_atari_h3.cpp (A2600, MCUME)

- Пул: статический bump-аллокатор
- `emu_DrawScreenPal16()` → EMU_FB (160×192 RGB565)
- `blit_emu_fb()` → центрированный блит на HDMI (1024×600)

## Управление (единый маппинг)

| Клавиша | NES | A2600 | A5200 | A7800 | SMS/GG |
|---------|:---:|:-----:|:-----:|:-----:|:------:|
| ↑ ↓ ← → | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad |
| **Z** | A | Fire | Fire | B1 (A) | Button 1 |
| **X** | B | — | Pause | B2 (B) | Button 2 |
| **S** | Select | Select | Start | Select | Pause |
| **Enter** | Start | Reset | Key 3 | Start | — |
| **ESC** | Выход | Выход | Выход | Выход | Выход |

## HDMI

Как в оригинальном lib-h3, без изменений:
1. PLL_DE → 432 МГц, PLL_VIDEO → 297 МГц
2. DE2 mixer0 → UI канал → framebuffer XRGB8888
3. TCON1 → LCD0 → HDMI PHY/DW-HDMI
4. Force-hotplug

Разрешение: **1024×600 @ 60 Гц**, pixel clock 51.2 МГц.

## Карта памяти (DRAM)

| Адрес | Назначение |
|-------|------------|
| 0x40000000 | Код (ELF → startup.S) |
| 0x50000000 | Буфер загрузки ROM с SD (24 МБ) |
| 0x5F800000 | Emu framebuffer (RGB565; A2600 160×192, NES/SMS 256×... ) |
| 0x5F900000 | HDMI framebuffer (1024×600 XRGB8888) |
| 0x80000000 | Стек (конец DRAM) |

## Загрузка

```
U-Boot SPL → U-Boot → fatload mmc 0 0x40000000 h3_bare.bin → go 0x40000000
→ startup.S → SVC mode → BSS=0 → main()
```

Или через FEL: `sunxi-fel write 0x40000000 build/h3_bare.bin execute 0x40000000`

## Производительность

Оценка на A2600: ~8000 опкодов/кадр, ~192 строки рендера, ~2 мс блит на 1024×600.
Укладывается в 16.6 мс (60 FPS) с запасом >10×.

Более тяжёлые системы (A5200 — ANTIC 140K, A7800 — MARIA 55K вызовов/кадр)
могут быть в 5-10× тяжелее. 512 МБ DRAM + Cortex-A7 @ 1.2 ГГц — запас достаточен.