# pico-retro-new-gen

Bare-metal мультисистемный эмулятор на Orange Pi Lite (Allwinner H3, 512 МБ).

Без Linux, без ОС: один бинарник загружается U-Boot'ом. HDMI 1024×600,
USB-клавиатура, ROM — с SD-карты (FAT32).

**Работает сейчас:** Atari 2600 (MCUME), Atari 5200, Atari 7800,
NES / Famicom (InfoNES), Sega Master System / Game Gear (smsplus).

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
| **ESC** | Выход | Выход | Выход | Выход | Выход |

## Системы

| Группа | Системы |
|--------|---------|
| ✅ Готово | Atari 2600, Atari 5200, Atari 7800, NES / Famicom, Sega Master System / Game Gear |
| 🔲 План | GB/GBC, ColecoVision, PC Engine, SNES, Mega Drive, Vectrex, Jaguar, аркады, ZX Spectrum, MSX, Радио-86РК, БК-0010, MS 1504, Atari Portfolio |

ROM-файлы — в `/roms/<id>/` на SD. Поддержка: `.a26` (4K/8K/16K), `.a52`, `.a78`, `.nes` (iNES 1.0), `.sms`, `.gg`.

## Документация

- `docs/ARCHITECTURE.md` — устройство кода
- `docs/ROADMAP.md` — статус систем и план
- `docs/BUILD.md` — сборка
- `docs/HARDWARE.md` — железо