# План разработки (Roadmap)

Ретроконсоль pico-retro-new-gen (Orange Pi Lite, H3, bare-metal, без ОС).
Актуальный статус систем (источник истины) — `h3_bare/cores/systems.h`;
меню читает его напрямую. Таблица ниже — документирование.

## Статус систем

| Группа | Системы (READY) |
|--------|-----------------|
| Портативные | Game Boy / GBC (binjgb), Game Gear (GPGX), GBA (gpSP), Atari Lynx (Handy), Neo Geo Pocket / Color (RACE) |
| Консоли | Atari 2600 (MCUME), Atari 5200, Atari 7800 (ProSystem), Master System (GPGX), NES / Famicom (FCEUmm), ColecoVision (Gearcoleco), PC Engine / TurboGrafx (Beetle PCE Fast), SNES (Snes9x 2005), Mega Drive / Genesis (GPGX), GCE Vectrex (vecx) |
| Аркады (FinalBurn Neo) | CPS-1, CPS-2, NEOGEO / MVS, Sega System 16, Toaplan 1 (68K), Cave (68K), FB Neo (сводный) |
| Компьютеры | ZX Spectrum (Fuse), MSX / MSX2 (fMSX), БК-0010/0011М (libretro-bk), Atari Portfolio (Fake86), МС 1504 (Fake86) |

Планируются (PLANNED в systems.h): Радио-86РК, Amstrad CPC, Atari 8-bit
(400/800/XL/XE), Commodore 64, Enterprise 64/128.

Легенда: ✅ READY · 🔲 PLANNED

## Очередь работ

1. **Atari 8-bit (400/800/XL/XE)** — ядро libretro-atari800 компилируется
   (этап 1), host-включение в прошивку — этап 2 (в дереве `h3_bare/cores/atari800/`,
   в OBJ не входит).
2. **Радио-86РК, Amstrad CPC, C64, Enterprise** — порт ядер (кандидаты: rk86,
   cap32, frodo/vice, ep128emu), после Atari 8-bit.
3. **Vectrex** — реализован (host `vecx_host.c`, systems.h READY, звук на I2S
   r735); стендовое подтверждение картинки — у владельца.
4. **PC Engine** — HuCard готов; CD (заглушен), PSG-качество — по потребности.
5. **WiFi (RTL8189FTV)** — SDIO-стек + firmware + TCP/IP — отдельная большая задача (не начата).
6. **Звук**: все системы подключены к I2S (r735..r776); тонкая настройка АЧХ/гейна — по стенду.

## Отложено / перспективы

- **Читы** — отключены по решению владельца (r500); код-основа остаётся
  (`cheatdb.c`, `gp_cheats.c`). Включение — послойно, после стабилизации.
- **GBA** — интерпретатор gpSP; для полноценных 60 fps нужен dynarec (ARMv7 JIT)
  либо -O3 на gba_cpu.o; открытый вопрос производительности.
- **WiFi/ESP** через UART — альтернатива RTL8189FTV.
- **Тематические рамки (бордюры)** для систем — низкий приоритет.
- **БК-0010: оверлеи `.OVL`/`.GMS`** — эмуляция ленты отключена; нужна подгрузка
  образов ленты/диска.
- **Terak 8510/a (модель БК)** — консольный рендер (текст в порт 0177564/0177764),
  до доработки — чёрный экран.
- **Сейвы/EEPROM** — переживают ли перезагрузку: конфиг на SD (/retro.cfg уже есть
  для ремапа) — перспектива.

## Каркас (готово)

- Меню с группами (Portable/Consoles/Arcade/Computers/Other), прокрутка, статусы.
- Браузер ROM (FAT32), автозапуск по sys_id, удаление ROM с подтверждением.
- USB-клавиатура (boot), автоповтор; Sega-геймпад 6-button; кнопочный пад.
- HDMI 1024×600 @ 60 Гц; SPI TFT (справка/тач) на CPU1; UART 115200.
- Звук: I2S0 + CPU2-долив (см. docs/AUDIO_SUBSYSTEM_PLAN.md).