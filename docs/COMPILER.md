# COMPILER.md — правила сборки и флаги, защищающие процессор

Bare-metal проект (Orange Pi Lite / Allwinner H3, ARMv7-A Cortex-A7).
Любое изменение флагов сборки потенциально ломает загрузку — прежде чем
двигать, прочитай ниже.

## Обязательные флаги (Makefile, в начале CFLAGS)

```make
CFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm
CFLAGS += -ffreestanding -Wall -Wextra -O2 -DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG
```

| Флаг | За что отвечает | Что будет при удалении/изменении |
|---|---|---|
| `-mcpu=cortex-a7` | Генерация только инструкций ARMv7-A | gcc может выдать чужой-ядерный opcode → UNDEF |
| `-marm` | Только ARM-режим | CPU1: `bx r0` из `cpu1_entry` запутает режим → Prefetch Abort |
| `-mfpu=neon` | Разрешает NEON-инструкции | (в паре с FPEXC.EN — см. ниже) |
| `-mfloat-abi=softfp` | Float через soft-ABI | При `hard` упадут кастомные printf/uart |
| `-ffreestanding` | Без зависимостей от host-libc | Могут попасть несовместимые библиотечные вызовы |
| `-O2` | Оптимизация, но volatile сохранён | — |
| `-DNDEBUG` | Убирает assert-проверки | — |

## Критические зависимости вне Makefile

1. **CPU1 (вторичное ядро) выходит из сброса с VFP/NEON выключенными.**
   Любая NEON-инструкция (а `tft_drv.c` при `-O2` их использует: vpush/vst1…)
   = Исключение UNDEF → CPU1 прыгает в SRAM-трамплин и вешается.
   Поэтому в `h3_bare/platform/startup.S` (функция `cpu1_entry`) **обязательно**
   включены:
   ```asm
   mrc p15, 0, r0, c1, c0, 2      /* CPACR */
   orr r0, r0, #(0xF << 20)       /* CP10, CP11 full access */
   mcr p15, 0, r0, c1, c0, 2
   isb
   mov r0, #0x40000000
   vmsr fpexc, r0                 /* FPEXC.EN = 1 */
   isb
   ```
   **НЕ удалять** — иначе любой NEON на CPU1 = падение.

2. **Регистры периферии объявлены `volatile`** (`SPI0_TXD8/RXD8/FCR/FSR`,
   GPIO, CCU). Убирать `volatile` нельзя — оптимизатор схлопнет обращения и
   шина перестанет работать.

## Что НЕ делать

- Не менять глобально `-marm` на `-mthumb`.
- Не добавлять `-mfloat-abi=hard`.
- Не убирать `volatile` с регистров.
- Не «чинить» undefined reference флагами типа `-nostdlib` без `-ffreestanding`
  (линковка идёт через `build/linker.rsp` + `-Wl,-gc-sections`).

## Команда сборки (Windows/MSYS2)

```bash
export PATH=/c/msys64/usr/bin:/c/ARM/gcc-arm-none-eabi-15.2.1/bin:$PATH
make clean 2>/dev/null; make -j4
```

Прошивка: `h3_bare.bin` в корне и `build/h3_bare.bin`.
Заливка: U-Boot `go 0x40000000` или `sudo sunxi-fel write 0x40000000 h3_bare.bin execute 0x40000000`.

## Признаки, что флаги сломали ядро

- В UART появляется `P:` / `D:` (Prefetch/Data Abort) с адресом.
- CPU1 молча застревает на `st=0x0E` (не доходит до справки TFT).
- HDMI не стартует (core0 застрял до инициализации DE2).

Первое действие при таких симптомах — вернуть флаги из таблицы выше и
проверить, что `FPEXC.EN` на CPU1 включён.