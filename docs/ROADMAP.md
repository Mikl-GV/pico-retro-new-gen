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
5. **WiFi (RTL8189FTV)** — SDIO + firmware + TCP/IP + FTP реализованы (r795..r846);
   дальше — только стабильность (TLS/шифрование FTP не планируется).
6. **Звук**: вывод снят (r777); синтез в ядрах сохранён. Возврат — отдельная дельта:
   I2S0 + CPU2-долив (история — в git), затем ресемплеры/гейн по стенду.

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
- Звук: вывод снят (r777); синтез в ядрах сохранён (см. раздел «Звук» выше).

## Аркады (FBNeo): принципы и очередь

Вендор — `h3_bare/cores/cps1/` (подмножество FBNeo). Подключено: CPS-1, CPS-2,
NEOGEO / MVS, Sega System 16, Toaplan 1 (68K), Cave (68K), сводный FB Neo
(`/roms/fbneo`). Каталог ROM — `docs/FBNEO-ROMS.md`.

Паттерн порта семейства:

1. Файлы — `cores/cps1/src/burn/drv/<family>/` (выборочно).
2. Секция объектов в Makefile (`c1*_`) + `cps1_rename.sh` (коллизии m68k*/YM*).
3. Регистрация в `driverlist.h` + `pDriver[]`.
4. Host-ветка в `cps1_host.cpp`: `g_*` по `HARDWARE_PREFIX`, кадр «как железо»
   (rot=0), ввод через `toa_input_cache`.
5. `systems.h`/меню/справка/About/Arcade-guide.
6. Стенд: экран, ввод, звук (звук сейчас не выводится — r777).

Очередь:

| # | Семейство | CPU | Игры | Сложность |
|---|-----------|-----|------|:---------:|
| 1 | Taito 68K (F2/F3) | 68K(+Z80/M6805) | Bubble Bobble, Rainbow Islands, Arkanoid, Rastan | средняя |
| 2 | Data East 68K | 68K+Z80 | Robocop 1-2, Bad Dudes, Heavy Barrel, Karnov | средняя |
| 3 | Psikyo 68K | 68K | Strikers 1945, Gunbird | выше средней |
| 4 | Midway 68K | 68K | MK 1-3, NBA Jam | высокая (T-unit) |
| 5 | Konami/Namco/Capcom ранние | 6809/6502 | 1942, Commando, Galaga, R-Type | высокая (нужны CPU) |

Риски: защитные MCU (частично покрыто M6805), видео Midway T-unit/System 32,
объём вендора (только выборка файлов семейства).