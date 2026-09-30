# Сборка (Linux / Windows, bare-metal H3)

## Требования

ARM-тулчейн (любой из):

```bash
# Рекомендуется
sudo apt install gcc-arm-none-eabi
# Альтернатива
sudo apt install gcc-arm-linux-gnueabihf
```

Для SD-образа:

```bash
sudo apt install u-boot-tools mtools
```

## Сборка бинарника

**Официальный путь — Makefile** (параллельная сборка, использует все ядра):

```bash
make -j$(nproc)
```

Совместимые обёртки (`build.sh` / `build.ps1`) делегируют make и работают
с теми же целями (`clean | sd | fel`, без аргумента = `make -j`):

```bash
./build.sh                 # Linux: эквивалент make -j
powershell -File .\build.ps1   # Windows
```

Результат: `build/h3_bare.bin` + копия `h3_bare.bin` в корень проекта — загрузка через U-Boot или FEL.

### Windows

1. ARM-тулчейн: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
   (распаковать, добавить `.../bin` в PATH).
2. make — через MSYS2 (`pacman -S make`) или любой mingw-make в PATH.
3. Сборка:
   ```bat
   make -j%NUMBER_OF_PROCESSORS%
   ```
   или `powershell -ExecutionPolicy Bypass -File .\build.ps1`.

## Сборка SD-образа

```bash
make sd          # или: ./build.sh sd
```

Собирает `build/h3_bare.img` (64 МБ) с U-Boot + FAT32 + ROM-папками.

## Запись на флешку (без образа)

Если на флешке уже есть U-Boot и FAT32 — обновить только бинарник:

```bash
sudo mount /dev/sdX1 /mnt
sudo cp h3_bare.bin /mnt/
sudo sync; sudo umount /mnt
```

## Полный SD-образ (с нуля)

```bash
sudo dd if=build/h3_bare.img of=/dev/sdX bs=1M conv=fsync
```

## Загрузка через FEL (USB, без SD)

```bash
sudo sunxi-fel write 0x40000000 h3_bare.bin execute 0x40000000
```

## ПРАВИЛО АКТУАЛЬНОСТИ СБОРЩИКОВ (важно)

- **Единый источник истины — `Makefile`**: состав файлов, флаги, rename-скрипты,
  линковка изменяются только там.
- `build.sh` / `build.ps1` — **тонкие обёртки** над make (см. их шапки).
  Им НЕ передаются рецепты сборки, поэтому они не могут устареть.
- **Запрещено** дублировать в обёртках списки файлов/команды компиляции из
  Makefile. Если прямая сборка в обёртках всё же понадобится — только после
  явного решения владельца, с полной синхронизацией и этой памяткой в шапке.
- Историческая справка: до r500 `build.sh`/`build.ps1` были полными
  сборщиками и отстали от Makefile на целые системы (Fuse/BK/MS1504/PCE/CPS1)
  — именно поэтому они переведены на делегирование.

## Файлы сборки

| Цель | Файл |
|------|------|
| Бинарник | `h3_bare.bin` (корень проекта) |
| ELF (отладка) | `build/h3_bare.elf` |
| SD-образ | `build/h3_bare.img` |
| boot-скрипт | `build/boot.scr` |