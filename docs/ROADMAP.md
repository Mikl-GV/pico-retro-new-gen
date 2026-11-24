# План разработки (Roadmap)

Ретроконсоль pico-retro-new-gen (Orange Pi Lite, H3, bare-metal, без ОС).
Актуальный статус систем (источник истины) — `h3_bare/cores/systems.h`;
меню читает его напрямую. Таблица ниже — документирование.

## Статус систем

| Группа | Системы (READY) |
|--------|-----------------|
| Портативные | Game Boy / GBC (binjgb), Game Gear (GPGX), GBA (mGBA-core, r1133), Atari Lynx (Handy), Neo Geo Pocket / Color (RACE) |
| Консоли | Atari 2600 (Stella 2014), Atari 5200, Atari 7800 (ProSystem), Master System (GPGX), NES / Famicom (FCEUmm), ColecoVision (Gearcoleco), PC Engine / TurboGrafx (Beetle PCE Fast), SNES (Snes9x 2005), Mega Drive / Genesis (GPGX), Sega 32X (PicoDrive, r1147), GCE Vectrex (vecx) |
| Аркады (FinalBurn Neo) | CPS-1, CPS-2, NEOGEO / MVS, Sega System 16, Toaplan 1 (68K), Cave (68K), FB Neo (сводный) |
| Компьютеры | ZX Spectrum (Fuse), MSX / MSX2 (fMSX), Atari 8-bit (400/800/XL/XE, звук r975), Amstrad CPC (Caprice32), Commodore 64 (Frodo, r1284), БК-0010/0011М (libretro-bk), **Радио-86РК (порт RK8266, r1185)**, Atari Portfolio (Fake86), МС 1504 (Fake86), **Enterprise 64/128 (ep128emu, r1354: автозагрузка, мультифайловые игры)**, **Apple II+ (ядра POM2, r1388; образы .dsk/.do/.po/.nib/.woz, hot-swap по F12 r1396)** |

Планируются (PLANNED в systems.h): —

Легенда: ✅ READY · 🔲 PLANNED

## Очередь работ

1. _(Enterprise 64/128 реализован: ep128emu, BIOS вшиты, автозагрузка и игры из
   нескольких файлов — r1354.)_
   Радио-86РК (порт RK8266, r1185), Amstrad CPC (cap32, r970) и Commodore 64
   (frodo-libretro, r1284) — готовы.
2. **Vectrex** — реализован (host `vecx_host.c`, systems.h READY, звук на I2S
   r735); стендовое подтверждение картинки — у владельца.
3. **PC Engine** — HuCard готов; CD (заглушен), PSG-качество — по потребности.
4. **WiFi (RTL8189FTV)** — SDIO + firmware + TCP/IP + FTP реализованы (r795..r846);
   дальше — только стабильность (TLS/шифрование FTP не планируется).
5. **Звук**: вывод возвращён (r895..r902 — I2S0 + CPU2-долив, кольцо в SRAM A1
   r957); синтез в ядрах сохранён. Дальше — только гейн/ремендеры по стенду и CAPS.

## Отложено / перспективы

- **Читы** — отключены по решению владельца (r500); код-основа остаётся
  (`cheatdb.c`, `gp_cheats.c`). Включение — послойно, после стабилизации.
- **GBA** — mGBA-core (r1133, вместо удалённого gpSP r1167); звук 32768 Гц →
  ресемплер 48 кГц. Открытый вопрос — производительность на стенде (RnRR и др.),
  сравнивается с эталоном; при нехватке — dynarec ARMv7.
- **Sega 32X** — PicoDrive с DRC SH2 (r1144+), картриджный путь; CD/Mega-CD не
  развивается. На стенде проверяется темп/картинка/звук (r1173..r1178).
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
- Звук: вывод I2S0/CPU2 (48 кГц стерео, r895+); синтез в ядрах сохранён (см. раздел «Звук» выше).

## Аркады (FBNeo): принципы и очередь

Вендор — `h3_bare/cores/cps1/` (подмножество FBNeo). Подключено: CPS-1, CPS-2,
NEOGEO / MVS, Sega System 16, Toaplan 1 (68K), Cave (68K), **Namco** (r1610,
`/roms/namco`), **Taito** (r1623, `/roms/taito`: Rastan, Rainbow Islands,
Volfied, Bubble Bobble, Arkanoid), **Data East** (r1626, `/roms/dataeast`:
Robocop, Bad Dudes, Heavy Barrel, Karnov), **Psikyo PS3** (r1627,
`/roms/psikyo`: Strikers 1945, Tengai), сводный FB Neo (`/roms/fbneo`).
Каталог ROM — `docs/FBNEO-ROMS.md`.

Паттерн порта семейства:

1. Файлы — `cores/cps1/src/burn/drv/<family>/` (выборочно).
2. Секция объектов в Makefile (`c1*_`) + `cps1_rename.sh` (коллизии m68k*/YM*).
3. Регистрация в `driverlist.h` + `pDriver[]`.
4. Host-ветка в `cps1_host.cpp`: `g_*` по `HARDWARE_PREFIX`, кадр «как железо»
   (rot=0), ввод — name-based писатель ((`nam_write_input`, годится и для
   Namco, и для Taito), при необходимости — свой кэш/разбор `BurnDrvGetInputInfo`.
5. `systems.h`/меню/справка/About/Arcade-guide.
6. Стенд: экран, ввод, звук (звук выводится — I2S0/CPU2, r895+).

Очередь:

| # | Семейство | CPU | Игры | Сложность |
|---|-----------|-----|------|:---------:|
| ~~1~~ | ~~Taito 68K/Z80 классика~~ (сделано r1623) | 68K+Z80+uPD7810 | Bubble Bobble, Rainbow Islands, Arkanoid, Rastan, Volfied | средняя |
| ~~2~~ | ~~Data East 68K~~ (сделано r1626) | 68K+6502/h6280+mcs51 | Robocop, Bad Dudes, Heavy Barrel, Karnov | средняя |
| ~~3~~ | ~~Psikyo 68K (PS3)~~ (сделано r1627) | 68K+Z80 | Strikers 1945, Tengai (Gunbird — нужен сет) | средняя |
| 4 | Midway 68K | 68K | MK 1-3, NBA Jam | высокая (T-unit) |
| 5 | Taito F2/F3, Irem M72/M92, UPL | 68K(+MCU) | R-Type 1/2/Leo, Ninja Kids, Pbobble3 | средняя/высокая |
| 6 | Konami/Capcom ранние | 6809/6502 | 1942, Commando, R-Type (Capcom) | высокая (нужны CPU) |

Риски: защитные MCU (частично покрыто M6805), видео Midway T-unit/System 32,
объём вендора (только выборка файлов семейства).