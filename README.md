# pico-retro-new-gen

Bare-metal мультисистемный эмулятор на Orange Pi Lite (Allwinner H3, 512 МБ).

Без Linux, без ОС: один бинарник загружается U-Boot'ом. HDMI 1024×600,
USB-клавиатура, ROM — с SD-карты (FAT32).

**Работает сейчас:** Atari 2600 (MCUME), Atari 5200, Atari 7800,
NES / Famicom (InfoNES), Sega Master System / Game Gear (smsplus),
Atari Portfolio (8088, BIOS вшит — без ROM на SD).

## Сборка и запись

```bash
sudo apt install gcc-arm-none-eabi u-boot-tools mtools
./build.sh                # -> build/h3_bare.bin
./build.sh sd             # -> build/h3_bare.img (полный SD-образ)
```

Обновление прошивки на флешке с U-Boot:

```bash
sudo mount /dev/sdX1 /mnt
sudo cp build/h3_bare.bin /mnt/
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

| Клавиша | NES | A2600 (MCUME) | A5200 | A7800 | SMS/GG |
|---------|:---:|:-------------:|:-----:|:-----:|:------:|
| ↑ / ↓ / ← / → | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad |
| **Z** | A (огонь) | Fire | Fire | B1 (A) | Button 1 |
| **X** | B | — | Pause | B2 (B) | Button 2 |
| **S** | Select | Select | Start | Select | Pause |
| **Enter** | Start | Game Reset | Key 3 | Start | — |
| **ESC** (удерж. ~1с) | Выход | Выход | Выход | Выход | Выход |

### Atari Portfolio

Портативный «компьютер» DIP DOS — **полная клавиатура, без игрового маппинга**:
ввод с USB и UART, Insert — экранная клавиатура, ESC (удерж ~1с) или команда `EXIT` — выход в меню.

## Системы

| Группа | Системы |
|--------|---------|
| ✅ Готово | Atari 2600, Atari 5200, Atari 7800, NES / Famicom, Sega Master System / Game Gear, **Atari Portfolio** (BIOS вшит, без ROM) |
| 🔲 План | GB/GBC, ColecoVision, PC Engine, SNES, Mega Drive, Vectrex, Jaguar, аркады, ZX Spectrum, MSX, Радио-86РК, БК-0010, MS 1504 |

ROM-файлы — в `/roms/<id>/` на SD. Поддержка: `.a26` (4K/8K/16K), `.a52`, `.a78`, `.nes` (iNES 1.0), `.sms`, `.gg`.
Atari Portfolio — **builtin**, запускается из меню без ROM на SD.

## Документация

- `docs/ARCHITECTURE.md` — устройство кода
- `docs/ROADMAP.md` — статус систем и план
- `docs/CONTROLS.md` — управление (полная карта кнопок)
- `docs/BUILD.md` — сборка
- `docs/HARDWARE.md` — железо