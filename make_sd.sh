#!/bin/bash
# make_sd.sh — подготовка SD-карты для pico-retro-new-gen V5.
# Формат: FAT32, директория /roms/ с подпапками a2600, a5200, a7800.
# Использование: sudo ./make_sd.sh /dev/sdX [путь_к_бинарнику]
set -euo pipefail

DEV="${1:-}"
BIN="${2:-build/h3_bare.bin}"
TOP="$(cd "$(dirname "$0")" && pwd)"

if [ -z "$DEV" ]; then
    echo "Использование: sudo $0 /dev/sdX [путь_к_бинарнику]"
    echo ""
    echo "Подготовит SD-карту для pico-retro-new-gen:"
    echo "  - U-Boot SPL + U-Boot в первых секторах"
    echo "  - FAT32 раздел H3_RETRO"
    echo "  - /roms/a2600/  /roms/a5200/  /roms/a7800/  -> скопировать туда ROM"
    echo "  - h3_bare.bin + boot.scr (автозагрузка)"
    echo ""
    echo "Пример: sudo $0 /dev/sdb"
    exit 1
fi

if [ "$EUID" -ne 0 ]; then echo "Нужен root"; exit 1; fi

# Проверка наличия бинарника
if [ ! -f "$BIN" ]; then
    echo "Бинарник не найден: $BIN"
    echo "Сначала собери: ./build.sh"
    exit 1
fi

echo "Запись на $DEV — данные будут уничтожены!"
read -p "Продолжить? (yes/no): " ans
if [ "$ans" != "yes" ]; then echo "Отменено"; exit 1; fi

# Собираем SD-образ
cd "$TOP"
./build.sh sd

# Запись образа
echo "Запись образа на $DEV..."
dd if=build/h3_bare.img of="$DEV" bs=1M conv=fsync status=progress

# Монтируем FAT-раздел
sleep 1
PART="${DEV}1"
if [ ! -b "$PART" ]; then PART="${DEV}p1"; fi
if [ ! -b "$PART" ]; then echo "Раздел не найден"; lsblk "$DEV"; exit 1; fi

mkdir -p /mnt/tmp_sd
mount "$PART" /mnt/tmp_sd

# Создаём /roms/ и подпапки
mkdir -p /mnt/tmp_sd/roms/a2600
mkdir -p /mnt/tmp_sd/roms/a5200
mkdir -p /mnt/tmp_sd/roms/a7800

# Копируем локальные ROM, если есть
if [ -d "$TOP/a2600_roms" ]; then
    cp -v "$TOP"/a2600_roms/* /mnt/tmp_sd/roms/a2600/ 2>/dev/null || true
fi
if [ -d "$TOP/a5200_roms" ]; then
    cp -v "$TOP"/a5200_roms/* /mnt/tmp_sd/roms/a5200/ 2>/dev/null || true
fi
if [ -d "$TOP/a7800_roms" ]; then
    cp -v "$TOP"/a7800_roms/* /mnt/tmp_sd/roms/a7800/ 2>/dev/null || true
fi

sync
umount /mnt/tmp_sd
rmdir /mnt/tmp_sd

echo "Готово. Вставь SD в Orange Pi Lite и включай."
echo ""
echo "Структура карты:"
echo "  /h3_bare.bin        — прошивка"
echo "  /boot.scr           — U-Boot скрипт"
echo "  /roms/a2600/        — ROM Atari 2600 (*.a26, *.bin)"
echo "  /roms/a5200/        — ROM Atari 5200 (*.a52, *.bin)"
echo "  /roms/a7800/        — ROM Atari 7800 (*.a78, *.bin)"