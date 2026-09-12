# toolchain-h3.cmake — cross-compile для Allwinner H3 (Cortex-A7), bare metal.
# Важно: CMAKE_SYSTEM_NAME=Generic — чтобы CMake на Windows-хосте НЕ добавлял
# PE-флаги (-lkernel32, --major-image-version) к арм-линкеру.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_PREFIX C:/ARM/gcc-arm-none-eabi-15.2.1/bin/arm-none-eabi-)

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc.exe)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc.exe)
set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy.exe)

# Не запускаем исполняемые файлы на хосте (они ARM)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Никаких системных библиотек
set(CMAKE_C_IMPLICIT_LINK_LIBRARIES "")
set(CMAKE_ASM_IMPLICIT_LINK_LIBRARIES "")
set(CMAKE_C_IMPLICIT_LINK_DIRECTORIES "")
set(CMAKE_ASM_IMPLICIT_LINK_DIRECTORIES "")

# Базовые флаги компиляции для A7
set(ARCH_FLAGS "-mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm")
set(CMAKE_C_FLAGS "${ARCH_FLAGS} -ffreestanding -Wall -Wextra -O2" CACHE STRING "H3 C flags")
set(CMAKE_ASM_FLAGS "${ARCH_FLAGS} -x assembler-with-cpp" CACHE STRING "H3 ASM flags")