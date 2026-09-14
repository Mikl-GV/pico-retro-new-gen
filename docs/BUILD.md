# Сборка (Linux, bare-metal H3)

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

```bash
./build.sh
```

Результат: `build/h3_bare.bin` (загрузка через U-Boot или FEL).

## Сборка SD-образа

```bash
./build.sh sd
```

Собирает `build/h3_bare.img` (64 МБ) с U-Boot + FAT32 + ROM-папками.

## Запись на флешку (без образа)

Если на флешке уже есть U-Boot и FAT32 — обновить только бинарник:

```bash
sudo mount /dev/sdX1 /mnt
sudo cp build/h3_bare.bin /mnt/
sudo sync; sudo umount /mnt
```

## Полный SD-образ (с нуля)

```bash
sudo dd if=build/h3_bare.img of=/dev/sdX bs=1M conv=fsync
```

## Загрузка через FEL (USB, без SD)

```bash
sudo sunxi-fel write 0x40000000 build/h3_bare.bin execute 0x40000000
```

## Файлы сборки

| Цель | Файл |
|------|------|
| Бинарник | `build/h3_bare.bin` |
| ELF (отладка) | `build/h3_bare.elf` |
| SD-образ | `build/h3_bare.img` |
| boot-скрипт | `build/boot.scr` |

Сборка CMake (опциональна): `cmake -B build && cmake --build build`