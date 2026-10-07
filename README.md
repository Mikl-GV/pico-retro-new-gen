# pico-retro-new-gen

Bare-metal мультисистемный эмулятор для Orange Pi Lite (Allwinner H3, Cortex-A7,
512 МБ, без ОС). Один бинарник `h3_bare.bin` грузится U-Boot/FEL по адресу
0x40000000. HDMI 1024×600 (меню/игры, core0) + SPI TFT 480×320 (справка, тач —
дополнение, core1). ROM — с SD (FAT32), ввод — USB-клавиатура (OHCI) +
Sega-геймпад 6-button / кнопочный пад (PCF8574, I2C). Звук — I2S0 (MAX98357A,
48 кГц), долив на CPU2.

Версия прошивки: **r776** (`g_fw_version` и строка `build:` в `h3_bare/src/main.c`).

## Системы (статус из `h3_bare/cores/systems.h`)

| Группа | Системы (READY) |
|--------|-----------------|
| Портативные | Game Boy / GBC, Game Gear, GBA, Atari Lynx, Neo Geo Pocket / Color |
| Консоли | Atari 2600, Atari 5200, Atari 7800, Master System, NES / Famicom (Dendy), ColecoVision, PC Engine / TurboGrafx (HuCard), SNES, Mega Drive / Genesis, GCE Vectrex |
| Аркады (FinalBurn Neo) | CPS-1, CPS-2, NEOGEO / MVS, Sega System 16, Toaplan 1 (68K), Cave (68K), FB Neo (сводный) |
| Компьютеры | ZX Spectrum (Fuse), MSX / MSX2 (fMSX, BIOS вшиты), БК-0010/0011М (ROM вшиты), Atari Portfolio (BIOS вшит, builtin), МС 1504 (BIOS PK300 вшит) |

Планируются: Радио-86РК, Amstrad CPC, Atari 8-bit (400/800/XL/XE), Commodore 64,
Enterprise 64/128 (см. `docs/ROADMAP.md`).

## Сборка и запись

Зависимости (Linux): `gcc-arm-none-eabi`, `u-boot-tools` (mkimage), `mtools` (для `make sd`).

```bash
make -j$(nproc)          # -> h3_bare.bin в корне и build/
make sd                  # -> build/h3_bare.img (SD-образ)
./build.sh / build.ps1   # тонкие обёртки над make (Linux/Windows)
```

Правка любого `.h` → обязателен `make clean && make` (зависимости заголовков
не отслеживаются). Версия — `rNNN` в `h3_bare/src/main.c`, инкремент на каждую
компиляцию.

Запись на флешку (прошивка U-Boot читает `h3_bare.bin` с FAT-раздела):

```bash
sudo mount /dev/sdX1 /mnt && sudo cp h3_bare.bin /mnt/ && sudo umount /mnt
```

ROM — в `/roms/<id>/` на SD: `.a26`, `.a52`, `.a78`, `.nes`, `.sms`, `.gg`,
`.gen`/`.md`, `.smc`/`.sfc`, `.gb`/`.gbc`, `.lnx`, `.ngp`/`.ngc`, `.gba`,
`.rom`/`.mx1`/`.mx2` (MSX), `.vec`, `.col`/`.bin` (Coleco), `.pce`,
`.z80`/`.sna`/`.tap`/`.tzx` (ZX), `.bin`/`.img` (БК-0010/МС 1504).
Аркады — папка с сырыми дампами чипов или `.zip` сета в
`/roms/<cps1|cps2|neogeo|toaplan|cave|segasys|fbneo>/` (список игр — `docs/FBNEO-ROMS.md`).

## Управление

Меню: ↑/↓ — выбор, Enter — открыть, ESC — назад. В эмуляторе: ESC (удержание ~1 с)
или Start+Select (геймпад) — выход. Полная карта кнопок — `docs/CONTROLS.md`.

## Ввод

- USB-клавиатура (HID boot) — меню и все эмуляторы.
- Sega-геймпад 6-button через PCF8574@0x20 (бит-бэнг I2C на PG9/PG8) — меню и игры.
- Кнопочный пад PCF8574@0x20/0x27 — B0..B7 (Up/Left/Right/Down/A/B/Start/Select).
- Ремап клавиатуры — Settings → Keyboard remap (`/retro.cfg` на SD).

## Читы

**Отключены** по решению владельца (r500): `cheats_reset()` в `emu_prepare()`
гарантирует пустой список; меню читов и загрузчик базы `.cht` не реализованы.
Код-основа (декодеры GG/AR, `cheatdb.c`, `gp_cheats.c`) остаётся для будущей
реализации. Включать частично нельзя.

## Документация

- `docs/HANDOVER.md` — точка сохранения и история дельт (r500..r776)
- `docs/ARCHITECTURE.md` — устройство кода, карта памяти
- `docs/ROADMAP.md` — статус систем и план
- `docs/CONTROLS.md` — полная карта управления
- `docs/BUILD.md` — сборка (Linux/Windows)
- `docs/HARDWARE.md` — железо, распиновка
- `docs/FBNEO-ROMS.md` — каталог аркадных ROM