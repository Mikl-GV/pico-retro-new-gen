cmake_minimum_required(VERSION 3.13)
project(h3_bare C ASM)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/include
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/cores
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/src
    ${CMAKE_CURRENT_SOURCE_DIR}/h3_bare/platform/fb
)

set(SRCS
    h3_bare/platform/startup.S
    h3_bare/src/uart.c
    h3_bare/src/printf.c
    h3_bare/src/libc_min.c
    h3_bare/src/main.c
    h3_bare/cores/cpu6502.c
    h3_bare/cores/a2600.c
    h3_bare/cores/a5200.c
    h3_bare/cores/a7800.c
    h3_bare/cores/test_a5200.c
    h3_bare/cores/touch_xpt2046.c
    h3_bare/cores/sd.c
    h3_bare/cores/fat.c
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