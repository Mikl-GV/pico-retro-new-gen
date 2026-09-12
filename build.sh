#!/bin/bash
# build.sh — сборка V5 под Linux.
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
mkdir -p "$BUILD"
CC="${PREFIX}gcc"
AS="${PREFIX}gcc"
OBJCOPY="${PREFIX}objcopy"

CFLAGS="-mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm"
CFLAGS="$CFLAGS -ffreestanding -Wall -Wextra -O2 -DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG"
INCLUDES="-I$TOP/h3_bare/include -I$TOP/h3_bare/cores -I$TOP/h3_bare/src -I$TOP/h3_bare/platform/fb"

SRC_CORE="$TOP/h3_bare/cores/cpu6502.c $TOP/h3_bare/cores/a2600.c $TOP/h3_bare/cores/a5200.c $TOP/h3_bare/cores/a7800.c $TOP/h3_bare/cores/test_a5200.c $TOP/h3_bare/cores/touch_xpt2046.c $TOP/h3_bare/cores/sd.c $TOP/h3_bare/cores/fat.c $TOP/h3_bare/cores/usb_ohci.c $TOP/h3_bare/cores/usb_kbd.c $TOP/h3_bare/cores/fb_text.c"
SRC_PLATFORM="$TOP/h3_bare/platform/udelay.c $TOP/h3_bare/platform/h3_hs_timer.c $TOP/h3_bare/platform/h3_ccu.c $TOP/h3_bare/platform/h3.c"
SRC_FB="$TOP/h3_bare/platform/fb/h3_de2.c $TOP/h3_bare/platform/fb/h3_hdmi.c $TOP/h3_bare/platform/fb/dw_hdmi.c $TOP/h3_bare/platform/fb/h3_lcd.c"
SRC_SRC="$TOP/h3_bare/src/uart.c $TOP/h3_bare/src/printf.c $TOP/h3_bare/src/libc_min.c $TOP/h3_bare/src/main.c"

# Ассемблер
$AS $CFLAGS -x assembler-with-cpp -c -o "$BUILD/startup.o" "$TOP/h3_bare/platform/startup.S"

# Компиляция
$CC $CFLAGS $INCLUDES -c -o "$BUILD/uart.o" "$TOP/h3_bare/src/uart.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/printf.o" "$TOP/h3_bare/src/printf.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/libc_min.o" "$TOP/h3_bare/src/libc_min.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/main.o" "$TOP/h3_bare/src/main.c"

$CC $CFLAGS $INCLUDES -c -o "$BUILD/cpu6502.o" "$TOP/h3_bare/cores/cpu6502.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/a2600.o" "$TOP/h3_bare/cores/a2600.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/a5200.o" "$TOP/h3_bare/cores/a5200.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/a7800.o" "$TOP/h3_bare/cores/a7800.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/test_a5200.o" "$TOP/h3_bare/cores/test_a5200.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/touch.o" "$TOP/h3_bare/cores/touch_xpt2046.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/sd.o" "$TOP/h3_bare/cores/sd.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/fat.o" "$TOP/h3_bare/cores/fat.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/usb_ohci.o" "$TOP/h3_bare/cores/usb_ohci.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/usb_kbd.o" "$TOP/h3_bare/cores/usb_kbd.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/fb_text.o" "$TOP/h3_bare/cores/fb_text.c"

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
    "$BUILD/startup.o" "$BUILD/uart.o" "$BUILD/printf.o" "$BUILD/libc_min.o" "$BUILD/main.o" \
    "$BUILD/cpu6502.o" "$BUILD/a2600.o" "$BUILD/a5200.o" "$BUILD/a7800.o" \
    "$BUILD/test_a5200.o" "$BUILD/touch.o" "$BUILD/sd.o" "$BUILD/fat.o" "$BUILD/usb_ohci.o" "$BUILD/usb_kbd.o" "$BUILD/fb_text.o" \
    "$BUILD/udelay.o" "$BUILD/h3_hs_timer.o" "$BUILD/h3_ccu.o" "$BUILD/h3.o" \
    "$BUILD/h3_de2.o" "$BUILD/h3_hdmi.o" "$BUILD/dw_hdmi.o" "$BUILD/h3_lcd.o" \
    -lgcc

$OBJCOPY -O binary --remove-section .uncached "$BUILD/h3_bare.elf" "$BIN"
echo "--- h3_bare.bin: $(stat -c%s "$BIN") байт ---"

# ---- SD-образ (опционально) ----
if [ "${1:-}" = "sd" ] || [ "${1:-}" = "full" ]; then
    echo "Сборка SD-образа..."

    # Загрузка прекомпиленного U-Boot для OPI Lite
    mkdir -p "$BUILD/u-boot"
    if [ ! -f "$BUILD/u-boot/u-boot-sunxi-with-spl.bin" ]; then
        UBOOT_URL="https://github.com/u-boot/u-boot/releases/download/v2024.10/u-boot-sunxi-with-spl.bin"
        wget -q "$UBOOT_URL" -O "$BUILD/u-boot/u-boot-sunxi-with-spl.bin" || {
            echo "Не удалось скачать U-Boot. Соберите вручную:"
            echo "  git clone https://github.com/u-boot/u-boot.git"
            echo "  cd u-boot && make orangepi_lite_defconfig && make"
            echo "  cp u-boot-sunxi-with-spl.bin $BUILD/u-boot/"
            exit 1
        }
    fi

    # boot.cmd → boot.scr
    cat > "$BUILD/boot.cmd" << EOF
fatload mmc 0 0x40000000 h3_bare.bin
go 0x40000000
EOF
    mkimage -A arm -T script -C none -n "pico-retro V5" \
        -d "$BUILD/boot.cmd" "$BUILD/boot.scr"

    # .img: SPL + U-Boot + FAT с boot.scr + h3_bare.bin
    SDK_IMG="$BUILD/sdk.img"
    dd if=/dev/zero bs=1M count=64 of="$SDK_IMG" 2>/dev/null
    # SPL в 8K, U-Boot в 40K
    dd if="$BUILD/u-boot/u-boot-sunxi-with-spl.bin" of="$SDK_IMG" conv=notrunc 2>/dev/null
    # FAT в конце (делать через mkfs.vfat + mcopy)
    sudo sh -c "
        LOOP=\$(losetup -fP --show "$SDK_IMG")
        parted -s \$LOOP mklabel msdos
        parted -s \$LOOP mkpart primary fat32 16M 64M
        mkfs.vfat -n H3_RETRO \${LOOP}p1
        mount \${LOOP}p1 /mnt
        cp \"$BIN\" /mnt/h3_bare.bin
        cp \"$BUILD/boot.scr\" /mnt/
        sync
        umount /mnt
        losetup -d \$LOOP
    " 2>/dev/null
    mv "$SDK_IMG" "$IMG"
    echo "--- h3_bare.img: $(stat -c%s "$IMG") байт ---"
fi

if [ "${1:-}" = "fel" ]; then
    echo "Загрузка через sunxi-fel..."
    sudo sunxi-fel write 0x40000000 "$BIN" execute 0x40000000
fi

echo "Готово."