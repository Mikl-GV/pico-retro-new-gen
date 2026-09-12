# Загрузка на Orange Pi Lite

## Вариант 1 — SD-образ (самый простой)

1. Собрать образ (см. BUILD.md): `./build.sh sd`
2. Записать `build/h3_bare.img` на microSD через Rufus (Windows) или dd:

```bash
# Linux
sudo dd if=build/h3_bare.img of=/dev/sdX bs=1M conv=fsync status=progress
```

Где `/dev/sdX` — ваша SD-карта (проверьте `lsblk` перед записью!).

3. Вставить карту в Orange Pi Lite, включить питание.
4. U-Boot автоматически загрузит `h3_bare.bin` с FAT-раздела.

Структура SD после записи:
- SPL + U-Boot — в первых секторах (невидимо)
- FAT32-раздел `H3_RETRO` с файлами: `h3_bare.bin`, `boot.scr`

### Обновление прошивки

Не перезаписывайте образ заново — достаточно заменить `h3_bare.bin`
на FAT-разделе карты:

```bash
# Linux
mount /dev/sdX1 /mnt
cp build/h3_bare.bin /mnt/
sync
umount /mnt
```

Или в Windows открыть том `H3_RETRO` и скопировать новый файл.

## Вариант 2 — sunxi-fel (USB, без SD)

Подходит для разработки: не нужна SD-карта, бинарник загружается
напрямую по USB.

1. Установить sunxi-tools:

```bash
sudo apt install sunxi-tools
```

2. Отключить плату, зажать кнопку FEL (на Orange Pi Lite она совмещена с
кнопкой POWER — удерживать при подаче питания), подключить microUSB к ПК.

3. Загрузить прошивку:

```bash
sudo sunxi-fel write 0x40000000 build/h3_bare.bin execute 0x40000000
```

4. Плата сразу запустит эмулятор.

### FEL на Orange Pi Lite

Orange Pi Lite **не имеет отдельной кнопки FEL**. FEL-режим включается
так:
1. Отключить питание (кабель DC)
2. **microUSB подключён к ПК**
3. **Зажать кнопку POWER** (она же FEL) и подать питание DC
4. Держать кнопку ~2 секунды

Проверить, что плата в FEL:

```
sunxi-fel version
```

Если версия определилась — FEL работает.

## Вариант 3 — U-Boot с существующей SD

Если на карте уже есть Armbian (или другой Linux с U-Boot):

1. Скопировать `h3_bare.bin` на FAT-раздел (первый раздел карты)
2. Войти в U-Boot консоль (любой символ при загрузке)
3. Загрузить бинарник:

```
fatload mmc 0 0x42000000 h3_bare.bin
go 0x42000000
```

Можно записать скрипт `boot.cmd` в FAT для автоматической загрузки.

## UART для отладки

UART0 выведен на пины 16 (TX) и 17 (RX) 40-pin разъёма → FTDI/USB:

- TX: пин 16 (PA4)
- RX: пин 17 (PA5)
- Скорость: **115200 бод**, 8N1

При загрузке плата выводит отладочные сообщения и меню выбора платформы.