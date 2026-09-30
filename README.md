# pico-retro-new-gen

**Новая генерация V9** — bare-metal мультисистемный эмулятор на Orange Pi Lite
(Allwinner H3, 512 МБ), переписанный с нуля.

Без Linux, без ОС: один бинарник загружается U-Boot'ом. HDMI 1024×600,
USB-клавиатура, ROM — с SD-карты (FAT32). Ввод также с Sega-геймпада
6-button (PCF8574@0x20), читы — из базы libretro (`/cheats/`).

**Работает сейчас (27 систем со статусом READY в `systems.h`):** Atari 2600,
Atari 5200, Atari 7800, Atari Lynx, NES / Famicom (FCEUmm), Sega Master System /
Game Gear / Mega Drive (Genesis Plus GX), Game Boy / Game Boy Color,
Game Boy Advance (gpSP), Neo Geo Pocket / Color (RACE), SNES (Snes9x 2005),
MSX / MSX2 (fMSX, BIOS+SubROM вшиты; машина Yamaha YIS-503III),
ColecoVision (Gearcoleco), PC Engine / TurboGrafx (Beetle PCE Fast, HuCard),
ZX Spectrum (Fuse, 48K..TS2068), БК-0010/0011М (libretro-bk, ROM вшиты),
Atari Portfolio (8088, BIOS вшит), **МС 1504** (Fake86, BIOS PK300 вшит),
аркады на FinalBurn Neo: CPS-1, CPS-2, NEOGEO/MVS, Sega System 16,
Toaplan 1 (68K), Cave (68K), FB Neo (сводный).
GCE Vectrex числится READY, но **отложен** (изображение не собирается — см. ROADMAP).

## Сборка и запись

### Windows

> Официальная сборка — через **Makefile**: нужен ARM-тулчейн и make (MSYS2).

1. Скачай ARM-тулчейн для Windows: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
   (файл вида `gcc-arm-none-eabi-*-win32-x86_64.zip`) и распакуй; добавь `...\bin` в PATH.
2. Поставь make (например, MSYS2: `pacman -S make`).
3. Собери:
   ```bat
   make -j%NUMBER_OF_PROCESSORS%
   ```
   или через обёртку: `powershell -ExecutionPolicy Bypass -File .\build.ps1`
4. Готово: прошивка `h3_bare.bin` в корне проекта (и в `build/`).

### Linux / macOS

```bash
sudo apt install gcc-arm-none-eabi u-boot-tools mtools
make -j$(nproc)           # параллельная сборка -> h3_bare.bin в корне (и в build/)
make sd                   # -> build/h3_bare.img (полный SD-образ)
# старый способ: ./build.sh [sd]
```

Обновление прошивки на флешке с U-Boot (прошивка — в **корне проекта**):

```bash
sudo mount /dev/sdX1 /mnt
sudo cp h3_bare.bin /mnt/
sudo sync; sudo umount /mnt
```

## Управление

Подробная карта кнопок — в [`docs/CONTROLS.md`](docs/CONTROLS.md).

### Меню (глобальное)

| Действие | Клавиша |
|----------|---------|
| Вверх / Вниз | ↑ / ↓ (удержание = автоповтор) |
| Открыть систему / запустить ROM | Enter |
| Назад / выйти из эмулятора | ESC |
| Settings | активировать из меню → Enter |

### Эмуляторы (в игре)

| Клавиша | NES | A2600 (MCUME) | A5200 | A7800 | SMS/GG | Game Boy | GBA | Lynx | NGP | Mega Drive | Vectrex |
|---------|:---:|:-------------:|:-----:|:-----:|:------:|:--------:|:---:|:----:|:---:|:----------:|:-------:|
| ↑ / ↓ / ← / → | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | Стик |
| **Z** | A (огонь) | Fire | Fire | B1 (A) | Button 1 | **B** | **A** | A | **A** | **A** | Кноп. 1 |
| **X** | B | — | Pause | B2 (B) | Button 2 | **A** | **B** | B | **B** | **B** | Кноп. 2 |
| **C** | — | — | — | — | — | — | — | — | — | **C** | Кноп. 3 |
| **V** | — | — | — | — | — | — | — | — | — | — | Кноп. 4 |
| **A** | — | — | — | — | — | — | — | — | — | **X** | — |
| **S** | Select | Select | Start | Select | Pause | Select | Select | Option 1 | **Select** | **Y** | — |
| **D** | — | — | — | — | — | — | — | — | — | **Z** | — |
| **Q** | — | — | — | — | — | — | L | — | — | Mode | — |
| **W** | — | — | — | — | — | — | R | — | — | — | — |
| **Enter** | Start | Game Reset | Key 3 | Start | Start | Start | Start | Option 2 | **Start** | Start | — |
| **ESC** (удерж. ~1с) | Выход | Выход | Выход | Выход | Выход | Выход | Выход | Выход | Выход | Выход | Выход |

Vectrex: стик = 4 направления (аналог), кнопки 1/2/3/4 (дефолт: Z/X/C/V), можно перемапить в Settings → Keyboard remap.

### Atari Portfolio

Портативный «компьютер» DIP DOS — **полная клавиатура, без игрового маппинга**:
ввод с USB и UART, Insert — экранная клавиатура, ESC (удерж ~1с) или команда `EXIT` — выход в меню.

## Системы

| Группа | Системы |
|--------|---------|
| ✅ Готово | Портативные: Game Boy / GBC, Game Gear, GBA, Atari Lynx, Neo Geo Pocket / Color. Консоли: Atari 2600, Atari 5200, Atari 7800, Master System, NES / Famicom (Dendy), ColecoVision, PC Engine / TurboGrafx (HuCard), SNES, Mega Drive / Genesis. Аркады (FinalBurn Neo): FB Neo, CPS-1, CPS-2, NEOGEO / MVS, Sega System 16, Toaplan 1 (68K), Cave (68K). Компьютеры: ZX Spectrum (Fuse), MSX / MSX2 (fMSX, машина YIS-503III), БК-0010/0011М, Atari Portfolio, **МС 1504** |
| 🚧 Отложено | GCE Vectrex (статус READY, но картинка не собирается — см. ROADMAP) |
| 🔲 План | Компьютеры: **Радио-86РК, Amstrad CPC, Atari 8-bit (400/800/XL/XE), Commodore 64, Enterprise 64/128** (ядра см. в docs/ROADMAP.md) |

ROM-файлы — в `/roms/<id>/` на SD: `.a26`, `.a52`, `.a78`, `.nes`, `.sms`, `.gg`, `.gen`/`.md`, `.smc`/`.sfc`, `.gb`/`.gbc`, `.lnx`, `.ngp`/`.ngc`/`.npc`, `.gba`, `.rom`/`.mx1`/`.mx2` (MSX), `.vec` (Vectrex), `.col`/`.bin` (ColecoVision), `.pce`, `.z80`/`.sna`/`.tap`/`.tzx` (ZX Spectrum), `.bin`/`.img` (БК-0010), `.bin`/BIOS (МС 1504).
Аркады (CPS-1/2, NEOGEO, Toaplan, Cave, S16, FB Neo): папка с сырыми дампами чипов или `.zip` целого сета в `/roms/<cps1|cps2|neogeo|toaplan|cave|segasys|fbneo>/` — список игр см. в docs/FBNEO-ROMS.md.
Atari Portfolio — **builtin**, запускается из меню без ROM на SD.
MSX / MSX2 (Ямаха YIS-503III) — **builtin** (BIOS + SubROM вшиты): из меню выбор «Start BASIC» или «Load cartridge from SD» (`/roms/msx/`). MSX-DOS — через `MSXDOS2.ROM` в `/roms/msx/bios/`.
МС 1504 — **BIOS PK300 вшит**: из меню «Встроенное ПО» (биос) или «Загрузка программ (SD)» (`/roms/ms1504/`, файл ПЗУ ≤64 КБ).
ZX Spectrum (Fuse) — **BIOS вшит**: из меню выбор модели (12) → ROM-снапшот (`.z80`/`.sna` из `/roms/zxspectrum/`) или `BASIC`.
БК-0010/0011М — **ROM вшиты** (MONIT10/BASIC10/FOCAL10/DISK_327/B11M_*): из меню выбор модели (6) → ROM (`.bin`/`.img` из `/roms/bk0010/`) или `BASIC`. Модель «BK-0010» грузит **FOCAL**, «BK-0010.01» — **BASIC**. FOCAL/монитор — чёрно-белые.

## Ввод

- **USB-клавиатура** — меню и все эмуляторы (HID boot protocol)
- **Sega-геймпад 6-button** через PCF8574@0x20 (TWI0: PA11=SCL, PA12=SDA) — меню (D-Pad=A/Start/B/Mode) и все эмуляторы (Atari Portfolio — клавиатурный компьютер, геймпад не подключается)
- **8-битный кнопочный пад PCF8574@0x20** (r0.389): B0=Вверх B1=Влево B2=Вправо B3=Вниз B4=A B5=B B6=Start B7=Select/Coin(аркады); подключён ко всем эмуляторам и меню; в меню — A=выбор, B=назад, направление с автоповтором; выход из эмулятора — удержание Start ~1 с (карта и детали — docs/CONTROLS.md)
- Карта кнопок — в [`docs/CONTROLS.md`](docs/CONTROLS.md)

## Читы (отключены до доработки)

> ⚠️ Состояние по коду (r500): **читы выключены полностью** по решению владельца —
> меню читов и загрузчик базы `.cht` из `/cheats/` не реализованы; `cheats_reset()`
> вызывается в `emu_prepare()` (единая точка входа всех эмуляторов), поэтому список
> читов всегда пуст и применение в host-слоях — no-op. Повторное включение — послойно,
> после стабилизации задуманных эмуляторов (реестр: docs/AUDIT-2026-09-30.md, M14/M15).
> Код-основа осталась: RAW-читы и `cheats_*` API (`h3_bare/cores/cheatdb.c`,
> `gp_cheats.c`) — для будущей реализации.

## Прошивка

Сборка: `make -j16` (бинарник в корне `h3_bare.bin`, дублируется в `build/`).
Подробнее — docs/BUILD-LINUX.md и docs/BUILD.md. Запись на флешку:
`sudo mount /dev/sdX1 /mnt && sudo cp h3_bare.bin /mnt/`.

## Документация

- `docs/ARCHITECTURE.md` — устройство кода
- `docs/ROADMAP.md` — статус систем, план и перспективы
- `docs/CONTROLS.md` — управление (полная карта кнопок)
- `docs/BUILD.md` — сборка
- `docs/HARDWARE.md` — железо