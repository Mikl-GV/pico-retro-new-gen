#!/bin/bash
# build.sh — сборка v7 (каркас: меню + браузер ROM).
# Использование: ./build.sh [clean|fel|sd]
set -euo pipefail

TOP="$(cd "$(dirname "$0")" && pwd)"
BUILD="$TOP/build"
BIN="$BUILD/h3_bare.bin"
IMG="$BUILD/h3_bare.img"

# ---- Определяем тулчейн ----
if command -v arm-none-eabi-gcc &>/dev/null; then
    PREFIX="arm-none-eabi-"
elif command -v arm-linux-gnueabihf-gcc &>/dev/null; then
    PREFIX="arm-linux-gnueabihf-"
else
    echo "ERROR: не найден arm-none-eabi-gcc или arm-linux-gnueabihf-gcc"
    echo "Установи: sudo apt install gcc-arm-none-eabi (или gcc-arm-linux-gnueabihf)"
    exit 1
fi
echo "Тулчейн: $PREFIX"

# ---- Сборка .bin ----
if [ "${1:-}" = "clean" ]; then
    rm -rf "$BUILD"
    echo "Очищено."
    exit 0
fi
mkdir -p "$BUILD"
CC="${PREFIX}gcc"
AS="${PREFIX}gcc"
OBJCOPY="${PREFIX}objcopy"

CFLAGS="-mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm"
CFLAGS="$CFLAGS -ffreestanding -Wall -Wextra -O2 -DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG"
INCLUDES="-I$TOP/h3_bare/include -I$TOP/h3_bare/cores -I$TOP/h3_bare/src -I$TOP/h3_bare/platform/fb"

SRC_CORE="$TOP/h3_bare/cores/menu.c $TOP/h3_bare/cores/rom_browser.c $TOP/h3_bare/cores/sd.c $TOP/h3_bare/cores/fat.c $TOP/h3_bare/cores/usb_ohci.c $TOP/h3_bare/cores/usb_kbd.c $TOP/h3_bare/cores/fb_text.c"
SRC_PLATFORM="$TOP/h3_bare/platform/udelay.c $TOP/h3_bare/platform/h3_hs_timer.c $TOP/h3_bare/platform/h3_ccu.c $TOP/h3_bare/platform/h3.c"
SRC_FB="$TOP/h3_bare/platform/fb/h3_de2.c $TOP/h3_bare/platform/fb/h3_hdmi.c $TOP/h3_bare/platform/fb/dw_hdmi.c $TOP/h3_bare/platform/fb/h3_lcd.c"
SRC_SRC="$TOP/h3_bare/src/uart.c $TOP/h3_bare/src/printf.c $TOP/h3_bare/src/libc_min.c $TOP/h3_bare/src/main.c"

# Ассемблер
$AS $CFLAGS -x assembler-with-cpp -c -o "$BUILD/startup.o" "$TOP/h3_bare/platform/startup.S"

# Ядро каркаса
$CC $CFLAGS $INCLUDES -c -o "$BUILD/menu.o" "$TOP/h3_bare/cores/menu.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/rom_browser.o" "$TOP/h3_bare/cores/rom_browser.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/sd.o" "$TOP/h3_bare/cores/sd.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/fat.o" "$TOP/h3_bare/cores/fat.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/usb_ohci.o" "$TOP/h3_bare/cores/usb_ohci.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/usb_kbd.o" "$TOP/h3_bare/cores/usb_kbd.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/fb_text.o" "$TOP/h3_bare/cores/fb_text.c"

# Служебные
$CC $CFLAGS $INCLUDES -c -o "$BUILD/uart.o" "$TOP/h3_bare/src/uart.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/printf.o" "$TOP/h3_bare/src/printf.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/libc_min.o" "$TOP/h3_bare/src/libc_min.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/main.o" "$TOP/h3_bare/src/main.c"

# Платформа
$CC $CFLAGS $INCLUDES -c -o "$BUILD/udelay.o" "$TOP/h3_bare/platform/udelay.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/h3_hs_timer.o" "$TOP/h3_bare/platform/h3_hs_timer.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/h3_ccu.o" "$TOP/h3_bare/platform/h3_ccu.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/h3.o" "$TOP/h3_bare/platform/h3.c"

$CC $CFLAGS $INCLUDES -c -o "$BUILD/h3_de2.o" "$TOP/h3_bare/platform/fb/h3_de2.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/h3_hdmi.o" "$TOP/h3_bare/platform/fb/h3_hdmi.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/dw_hdmi.o" "$TOP/h3_bare/platform/fb/dw_hdmi.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/h3_lcd.o" "$TOP/h3_bare/platform/fb/h3_lcd.c"

# Линковка
$CC -T "$TOP/h3_bare/platform/linker.ld" -nostdlib -Wl,-gc-sections \
    -o "$BUILD/h3_bare.elf" \
    "$BUILD/startup.o" \
    "$BUILD/menu.o" "$BUILD/rom_browser.o" "$BUILD/sd.o" "$BUILD/fat.o" \
    "$BUILD/usb_ohci.o" "$BUILD/usb_kbd.o" "$BUILD/fb_text.o" \
    "$BUILD/uart.o" "$BUILD/printf.o" "$BUILD/libc_min.o" "$BUILD/main.o" \
    "$BUILD/udelay.o" "$BUILD/h3_hs_timer.o" "$BUILD/h3_ccu.o" "$BUILD/h3.o" \
    "$BUILD/h3_de2.o" "$BUILD/h3_hdmi.o" "$BUILD/dw_hdmi.o" "$BUILD/h3_lcd.o" \
    -lgcc

$OBJCOPY -O binary --remove-section .uncached "$BUILD/h3_bare.elf" "$BIN"
echo "--- h3_bare.bin: $(stat -c%s "$BIN") байт ---"

# ---- SD-образ (опционально) ----
if [ "${1:-}" = "sd" ] || [ "${1:-}" = "full" ]; then
    echo "Сборка SD-образа..."
    mkdir -p "$BUILD/u-boot"
    if [ ! -f "$BUILD/u-boot/u-boot-sunxi-with-spl.bin" ]; then
        # Локально собранный U-Boot: SPL + u-boot-dtb (sunxi раскладка: SPL на 8K, U-Boot на 32K)
        # Положить в build/u-boot/u-boot-sunxi-with-spl.bin
        echo "Файл build/u-boot/u-boot-sunxi-with-spl.bin не найден."
        echo "Соберите U-Boot вручную:"
        echo "  git clone --depth 1 --branch v2021.04 https://github.com/u-boot/u-boot.git"
        echo "  cd u-boot && make orangepi_lite_defconfig"
        echo "  make -j$(nproc) CROSS_COMPILE=arm-none-eabi- spl/u-boot-spl.bin u-boot-dtb.bin"
        echo "  # объединить: SPL на 8K + U-Boot на 32K -> build/u-boot/u-boot-sunxi-with-spl.bin"
        exit 1
    fi

    cat > "$BUILD/boot.cmd" << EOF
fatload mmc 0 0x40000000 h3_bare.bin
go 0x40000000
EOF
    mkimage -A arm -T script -C none -n "pico-retro V7" \
        -d "$BUILD/boot.cmd" "$BUILD/boot.scr"

    # Сборка образа без sudo: raw 64MB + SPL(8K) + U-Boot(32K) + FAT32(16M..64M) + MBR
    SDK_IMG="$BUILD/h3_bare.img"
    dd if=/dev/zero bs=1M count=64 of="$SDK_IMG" 2>/dev/null
    # SPL и U-Boot — по sunxi раскладке (взять из объединённого bin: SPL первые 8K, U-Boot на 32K)
    dd if="$BUILD/u-boot/u-boot-sunxi-with-spl.bin" of="$SDK_IMG" bs=1k seek=0 conv=notrunc 2>/dev/null
    FATPART="$BUILD/fatpart.bin"
    dd if=/dev/zero bs=1M count=48 of="$FATPART" 2>/dev/null
    mkfs.vfat -n H3_RETRO "$FATPART" >/dev/null 2>&1
    export MTOOLS_SKIP_CHECK=1
    mcopy -i "$FATPART" "$BUILD/boot.scr" ::boot.scr
    mcopy -i "$FATPART" "$BIN" ::h3_bare.bin
    mmd -i "$FATPART" ::roms
    for d in "$TOP"/roms/*/; do
        mmd -i "$FATPART" "::roms/$(basename "$d")"
    done
    for f in "$TOP"/roms/*/*; do
        [ -f "$f" ] && mcopy -i "$FATPART" "$f" "::roms/$(basename "$(dirname "$f")")/$(basename "$f")"
    done
    dd if="$FATPART" of="$SDK_IMG" bs=1M seek=16 conv=notrunc 2>/dev/null
    python3 - "$SDK_IMG" <<'EOF'
import struct, sys
img = open(sys.argv[1], 'r+b')
img.seek(446)
img.write(b'\x00' * 64)
img.seek(446)
part = b'\x80' + b'\x01\x01\x00' + b'\x0c' + b'\xfe\xff\xff' + struct.pack('<II', 32768, 98304)
img.write(part)
img.seek(510)
img.write(b'\x55\xaa')
img.close()
EOF
    rm -f "$FATPART"
    echo "--- h3_bare.img: $(stat -c%s "$SDK_IMG") байт ---"
fi

if [ "${1:-}" = "fel" ]; then
    echo "Загрузка через sunxi-fel..."
    sudo sunxi-fel write 0x40000000 "$BIN" execute 0x40000000
fi

echo "Готово."