#!/bin/bash
# build.sh — сборка v8 (мультисистемный эмулятор H3).
# Использование: ./build.sh [clean|sd|fel]
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
CXX="${PREFIX}g++"
AS="${PREFIX}gcc"
OBJCOPY="${PREFIX}objcopy"

CFLAGS="-mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm"
CFLAGS="$CFLAGS -ffreestanding -Wall -Wextra -O2 -DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG"
INCLUDES="-I$TOP/h3_bare/include -I$TOP/h3_bare/cores -I$TOP/h3_bare/cores/nes -I$TOP/h3_bare/cores/mcume -I$TOP/h3_bare/cores/a7800 -I$TOP/h3_bare/cores/a5200 -I$TOP/h3_bare/cores/smsplus -I$TOP/h3_bare/cores/gameboy -I$TOP/h3_bare/cores/portfolio -I$TOP/h3_bare/cores/lynx -I$TOP/h3_bare/src -I$TOP/h3_bare/platform/fb"
CXXFLAGS="$CFLAGS -fno-exceptions -fno-rtti -fno-threadsafe-statics"

# Ассемблер
$AS $CFLAGS -x assembler-with-cpp -c -o "$BUILD/startup.o" "$TOP/h3_bare/platform/startup.S"

# --- Ядро каркаса ---
$CC $CFLAGS $INCLUDES -c -o "$BUILD/menu.o" "$TOP/h3_bare/cores/menu.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/rom_browser.o" "$TOP/h3_bare/cores/rom_browser.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/settings.o" "$TOP/h3_bare/cores/settings.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/sd.o" "$TOP/h3_bare/cores/sd.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/fat.o" "$TOP/h3_bare/cores/fat.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/usb_ohci.o" "$TOP/h3_bare/cores/usb_ohci.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/usb_kbd.o" "$TOP/h3_bare/cores/usb_kbd.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/fb_text.o" "$TOP/h3_bare/cores/fb_text.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/led.o" "$TOP/h3_bare/cores/led.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/emu.o" "$TOP/h3_bare/cores/emu.c"

# --- MCUME (Atari 2600) ---
MCUME="$TOP/h3_bare/cores/mcume"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/system_atari_h3.o" "$TOP/h3_bare/cores/system_atari_h3.cpp"
for fn in Vcsemu Vmachine Raster Table Display Collision Tiasound Options Keyboard Exmacro; do
    $CC $CFLAGS $INCLUDES -std=gnu89 -O0 -c -o "$BUILD/mcume_$fn.o" "$MCUME/$fn.c"
done
for fn in Cpu Memory; do
    $CC $CFLAGS $INCLUDES -std=gnu89 -O2 -c -o "$BUILD/mcume_$fn.o" "$MCUME/$fn.c"
done

# --- A7800 ---
A7800="$TOP/h3_bare/cores/a7800"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/system_a7800_h3.o" "$TOP/h3_bare/cores/system_a7800_h3.cpp"
for fn in ProSystem Sally Maria Memory Cartridge Pokey Riot Tia Region Bios Palette; do
    $CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/a7800_$fn.o" "$A7800/$fn.cpp"
done

# --- A5200 ---
A5200="$TOP/h3_bare/cores/a5200"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/system_a5200_h3.o" "$TOP/h3_bare/cores/system_a5200_h3.cpp"
$CC $CFLAGS $INCLUDES -std=gnu11 -c -o "$BUILD/a5200_atari5200.o" "$A5200/atari5200.c"
for fn in antic cpu crc32 gtia pokey pokeysnd; do
    $CC $CFLAGS $INCLUDES -std=gnu89 -c -o "$BUILD/a5200_$fn.o" "$A5200/$fn.c"
done

# --- SMS Plus ---
SMS="$TOP/h3_bare/cores/smsplus"
$CXX $CXXFLAGS $INCLUDES -fhosted -O0 -c -o "$BUILD/system_sms_h3.o" "$TOP/h3_bare/cores/system_sms_h3.cpp"
for fn in sms system loadrom render vdp sn76496; do
    $CC $CFLAGS $INCLUDES -std=gnu89 -O0 -c -o "$BUILD/sms_$fn.o" "$SMS/$fn.c"
done
$CC $CFLAGS $INCLUDES -std=gnu89 -O2 -c -o "$BUILD/sms_z80.o" "$SMS/z80.c"

# --- Portfolio ---
PORT="$TOP/h3_bare/cores/portfolio"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/portfolio_system.o" "$PORT/system_portfolio.cpp"
for fn in cpu i8253 i8259; do
    $CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/portfolio_$fn.o" "$PORT/$fn.cpp"
done

# --- Game Boy ---
GB="$TOP/h3_bare/cores/gameboy"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/gameboy_host.o" "$TOP/h3_bare/cores/gameboy_host.cpp"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/gameboy_stubs.o" "$TOP/h3_bare/cores/gameboy_stubs.c"
GBFLAGS="$CFLAGS -std=gnu99 -DPRIu64=\"llu\" -DPRIx64=\"llx\" -DPRId64=\"lld\""
for fn in emulator memory joypad; do
    $CC $GBFLAGS $INCLUDES -c -o "$BUILD/gb_$fn.o" "$GB/$fn.c"
done

# --- Lynx (Handy) ---
LYNX="$TOP/h3_bare/cores/lynx"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/lynx_host.o" "$TOP/h3_bare/cores/lynx_host.cpp"
for fn in system mikie susie cart memmap eeprom rom ram lynxdec; do
    $CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/lynx_$fn.o" "$LYNX/$fn.cpp"
done
$CXX $CXXFLAGS $INCLUDES -fhosted -c -o "$BUILD/lynx_blip_buffer.o" "$LYNX/blip/Blip_Buffer.cpp"
$CXX $CXXFLAGS $INCLUDES -fhosted -c -o "$BUILD/lynx_blip_stereo.o" "$LYNX/blip/Stereo_Buffer.cpp"

# --- NES ---
NES="$TOP/h3_bare/cores/nes"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/nes_core.o" "$NES/InfoNES.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/nes_mapper.o" "$NES/InfoNES_Mapper.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/nes_papu.o" "$NES/InfoNES_pAPU.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/nes_cpu.o" "$NES/K6502.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/nes_host.o" "$TOP/h3_bare/cores/nes_host.cpp"

# --- Служебные ---
$CC $CFLAGS $INCLUDES -c -o "$BUILD/uart.o" "$TOP/h3_bare/src/uart.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/printf.o" "$TOP/h3_bare/src/printf.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/libc_min.o" "$TOP/h3_bare/src/libc_min.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/main.o" "$TOP/h3_bare/src/main.c"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/cxx_runtime.o" "$TOP/h3_bare/src/cxx_runtime.cpp"

# Платформа
$CC $CFLAGS $INCLUDES -c -o "$BUILD/udelay.o" "$TOP/h3_bare/platform/udelay.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/h3_hs_timer.o" "$TOP/h3_bare/platform/h3_hs_timer.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/h3_ccu.o" "$TOP/h3_bare/platform/h3_ccu.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/h3.o" "$TOP/h3_bare/platform/h3.c"

$CC $CFLAGS $INCLUDES -c -o "$BUILD/h3_de2.o" "$TOP/h3_bare/platform/fb/h3_de2.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/h3_hdmi.o" "$TOP/h3_bare/platform/fb/h3_hdmi.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/dw_hdmi.o" "$TOP/h3_bare/platform/fb/dw_hdmi.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/h3_lcd.o" "$TOP/h3_bare/platform/fb/h3_lcd.c"

# Линковка (g++ для подтягивания libstdc++)
$CXX -T "$TOP/h3_bare/platform/linker.ld" -nostdlib -Wl,-gc-sections \
    -o "$BUILD/h3_bare.elf" \
    "$BUILD/startup.o" \
    "$BUILD/menu.o" "$BUILD/rom_browser.o" "$BUILD/settings.o" "$BUILD/emu.o" \
    "$BUILD/system_atari_h3.o" "$BUILD/mcume_Vcsemu.o" "$BUILD/mcume_Vmachine.o" \
    "$BUILD/mcume_Raster.o" "$BUILD/mcume_Table.o" "$BUILD/mcume_Display.o" \
    "$BUILD/mcume_Collision.o" "$BUILD/mcume_Tiasound.o" "$BUILD/mcume_Options.o" \
    "$BUILD/mcume_Keyboard.o" "$BUILD/mcume_Exmacro.o" "$BUILD/mcume_Cpu.o" "$BUILD/mcume_Memory.o" \
    "$BUILD/system_a7800_h3.o" "$BUILD/a7800_ProSystem.o" "$BUILD/a7800_Sally.o" \
    "$BUILD/a7800_Maria.o" "$BUILD/a7800_Memory.o" "$BUILD/a7800_Cartridge.o" \
    "$BUILD/a7800_Pokey.o" "$BUILD/a7800_Riot.o" "$BUILD/a7800_Tia.o" \
    "$BUILD/a7800_Region.o" "$BUILD/a7800_Bios.o" "$BUILD/a7800_Palette.o" \
    "$BUILD/system_a5200_h3.o" "$BUILD/a5200_atari5200.o" "$BUILD/a5200_antic.o" \
    "$BUILD/a5200_cpu.o" "$BUILD/a5200_crc32.o" "$BUILD/a5200_gtia.o" \
    "$BUILD/a5200_pokey.o" "$BUILD/a5200_pokeysnd.o" \
    "$BUILD/system_sms_h3.o" "$BUILD/sms_sms.o" "$BUILD/sms_system.o" \
    "$BUILD/sms_loadrom.o" "$BUILD/sms_render.o" "$BUILD/sms_vdp.o" \
    "$BUILD/sms_sn76496.o" "$BUILD/sms_z80.o" \
    "$BUILD/portfolio_system.o" "$BUILD/portfolio_cpu.o" \
    "$BUILD/portfolio_i8253.o" "$BUILD/portfolio_i8259.o" \
    "$BUILD/gameboy_host.o" "$BUILD/gameboy_stubs.o" \
    "$BUILD/gb_emulator.o" "$BUILD/gb_memory.o" "$BUILD/gb_joypad.o" \
    "$BUILD/lynx_host.o" "$BUILD/lynx_system.o" "$BUILD/lynx_mikie.o" \
    "$BUILD/lynx_susie.o" "$BUILD/lynx_cart.o" "$BUILD/lynx_memmap.o" \
    "$BUILD/lynx_eeprom.o" "$BUILD/lynx_rom.o" "$BUILD/lynx_ram.o" \
    "$BUILD/lynx_lynxdec.o" "$BUILD/lynx_blip_buffer.o" "$BUILD/lynx_blip_stereo.o" \
    "$BUILD/nes_core.o" "$BUILD/nes_mapper.o" "$BUILD/nes_papu.o" "$BUILD/nes_cpu.o" "$BUILD/nes_host.o" \
    "$BUILD/sd.o" "$BUILD/fat.o" \
    "$BUILD/usb_ohci.o" "$BUILD/usb_kbd.o" "$BUILD/fb_text.o" "$BUILD/led.o" \
    "$BUILD/uart.o" "$BUILD/printf.o" "$BUILD/libc_min.o" "$BUILD/main.o" "$BUILD/cxx_runtime.o" \
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
        echo "Файл build/u-boot/u-boot-sunxi-with-spl.bin не найден."
        echo "Соберите U-Boot вручную (см. docs/BUILD.md)."
        exit 1
    fi
    cat > "$BUILD/boot.cmd" << EOF
fatload mmc 0 0x40000000 h3_bare.bin
go 0x40000000
EOF
    mkimage -A arm -T script -C none -n "pico-retro V8" \
        -d "$BUILD/boot.cmd" "$BUILD/boot.scr"
    SDK_IMG="$BUILD/h3_bare.img"
    dd if=/dev/zero bs=1M count=64 of="$SDK_IMG" 2>/dev/null
    dd if="$BUILD/u-boot/u-boot-sunxi-with-spl.bin" of="$SDK_IMG" bs=1k seek=8 conv=notrunc 2>/dev/null
    FATPART="$BUILD/fatpart.bin"
    dd if=/dev/zero bs=1M count=48 of="$FATPART" 2>/dev/null
    mkfs.vfat -F 32 -n H3_RETRO "$FATPART" >/dev/null 2>&1
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