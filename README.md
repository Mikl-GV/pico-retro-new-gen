# pico-retro-new-gen

**Новая генерация V9** — bare-metal мультисистемный эмулятор на Orange Pi Lite
(Allwinner H3, 512 МБ), переписанный с нуля.

Без Linux, без ОС: один бинарник загружается U-Boot'ом. HDMI 1024×600,
USB-клавиатура, ROM — с SD-карты (FAT32). Ввод также с Sega-геймпада
6-button (PCF8574@0x20), читы — из базы libretro (`/cheats/`).

**Работает сейчас:** Atari 2600 (MCUME), Atari 5200, Atari 7800,
Atari Lynx (Handy), NES / Famicom (FCEUmm, 432 маппера, SuborKB-клавиатура),
Sega Master System / Game Gear / Mega Drive (Genesis Plus GX),
Game Boy / Game Boy Color (binjgb), Game Boy Advance (gpSP),
Neo Geo Pocket / Pocket Color (RACE),
SNES / Super Famicom (Snes9x 2005),
Atari Portfolio (8088, BIOS вшит — без ROM на SD).

## Сборка и запись

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

| Клавиша | NES | A2600 (MCUME) | A5200 | A7800 | SMS/GG | Game Boy | GBA | Lynx | NGP | Mega Drive |
|---------|:---:|:-------------:|:-----:|:-----:|:------:|:--------:|:---:|:----:|:---:|:----------:|
| ↑ / ↓ / ← / → | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad |
| **Z** | A (огонь) | Fire | Fire | B1 (A) | Button 1 | **B** | **A** | A | **A** | **A** |
| **X** | B | — | Pause | B2 (B) | Button 2 | **A** | **B** | B | **B** | **B** |
| **C** | — | — | — | — | — | — | — | — | — | **C** |
| **A** | — | — | — | — | — | — | — | — | — | **X** |
| **S** | Select | Select | Start | Select | Pause | Select | Select | Option 1 | **Select** | **Y** |
| **D** | — | — | — | — | — | — | — | — | — | **Z** |
| **Q** | — | — | — | — | — | — | L | — | — | Mode |
| **W** | — | — | — | — | — | — | R | — | — | — |
| **Enter** | Start | Game Reset | Key 3 | Start | Start | Start | Start | Option 2 | **Start** | Start |
| **ESC** (удерж. ~1с) | Выход | Выход | Выход | Выход | Выход | Выход | Выход | Выход | Выход | Выход |

### Atari Portfolio

Портативный «компьютер» DIP DOS — **полная клавиатура, без игрового маппинга**:
ввод с USB и UART, Insert — экранная клавиатура, ESC (удерж ~1с) или команда `EXIT` — выход в меню.

## Системы

| Группа | Системы |
|--------|---------|
| ✅ Готово | Atari 2600, Atari 5200, Atari 7800, Atari Lynx, NES / Famicom, Sega Master System, Sega Game Gear, Sega Mega Drive / Genesis, Game Boy / GBC, **Game Boy Advance**, **Neo Geo Pocket / Color**, **SNES / Super Famicom**, Atari Portfolio, **MSX / MSX2 (Ямаха YIS-503II)** |
| 🔲 План | ColecoVision, PC Engine, Vectrex, Jaguar, аркады, ZX Spectrum, Радио-86РК, БК-0010, MS 1504 |

ROM-файлы — в `/roms/<id>/` на SD. Поддержка: `.a26` (4K/8K/16K), `.a52`, `.a78`, `.nes` (iNES 1.0), `.sms`, `.gg`, `.gen`/`.md`, `.smc`/`.sfc`, `.gb`/`.gbc`, `.lnx`, `.ngp`/`.ngc`/`.npc`, `.gba`, `.rom`/`.mx1`/`.mx2` (MSX).
Atari Portfolio — **builtin**, запускается из меню без ROM на SD.
MSX / MSX2 (Ямаха YIS-503II) — **builtin** (BIOS + BASIC вшиты): из меню выбор «Start BASIC» или «Load cartridge from SD» (`/roms/msx/`). MSX-DOS — через `MSXDOS2.ROM` в `/roms/msx/bios/`.

## Ввод

- **USB-клавиатура** — меню и все эмуляторы (HID boot protocol)
- **Sega-геймпад 6-button** через PCF8574@0x20 (TWI0: PA11=SCL, PA12=SDA) — меню (D-Pad=A/Start/B/Mode) и все эмуляторы (Atari Portfolio — клавиатурный компьютер, геймпад не подключается)
- Карта кнопок — в [`docs/CONTROLS.md`](docs/CONTROLS.md)

## Читы

- База читов — на SD в `/cheats/<система>/` (папка `cht` из libretro-database, имена папок как в базе)
- Файл чита ищется **по основному имени ROM без спецсимволов в скобках** — `[!]`, `(J)`, `(M4)`, `[T+Rus]` и т.п. отбрасываются, сравниваются базовые имена регистронезависимо
- В списке ROM: клавиша **S** или геймпад **Mode** → меню читов
  (стрелки = выбор, Enter/A/Mode = вкл/выкл, C = все, X = нет, Start/Mode = запуск с читами, ESC = назад)
- Меню читов всегда показывает строку **Manual code entry** — ручной ввод кода
  (экранный «как Game Genie»: крестовина выбирает слот/символ, A — вставить, или клавиатура)
- **Нативные движки**: MD/SMS/GG (Game Genie + Action Replay), NES (FCEUmm), SNES (Snes9x), Game Boy / GBC
- **RAW-читы `AAAA:VV[:CC]`** (адрес:значение, опционально байт-условие) работают во всех системах:
  A2600 (RIOT RAM), A5200 (RAM 64K), A7800 (RAM 16K), Lynx (RAM 64K), NGP/NGPC (карта памяти TLCS)
- Ручной ввод кода — через `cheats_manual_add` / `cheats_parse_raw`

## Прошивка

После сборки (`./build.sh`) готовый бинарник лежит в **корне проекта** — `h3_bare.bin`
(дублируется в `build/`). Запись на флешку: `sudo mount /dev/sdX1 /mnt && sudo cp h3_bare.bin /mnt/`.

## Документация

- `docs/ARCHITECTURE.md` — устройство кода
- `docs/ROADMAP.md` — статус систем, план и перспективы
- `docs/CONTROLS.md` — управление (полная карта кнопок)
- `docs/BUILD.md` — сборка
- `docs/HARDWARE.md` — железо