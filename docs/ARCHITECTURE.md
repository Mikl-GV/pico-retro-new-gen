# Архитектура

Bare-metal мультисистемный эмулятор для Allwinner H3 (Orange Pi Lite).
Без ОС, без фреймворков. HDMI-стек — из `lib-h3` (MIT).

## Слои

```
┌───────────────────────────────────────────────────────────────┐
│                         main.c                                │
│          меню → браузер ROM → диспетчер эмуляторов            │
│     (a2600 / a5200 / a7800 / nes / sms / portfolio)           │
├───────────────────────────────────────────────────────────────┤
│  mcume/    a5200/    a7800/    nes/    smsplus/  portfolio/   │
│  (A2600)   (A5200)   (A7800)   (NES)   (SMS/GG) (8088)       │
├───────────────────────────────────────────────────────────────┤
│  host-слои: system_atari_h3.cpp  system_a5200_h3.cpp          │
│  system_a7800_h3.cpp  nes_host.cpp  system_sms_h3.cpp         │
│  portfolio/system_portfolio.cpp (+ pofo_compat_h3.h)          │
├───────────────────────────────────────────────────────────────┤
│  emu.c (циклы + emu_scale)  menu.c  rom_browser.c  settings.c │
│  usb_kbd.c  usb_ohci.c  sd.c  fat.c                           │
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
| Atari 2600 | MCUME (Virtual VCS) | C (gnu89) | 160×192 → EMU_FB | usb_kbd_get_raw |
| Atari 5200 | pico5200 (Atari800) | C (gnu89) | 320×240 → EMU_FB | usb_kbd_get_raw |
| Atari 7800 | ProSystem | C++17 | 320×240 через maria_LineReady → EMU_FB | usb_kbd_get_raw |
| NES | InfoNES | C++ | 256×240 → EMU_FB | usb_kbd_get_raw |
| SMS/GG | smsplus | C (gnu89) | 256×192 через sms_render_line → EMU_FB | usb_kbd_get_raw |
| Atari Portfolio | Fake86 (8088) | C++ | 320×240 через compat-слой → EMU_FB | USB-клава + UART (полная клавиатура) |

Каждый эмулятор:
- `*_init_game(rom, size)` — загрузка, инициализация
- `*_run_frame()` — один кадр (CPU + видео + звук)
- опционально `*_render_frame()` — блит в EMU_FB если не built-in

Пиксели всех систем складываются в **общий буфер EMU_FB** (0x5F800000, 320×240 RGB565),
затем `emu_scale(src_w, src_h)` растягивает на весь HDMI (1024×600) — высота 600,
ширина пропорционально, поля по бокам.

### EMU_FB / масштабирование

- `EMU_FB` — 320×240 × 2 байта = 150 КБ, один на все системы
- Каждый эмулятор пишет кадр в левый верхний угол своего родного разрешения
  (160×192, 256×240, 256×192, 320×240)
- `emu_scale(w, h)` — nearest neighbour на всю высоту (600), центрирование по X
- Вызывается в `emu_run_*()` после каждого кадра

### system_a7800_h3.cpp (A7800, ProSystem)

- Пул памяти: статический `a7_rom_data[1MB]` под ROM
- `maria_LineReady()` — построчный рендер с масштабированием visibleArea → 240 строк
- Кнопки: pad-маска из usb_kbd, маппинг в 17-байтовый input-массив RIOT

### system_a5200_h3.cpp (A5200, Atari800-derived)

- Пул памяти: `a5_memory_pool[68K]` (64K RAM + резерв)
- `a5_DrawLinePal16()` — строка 320px → EMU_FB
- Заглушки StateSav/SndSave/emuFile — ядро Atari800 тянет много legacy

### system_sms_h3.cpp (SMS/GG, smsplus)

- Пул: статический `sms_heap[128K]` через frens_f_malloc
- `sms_render_line()` → промежуточный `last_fb[192][256]` → `blit_fb()` в EMU_FB
- SN76489-звук заглушен (snd.enabled=1, буферы молчат)

### nes_host.cpp (NES, InfoNES)

- Рендер: SCREEN[240][256] → построчно через palette LUT → EMU_FB
- Throttle через emu_throttle, 60/50 FPS (настройка в Settings)

### system_atari_h3.cpp (A2600, MCUME)

- Пул: статический bump-аллокатор
- `emu_DrawScreenPal16()` → EMU_FB (160×192 RGB565)
- Сложность P1 (Novice/Expert) — настройка Settings

### portfolio/system_portfolio.cpp (Atari Portfolio, Fake86 8088)

- 8088 CPU (Fake86) + PIC 8259 + PIT 8253 + HD61830 LCD-контроллер
- ROM 256K (cart+BIOS) и chargen (лат/кир) — вшиты в заголовки
- RAM 128K — один статический блок `pofo_ram[0x20000]`
- `pofo_compat_h3.h` — слой совместимости display/joypad/uart/timer поверх H3
- Ввод: полная USB-клавиатура (HID→сканкод матрицы Portfolio), UART-канал (PuTTY),
  экранная клавиатура по Insert
- Вывод: HD61830 VRAM → LCD-рендер → EMU_FB → emu_scale
- UART-эхо экрана (текст DIP DOS в терминал), команды PIN/APPS/HELP/EXIT

## Управление

Полная карта — в `docs/CONTROLS.md`. Кратко:

| Клавиша | NES | A2600 | A5200 | A7800 | SMS/GG | Portfolio |
|---------|:---:|:-----:|:-----:|:-----:|:------:|:---------:|
| ↑ ↓ ← → | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | курсор/VK |
| Z | A | Fire | Fire | B1(A) | Btn1 | буква Z |
| X | B | — | Pause | B2(B) | Btn2 | буква X |
| S | Select | Select | Start | Select | Pause | буква S |
| Enter | Start | Reset | Key3 | Start | — | Enter |
| Insert | — | — | — | — | — | VK (экранная клава) |
| ESC | Выход | Выход | Выход | Выход | Выход | удерж. ~1с — выход или EXIT |

## Настройки (Settings)

| # | Функция |
|---|---------|
| 1 | Создать папки ROM на SD |
| 2 | Input Test (тест кнопок) |
| 3 | Video Mode: 60 Hz ⇄ 50 Hz |
| 4 | Atari 2600 Difficulty: Novice ⇄ Expert |

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
| 0x5F800000 | EMU_FB — общий кадровый буфер эмуляторов (320×240 RGB565) |
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

Более тяжёлые системы (A5200 — ANTIC 140K, A7800 — MARIA 55K вызовов/кадр,
Portfolio — 8088 интерпретация + LCD-рендер) могут быть в 5-10× тяжелее.
512 МБ DRAM + Cortex-A7 @ 1.2 ГГц — запас достаточен.