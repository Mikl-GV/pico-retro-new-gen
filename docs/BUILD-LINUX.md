# BUILD-LINUX.md — сборка под Linux (Debian/Ubuntu, Arch)

Прошивка `h3_bare.bin` для Orange Pi Lite (Allwinner H3). Ниже — полный путь
от установки тулчейна до заливки на SD/FEL. Флаги компилятора (защита CPU) —
см. `docs/COMPILER.md`, менять только вместе с ним.

## 1. Установка тулчейна ARM bare-metal

Проект рассчитан на **gcc-arm-none-eabi (реально проверено 15.2.1)**.

### Debian / Ubuntu

Пакет `gcc-arm-none-eabi` из apt старый (может не дать `-mcpu=cortex-a7`
корректно у новых ядер), надёжнее — Arm GNU Toolchain:

```bash
# Зависимости
sudo apt update && sudo apt install -y make git python3

# Качаем официальный (пример 15.x):
# https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
wget https://developer.arm.com/-/media/Files/downloads/gnu/15.2.Rel1/binrel/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz

tar -xJf arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz
sudo mkdir -p /opt/arm && sudo cp -r arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi /opt/arm/
export PATH=/opt/arm/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin:$PATH
```

Либо быстрый вариант через apt (работает, но тулчейн старее):

```bash
sudo apt install -y gcc-arm-none-eabi binutils-arm-none-eabi
```

### Arch / Manjaro

```bash
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-binutils arm-none-eabi-newlib make git
```

### Проверка

```bash
arm-none-eabi-gcc --version   # должно быть "arm-none-eabi-gcc (15...)"
```

## 2. Сборка

Makefile сам находит тулчейн по `command -v arm-none-eabi-gcc`; если обоих
нет — упадёт на `arm-linux-gnueabihf-` (для нашей задачи не годится, ставь
bare-metal тулчейн).

```bash
cd pico-retro-new-gen
export PATH=/opt/arm/..../bin:$PATH   # если вручную ставил не в PATH
make clean 2>/dev/null
make -j4
```

Результат: `h3_bare.bin` (в корне и `build/h3_bare.bin`).

### Цели Makefile

| Цель | Что делает |
|---|---|
| `make` (all) | собрать `build/h3_bare.bin` + копию в корень |
| `make -j$(nproc)` | параллельная сборка |
| `make clean` | удалить `build/` |
| `make sd` | собрать SD-образ `build/h3_bare.img` |
| `make fel` | залить через `sunxi-fel` (нужны `sunxi-tools` + root) |

### make sd — дополнительные зависимости

Для SD-образа нужны `u-boot-tools` (mkimage), `mtools` (mcopy/mmd),
`dosfstools` (mkfs.vfat):

```bash
sudo apt install -y u-boot-tools mtools dosfstools
# потом
make sd
```

U-Boot-файл `build/u-boot/u-boot-sunxi-with-spl.bin` кладётся вручную
(см. `docs/BUILD.md`, если есть; под H3 собирается из исходников u-boot:
`orangepi_lite_defconfig`).

## 3. Заливка

### SD-карта (U-Boot `go`)

Смонтируй SD с FAT-разделом (sda1 = U-Boot + прошивка):

```bash
sudo mount /dev/sdX1 /mnt
sudo cp h3_bare.bin /mnt/h3_bare.bin
sync && sudo umount /mnt
```

На консоли U-Boot:

```
fatload mmc 0 0x40000000 h3_bare.bin
go 0x40000000
```

### FEL-загрузка (без SD, нужны root и sunxi-tools)

```bash
sudo apt install -y sunxi-tools
sudo make fel    # sunxi-fel write 0x40000000 h3_bare.bin execute 0x40000000
```

## 4. Признаки, что что-то пошло не так

- В UART `P:`/`D:` (Prefetch/Data Abort) — флаги/адреса; сверь `COMPILER.md`.
- CPU1 молча на `st=0x0E` — не доходит до справки TFT; сначала проверь FPEXC.EN.
- `undefined reference` между ядрами/файлами — линковка через
  `build/linker.rsp` + `-Wl,-gc-sections`; не «лечи» `-nostdlib` без
  `-ffreestanding`.

## 5. Быстрая запись в README

Если собираешь начисто на свежем Linux — минимальная последовательность:

```bash
# Debian/Ubuntu
sudo apt install -y make git python3 gcc-arm-none-eabi
git clone https://github.com/Mikl-GV/pico-retro-new-gen
cd pico-retro-new-gen
make -j4
```