#!/bin/bash
# build.sh — сборка v9 (мультисистемный эмулятор H3).
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
INCLUDES="-I$TOP/h3_bare/include -I$TOP/h3_bare/cores -I$TOP/h3_bare/cores/gpgx/core -I$TOP/h3_bare/cores/gpgx/libretro_inc -I$TOP/h3_bare/cores/gpgx/core/z80 -I$TOP/h3_bare/cores/gpgx/core/m68k -I$TOP/h3_bare/cores/gpgx/core/ntsc -I$TOP/h3_bare/cores/gpgx/core/sound -I$TOP/h3_bare/cores/gpgx/core/input_hw -I$TOP/h3_bare/cores/gpgx/core/cart_hw -I$TOP/h3_bare/cores/gpgx/core/cart_hw/svp -I$TOP/h3_bare/cores/gpgx/core/cd_hw -I$TOP/h3_bare/cores/fceumm -I$TOP/h3_bare/cores/fceumm/inc -I$TOP/h3_bare/cores/fceumm/input -I$TOP/h3_bare/cores/fceumm/boards -I$TOP/h3_bare/cores/fceumm/palettes -I$TOP/h3_bare/cores/fceumm/fir -I$TOP/h3_bare/cores/mcume -I$TOP/h3_bare/cores/a7800 -I$TOP/h3_bare/cores/a5200 -I$TOP/h3_bare/cores/gameboy -I$TOP/h3_bare/cores/portfolio -I$TOP/h3_bare/cores/lynx -I$TOP/h3_bare/cores/ngp -I$TOP/h3_bare/src -I$TOP/h3_bare/platform/fb"
SNES_INCLUDES="-I$TOP/h3_bare/cores/snes -I$TOP/h3_bare/cores/snes/libretro-common/include $INCLUDES"
CXXFLAGS="$CFLAGS -fno-exceptions -fno-rtti -fno-threadsafe-statics"

# Ассемблер
$AS $CFLAGS -x assembler-with-cpp -c -o "$BUILD/startup.o" "$TOP/h3_bare/platform/startup.S"

# --- Ядро каркаса ---
$CC $CFLAGS $INCLUDES -c -o "$BUILD/menu.o" "$TOP/h3_bare/cores/menu.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/rom_browser.o" "$TOP/h3_bare/cores/rom_browser.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/cheatdb.o" "$TOP/h3_bare/cores/cheatdb.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/settings.o" "$TOP/h3_bare/cores/settings.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/sd.o" "$TOP/h3_bare/cores/sd.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/fat.o" "$TOP/h3_bare/cores/fat.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/usb_ohci.o" "$TOP/h3_bare/cores/usb_ohci.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/usb_kbd.o" "$TOP/h3_bare/cores/usb_kbd.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/sega_pad.o" "$TOP/h3_bare/cores/sega_pad.c"
$CC $CFLAGS $INCLUDES -c -o "$BUILD/remap.o" "$TOP/h3_bare/cores/remap.c"
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

# --- Neo Geo Pocket / Pocket Color (RACE core) ---
NGP="$TOP/h3_bare/cores/ngp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/ngp_host.o" "$NGP/ngp_host.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/ngp_main.o" "$NGP/main.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/ngp_memory.o" "$NGP/memory.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/ngp_graphics.o" "$NGP/graphics.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/ngp_tlcs900h.o" "$NGP/tlcs900h.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/ngp_z80.o" "$NGP/z80.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/ngp_flash.o" "$NGP/flash.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/ngp_neopopsound.o" "$NGP/neopopsound.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/ngp_sound.o" "$NGP/sound.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/ngp_ngpBios.o" "$NGP/ngpBios.cpp"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/ngp_input.o" "$NGP/input.cpp"

# --- GBA (gpSP) ---
GBA_SP="$TOP/h3_bare/cores/gba_sp"
GBA_CFLAGS="$CFLAGS -DINLINE=inline"
$CC $GBA_CFLAGS $INCLUDES -c -o "$BUILD/gba_host.o" "$TOP/h3_bare/cores/gba_host.c"
$CC $GBA_CFLAGS $INCLUDES -c -o "$BUILD/gba_compat.o" "$GBA_SP/gba_compat.c"
$CC $GBA_CFLAGS -I$GBA_SP -x assembler-with-cpp -c -o "$BUILD/gba_bios_data.o" "$GBA_SP/bios_data.S"
GBA_RENAME="--redefine-sym vram=gpsp_vram --redefine-sym reg=gpsp_reg --redefine-sym cheats=gpsp_cheats --redefine-sym init_memory=gpsp_init_memory --redefine-sym init_cpu=gpsp_init_cpu --redefine-sym load_bios=gpsp_load_bios"
for fn in main gba_memory sound gba_cc_lut gbp cheats savestate serial serial_proto rfu; do
    $CC $GBA_CFLAGS $INCLUDES -c -o "$BUILD/gba_$fn.o.tmp" "$GBA_SP/$fn.c"
    $OBJCOPY $GBA_RENAME "$BUILD/gba_$fn.o.tmp" "$BUILD/gba_$fn.o"; rm -f "$BUILD/gba_$fn.o.tmp"
done
for fn in cpu video; do
    $CXX $CXXFLAGS $GBA_CFLAGS $INCLUDES -fno-exceptions -fno-rtti -c -o "$BUILD/gba_$fn.o.tmp" "$GBA_SP/$fn.cc"
    $OBJCOPY $GBA_RENAME "$BUILD/gba_$fn.o.tmp" "$BUILD/gba_$fn.o"; rm -f "$BUILD/gba_$fn.o.tmp"
done

# --- MSX/MSX2 (fMSX 6.0) ---
MSX="$TOP/h3_bare/cores/msx"
MSX_INC="-I$MSX -I$MSX/host -I$MSX/host/include -I$MSX/fMSX -I$MSX/Z80 -I$MSX/EMULib -I$MSX/NukeYKT"
MSX_CFLAGS="$CFLAGS -DLSB_FIRST -D__C99__ -DINLINE=inline $MSX_INC"
MSX_RENAME="--redefine-sym CPU=msx_CPU --redefine-sym RAM=msx_RAM --redefine-sym LoadROM=msx_LoadROM --redefine-sym rfopen=msx_rfopen --redefine-sym rfclose=msx_rfclose --redefine-sym rfread=msx_rfread --redefine-sym rfwrite=msx_rfwrite --redefine-sym rfseek=msx_rfseek --redefine-sym rftell=msx_rftell --redefine-sym rfgets=msx_rfgets --redefine-sym rfeof=msx_rfeof --redefine-sym rfgetc=msx_rfgetc --redefine-sym rfputc=msx_rfputc --redefine-sym filestream_rewind=msx_filestream_rewind --redefine-sym sscanf=msx_sscanf --redefine-sym time=msx_time --redefine-sym localtime=msx_localtime --redefine-sym strlcpy=msx_strlcpy --redefine-sym fill_pathname_join=msx_fill_pathname_join --redefine-sym strcasestr=msx_strcasestr --redefine-sym chdir=msx_chdir --redefine-sym getcwd=msx_getcwd"
for spec in "msx_host|$MSX/host/msx_host.c" "msx_compat|$MSX/host/msx_compat.c" "msx_log|$MSX/host/msx_log.c" "msx2_rom_data|$MSX/host/msx2_rom_data.c" "msx2ext_rom_data|$MSX/host/msx2ext_rom_data.c" "msx_MSX|$MSX/fMSX/MSX.c" "msx_V9938|$MSX/fMSX/V9938.c" "msx_Sound|$MSX/EMULib/Sound.c" "msx_SHA1|$MSX/EMULib/SHA1.c" "msx_Floppy|$MSX/EMULib/Floppy.c" "msx_FDIDisk|$MSX/EMULib/FDIDisk.c" "msx_MCF|$MSX/EMULib/MCF.c" "msx_Z80|$MSX/Z80/Z80.c" "msx_I8255|$MSX/EMULib/I8255.c" "msx_YM2413|$MSX/EMULib/YM2413.c" "msx_AY8910|$MSX/EMULib/AY8910.c" "msx_SCC|$MSX/EMULib/SCC.c" "msx_WD1793|$MSX/EMULib/WD1793.c" "msx_opll|$MSX/NukeYKT/opll.c" "msx_WrapNukeYKT|$MSX/NukeYKT/WrapNukeYKT.c"; do
    name="${spec%%|*}"; file="${spec##*|}"
    $CC $MSX_CFLAGS $INCLUDES -c -o "$BUILD/$name.o.tmp" "$file"
    $OBJCOPY $MSX_RENAME "$BUILD/$name.o.tmp" "$BUILD/$name.o"; rm -f "$BUILD/$name.o.tmp"
done

# --- NES (FCEUmm: точный CPU/PPU, 250+ мапперов, SuborKB-клавиатура) ---
FCEUMM="$TOP/h3_bare/cores/fceumm"
FCEUMM_CFLAGS="$CFLAGS -DFRONTEND_SUPPORTS_RGB565 -DFCEU_VERSION_NUMERIC=9900"
$CXX $CXXFLAGS $INCLUDES -c -o "$BUILD/fceumm_host.o" "$FCEUMM/nes_host_fceumm.cpp"
for fn in fceu x6502 ppu sound cart ines input fds fds_apu palette video file general state crc32 md5 fceu-endian fceu-memory cheat filter libretro_compat; do
    $CC $FCEUMM_CFLAGS $INCLUDES -c -o "$BUILD/fceumm_$fn.o" "$FCEUMM/$fn.c"
done
# vsuni/unif: нужны для линковки (UNIFchrrama, FCEU_VSUni*). NSF заменим заглушками
for fn in vsuni unif; do
    $CC $FCEUMM_CFLAGS $INCLUDES -c -o "$BUILD/fceumm_$fn.o" "$FCEUMM/$fn.c"
done
for f in "$FCEUMM"/input/*.c; do
    fn=$(basename "$f" .c)
    $CC $FCEUMM_CFLAGS $INCLUDES -c -o "$BUILD/fceumm_in_$fn.o" "$f"
done
for f in "$FCEUMM"/boards/*.c; do
    fn=$(basename "$f" .c)
    $CC $FCEUMM_CFLAGS $INCLUDES -c -o "$BUILD/fceumm_b_$fn.o" "$f"
done

# --- Snes9x 2005 (SNES / Super Famicom) ---
SNES="$TOP/h3_bare/cores/snes"
SNES_CFLAGS="$CFLAGS -DLOAD_FROM_MEMORY -DHAVE_NO_LANGEXTRA -DLAGFIX -Wno-incompatible-pointer-types"
$CXX $CXXFLAGS $SNES_CFLAGS $SNES_INCLUDES -c -o "$BUILD/snes_host.o" "$TOP/h3_bare/cores/snes_host.cpp"
$CC $CFLAGS $SNES_INCLUDES -c -o "$BUILD/snes_compat.o" "$TOP/h3_bare/cores/snes_compat.c"
for fn in c4 c4emu cheats2 cheats clip cpu cpuexec cpuops data dma dsp1 fxemu fxinst gfx getset globals memmap obc1 ppu sa1 sa1cpu sdd1 sdd1emu seta010 seta011 seta018 seta spc7110 spc7110dec srtc tile; do
    $CC $SNES_CFLAGS $SNES_INCLUDES -c -o "$BUILD/snes_$fn.o" "$SNES/$fn.c"
done
for fn in apu soundux spc700; do
    $CC $SNES_CFLAGS $SNES_INCLUDES -c -o "$BUILD/snes_$fn.o" "$SNES/$fn.c"
done
# PPU дублируется между FCEUmm и Snes9x — переименовываем во всех snes_*.o
for f in "$BUILD"/snes_*.o; do
    "$OBJCOPY" --redefine-sym PPU=snes_PPU "$f" "$f.tmp" && mv "$f.tmp" "$f"
done

# --- Sega Mega Drive / SMS (Genesis Plus GX) ---
GPGX="$TOP/h3_bare/cores/gpgx"
GPGX_CFLAGS="$CFLAGS -DLSB_FIRST -DBYTE_ORDER=LITTLE_ENDIAN -DMAXROMSIZE=16777216 -DUSE_16BPP_RENDERING -DFRONTEND_SUPPORTS_RGB565"
$CC $GPGX_CFLAGS $INCLUDES -c -o "$BUILD/gpgx_host.o" "$GPGX/system_gpgx_h3.c"
$CC $GPGX_CFLAGS $INCLUDES -c -o "$BUILD/gpgx_mathx.o" "$GPGX/gpgx_math.c"
$CC $GPGX_CFLAGS $INCLUDES -c -o "$BUILD/gpgx_missing.o" "$GPGX/gpgx_missing.c"
$CC $GPGX_CFLAGS $INCLUDES -c -o "$BUILD/gp_cheats.o" "$TOP/h3_bare/cores/gp_cheats.c"
for f in "$GPGX"/core/*.c; do
    fn=$(basename "$f" .c)
    $CC $GPGX_CFLAGS $INCLUDES -c -o "$BUILD/gpgx_core_$fn.o" "$f"
done
for d in z80 m68k ntsc sound input_hw cart_hw cd_hw; do
    for f in "$GPGX"/core/$d/*.c; do
        fn=$(basename "$f" .c)
        $CC $GPGX_CFLAGS $INCLUDES -c -o "$BUILD/gpgx_${d}_$fn.o" "$f"
    done
done
for f in "$GPGX"/core/cart_hw/svp/*.c; do
    fn=$(basename "$f" .c)
    $CC $GPGX_CFLAGS $INCLUDES -c -o "$BUILD/gpgx_svp_$fn.o" "$f"
done

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
    "$BUILD/menu.o" "$BUILD/rom_browser.o" "$BUILD/cheatdb.o" "$BUILD/settings.o" "$BUILD/emu.o" \
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
    "$BUILD/portfolio_system.o" "$BUILD/portfolio_cpu.o" \
    "$BUILD/portfolio_i8253.o" "$BUILD/portfolio_i8259.o" \
    "$BUILD/gameboy_host.o" "$BUILD/gameboy_stubs.o" \
    "$BUILD/gb_emulator.o" "$BUILD/gb_memory.o" "$BUILD/gb_joypad.o" \
    "$BUILD/lynx_host.o" "$BUILD/lynx_system.o" "$BUILD/lynx_mikie.o" \
    "$BUILD/lynx_susie.o" "$BUILD/lynx_cart.o" "$BUILD/lynx_memmap.o" \
    "$BUILD/lynx_eeprom.o" "$BUILD/lynx_rom.o" "$BUILD/lynx_ram.o" \
    "$BUILD/lynx_lynxdec.o" "$BUILD/lynx_blip_buffer.o" "$BUILD/lynx_blip_stereo.o" \
    "$BUILD/ngp_host.o" "$BUILD/ngp_main.o" "$BUILD/ngp_memory.o" \
    "$BUILD/ngp_graphics.o" "$BUILD/ngp_tlcs900h.o" "$BUILD/ngp_z80.o" \
    "$BUILD/ngp_flash.o" "$BUILD/ngp_neopopsound.o" "$BUILD/ngp_sound.o" \
    "$BUILD/ngp_ngpBios.o" "$BUILD/ngp_input.o" \
    "$BUILD/gba_host.o" "$BUILD/gba_compat.o" "$BUILD/gba_bios_data.o" \
    "$BUILD/gba_main.o" "$BUILD/gba_gba_memory.o" "$BUILD/gba_sound.o" \
    "$BUILD/gba_gba_cc_lut.o" "$BUILD/gba_gbp.o" "$BUILD/gba_cheats.o" \
    "$BUILD/gba_savestate.o" "$BUILD/gba_serial.o" "$BUILD/gba_serial_proto.o" \
    "$BUILD/gba_rfu.o" "$BUILD/gba_cpu.o" "$BUILD/gba_video.o" \
    "$BUILD/msx_host.o" "$BUILD/msx_compat.o" "$BUILD/msx_log.o" \
    "$BUILD/msx2_rom_data.o" "$BUILD/msx2ext_rom_data.o" \
    "$BUILD/msx_MSX.o" "$BUILD/msx_V9938.o" "$BUILD/msx_Sound.o" "$BUILD/msx_SHA1.o" \
    "$BUILD/msx_Floppy.o" "$BUILD/msx_FDIDisk.o" "$BUILD/msx_MCF.o" "$BUILD/msx_Z80.o" \
    "$BUILD/msx_I8255.o" "$BUILD/msx_YM2413.o" "$BUILD/msx_AY8910.o" "$BUILD/msx_SCC.o" \
    "$BUILD/msx_WD1793.o" "$BUILD/msx_opll.o" "$BUILD/msx_WrapNukeYKT.o" \
    "$BUILD/fceumm_host.o" \
    "$BUILD/fceumm_fceu.o" "$BUILD/fceumm_x6502.o" "$BUILD/fceumm_ppu.o" "$BUILD/fceumm_sound.o" \
    "$BUILD/fceumm_cart.o" "$BUILD/fceumm_ines.o" "$BUILD/fceumm_input.o" "$BUILD/fceumm_fds.o" \
    "$BUILD/fceumm_fds_apu.o" "$BUILD/fceumm_palette.o" "$BUILD/fceumm_video.o" "$BUILD/fceumm_file.o" \
    "$BUILD/fceumm_general.o" "$BUILD/fceumm_state.o" "$BUILD/fceumm_crc32.o" "$BUILD/fceumm_md5.o" \
    "$BUILD/fceumm_fceu-endian.o" "$BUILD/fceumm_fceu-memory.o" "$BUILD/fceumm_cheat.o" \
    "$BUILD/fceumm_filter.o" "$BUILD/fceumm_libretro_compat.o" \
    "$BUILD/fceumm_vsuni.o" "$BUILD/fceumm_unif.o" \
    "$BUILD/fceumm_in_"*.o "$BUILD/fceumm_b_"*.o \
    "$BUILD/snes_host.o" "$BUILD/snes_compat.o" \
    "$BUILD/snes_c4.o" "$BUILD/snes_c4emu.o" "$BUILD/snes_cheats2.o" "$BUILD/snes_cheats.o" \
    "$BUILD/snes_clip.o" "$BUILD/snes_cpu.o" "$BUILD/snes_cpuexec.o" "$BUILD/snes_cpuops.o" \
    "$BUILD/snes_data.o" "$BUILD/snes_dma.o" "$BUILD/snes_dsp1.o" "$BUILD/snes_fxemu.o" \
    "$BUILD/snes_fxinst.o" "$BUILD/snes_gfx.o" "$BUILD/snes_getset.o" "$BUILD/snes_globals.o" \
    "$BUILD/snes_memmap.o" "$BUILD/snes_obc1.o" "$BUILD/snes_ppu.o" "$BUILD/snes_sa1.o" \
    "$BUILD/snes_sa1cpu.o" "$BUILD/snes_sdd1.o" "$BUILD/snes_sdd1emu.o" "$BUILD/snes_seta010.o" \
    "$BUILD/snes_seta011.o" "$BUILD/snes_seta018.o" "$BUILD/snes_seta.o" "$BUILD/snes_spc7110.o" \
    "$BUILD/snes_spc7110dec.o" "$BUILD/snes_srtc.o" "$BUILD/snes_tile.o" \
    "$BUILD/snes_apu.o" "$BUILD/snes_soundux.o" "$BUILD/snes_spc700.o" \
    "$BUILD/gpgx_core_"*.o "$BUILD/gpgx_z80_"*.o "$BUILD/gpgx_m68k_"*.o "$BUILD/gpgx_ntsc_"*.o \
    "$BUILD/gpgx_sound_"*.o "$BUILD/gpgx_input_hw_"*.o "$BUILD/gpgx_cart_hw_"*.o \
    "$BUILD/gpgx_cd_hw_"*.o "$BUILD/gpgx_svp_"*.o \
    "$BUILD/gpgx_host.o" "$BUILD/gpgx_mathx.o" "$BUILD/gpgx_missing.o" "$BUILD/gp_cheats.o" \
    "$BUILD/sd.o" "$BUILD/fat.o" \
    "$BUILD/usb_ohci.o" "$BUILD/usb_kbd.o" "$BUILD/sega_pad.o" "$BUILD/fb_text.o" "$BUILD/led.o" \
    "$BUILD/uart.o" "$BUILD/printf.o" "$BUILD/libc_min.o" "$BUILD/main.o" "$BUILD/cxx_runtime.o" \
    "$BUILD/udelay.o" "$BUILD/h3_hs_timer.o" "$BUILD/h3_ccu.o" "$BUILD/h3.o" \
    "$BUILD/h3_de2.o" "$BUILD/h3_hdmi.o" "$BUILD/dw_hdmi.o" "$BUILD/h3_lcd.o" \
    -lgcc -lc -lm -lgcc

$OBJCOPY -O binary "$BUILD/h3_bare.elf" "$BIN"
# Дублируем прошивку в корень проекта — чтобы не искать в build/
cp -f "$BIN" "$TOP/h3_bare.bin"
echo "--- h3_bare.bin: $(stat -c%s "$BIN") байт (build/ и корень) ---"

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
    mkimage -A arm -T script -C none -n "pico-retro V9" \
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