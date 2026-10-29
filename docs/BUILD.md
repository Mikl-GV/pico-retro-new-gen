# Сборка (Linux / Windows, bare-metal H3)

Единственный источник истины по сборке — `Makefile` (состав файлов, флаги,
rename-скрипты, линковка). `build.sh` / `build.ps1` — тонкие обёртки над `make`
(`all | clean | sd | fel`); прямых рецептов в них нет и быть не должно.

## 1. Требования

- **gcc-arm-none-eabi** (проверено на 15.2.1). Makefile выбирает префикс по
  `command -v arm-none-eabi-gcc`; если тулчейна нет — молча уйдёт на
  `arm-linux-gnueabihf-` (для прошивки не годится).
- `make`, `git`.
- Для `make sd`: `u-boot-tools` (mkimage), `mtools` (mcopy/mmd), `dosfstools`.

Debian/Ubuntu:

```bash
sudo apt install -y make git python3 gcc-arm-none-eabi u-boot-tools mtools dosfstools
# надёжнее — Arm GNU Toolchain 15.x с developer.arm.com (apt-пакет старее)
```

Arch/Manjaro:

```bash
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-binutils arm-none-eabi-newlib make git
```

Проверка: `arm-none-eabi-gcc --version`.

## 2. Сборка

```bash
make -j$(nproc)     # build/h3_bare.bin + копия h3_bare.bin в корень
```

**После правки любого `.h` — обязателен `make clean && make`:** зависимости от
заголовков не отслеживаются (`-MMD` нет), иначе объекты останутся со старыми
константами (вплоть до несовпадения ABI структур, например `fat_entry_t`).

Цели:

| Цель | Что делает |
|---|---|
| `make` (all) | `build/h3_bare.bin` + копия в корень |
| `make -j$(nproc)` | параллельная сборка |
| `make clean` | удалить `build/` |
| `make sd` | `build/h3_bare.img` (нужен U-Boot SPL) |
| `make fel` | залить через `sunxi-fel` (root + sunxi-tools) |

Windows (MSYS2 + ARM toolchain в PATH):

```bat
make -j%NUMBER_OF_PROCESSORS%
```

## 3. Флаги, которые нельзя менять без причины

```make
CFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm
CFLAGS += -ffreestanding -Wall -Wextra -O2 -DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG
```

| Флаг | Зачем | Если убрать/сменить |
|---|---|---|
| `-mcpu=cortex-a7` | только ARMv7-A | чужой opcode → UNDEF |
| `-marm` | ARM-режим | `bx` из `cpu1_entry` собьёт режим → Prefetch Abort |
| `-mfpu=neon` | NEON | (нужен ещё FPEXC.EN, см. ниже) |
| `-mfloat-abi=softfp` | soft-ABI | `hard` ломает кастомные printf/uart |
| `-ffreestanding` | без host-libc | несовместимые библиотечные вызовы |
| `-DNDEBUG` | без assert | «защита» только ручная — см. аудит |

Критично: CPU1/CPU2 выходят из сброса с выключенными VFP/NEON. В
`h3_bare/platform/startup.S` (`cpu1_entry`, `cpu2_entry`) обязательны
`CPACR = 0xF << 20` и `FPEXC.EN = 1`. Без них первая NEON-инструкция
`tft_drv.c` = UNDEF и тихое зависание ядра.

Регистры периферии обязаны оставаться `volatile` (SPI, GPIO, CCU, MMC) —
оптимизатор иначе схлопнет обращения.

## 4. Заливка

SD (U-Boot уже на карте, FAT-раздел):

```bash
sudo mount /dev/sdX1 /mnt && sudo cp h3_bare.bin /mnt/ && sync && sudo umount /mnt
```

В консоли U-Boot:

```
fatload mmc 0 0x40000000 h3_bare.bin
go 0x40000000
```

FEL (без SD):

```bash
sudo sunxi-fel write 0x40000000 h3_bare.bin execute 0x40000000
```

Полный образ: `sudo dd if=build/h3_bare.img of=/dev/sdX bs=1M conv=fsync`.

## 5. Если что-то не так

- `P:` / `D:` в UART — Prefetch/Data Abort: смотри адрес, сверь карту памяти
  (`docs/ARCHITECTURE.md`).
- CPU1 не доходит до справки TFT (`TFT_STAT=0x0E`) — проверь FPEXC.EN.
- `undefined reference` — линковка идёт через `build/linker.rsp` +
  `-Wl,-gc-sections`, `-nostdlib` без `-ffreestanding` не «лечить».
- Сборка «прошла», но поведение старое — забыт `make clean` после `.h`.

## 6. Файлы сборки

| Что | Где |
|---|---|
| Бинарник | `h3_bare.bin` (корень), `build/h3_bare.bin` |
| ELF (отладка) | `build/h3_bare.elf` |
| SD-образ | `build/h3_bare.img` |
| boot-скрипт | `build/boot.scr` |
