# pico-retro-new-gen

Ретроконсоль **bare metal** нового поколения на Orange Pi Lite (Allwinner H3, Cortex-A7) —
**мультисистемная**: Atari 2600, Atari 5200, Atari 7800 (+Nintendo NES и др. в разработке).

Без Linux, без ОС: один бинарник загружается U-Boot'ом прямо в память и
эмулирует. Вывод — **HDMI 1024×600** через 7″ сенсорную панель (XPT2046).

## Возможности

- **3 платформы**: Atari 2600, 5200, 7800
- **Bare-metal**: без ОС, один `h3_bare.bin` грузится через U-Boot
- **HDMI 1024×600** (7″ панель, force-hotplug для панелей без HPD)
- **Сенсорная панель XPT2046** (SPI0, PIN-вывод для 7″ LCD)
- **Меню** выбора платформы (1/2/3 — UART или тачем)
- **ROM вшиты** (Asteroids 7800 стартует сразу) + загрузка с SD (в разработке)
- **RGB565-рендер**: эмулятор рисует в framebuffer, HDMI-масштаб 3× (nearest)

## Быстрый старт (Linux)

Сборка — на Linux (или в WSL), тулчейн arm-none-eabi или arm-linux-gnueabihf.

```
sudo apt install gcc-arm-none-eabi cmake ninja-build
./build.sh
```

Результат:

```
build/h3_bare.bin          — бинарник для U-Boot/FEL
build/h3_bare.img          — готовый SD-образ (U-Boot + FAT + .bin)
```

Запись на SD:

```
sudo dd if=build/h3_bare.img of=/dev/sdX bs=1M conv=fsync
```

Или через **sunxi-fel** (USB без SD):

```
sudo sunxi-fel write 0x40000000 build/h3_bare.bin execute 0x40000000
```

## Системы и ROM

| Система | Статус | ROM |
|---------|--------|-----|
| Atari 2600 | ✓ | `a2600_roms/` (загрузка с SD — в разработке) |
| Atari 5200 | ✓ (mini-BIOS) | `a5200_roms/` |
| Atari 7800 | ✓ (Asteroids вшит) | `a7800_roms/` |

## Документация

- [docs/BUILD.md](docs/BUILD.md) — сборка на Linux
- [docs/SD.md](docs/SD.md) — запись на SD / FEL
- [docs/HARDWARE.md](docs/HARDWARE.md) — распиновка, HDMI, сенсор
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — как устроено (ядро 6502, TIA, ANTIC, MARIA)

## Структура

```
h3_bare/
  cores/      — 6502, A2600, A5200, A7800, touch
  platform/   — boot, linker, HDMI-стек (lib-h3 MIT)
  src/        — main.c, uart, printf, libc_min
  include/    — h3 regs, ROMs
  fb/         — DE2/HDMI (MIT, lib-h3)
docs/         — документация
a???_roms/    — ROM-игры
build.sh      — сборка
```

## Лицензии

- Код ядра и эмуляторов — собственный (без ограничений).
- HDMI/DE2 — `platform/fb/` из [lib-h3](https://github.com/vanvught/rpidmx512), MIT.
- ROM-файлы — распространяются только для личного использования (проверьте правомерность).

## V4 → V5

- V4 — Raspberry Pi Pico (RP2040), 6 платформ.
- V5 — Orange Pi Lite (H3), bare-metal, HDMI, тач.

Старые версии — в ветках `v2`, `v3`, `v4` этого репозитория.