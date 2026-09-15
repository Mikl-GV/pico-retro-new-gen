cmake_minimum_required(VERSION 3.13)
project(h3_bare C CXX ASM)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/include
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/nes
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/mcume
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/a7800
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/a5200
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/smsplus
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/portfolio
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/src
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/platform/fb
)

set(SRCS
    h3_bare/platform/startup.S
    h3_bare/src/uart.c
    h3_bare/src/printf.c
    h3_bare/src/libc_min.c
    h3_bare/src/main.c
    h3_bare/cores/menu.c
    h3_bare/cores/rom_browser.c
    h3_bare/cores/settings.c
    h3_bare/cores/emu.c
    h3_bare/cores/system_atari_h3.cpp
    h3_bare/cores/mcume/Vcsemu.c
    h3_bare/cores/mcume/Vmachine.c
    h3_bare/cores/mcume/Cpu.c
    h3_bare/cores/mcume/Memory.c
    h3_bare/cores/mcume/Raster.c
    h3_bare/cores/mcume/Table.c
    h3_bare/cores/mcume/Display.c
    h3_bare/cores/mcume/Collision.c
    h3_bare/cores/mcume/Tiasound.c
    h3_bare/cores/mcume/Options.c
    h3_bare/cores/mcume/Keyboard.c
    h3_bare/cores/mcume/Exmacro.c
    h3_bare/cores/system_a7800_h3.cpp
    h3_bare/cores/a7800/ProSystem.cpp
    h3_bare/cores/a7800/Sally.cpp
    h3_bare/cores/a7800/Maria.cpp
    h3_bare/cores/a7800/Memory.cpp
    h3_bare/cores/a7800/Cartridge.cpp
    h3_bare/cores/a7800/Pokey.cpp
    h3_bare/cores/a7800/Riot.cpp
    h3_bare/cores/a7800/Tia.cpp
    h3_bare/cores/a7800/Region.cpp
    h3_bare/cores/a7800/Bios.cpp
    h3_bare/cores/a7800/Palette.cpp
    h3_bare/cores/system_a5200_h3.cpp
    h3_bare/cores/a5200/atari5200.c
    h3_bare/cores/a5200/antic.c
    h3_bare/cores/a5200/cpu.c
    h3_bare/cores/a5200/crc32.c
    h3_bare/cores/a5200/gtia.c
    h3_bare/cores/a5200/pokey.c
    h3_bare/cores/a5200/pokeysnd.c
    h3_bare/cores/system_sms_h3.cpp
    h3_bare/cores/smsplus/sms.c
    h3_bare/cores/smsplus/system.c
    h3_bare/cores/smsplus/loadrom.c
    h3_bare/cores/smsplus/render.c
    h3_bare/cores/smsplus/vdp.c
    h3_bare/cores/smsplus/sn76496.c
    h3_bare/cores/smsplus/z80.c
    h3_bare/cores/nes_host.cpp
    h3_bare/cores/portfolio/cpu.cpp
    h3_bare/cores/portfolio/i8253.cpp
    h3_bare/cores/portfolio/i8259.cpp
    h3_bare/cores/portfolio/system_portfolio.cpp
    h3_bare/cores/nes/InfoNES.cpp
    h3_bare/cores/nes/InfoNES_Mapper.cpp
    h3_bare/cores/nes/InfoNES_pAPU.cpp
    h3_bare/cores/nes/K6502.cpp
    h3_bare/cores/sd.c
    h3_bare/cores/fat.c
    h3_bare/cores/usb_ohci.c
    h3_bare/cores/usb_kbd.c
    h3_bare/cores/fb_text.c
    h3_bare/platform/udelay.c
    h3_bare/platform/h3_hs_timer.c
    h3_bare/platform/h3_ccu.c
    h3_bare/platform/h3.c
    h3_bare/platform/fb/h3_de2.c
    h3_bare/platform/fb/h3_hdmi.c
    h3_bare/platform/fb/dw_hdmi.c
    h3_bare/platform/fb/h3_lcd.c
)

add_executable(h3_bare.elf ${SRCS})

target_compile_options(h3_bare.elf PRIVATE -DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG)

# MCUME-ядро: старый C (gnu89), большинство файлов -O0 (проверено на V4: -O1+ ломает игры),
# только Cpu.c (главный интерпретатор) -O2.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/mcume/Vcsemu.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/mcume/Vmachine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/mcume/Raster.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/mcume/Table.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/mcume/Display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/mcume/Collision.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/mcume/Tiasound.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/mcume/Options.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/mcume/Keyboard.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/mcume/Exmacro.c
    PROPERTIES COMPILE_OPTIONS "-std=gnu89;-O0"
)
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/mcume/Cpu.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/mcume/Memory.c
    PROPERTIES COMPILE_OPTIONS "-std=gnu89;-O2"
)
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/system_atari_h3.cpp
    PROPERTIES COMPILE_OPTIONS "-O0"
)
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/a5200/antic.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/a5200/cpu.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/a5200/crc32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/a5200/gtia.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/a5200/pokey.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/a5200/pokeysnd.c
    PROPERTIES COMPILE_OPTIONS "-std=gnu89"
)
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/a5200/atari5200.c
    PROPERTIES COMPILE_OPTIONS "-std=gnu11"
)
# smsplus host: shared.h тянет <math.h>, которому нужен hosted mode (freestanding
# ломает libstdc++ headers: requires_hosted.h / tr1 gamma.tcc). -fhosted отменяет
# глобальный -ffreestanding только для этого файла (позже в порядке флагов).
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/system_sms_h3.cpp
    PROPERTIES COMPILE_OPTIONS "-fhosted;-O0"
)
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/smsplus/sms.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/smsplus/system.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/smsplus/loadrom.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/smsplus/render.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/smsplus/vdp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/smsplus/sn76496.c
    PROPERTIES COMPILE_OPTIONS "-std=gnu89;-O0"
)
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores/smsplus/z80.c
    PROPERTIES COMPILE_OPTIONS "-std=gnu89;-O2"
)
target_link_libraries(h3_bare.elf PRIVATE -lgcc)

target_link_options(h3_bare.elf PRIVATE
    -T ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/platform/linker.ld
    -nostdlib
    -Wl,-gc-sections
)

add_custom_command(TARGET h3_bare.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
        -O binary --remove-section .uncached $<TARGET_FILE:h3_bare.elf>
        ${CMAKE_BINARY_DIR}/h3_bare.bin
    COMMENT "Generating .bin"
)