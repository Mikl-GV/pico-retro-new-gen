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
| 11 | ColecoVision | Z80 | 🔲 план | — |
| 12 | ZX Spectrum | Z80 | 🔲 план | — |
| 13 | MSX / MSX2 | Z80 | 🔲 план | — |
| 14 | SNES | 65816 | 🔲 план | — |
| 15 | PC Engine | HuC6280 | 🔲 план | — |
| 16 | Аркады | 68000/Z80 | 🔲 план | — |
| 17 | GCE Vectrex | 6809 | 🔲 план | — |
| 18 | Atari Jaguar | 68000/JRISC | 🔲 план | — |
| 19 | Радио-86РК, БК-0010, MS 1504 | 8080 | 🔲 план | — |
| 20 | Game Boy Advance | ARM7TDMI | 🔲 план | gpSP (ядро в h3_bare/cores/gba_sp/, порт не доделан) |

**Легенда:** ✅ готово · 🚧 в работе · 🔲 в плане

> Важно: список и статусы систем живут в `h3_bare/cores/systems.h` — меню читает их оттуда. Таблица выше — только документирование.

## Очередь работ

1. **Game Boy Advance** — порт gpSP (ядро в h3_bare/cores/gba_sp/, host-слой написан, не собран)
2. **Atari Jaguar** — референс: virtualjaguar-libretro (нужен отбор только ядра, ~2MB, 68000+JRISC, тяжеловат)
3. **Тач-экран** (USB HID, VID 0EEF/PID 0005 — промежуточный MCU как мышь, не «чистый GT911») — interrupt-IN в OHCI, управление меню и эмулятором
4. **Z80-системы** (ZX Spectrum, Coleco, MSX) — ядро Genesis Plus GX (общий Z80+рендер)
5. **16-бит** — SNES, PC Engine (на грани без JIT)
6. **WiFi (RTL8189FTV)** — SDIO-стек, firmware, TCP/IP — отдельная большая задача

## Каркас (готово)

- Меню с группами (Portable/Consoles/Arcade/Computers/Other), прокрутка, сортировка, статусы READY/PLANNED
- Браузер ROM на SD (FAT32), автозапуск эмулятора по sys_id, удаление ROM
- USB-клавиатура (boot protocol), автоповтор, HID-раскладки
- HDMI 1024×600 @ 60 Гц
- UART-отладка 115200 8N1
- 7 эмуляторов в одном бинаре (~1.1 МБ)
- GPT/поддержка нескольких FAT-разделов, авто-поиск /roms