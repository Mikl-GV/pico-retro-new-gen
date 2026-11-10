# HANDOVER — текущее состояние (r1208)

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
- Windows: сборка ТОЛЬКО прямым `make` из PowerShell (не через `bash -lc`, не
  через `C:\msys64\tmp\mkrk4.sh` — там `-j8` и теряется параллелизм на 20 ядрах):
  `$env:PATH="C:\msys64\usr\bin;C:\ARM\gcc-arm-none-eabi-15.2.1\bin;"+$env:PATH; make clean; make -j20`.
- После правки любого `.h` — `make clean` (страховка, deps r1155 отслеживаются).
- Версия: `g_fw_version` и `build:` в `h3_bare/src/main.c`. Текущая — **r1208**.
  Синхронизировать: main.c, README.md, AGENTS.md, этот файл.
- make: `Makefile` и `makefile` — два одинаковых файла (правки в ОБА).
- 32X SH2 DRC опция сборки: `make PICO_DRC=1` (дефолт ВКЛ, r1169);
  `PICO_DRC=0` — интерпретатор.

## 3. Ключевые памяти (linker.ld / libc_min.c)

> **Актуальная карта памяти, владельцы зон, перекрытия и правила MMU —
> `docs/MEMORY.md`** (сверено с `readelf/nm` r1208). Ниже — только история
> решений; при расхождении верно `MEMORY.md` + код.

- `.text` 0x40000000; `.coherent` 0x4A400000 (**write-back**, r1206: NC ломает
  OHCI); LPAE-таблицы: L0-копия 0x4A40C000, L1-копии 0x4A410000+ (r1202).
- `.gb_heap` NOLOAD **0x4A700000..0x4BF00000 (24 МБ)** — пул-аллокатор
  `malloc/calloc/realloc/free` в `gameboy_stubs.c` (bump+LIFO), общий для ядер
  (SNES/GB/…). `_gb_heap_start/end` — символы линкера. `gb_heap_reset()` в
  `emu_prepare()` обнуляет позицию (кроме `g_emu_keep_heap=1` для A8).
- newlib sbrk: `SBRK_START=_gb_heap_end` (=0x4BF00000), `SBRK_LIMIT=0x4EF00000`
  (резерв под `_menu_arena` 0x4F000000). `libc_min.c` — форматтеры, mem-функции.
- **r1194 (MMU sbrk-зона) — ИСТОРИЯ, функция удалена в r1202.**
  `mmu_map_dram_cacheable()` прошивала short-дескрипторы в LPAE-таблицу (чужой
  формат, никогда не применялась); «дыр» в карте не было — L1 U-Boot затирал
  стек. Подробности ниже (r1202) и в `MEMORY.md`.
- **r1196 (фикс r1195):** r1195 перестраивал ВСЮ DRAM-карту (0x40000000..
  0x60100000) cacheable-секциями и обнулял TTBCR/TTBR1 — на стенде старт
  завис сразу после `remap: loaded` (v1195). Возвращена ТОЧЕЧНАЯ карта
  r1194: только sbrk-зона 0x4BF00000..0x4EF00000, TTBCR/TTBR1 не трогаются
  (модель памяти проекта — «один эмулятор, перезанимаемые пулы»,
  ARCHITECTURE.md: остальная карта U-Boot проверена 30 системами).
  В `mmu_map_dram_cacheable` добавлен clean+invalidate L1-таблицы
  (DCCIMVAC по 32-байтным линиям) до TLB invalidate: таблица 0x4A40C000 на
  этом шаге ещё cacheable, а аппаратный walker читает DRAM напрямую — без
  этого записи карты не доходили до walker'а (стенд r1194: M1 в L1 валидна,
  но F2=06 translation fault).
- **r1197 — ОТКАЧЕНО в r1198 (не прошивать):** попытка поставить TTBCR=0 /
  TTBR1=0x4A40C000 в startup.S сразу после переноса L1. На стенде загрузка
  умерла после `123` (строки DGN '1','2','3' есть, '4' нет): отказ в
  таблице/TLB, обработчик исключения сам не может работать (VBAR/стек на
  невалидной трансляции) → тихий hang. Вывод: **TTBCR/TTBR1 не трогать**
  (это системная настройка U-Boot, на которой работают все 30 систем).
- **r1198:** откат r1197 (startup.S восстановлен байт-в-байт к r1196) +
  диагностика карты sbrk-зоны в `emu_run_amiga` ДО первого выделения PUAE:
  по строке `sbrk-map: MB=<n> OK/BAD` на каждую 1-МБ секцию
  0x4BF00000..0x4EF00000 (write+read-back). По логу станет видно, какая
  секция реально недоступна (ожидание: либо все OK и падение позже — значит
  карту портит кто-то ещё; либо первая же BAD/abort — карта не применяется
  вообще). Точечная карта r1196 (0x4BF00000..0x4EF00000 + clean+invalidate
  L1) сохранена.
- **Памятка «что ломает систему» — `docs/PITFALLS.md`** (обязательна к
  прочтению перед любой правкой MMU/памяти; там разобраны r1195 и r1197).
- **r1199 (диагностика Amiga):** стенд r1198 показал, что точечная карта
  sbrk-зоны РАБОТАЕТ (`sbrk-map: 48/48 OK`), а падение переехало на
  `F1=0x467AB5F0` — это `.bss` (символ `puae_spixels`), при этом
  `L1[0x467]=0xFFFF7FFF` (мусор), `M1`/`M2` в abort-дампе тоже мусорные,
  хотя `.bss` обнулялась на старте. Гипотезы: порча L1-таблицы после boot
  (USB DMA / запись через кэш) или уход bump-указателя PUAE из sbrk-зоны
  (LIFO-free чужого указателя). Добавлено: `mmu_l1_dump(tag)` — сумма 4096
  дескрипторов + ключевые секции, вызывается в main после настройки MMU
  (`L1[boot]`) и в `emu_run_amiga` перед PUAE (`L1[amiga]`);
  `sbrk-map` сжат до сводки; в `puae_malloc` — печать первых 24 выделений
  ≥64 КБ и защита от brk вне 0x4BF00000..0x4EF00000; в `puae_free` — печать
  первых 16 LIFO-откатов. По логу сравнить суммы и найти момент порчи.
- **r1200 (расшифровка F2=0x06 + проверка кэша таблицы):** по ARM ARM
  DFSR FS=0b00110 = **Access flag fault, level 2** (short) — walker дошёл до
  L2 и увидел AF=0. Стенд r1199: `L1[boot]` и `L1[amiga]` ИДЕНТИЧНЫ
  (sum=4DD37314), но в DRAM-копии таблицы мусор
  `[400]=FFFFBFFD [420]=FFFFFFD7 [467]=FFFF7FFF`, а `[4BF]=4BF11C0E` (наш)
  верный — при том, что код из 0x40000000 работает. Добавлено: startup.S
  сохраняет TTBR0 загрузчика в SRAM 0x8B0; `mmu_l1_dump` читает дескрипторы
  ДО и ПОСЛЕ принудительного DCCIMVAC 16 КБ таблицы и печатает `pre`/`post`
  (+ `(CHANGED!)`) и дескрипторы оригинальной таблицы U-Boot.
- **r1201 (гипотеза LPAE — главная):** стенд r1200:
  `L1 pre == L1 post == (same)`, т.е. кэш и DRAM совпадают и в DRAM-копии
  таблицы по индексам секций ПУСТО (0xFFFFFFFF), а `uboot TTBR0=5FFF4000`
  тоже весь 0xFFFFFFFF (его уже затёр наш стек 0x60000000 — источник копии
  прочитать нельзя). При этом система работает и наш toчечный `[4BF]`
  лежит правильно. Вывод: **таблица 0x4A40C000 — НЕ short-descriptor**
  (иначе .text не исполнился бы). `TTBCR=0x80000F00` — валидный **LPAE**
  (бит31 EAE=1, T0SZ=0, IRGN0/ORGN0=WB), а `F2=0x06` в LPAE-кодировке
  DFSR = **Translation fault, level 2** (не «access flag»). Значит ядро
  работает в long-descriptor (LPAE), а все наши `mmu_mark_uncached`/
  `mmu_map_dram_cacheable` пишут 32-битные short-дескрипторы — ЧУЖОЙ формат
  (это же объясняет hang r1197: TTBCR=0 переключал на short, а таблица
  оставалась long). r1201 печатает проверку формата: `cpsr/sctlr/pfr1/mair/
  ttbcr` и 8-байтные записи по смещениям 8/16 (`e8/e16`, `lpae=`) — если
  `e8` валиден (бит0=1), формат LPAE подтверждён, и фикс должен писать
  **64-битные long-дескрипторы** (LPAE), а не short-секции.
- **r1202 (КОРЕНЬ найден и исправлен): LPAE + затёртые стеком L1-таблицы.**
  Стенд r1201: `ttbcr=80000F00` (EAE=1), `e8=00000000:5FFF1003` — валидная
  64-битная L0-запись, ссылается на **L1 по 0x5FFF1000**; `e16` -> 0x5FFF2000.
  Т.е. U-Boot работает в LPAE, а его L1-таблицы лежат в 0x5FFF1000/0x5FFF2000
  — ровно под нашим SVC-стеком (0x60000000, растёт вниз). Глубокие вызовы
  (PUAE/FAT) затирали L1 → случайные `Translation fault, level 2`
  (`F2=0x06` в LPAE-кодировке DFSR) по разным адресам: 0x4A800000,
  0x4C000000, 0x467AB5F0. Это объясняет ВСЕ прошлые «мистические» падения,
  включая r722-историю (тогда копировали таблицу, но для short-режима).
  FIX: `startup.S` копирует L0 в 0x4A40C000 и **все L1, на которые ссылается
  L0**, в 0x4A410000+ (до 4 таблиц), правит указатели в L0, затем TTBR0=копия
  и TLB invalidate. `mmu_mark_uncached` переписан на LPAE: 2-МБ L1-блок
  .coherent переводится на MAIR1-байт 4 = 0x44 (Normal Non-cacheable), чужие
  атрибуты MAIR не трогаются. `mmu_map_dram_cacheable` УДАЛЕНА (лечила
  симптом: «дыр» в карте не было — её затирал стек). TTBCR/TTBR0 не трогаем.
- **r1203 (фикс AP в LPAE):** стенд r1202 — загрузка падала на USB:
  `F1=0x4A400200` (HCCA в .coherent), `F2=0x0E` = **Permission fault,
  level 2**. Причина: `mmu_mark_uncached` ставил `AP[2:1]=0b11`, а в LPAE
  это **read-only** (0b01 = RW). Первая же запись USB в HCCA — отказ.
  FIX: AP/SH не трогаем вообще (оставляем как задал U-Boot, обычно AP=0b01),
  меняем только `AttrIndx=4` (MAIR1 байт 4 = Normal Non-cacheable) и `AF=1`.
- **r1204 (MAIR-индекс 4 опасен):** стенд r1203 — USB-энумерация сломалась
  на втором устройстве (`usb: cc setup=15 addr=0`, `get_dev_desc @0 fail`),
  затем `F1=00000003`, `F2=0x21` (LPAE: **Alignment fault** — обращение к
  адресу 3, следствие сбойной энумерации). Причина: MAIR1-байт 4, вероятно,
  использован U-Boot для периферии — регистры USB стали Normal NC вместо
  Device (потеря порядка). Правка атрибутов ВРЕМЕННО ОТКЛЮЧЕНА
  (`mmu_mark_uncached` — no-op), `.coherent` остаётся как у U-Boot (WB);
  когерентность держат cache_clean/invalidate в USB и SRAM-кольцо/индексы.
  Добавлен `mmu_l1_dump("early")` в начале main (ДО USB): печатает MAIR0/
  MAIR1 и реальные AttrIndx для 0x01C00000(периферия)/0x40000000/0x4A400000/
  0x4BF00000/0x5F800000 — по нему выбрать СВОБОДНЫЙ индекс и вернуть пометку.
- **r1205 (индекс NC найден):** стенд r1204 — загрузка полностью жива, USB и
  звук работают. `mair1=0x44E048E0` -> **индекс 7 = 0x44 = Outer NC / Inner NC
  уже есть в MAIR U-Boot**; периферия использует индекс 0 (Device), DRAM — 2
  (WB). FIX: `mmu_mark_uncached` ставит `.coherent` **AttrIndx=7** (только
  биты [4:2]), MAIR не переписывается, AP/SH/AF не трогаются. Так DMA-зона
  (OHCI HCCA/ED/TD, кольцо, флаги CPU2) реально non-cacheable без побочных
  эффектов на периферию. КОРЕНЬ падений Amiga (L1-таблицы под стеком)
  исправлен ещё в r1202 — теперь надо проверить запуск ADF на стенде.
- **r1206 (.coherent остаётся write-back):** стенд r1205 — та же поломка USB,
  что и в r1203 (`cc setup=15` -> Data Abort addr=3), т.е. рушит не MAIR, а
  сам перевод `.coherent` в non-cacheable (AttrIndx 7). Вывод: на этом ядре
  OHCI-драйвер совместим только с WB-маппингом. Важно: с переходом U-Boot на
  LPAE прежние short-дескрипторные «uncached» записи НИКОГДА не применялись —
  система всё это время жила с `.coherent` WB, и USB/звук/игры работали.
  FIX: `mmu_mark_uncached` — намеренный no-op (развёрнутое обоснование в
  h3.c). Когерентность держат: `cache_clean/cache_invalidate` в USB,
  кольцо/индексы в SRAM A1 (r957/r929), SRAM-дубли флагов (0x90/0x94).
  Карта LPAE (копия L0+L1) из r1202 сохранена — корень падений Amiga закрыт.
- **r1207 (диагностика зависания Amiga):** стенд r1206 — abort'а НЕТ,
  `sbrk-map: 48/48 OK`, аллокации PUAE идут до `4C4527F0` (chipmem 1.5 МБ),
  дальше **зависание, экрана нет** (подтверждено владельцем). Добавлено:
  маркеры `Amiga: init…` / `Amiga: init ok` / `Amiga: load…` вокруг
  `puae_retro_load_game` и трассировка первых 80 `environ`-команд в
  `host_environment` (через `uart0_printf`) — по логу видно, на каком env
  ядро встало. Сборка OK (34295084 б), на стенд ещё не прошивалась.
- **r1208 (документация памяти):** создан `docs/MEMORY.md` — полная карта DRAM
  (разделы, адреса, размеры сверены с `readelf/nm`), SRAM A1, периферия,
  устройство LPAE-карты (TTBR0/L0/L1, MAIR, AttrIndx), владельцы пулов и
  перекрытия (ZIP_BUF1 92 МБ накрывает `.vice_bss`/`.fs_bss` — допустимо, т.к.
  аркады не используют `fs_layer`, одновременного запуска нет), чек-лист для
  новых ядер. Исправлены устаревшие комментарии: `linker.ld` (`.coherent` —
  WB, не uncached; sbrk start 0x4BF00000 вместо 0x4B700000) и шапка MMU в
  `h3.c` (описывала удалённую NC-логику). Код не менялся.
- `.noinit_fbn` 0x54000000 (zlib/fbn, ASSERT ≤0x5C000000); `.vice_bss`
  0x52000000; `.fs_bss` 0x53E00000; `.a8_bss` 0x5F000000; `EMU_FB` 0x5F800000;
  FB back 0x5F400000; ZIP_BUF2 0x5C000000.

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
- **Amiga: зависание после `init`** (r1206: abort'а нет, PUAE аллоцирует до
  `4C4527F0`, экрана нет). Прошить **r1207/r1208**, снять лог: последний
  `PUAE env N …` + какой маркер `Amiga:` напечатан последним → там и встало
  (`init` = создание ядра, `load` = `puae_retro_load_game`).
- Файл с SD-образами для Amiga — только ADF (Kickstart вшит).
- **Карта памяти и правила размещения — `docs/MEMORY.md`** (обязательно перед
  добавлением секций/буферов/ядер; там же LPAE-устройство и свободные зоны).
- `docs/HANDOVER_-r1192.md` — история дельт r849..r1192 (референс).