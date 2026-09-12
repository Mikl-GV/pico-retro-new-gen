# Сборка (Linux)

## Требования

Нужен кросс-тулчейн ARMv7-A (hard float). Подойдёт любой из:

```bash
# Вариант 1 — arm-none-eabi (рекомендуется)
sudo apt install gcc-arm-none-eabi

# Вариант 2 — arm-linux-gnueabihf
sudo apt install gcc-arm-linux-gnueabihf
```

Остальное: `make`, `cmake` (не обязателен — есть `build.sh`), `git`.
Для SD-образа дополнительно: `wget`, `mkimage` (u-boot-tools), `parted`, `dosfstools`, `sudo`.

```bash
sudo apt install build-essential cmake ninja-build wget u-boot-tools parted dosfstools
```

## Сборка бинарника

```bash
git clone https://github.com/Mikl-GV/pico-retro
cd pico-retro
./build.sh
```

Результат:
- `build/h3_bare.bin` — исполняемый бинарник (загрузка через U-Boot/fel)
- `build/h3_bare.elf` — ELF (для отладки, gdb)

## Сборка SD-образа

```bash
./build.sh sd
```

Собирает `build/h3_bare.img` (64 MB) — **готовый образ для Rufus/dd**:
- U-Boot SPL + U-Boot в первых секторах
- FAT-раздел с `boot.scr` + `h3_bare.bin`

U-Boot скачивается автоматически (релиз v2024.10). Если скачивание не работает —
соберите вручную:

```bash
git clone https://github.com/u-boot/u-boot.git
cd u-boot
make orangepi_lite_defconfig
make
# результат: u-boot-sunxi-with-spl.bin — положить в build/u-boot/
```

## Альтернативные способы сборки

Сборка через CMake (если хотите IDE/та больше контроля):

```bash
cmake -B build -G Ninja --toolchain h3_bare/toolchain-h3.cmake
cmake --build build
```

## Что собирается где

| Цель | Файл |
|---|---|
| Бинарник | `build/h3_bare.bin` |
| ELF | `build/h3_bare.elf` |
| SD-образ | `build/h3_bare.img` |
| boot-скрипт | `build/boot.scr` |