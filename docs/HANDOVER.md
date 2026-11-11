# HANDOVER — текущее состояние (r1227)

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
- Версия: `g_fw_version` и `build:` в `h3_bare/src/main.c`. Текущая — **r1227**.
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
- **r1209 (трассировка старта PUAE, ВРЕМЕННО):** стенд r1208 — тот же висяк:
  `Amiga: load…` → выделения 512K/1M/512K, два `fopen miss '/rom.key'`, затем
  560041/1M/512K×3/1.5 МБ (brk `4C5D27F0`) и тишина. Добавлено (снять после
  отладки): маркеры `RL: enter/config/umain ok/first pass/end` в
  `libretro/libretro-core.c` `retro_load_game`; маркеры `M2: <этап>` в
  `sources/src/main.c` `real_main2` (graphics_setup, machdep_init, setup_sound,
  inputdevice_init, target_run, savestate_init, memory_reset, custom_init,
  DISK_init, init_m68k, gui_update, graphics_init, init_audio); включён
  встроенный `write_log` PUAE (`uaeunp.c`, лимит 400 строк — раньше `debug=0`
  и старт ядра шёл молча); в host — трасса env фазы загрузки (`PUAE envL N`,
  без повторов SET_VARIABLES). По последнему маркеру видно, в каком этапе
  встало: нет   `M2:` вообще — внутри `target_run()`; после `target_run` — в
  savestate/memory/custom/DISK/graphics/audio.
- **r1210 (углублённая трассировка, ВРЕМЕННО):** стенд r1209 дал точку:
  `M2: DISK_init` есть, `M2: init_m68k` НЕТ → виснет в `init_m68k`
  (`reset_frame_rate_hack` пуст, `m68k_speed>=0`). Последнее выделение
  1572864 Б = `65536 * sizeof(struct instr)` из `readcpu.c:966`
  (`init_table68k`), т.е. дошли до построения таблиц 68k и встали в
  `build_insn`/`do_merges`/`build_cpufunctbl`. Найдено и исправлено:
  `write_log` в сборке — пустая заглушка из `gencpu.c` (`uaeunp.c`, где
  рабочая версия, в объекты НЕ входит), поэтому лог ядра всегда молчал;
  теперь заглушка пишет в UART (лимит 400 строк). Добавлены: `NC: <шаг>` в
  `init_m68k`/`build_cpufunctbl`, `RT: free/malloc/init-loop/defs=N/insn N/
  merges/done` в `readcpu.c init_table68k` (индекс инструкции при каждом
  `build_insn`), `ret=%08X` (адрес возврата) в лог крупных `puae_malloc` —
  по нему `addr2line`/`nm` даёт точное место выделения. Правка `uaeunp.c`
  откачена (файл не компилируется).
- **r1211 (причина «тишины» найдена):** стенд r1210 — последняя строка
  `RT: insn 0`, т.е. виснет в **первом же** `build_insn(0)` (defs68k[0] =
  `ORSR.B #1`). Ключ: `abort()` в прошивке — `for (;;) {}` из
  `h3_bare/cores/coleco_compat.c:11` (LD-заглушка под libgcc/libstdc++),
  поэтому любой `abort()` в ядре = немое зависание без дампа и без лога.
  Добавлено (r1211, временно): все `abort()` в `readcpu.c` заменены на
  `RT_ABORT()` — печать `RT: ABORT line N` и остановка; маркеры `b: op=…`,
  `b: mn=…`, `b: src pos/mode`, `b: eol pos/find`, `b: end` внутри
  `build_insn`. По логу: либо номер строки abort (тогда виден дефект
  разбора), либо место, где реально крутится цикл.
  Отдельный вывод для проекта: `abort()` стоит сделать печатающим
  (адрес возврата) — сейчас он скрывает любые падения во всех ядрах.
- **r1212 (штатная диагностика вместо заплаток):** все временные трассы
  удалены (M2/RL/envL/NC/RT/b:/ret=), вендорные файлы PUAE возвращены к
  исходному виду.
  - **Единый `panic()`** в `h3_bare/src/libc_min.c`: печатает
    `PANIC: <причина> code=<n> ret=<адрес возврата>` и останавливает ядро
    (`wfi`). Через него теперь идут `abort`, `exit`, `_exit`, `_Exit`, `_kill`,
    `__assert_func` (последний печатает ещё файл:строку и выражение). Глухие
    заглушки `abort()/_exit()/_kill()` удалены из `coleco_compat.c` — это и был
    источник «немых» зависаний (PUAE r1210: висяк в `build_insn` без следа).
    Адрес возврата разворачивается в строку:
    `arm-none-eabi-addr2line -f -e build/h3_bare.elf <ret>`.
  - **Логи ядер:** порог вывода один — `int g_core_log_min` в `emu.c`
    (0=DEBUG…3=ERROR; по умолчанию **2/WARN**). `c64_host.c:c64_log` и
    `puae_host.c:puae_log` читают его. У PUAE раньше стоял жёсткий
    `lvl < RETRO_LOG_WARN → return`, поэтому INFO-сообщения ядра (старт CPU,
    диск, память) не печатались вовсе — теперь `emu_run_amiga` поднимает порог
    до INFO на время запуска и возвращает WARN при выходе. Для VICE INFO
    по-прежнему нельзя включать по умолчанию (r987: блокирующий UART тормозил
    эмуляцию), но аварии VICE (abort/assert) с этого релиза видны.
  - Прошлые стенды (r1209/r1210) показали: виснет первый же `build_insn(0)`
    (`RT: insn 0`, `defs68k[0]` = `ORSR.B #1`). Если причина — `abort()`,
    r1212 напечатает его строку; если снова тишина — это цикл, и тогда нужен
    точечный пробник именно там (после отладки удалить).
- **r1213 (КОРЕНЬ падения Amiga: нулевая `_ctype_`):** стенд r1212 дал
  `puaeI# Internal error; file …/readcpu.c, line 733` + `PANIC: exit code=0
  ret=4107875C`. Причина: в `cores/fceumm/libretro_compat.c` лежала заглушка
  `const char _ctype_[256] = { 0 };` («линкуем -nostdlib»). Она удовлетворяла
  линкер, настоящая таблица не подключалась (libc не линкуется вовсе), поэтому
  `isspace(' ')` возвращал 0 **во всей прошивке**. Парсер `build_insn` не видел
  пробел, складывал в `TCHAR mnemonic[10]` всю строку `"ORSR.B  #1"` (переполнение
  буфера), мнемоника не находилась в `lookuptab` → PUAE-шный `abort()`
  (`sysdeps.h` подменяет его на `write_log("Internal error…") + exit(0)`) →
  наш `panic`. FIX: одна newlib-совместимая `_ctype_[257]` в
  `h3_bare/src/libc_min.c` (индекс 0 = EOF, флаг символа `c` по индексу `c+1`);
  заглушка удалена. Проверено по образу: `space=0x88, '0'=0x04, 'A'=0x41,
  'a'=0x42, DEL=0x20, TAB=0x28`.   Это же чинит ctype-зависимый разбор в VICE
  (C64/C128), FBNeo, FCEUmm, mGBA — «та же проблема в Коммодоре» была реальной.
- **r1214 (логи, форматтер, видео Amiga):**
  - **vsnprintf: добавлены `%f`/`%e`/`%g`** (`fmt_double` в `libc_min.c`, без
    libm: знак, целая часть, prec цифр, округление; `|v|>=1e19` → `inf`),
    флаги `-`/`+` и точность `.N`; `%s` понимает `%.Ns` как предел длины.
    Причина: без `%f` аргумент-double НЕ забирался из va_list, и все следующие
    конверсии читали чужие значения — отсюда мусор в логах PUAE
    (`V=.4fHz`, `IDX=… (▒▒▒)`, `Reset at 0▒F…`, склейки строк).
  - **Логи ядер свернуты:** `g_core_log_min` (emu.c) по умолчанию 2/WARN,
    временный подъём до INFO в `emu_run_amiga` убран. Счётчики диагностики
    аллокатора PUAE (`alloc`≥64 КБ, `free rewind`) вынесены в файл и
    сбрасываются в `puae_heap_reset` — лимит теперь на сессию, а не на всё
    время работы (раньше после первого запуска часть лога пропадала).
  - **Видео Amiga без двойного масштаба:** новый `emu_scale_rgb565_src`
    (emu.c/emu.h) — nearest-скейл «сырого» RGB565-кадра произвольной
    геометрии (320×256 / 640×256 / 640×512, с pitch и полями) сразу в HDMI FB.
    `host_video` PUAE больше не ужимает кадр в EMU_FB 320×240: именно это давало
    «сдвиг растра» на чересстрочных кадрах и лишний проход с делением на
    каждый пиксель. Геометрия стабильна: 320×256 и 640×512 дают одинаковые
    750×600 по центру, поля — цветом `g_border_color` или чёрным. `emu_scale`
    отрефакторен на общий `scale_rgb565_to_fb` (поведение прежнее: поля
    заливаются только при заданном `g_border_color`), цикл `emu_run_amiga`
    делает только `fb_flush`.
  - Открытые вопросы Amiga: скорость (интерпретатор 68000 без JIT — JIT в
    `retrodep/sysconfig.h` закомментирован, jit/*.c не входят в сборку) и
    кадры с чересстрочной геометрией — проверять на стенде после r1214.
- **r1215 (диск DF0, ограниченная проба логов, геометрия кадра):**
  - **Вставка диска:** после `retro_load_game` host вызывает
    `puae_retro_disk_set_image_index(0)` и `puae_retro_disk_set_eject_state(false)`
    — это штатный контракт libretro. В ядре
    (`libretro-core.c: retro_disk_set_eject_state`) вставка идёт через
    `changed_prefs.floppyslots[0].df` + `DISK_reinsert(0)`; раньше host полагался
    только на путь в конфиге, и AROS показывал «insert disk» (DF0: не смонтирован).
  - **Ограниченная проба логов:** `g_diag_budget` (puae_host.c) — на старте
    сессии порог поднимается до INFO, но не более чем на 120 строк, дальше
    автоматически возвращается WARN. Видно вставку диска и режимы экрана, UART
    не заливается, эмуляция не тормозит.
  - **Геометрия кадра:** одна строка на сессию `Amiga video: WxH pitch=N`
    (`host_video`) — по ней видно, что именно отдаёт ядро (320×256, 640×512,
    поля/pitch), чтобы точно поправить «сдвиг вправо».
  - `#include <stdio.h>` перенесён в шапку `puae_host.c` (printf используется в
    `host_video` до блока POSIX-стабов).
- **r1217 (геометрия кадра + замер производительности):** кроп ядра
  (`puae_crop`) оказался непригоден: с ним ядро отдаёт **320×200 при
  pitch=1440** — окно уже полной строки, на экране видна только часть кадра
  («растр больше экрана»). Кроп выключен; overscan режем сами в
  `emu_scale_rgb565_src`: кадр 720×574 → активные 640×512 по центру (поля 40
  по X, 31 по Y) → 750×600 на экране, без сдвига вправо/вниз.
  Добавлен замер в цикле Amiga: на выходе печатается
  `Amiga perf: fps=… core=…ms video=…ms` (ядро vs скейл+fb_flush) — по нему
  видно, что именно ограничивает скорость.
  Состояние диска на стенде r1216: `Amiga fs: stat=1 size=901120` (файл виден
  fs-слою), но `Amiga disk: mount=0` — ядро не положило контент в свой список
  дисков (`dc->count == 0`), поэтому AROS просит носитель; разбирать
  `path_is_valid()`/`dc_add_file` в ядре.
- **r1218 (режим CPU PUAE):** по умолчанию ядро собирало 68000 в режиме
  «More compatible» — самый медленный интерпретатор (`Building CPU, 44274
  opcodes (0 1 1)`). Host отдаёт `puae_cpu_compatibility="normal"`
  (стало `(0 0 1)`) и `puae_immediate_blits="immediate"`.
- **r1219 (единый форматтер):** `printf`/`uart0_printf` (`src/printf.c`) теперь
  сами зовут `vsnprintf` — до этого у них был свой разбор, и `%f` в строке
  `Amiga perf` печатался литералом (`fps=%.1f`).
- **r1220 (повторная вставка диска):** ядро применяет `chan`-настройки диска
  только при `libretro_runloop_active`, поэтому host повторяет
  `eject(false)` после первого кадра и печатает `Amiga disk retry: mount=N`.
- **r1221 (JIT — отказ, откачено в r1222):** `#define JIT` в
  `retrodep/sysconfig.h` + `-DJIT` + объекты `jit/compemu*` не собираются:
  `traps.h`/`memory.h` ждут `uae_u32`-типы из полного билда, `compemu_support`
  — C++. Vendor-порт libretro JIT не поддерживает; для скорости нужен реальный
  порт, а не флаги.
- **r1223 (currprefs диска):** в `retro_disk_set_eject_state` после записи в
  `changed_prefs.floppyslots[0].df` добавлено копирование в
  `currprefs.floppyslots[0].df`: привод при вставке читает именно currprefs,
  а `DISK_reinsert()` только извлекает диск и помечает смену. Стенд: retry
  даёт `mount=1`, но AROS всё равно просит носитель.
- **r1224 (счётчики чтений fs):** `g_fs_read_ops`/`g_fs_read_bytes` в
  `cores/fs_layer.c` (`fread` и fd-`read`), на выходе печатается
  `Amiga disk reads=N bytes=N` — отличает «привод не читает образ» от
  «образ читается, но не признан загрузочным».
- **r1225 (вшитый Kickstart 1.3 + второй файл в fs-слое):**
  - Владелец положил эталоны в `emulator-refs/bios` (`kick12/13/20/30/31/40.rom`
    по 512 КБ). `kick13.rom` — две побайтно одинаковые половины по 256 КБ,
    MD5 первой `82a21c1890cae844b3df741f2762d48d` = **Kickstart 1.3 rev 34.5
    (A500)**. Из первой половины собран `h3_bare/cores/puae/bios/kick34005.A500`
    (262144 б) и вшит: `h3_bare/cores/puae/kickstart_data.S` (`.incbin`,
    символы `puae_kick13_rom`/`_end`), правило — `Makefile`
    (`build/puae_kickstart_data.o`, `$(AS) -I$(PUAE_ROOT)`).
  - `cores/fs_layer.c`: **второй файл** слоя — `fs_register_bios()` +
    `path_matches_bios()` + `fs_source()` (какой буфер отдать: образ или BIOS).
    Через `fs_source` работают `fopen`/`stat`/`access`/`open`; у fd-доступа
    свой буфер на дескриптор (`g_fd_data[]`/`g_fd_size[]`), иначе `read`/`lseek`
    читали образ. Мост — `a8_fs_register_bios()` в `cores/atari800/a8_fs.c`.
  - `cores/puae_host.c`: `GET_SYSTEM_DIRECTORY` отдаёт `"."` (было `false`),
    `puae_kickstart="kick34005.A500"` (было `"aros"`), Kickstart регистрируется
    до `retro_load_game`. Ядро склеивает `"." + "/kick34005.A500"`, проверяет
    через `stat` (слой отвечает размером 262144) и пишет
    `kickstart_rom_file=./kick34005.A500` в конфиг; AROS остаётся значением
    `"aros"`.
  - Образ вырос на 262 КБ: 34 553 964 б (r1224 — 34 291 308 б). Блоб в
    `.data` (`puae_kick13_rom`, `0x40000`; адрес символа плавает с порядком
    объектов — см. `docs/MEMORY.md`).
- **r1226 (диагностика путей fs):** стенд r1225 дал `fs: bios … size=262144`
  и `Amiga fs: stat=1`, но ядро печатало `Kickstart ROM "kick34005.A500"
  not found!`. Добавлены промахи `fs: stat miss '<путь>'` (до 8 за сессию) и
  проба в host: `Amiga fs: kick plain=<ok>/<size> dotted=<ok>/<size>`.
  Стенд r1226: проба `plain=1/262144 dotted=1/262144` (слой отвечает верно),
  при этом ядро зовёт `stat` только для `./uae_data`, `.`, `./uae`, `/TEMP`,
  а путь Kickstart до `stat` не доходит вовсе.
- **r1227 (КОРЕНЬ «not found» и «insert disk»: заглушка `path_is_valid`):**
  в прошивке единственным определением `path_is_valid` был стаб
  `cores/fceumm/libretro_compat.c` (`return 0`), и из-за
  `--allow-multiple-definition` он перекрывал настоящие реализации
  libretro-common у ВСЕХ ядер. Ядра проверяют существование файла именно этой
  функцией, поэтому PUAE не находил Kickstart, а диск DF0 не проходил проверку
  пути (`mount` без чтения, `Amiga disk reads=0`); та же ложь работала и для
  образов других систем. FIX: стаб удалён, одна реализация —
  `cores/fs_layer.c: path_is_valid()` (`stat` слоя + ненулевой размер).
  В сборке символ единственный (`build/fs_layer.o`), `linkcheck: OK`.
  Образ r1227 — 34 554 412 б.

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
- Видео: `emu_scale_rgb565_src` — RGB565-кадр ядра (720×574 и др., с полями и
  pitch) масштабируется сразу в HDMI FB, EMU_FB 320×240 не используется;
  overscan режется вручную (640×512 по центру). Аудио 44100→48000 стерео;
  ввод USB-клава (HID→RETROK) + Sega-пад; environ
  `puae_kickstart="kick34005.A500"` (вшитый Kickstart 1.3 rev 34.5, `.incbin`
  `cores/puae/bios/kick34005.A500`; AROS остаётся значением `"aros"`, с SD
  ничего не грузится); `puae_model="auto"`, `puae_cpu_compatibility="normal"`.
- Ромы: `/roms/amiga/*.adf` (id="amiga"). ADF виден fs-слою
  (`stat=1 size=901120`), но ядро не монтирует его в DF0 — открытый вопрос
  (см. r1223/r1224 в дельтах).

## 6. Известные ограничения / TODO

- **32X требует стенда** (DRC на H3): BC Racers, Star Trek — нет `U`-падения
  после r1172; `32X diag` fps/масштаб.
- **Amiga: диск DF0 не монтируется** (`Amiga disk retry: mount=1`, но AROS
  просит носитель). С r1224 на выходе печатается `Amiga disk reads=N bytes=N`:
  `0` — привод не читает образ (путь/currprefs), `>0` — образ читается, но не
  признан загрузочным. Скорость заморожена владельцем (интерпретатор без JIT,
  `core≈33 мс`, в Windows-сборке так же медленно).
- Файл с SD-образами для Amiga — только ADF (Kickstart вшит).
- **Карта памяти и правила размещения — `docs/MEMORY.md`** (обязательно перед
  добавлением секций/буферов/ядер; там же LPAE-устройство и свободные зоны).
- `docs/HANDOVER_-r1192.md` — история дельт r849..r1192 (референс).