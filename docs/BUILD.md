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

Быстрая параллельная сборка (использует все ядра):

```bash
make -j$(nproc)
```

Результат: `build/h3_bare.bin` + копия `h3_bare.bin` в корень проекта — загрузка через U-Boot или FEL.

Поддерживается и старый скрипт (последовательная сборка, ~5 мин):

```bash
./build.sh
```

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

## Файлы сборки

| Цель | Файл |
|------|------|
| Бинарник | `h3_bare.bin` (корень проекта) |
| ELF (отладка) | `build/h3_bare.elf` |
| SD-образ | `build/h3_bare.img` |
| boot-скрипт | `build/boot.scr` |