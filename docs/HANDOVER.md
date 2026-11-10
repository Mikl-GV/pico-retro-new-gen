# HANDOVER — текущее состояние (r1193)

> Точка сохранения для нового чата. История дельт r849..r1192 — в
> `docs/HANDOVER_-r1192.md`. Здесь только актуальное для продолжения работы.
> Правила проекта — `AGENTS.md` (единственный источник правил).

---

## 1. Что это

Bare-metal мультисистемный эмулятор для Orange Pi Lite (Allwinner H3, Cortex-A7,
512 МБ, без ОС). Один бинарник `h3_bare.bin` грузится U-Boot/FEL по адресу
0x40000000. HDMI 1024×600 (меню/игры, core0) + SPI TFT 480×320 (справка/тач,
CPU1 — дополнение, не основной путь). ROM — SD/USB (FAT32), ввод — USB-клавиатура
(OHCI) + Sega-геймпад/кнопочный пад (PCF8574, bit-bang I2C на PG9/PG8).
30 систем READY (`h3_bare/cores/systems.h` — источник истины), 2 PLANNED.

**Звук:** вывод I2S0/CPU2 (r895..r902). Тракт: core0 `i2s_push_sample` → ring
(`.coherent`, uncached) → **CPU2** доливает в TX FIFO (guard `TXE_CNT`, темп =
аппаратный FIFO 48 кГц). `i2s.c/h` — рабочий слой; init I2S0: PLL 24.576 МГц,
делители sun8i, FMT0 SR=WSS=3, MODE_LEFT, PA10-SD. Тест-тон (Settings) идёт
отдельным raw-путём (r1152). DC-блокер FC≈119 Гц на CPU2 (default ON); снят
`i2s_dc_bypass(1)` там, где владелец решил «эталонно без ФВЧ»: GBA, MD/SMS/GG,
32X (r1145/r1178).

## 2. Сборка и проверка

```bash
make clean && make -j$(nproc)     # build/h3_bare.bin + копия в корень
```

- Тулчейн: `arm-none-eabi-gcc` 15.2.1 в PATH (`C:\ARM\gcc-arm-none-eabi-15.2.1`).
- Windows/MSYS2: `bash -lc 'export PATH=/c/ARM/gcc-arm-none-eabi-15.2.1/bin:$PATH; cd /e/Giga_1/pico-retro-new-gen && make clean && make -j8'`.
- После правки любого `.h` — `make clean` (страховка, deps r1155 отслеживаются).
- Версия: `g_fw_version` и `build:` в `h3_bare/src/main.c`. Текущая — **r1193**.
  Синхронизировать: main.c, README.md, AGENTS.md, этот файл.
- make: `Makefile` и `makefile` — два одинаковых файла (правки в ОБА).
- Менеджер сборки: `C:\msys64\tmp\mkrk4.sh` (env через msys, `make -j8`).
- 32X SH2 DRC опция сборки: `make PICO_DRC=1` (дефолт ВКЛ, r1169);
  `PICO_DRC=0` — интерпретатор.

## 3. Ключевые памяти (linker.ld / libc_min.c)

- `.text` 0x40000000; `.coherent` (uncached) 0x4A400000; L1-таблица 0x4A40C000
  (перенос из зоны стека, r722).
- `.gb_heap` NOLOAD **0x4A700000..0x4BF00000 (24 МБ)** — пул-аллокатор
  `malloc/calloc/realloc/free` в `gameboy_stubs.c` (bump+LIFO), общий для ядер
  (SNES/GB/…). `_gb_heap_start/end` — символы линкера. `gb_heap_reset()` в
  `emu_prepare()` обнуляет позицию (кроме `g_emu_keep_heap=1` для A8).
- newlib sbrk: `SBRK_START=_gb_heap_end`, `SBRK_LIMIT=0x4EF00000` (резерв под
  `_menu_arena` 0x4F000000). `libc_min.c` — форматтеры, mem-функции.
- `.noinit_fbn` 0x54000000 (zlib/fbn); `.vice_bss` 0x52000000; `.fs_bss`
  0x53E00000; `.a8_bss` 0x5F000000; `EMU_FB` 0x5F800000; FB back 0x5F400000.

## 4. Системы (id → ядро / host)

Ported | ядро (папка) | host | примечание
---|---|---|---
gba | mGBA-core (`h3_bare/cores/gba_mgba`) | gba_mgba_host.c | BIOS вшит, звук 32768→48k
ngp | libretro-ngp | ngp_host.cpp |
lynx | libretro-handy | lynx_host.c |
gameboy | binjgb | gameboy_host.c | пул stubs
gamegear / sms / megadrive / sg1000 | GPGX (`cores/gpgx`) | gpgx_host.c | SG r1190 SYSTEM_SG
a2600/a5200/a7800 | libretro-stella6502 / a5200(Atari800) / a7800 | system_*_h3.c |
nes | fceumm | fceumm_host.c |
pce | libretro-pce | pce_host.c |
snes | Snes9x 2005 (`cores/snes9x`) | snes_host.c / snes_compat.c |
32x | PicoDrive (`cores/picodrive`) | 32x_host.c | SH2 DRC, kart лишь
vectrex | libretro-vecx | vecx_host.c |
fbneo/cps1/cps2/neogeo/segasys/toaplan/cave | FBNeo (`cores/fbneo`) | fbneohost / cps1_host.cpp |
zxspectrum | libretro-fuse | fuse_host.c |
msx | libretro-fmsx | msx_compat.c | builtin BIOS
atari800 | libretro-atari800 | a8_host.c / a8_fs.c | keep_heap
bk0010 | libretro-bk | bk_host.c |
radio86rk | RK8266 (`cores/rk86`) | rk86_host.c | меню/вшит монитор
portfolio | 8086 (`cores/portfolio`) | portfolio_host.c | builtin
ms1504 | TTL (`cores/ms1504`) | ms1504_host.c | builtin
cpc | libretro-cap32 | cpc_host.c |
c64 | VICE (`cores/vice`) | c64_host.c | vice_bss отдельно
**amiga** | **PUAE/libretro-uae (`cores/puae`)** | **puae_host.c** | **r1191, AROS вшит; свой аллокатор puae_\* на sbrk r1192/r1193**

## 5. Amiga / PUAE (актуальное)

- Ядро `h3_bare/cores/puae` (вендор libretro-uae, WinUAE 5.3.1): ядро+retrodep+
  libretro-common+zlib, 181 объект, собрано `sources.mk`/`rules.mk` (имена
  `pz_*_c.o`). `puae_rename.sh` — префикс `puae_` всем глобалам набора
  (`--redefine-sym`), снимает коллизии.
- **Аллокатор**: global `malloc/calloc/free/strdup` в проекте — это gb-пул.
  PUAE НЕ должен в него ходить (стендовый abort F1=0x4A800000). Rename-канон
  включает ЯВНО `malloc calloc realloc free memalign strdup strndup`
  (эти символы в объектах набора — U-ссылки, определены вне набора, поэтому
  в DEFS не попадают и добавляются отдельным циклом). `puae_host.c` определяет
  bump-аллокатор `puae_malloc/calloc/realloc/free/strdup/strndup` на sbrk-зоне
  (`SBRK_LIMIT`), `puae_heap_reset()` в `emu_run_amiga` до `emu_prepare()`.
- **ВАЖНО**: правило `build/puae_renamed.stamp` ДОЛЖНО зависеть от
  `puae_rename.sh` (`Makefile:780`) — иначе правки скрипта не перезапускают
  переименование (тут ловили r1193: `calloc` в memory.o оставался глобальным).
- Видео RGB565 retro_bmp→EMU_FB 320×240 (nearest для superhires); аудио
  44100→48000 стерео; ввод USB-клава (HID→RETROK)+Sega-пад; environ
  `puae_kickstart="aros"` (BIOS вшит через `.incbin` aros.rom.c, с SD НЕ
  грузится); `puae_model="auto"`.
- Ромы: `/roms/amiga/*.adf` (id="amiga"). Стенд ждёт подтверждения запуска ADF
  с r1193 (предыдущие abort F1=0x4A800000 — причина fix'а).

## 6. Известные ограничения / TODO

- **32X требует стенда** (DRC на H3): BC Racers, Star Trek — нет `U`-падения
  после r1172; `32X diag` fps/масштаб.
- **Amiga ждёт стенда**: запуск ADF (только что фикс r1193), повторные вход/выход,
  отсутствие регрессий у gb-пула ядер.
- Файл с SD-образами для Amiga — только ADF (Kickstart вшит).
- `docs/HANDOVER_-r1192.md` — история дельт r849..r1192 (референс).