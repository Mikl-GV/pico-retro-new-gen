# pico-retro-new-gen

**Новая генерация V9** — bare-metal мультисистемный эмулятор на Orange Pi Lite
(Allwinner H3, 512 МБ), переписанный с нуля.

Без Linux, без ОС: один бинарник загружается U-Boot'ом. HDMI 1024×600,
USB-клавиатура, ROM — с SD-карты (FAT32).

**Работает сейчас:** Atari 2600 (MCUME), Atari 5200, Atari 7800,
Atari Lynx (Handy), NES / Famicom (FCEUmm, 432 маппера, SuborKB-клавиатура),
Sega Master System / Game Gear, Sega Mega Drive / Genesis (Genesis Plus GX),
Game Boy / Game Boy Color (binjgb),
SNES / Super Famicom (Snes9x 2005),
Atari Portfolio (8088, BIOS вшит — без ROM на SD).

## Сборка и запись

```bash
sudo apt install gcc-arm-none-eabi u-boot-tools mtools
./build.sh                # -> h3_bare.bin в корне проекта (и в build/)
./build.sh sd             # -> build/h3_bare.img (полный SD-образ)
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

| Клавиша | NES | A2600 (MCUME) | A5200 | A7800 | SMS/GG | Game Boy | Lynx | Mega Drive |
|---------|:---:|:-------------:|:-----:|:-----:|:------:|:--------:|:----:|:----------:|
| ↑ / ↓ / ← / → | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad |
| **Z** | A (огонь) | Fire | Fire | B1 (A) | Button 1 | **B** | A | **A** |
| **X** | B | — | Pause | B2 (B) | Button 2 | **A** | B | **B** |
| **C** | — | — | — | — | — | — | — | **C** |
| **A** | — | — | — | — | — | — | — | **X** |
| **S** | Select | Select | Start | Select | Pause | Select | Option 1 | **Y** |
| **D** | — | — | — | — | — | — | — | **Z** |
| **Q** | — | — | — | — | — | — | — | Mode |
| **Enter** | Start | Game Reset | Key 3 | Start | Start | Start | Option 2 | Start |
| **ESC** (удерж. ~1с) | Выход | Выход | Выход | Выход | Выход | Выход | Выход | Выход |

### Atari Portfolio

Портативный «компьютер» DIP DOS — **полная клавиатура, без игрового маппинга**:
ввод с USB и UART, Insert — экранная клавиатура, ESC (удерж ~1с) или команда `EXIT` — выход в меню.

## Системы

| Группа | Системы |
|--------|---------|
| ✅ Готово | Atari 2600, Atari 5200, Atari 7800, Atari Lynx, NES / Famicom, Sega Master System, Sega Game Gear, Sega Mega Drive / Genesis, Game Boy / GBC, **SNES / Super Famicom**, Atari Portfolio |
| 🔲 План | ColecoVision, PC Engine, Vectrex, Jaguar, аркады, ZX Spectrum, MSX, Радио-86РК, БК-0010, MS 1504, Game Boy Advance |

ROM-файлы — в `/roms/<id>/` на SD. Поддержка: `.a26` (4K/8K/16K), `.a52`, `.a78`, `.nes` (iNES 1.0), `.sms`, `.gg`, `.gen`/`.md`, `.smc`/`.sfc`, `.gb`/`.gbc`, `.lnx`.
Atari Portfolio — **builtin**, запускается из меню без ROM на SD.

## Прошивка

После сборки (`./build.sh`) готовый бинарник лежит в **корне проекта** — `h3_bare.bin`
(дублируется в `build/`). Запись на флешку: `sudo mount /dev/sdX1 /mnt && sudo cp h3_bare.bin /mnt/`.

## Документация

- `docs/ARCHITECTURE.md` — устройство кода
- `docs/ROADMAP.md` — статус систем, план и перспективы
- `docs/CONTROLS.md` — управление (полная карта кнопок)
- `docs/BUILD.md` — сборка
- `docs/HARDWARE.md` — железо