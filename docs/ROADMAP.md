# План разработки (Roadmap)

> Ретроконсоль pico-retro-new-gen (Orange Pi Lite, H3, bare-metal, без ОС).

## Статус систем

| # | Система | CPU | Статус | Ядро |
|--|---------|-----|--------|------|
| 1 | Atari 2600 | 6502 | ✅ работает | MCUME (Virtual VCS) |
| 2 | Atari 5200 | 6502 | ✅ работает | pico5200 (Atari800-derived) |
| 3 | Atari 7800 | 6502 | ✅ работает | ProSystem |
| 4 | NES / Famicom (Dendy) | 6502 | ✅ работает | InfoNES (140 мапперов) |
| 5 | Sega Master System / Game Gear | Z80 | ✅ работает | smsplus |
| 6 | ZX Spectrum | Z80 | 🔲 план | — |
| 7 | Game Boy / GBC | Z80 | 🔲 план | — |
| 8 | ColecoVision | Z80 | 🔲 план | — |
| 9 | MSX / MSX2 | Z80 | 🔲 план | — |
| 10 | Аркады (CPS-1/2, Neo Geo, Galaxian, Toaplan) | 68000/Z80 | 🔲 план | — |
| 11 | Sega Mega Drive | 68000 | 🔲 план | — |
| 12 | SNES | 65816 | 🔲 план | — |
| 13 | PC Engine | HuC6280 | 🔲 план | — |
| 14 | Atari Portfolio | 8088 | 🔲 план | — |
| 15 | Радио-86РК, БК-0010, MS 1504 | 8080 | 🔲 план | — |

**Легенда:** ✅ готово · 🚧 в работе · 🔲 в плане

## Очередь работ

1. **Atari Portfolio** — порт из V4, требует UART-терминал, виртуальную клавиатуру HD61830 LCD и 2 МБ данных
2. **Тач GT911** (USB) — interrupt-IN в OHCI, управление меню и эмулятором
3. **Z80-системы** (ZX Spectrum, Game Boy, Coleco, MSX) — на smsplus-референсах
4. **16-бит** — Mega Drive, PC Engine, SNES (на грани без JIT)
5. **WiFi (RTL8189FTV)** — SDIO-стек, firmware, TCP/IP — отдельная большая задача

## Каркас (готово)

- Меню с группами, прокрутка, сортировка, статусы READY/IN_PROGRESS/PLANNED
- Браузер ROM на SD (FAT32), автозапуск эмулятора по sys_id
- USB-клавиатура (boot protocol), автоповтор
- HDMI 1024×600 @ 60 Гц
- UART-отладка 115200 8N1
- 5 эмуляторов в одном бинаре (620 КБ)