# pico-retro-new-gen

Bare-metal мультисистемный эмулятор на Orange Pi Lite (Allwinner H3, 512 МБ).

Без Linux, без ОС: один бинарник загружается U-Boot'ом. HDMI 1024×600, USB-клавиатура, ROM с SD-карты.

## Сборка

```
sudo apt install gcc-arm-none-eabi u-boot-tools mtools
./build.sh              # -> build/h3_bare.bin
./build.sh sd           # + build/h3_bare.img (SD-образ)
```

Запись: `sudo dd if=build/h3_bare.img of=/dev/sdX bs=1M conv=fsync`

## Системы

| Группа | Системы |
|--------|---------|
| Портативные | Game Boy/GBC, Sega Game Gear |
| Консоли | Atari 2600/5200/7800, SMS, ColecoVision, NES, Galaxian, CPS-1/2, Neo Geo, Toaplan, Sega System, Mega Drive, SNES, PC Engine |
| Компьютеры | ZX Spectrum, MSX, Радио-86РК, БК-0010 |

ROM-файлы — в `/roms/<id>/` на SD-карте, без копирайта в репозитории.