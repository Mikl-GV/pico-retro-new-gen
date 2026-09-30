# Архитектура

Bare-metal мультисистемный эмулятор для Allwinner H3 (Orange Pi Lite).
Без ОС, без фреймворков. HDMI-стек — из `lib-h3` (MIT).

## Слои

```
┌───────────────────────────────────────────────────────────────┐
│                         main.c                                │
│          меню → браузер ROM → диспетчер эмуляторов            │
│   (a2600/…/sms/gameboy/…/zxspectrum/bk0010/model-диалоги)      │
├───────────────────────────────────────────────────────────────┤
│  mcume/    a5200/    a7800/    fceumm/    gpgx/     portfolio/│
│  (A2600)   (A5200)   (A7800)   (NES)   (MD+SMS)      (8088)   │
│  gameboy/ (binjgb)   lynx/ (Handy)   snes/ (Snes9x 2005)      │
│  ngp/ (RACE)  vecx/ (Vectrex, отложен)  gearcoleco/ (Coleco)  │
│  msx/ (fMSX)  fuse/ (ZX Spectrum)  pce_fast/ (PC Engine)      │
│  bk/ (BK-0010/0011M)                                          │
├───────────────────────────────────────────────────────────────┤
│  host-слои: system_atari_h3.cpp  system_a5200_h3.cpp          │
│  system_a7800_h3.cpp  nes_host_fceumm.cpp  system_gpgx_h3.c   │
│  gameboy_host.cpp  lynx_host.cpp  snes_host.cpp  ngp_host.cpp │
│  vecx_host.c (Vectrex)  gba_host.c (GBA)  msx_host.c (MSX)    │
│  coleco_host.cpp  fuse_host.c  pce_host.c  bk_host.c          │
│  portfolio/system_portfolio.cpp (+ pofo_compat_h3.h)          │
├───────────────────────────────────────────────────────────────┤
│  emu.c (циклы + emu_scale)  menu.c  rom_browser.c  settings.c │
│  usb_kbd.c  usb_ohci.c  sd.c  fat.c  led.c  fb_text.c         │
│  uart.c  printf.c  libc_min.c  cxx_runtime.cpp                │
│  cheatdb.c (чит-менеджер, парсер .cht)  gp_cheats.c (GPGX)    │
│  sega_pad.c (Sega 6-btn геймпад, PCF8574)  i2s.c (звук, off)  │
├───────────────────────────────────────────────────────────────┤
│  CPU1 (tft_drv.c): SPI0 ILI9486 480×320 + тач TSC2046I (PA21) │
│  зв′язь — SRAM A1 (0x34..0x70), heartbeat — PL10              │
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
| MSX / MSX2 (YIS-503III) | **fMSX 6.0** | C | 256×212 → V9938 → EMU_FB | usb_kbd_get_raw + sega_pad |
| GCE Vectrex | **libretro-vecx** | C | 330×410 (векторы → растр) → прямая запись в HDMI FB (не EMU_FB) | usb_kbd_get_raw + sega_pad |
| ColecoVision | **Gearcoleco** | C++ | 256×192 → g_col_fb → построчно → EMU_FB | usb_kbd_get_raw + sega_pad (full keypad) |
| ZX Spectrum | **Fuse (libretro)** | C | 320×240 → EMU_FB (fuse_host.c) | usb_kbd + sega_pad (порт 2 клавиатуры) |
| PC Engine / TG | **Beetle PCE Fast** | C | 256×240 → EMU_FB (pce_host.c) | usb_kbd + sega_pad |
| БК-0010/0011М | **BK-Terak-Emu (libretro)** | C | 512×512 (кадр 256×256 content) → EMU_FB (bk_host.c) | usb_kbd (полная клава) + sega_pad |

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
- Менеджер памяти — bump-аллокатор `gb_heap` (12 МБ) в gameboy_stubs.c, `gb_heap_reset()`
  находится в linker.ld (резерв _gb_heap_start/end между BSS и _hend).
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

### msx_host.c (MSX / MSX2, fMSX 6.0)

- Ядро: `msx/` (fMSX 6.0: fMSX/, Z80/, EMULib/, NukeYKT/ — C-код, C99)
- Host: `msx_host.c` — кадровый цикл RunZ80, рендер V9938 (TEXT80 / SCREEN6-7, PAL 50 Гц) → EMU_FB
- Ввод: USB-клавиатура + Sega-геймпад → KeyState; запуск «BASIC / Load cartridge» из main.c
- `msx_compat.c` — стабы rf*/sscanf/time поверх вшитых BIOS и FAT SD; BIOS MSX2.ROM + MSX2EXT.ROM вшиты
- objcopy-переименование конфликтующих символов (MSX_RENAME) — изоляция от CPU/RAM/rf* других ядер
- _sbrk — рабочий bump-аллокатор (libc_min.c, от _hend)

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

### coleco_host.cpp (ColecoVision, Gearcoleco)

- Ядро Gearcoleco (Ignacio Sanchez), C++, сборка с `-DGEARCOLECO_DISABLE_DISASSEMBLER`
  (дизассемблер выделяет record на каждую инструкцию из bump-пула — в играх не нужен).
- OS-7 BIOS вшит (`coleco_bios_data`, 8 КБ, CRC32 0x3AA93EF3 из bios_data.S, .bin в .gitignore).
  Порядок загрузки как в эталоне libretro (`load_colecovision_firmware`): **сначала
  `GetMemory()->LoadBiosFromBuffer()`, затем `LoadAdamFirmware(GC_ADAM_FIRMWARE_OS7)`** —
  без BIOS в Memory `IsBiosLoaded()=false` и машина никогда «ready».
- Рендер: ядро рисует 256×192 подряд (pitch 256) в свой буфер `g_col_fb[256×192]`,
  host построчно (`memcpy` ×192) переносит в EMU_FB (pitch 320) → `emu_scale(256,192)`.
  (Прямая запись в EMU_FB давала «дубль со сдвигом» из-за разницы pitch.)
- Ввод: D-Pad + Fire1(левая)/Fire2(правая) + **полный keypad**: цифры 1..9,0, `*`, `#`.
  Клавиатура: 1..9,0=keypad, Q=`*`, W=`#`, Z=Fire1, X=Fire2, Enter=Start(keypad8), S=`#`.
  Sega-пад: крестовина=D-Pad, A=Fire1, B=Fire2, Start=keypad8, Mode=`#`.
  Передача в ядро — `KeyPressed/KeyReleased` по факту (keypad независим от джойстика:
  `Input::KeyPressed`, key>0x0F → m_Gamepad, key<=0x0F → m_KeypadState).
- Конфликт blargg с Lynx (r0.193): обе системы делят blip-код; `gc_rename.sh` добавляет
  суффикс `_gc` всем blargg-символам в gc-объектах по ЖЁСТКОМУ списку токенов
  (Blip_Buffer/Blip_Synth/Effects_Buffer/Multi_Buffer/Stereo_Buffer/Silent_Blip_Buffer)
  и читает символы через `$NF` (у U-символов нет колонки адреса). Иначе `gc_Sms_Apu.o`
  линковался с Lynx-версией Blip_Buffer → рассинхрон → Data Abort в `new Sms_Apu()`.
- Звук: `RunToVBlank(..., NULL, NULL)` — audio-буфер не выводится (нет DAC), но `Audio::Init`
  инициализирует blargg-цепочку нормально; `Audio::EndFrame` имеет NULL-guard.

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

### led.c (светодиоды) — роли с r155

- **PL10** — зелёный, «проц жив»: мигает 0.5/0.5 с с **CPU1** (`led_heartbeat_cpu1`).
  Вынесен на R_PIO специально — CPU1 не трогает PA_DAT (меньше RMW-гонки с падом/тачем).
- **PA15** — красный, «обращение к SD» (`led_sd_on/off` в sd.c). Живёт на PA_DAT —
  редкие короткие всплески, RMW-гонка возможна (см. sega_pad.c / tft_drv.c).
- Активный уровень HIGH (проверено на железе: горит при DAT=1)
- R_PIO требует включения тактирования (PRCM) — настраивается через U-Boot `gpio set PL10`
  в boot.scr (или остаётся в функции 7)

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
| 1 | Создать папки ROM на SD (основная + alt_dir для NES/SMS, двойное подтверждение) |
| 2 | Input Test (тест кнопок NES/A2600) |
| 3 | Atari 2600 Difficulty: Novice ⇄ Expert (Enter или ←/→) |
| 4 | Sega 6-button gamepad — тест скана геймпада (сырые чтения, биты, время; UART при смене) |
| 5 | Keyboard remap (по системам, `/retro.cfg`) |
| 6 | Touch Calibration (TFT, тач TSC2046I) |
| 7 | ROM partition info (справка по разметке SD) |

> Порядок пунктов совпадает с TFT-меню (`tft_drv.c`) — при изменении править оба.
> Настройки частоты кадра **убраны** (r158): эмуляторы всегда 60 Гц.

## Ввод (usb_kbd.c / usb_ohci.c)

- USB-клавиатура (boot protocol), автоповтор; OHCI1/OHCI2 (два порта)
- `usb_input_poll()` — общий ввод меню. **Клавиатура и Sega-геймпад равноправны (r155)**:
  фронт джоя обрабатывается всегда, одновременное клавиатурное событие не теряется
  (`g_kbd_saved`), затем тач-экран (если USB-устройство энумерировано) по зонам:
  верх = Up, низ = Down, середина слева = ESC, справа = Enter; только фронт касания
- USB-тач (Waveshare GT911, VID 0EEF / PID 0005): парсер HID-пакета — Report ID 0x01,
  Status бит0 = нажатие, X/Y 16-бит Little-Endian, диапазон 0..4095; `usb_touch_poll()`
- **SPI-тач TFT TSC2046I** — на CPU1 (tft_drv.c): протокол XPT2046 (Mode 1 только),
  CS=PA21, калибровка 5 мишеней. Подробно в `docs/HARDWARE.md`

## Sega-геймпад 6-button (sega_pad.c)

> **⚠️ НЕ ЛАЗИТЬ без стенда** — подробно в начале `sega_pad.c`. Тайминги и фазы
> подобраны эмпирически; изменения ломают детект 6-btn. Проверка любого изменения —
> Settings → Sega 6-button test (C3: Z/Y/X/Mode при отпущенной крестовине).

- `sega_pad_scan()` — классический протокол Sega 6-button через PCF8574@0x20
  (bit-bang I2C ~370 кГц, TWI0 PA11=SCL/PA12=SDA): 8 уровней TH, старшие кнопки
  в ур.7 после маркера 6-btn (ур.6), скан ~1.4 мс < окна чипа 1.6 мс, idle TH=1.
  **Clash-защита линий D0-D3 (r157)**: старшая кнопка выдаётся только если линия
  не занята крестовиной (иначе «вправо» давало «фантом Mode/Select»).
- Маска пада: UP=0x01 DOWN=0x02 LEFT=0x04 RIGHT=0x08 A=0x10 B=0x20 C=0x40
  START=0x80 X=0x100 Y=0x200 Z=0x400 MODE=0x800
- **Два режима использования**: меню — слой `usb_pad_update` (кэш 12 мс + антидребезг
  3 скана); игры — прямой `sega_pad_scan()` раз в кадр из host-слоёв.
- **Re-init пада** на входе/выходе эмулятора (`sega_pad_init`) + **выход Start+Mode
  ~0.9 с** (armed + sticky в emu.c).
- **Пауза = Start, а не Mode** (r155): у всех систем правит правило маппингов в docs/CONTROLS.md.
- Встроен в меню (`usb_input_poll`) и во все хосты; Atari Portfolio — клавиатурный
  компьютер, геймпад не подключается.
- Статус: `sega_pad_get_status()` (ACK/PAD/PAD6), `sega_pad_get_scan_us()`, `sega_pad_get_raw()`

## Читы (cheatdb.c / gp_cheats.c)

> r0.181: **UI-точка входа читов убрана** (меню по S/Mode в `rom_browser.c`
> удалено). Модули cheatdb/gp_cheats и применение в ядрах остаются в коде
> (пользователь отказался от функции UI, модули не вычищены).

- `cheatdb.c/h` — менеджер: парсер `.cht` базы libretro (`/cheats/<система>/<ром>.cht`),
  регистронезависимый поиск по имени файла, включение/выключение читов, ручной ввод кода
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

## Карта памяти (r0.416, точные адреса из `nm build/h3_bare.elf`)

| Адрес | Назначение |
|-------|------------|
| 0x00000000..0x00006000 | **SRAM A1** (24 КБ, некэш. для обоих ядер): one-shot-гейт абортов `0x18`; SMP-почта `0x20` (magic CPU1) / статус CPU1 `0x24`; пробы `0x28..0x30`; SRAM-почта калибровки/кнопок/настроек `0x34..0x8C`; A2600 diff `0x70`; флаг «игра активна» `0x74` (TFT frozen) |
| 0x40000000 | Образ (подряд): `.text` → `.init_array` → `.rodata` → `.ARM.extab/.exidx` → `.data` → `.bss` |
| 0x40000000..0x4073F0C0 | `.text` + init_array (код ≈ 7.4 МБ, r0.416); векторы `_vectors`=0x40000000, `_prefetch`=0x40000084, `_dataabort`=0x40000164, `_start`=0x400002F8 |
| 0x4073F0C0..0x40A37588 | `.rodata` (≈ 2.9 МБ) |
| 0x40A37588..0x40CE4C00 | `.data` (≈ 2.7 МБ, копируется из образа) |
| 0x40CE4C00..0x4AC0071E | `.bss` (`_bstart1.._bend1`, ≈ 140 МБ, обнуляется в `startup.S`) |
| 0x49391DA0..0x4AB91DA0 | `_gb_heap_start.._gb_heap_end` — bump-пул кучи **~135 МБ** (все ядра; `malloc/free` из `gameboy_stubs.c` в том же пуле) |
| 0x4AB91DA0 | `_hend` — конец кучи; `_sbrk`-арена растёт вверх, лимит `SBRK_LIMIT=0x4F000000` |
| 0x4AC00000..0x4AC00720 | `.libh3_coherent` (резерв 1 МБ, **uncached**): OHCI ED/TD/HCCA, USB-отчёты, `g_ts_*`/`g_cal_*`. Начало помечается `mmu_mark_uncached(libh3_coherent_region)` — **символ линкера, а не хардкод** (адрес уезжает при росте образа) |
| 0x4F000000 | `_menu_arena` (512 слотов + имена); граница `_sbrk` |
| 0x50000000..0x51800000 | `ROM_BUF` — буфер загрузки ROM с SD (24 МБ) |
| 0x5F800000..0x5F825800 | `EMU_FB` — общий буфер эмуляторов (320×240 RGB565) |
| 0x5F900000..0x5FB58000 | HDMI framebuffer (1024×600 XRGB8888) |
| 0x5FDFD000..0x5FE01000 | стеки CPU1 (исключения + SVC, TFT-ядро) |
| 0x5FF00000..0x5FF03000 | стеки исключений core0 |
| 0x60000000 | SVC-стек core0 (растёт вниз) |

Пересечений нет. `_hend` вычисляется линкером сразу после резерва кучи
(см. `h3_bare/platform/linker.ld`); `.coherent`, `_menu_arena` и framebuffer'ы —
фиксированные адреса вне образа. Проверка адресов: `nm build/h3_bare.elf`,
`arm-none-eabi-size build/h3_bare.elf`, `arm-none-eabi-readelf -lW`.

**Числа-размеры (r0.416, `arm-none-eabi-readelf -SW`):** `.text` 0x73F0B0 (≈7.4 МБ),
`.rodata` 0x2F4C20 (≈2.9 МБ), `.data` 0x2AD65C (≈2.7 МБ), `.bss` 0x86AD188 (≈140 МБ),
образ `h3_bare.bin` 13 519 844 Б.

## Загрузочная карта памяти (r0.416)

### Последовательность запуска

```
SD/MMC: U-Boot SPL -> U-Boot (SPL в SRAM, U-Boot в DRAM)
-> U-Boot: gpio-настройка светодиодов, `fatload mmc 0 0x40000000 h3_bare.bin` -> `go 0x40000000` (ARM state)
   (или FEL: `sunxi-fel write 0x40000000 h3_bare.bin execute 0x40000000`)
-> startup.S @ 0x40000000: SVC mode; таблица векторов (VBAR=0x40000000);
   BSS=0 (_bstart1.._bend1, ≈140 МБ); .coherent NOLOAD-зона; MMU + mmu_mark_uncached(libh3_coherent_region);
   UART0 115200, LED, main()
-> main(): USB/OHCI/TFT/SD-инициализация -> меню -> эмулятор
```

### Что где лежит при загрузке/работе

| Фаза | Адрес | Что |
|---|---|---|
| U-Boot SPL | SRAM 0x0000xxxx | минимальный загрузчик из MBR SD |
| U-Boot | DRAM (низкие адреса, вне образа) | fatload/go, потом НЕ трогаем |
| Образ | 0x40000000..0x40005400(+размер) | h3_bare.bin целиком: .text/.rodata/.data (BSS — NOBITS, не в файле) |
| Векторы CPU | 0x40000000 (VBAR) | startup.S, хранится в .text |
| SRAM A1 | 0x00000000..0x00006000 | межъядерная почта: гейт `0x18`, SMP `0x20/0x24`, пробы `0x28..0x30`, настройки `0x34..0x8C`, diff `0x70`, флаг «игра» `0x74` |
| Куча эмуляторов | 0x49391DA0..0x4AB91DA0 | bump ≈135 МБ (`_gb_heap_start.._gb_heap_end`); маллок всех ядер |
| sbrk-арена | 0x4AB91DA0..0x4F000000 | растёт вверх от `_hend` (системные вызовы smalloc) |
| coherent (uncached) | 0x4AC00000 (резерв 1 МБ) | OHCI ED/TD/HCCA, USB-отчёты клавиатуры/тача, `g_ts_*`/`g_cal_*` — DMA-буферы, недоступные кэшу |
| Меню | 0x4F000000 | `_menu_arena` |
| SAT ROM | 0x50000000..0x51800000 | `ROM_BUF` — образы ROM с SD (24 МБ) |
| Видео | 0x5F800000 (EMU_FB 150 КБ), 0x5F900000 (HDMI 2.4 МБ) | кадр эмулятора 320×240 RGB565 -> апскейл 1024×600 XRGB8888 |
| Стеки | 0x5FDFD000 (CPU1), 0x5FF00000 (core0 exc), 0x60000000 (SVC core0 вниз) | — |

### Почему так

- Образ начинается с 0x40000000 — это адрес, куда U-Boot/FEL прыгает `go`, и
  одновременно адрес векторов (VBAR); startup.S сразу ставит SVC и таблицу векторов.
- BSS/куча/коварент уезжают вверх при росте кода — линкер считает `_gb_heap_start/end`
  и `libh3_coherent_region` заново; коварент помечается MMU uncached по **символу**,
  а не хардкоду (см. `src/main.c: mmu_mark_uncached`).
- Фреймбуферы/стеки/ROM_BUF — фиксированные адреса в верхнем DRAM, вне образа,
  чтобы рост кода их не задевал.

## Загрузка

```
U-Boot SPL → U-Boot → (boot.scr: gpio-настройка светодиодов) → fatload mmc 0 0x40000000 h3_bare.bin → go 0x40000000
→ startup.S → SVC mode → BSS=0 → main()
```

Или через FEL: `sunxi-fel write 0x40000000 h3_bare.bin execute 0x40000000`

## Производительность

A2600 (MCUME): `mainloop` — 7600 инструкций, с r157 останавливается на границе
кадра (`tv_draw_count`); на железе run/s=60, sim/s=60, кадр ≈ 2–5.8 мс —
запас до 16.6 мс. Более тяжёлые системы (A5200 — ANTIC 140K, A7800 — MARIA 55K
вызовов/кадр, Portfolio — 8088 интерпретация + LCD-рендер) тяжелее, но укладываются.
512 МБ DRAM + Cortex-A7 @ 1.2 ГГц — запас достаточен.