# Архитектура

Bare-metal мультисистемный эмулятор для Allwinner H3 (Orange Pi Lite).
Без ОС, без фреймворков. HDMI-стек — из `lib-h3` (MIT).

## Слои

```
┌───────────────────────────────────────────────────────────────┐
│                         main.c                                │
│          меню → браузер ROM → диспетчер эмуляторов            │
│   (a2600/a5200/a7800/nes/sms/gameboy/lynx/ngp/portfolio/md)    │
├───────────────────────────────────────────────────────────────┤
│  mcume/    a5200/    a7800/    fceumm/    gpgx/     portfolio/│
│  (A2600)   (A5200)   (A7800)   (NES)   (MD+SMS)      (8088)   │
│  gameboy/ (binjgb)   lynx/ (Handy)   snes/ (Snes9x 2005)      │
│  ngp/ (RACE — TLCS900H+Z80, NGP/NGPC)                          │
├───────────────────────────────────────────────────────────────┤
│  host-слои: system_atari_h3.cpp  system_a5200_h3.cpp          │
│  system_a7800_h3.cpp  nes_host_fceumm.cpp  system_gpgx_h3.c   │
│  gameboy_host.cpp  lynx_host.cpp  snes_host.cpp  ngp_host.cpp │
│  portfolio/system_portfolio.cpp (+ pofo_compat_h3.h)          │
├───────────────────────────────────────────────────────────────┤
│  emu.c (циклы + emu_scale)  menu.c  rom_browser.c  settings.c │
│  usb_kbd.c  usb_ohci.c  sd.c  fat.c  led.c  fb_text.c         │
│  uart.c  printf.c  libc_min.c  cxx_runtime.cpp                │
│  cheatdb.c (чит-менеджер, парсер .cht)  gp_cheats.c (GPGX)    │
│  sega_pad.c (Sega 6-btn геймпад, PCF8574)                      │
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
| Atari 2600 | MCUME (Virtual VCS) | C (gnu89) | 160×192 → EMU_FB | usb_kbd_get_raw + sega_pad |
| Atari 5200 | pico5200 (Atari800) | C (gnu89) | 320×240 → EMU_FB | usb_kbd_get_raw + sega_pad |
| Atari 7800 | ProSystem | C++ | 320×240 через maria_LineReady → EMU_FB | usb_kbd_get_raw + sega_pad |
| NES / Famicom | **FCEUmm** (FCE Ultra) | C | 256×240 → EMU_FB | usb_kbd_get_raw + SuborKB + sega_pad |
| SMS / GG / MD | **Genesis Plus GX** | C | 256×192 / 256×224 / 320×224 → EMU_FB | usb_kbd_get_raw + sega_pad |
| Game Boy / GBC | binjgb | C | 160×144 → EMU_FB | usb_kbd_get_raw + sega_pad |
| Game Boy Advance | **gpSP** (gpsp) | C/C++ | 240×160 → EMU_FB | usb_kbd_get_raw + sega_pad |
| Atari Lynx | Handy | C++ | 160×102 → EMU_FB | usb_kbd_get_raw + sega_pad |
| Neo Geo Pocket / Pocket Color | **RACE** | C++ | 160×152 → EMU_FB | usb_kbd_get_raw + sega_pad |
| Atari Portfolio | Fake86 (8088) | C++ | 320×240 через compat-слой → EMU_FB | USB-клава + UART (полная клавиатура) |
| SNES / Super Famicom | **Snes9x 2005** (libretro) | C | 256×224/240 → GFX.Screen → EMU_FB | usb_kbd_get_raw + sega_pad |

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
- `emu_set_border_color(rgb888)` — цвет полей по бокам (XRGB8888), свой для каждой
  системы: A2600 — тёмно-янтарный, A5200 — синий, A7800 — бордовый, NES — бордовый,
  SMS — синий, MD — тёмно-синий, Game Boy — зелёный, Lynx — фиолетовый, Portfolio — оливковый,
  SNES — тёмно-синеватый, NGP — тёмно-синий

### system_a7800_h3.cpp (A7800, ProSystem)

- Пул памяти: статический `a7_rom_data[1MB]` под ROM + `a7_ram[16K]` под writable RAM
  (`a7800_set_memory()` обязательно вызывается в init — иначе memory_ram=NULL и memory_Reset() пишет по нулю)
- `maria_LineReady()` — построчный рендер с масштабированием visibleArea → 240 строк
- Кнопки: pad-маска из usb_kbd, маппинг в 17-байтовый input-массив RIOT

### system_a5200_h3.cpp (A5200, Atari800-derived)

- Пул памяти: `a5_memory_pool[68K]` (64K RAM + резерв)
- `a5_DrawLinePal16()` — строка 320px → EMU_FB
- Заглушки StateSav/SndSave/emuFile — ядро Atari800 тянет много legacy

### system_gpgx_h3.c (SMS/GG/MD, Genesis Plus GX)

- Ядро: `gpgx/core/` (genesis.c, vdp_*, mem68k, m68k/z80, sound, cart_hw, cd_hw — без CHD/MP3-декодеров)
- `gpgx_init_game()`: копирует ROM в `cart.rom`, детект SMS (TMR SEGA) / MD, byte-swap
  для 16-бит, конфиг `t_config`, инициализация `bitmap.data[720×576]` (как libretro.c)
- Рендер: `bitmap.data` (RGB565) + viewport → EMU_FB, MD 320×224, SMS 256×192
- Ввод: USB-клавиатура → 6-кнопочный геймпад MD (Z=A X=B C=C A=X S=Y D=Z Q=Mode Enter=Start),
  для SMS: S=Pause (кнопка на корпусе), Enter=Start
- Вспомогательные: `gpgx_math.c` (sin/cos/pow/log — инициализация таблиц звука),
  `gpgx_missing.c` (rf*/crc32/BIOS-пути/load_archive заглушки)
- Фикс Comix Zone: в `cart_hw/sram.c` отключён авто-SRAM для игр с `"COMIX ZONE"`
  в заголовке (2MB картридж без батарейки — авто-SRAM по умолчанию ломал зеркало ROM в `$200000`)

### nes_host_fceumm.cpp (NES/Famicom, FCEUmm)

- Ядро: `fceumm/` (fceu.c, x6502, ppu, sound, cart/ines, boards/ 432 маппера, input/ SuborKB)
- Стаб libretro: `libretro.h` + `libretro_compat.c` (filestream/memstream/string/sscanf/ctype)
- Рендер: XBuf[256×240] + XDBuf (деэмфазис) → palette LUT → EMU_FB
- Ввод: USB-клавиатура → геймпад (Z=A X=B S=Select Enter=Start) + SuborKB-клавиатура
  (Сюбор/LIKO-картриджи; таблица HID-сканкод → SuborKeyboardData[0x65], подключается
  только когда FCEUmm определил inputfc=SIFC_SUBORKB/FKB по CRC-базе)
- ESC: одиночный → SuborKB (Break), выход — по удержанию ~0.9 с

### gameboy_host.cpp (Game Boy / GBC, binjgb)

- Ядро binjgb (облегчённая сборка: emulator.c, memory.c, joypad.c; common.c заменён stubs)
- Менеджер памяти — bump-аллокатор `gb_heap` (3 МБ) в gameboy_stubs.c, `gb_heap_reset()`
- Рендер: RGBA-буфер → RGB565 → EMU_FB (160×144)
- Ввод: USB-клавиатура → кнопки Game Boy (Z=B, X=A, S=Select, Enter=Start, стрелки=D-Pad)
- Звук: аудио-буфер 44100 Гц, заглушен (нет DAC-вывода), но без звука ядро не зависает

### gba_host.c (Game Boy Advance, gpSP)

- Ядро: `gba_sp/` (gpSP: cpu.cc, video.cc, gba_memory.c, sound.c — интерпретатор, без dynarec)
- Порт — как FCEUmm/Snes9x: без libretro.c, ядро линкуется напрямую; стабы `gba_compat.c`
  (time/localtime/netplay/input-savestate), filestream/sscanf — из `fceumm/libretro_compat.c`
- ROM из памяти: host ставит `g_ram_rom/g_ram_rom_size`, ядро мапит страницы ROM напрямую
  на ROM_BUF (без копирования и без LRU-аллокаций, которые требуют 32 МБ кучи)
- BIOS — встроенный open-source 16KB (`bios_data.S` + `bios/open_gba_bios.bin`)
- Имена, конфликтующие с другими ядрами (fceumm/gpgx), переименованы через `objcopy`
  (`vram/reg/cheats/init_memory/init_cpu/load_bios` → `gpsp_*`) — см. Makefile `GBA_RENAME`
- Рендер: `gba_screen_pixels` (240×160 RGB565) → EMU_FB
- Ввод: USB-клавиатура → кнопки GBA (Z=A X=B S=Select Enter=Start Q=L W=R + Sega-геймпад C=L X=R)
- Звук: заглушен (нет DAC-вывода)

### snes_host.cpp (SNES / Super Famicom, Snes9x 2005)

- Ядро: `snes/` (Snes9x 2005, libretro, 39 C-файлов + libretro-common-заглушки)
- `snes_init_game()`: `S9xInitMemory → InitAPU → InitDisplay → InitGFX → InitSound → LoadROM (прямое копирование в Memory.ROM) → S9xReset`. Настройки `Settings.Mute=true` (звук заглушен)
- Рендер: `GFX.Screen` (RGB565, pitch = IMAGE_WIDTH×2 = 1024) → EMU_FB. Разрешение 256×224/240 (H32/H40, NTSC/PAL)
- Ввод: USB-клавиатура → геймпад SNES (Z=B, X=Y, A=A, S=X, Q=L, W=R, Space=Select, Enter=Start)
- Выход: ESC-удержание ~0.9 с
- Сборка с `-DLAGFIX`: иначе `S9xMainLoop` не возвращается (бесконечный цикл без флага `finishedFrame`)

### lynx_host.cpp (Atari Lynx, Handy)

- Порт Handy (K. Wilkins) — работает (рендер починен: pitch байты, сброс heap 3 МБ перед init,
  страховка DISPCTL.DMAEnable)
- `handy_compat.h` — заглушки libretro-common (filestream/strlcpy/string) для bare-metal
- C++ runtime — `cxx_runtime.cpp` (operator new/delete поверх malloc, __cxa_pure_virtual)
- Рендер: Handy рисует в собственный буфер 160×102 через callback → EMU_FB
- Ввод: USB-клавиатура → кнопки Lynx (Z=A X=B S=Option1 Enter=Option2)

### ngp_host.cpp + ngp/ (Neo Geo Pocket / Pocket Color, RACE)

- Ядро RACE (alekmaul): TLCS-900H (tlcs900h.cpp) + Z80 (z80.cpp), память (memory.cpp),
  рендер Thor (graphics.cpp), флеш-картриджи (flash.cpp), звук TI SN76496 (neopopsound.cpp)
- BIOS: koyote.bin (12 КБ) вшит в koyote_bin.h; `loadBIOS()` возвращает 0 — mem_init()
  строит таблицу векторов/BIOS-вызовов сам (ветка NGPC в memory.cpp)
- Большие буферы — в BSS: `mainrom[4MB]`, `mainram[224KB]`, `cpurom[256KB]`,
  `drawBuffer[SIZEX×152]`, `totalpalette[32768]` (~4.7 МБ суммарно)
- Рендер: `myGraphicsBlitLine()` в `graphicsBlitLine(160×152)` → drawBuffer (pitch SIZEX=320)
  → `blit_to_fb()` копирует 160×152 в левый верхний угол EMU_FB → emu_scale(160,152)
- Ввод: USB-клавиатура → ngpInputState (Up=0x01 Down=0x02 Left=0x04 Right=0x08
  A=0x10 (Z) B=0x20 (X) Select=0x40 (S) Start=0x80 (Enter)); читается в 0x6F82
- Палитра: totalpalette (RGB565) заполняется palette_init16(0xF800,0x07E0,0x001F) в graphics_init

### led.c (светодиоды)

- PA15 — «код жив» (мигает в emu_throttle, по таймеру кадров)
- PL10 — «обращение к SD» (led_sd_on/off в sd_read_sector)
- Активный уровень HIGH (проверено на железе: горит при DAT=1)
- R_PIO требует включения тактирования (PRCM) — PL10 может не заводиться из bare-metal,
  настраивается через U-Boot `gpio set PL10` в boot.scr

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
- UART-эхо экрана (текст DIP DOS в терминал, только изменившиеся строки, \r\n),
  команды PIN/APPS/HELP/EXIT; `PIN` — анимированный «взлом кода» в стиле Terminator 2
  (рендер `pofo_render_pin`: >TEST.0.0 → >ENTRY CODE + бегущий код → ACCESS DENIED)
- Вход с UART защищён от зависания: каждый `uart_getc` предваряется `uart_is_readable()`

## Управление

Полная карта — в `docs/CONTROLS.md`. Кратко:

| Клавиша | NES | A2600 | A5200 | A7800 | SMS/GG | Game Boy | Lynx | NGP | Mega Drive | Portfolio |
|---------|:---:|:-----:|:-----:|:-----:|:------:|:--------:|:----:|:---:|:----------:|:---------:|
| ↑ ↓ ← → | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | курсор/VK |
| Z | A | Fire | Fire | B1(A) | Button1 | B | A | **A** | **A** | буква Z |
| X | B | — | Pause | B2(B) | Button2 | A | B | **B** | **B** | буква X |
| C | — | — | — | — | — | — | — | — | **C** | — |
| A | — | — | — | — | — | — | — | — | **X** | — |
| S | Select | Select | Start | Select | **Pause** | Select | Opt1 | **Select** | **Y** | буква S |
| D | — | — | — | — | — | — | — | — | **Z** | — |
| Q | — | — | — | — | — | — | — | — | Mode | — |
| Enter | Start | Reset | Key3 | Start | Start | Start | Opt2 | **Start** | Start | Enter |
| Insert | — | — | — | — | — | — | — | — | — | VK (экранная клава) |
| ESC | Выход | Выход | Выход | Выход | Выход | Выход | Выход | Выход | Выход | удерж. ~1с — выход или EXIT |

## Настройки (Settings)

| # | Функция |
|---|---------|
| 1 | Создать папки ROM на SD (основная + alt_dir для NES/SMS) |
| 2 | Input Test (тест кнопок NES/A2600) |
| 3 | Video Mode / Throttle: 6 частот (60, 50, 45, 40, 35, 30 Hz), ←/→ или Enter — циклически |
| 4 | Atari 2600 Difficulty: Novice ⇄ Expert |
| 5 | Sega 6-button gamepad — тест скана геймпада (сырые чтения, биты, время; UART при смене) |
| 6 | ROM partition info (справка по разметке SD) |

## Ввод (usb_kbd.c / usb_ohci.c)

- USB-клавиатура (boot protocol), автоповтор; OHCI1/OHCI2 (два порта)
- `usb_input_poll()` — общий ввод меню: сначала клавиатура, затем Sega-геймпад
  (фронт нажатия: крестовина → стрелки, A/Start → Enter, B/Mode → ESC),
  затем тач-экран (если устройство энумерировано) по зонам: верх = Up, низ = Down,
  середина слева = ESC, справа = Enter; только фронт касания
- Тач (Waveshare GT911, VID 0EEF / PID 0005): парсер HID-пакета — Report ID 0x01,
  Status бит0 = нажатие, X/Y 16-бит Little-Endian, диапазон 0..4095 (матрица GT911);
  `usb_touch_poll()` в usb_kbd.c

## Sega-геймпад 6-button (sega_pad.c)

- `sega_pad_scan()` — классический протокол Sega 6-button через PCF8574@0x20
  (bit-bang I2C ~400 кГц, TWI0 PA11=SCL/PA12=SDA): 4 цикла SELECT, X/Y/Z/Mode
  читаются в Цикле 4 после 3 холостых циклов переключения TH. Подробно — в `docs/HARDWARE.md`
- Маска пада: UP=0x01 DOWN=0x02 LEFT=0x04 RIGHT=0x08 A=0x10 B=0x20 C=0x40
  START=0x80 X=0x100 Y=0x200 Z=0x400 MODE=0x800
- Статус: `sega_pad_get_status()` (SEGA_STATUS_ACK / SEGA_STATUS_PAD), время скана
  `sega_pad_get_scan_us()`, сырые чтения `sega_pad_get_raw()`
- Встроен в меню (`usb_input_poll`) и в хосты: GPGX, SNES, NES, GB, Lynx, NGP,
  A2600, A5200, A7800 (у систем мапятся только существующие кнопки; таблица в `docs/CONTROLS.md`)
- Atari Portfolio — клавиатурный компьютер, геймпад не подключается

## Читы (cheatdb.c / gp_cheats.c)

- `cheatdb.c/h` — менеджер: парсер `.cht` базы libretro (`/cheats/<система>/<ром>.cht`),
  регистронезависимый поиск по имени файла, включение/выключение читов, ручной ввод кода
- Меню читов — в `rom_browser.c`: клавиша **S** или геймпад **Mode** открывают
  (при входе — ожидание полного отпускания геймпада `usb_pad_wait_release()`);
  стрелки = выбор, Enter/A/Mode = вкл/выкл, C = все, X = нет, Start/Mode = запуск,
  ESC/Backspace = назад. Последней строкой — **Manual code entry** (ручной ввод),
  экран открывается всегда, даже если `.cht` для игры нет
- Применение по системам:
  - **GPGX (MD/SMS/GG)** — `gp_cheats.c`: декодеры Game Genie 8/16-бит + Action Replay;
    ROM-патчи через `z80_readmap` (переживают банкинг, `ROMCheatUpdate` вызывается ядром),
    RAM-патчи в `work_ram` раз в кадр (`RAMCheatUpdate`)
  - **NES (FCEUmm)** — `FCEUI_DecodeGG`/`DecodePAR` + `FCEUI_AddCheat`,
    `FCEU_ApplyPeriodicCheats` каждый кадр в ядре
  - **SNES (Snes9x)** — `S9xGameGenieToRaw`/`ProActionReplayToRaw` + `S9xAddCheat`,
    `Settings.ApplyCheats=true`
  - **Game Boy (binjgb)** — декодер Game Genie GB в `gameboy_host.cpp`
    (патч всех банков ROM напрямую, с compare-условием)
  - **RAW-читы `AAAA:VV[:CC]`** — универсальный формат (адрес:значение[:байт-условие])
    для систем без своего движка. Парсинг — `cheats_parse_raw`, применение каждый кадр:
    - **A2600 (MCUME)** — `theRam[addr & 0x7F]` (RIOT RAM 128 байт)
    - **A5200** — `memory[addr & 0xFFFF]` (RAM 64K)
    - **A7800** — `memory_ram[addr & 0x3FFF]` (RAM 16K)
    - **Lynx (Handy)** — `Peek_RAM`/`Poke_RAM` (RAM 64K)
    - **NGP/NGPC (RACE)** — `tlcsMemReadB`/`tlcsMemWriteB` (карта памяти TLCS-900H)

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
| 0x40000000 | Образ: .text → .ARM.exidx → .data → .bss (подряд, ALIGN(4)) |
| 0x425caa00 | `_bend1` / `_hend` — конец BSS = старт свободной памяти |
| 0x4F000000 | MENU_ARENA — буфер пунктов меню (512 слотов + имена) |
| 0x50000000 | Буфер загрузки ROM с SD (24 МБ) |
| 0x5F800000 | EMU_FB — общий кадровый буфер эмуляторов (320×240 RGB565) |
| 0x5F900000 | HDMI framebuffer (1024×600 XRGB8888) |
| 0x60000000 | Стек (конец 512 МБ DRAM, растёт вниз; сверху ничего нет) |

Жёстких адресов между секциями образа нет — `_hend` вычисляется линкером
сразу после `.bss` (см. `h3_bare/platform/linker.ld`).

## Загрузка

```
U-Boot SPL → U-Boot → (boot.scr: gpio-настройка светодиодов) → fatload mmc 0 0x40000000 h3_bare.bin → go 0x40000000
→ startup.S → SVC mode → BSS=0 → main()
```

Или через FEL: `sunxi-fel write 0x40000000 h3_bare.bin execute 0x40000000`

## Производительность

Оценка на A2600: ~8000 опкодов/кадр, ~192 строки рендера, ~2 мс блит на 1024×600.
Укладывается в 16.6 мс (60 FPS) с запасом >10×.

Более тяжёлые системы (A5200 — ANTIC 140K, A7800 — MARIA 55K вызовов/кадр,
Portfolio — 8088 интерпретация + LCD-рендер) могут быть в 5-10× тяжелее.
512 МБ DRAM + Cortex-A7 @ 1.2 ГГц — запас достаточен.