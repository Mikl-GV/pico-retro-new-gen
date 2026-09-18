# План разработки (Roadmap)

> Ретроконсоль pico-retro-new-gen (Orange Pi Lite, H3, bare-metal, без ОС).

## Статус систем

| # | Система | CPU | Статус | Ядро |
|--|---------|-----|--------|------|
| 1 | Atari 2600 | 6502 | ✅ работает | MCUME (Virtual VCS) |
| 2 | Atari 5200 | 6502 | ✅ работает | pico5200 (Atari800-derived) |
| 3 | Atari 7800 | 6502 | ✅ работает | ProSystem |
| 4 | NES / Famicom (Dendy) | 6502 | ✅ работает | FCEUmm (432 маппера, SuborKB) |
| 5 | Sega Master System | Z80 | ✅ работает | Genesis Plus GX |
| 6 | Game Boy / Game Boy Color | Z80 | ✅ работает | binjgb |
| 7 | Atari Portfolio | 8088 | ✅ работает | Fake86 (builtin, без ROM) |
| 8 | Game Gear | Z80 | ✅ работает | Genesis Plus GX |
| 9 | Sega Mega Drive / Genesis | 68000 | ✅ работает | Genesis Plus GX |
| 10 | Atari Lynx | 6502 | ✅ работает | Handy |
| 11 | Neo Geo Pocket / Pocket Color | TLCS900H+Z80 | ✅ работает | RACE |
| 12 | SNES | 65816 | ✅ работает | Snes9x 2005 (libretro) |
| 13 | ColecoVision | Z80 | 🔲 план | — |
| 14 | ZX Spectrum | Z80 | 🔲 план | — |
| 15 | MSX / MSX2 | Z80 | 🔲 план | — |
| 16 | PC Engine | HuC6280 | 🔲 план | — |
| 17 | Аркады | 68000/Z80 | 🔲 план | — |
| 18 | GCE Vectrex | 6809 | 🔲 план | — |
| 19 | Atari Jaguar | 68000/JRISC | 🔲 план | — |
| 20 | Радио-86РК, БК-0010, MS 1504 | 8080 | 🔲 план | — |
| 21 | Game Boy Advance | ARM7TDMI | 🔲 план | gpSP (ядро в h3_bare/cores/gba_sp/, порт не доделан) |

**Легенда:** ✅ готово · 🚧 в работе · 🔲 в плане

> Важно: список и статусы систем живут в `h3_bare/cores/systems.h` — меню читает их оттуда. Таблица выше — только документирование.

## Готовые механики (добавлено в этой серии)

- **Sega-геймпад 6-button** через PCF8574@0x20 (TWI0, PA11/PA12): бит-бэнг I2C ~400 кГц,
  8-шаговый классический протокол Sega (Цикл1 TH=1/TH=0 → 3 холостых цикла → Цикл4 TH=1
  для X/Y/Z/Mode), маска пада 16-бит (UP…MODE). Встроен в меню и во все эмуляторы,
  включая Atari 2600/5200/7800 (A=Fire, B=Pause, Start=Reset/Start, Mode=Select/Start);
  Atari Portfolio — клавиатурный компьютер, геймпад не подключается.
- **Читы (выбор из базы libretro + ручной ввод):** менеджер `cheatdb.c/h` (парсер `.cht`,
  поиск по имени ROM в `/cheats/<система>/`), в списке ROM: клавиша **S** или геймпад
  **Mode** открывают меню (в нём A/Mode — отметить, Start — запустить).
  Применение читов:
  - GPGX (MD/SMS/GG) — `gp_cheats.c` (декодеры Game Genie 8/16-бит, Action Replay;
    ROM-патчи через `z80_readmap`, RAM-патчи раз в кадр)
  - NES (FCEUmm) — `FCEUI_DecodeGG/PAR` + `FCEUI_AddCheat`
  - SNES (Snes9x) — `S9xGameGenieToRaw/ProActionReplayToRaw` + `S9xAddCheat`
  - Game Boy (binjgb) — декодер Game Genie GB в `gameboy_host.cpp` (патч всех банков ROM)

## Очередь работ

1. **Atari Jaguar** — референс: virtualjaguar-libretro (нужен отбор только ядра, ~2MB, 68000+JRISC, тяжеловат)
2. **Тач-экран** (USB HID, VID 0EEF/PID 0005 — промежуточный MCU как мышь, не «чистый GT911») — interrupt-IN в OHCI, управление меню и эмулятором
3. **Z80-системы** (ZX Spectrum, Coleco, MSX) — ядро Genesis Plus GX (общий Z80+рендер)
4. **PC Engine** — HuC6280, лёгкие ядра (mednafen_pce_fast?)
5. **WiFi (RTL8189FTV)** — SDIO-стек, firmware, TCP/IP — отдельная большая задача

## Перспективы (идеи, не начаты)

1. **WiFi на ESP8266/ESP32** — альтернатива RTL8189FTV: модуль ESP через UART/USB, AT-команды, без SDIO-стека
2. **RAM-патч движок читов для Atari 2600/5200/7800 + Lynx** — формат базы libretro: address/value/
   bit_position/repeat (это RAM-патчи, не коды Game Genie); применить к памяти каждого ядра
   (MCUME / Atari800 / ProSystem / Handy) раз в кадр, аналогично `RAMCheatUpdate` в GPGX
3. **Звук на TDA1378 (или TDA1543/DAC)** — сейчас звук заглушен во всех ядрах (нет DAC-вывода); нужен I2S/PWM-выход на усилитель
4. **Сохранение настроек в память** (throttle 50/60 Гц, сложность A2600) — сбрасываются при перезагрузке; вариант — конфиг-файл на SD или EEPROM-сектор
5. **Выбор «клавиатура или джойстик» для Dendy/NES в меню настроек** — для игр с SuborKB и без; переключатель, какой порт ввода активен
6. **Расширенные бордюры (рамки с паттернами/логотипами)** — сейчас простые цветные поля; в перспективе — тематические PNG-рамки для каждой системы

## Каркас (готово)

- Меню с группами (Portable/Consoles/Arcade/Computers/Other), прокрутка, сортировка, статусы READY/PLANNED
- Браузер ROM на SD (FAT32), автозапуск эмулятора по sys_id, удаление ROM (с двойным подтверждением), меню читов по S
- USB-клавиатура (boot protocol), автоповтор, HID-раскладки
- Sega-геймпад 6-button через PCF8574@0x20 (меню + игры)
- HDMI 1024×600 @ 60 Гц
- UART-отладка 115200 8N1
- 12 эмуляторов в одном бинаре (~3.5 МБ)
- GPT/поддержка нескольких FAT-разделов, авто-поиск /roms