# HANDOVER — текущее состояние (r1742)

> Исторические ссылки на `docs/AUDIT/*` и старые `docs/AUDIT_2026-*.md` —
> архив: файлы удалены, актуальный послойный отчёт —
> `docs/AUDIT_2026-11-27_LAYERS.md`.

## r1742 — Sega System 16B: stage-таймеры кадра

- `sys16_run.cpp`: `g_s16_us_cpu/snd/mcu/draw`; в `System16BFrame` измеряются
  68000 (`SekRun`), Z80+YM2151/MSM6295/UPD7759, mcs51+`Simulate8751`,
  `BurnDrvRedraw` (`System16BUpdate`).
- `cps1_host.cpp`: суммы идут в `PROF` (`run` = 68000, `dcs` = MCU),
  при `g_sega` печатается `S16: perf cpu … snd … mcu … draw …`.
- Цель — понять, что даёт 19–22 мс на Goldnaxe; оптимизацию не делаем до цифр.
- Сборка: Musashi `26 339 764 Б`, Cyclone `26 777 100 Б`, `linkcheck: OK`.

## r1741 — сборка из клона: TOS в репозитории + гонка m68kmake

- `.gitignore`: исключение для `h3_bare/cores/atari_st_cw/bios/etos192us.img`
  (196 608 Б). Без него `tos_data.S` не собирался из свежего клона.
- `Makefile`: `m68kops.c`/`m68kops.h` — grouped target `&:`; иначе `-j20`
  запускал `m68kmake` дважды и два процесса писали одни файлы.
- Проверено чистой локальной сборкой: Musashi `26 338 356 Б`,
  Cyclone `26 775 628 Б`, `linkcheck: OK`.
- Свежий клон `de5190a` собирается (`linkcheck: OK`), но его бинарник на
  3 840 Б больше и адреса `.rodata/.data/.bss` сдвинуты. Причина ещё не
  найдена; на запуск не влияет, разобрать отдельно.

## r1740 — чистка логов после DCS-SMP и CPU1-видеоконвейера

- Удалён `DCS: local core0 (smp removed)`; `dcs_host_setup()` больше не печатает.
- Удалены логи `ARC: vid off (slow/stall)`, `ARC: vid abrt`, `ARC: vid CPU1,
  worker` вместе со статистикой `g_vidw_us_sum/cnt`. Сам воркер остаётся
  выключенным (`k_vidw_enable=0`), код не тронут.
- `CPS: perf` печатает `emu` и `render`; `snd`/`draw`/`68k` — только если
  реально измерены (ненулевые). Поля `run/scan/dcs` (этапы DCS-SMP) из
  человекочитаемой строки убраны, в `PROF` остаются для парсера.
- Default Musashi: `linkcheck: OK`, `h3_bare.bin` = 26 338 356 Б.
- Cyclone (`ARC_CYCLONE=1`): `linkcheck: OK`, `h3_bare.bin` = 26 775 628 Б.
  На стенде не проверялось — изменены только логи.

## r1739 — Cyclone: убран byte-swap shadow (r1738 давал чёрный экран)

- Симптом r1738 на стенде: DINO загружается (16/16 ROM, `init OK`, 59.6 fps),
  но игра не стартует — чёрный экран.
- Причина: FBNeo при `-DLSB_FIRST` хранит 68k-слова в **native-endian**
  (little-endian), `BURN_ENDIAN_SWAP_*` — тождественны, ROM-буфер уже готов к
  прямому `ldrh`. fetch-shadow из r1738 повторно переставлял байты;
  `C68KDoReset` читал reset-вектор из переставленной тени и получал битый
  начальный PC (попадал в середину данных вместо кода) → CPU исполнял мусор,
  видео CPS-1 не инициализировалось.
- Исправление: `c68k_fetch_shadow` + `C68KShadowPage/Update/Free` и их вызовы
  в `m68k_write*`/`SekExit` удалены; `SekMapMemory` снова мапит fetch напрямую
  (`pMemMap[SEK_WADD*2] = Ptr + i`), как в upstream libretro `c68000_intf.cpp`.
  `m68k_checkpc`/trap-страница остаются как в upstream.
- Версия r1739: `linkcheck: OK`, `h3_bare.bin` = 26 775 948 Б; в ELF есть
  `c1m68k_checkpc` и `CycloneRun`. На стенде — проверить.

> r1738 ниже — историческая запись; её fetch-shadow отменён r1739.

## r1738 — Cyclone 68000: рабочий Sek-адаптер + fetch-shadow + флаг ARC_CYCLONE

- **c68000_intf.cpp** (libretro-FBNeo, Sek-совместимый) вендорен в
  `h3_bare/cores/cps1/src/cpu/`; `Cyclone.S` (1.85 МБ, сгенерированный) — в
  `src/cpu/cyclone/`. Сборка Cyclone по умолчанию **не** используется.
- **Fetch-shadow:** Cyclone исполняет опкоды напрямую и требует
  byte-swapped память. FBNeo хранит 68k-память big-endian, поэтому каждая
  fetch-страница получает swapped-копию (`C68KShadowPage`), а записи
  обновляют её (`C68KShadowUpdate`); данные по-прежнему идут через
  `SekRead*/Write*`. Тени освобождаются в `SekExit`.
- **Makefile:** `ARC_CYCLONE=0` (по умолчанию) — Musashi;
  `ARC_CYCLONE=1` — `c1i_c68000_intf.o` + `c1cyc_Cyclone.o` (+
  `cps1_rename.sh`, чтобы `m68k_*` совпали с Cyclone.S).
- **m68k_core.h:** флаг варианта ядра; при `ARC_CYCLONE=1` — Cyclone.
- **Ограничения пилота:** только 68000 (Psikyo 68EC020 — Musashi); игры,
  исполняющие код из RAM без `MAP_FETCH`, уходят в trap-страницу; крупные
  ROM-тени Neo Geo/CPS-2 могут не влезть в пул FBNeo; на стенде не проверено.
- Сборка Musashi (`make -j20`): `linkcheck: OK`, `h3_bare.bin` = 26 338 676 Б.
- Сборка Cyclone (`make ARC_CYCLONE=1 -j20`): `linkcheck: OK`,
  `h3_bare.bin` = 26 775 948 Б; в ELF есть `c1m68k_checkpc` и `CycloneRun`.
  На стенде не проверялась; текущий `h3_bare.bin` — Cyclone-вариант.

## r1737 — Cyclone 68000: вендоринг исходников + флаг сборки (Musashi по умолчанию)

Пилот утверждён владельцем: флаг + откат на Musashi.

- **Вендоринг:** `h3_bare/cores/cps1/src/cpu/cyclone/` — `notaz/cyclone68000`
  (HEAD `fe89d4e`), 15 файлов: генератор (`Main.cpp`, `Ea.cpp`, `Op*.cpp`,
  `App.h`, `config.h`), его `Disa.c/h`, `Cyclone.h`, `Cyclone.txt`, лицензии
  (GPLv2 / MAME). Бинарников и `Cyclone.s` в дереве нет.
- **Cyclone.s собран и проверен, но НЕ коммитится:** генератор `cyclone_gen`
  собирается нативным `C:\msys64\ucrt64\bin\gcc` (16.2.0), выдаёт 1.7 МБ
  ассемблера; `arm-none-eabi-gcc -mcpu=cortex-a7 -marm -c` его принимает
  (объект 460 КБ). Правило «GitHub не помойка» — генерируемый артефакт
  держим вне репозитория; сборка ядра будет генерировать его на месте.
- **m68k_core.h (новый):** `ARC_CYCLONE=0` по умолчанию (Musashi).
  При `ARC_CYCLONE=1` — `#error` с указанием на `docs/ARCADE_PLAN.md §6.1`:
  адаптер памяти Musashi→Cyclone ещё не написан, поэтому включение флага
  останавливает сборку явно, а не ломает линковку.
- **m68000_intf.cpp:** подключён `m68k_core.h`; логика Musashi не менялась.
- **Не изменялись:** `i2s.c` (звук/I2S/CPU2), `tft_drv.c` (TFT).
- Сборка: `make -j20` → `linkcheck: OK`, `h3_bare.bin` = 26 338 676 Б,
  метка `r1737`.

## r1736 — профилировщик аркад: строка PROF + парсер + матрица

- **cps1_host.cpp:** раз в 120 кадров рядом с `CPS: perf`/`IRM: perf`
  печатается машинная строка
  `PROF,<sys>,<game>,<fps*10>,<emu>,<run>,<scan>,<dcs>,<snd>,<draw>,<render>,<68k>`
  (средние мкс/кадр). Для Irem `run` = CPU M72; для Midway `run/scan/dcs` —
  TMS34010/сканлайн/DCS.
- **tools/arc_profile.ps1:** парсер UART-лога — сводка по системе/игре,
  `Headroom_us`, `Load_pct`, `Bottleneck`; `-Csv` для выгрузки, `-SkipFirst`
  (по умолчанию 1) отбрасывает прогрев.
- **docs/ARC_PROFILE.md:** поля, порядок снятия, матрица систем/игр (по 1–3
  игры), критерии приёмки итерации.
- Сборка: `make -j20` → `linkcheck: OK`, `h3_bare.bin` = 26 338 676 Б,
  метка `r1736`; парсер проверен на тестовом логе (3 системы, сводка/CSV).

## r1735 — DCS Midway: удалён вынос DSP на CPU1 (локальный core0)

По решению владельца многоядерный DCS признан тупиковым. Удалён **только**
вынос ADSP2100 на CPU1; звук Midway остаётся, I2S/CPU2 и TFT не тронуты.

- **cps1_host.cpp:** убраны DCS-почта (`g_dcs_in/out`), SMP-состояние,
  ожидания/таймауты и `dcs_worker_step()`; `dcs_host_*` снова исполняют DSP
  локально на core0. В `arc_vid_worker_loop` шага DCS больше нет.
- **dcs2k.{cpp,h}:** удалены `Dcs2kFlushToWorker`, `Dcs2kFlushOutput`,
  `Dcs2kFlushMixer`, `Dcs2kInvalidateWorker`, `DcsCheckIRQWorker`,
  `Dcs2kDebugGet`; `DcsCheckIRQ` остаётся для локального пути.
- **adsp2100_intf.cpp:** удалены `Adsp2100FlushToWorker`/`InvalidateWorker`.
- **midtunit.cpp:** убраны `dcs_host_wait()` и `Dcs2kFlushOutput()`.
- **Не изменялись:** `i2s.c` (I2S/CPU2), `tft_drv.c` (TFT).
- Сборка: `make -j20` → `linkcheck: OK`, `h3_bare.bin` = 26 338 356 Б;
  на стенде не прогонялось.

## r1734 — M-01 (.ep_bss), M-02/M-03 (MEMORY), H-01/H-05 (доки), H-02 (build.log)

Шестой пакет плана из `docs/AUDIT_2026-11-27_LAYERS.md` §3 (пункты 12–13).

- **linker.ld / startup.S — M-01:** удалена пустая `.ep_bss` (SIZE 0,
  единственная NOLOAD-PROGBITS) и её no-op цикл обнуления. EP-кадр `ep_rgb32`
  (884 КБ) живёт в `.noinit_fbn` (0x5517B900 на r1733) — комментарии/ASSERT
  исправлены.
- **MEMORY.md — M-02:** строка «свободно 0x4479D520..0x4A400000 ≈92 МБ» была
  неверна: ZIP_BUF_BIG начинается с 0x44C00000 (≤88 МБ) и перекрывает зону;
  свободно только от `_bend1` до `0x44C00000`.
- **MEMORY.md — M-03:** `.noinit_fbn` ≈127 МиБ; пул `pool_mem` (0x55343900,
  0x06C00000 = 108 МиБ) — лишь часть секции, а не вся она.
- **ARCHITECTURE.md — H-05:** карта памяти приведена к r1733 (text/rodata/data/
  bss/noinit/bin), убрано ложное «`.ep_bss` отдельно», битая ссылка на
  `AUDIT_2026-10-29_r848_FIXPLAN.md` заменена на `MEMORY.md` §3.
- **VENDOR.md — H-01:** снята битая ссылка на `docs/AUDIT/SUMMARY.md`;
  в HANDOVER добавлена пометка об архивных ссылках.
- **H-02:** `build.log` (87 МБ, untracked) вынесен в temp (логи сохранены);
  `503III/` оставлен — SHA256 дампов не совпадает с `fMSX/ROMs` (не дубли).
- Сборка: `make clean && make -j20` → `linkcheck: OK`,
  `h3_bare.bin` = 26 341 508 Б, метка `r1734`; `.ep_bss` в `readelf` отсутствует.

## r1733 — D-04..D-07: мнимое R/W (Fuse, Frodo, UAE4All, EP)

Пятый пакет плана из `docs/AUDIT_2026-11-27_LAYERS.md` §3 (пункт 7) —
единый принцип: `fs_layer` отдаёт `fopen("r+b")`, но `fwrite` всегда 0, поэтому
ядро не должно считать образ записываемым.

- **fuse/disk.c — D-04:** `disk_open2()` больше не пробует `r+b`:
  `d->wrprot = 1` всегда. `compat_file_write` уже возвращает ошибку (fwrite=0),
  `utils_write_file` её видит; не хватало только честного wrprot — eject не
  предлагает «сохранить» и не теряет изменения молча.
- **frodo/1541d64.cpp — D-05:** после открытия образа принудительно
  `write_protected = true` (BAM/сектора не «пишутся» в никуда).
- **uae4all/disk.cpp — D-06:** `DISK_validate_filename()` ставит `wrprot=1` и
  при успешном `zfile_open("r+b")`; `DISK_ersatz_read()` не разыменовывает
  пустой `floppy[0].diskfile`. `drive_insert` оставлен как есть: успех=1,
  отказ=0, и оба вызывающих (`:846` `? 0 : -1`, `:1519` `if (!drive_insert)`)
  трактуют 0 как отказ — смена на -1 сломала бы `:846`.
- **ep128emu/cpcdisk.cpp — D-07:** убран сброс `writeProtectFlag = false` после
  `fileOpen("r+b")` — флаг остаётся true (write-protect), `writeSector` не
  рапортует ложный успех.
- Сборка: `make -j20` → `linkcheck: OK`, `h3_bare.bin` = 26 341 508 Б,
  метка `r1733`.

## r1732 — D-01 (fat_scratch в F12), D-09 (PXT readonly), D-10 (overflow HDD)

Четвёртый пакет плана из `docs/AUDIT_2026-11-27_LAYERS.md` §3 (пункты 5–7).

- **disk_swap.c — D-01:** выбранная запись копируется в `fat_entry_t sel` до
  `fat_find()`, который перезаписывает общий `fat_scratch()` (там же лежал
  `list`). Имя/путь теперь берутся из копии, а не из затираемого списка.
  В текущем коде ветки отказа (`Read failed`/`Insert failed`/`File not found`)
  делают `return 0`, поэтому перерисовки чужого каталога после `fat_find` нет —
  правка устраняет саму зависимость от scratch (защита от будущих правок).
- **disks-h3.c.inl — D-09:** `pxt_disk_set()` ставит `readonly = 1` для
  RAM-образа A:. INT13h AH=03 писал в `ROM_BUF`, на SD образ после выхода
  оставался прежним — запись «терялась». Теперь честный write-protect
  (AH=03, CF=1), как у потокового HDD (`pxt_disk_set_stream`).
- **pxt_host.c — D-10:** граница HDD-потока проверяется без переполнения:
  `len > size || off > size - len` вместо `off + len > size` (при `off ≈ 4 ГБ`
  сумма оборачивалась в uint32 и проверка обходилась).
- Сборка: `make -j20` → `linkcheck: OK`, `h3_bare.bin` = 26 341 636 Б,
  метка `r1732`.

## r1731 — U-05 (interrupt после recover), U-08 (found/type в .coherent)

Третий пакет плана из `docs/AUDIT_2026-11-27_LAYERS.md` §3 (пункт 4).

- **usb_kbd.c — U-05:** новая `usb_kbd_restart_intr_base(base)` перемонтирует
  interrupt-кольца устройств на указанном PHY: клавиатура — слот 0 (включает
  PLE), геймпад/тачпад — слот 1. `usb_hc_full_recover()` вызывает её сразу
  после `usb_ohci_init(g_pre_base)`: HCR обнуляет periodic-список (HCCA,
  `peried=0`), и без перемонтажа клавиатура на этом PHY после recover мертва.
- **usb_kbd.c — U-08:** `g_kbd`/`g_touch`/`g_pad` и их report-буферы выровнены
  на 32 Б (было 8), а после копирования найденных устройств `dev_publish()`
  делает `usb_cache_clean` структуры. `report[8]` (DMA) и `found/type` делят
  32-Б линию: без clean первый `cache_inv(report)` отбрасывал грязную строку,
  и `found` «забывался» (чтение 0 из DRAM).
- Сборка: `make -j20` → `linkcheck: OK`, `h3_bare.bin` = 26 341 572 Б,
  метка `r1731`.

## r1730 — U-02 (SD write), U-03 (bulk TD), U-04 (CSW residue); U-01 проверен

Второй пакет плана из `docs/AUDIT_2026-11-27_LAYERS.md` §3 (пункты 1–3).

- **sd.c — U-02:** `mmc_write_data()` больше не кастует `const uint8_t*` в
  `uint32_t*` (LDR с невыровненного адреса = Data Abort). Буфер `g_wr.secbuf`
  (`fat.c`) не гарантированно выровнен; чтение переведено на `memcpy` (r1271
  это уже сделано для `mmc_read_data`, write-путь пропустили).
- **usb_ohci.c — U-03:** после цикла ожидания `bulkcur` перед `memset` TD
  добавлена проверка: если HC всё ещё держит список — `bulk_release()` и `-1`.
  Раньше на следующем вызове TD затирался, пока HC мог его читать.
- **usb_msd.c — U-04:** CSW теперь отвергается и при `residue != 0`
  (`status || residue`). EHCI/OHCI-путь короткий пакет считали успехом, и MSD
  отдавал недочитанные данные наверх.
- **usb_ohci.c — U-01 НЕ менялся (проверено):** ветка короткого IN достижима.
  `accepted = td->be - cbp + 1` при неполном приёме даёт остаток (≠ len),
  стенд r969 печатал `short IN got=448 want=512` — то есть детект работал;
  замена на сравнение `cbp != cbp0` дала бы ложный DataUnderrun на каждой
  передаче, если HC не пишет CBP обратно в TD. Оставлено как есть.
- Сборка: `make -j20` → `linkcheck: OK`, `h3_bare.bin` = 26 341 316 Б,
  метка `r1730`.

## r1729 — K-01 (clean почты 0x28), V-03 (dsb sy DCS), L-01 (сброс EMA)

Первый пакет плана исправлений из `docs/AUDIT_2026-11-27_LAYERS.md` §3
(пункты 8, 10, 11 — безопасные однострочные правки).

- **cps1_host.cpp:1127 — K-01:** `usb_cache_clean(0x9000u, 0x80u)` заменён на
  `0x28`. r1727 сузил диапазон в `:897`/`:930`, но быстрый путь выдачи кадра
  остался с `0x80` и чистил линию `0x9040..0x905F` — там MMU-параметры CPU1 и
  флаг готовности `0x9058` (пишет CPU1 в `startup.S`, читает core0). Переносится
  в DRAM устаревшая копия строки — тот же false sharing, что чинили r1726 у
  DCS-почты. Дефект отложенный: конвейер выключен (`k_vidw_enable=0`).
- **cps1_host.cpp — V-03:** в начало `dcs_host_publish()` добавлен
  `__asm volatile("dsb sy" ::: "memory")`. Поля `DCS_RUN_TARGET → DCS_RUN_SEQ →
  DCS_PING` писались и выгружались без барьера: CPU1 мог увидеть свежий `SEQ`
  раньше `PING` (ARMv7-A не гарантирует порядок записей до clean).
- **i2s.c — L-01:** `i2s_ring_reset()` обнуляет `s_ring_ema` (EMA темпа потока).
  Раньше статик переживал сессию, и первый кадр новой игры получал target от
  темпа прошлой (PAL 50 Гц после NTSC 60 Гц — завышенный запас). Объявление
  `s_ring_ema` поднято вверх файла (определение ниже сохранено).
- Сборка: см. ниже, метка `r1729`.

## r1728 — аудит: DK-6 (PXT HDD), UB-1 (MBR-буферы), ME-1..3 (карта памяти)

Применён предложенный порядок из `docs/AUDIT_2026-11-27_LAYERS.md`.

- **pxt_host.c — DK-6:** `pxt_mount_hdd` теперь снимает слот целиком ДО поиска
  файла (`g_hdd_size[i]=0`, `g_hdd[i].size=0`, `pxt_disk_set_stream(unit,0,0)`).
  Раньше при ромсете без `hdd2.img` `g_hdd_size[1]` держал прошлый размер, и
  читалка D: (`pxt_hdd_read1`) продолжала отдавать СТАРЫЙ поток — диск D:
  залипал между сессиями. Нулевая длина проходит через `ejectdisk()` +
  `inserted=0` (`disks-h3.c.inl:108-117`), NULL-читалка встречается в
  `pxt_hdd_read_slot` только после проверки `g_hdd_size`.
- **usb_hc.c — UB-1:** три копии `static uint8_t mbr[512]` в `.coherent`
  (pre-enum, EHCI, OHCI) заменены одним `g_mbr` (0x4A402300). Pre-enum читал
  сектор «в никуда» (содержимое не разбиралось). Экономия 1024 Б `.coherent`,
  поведение не меняется: пути выполняются последовательно.
- **docs/MEMORY.md §2 — ME-1..3:** таблица переснята на r1728: `.text`
  0x40000000/0xDE415C, `.rodata` 0x40DE4180/0x5F24D8, `.data`
  0x413DC9A0/0x5425E4, `.bss` 0x4191F000..0x4479D520, `_coherent_end`
  0x4A402A40, `.rgb_hi` 0x53000000..0x531B8000 (**1.72 МБ**, прежние
  «1.25 МБ» неверны), конец пула FBNeo 0x5BF43900. Добавлена строка про
  32-битные кадровые буферы (0x54000000..0x55343900) — они лежат ПЕРЕД
  `pool_mem` и пул не уменьшают. Эталонные значения §8 и адреса вшитых BIOS
  (плавают) тоже обновлены.
- **TR-1:** `docs/AUDIT/` (6 файлов) удалён с диска, но в HEAD остался —
  удаление не закоммичено. Решение о коммите — за владельцем.
- **TR-2 отменена:** `fw_nic.bin` НЕ лишний — `h3_bare/platform/fw_blob.S:11`
  делает `.incbin "fw_nic.bin"`, а `$(BUILD)/fw_blob.o` входит в `OBJ`
  (Makefile:86). Из индекса убран и сразу возвращён; в `.gitignore` пояснение.
- Сборка: `make -j20` → `linkcheck: OK`, `h3_bare.bin` = 26 341 252 Б,
  обе версии (`build/` и корень). Warnings `pxt_host.c` были и раньше.

## r1727 — межъядерный обмен: развод magic CPU1/CPU2, сужение кэш-диапазона почты

- **Аудит послойный:** `docs/AUDIT_2026-11-27_LAYERS.md` — слои память,
  USB/MSD, диски, звук, хвосты. Открыто: OHCI, INT13h/disks-h3, эталоны (N),
  клавиатура (M).
- **h3_smp.c / startup.S:** у CPU1 теперь своя ячейка magic `0xC4`, у CPU2 — `0x20`.
  Раньше оба писали в `0x20`, а `h3_secondary_start` обнуляет её перед каждой попыткой:
  живой CPU2 из прошлой попытки успевал прописать magic снова, и core0 мог принять
  чужое ядро за поднятое (ложное «ядро живо»). `cpu1_entry` пишет `0x18C`→`0xC4`
  (`mov r2, #0xC4`), ожидание — по своей ячейке в зависимости от `cpu`.
- **cps1_host.cpp:** `arc_vid_worker_poll` чистит/инвалидирует только почту видео
  `0x9000..0x9028` вместо `0x9000..0x9080` — прежний диапазон задевал параметры MMU
  (`0x9040..0x9058`), ACTLR (`0x9070`) и счётчики CPU1; при включении конвейера это
  был тот же false sharing, что чинили r1726 для DCS-почты.
- **i2s.c:** комментарий карты SRAM приведён к фактам (данные кольца — в SRAM с r957,
  полная таблица — `docs/MEMORY.md §3`).
- **startup.S:** устаревший комментарий «кольцо/почта в .coherent» исправлен на SRAM A1.
- **docs/MEMORY.md §3:** добавлены `0x74` (игра активна), `0xA4/0xA8` (пауза/актив),
  `0xAC/0xB0/0xB4` (FW/SKIP/BEAT), `0xB8/0xBC` (CMD/ACK), `0xC4` (magic CPU1),
  `0x9070` (ACTLR), уточнён статус CPU1 `0x24`; «правило печати» переформулировано
  как **опыт r1687** (не догма).
- DCS-почта в `.bss` (r1726) осталась как документированное исключение из «только SRAM».

## r1726 — вход и выход почты DCS в разных линиях кэша

## r1726 — вход и выход почты DCS в разных линиях кэша

- Стенд r1725: картина не изменилась (`total=50 tgt=30 lastd=-20`, `seq=3
  done=2`) — правка порядка refresh не помогла.
- Корень: `g_dcs_mail[16]` — 16 слов = 64 байта = **ровно одна линия L1**, и
  слова 0..7 / 8..15 делили её. `clean` core0 записывал в DRAM свою (устаревшую)
  копию строки и затирал DONE/TOTAL, которые только что выгрузил CPU1;
  `invalidate` CPU1 выбрасывал его собственные записи.
- `cps1_host.cpp`: два массива по 8 слов с `aligned(64)` —
  `g_dcs_in` (пишет только core0) и `g_dcs_out` (пишет только CPU1).
  Новые операции: `dcs_host_publish` (clean in + invalidate out),
  `dcs_host_refresh` (invalidate out), `dcs_worker_refresh` (invalidate in),
  `dcs_worker_flush` (clean out). Все прежние вызовы заменены.
- Сборка см. ниже, метка `r1726`.

## r1725 — refresh почты до решения о простое

- Стенд r1724: `total=50 tgt=30 lastd=-20 on=1`, но `seq=3 done=2` — DSP цель
  догнал, а RPC 3 воркер не увидел и ушёл в «простой»; core0 словил таймаут.
- Причина: решение о простое (`dv <= 0 && DCS_SEQ == DCS_DONE`) принималось
  **до** `dcs_sram_refresh()` — SEQ/DONE читались из устаревшей строки кэша.
- `cps1_host.cpp`: `dcs_sram_refresh()` перенесён в начало шага, до записи
  STAMP/TOTAL и проверки простоя; дублирующий refresh убран; ветка простоя
  перед возвратом делает `dcs_sram_flush()`, чтобы STAMP/TOTAL видел core0.
- Сборка см. ниже, метка `r1725`.

## r1724 — прогресс DSP виден всегда, расширенный отчёт таймаута

- Стенд r1723: SMP включился (`setup game=1 cpu1=1 blocked=0`), но первый же
  `dcs_host_wait` упал: `total=0 tgt=30 echo=1`. DSP не исполнил ни одного
  цикла, хотя цель ядро увидело.
- Причина неочевидна: `DCS_TOTAL` писался только в конце шага, а в ветке
  простоя шаг возвращается раньше — core0 мог видеть устаревший ноль.
- `cps1_host.cpp`: `DCS_TOTAL` пишется сразу после `DCS_STAMP` (до ветвей);
  новое слово 6 `DCS_LAST_D` — сколько отдано в `Dcs2kRun` на шаге; отчёт
  о таймауте печатает `on=` и `lastd=`.
- Сборка см. ниже, метка `r1724`.

## r1723 — диагностика выбора воркера и сброс блокировки на новой игре

- Стенд r1722: `dcs 16.7` с первого кадра и **ни одной** строки `DCS: worker
  CPU1 (smp)`/`core0` — `dcs_host_setup` молча выходил по `want == g_dcs_smp`.
- Найдено: `g_dcs_blocked` (r1711) нигде не сбрасывался — таймаут в одной игре
  навсегда отключал SMP до перезагрузки устройства.
- `cps1_host.cpp`: `dcs_host_setup` печатает один раз
  `DCS: setup game=… cpu1=… blocked=…` — причина видна сразу;
  `dcs_host_new_game()` снимает блокировку и SMP.
- `dcs2k.cpp`: `Dcs2kInit()` вызывает `dcs_host_new_game()` при старте игры.
- Сборка см. ниже, метка `r1723`.

## r1722 — после сброса DSP программа и состояние выгружаются воркеру

- Стенд r1721: таймаут остался, но диагностика показала `mixer pos=0 from=571
  gen=0` — DSP не инициализировал авто-буфер, т.е. не исполнил программу.
- Причина: `Dcs2kReset()` (dcs2k.cpp) делает `Adsp2100Reset()` + `Dcs2kBoot()`,
  переписывая `pIntRAM` и состояние ADSP, но **не вызывал**
  `Dcs2kFlushToWorker()`. CPU1 продолжал исполнять старую копию из своего
  кэша: DSP уходил в `idle`, `bGenerateIRQ` оставался 0.
- `dcs2k.cpp`: в конец `Dcs2kReset()` добавлен `Dcs2kFlushToWorker()`; новая
  `Dcs2kInvalidateWorker()` сбрасывает строки `pIntRAM/pExtRAM/pDataRAM/
  pDataRAM0/pSoundROM` и вызывает `Adsp2100InvalidateWorker()`.
- `adsp2100_intf.cpp`: `Adsp2100InvalidateWorker()` — инвалидация `pMemMap`
  и `pADSP` (парная к `Adsp2100FlushToWorker`, которая только чистит).
- `cps1_host.cpp`: RPC 4 (reset) в воркере сначала инвалидирует кэш
  (`Dcs2kInvalidateWorker()`), затем вызывает `Dcs2kResetWrite()`.
- Сборка см. ниже, метка `r1722`.

## r1721 — IRQ-чек DSP без сброса звукового буфера

- Стенд r1720: `54.7 fps / dcs 0.2` только на статичной заставке; в игре снова
  `DCS: timeout` (`total=61874 tgt=183714`) и откат на core0 (`dcs 16.6`).
- Причина: `DcsCheckIRQ()` в `dcs2k.cpp` при `pBurnSoundOut == NULL` обнуляет
  `mixer_pos`. Указатель принадлежит core0; на CPU1 он читается как NULL
  (строка кэша не обновляется), поэтому каждый IRQ-чек стирал собранный
  буфер и DSP не продвигался.
- `dcs2k.cpp`: добавлен `DcsCheckIRQWorker()` — без сброса буфера, для
  исполнения на ядре DSP; `Dcs2kDebugGet()` отдаёт `mixer_pos`/`samples_from`/
  `bGenerateIRQ` для отчёта.
- `cps1_host.cpp`: RPC 6 в воркере вызывает `DcsCheckIRQWorker()`; лог
  таймаута печатает `DCS: mixer pos=… from=… gen=…`.
- Сборка см. ниже, метка `r1721`.

## r1720 — IRQ-чек DSP перенесён на ядро DSP

- Стенд r1719: 25 кадров подряд `54.7 fps / dcs 0.2`, затем `DCS: timeout`
  (`total=28704 tgt=183714`) и откат на core0 (`dcs 17`, 35–38 fps).
- Причина: `HandleDCSIRQ()` → `DcsCheckIRQ()` (midtunit.cpp) исполнялся на
  core0 и трогал состояние ADSP (`nTxIR`, `mixer_buffer`, `mixer_pos`,
  `nNextIRQCycle`) — гонка с DSP, исполняемым на CPU1. Таймаут `dcs_host_wait`
  навсегда ставил `g_dcs_smp = 0` (r1711).
- `cps1_host.cpp`: новое RPC 6 → `DcsCheckIRQ()` в `dcs_worker_step`;
  `dcs_host_check_irq()` для SMP и локального пути.
- `midtunit.cpp`: `HandleDCSIRQ` вызывает `dcs_host_check_irq()`.
- `dcs2k.cpp`: `Dcs2kFlushMixer()` дополнительно выгружает `mixer_pos` и
  `samples_from` — их читает `Dcs2kRender` на core0.
- Сборка см. ниже, метка `r1720`.

## r1719 — учёт циклов DSP синхронизирован со сбросом на newframe

- Стенд r1718: `total=7014 tgt=183714` при 174 млн шагов цикла — DSP исполнено
  0.4% заказа, `Dcs2kRun(d)` почти не продвигает.
- Причина: `adsp2100.cpp:930` `adsp21xx_new_frame()` обнуляет `total_cycles`,
  а `DCS_RUN_TARGET` в `dcs_host_sync` копится накопительно. После каждого
  `Dcs2kNewFrame()` (RPC 5) цель убегает на величину всего кадра, DSP упирается
  в HALT, и `d` никогда не закрывается.
- `cps1_host.cpp`: на CPU1 заведена накопительная база `g_dcs_total_acc`;
  `dcs_progress()` = база + `Dcs2kTotalCycles()`. В обработчике RPC 5 база
  пополняется до `Dcs2kNewFrame()`; `dv`, `d` и `DCS_TOTAL` считаются по
  `dcs_progress()`. При переходе `DCS_ON` 0→1 (новый сеанс) база обнуляется.
- Сборка см. ниже, метка `r1719`.

## r1718 — канал DCS переведён на счётчик циклов DSP

- Стенд r1717: `ping=291 echo=242` — почта видна, ядро читает свежие цели;
  но `rdone=15` при 291 — логика «номер цели» не продвигалась. Значит дело не
  в видимости, а в учёте прогресса.
- `cps1_host.cpp`: в почту добавлено слово 15 `DCS_TOTAL` — сколько циклов
  DSP реально исполнено; воркер пишет его каждый шаг, `Dcs2kRun(d)` идёт пока
  `d = target - total > 0` независимо от `RUN_SEQ`. `dcs_host_wait` ждёт
  `DCS_TOTAL >= DCS_RUN_TARGET` (новый `dcs_wait_ge`), а не `RUN_DONE`.
- Отчёт о таймауте вынесен в `dcs_fail_timeout()`, лог печатает `total=`/`tgt=`.
- Сборка 26 340 356 Б, метка `r1718`.

## r1717 — диагностика канала целей DCS и правка MMU-формулировок

- Стенд r1716: `actlr0/actlr1` с битом 6 (SMP=1), `sctlr0/sctlr1` с битом C —
  кэши включены на обоих ядрах; `steps` вырос 34.7 → 101.6 млн (цикл CPU1
  стал быстрее втрое). Но канал целей стоит: `seq=done=8` (RPC идут),
  `rseq=291 rdone=15` — CPU1 не видит новые цели.
- `cps1_host.cpp`: в почту добавлены `DCS_PING` (слово 13 — что поставил
  core0) и `DCS_ECHO` (слово 14 — что прочитал CPU1 после `refresh`);
  лог таймаута печатает `ping=` и `echo=`. Это разделяет две гипотезы:
  «ядро не видит» (echo < ping) и «видит, но не выполняет» (echo ≈ ping).
- `startup.S`/`MEMORY.md`: исправлена формулировка про размеры в LPAE —
  запись L0 покрывает 512 ГБ, запись L1 — 1 ГБ; DRAM стенда 512 МБ
  (2 × K4B2G16 по 256 МБ), а не «1 ГБ на запись L0».
- Сборка 26 340 420 Б, метка `r1717`.

## r1716 — диагностика ACTLR/SCTLR и разгрузка hot loop CPU1

- Стенд r1715: MK2 36–37 fps (`run` 9–11 → 4–5.8) — L1 на core0 заработал,
  но `rdone` вырос лишь 11→15, а `steps=34.7 млн` за 500 мс (≈17 тактов на
  итерацию) — на CPU1 кэш, похоже, так и не включился, и безусловный
  `usb_cache_invalidate` в `arc_vid_worker_poll` съедал цикл.
- `emu.c`: строка `CPU: actlr0=… sctlr0=… actlr1=… sctlr1=…` при старте игры
  (UART к этому моменту работает; `printf` в `main.c` до меню в лог не попадал).
- `startup.S`: фактический ACTLR CPU1 сохраняется в SRAM `0x9070`.
- `cps1_host.cpp`: `arc_vid_worker_poll` опрашивает видео-почту раз в 64
  итерации (конвейер выключен), а не инвалидирует каждую итерацию.
- Сборка 26 340 356 Б, метка `r1716`.

## r1715 — почта DCS разнесена по двум кэш-линиям

- `cps1_host.cpp`: поля почты DCS разделены по линиям — слова 0..7 пишет
  только core0 (`RUN_TARGET/RUN_SEQ/CMD/ARG/SEQ/ON`), слова 8..15 — только CPU1
  (`RUN_DONE/RESP/DONE/STEPS/STAMP`); выравнивание `aligned(64)`.
  Раньше `clean`/`invalidate` одной стороны выбивал линию с полями другой
  (false sharing на каждой задаче DSP).
- Сборка 26 340 164 Б, метка `r1715`.

## r1714 — ACTLR.SMP=1 (L1-кэши на Cortex-A7)

- Проверка кода: `ACTLR` (c1,c0,1) в проекте не трогался ни разу. На
  Cortex-A7 при `ACTLR.SMP=0` L1-кэши отключены аппаратно, независимо от
  `SCTLR.C` — это объясняет и «аномальные 21.5 мс» видео-воркера (r1694),
  и слабый эффект оптимизаций (r1705–r1707).
- `startup.S`: CPU1 ставит `ACTLR.SMP` до включения MMU/кэшей.
- `main.c`: core0 ставит `ACTLR.SMP` и печатает `cpu0: ACTLR=… SMP=…`.
- Сборка 26 340 164 Б, метка `r1714`.

## r1713 — D-cache CPU1 и двусторонняя синхронизация почты DCS

- Стенд r1712 показал, что почта в DRAM доходит (`stamp=DC5A0001`,
  `seq=done=8`), но **CPU1 без кэша не успевает**: `rseq=291 rdone=11`
  за 500 мс, дальше откат на core0 и 29–31 fps.
- `main.c`: маска SCTLR для CPU1 снова `M|C|I` (D-cache включён).
- `cps1_host.cpp`: `dcs_worker_step` опрашивает почту выборочно — пока работы
  нет, раз в 64 итерации (иначе 26 млн кэш-операций за игру), перед работой
  `dcs_sram_refresh()` (свежие CMD/ARG/TARGET), после работы
  `Dcs2kFlushMixer()` + `dcs_sram_flush()` (DONE/RESP → DRAM).
- `dcs2k.cpp/h`: `Dcs2kFlushMixer()` — clean заполненной части `mixer_buffer`
  с CPU1 перед записью DONE; `Dcs2kFlushOutput()` на core0 остаётся.
- Сборка 26 340 100 Б, метка `r1713`.

## r1712 — почта DCS перенесена из SRAM в .bss

- Лог r1711: `DCS: timeout alive=3735879682 stage=1 steps=1133518307
  seq=1 done=0` — цикл CPU1 крутится (alive/steps огромны), но `done=0`:
  воркер видел `DCS_ON=0` и выходил, то есть почта по адресам SRAM A1
  `0x90C0..0x90E0` ему недоступна (видео-конвейер после r1694 не используется,
  так что это не проверялось).
- `cps1_host.cpp`: почта DCS — `static volatile uint32_t g_dcs_mail[16]`
  в `.bss` (DRAM): адрес совпадает в картах обоих ядер, core0 выталкивает
  линии `dcs_sram_flush()`, воркер читает DRAM напрямую.
- Добавлен отпечаток `DCS_STAMP = 0xDC5A0001` (пишет CPU1, если дошёл до
  обработки) — печатается в строке таймаута: видно, доходит ли почта.
- Сборка 26 339 908 Б, метка `r1712`.

## r1711 — диагностика DCS-воркера и одноразовый откат

- r1710: таймаут сработал, но `dcs_host_setup(1)` вызывается в начале каждого
  кадра и снова включал SMP → каждый кадр терял 500 мс на таймаут и лог
  забивался строкой `DCS: worker CPU1 (smp)`.
- Введён `g_dcs_blocked`: после первого провала SMP не включается до конца
  игры; строка `DCS: worker core0` печатается однократно.
- При таймауте печатается состояние ядра:
  `DCS: timeout alive=… stage=… steps=… seq=… done=… rseq=… rdone=…`
  (`alive` — итерации цикла CPU1 0x905C, `stage` — 0x9060, `steps` — новый
  счётчик входов в шаг 0x90E4). По ним сразу видно: цикл не крутится,
  не видит `DCS_ON` или завис в `Dcs2kRun`.
- Сборка 26 339 972 Б, метка `r1711`.

## r1710 — выталкивание .bss и безопасный откат DCS

- r1709 всё ещё вис на MK2 (лог: `DCS: worker CPU1 (smp)`, затем тишина).
  Причина: у CPU1 нет D-cache, а статики драйверов (`dcs2k`: nCtrlReg,
  sample_rate, указатели банков; `adsp2100_intf`: карта страниц) core0 держал
  в D-cache после Init — воркер читал нули и уходил в разнос.
- `cps1_host.cpp`: после `BurnRecalcPal()` — `usb_cache_clean(_bstart1,
  _bend1-_bstart1)`, один раз на вход в игру (~7.6 МБ, ~7 мс).
- Там же: если `dcs_wait_flag` истёк (500 мс), печатается
  `DCS: worker timeout, fallback to core0` и SMP отключается — игра
  продолжается на core0 прежним путём, без зависания.
- Сборка 26 339 780 Б, метка `r1710`.

## r1709 — исправлено зависание MK2 в r1708

- Симптом: MK2 останавливался сразу после `ARC: scaler` (ни одной строки
  `CPS: perf`). Причина: core0 писал почту DCS в SRAM через свой D-cache, а
  CPU1 (кэш выключен) читал DRAM — `DCS_ON`/`TARGET`/`CMD` оставались нулями;
  плюс в DRAM не была вытолкнута карта памяти ADSP (`pMemMap`) и состояние
  `pADSP`, поэтому воркер брал устаревшие указатели.
- `cps1_host.cpp`: `dcs_sram_flush()` (clean 0x90C0..0x90DF + 0x90E0) после
  каждой записи цели/RPC, `dcs_sram_refresh()` (invalidate той же линии) перед
  чтением флагов; `dcs_wait_flag` инвалидирует кэш на каждой итерации.
  `usb_cache_invalidate` добавлен в `extern "C"`-блок объявлений.
- `adsp2100_intf.cpp`: `Adsp2100FlushToWorker()` — clean `pMemMap` и `pADSP`;
  вызывается из `Dcs2kFlushToWorker()` (`dcs2k.cpp`) после init/загрузки ROM.
- Диагностика: одна строка `DCS: worker CPU1 (smp)` или `DCS: worker core0`
  при смене режима — видно, включился ли вынос.
- Сборка 26 340 996 Б, метка `r1709`.

## r1708 — DCS (MK/MK2) на CPU1 через SRAM-почту

- Лог r1706: MK2 `dcs 22.9` мс из `emu 30.9`. DSP ADSP2100 (10 МГц) уносим с
  core0 на CPU1: core0 только ставит цель «догнать до N циклов» и делает RPC
  при обращении main CPU к регистрам звука (это редко, там ждём).
- Почта в SRAM A1 `0x90C0..0x90E0` (числа и счётчики, без строк):
  `DCS_RUN_TARGET/SEQ/DONE`, `DCS_CMD/ARG/RESP/SEQ/DONE`, `DCS_ON`.
- `cps1_host.cpp`: `dcs_host_setup/sync/total/wait/ctrl_read/data_read/
  data_write/reset_write/newframe`, `dcs_worker_step()` в цикле
  `arc_vid_worker_loop` (цель + RPC). Если CPU1 не поднят — `g_dcs_smp = 0`
  и всё исполняется на core0 прежним путём.
- `midtunit.cpp`: `dcs_sound_sync/end` → `dcs_host_sync/wait`, порты → RPC,
  `Dcs2kNewFrame` → `dcs_host_setup(1)+dcs_host_newframe`, перед
  `Dcs2kRender` — `Dcs2kFlushOutput()`.
- `dcs2k.cpp`: `Dcs2kFlushToWorker()` (clean pIntRAM/pExtRAM/ROM после
  `Dcs2kInit`/`Dcs2kMapSoundROM` — CPU1 читает DRAM), `Dcs2kFlushOutput()`
  (invalidate заполненной части `mixer_buffer` перед рендером на core0).
- Ограничение: CPU1 сейчас `M|I` (без D-cache), DSP там может быть медленнее,
  чем на core0; цифры стенда решат, включать ли D-cache с точечной чисткой.
- Сборка 26 340 484 Б, метка `r1708`.

## r1707 — ADSP2100: выборка опкодов без вызова на инструкцию

- Профиль MK2 (лог r1706): `dcs 22.9` из `emu 30.9` — узкое место ядро
  ADSP2100 (DCS, 10 МГц, ~185k циклов/кадр), а не TMS34010 (`run` 9.1).
- Каждый опкод читался через `adsp21xx_read_dword_32le`: сдвиг, PFN, выбор
  страницы, проверка хендлера, вызов. `adsp2100.cpp`: добавлен быстрый путь
  `adsp_fetch_op()` — программная память DCS плоская: `0x0000..0x03FF` →
  `pIntRAM`, `0x0800..0x1FFF` → текущий банк `pExtRAM32` (указатель на
  указатель, чтобы смена банка была видна). Прочие драйверы указатели не
  задают и идут прежним путём — поведение не меняется.
- `dcs2k.cpp` (`SetupMemory`): задаёт `adsp_fast_prg_lo/hi`.
- Сборка 26 337 652 Б, метка `r1707`.

## r1706 — заливка тайлмап DEC0 только по читаемым столбцам

- Лог r1705: `lay` 8.8 → 6.6 (t1 2.7→2.1, t2 3.0→2.3, char 3.0→2.2),
  robocop 52 → 56–57.4 fps. Остаток — `memset` всей карты 1024×256×2
  (512 КБ на слой, ~3.9 мс на три слоя): его r1705 не сокращал.
- `d_dec0.cpp`: `Dec0ClearX()` чистит только столбцы тайлов `k0..k1`
  (с учётом wrap), все строки; при полном диапазоне — один `memset`.
  Применено в Tile1/Tile2/Char.
- Ожидание: `lay` ~3.5 мс, robocop в упоре throttle 57 Гц.
- Сборка 26 337 332 Б, метка `r1706`.

## r1705 — рендер DEC0 режется по видимому окну

- Лог r1704 разложил `lay`: `t1 2.7 + t2 3.0 + char 3.0 + spr 0` при экране
  256×240. Все три слоя заливали и рисовали ВСЮ карту 1024×256 (4-кратная
  работа по X).
- `d_dec0.cpp`: `Dec0VisibleX()` считает реальный X-диапазон карты по
  `xScroll` и row-scroll (как в `DrvRenderCustomTilemap`); циклы `mx` в
  `DrvRenderTile1Layer`/`Tile2`/`Char` идут только по нужным тайлам
  (`mx = k & (mw-1)`, wrap). При флипе или широком row-scroll — полный
  диапазон, как раньше.
- Замер `draw`/`snd` добавлен и в `DrvFrame` (путь baddudes и др.): раньше
  там было `draw 0.0`.
- Ожидание: `lay` ~8.8 → ~2.5 мс, robocop 52 → 57 Гц (упор в throttle).
- Сборка 26 337 140 Б, метка `r1705`.

## r1704 — USB: полный recover при мёртвом bulk-канале

- Симптом (лог r1703, MK/MK2): чтение ROM с флешки встаёт — `msd: op=0x28
  CBW fail`, `ohci-bulk: TIMEOUT`, `bcur == bulkhead`, `ctl=0xB0` (BLE=0).
  Прежний BOT recovery (r1189) канал не поднимал, дальше шёл каскад.
- `usb_hc.c`: `usb_hc_full_recover()` — HCR (`usb_ohci_init`) + полное
  перечисление флешки (`usb_ohci_enumerate_flash`). Только OHCI-путь, EHCI не
  трогаем. `usb_ohci_enumerate_flash` теперь берёт базу из HID-скана
  (`g_pre_base`, B400 или C400) вместо жёсткой `0x01C1B400` — иначе при
  флешке на PHY2 re-enum уходил не туда.
- `usb_msd.c`: `usb_msd_cmd` при `CBW fail` (-2) сначала делает BOT-recover,
  затем однократный полный recover; guard `g_full_recover_busy` не даёт
  рекурсии (re-enum сам читает сектора). Всего до 4 попыток.
- Сборка 26 334 708 Б, метка `r1704`, `linkcheck: OK`,
  `_bend1 = 0x4479C520` (перекрытий нет).

## r1703 — вклад слоёв в рендер DEC0

- Лог r1702: `DE: draw clear 0.1 pal 0.0 lay 8.8–9.5 copy 0.6` — виноват не
  `BurnTransferCopy`, а отрисовка слоёв. В `DrvRenderTile1Layer`,
  `DrvRenderTile2Layer`, `DrvRenderCharLayer`, `DrvRenderSprites` добавлены
  замеры (`g_de_us_t1/t2/char/spr`), хост печатает строку
  `DE: lay t1 … t2 … char … spr … ms`.
- Там же видно устройство рендера: каждая функция заливает ВСЮ тайлмапу
  `memset(W*H*2)` (при RenderType=0 это 1024×256 = 512 КБ на слой) и рисует
  все тайлы карты, хотя на экране окно 256×240. Потенциал — до 4× на слоях.
- Попутно по логу: `MK`/`MK2` зависание и `msd: op=0x28 CBW fail` — сбой
  чтения ROM с USB **до** запуска эмулятора (OHCI bulk: `bcur == bulkhead`,
  `ctl=0xB0`, фрейм-номер идёт). USB-код с r1692–r1699 не менялся, сборка
  `usb_*.o` идёт без `-O3`. Нужен отдельный разбор транспорта.
- `tengai`: `missing rom u61.bin` — неполный ZIP, не баг прошивки.
- Сборка 26 334 836 Б, метка `r1703`.

## r1702 — рендер robocop = 10 мс, раскладка BurnTransfer

- Лог r1701: у robocop `draw` **9.6–10.2 мс** из 17.5 (68k 2.5, snd 0.8) —
  узкое место не CPU, а `RobocopDraw()`.
- Найдено наследие: `g_de_us_snd/draw` живут дольше одной игры, поэтому у MK
  и tankfrce поле `draw` показывало значение от robocop. Теперь счётчики
  обнуляются сразу после `BurnDrvInit`.
- В `RobocopDraw()` добавлена разбивка: `BurnTransferClear`, `DrvCalcPalette`,
  слои/спрайты, `BurnTransferCopy`; хост печатает строку
  `DE: draw clear … pal … lay … copy … ms` раз в 120 кадров (только для DEC0).
- Сборка 26 332 852 Б, метка `r1702`.

## r1701 — честный счётчик 68k + разрез DEC0

- Найдено по логу r1700: `pf_m68k` не обнулялся после отчёта, поэтому «68k»
  рос от отчёта к отчёту (robocop 2.5 → 29.5 мс, NeoGeo 30.7 → 101.8), а у MK
  показывал значение предыдущей игры. Исправлено: `pf_m68k = 0` в блоке сброса.
  Реальная доля 68000 у robocop — **≈2.5 мс** из 17.4 мс: Musashi не узкое
  место, прирост 42–45 → 49–52 fps дал `-O3` по остальному коду драйвера.
- Поле `68k` печатается только если за интервал был Musashi: Midway T-unit —
  это **TMS34010** (`midtunit.cpp:1282` `CPU_RUN(0, TMS34010)`), 68000 там нет.
- `d_dec0.cpp`: замеры звука и рендера в `RobocopFrame` (`g_de_us_snd`,
  `g_de_us_draw`), хост складывает их в `snd`/`draw` строки `CPS: perf` —
  видно, где у robocop остальные ~15 мс (CPU h6280/M6502 или рендер).
- Сборка 26 331 636 Б, метка `r1701`, `linkcheck: OK`,
  `_bend1 = 0x4479B520` (перекрытий нет).

## r1700 — отчёты perf снова каждые 120 кадров

- `IRM: perf` / `CPS: perf` печатаются раз в **120** кадров (~2 с) вместо 600:
  владелец просил чаще, чтобы видеть просадки без ожидания 10 с.
- Дрожание изображения в меню на стенде r1699 оказалось **багом прошивки**
  (повторная прошивка вылечила), к правкам r1699 отношения не имеет;
  откат `-O3` не понадобился. Карта памяти проверена: `_bend1 = 0x4479B520`
  (запас до ZIP-буфера `0x44C00000` — 4.4 МБ), `.rgb_hi 0x53000000`,
  `.coherent 0x4A400000` — перекрытий нет.
- Сборка 26 330 868 Б, метка `r1700`, `linkcheck: OK`.

## r1699 — оптимизация сборки CPS1 + замер 68000

- `CPS1_CFLAGS`/`CPS1_CXXFLAGS`: `-O2` → **`-O3`**, добавлены
  `-fomit-frame-pointer` и `-fwrapv` (последний — чтобы -O3 не полагался на UB
  при переполнении в вендорском коде). Musashi (68000) раньше собирался с -O2,
  NEC V30 — уже с -O3; теперь одинаково быстро у всех.
- Убран `-DFBNEO_DEBUG`: отладочные блоки FBNeo (`BurnDump`, проверки в
  `BurnDrvInit`) больше не попадают в горячий путь.
- `m68kdasm.o` (отладочный дизассемблер Musashi) исключён из линка: без
  FBNEO_DEBUG он тянул `SekDbgFetchWordDisassembler`, которого в сборке нет,
  а в рантайме не используется.
- Добавлен замер доли 68000 в кадре (`m68000_intf.cpp`, `g_m68k_us_frame`);
  строка `CPS: perf` теперь печатает `68k %.1f` рядом с `run/scan/dcs` —
  сразу видно, съедает ли время CPU или драйвер. (у не-T-unit игр остальные
  поля были нулями — это и было слепой зоной).
- Размер: 25 856 548 → **26 330 868 Б** (+474 КБ от -O3). `linkcheck: OK`.
- Риск: -O3 увеличил код — на стенде смотреть, не проседает ли из-за I-cache;
  откат — `-O2` в двух строках Makefile.

## r1698 — ввод Namco приведён к общему маппингу аркад

- Причина «монета на T/5/6, старт на 1»: входы вида `Coin 1`/`Start 1`
  (Galaxian-семейство, Pac-Man, Galaga, Rallyx) попадали в слоты 7/8 =
  `kick1/kick2`. Теперь монета = слот 10 (`select` = **S**), старт = слот 11
  (`start` = **Enter**), сервис = слот 12 (**T**) — как в CPS/DataEast/Irem.
- Покрыты варианты имён: `Coin 1..3`, `Coin A/B`, `Bill 1`, `Start 1/2`,
  `Start A/B`, `Start 1 / Fire 1`, `Start 1/P1 Fire 1`, `Service 1/2`,
  `Fire` без номера, `P1 Fire`, `P1 Button A/B/C`. P3/P4 не поддерживаем.
- Второй разнобой — полярность: у Galaga входы (`input.ports[].current.bits`)
  стартуют с `0xff` (нажатие = 0), остальные — с 0 (нажатие = 1). Полярность
  определяется по значению поля **до первой записи** и применяется только к
  Namco (`g_nam`), проверенные CPS/DataEast/Toaplan не затронуты.
- Не маппятся осознанно: двойные стики (`Left Up`, `Right Down`, `P1 Left
  Stick …`), `Gear Up/Down`, `Tilt` — для панели не нужны.
- Проверка имён по всем Namco-драйверам: непокрытых кнопок не осталось, кроме
  перечисленных выше.
- Инцидент: `main.c` был испорчен правкой через PowerShell (UTF-8 прочитан как
  CP1251, CRLF) — восстановлен обратной перекодировкой; файл снова без BOM и
  с LF. Правило «кириллицу править только edit/write» подтверждено.
- Сборка 25 856 548 Б, метка `r1698`, `linkcheck: OK`.

## r1696 — спам-логи аркад убраны, отчёт реже

- Убрана отработанная диагностика Irem: `IRM: early f=1..3` (r1676, ловила
  зависание MCU) и `IRM: dbg …` (r1674, снимок NMI/DAC/длины кадра раз в 120
  кадров). Вместе с ними сняты неиспользуемые `extern`-объявления `g_m72_dbg_*`
  в блоке статистики.
- `IRM: perf` / `CPS: perf` печатаются раз в **600** кадров (~10 с) вместо 120 —
  строка остаётся для контроля скорости, но лог не забивает.
- Правки маппинга ввода Namco (хвостовые пробелы, `Button A/B/C`, `Fire`
  без номера) ОТКАЧЕНЫ по указанию владельца: клавиши там замаплены иначе,
  разбираться отдельно.
- Сборка 25 856 100 Б, метка `r1696`, `linkcheck: OK`.

## r1695 — чистка лога (по списку владельца)

- Убраны неинформативные строки: `MMU: cpu1 ttbr0/ttbcr/dacr/mask`,
  `DMA: st …` (и само чтение регистров DMAC), `MMU: cpu1 sctlr before/after`,
  `DMP GCR=…` (дамп SPI, выходил битым — `spi_dump()` теперь пустая заглушка,
  чтобы не править 12 точек вызова).
- Убраны дубли: `RS <game>: N ok / N total` (то же печатает `ARC: roms`);
  `CPS: <game> frame WxH pitch N` (размер даёт строка драйвера
  `<PRE>: game … full=WxH -> WxH pitch=N`); `ARC: video WxH` (тот же размер).
- `rom_browser: /roms/<sys> n=N` НЕ трогали — два вывода = два входа в браузер.
- Оставлено: `TFT: SPI fail @<cmd>` — печатается только при отказе обмена;
  при подключённой панели строка не появится.
- Маска SCTLR CPU1 возвращена к `M|I` (r1692 включал `M|C|I` ради конвейера,
  r1694 конвейер выключен) — D-cache вторичному ядру больше не нужен.
- Сборка 25 856 804 Б, метка `r1695`, `linkcheck: OK`.

## r1694 — конвейер CPU1 выключен, подтверждено на стенде

- Стенд r1693 (D-cache on): `ARC: vid off (slow 21509 us)` — воркер CPU1 делает
  кадр 384×256 за 21.5 мс. `usb_cache_invalidate/clean` в проекте работают по
  одной линии через регистры PL310 — на 196 КБ входа + 384 КБ выхода на кадр
  это ~4600 линий, дороже самой конверсии.
- Три конфигурации сопоставлены: воркер D-cache off = 10.6 мс (r1691/новый
  onward), воркер D-cache on + обслуживание кэша = 21.5 мс (r1693), а
  синхронный NEON-путь на core0 = 1.3 мс (`render`). Конвейер не окупается.
- Корень: кросс-ядерная передача кадра не влезает в SRAM A1 (48 КБ), а
  некэшированная DRAM на вторичном ядре стоит ~580 КБ транзакций/кадр.
- Решение: `k_vidw_enable = 0`, кадр конвертируется на core0 (NEON `cps_row_rgb32`,
  путь r1512/DE2 GSU). Код конвейера, автокалибровка и heartbeat CPU1 сохранены.
- Прошлая цель этапа A (первый кадр на CPU1) снята как нецелесообразная;
  этап B/C — звук на CPU2 по проверенной SRAM-схеме (индексы `0x98/0x9C`,
  данные в SRAM) к конвейеру CPU1 отношения не имеют и продолжат работать.
- Сборка 25 857 700 Б, метка `r1694`, `linkcheck: OK`.

## r1693 — D-cache включён + обслуживание SRAM-почты, PL10 мигает

- Стенд r1691 (D-cache off): воркер **работает**, но конверсия 384×256 на CPU1
  заняла **10.6 мс** (`ARC: vid off (slow 10610 us)`) — без D-cache каждое
  чтение пикселя идёт в DRAM. То есть причина отказов была не в коде, а в
  скорости.
- Решение (по схеме передачи как у TFT/звука — только числа в SRAM, без строк):
  D-cache включён (`SCTLR = M|C|I`), а почта обслуживается явно:
  `usb_cache_invalidate(0x9000..0x9080)` перед чтением флагов воркером,
  `usb_cache_clean(0x9000..0x9080)` после записи `US/DONE`; на core0 — clean
  после выставления `VIDW_REQ`. Через почту по-прежнему ходят только числа.
- Воркер сам освежает вход (`invalidate` pBurnDraw) и чистит выход (`.rgb_hi`).
- В `arc_vid_worker_loop()` добавлен `led_heartbeat_cpu1()` раз в ~2 млн
  итераций — PL10 снова мигает, когда CPU1 жив (владелец проверяет этим).
- Сборка 25 857 764 Б, метка `r1693`, `linkcheck: OK`. **Стенд: ждём
  `ARC: vid CPU1, worker < 1 мс`; если снова `slow`, порог/алгоритм правим.**

## r1690 — сигнатуры счётчиков CPU1 и ответ по «HW: period»

- Стенд r1689: `ARC: vid off (stall req=1 done=0 stage=1 alive=1097866723
  calls=1099963875)` — `stage=1` (ядро дошло до воркер-цикла), но `alive/calls`
  похожи на мусор U-Boot (`0x4170…`/`0x4186…`), а не на счётчики. Перед стартом
  CPU1 core0 пишет сигнатуры `0xDEAD0001/2/3` в `0x905C/0x9060/0x9064`:
  остались сигнатуры — CPU1 в эти адреса не писал; `alive` растёт — писал.
- `HW: period=16667 us` — **не** фактический период игры: строка печатается в
  `emu_prepare()` (`emu.c:504`) после сброса периода в дефолт 60 Гц. В игре
  `cps1_host.cpp:1774` ставит период драйвера (`CPS: throttle 55 Hz (period
  18181 us)`), на выходе возвращает сохранённый. Плохой звук — не от 16667,
  а от недобора кадров (`under`), когда `emu` больше периода кадра.
- Сборка 25 857 252 Б, метка `r1690`, `linkcheck: OK`.

## r1689 — счётчики жизни CPU1: почему конвейер откатывается

- Стенд r1688: `mask=00551001`, `sctlr before=05c52878 after=05c53879`
  (M=1, C=0, I=1) — MMU/I-cache включились, но конвейер снова откатился:
  `ARC: vid off (stall req=1 done=0)` — CPU1 не принял НИ ОДНОЙ задачи.
- Значит дело не в D-cache. Введены счётчики (только числа, без строк):
  `0x905C` — итерации воркер-цикла, `0x9060` — стадия (1 = воркер-цикл,
  2 = TFT-цикл), `0x9064` — сколько задач принял воркер. Причина отката
  печатается с ними: `ARC: vid off (stall req=… done=… stage=… alive=… calls=…)`.
- Читать так: `alive=0` — CPU1 не дошёл/упал (fault после включения MMU);
  `alive>0, calls=0` — цикл жив, но задачу из почты не видит; `stage=2` —
  `tft_init()` вернул 0 и ядро сидит в TFT-цикле.
- Сборка 25 857 188 Б, метка `r1689`, `linkcheck: OK`.

## r1688 — D-cache на CPU1 ВЫКЛЮЧЕН (SRAM-почта), I-cache оставлен

- Стенд r1686: MMU на CPU1 включился (`sctlr before=05c52878 after=05c5387d`,
  M=1 C=1 I=1), TFT-panel нет → `arc_vid_worker_loop()` работает (спам
  `TSTOUCH watchdog` исчез), но **конвейер откатился**: строки `ARC: vid` нет,
  т.е. `g_vidw_on` сбросился по stall — задача из почты не доходила.
- Причина: SRAM A1 в таблицах U-Boot — Normal-память; с D-cache ядро читает
  почту из своего кэша и не видит записи core0 (владелец: «через почту глюки»).
  Поэтому маска SCTLR для вторичных ядер теперь `M|I` (0x1001), **без C**:
  выборка инструкций кэшируется, данные идут в DRAM напрямую.
- В воркере убраны `usb_cache_invalidate/clean` (при C=0 не нужны), остаётся
  `dsb st` перед `VIDW_DONE`. На core0 `usb_cache_clean(pBurnDraw)` остаётся —
  core0 пишет кадр через свой D-cache.
- Диагностика отката: `ARC: vid off (stall req=… done=…)` и
  `ARC: vid off (slow … us)` — причина видна в логе без догадок.
- Стенд r1686, замеры: baddudes 37.3 fps (emu 25.0, render 0.8), karnov 60 fps
  (emu 8.1–8.6, render 0.7), dbreed 47–53 fps (cpu 14.7–17.1, render 1.2–1.3).
- Сборка 25 857 124 Б, метка `r1688`, `linkcheck: OK`. **Стенд: ждём
  `ARC: vid CPU1, worker …` в игре и отсутствие строки `ARC: vid off`.**

## r1685 — CPU1 получил MMU + кэши, воркер переведён на NEON, конвейер включён

- Решение владельца: включить MMU/кэши на вторичных ядрах («через маску читай и
  меняй»). core0 кладёт в SRAM A1 `0x9040..0x9050` свои `TTBR0` (копия L0
  `0x4A40C000`), `TTBCR` (EAE=1), `DACR` и **маску** SCTLR (`M|C|I|+Z`); `cpu1_entry`
  ставит их, делает `TLBIALL` + `ICIALLU`, правит SCTLR только маской.
- `startup.S`: MMU/кэши включаются ПОСЛЕ VFP/NEON и ДО прыжка в `tft_core_main`.
  Фактический SCTLR пишется в `0x904C`, core0 печатает
  `MMU: cpu1 sctlr after=… (M=… C=… I=…)`.
- Воркер (CPU1) переведён на NEON-конверсию без LUT и обслуживает кэш:
  `usb_cache_invalidate()` входного `pBurnDraw` (core0 писал через свой кэш) и
  `usb_cache_clean()` выходного буфера (иначе DE2/DMA увидят старое).
- «Зомби» DMA от U-Boot: регистры контроллера `0x01C02000` только ЧИТАЮТСЯ и
  печатаются строкой `DMA: st …` — не трогаем USB OHCI/EHCI и DE2.
- `k_vidw_enable = 1` — конвейер включён; при 4 кадрах без ответа CPU1 или
  воркере дольше 8 мс конвейер сам откатывается на NEON-путь core0.
- Сборка 25 856 804 Б, метка `r1685`, `linkcheck: OK`. **Стенд: ждём лог
  `MMU: cpu1 … M=1 C=1 I=1`, `ARC: vid CPU1, worker < 1 мс`, TFT-меню живое.**

## r1684 — NEON-конверсия кадра (скорость за счёт памяти, конвейер CPU1 выключен)

- Владелец: «делай всё для скорости, памяти много». Ключевой факт: CPU1 стартует
  **без MMU и кэшей** (`startup.S`), поэтому пиксельная конверсия там читает LUT
  из DRAM на каждый пиксель — схема r1683 невыгодна.
- `cps_row_rgb32()` — NEON-конверсия строки RGB565→XRGB8888, 8 пикселей за
  итерацию (vld1.16 → vmovl → 3 сдвига/канал → vst1.32); формула совпадает с
  LUT пиксель-в-пиксель: `(r5<<19)|(g6<<10)|(b5<<3)`. Хвост `w%8` и FLIPPED —
  скалярный LUT-путь.
- Конвейер r1683 сохранён, но `k_vidw_enable = 0` (включается одной константой
  для стендового замера). Основной путь: NEON в буфер 0 + `usb_cache_clean`.
- Почта конвейера (`0x9000..0x9024`) и `h3_de2_ui_set_addr()` остаются — это
  база для этапа C (звук/DCS), где межъядерная схема обязательна.
- Сборка 25 856 228 Б, метка `r1684`, `linkcheck: OK`. **Стенд: ждём падения
  `render` с 1.6 мс (r1681) и отсутствия визуальных артефактов.**

## r1683 — видеоконвейер аркад на CPU1 (этап A плана PLAN-MULTICORE)

- CPU1 в игре свободен (tft_core_main при `0x74==1` только спит 30 мс), поэтому
  конверсия кадра RGB565→XRGB8888 ушла на него: core0 отдаёт задачу по почте в
  SRAM A1 (`0x9000..0x9024`, см. `docs/MEMORY.md`) и не ждёт; DE2 показывает
  предыдущий готовый буфер (`cps_rgb32[2]` в `.rgb_hi`, 2×480 КБ).
- `h3_de2_ui_set_addr()` — переключение только `TOP_LADDR` + `DBUFFER`, без
  перестройки слоя/скейлера (перекалибровка по-прежнему при смене w/h, r1506).
- Защита: первый кадр сессии — синхронный (в буфер 0); если задача висит
  >3 кадров (CPU1 занят TFT-меню/калибровкой), конвейер на сессию отключается и
  кадр снова конвертируется на core0 — картинка не замирает.
- Когерентность: `pBurnDraw` перед выдачей задачи чистится (`usb_cache_clean`),
  LUT — один раз за загрузку; CPU1 пишет буфер не-кэш напрямую в DRAM.
- Диагностика: строка `ARC: vid CPU1, worker X.XX ms` каждые 120 кадров —
  реальная стоимость конверсии на ядре без кэшей (решает судьбу схемы).
- Сборка 25 855 908 Б, метка `r1683`, `linkcheck: OK`. **r1684: конвейер
  выключен по умолчанию (`k_vidw_enable = 0`) — NEON на core0 быстрее.**

## r1682 — Irem M72: подпрофиль кадра, -O3 на NEC

- Стенд r1681: MCU починен — imgfight/loht запускаются, `mcu=12117`,
  `mcalls=285`, `mcyc≈12050`. Но темп 43 fps (`cpu 18.7` из `emu 20.6`,
  `snd 0.5`, `draw 1.0`, `render 1.6`). dbreed/rtype 50–55 fps.
- Добавлен подпрофиль внутри слайс-цикла: `g_m72_dbg_us_v30/mcu/scan/z80`
  (V30, i8751, scanline-прерывания, Z80+BurnTimer). В строке `IRM: dbg`
  печатаются как `v30=… mus=… scan=… z80=…`.
- `c1nec_nec.o` и `c1nec_v25.o` переведены на `-O3` (были на базовом `-O2`).
- Сборка 25 831 508 Б, метка `r1682`, `linkcheck: OK`.

## r1681 — i8051: счётчик циклов MCU по эталону (чинит imgfight/loht)

- Стенд r1680 (imgfight/loht): `mcalls=284 mcyc=1726531 mcu=0 tgt=12117` —
  драйвер за кадр просил у MCU 1.7 млн циклов вместо 12 117, но ядро
  показывало 0 пройденных. Значит счётчик не пополнялся, и `DrvMCUSync()`
  каждый слайс требовал всё больше — V30 висел в ожидании i8751.
- Причина в порте `mcs51.cpp`: счётчик прибавлял `(cycles - icount)` **до**
  исполнения (где `icount == cycles`) → всегда +0; накопления в конце не было.
- Приведено к эталону (`d025cfc`): `g_mcs51_cycle_start = cycles` в начале,
  в конце прогона `done = cycles - icount`, `cycle_start = icount = 0`,
  `g_mcs51_total += done`; `mcs51TotalCycles()` = `total + (start - icount)`;
  `mcs51NewFrame()` обнуляет оба счётчика.
- Сборка 25 830 548 Б, метка `r1681`, `linkcheck: OK`. **Стенд: ожидается
  `mcu ≈ 12117`, `mcalls=284`, `mcyc ≈ 12117` и рабочий геймплей imgfight/loht.**

### Состояние звуковых счётчиков (r1680, игры без MCU)

- `nmi` — ровно 142/кадр (dbreed, rtype2, xmultipl) ✔
- `len` — чередуется 872/873 (48000·100/5500 = 872.7) ✔
- `dac` — 2…32102, растёт при проигрывании сэмплов и стоит в тишине ✔
- `pairs` ≈ `fw`, `drop=0`; остаётся `under` (385 у dbreed) — это недобор
  кадров (`emu` 16.5–18.5 мс при бюджете 18.18), а не ошибка счётчиков.

## r1680 — Irem M72: MCU-вызов через обёртку, target в снимке

- Стенд r1679: `mcalls=0 mcyc=0` — это дефект диагностики, а не драйвера:
  макрос `CPU_RUN_SYNCINT(2, mcs51)` зовёт `mcs51Run` **напрямую**, минуя
  `DrvMCURun`, где стояли счётчики. `mcu=0` остаётся фактом.
- В `DrvFrame` вызов MCU заменён на `DrvMCURun(((i + 1) * nCyclesTotal[2] /
  nInterleave) - mcs51TotalCycles())` — то же выражение, что в макросе, но
  через обёртку: `mcalls`/`mcyc` теперь настоящие.
- В снимок добавлен `tgt` = `nCyclesTotal[2]` (ожидается 12117 при 8 МГц/12);
  `use_mcu` обновляется каждый кадр, а не только в первом.
- Строки: `IRM: early/dbg … mcalls=… mcyc=… tgt=…`.
- Сборка 25 830 484 Б, метка `r1680`, `linkcheck: OK`.

## r1679 — Irem M72: диагностика MCU (imgfight/loht висят)

- Стенд r1678: у imgfight/loht `use_mcu=1`, но **`mcu=0`** — i8751 не выполнил
  ни одного цикла; `nmi=0` и `dac=0`, потому что при `use_mcu=1` обе ветки
  сэмплирования в `DrvFrame` отключены (звук идёт через MCU). Кадров прошло
  19 и 8 (`pairs 16581`/`6981`), дальше стоп: V30 ждёт ответа MCU.
- Добавлены `g_m72_dbg_mcucalls`/`g_m72_dbg_mcucycles` (считаются в
  `DrvMCURun`) и печать `mcalls=… mcyc=…` в строках `IRM: early`/`IRM: dbg`.
  Это разделит две причины: драйвер не зовёт `mcs51Run` вообще или зовёт с
  нулевыми циклами (`nCyclesTotal[2]`).
- dbreed на r1678: 142 NMI/кадр, `dac` включился с 1080-го кадра
  (8929 → 28075 за 120 кадров), 47–53 fps, `under 958` — рванье звука идёт от
  недобора кадров (кольцо пустеет), не от NMI.
- Сборка 25 830 484 Б, метка `r1679`, `linkcheck: OK`.

## r1678 — Game Gear: включён аппаратный скалер DE2

- Вопрос владельца по логу GG (`GG: video rom=320x224 view=160x144 [cropped]`,
  строки `GG: scaler` нет). Код DE2-пути был с r1503 (`gg_render_frame()`:
  RGB565→XRGB8888 в `gg_rgb32` + `h3_de2_ui_scaler_rect`), но в цикле
  `emu_run_gg` (`emu.c`) его **никто не вызывал** — флаг
  `g_gg_hw_scaler_active` всегда 0, кадр шёл программным
  `emu_scale_int(160,144)`.
- Добавлен `extern void gg_render_frame(void)` и вызов сразу после
  `gg_run_frame()`. При успехе DE2 программный скейл пропускается, в логе
  появляется `GG: scaler DE2 GSU 32bpp 160x144->…`.
- Mega Drive не затронут: `md_render_frame()` вызывается из `gpgx_run_frame`
  (r1505).
- Сборка 25 830 292 Б, метка `r1678`, `linkcheck: OK`. Стенд — за владельцем.

## r1677 — Irem M72: слайсов 852 → 284

- `d_m72.cpp`: `multiplier` 3 → 1. Было 852 вызова `CPU_RUN(0,Vez)` и
  `CPU_RUN_TIMER(1)` на кадр, стало 284; NMI по-прежнему 142/кадр
  (`nSampleInt = 2`), scanline-прерывания — на каждую строку. Цель — снять
  накладные, чтобы `emu` влезал в 18.18 мс.
- Сборка 25 830 292 Б, метка `r1677`, `linkcheck: OK`.

## r1676 — Irem M72: сброс диагностики, печать первых кадров

- Статика диагностики (`g_m72_dbg_*`, `s_m72_*_acc`) теперь сбрасывается в
  `DrvInit` — раньше счётчики переходили в следующую игру.
- Хост печатает `IRM: early f=… use_mcu=…` для первых трёх кадров: у
  imgfight/loht зависание не доживает до отчёта раз в 120 кадров.
- `nspirit` убран из меню (в сете fbneo нет обязательного ROM MCU
  `nin_c-pr-b.ic1`), файл удалён и из `roms/irem`.
- Сборка 25 830 420 Б, метка `r1676`, `linkcheck: OK`.

## r1675 — Irem M72: снимок до VezClose

- Крах r1674 был не от printf, а от `VezTotalCycles()` **после** `VezClose()`:
  обращение к закрытому ядру (`PC=0x0E, LR=4`). Снимок `len/vez/mcu` берётся
  до закрытия V30.
- Стенд r1675 (dbreed, hharry, xmultipl, rtype2): **142 NMI/кадр**, `len`
  чередуется 872/873, `vez` 145613–145885 при номинале 145454, `emu` 17–19 мс
  при бюджете 18.18. Вывод: прерывания и длина звукового кадра корректны —
  звук рвётся из-за недобора кадров (кольцо i2s пустеет), а не от NMI.
- imgfight и loht по-прежнему зависают через несколько кадров
  (`use_mcu=1`, i8751) — открытый вопрос.
- Сборка 25 830 164 Б, метка `r1675`, `linkcheck: OK`.

## r1674 — Irem M72: диагностика ушла в хост (крах из-за printf в драйвере)

- Стенд r1673: после `M72D: first frame` — exception `PC=0x0E, LR=4,
  SP=0x600001D3` на dbreed и loht. Причина — `printf` из драйвера: в прошивке
  `printf` держит `char buf[640]` на стеке плюс newlib `vsnprintf`; из глубины
  `BurnDrvFrame` стек переполняется. В хосте тот же printf безопасен — там
  другой запас стека.
- Диагностика переведена на снимок в глобалы (`g_m72_dbg_*`), печать — в
  `cps1_host.cpp` рядом со строкой perf:
  `IRM: dbg f=… nmi=… dac=… len=… vez=… mcu=… use_mcu=…`.
  Норма: 142 NMI (REAL_NMI) или 142 DAC-записи (FAKE_NMI) за кадр.
- Сборка 25 830 164 Б, метка `r1674`, `linkcheck: OK`.

## r1673 — Irem M72: диагностика звука и зависаний

- Стенд r1672 (профиль): rtype держит 48–50 fps (`cpu 15–16`), dbreed 45–48
  (`cpu 17`), rtype2 51–53, xmultipl 46–48; `draw` 0.9, `render` 1.9 — то есть
  время почти целиком в интерпретаторе V30. imgfight и loht **зависают**
  после init (MCU-игры: i8751 + `DrvMCUSync` в каждом обращении к портам).
- Владелец: у rtype звук чистый, у rtype2/dbreed/xmultipl — «рычание, как
  50 Гц фон»; та же картина у Psikyo и Midway. По коду rtype — единственная из
  проверенных с `Z80_FAKE_NMI` (драйвер сам двигает сэмплер и пишет DAC),
  остальные — `Z80_REAL_NMI` (Z80-программа по NMI). Значит дефект в тракте
  Z80/NMI/DAC, а не в YM2151 или кольце i2s.
- Добавлена временная диагностика (`d_m72.cpp`): `M72D: f=%u nmi=%u dac=%u
  len=%d vez=%d mcu=%d` раз в 60 кадров и `M72D: first frame, mcu=%d` в начале
  первого кадра (отличает зависание внутри кадра от отсутствия кадров).
  Норма: 142 NMI/кадр (REAL_NMI) или 142 DAC-записи (FAKE_NMI).
- Сборка 25 830 100 Б, метка `r1673`, `linkcheck: OK`.

## r1672 — Irem M72: профиль кадра

- Стенд r1671: dbreed и hharry запускаются (init OK, 384×256, ввод по именам,
  8/10 слотов — остальные DIP), но темп 45–51 fps вместо 55: `emu 18.5 мс`
  при периоде 18181 мкс. `CPS: perf` показывал нули — профиля у M72 не было.
- Добавлен профиль `DrvFrame` M72 (`d_m72.cpp`): `g_m72_us_cpu` (V30 + i8751),
  `g_m72_us_snd` (Z80 + YM2151/DAC), `g_m72_us_draw` (DrvDraw). Хост печатает
  строку `IRM: perf N fps, emu X ms (cpu A snd B draw C), render D ms`.
- Кандидаты по коду: `nInterleave = 284 * 3` (852 вызова CPU_RUN на кадр),
  V30-интерпретатор, DrvDraw (284 строки tilemap+спрайты). Решение — по замеру.
- Сборка 25 829 780 Б, метка `r1672`, `linkcheck: OK`.

## r1671 — Irem M72: сеты ROM, nspirit исключён

- Из `roms/fbneo` в `roms/irem` перенесены 8 сетов: rtype, rtype2, imgfight,
  xmultipl, dbreed, hharry, loht, nspirit. Имена и размеры ROM сверены с
  `d_m72.cpp` — у 7 сетов совпадают полностью.
- `nspirit` убран из `pDriver`: в сете нет обязательного ROM MCU
  `nin_c-pr-b.ic1` (BRF_ESS), драйвер возвращает ошибку init. Нужен полный сет.
- В `roms/fbneo` нет также airduel, bchopper, ltswords, cosmccop, poundfor,
  majtitle; `rtypeleo` — плата M92, драйвера в прошивке нет (не копировался).
- Сборка 25 828 948 Б, метка `r1671` @ 0x40EA75E0, `linkcheck: OK`.

## r1670 — Irem M72: R-Type, R-Type II, Image Fight, X Multiply

- Вендорены `src/burn/drv/irem/{d_m72.cpp, irem_cpu.cpp/h}` из `d025cfc`.
  Новых CPU не понадобилось: V30 — `c1nec_*` (Toaplan), Z80, YM2151, DAC и
  mcs51 (i8051) уже собирались.
- Makefile: `CPS1_IREM := d_m72 irem_cpu`, объекты `c1ir_*`, `-I drv/irem`.
- `driverlist.h`: 14 родительских сетов (rtype, rtype2, imgfight, xmultipl,
  dbreed, bchopper, nspirit, airduel, hharry, ltswords, cosmccop, loht,
  poundfor, majtitle); клоны (kengo→ltswords, dkgensan→hharry,
  gallop→cosmccop, mrheli→bchopper) грузятся с родительских ROM.
- Система: `SYS("irem", "Irem (Arcade)", ARCADE, READY)`, корень `/roms/irem`,
  ветка `emu_run_irem`; `is_arcade_sys`/справка TFT дополнены.
- `cps1_host.cpp`: флаг `g_irem` по `HARDWARE_PREFIX_IREM`; DIP-дефолты,
  name-based ввод и отдельная ветка кадра 384×256 (общий путь дал бы 224
  строки). Вертикальные игры (rtype) выводятся без поворота — как у
  Toaplan/Cave, панель ставится вертикально.
- Правка эталона: в `mcs51.cpp/h` добавлена `mcs51Idle(cycles)` (в эталоне
  есть, в порте отсутствовала) — i8751 в M72 считается в idle-такте.
- Сборка 25 828 964 Б, метка `r1670` @ 0x40EA75E0, `linkcheck: OK`.
  **На стенде не проверено.**

## r1669 — Midway: inline-доступ к памяти ядра, профиль по фазам

- Вопрос владельца по r1668: что съедает CPU. По логу: `cpu` = 18.6–20.0 мс
  из 20.9 (MK1 в бою), `render` стабильно 1.6–2.3 мс, `draw` 1.0 → узкое место
  в эмуляции (TMS34010 + DCS), а не в выводе кадра.
- Новое ядро обращалось к памяти через внешние `TMS34010ReadWord/WriteWord` —
  вызов функции на каждый доступ интерпретатора (то же, что было в tms34 до
  r1663). Заведён `src/cpu/tms34010_fast.h`: структура карты + inline
  `tms_read_word/byte`, `tms_write_word/byte`; `tms34010_memacc.h` переведён на
  них. В `c1t34n_tms34010.o` — 0 вызовов `TMS34010ReadWord/WriteWord`
  (всего 82 `bl`), как в r1663 для старого ядра.
- Профиль `TUnitFrame` разбит по фазам слайса: `run` (CPU_RUN TMS34010),
  `scan` (GenerateScanline + рендер драйвера), `dcs` (IRQ+sync DCS).
  Строка: `CPS: perf N fps, emu X ms (run A scan B dcs C snd D draw E), render F ms`.
- Сборка 25 749 884 Б, метка `r1669` @ 0x40E9D250, `linkcheck: OK`.

## r1668 — Midway: ширина строки (pixperclock)

- Стенд r1667: кадр 400×256, 54.7 fps в меню и ~42 fps в бою (`emu 20.9 мс`,
  `cpu 19.1`), полный кадровый путь живой — но видна **только левая половина**
  изображения.
- Причина: старое ядро отдавало `heblnk/hsblnk` в пикселях
  (`SMART_IOREG(HEBLNK) * pixperclock`), новое отдаёт сырые пиксель-клоки.
  У T-unit `pixperclock = 2` (`TMS34010SetPixClock(4000000, 2)`) → драйвер
  получал 200 px вместо 400 и занулял правую половину строки.
- Правка: `g_tms_pixperclock` (в `tms34010_intf.cpp`) выставляется из
  `TMS34010SetPixClock()` (перенесён туда из `tms34010_compat.cpp`); в
  `generate_scanline` `heblnk/hsblnk` умножаются на него. `enabled` теперь
  реальный флаг `DPYCTL & 0x8000`, а не константа 1.
- Сборка 25 730 428 Б, метка `r1668` @ 0x40E98640, `linkcheck: OK`.

## r1666–r1667 — Midway: новое ядро TMS34010, старое удалено

- Ядро заменено на `src/cpu/tms34010/` из `d025cfc` (11 файлов:
  `tms34010.cpp/h` + 9 `*.h`, `tms34010_optable.cpp`). `dasm`/`new_dasm` не
  собираются — порту они не нужны. Старое `src/cpu/tms34/`, `tms34_intf.*` и
  `tms34_fast.h` удалены, ветка `make TU_CORE=old` и правило `c1t34_%` — тоже.
- Правки нового кода (все в `docs/VENDOR.md`): карта памяти в `BurnMalloc`
  (8 МБ статикой не влезали — `.bss` до `0x44C00000`), shift-колбэки через
  trampoline (`tms::dword` = `unsigned long`, `UINT32` = `unsigned int`),
  `TMS34010ScanConfig`/`TMS34010Exit` в `cpu_core_config` порта,
  `display_info += enabled` + заполнение `vsblnk/veblnk` (их читает
  `midtunit.cpp`).
- Новые файлы: `tms34010_compat.{h,cpp}` — мост к API драйвера
  (`Init(nCpu)`, `Exit`, `SetCpuCyclesPerFrame` — пустышки,
  `SetToShift/FromShift` — приведение `UINT16*`→`void*`; `SetPixClock` с r1668
  живёт в `tms34010_intf.cpp`). Тайминги дисплея новое ядро берёт из IOREG,
  счёт циклов/кадр задаёт сам драйвер слайсами, поэтому
  `TMS34010ResetPoolCache()` (r1661) стал не нужен и удалён.
- `Makefile`: `CPS1_TUCORE_OBJ` — постоянный список
  (`c1t34n_tms34010.o`, `c1t34n_tms34010_optable.o`, `c1i_tms34010_intf.o`,
  `c1i_tms34010_compat.o`), `-O3` на ядре сохранён.
- Сборка 25 730 356 Б (−185 688 к r1665), метка `r1667` @ 0x40E98600,
  `linkcheck: OK`. Ядро в образе: 304 символа `tms::*`.
- **На стенде не проверено** — ждёт прогона MK/MK2: кадр, кнопки, звук и
  `CPS: perf` (сравнить `emu` с 20.7 мс MK1 / 30.7–37.9 мс MK2 на старом ядре).

## r1665 — Midway: разбивка времени кадра T-unit (профиль)

- Стенд r1664: `-O3` на `midtunit.cpp` ничего не изменил (бои MK1 всё те же
  ~20.7 мс emu) → узкое место не в DMA-отрисовке драйвера. MK2: 25–30 fps
  (`emu 30.7–37.9 мс`) — сверху ложится DCS (ADSP2100).
- Добавлен внутренний профиль `TUnitFrame` (временный, вместе с `CPS: perf`):
  `g_tu_us_cpu/snd/draw` — мс на цикл 288 слайсов (TMS34010 + DCS sync),
  синтез звука в буфер и `TUnitDraw` (перенос кадра в pBurnDraw).
  Строка: `CPS: perf N fps, emu X ms (cpu A snd B draw C), render D ms`.
- Это даёт выбор следующего шага по фактам, а не наугад: чинить gfx-операции
  TMS34010 (пиксельные блоки), ADSP2100/DCS или кадровый путь.
- Сборка 25 916 044 Б, метка `r1665` @ 0x40EC4580, `linkcheck: OK`.

## r1664 — Midway: драйвер T-unit с -O3; проверка звукового тракта

- Стенд r1663: кнопки работают (`slots P1 10/10`), в меню/начале боя
  54.7 fps (`emu 14.8 мс, render 2.0 мс`), в бою просадка до ~42 fps
  (`emu 20.8 мс`) → кольцо i2s пустеет (`under`), в звуке провалы.
- Проверка звука: ресемплер ядер корректен — `nBurnSoundRate = 48000`
  выставляется до `BurnDrvInit`, YM2151/OKI рендерят сразу в 48 кГц
  (`burn_ym2151.cpp: nSamplesNeeded = nSegmentEnd * rate / nBurnSoundRate`),
  I2S тоже 48 кГц; `nBurnSoundLen` 877/878 по A-05. Хост-ресемплера нет —
  провалы только из-за недобора кадров.
- `c1mw_*` (midtunit.cpp с DMA-отрисовкой спрайтов) переведён на `-O3`.
- `.h` не менялись; пересобраны `c1mw_*` и линк.
- Сборка 25 915 212 Б, метка `r1664` @ 0x40EC4270, `linkcheck: OK`.
- Стенд: ожидает `CPS: perf` в бою (цель — emu ≤ 16 мс).

## r1663 — Midway: кнопки MK и inline-доступ к памяти TMS34010

- Стенд r1662 (замер): `emu ≈16–21.5 мс`, `render ≈2 мс` при бюджете 18.3 мс —
  кадр не держится, отсюда under и трески. (В r1662 принт печатал мкс как «мс»
  и делил fps на 1000 — в r1663 единицы исправлены.)
- Кнопки ударов не работали: T-unit объявляет их словами («P1 High Punch»,
  «P1 Block», «P1 High Kick», «P1 Low Punch», «P1 Low Kick», «P1 Block 2»), а
  `nam_write_input` знал только «Button N/Fire N». Слоты расширены до 13
  (4..9 — шесть кнопок), добавлены имена T-unit: a/b/c → HP/Block/HK,
  kick1/kick2/kick3 (Q/E/R) → LP/LK/Block 2.
- Производительность: чтение/запись памяти TMS34010 были внешними функциями
  (`tms34_intf.cpp`) — вызов на каждый доступ эмулятора. Перенесены в
  `tms34/tms34_fast.h` как `static inline` (карта памяти описана там же),
  `tms34_intf.cpp` и `tms34010.cpp` включают этот заголовок. Внешний API
  интерфейса не изменился.
- `.h` менялись (`tms34_fast.h`, `tms34_intf.h`) → чистая сборка обязательна.
- Сборка 25 913 996 Б, метка `r1663` @ 0x40EC3DB0, `linkcheck: OK`; в объекте
  ядра вызовов `TMS34010Read/Write*` не осталось (0 из 325 `bl`) — доступ
  полностью инлайнен.
- Стенд: ожидает проверки (MK/MK2: кнопки, `CPS: perf`).

## r1662 — Midway: замер производительности (временная диагностика)

- Стенд r1661: MK1/MK2 запускаются, звук есть, но кадр не тянется (54.7 Гц не
  держатся) → under в AUDIO-END, «наслоение»/трески. JIT у TMS34010 и ADSP2100
  нет: оба — интерпретаторы FBNeo, вся нагрузка на core0.
- В `cps1_host.cpp` добавлен временный замер (снять после стенда): раз в 120
  кадров строка `CPS: perf <fps> fps, emu <мс>, render <мс>` — реальный fps
  окна и средние затраты на `BurnDrvFrame` (включая звук) и на кадр (GSU/CPU).
- `tms34010.cpp` (`c1t34_`) и `adsp2100.cpp` (`c1ads_`) собираются с `-O3`
  (было `-O2`) — дешёвая попытка ускорить интерпретаторы.
- `.h` не менялись; пересобраны изменённые объекты.
- Стенд: ожидает `CPS: perf` из лога — по нему видно, упирается ли в эмуляцию
  (emu ≈ 20+ мс) или в конвертацию кадра (render).

## r1661 — Midway: DIP-дефолты MK1 и сброс кэша карты TMS34010

- MK1 уходил в сервисное меню: хост не выставлял DIP-дефолты драйвера
  (`MkDIPList`: Test Switch Off = бит 0x01, Skip Powerup Test — 0xF0 в DIP1),
  без них `DrvDips` = 0 = «Test Switch: On». Добавлен `g_mid` в вызов
  `arc_dip_defaults()`.
- MK2 падал, если запускался ВТОРОЙ игрой сессии (abort: DFAR=0x000A0031,
  LR→`TMS34010ReadWord`): `BurnInitMemoryManager()` обнуляет пул в каждом
  `BurnDrvInit`, а `MapStore` кэшировался между играми — карта писалась в зону,
  которую уже занимает `AllMem`, и затиралась. Добавлен
  `TMS34010ResetPoolCache()` (`tms34_intf.*`), хост зовёт его ДО `BurnDrvInit()`.
- `MAX_CPUS` 4→1 (`tms34_intf.cpp`): порт держит один TMS34010 (T-Unit);
  экономия 24 МБ пула FBNeo (карта — 8 МБ на CPU).
- `.h` не менялись; сборка чистая (`make clean && make`) по требованию владельца.
- Сборка 25 853 948 Б, метка `r1661` @ 0x40EB5300, `linkcheck: OK`.
- Стенд: ожидает проверки (MK1 — сразу игра, MK2 — вторым запуском).

## r1660 — Midway T-unit: Mortal Kombat 1/2 (TMS34010), звук ADPCM и DCS

- Вендор из эталона `d025cfc`: `src/cpu/tms34/*` + `tms34_intf.*` (TMS34010),
  `src/cpu/adsp2100/*` + `adsp2100_intf.*` (DCS-звук MK2), драйверы
  `src/burn/drv/midway/{midtunit.cpp,midtunit.h,midtunit_dma.h,d_tunit.cpp,
  dcs2k.cpp,dcs2k.h}`. Остальные файлы каталога midway (Wolf/Unit/X/Y/Kinst,
  williams_*) в дерево НЕ взяты.
- Makefile: `CPS1_MID := midtunit d_tunit dcs2k` (c1mw_), `c1t34_tms34010`,
  `c1ads_adsp2100`, `c1i_tms34_intf`, `c1i_adsp2100_intf`; в `CPS1_INC`
  добавлен `-I…/drv/midway`. `34010tbl/fld/ops/gfx.c` и `2100ops.c` входят
  ТОЛЬКО как #include внутрь `tms34010.cpp`/`adsp2100.cpp` (иначе двойные
  определения).
- `driverlist.h`: зарегистрированы mk, mkr4, mkplus, mktturbo, mk2, mk2r42,
  mk2r32e, mk2r31e, mk2r30, mk2r21 (T-unit; NBA Jam и прочие — позже).
- `cps1_host.cpp`: ветка `g_mid` (префикс `HARDWARE_PREFIX_MIDWAY`), кадр
  драйвера 400×254/256 с питчем 400·2; `CPS_MID_W` расширил клампы обоих
  рендеров (буфер `g_frame` 384×320 слов вмещает 400·256 = 102400), для
  `cps_rgb32` (122880 слов) добавлен guard объёма; ввод — по стандартным
  именам (`nam_write_input`), ключи как у прочих аркад.
- Правка эталона (`tms34_intf.cpp`): `MapStore[MAX_CPUS]` статикой занимал
  32 МБ `.bss` → линковка «.bss overlaps ZIP_BUF_BIG (0x44C00000)». Выделен
  через `BurnMalloc` при первом обращении (`MapStoreAlloc`), `.bss` не вырос.
- `systems.h`/`rom_browser`/`tft_drv`: система «Midway (Arcade)», корень
  `/roms/midway`, диспетчер `emu_run_midway`.
- Сборка 25 853 884 Б, метка `r1660` @ 0x40EB52C0, `linkcheck: OK`.
- Стенд: НЕ проверено (нужны ROM-сеты mk/mk2 в `/roms/midway`).

## r1659 — снята мёртвая диагностика r1630 в драйвере Psikyo; сверка с эталоном

- `d_psikyo.cpp`: удалены `PsikyoDbgCmd/Ack/Irq` и `PsikyoDbgCount()` — их
  никто не вызывал со времён r1630; `fm.c` возвращён к эталону (лишняя
  пустая строка).
- Сверка с эталоном `d025cfc` (2026-11-25): `d_psikyo.cpp`, `fm.c`,
  `ymf278b.c`, `cps_run.cpp` совпадают побайтово; расхождения — только
  описанные в `VENDOR.md` правки порта (лимитер YM2610/YMF278B, A-05).
- Апстрим после `d025cfc` ушёл на 494 коммита; по нашим путям значимо только
  `419854d` (fix raster CPS1) и `0f02e70` (старт кадра с vblank) — брать по
  жалобе. Подробности — `VENDOR.md`, раздел «Актуальность эталона».
- Сборка 25 524 108 Б, метка `r1659` @ 0xE70880. `linkcheck: OK`.
- Сравнение с релизом FBNeo на ПК (тот же ROM с SD): в порту звук есть,
  картинка без «расплывания» — разница в хост-слое (I2S, DE2 GSU, период),
  драйвер побайтово эталонный. Правило «не откатывать хост к эталону» —
  `docs/VENDOR.md`; итог аудита — `docs/AUDIT/SUMMARY.md`.

## r1658 — A-07: SNES hi-res 512 усредняется до 256, а не обрезается

- `snes_run_frame` копировал в EMU_FB только левые 320 колонок 512-широкого
  кадра — правые 192 не попадали в кадр вовсе (не «сжатие», а потеря).
- Теперь при `w > EMU_W` пары пикселей усредняются в 256 логических
  (маска `0xF7DE` — гасит LSB каналов R/G/B); эффективная ширина хранится в
  `g_eff_w` (сброс в `snes_init_game`) и используется и DE2-путём, и
  программным `emu_scale` — геометрия 256×h → `dw = 256*600/h`.
- Логическая ширина у SNES всегда 256 (и в normal, и в hi-res), поэтому
  слой DE2 не нужно перенастраивать при смене режима.
- Сборка 25 524 300 Б, метка `r1658` @ 0xE70940. `linkcheck: OK`.
- Стенд владельца: hi-res-кадр целиком — «Проверено ОК»; A-07 закрыт.
- A-16 закрыт (r1657, стенд): under ≈0.02% при drop=0, шум не воспроизводится.
- A-10 закрыт аудитом (r1658): вложенных использований `g_scratch_dir` в 13
  точках нет (`disk_swap` компактит в том же буфере при `w <= i` — безопасно),
  правило записано в `fat.h`. Только документация — прошивка не пересобиралась.

## r1657 — стенд A-05 ОК; убрана отладочная диагностика звука

- Стенд владельца r1656: NEOGEO `fill=811..1695/8192 drop=0 under=1`,
  Psikyo gunbird `fill=809..3707 drop=0 under=1547` — темп ровный, A-05
  подтверждён («работает отлично»).
- Убрано (вопросы закрыты): `PSI clip: peak/soft/lf` и НЧ-метр r1655,
  `PSI init: rate/len` (r1629 DBG), постоянный список `SND <game> cores:`
  (не меняется во времени).
- Оставлено: мягкий лимитер Psikyo (это звук, не диагностика) и строка
  `ARC in:` — реальные найденные слоты ввода, печатается раз на игру.
- Сборка 25 524 108 Б, метка `r1657` @ 0xE70880. `linkcheck: OK`.

## r1656 — A-05: точный темп звука (`nBurnSoundLen`)

- `BurnSetRefreshRate` давал постоянную длину кадра; для Psikyo (59.29 Гц)
  это 810 при среднем 809.58 → +25 пар/с: кольцо i2s медленно заполнялось
  (fill 810..4794 из 8192), продюсер ждал в `i2s_ring_wait`.
- `BurnDrvFrame` теперь пересчитывает `nBurnSoundLen` из накопителя остатка
  (809/810 вперемешку) — средний темп ровно `nBurnSoundRate`; накопитель
  сбрасывается в `BurnSetRefreshRate`.
- Сборка 25 524 620 Б, метка `r1656` @ 0xE70A80. `linkcheck: OK`.
- Стенд: ожидание — fill стабилен (~810..1700), under не растёт, drop=0.

## r1655 — мягкий лимитер ДО клипа ядра; НЧ-метр (Psikyo)

- Причина «рычания» низов: клип живёт ВНУТРИ ядер и срабатывает раньше
  хоста — `BurnYM2610Update` (FM + AY×1.2) и `BurnYMF278BUpdate` (OPL4×2.80,
  до 91747). Мягкий ограничитель r1651 в цикле кадра получал уже срезанный
  сигнал и мог только сглаживать клип.
- Введён `BURN_SND_SOFT` (порог 0.7 FS) и флаги `bYM2610SoftLimit` /
  `bYMF278BSoftLimit`: включаются только для Psikyo; NEOGEO и прочие — штатный
  `BURN_SND_CLIP`.
- Ресемплер к «рычанию» низов не причастен: 4-point cubic даёт плоскую АЧХ в
  полосе, а децимация без ФНЧ портит только верх (24…27.7 кГц). В r1654
  ресемплера нет вовсе (Normal-путь).
- Логи стенда r1654: under=553/1207 (~27/с, по 4 пары нулей = 0.08 мс),
  drop=0 — недолив ест 0.02% времени, «рычание» им не объясняется; `fw` в
  логе кумулятивен между играми (счётчик не сбрасывается, SKIP — спин).
- НЧ-метр: `PSI clip: peak=.. soft=.. lf=..` — пик после ФНЧ 119 Гц (отвечает,
  низы ли на пределе).
- Стенд r1655: gunbird `peak=12008 lf=977 soft=0` — клипа нет вообще (причина
  «рычания» была алиасингом ресемплера, снят r1653); s1945 `peak=27289
  lf=21837 soft=330..352` — жёсткий клип заменён плавным сжатием; tengai
  `peak=17953 lf=8078 soft=0`. ФВЧ в CPS стоит shift=9 (~15 Гц), бас не режет.
  under=322/1525/317, drop=0 — недолив ~0.02% времени.
- Сборка 25 524 492 Б, метка `r1655` @ 0xE70A00. `linkcheck: OK`.

## r1654 — Psikyo: Normal-путь YM2610 закреплён (звук)

## r1654 — Psikyo: Normal-путь YM2610 закреплён (звук)

- Стенд владельца на r1653: «рычание» gunbird заметно уменьшилось, звук
  сравнялся с s1945/tengai. Причина — алиасинг децимации 55.5→48 кГц, а не
  клиппинг; мягкий ограничитель r1651 остаётся страховкой от перегруза.
- `nFMInterpolation = 0` закреплён как постоянное решение для всех драйверов;
  комментарий в `cps1_host.cpp` переписан без остатков про native-режим.
  Возврат к 3 — только по решению владельца, с ФНЧ перед децимацией.
- Сборка 25 523 788 Б, метка `r1654` @ 0xE70770. `linkcheck: OK`.

## r1653 — тест: Psikyo без native-ресемплинга (nFMInterpolation = 0)

- По просьбе владельца — отделить алиасинг от клиппинга в жалобе на
  «рычание» YM2610 (gunbird). `nFMInterpolation` для всех драйверов = 0:
  чип рендерится прямо в 48 кГц (Normal-путь), без децимации 55.5→48.
- Это ТЕСТОВАЯ прошивка: если рычание/песок уйдёт — виноват ресемплер,
  тогда думаем про ФНЧ перед децимацией; если останется — причина в
  перегрузе маршрутов (мягкий ограничитель r1651 уже включён).
- Вернуть `nFMInterpolation = g_psikyo ? 3 : 0` (r1640), если Normal-путь
  не лучше: он нештатный для YM2610 и может звучать иначе на NEOGEO-сетях
  (там теперь тоже 0 — на NEOGEO проверялся именно этот режим, r1500).
- Сборка 25 523 788 Б, метка `r1653` @ 0xE70770. `linkcheck: OK`.

## r1652 — чистка мёртвой диагностики, реестр A-03, страховка кадра A-01

- Удалена отладочная обвязка, оставшаяся после r1634-r1639 (логи уже сняты):
  `g_dbg_ym2610w` (fm.c, инкремент в `YM2610Write`), `g_dbg_y2610_upd/pos`
  (burn_ym2610.cpp), `BurnYMF278BDebug` + `BurnYMF278BWrites` (обёртки и
  прототипы), `ymf278b_dbg` и все `m_dbg_*` в `ymf278b.c` — в том числе
  цикл подсчёта пика в горячем пути рендера OPL4.
- `docs/AUDIT`: A-03 переведён в «закрыто (r1649)» (bump откатывает хвост),
  описание в `01_memory.md` переписано по факту.
- A-01: кадр `g_frame` обёрнут в структуру `g_frame_mem`
  (0x440A8FA0 = кадр 0x3C000 + guard 0x100) — guard гарантированно ПОСЛЕ
  кадра, следующий символ `nNeoSMARNGAddress` сдвинут на 0x440E50A0.
  Размер закреплён `_Static_assert == 0x3C000`; клампы строк ≤320 в обоих
  путях рендера уже были.
- Сборка 25 523 788 Б, метка `r1652` @ 0xE70770. `linkcheck: OK`.

## r1651 — Psikyo: мягкий ограничитель звука вместо жёсткого клипа

- Владелец: «рычание» gunbird слышно только на активном звуке (на паузе
  чисто) — признак перегруза. Маршруты драйвера: OPL4 2.80, AY8910 1.20,
  YM2610 1.00; при сложении чипов пики упираются в `BURN_SND_CLIP` ядра
  (жёсткое обрезание) — это и есть «рычание».
- Правка (`cps1_host.cpp`, только `g_psikyo`): перед `i2s_push_block`
  сэмплы выше 0.7 FS (22937) проходят плавное сжатие
  `y = T + (a-T)*(32767-T)/((a-T)+(32767-T))`; до порога сигнал не
  меняется, громкость не снижается. При a=32767 выход 27852 (~85% FS).
- Базовая диагностика вместо удалённого `PSI snd`: раз в 300 кадров
  `PSI clip: peak=N soft=M` (peak — максимум |сэмпла| за интервал,
  soft — сколько сэмплов прошло через ограничитель).
- Сборка 25 524 620 Б, метка `r1651` @ 0xE70AB0.
- Стенд: если `soft=0`, клиппинга нет — причина «рычания» другая
  (алиасинг при 55.5→48 кГц), тогда следующий шаг — `nFMInterpolation=0`.

## r1650 — пул 108 МБ: svc загружает ADPCM (шипение), отладочные логи PSIKYO убраны

- Стенд r1649: `svc` дошёл до `init OK`, но при загрузке ещё один
  `pool exhausted` — `used=95 595 544 need=16 777 216 of 111 149 056`,
  и вместо тишины шипение. Значит откат хвоста сработал (было 110 МБ,
  стало 91.2 МБ), но ADPCM-блок YM2610 (16 МБ) не выделился: драйвер
  читает по NULL → мусор в звуке.
- Не хватало 1.2 МБ: 91.2 + 16 = 107.2 при пуле 106. Освобождён хвост
  `.noinit_fbn`: кадровые буферы `cps_rgb32` (0x78000), `s32x_rgb32`
  (0x4B000), `g_fb` (0x7D000) перенесены в новую секцию `.rgb_hi`
  0x53000000 (свободная зона 0x524A78A0..0x53E00000, `_rgb_hi_end` =
  0x53140000). Пул: `CPS1_MEM_POOL_SIZE` 106 → 108 МБ
  (0x55343900..0x5BF43900, до ZIP_BUF2 0x5C000000 остаётся 0.75 МБ).
- Убраны отладочные логи PSIKYO: блок `PSI snd` (26 полей каждые 300
  кадров) удалён из `cps1_host.cpp`. Осталась базовая диагностика:
  `PSI init` (rate/len), `ARC: …`, `AUDIO-END`. Счётчики `g_dbg_*` в ядре
  пока не удалены — отдельной правкой (r1651), чтобы не смешивать.
- Сборка 25 524 236 Б, метка `r1650` @ 0xE70920.
- Проверка на стенде: `svc` — `init OK` без `pool exhausted`, звук без
  шипения; s1945/gunbird — звук и наличие шума (отдельная причина).

## r1649 — NEOGEO svc: bump-пул теперь возвращает хвост (A-02, продолжение)

- Стенд r1648: `BurnMalloc pool exhausted: used=110 108 672 need=8 388 608
  of 111 149 056` — то есть постоянные данные svc (104.9 МБ) в пул влезают,
  а временный буфер загрузки C-ROM нет.
- Причина: `NeoLoadSprites` выделяет `pBuf1 = 2×C-ROM` (для svc 8+8 = 16 МБ),
  `load.cpp` — ещё буфер размером ROM; оба зовут `BurnFree`, а `_BurnFree` в
  bump-пуле только обнулял слот: `s_pool_used` не уменьшался, память не
  возвращалась до следующего `BurnDrvInit`.
- Правка (`burn_memory.cpp`): если освобождаемый блок — последний
  (`ptr + align8(size) == s_pool + s_pool_used`), `s_pool_used` откатывается.
  Временные буферы освобождаются в порядке LIFO, поэтому этого достаточно;
  ранние «дыры» не сжимаются (как и раньше).
- Пул не увеличивался, карта памяти не менялась.
- Сборка 25 524 876 Б, метка `r1649` @ 0xE70BA0. Нужен прогон svc: при успехе
  `ARC: init OK`, при повторе — снова строка `pool exhausted` с новым used.

## r1648 — видимая причина отказа NEOGEO-драйвера (диагностика)

- Стенд: `svc` — ROM 17/17 ok, но `CPS: svc init failed (BurnDrvInit)`. Причина
  в логе не видна: `_BurnMalloc` при исчерпании пула и `NeoInit` при отказе
  `LoadRoms` пишут через `bprintf`, а это заглушка (`burn.cpp:15-18`).
- Добавлено: `_BurnMalloc` печатает в UART
  `BurnMalloc pool exhausted: used=… need=… of 106 МБ`; `NeoInit` печатает
  `NeoInit: LoadRoms failed` на обоих путях. В релизе это срабатывает только
  при ошибке, в норме молчит.
- svc идёт путём `HARDWARE_SNK_NEOGEO` → `LoadRoms()` (`neo_run.cpp:4282`),
  не MVS/PCB. Нехватка памяти — первая гипотеза (ROM svc ~88.5 МБ при пуле
  106 МБ), но без новой строки её нельзя ни подтвердить, ни снять.
- Сборка 25 524 876 Б, метка `r1648` @ 0xE70BA0. Нужен прогон `svc` и 2–3
  строки лога.

## r1647 — возвращён «Video: no tearing (+1 frame)» (A-19)

- Владелец: «удалил зря, посчитал не нужным, а он нужен». Нашлось по истории:
  пункт назывался `SET_VIDEO_VSYNC` / `g_video_vsync` (r1183), удалён в
  r1521-r1538 (коммит `71ce12b`). Механика жива: `fb_flip_set(1)` собирает
  кадр в `FB_ADDR_BACK` (0x5F400000), `fb_flush()` переключает адрес DE2 UI
  (`h3_de2_set_ui_addr`), разрыв кадра уходит ценой +1 кадра задержки.
- Возвращено: `int g_video_vsync = 0;` в `emu.c`, `fb_flip_set(g_video_vsync)`
  в `emu_prepare()`; пункт в конец enum `SET_VIDEO_VSYNC`, label
  «Video: no tearing (+1 frame)», значение On/Off, Enter — тоггл.
  Хосты прямого вывода (C64/CPS1/Vectrex/BK и др.) по-прежнему зовут
  `fb_flip_set(0)` — режим к ним не применяется.
- В конфиг НЕ пишется (как было в r1183): переключение не трогает
  `remap_save()` и USB, поэтому невоспроизводим срыв из r1643.
- Игры на DE2 GSU (CPS/NES/SNES) рисуют в EMU_FB и этот режим не используют —
  там разрыв кадра лечится иначе (A-18, см. `docs/AUDIT/04_video.md`).
- Заодно убрана лишняя строка «Video: integer scale» из `set_labels`
  (в C превышение инициализаторов — warning, а не ошибка, поэтому r1646
  собиралась). Сборка 25 524 876 Б, метка `r1647` @ 0xE70B80.
- Стенд: не проверено.

## r1646 — видеофильтр убран (падал при переключении), A-18/A-19 сняты

- Владелец: переключение `Video: integer scale` роняло систему. Пункт
  `SET_VIDEO_FILTER`, ключ `video=` в `/retro.cfg`, режим `h3_de2_video_mode`
  и INTEGER-блок в `h3_de2_ui_scaler_rect` удалены полностью (проверено
  grep — ссылок нет). Скейлер вернулся к поведению до r1643.
- Причина падения: смена режима на живой системе вызывает
  `remap_save()` (запись `/retro.cfg`). В логе владельца в этот момент
  `msd: op=0x2A CSW fail` + `usb: ctrl TIMEOUT a=1 … cc=0/15/15` — срыв
  USB-масс-памяти; `h3_de2_ui_scaler_rect` при переинициализации на лету
  трогает `DBUFFER`/`CTRL` (r1466: включение скейлера на H3 гасило выход).
  Реализацию на живой системе (без перезагрузки) не делаем: риска больше,
  чем эффекта.
- Старой реализации «видеофильтра» в истории нет: A-19 подтвердил, что
  поиск по `video_filter|scanline|deflicker|smooth|bilinear` ничего не нашёл
  (удалён вместе со старым программным скейлером r968/r1512). Поэтому
  фильтр не восстанавливаем — концов нет.
- Джиттер строк в середине экрана (A-18) остаётся: он от дробного шага
  600/224 = 2.678, а не от отсутствия фильтра. Вариант с ×2 на лету
  признан нежизнеспособным (см. выше); если понадобится — только сценарий
  «выбор в меню до запуска ядра, с полной переинициализацией DE2».
- Сборка 25 524 748 Б, метка `r1646` @ 0xE70B20. Стенд: не проверено.

## r1645 — NES: Europe-ромы шли по NTSC (A-12)

- Причина: `nes_host_fceumm.cpp` звал `FCEUI_LoadGame("rom", …)`, а детект
  региона в `ines.c:1318-1332` (`iNESCart.region = 1`) ищет подстроки
  `"(Europe)"` / `"(PAL)"` / `"(E)"` **в имени файла**. С именем `"rom"`
  условие никогда не выполнялось → `FCEUI_SetVidSystem(0)` → `FSettings.PAL = 0`
  → `machine NTSC` в логе и `emu_set_period_us(16667)` вместо 20000.
  Игра на PAL-таймингах при 60 Гц кадра выглядит «быстрее чем PAL».
- Правка: `fceumm_init_game(rom, size, name)` — имя прокидывается из
  `emu_run_nes()` (`rom_name`) в `FCEUI_LoadGame` (пустое имя → `"rom"`).
  NES2.0 не затронут: у него регион берётся из байта заголовка, блок
  по имени под `if (iNESCart.iNES2 == 0)`. Файл `nes.pal` в этом не участвует
  (это палитра экрана, `FCEUD_LoadCPalette`), на темп она не влияет.
- Сборка 25 525 132 Б, метка `r1645` @ 0xE70C90.
- Стенд: не проверено.

## r1644 — удаление файлов: предел цепочки и ошибка без выхода (A-14)

- `fat.c`: `fat_free_chain` ограничивал обход жёсткими 100000 кластеров
  вместо числа кластеров тома — при кластере 512 Б крупный файл обрезал
  цепочку, файл исчезал, хвост кластеров оставался занятым (утечка места).
  Теперь предел `chain_guard_limit()` — как в `read_chain`/`find_dentry_loc`.
- `rom_browser.c`: при ошибке удаления (`fat_delete_file` < 0) браузер
  больше не закрывается молча в меню, а показывает «Delete failed» и имя
  файла, остаётся в списке.
- Формат/запись FAT не менялись: `fat_set_cluster` по-прежнему пишет все
  копии FAT (`g_num_fats`).
- Сборка 25 525 068 Б, метка `r1644` @ 0xE70C50.
- Стенд: владелец подтвердил, что удаление работает (r1643/r1644).

## r1643 — видеофильтр: целочисленный масштаб (A-18/A-19)

- `h3_de2.c`: добавлен режим `h3_de2_video_mode` (0 = SMOOTH — прежний
  дробный шаг 600/224 = 2.678, 1 = INTEGER). В `h3_de2_ui_scaler_rect` при
  INTEGER окно сужается до кратного ×k (`k = min(dw/src_w, dh/src_h)`, ≥1) и
  центрируется внутри исходной рамки — строки не «прыгают» между кадрами.
  Хостам менять ничего не нужно: точка масштаба одна.
- `settings.c`: пункт «Video: integer scale» (`SET_VIDEO_FILTER`, добавлен в
  конец enum — числовые значения прежних пунктов не сдвинуты). Переключение
  Enter или ←/→, значение применяется сразу и сохраняется.
- `remap.c`: ключ `video=0|1` в `/retro.cfg` читается при загрузке и пишется
  при `remap_save()` (иначе выбор терялся при сохранении раскладок).
- TFT-меню рисует первые 8 пунктов (`tft_drv.c` items[7]) — новый пункт
  виден в HDMI-меню; на TFT пока не отображается (отдельная задача).

Чистая сборка: `linkcheck: OK`, `h3_bare.bin` = 25 524 876 Б, метка `r1643`
в образе (@0xE70BA0).

- Версия: `g_fw_version = "r1643"`.

## r1642 — Этап 1 аудита: границы и мёртвый код

- **A-13:** убран дубль `#define LOG_MEMORY_USAGE 0` (`burn_memory.cpp`) и
  неиспользуемая `cps_hw_scaler_active()` (`cps1_host.cpp`). `sourcefile_table`
  оставлена — она используется (`burn.cpp:778-780`), предупреждение ложное.
- **A-04:** адреса ZIP-буферов вынесены в числа (`ZIP_BUF1_ADDR` и т.д.),
  добавлены `static_assert`: ZIP_BUF1+64 МБ ≤ 0x54000000, ZIP_BUF_BIG+88 МБ
  ≤ 0x4A400000, ZIP_BUF2+48 МБ ≤ 0x5F000000 (запас был 0 байт).
- **A-09:** `cpc_render_frame` клампит кадр до 768×272 с одноразовым логом.
- **A-06:** при `nBurnSoundLen > 2048` печатается одноразовое сообщение
  (раньше хвост кадра молча отбрасывался).

Чистая сборка: `linkcheck: OK`, `h3_bare.bin` = 25 524 492 Б, метка `r1642`
в образе (@0xE70A30).

- Версия: `g_fw_version = "r1642"`.

## r1641 — пул FBNeo 96 → 106 МБ (падение NEOGEO)

Реальные NEOGEO-сеты в `roms/neogeo`: суммы ROM-таблиц 85..89,5 МБ
(kof2003 89,5; svc/kof2000/mslug5/garou 88,5; kof2002/matrim 85,1), к ним
добавляются 68K/Z80 RAM, VRAM, палитры и буферы. Пул 96 МБ был впритык, а
исчерпание молча возвращало NULL (`bprintf` — заглушка, `burn.cpp:15-18`),
драйверы NULL не проверяют → падение без лога.

`CPS1_MEM_POOL_SIZE` = 106 МБ (`burn_memory.cpp`). До ZIP_BUF2 (0x5C000000)
было свободно ≈11 МБ; после пула лежат `cps_rgb32`/`s32x_rgb32`/`g_fb`
(~1,1 МБ), поэтому взят запас 10 МБ.

Адреса r1641: `pool_mem` 0x55343900 + 0x06A00000 → 0x5BD43900;
`cps_rgb32` 0x5BD43900, `s32x_rgb32` 0x5BDBB900, `g_fb` 0x5BE06900..
0x5BE83900 (< 0x5C000000, запас ≈1,5 МБ). `linkcheck: OK`,
bin 25 524 236 Б, метка `r1641` в образе (@0xE70920).

Не сделано (ждёт решения): сеты > 106 МБ (`cphd` 201 МБ и др.) в
`roms/neogeo` отсутствуют, но включены в меню — при их запуске будет тот же
молчаливый NULL; диагностика `pool exhausted` через реальный вывод не
включена.

Стенд (2026-11-25): **NEOGEO ОК** (владелец) — kof2003/svc/kof2000/mslug5/
garou работают, падений нет. A-02 закрыт на стенде.

## r1640 — nFMInterpolation задаётся для каждого драйвера

Флаг больше не протекает между системами: `nFMInterpolation = g_psikyo ? 3 : 0`
перед `BurnDrvInit`. Для NEOGEO остаётся 0 (его YM2610 проверен в этом
режиме), для Psikyo — 3 (native 55.5 кГц + ресемплинг).

## r1639 — gunbird: native-частота YM2610 + лог не в горячем цикле

Вопрос владельца «логи не портят звук?» — да, портят: UART синхронный
(`uart_putc` ждёт TX FIFO, `libc_min.c:377` — побайтово), 115200 8N1,
строка `PSI snd` ≈ 224 символа → **≈19.4 мс блокировки**, больше кадра
(16.86 мс). Раз в 60 кадров (≈1 с) это давало регулярную паузу; кольцо
(8192) её обычно перекрывает (fill до 3467 пар ≈ 72 мс), но при низком fill
пауза добавляет недоливы (`under`).

Изменения (одна сборка, заменяет непрошитую r1638):

- `PSI snd` печатается раз в **300** кадров (5 с) вместо 60 — в 5 раз меньше
  блокирующих пауз, поля те же.
- Для Psikyo включён `nFMInterpolation = 3` перед `BurnDrvInit`: YM2610
  (gunbird) рендерится на native-частоте чипа (clock/144 ≈ 55.5 кГц) с
  4-point ресемплингом в 48 кГц — штатный режим FBNeo. При `0` включался
  Normal-путь (чип рендерится прямо в 48 кГц). `Precalc` инициализирован
  с r1633, поэтому ресемплинг безопасен. s1945/tengai работают через
  YMF278B — на них флаг не влияет.

Чистая сборка: `linkcheck: OK`, `h3_bare.bin` = 25 524 236 Б, метка `r1639`
в образе (@0xE70920). Ждёт проверки gunbird на стенде (рычание).

- Версия: `g_fw_version = "r1639"`.

## r1637 — включена итоговая статистика кольца I2S (проверка «рвётся со временем»)

Подозрение: 810 пар/кадр при 59.3 Гц дают ~48 030 пар/с против 48 000 у I2S —
кольцо (8192 пары) медленно наполняется, и `i2s_push_block_raw()` при
переполнении теряет пары (`g_drop_cnt += n - written; break;` — `i2s.c:441`),
т.е. звук рвётся, накапливаясь со временем.

Статистика в `emu.c` уже была (`emu_audio_stop()` печатает
`AUDIO-END: fill=min..max/8192 drop=.. under=.. pairs=.. fw=.. skip=..`), но
выводилась только при `i2s_audio_diag() != 0`. В `run_cps` добавлен
`i2s_audio_diag_set(1)` (`cps1_host.cpp`, сразу после `i2s_dc_shift_set(9)`).

Чистая сборка (`make clean && make`): `linkcheck: OK`,
`h3_bare.bin` = 25 524 236 Б, метка `r1637` в образе (@0xE70920).

### Проверки gunbird «жирные/нечёткие буквы» (только эта игра)

- ROM: CRC32 всех 11 файлов `gunbird.zip` совпали с `gunbirdRomDesc`
  (4.u46 b78ec99d, 5.u39 925f095d, u14 7d7e8a00, u24 5e3ffc9d, u15 a827bfb5,
  u25 ef652e0c, u3 0905aeb2, u33 54494e6b, 3.u71 2168e4ba, u64 e187ed4f,
  u56 9e07104d) — набор тот, что ждёт драйвер.
- `psikyo_tile.cpp`, `psikyo_sprite.cpp`, `psikyo_render.h`, `psikyo_palette.cpp`,
  `psikyo.h` — MD5 совпадают с эталоном `d025cfc`; `d_psikyo.cpp` отличается
  ровно на 14 строк debug-счётчиков r1630 (функциональных правок нет).
- `psikyo_tile_func.h`/`_table.h` (сгенерированы нами): 12 вариантов,
  индексы 0..11; `nRenderFunction` 0/4/8 + 0..3 покрываются полностью,
  порядок NOCLIP/CLIP/ROWSCROLL совпадает с использованием в
  `psikyo_tile.cpp`. Отсутствие `+ nTileXPos*2` в rowscroll-ветке корректно:
  `psikyo_render.h:199` (`XPOS nTileXPos`) добавляет X внутри.
- `DrvDraw()` вызывается один раз за кадр (`d_psikyo.cpp:1733`),
  `BurnTransferCopy` — один раз, `BurnTransferClear` есть,
  прозрачный цвет и цвет очистки согласованы (`0x800`/`0x80f`).
- `nScreenWidth`/`nTransWidth` берутся из `BurnDrvGetVisibleSize` (320×224,
  `tiles_generic.cpp:29-44`, `319-337`) — совпадает с `nBurnPitch`=640.

Вывод: в коде порта отличий от FBNeo для gunbird не найдено; чтобы определить
природу артефакта, нужен снимок экрана/фото (двоение по X или Y) и A/B-тест
принудительного CPU-пути вывода для этой игры.

- Версия: `g_fw_version = "r1637"`.

## r1636 — OPL4: диагностика слотов; YM2610: кадровые рендеры

Лог r1635 закрыл две версии: `ovf=0` при `rmax` до 0x29c721 (2,7 МБ) — чтения
внутри PCM-ROM, «шум» не от `read_memory()`. У gunbird `y2610` растёт ровно
(~528 записей/с), т.е. Z80 непрерывно шлёт команды; `nz=0` в логе может быть
штатной тишиной игры (замечание владельца), поэтому судить только по кадрам,
где звук должен быть.

Добавлено (одна сборка):

- `ymf278b_dbg()`: `b3` — активные слоты с `bits==3` (в этом режиме
  `sample = 0`, провалы); `nenv` — слоты с `env_vol == 0` (заглохли по
  огибающей, накопление); `nst` — слоты с `stepptr >= endaddr` (позиция вне
  сэмпла). Обёртка пишет `sz/pos/fr` в `pOut[14..16]` (было 12..14 — сдвинуто
  под новые поля `o[11..13]`).
- `burn_ym2610.cpp`: `g_dbg_y2610_upd` (вызовы кадрового `BurnYM2610Update`)
  и `g_dbg_y2610_pos` (позиция рендера на входе) — видно, рендерит ли кадровый
  вызов или всё уже сделал stream-callback.

Хост печатает `b3 nenv nst sz pos fr y2610 y26u y26p` (массив `y[20]`).

Проверка: `linkcheck: OK`, `h3_bare.bin` = 25 524 236 Б, метка `r1636`
в образе (@0xDE0489). Ждёт лог в момент начала порчи звука.

Чистовая сборка (`make clean && make`) дала тот же размер и ту же метку,
символы `_Precalc` (.bss), `g_dbg_ym2610w`, `g_dbg_y2610_upd/pos` на месте —
инкрементальная сборка r1636 была корректной, причина не в сборке.

- Версия: `g_fw_version = "r1636"`.

## r1635 — лог r1634: ovf/rmax не выводились (перекрытие индексов)

В r1634 поля `ovf/rmax` печатались из `y[12]/y[13]`, но `BurnYMF278BDebug`
писал `sz/pos/fr` в `pOut[9..11]`, а `ymf278b_dbg` — `romovf/rommax` в
`o[9]/o[10]`: значения затирались, в логе всегда 0. Исправлено:
`ymf278b_dbg` отдаёт `o[0..10]`, обёртка пишет `sz/pos/fr` в `pOut[12..14]`,
хост печатает `ovf=y[9] rmax=y[10] sz=y[12] pos=y[13] fr=y[14]`.

Лог r1634 (tengai, s1945): оба звучат — `nz` до 1618, `mmax` 308..2010,
`yw` растёт (Z80 пишет в OPL4), `y2610=0` (у этих игр YM2610 нет — норма).
У tengai выход `max` за ~20 с падает 1447→512 при `sl` 8..10: накопление
или затухание музыки — проверять по `ovf/rmax` на r1635.

Проверка: `linkcheck: OK`, `h3_bare.bin` = 25 524 300 Б, метка `r1635`
в образе (@0xDE0499).

- Версия: `g_fw_version = "r1635"`.

## r1634 — диагностика «грязного» звука OPL4 и потока команд YM2610

Звук s1945 на r1633 подтверждён владельцем («есть, грязный»). Две проверяемые
гипотезы, обе требуют данных со стенда:

- «Грязь» OPL4 может быть чтением за границей PCM-ROM: `read_memory()`
  (`ymf278b.c:171`) при `offset >= m_romsize` возвращает `0xFF` — постоянный
  шумовой уровень. Добавлены счётчик `m_dbg_romovf` и максимум адреса
  `m_dbg_rommax`, в лог — поля `ovf=` и `rmax=`.
- Для gunbird прежнее поле `yw` бесполезно (это счётчик записей YMF278B, а там
  YM2610). Добавлен `g_dbg_ym2610w` в `YM2610Write()` (`fm.c:4170`) — видно,
  продолжает ли Z80 писать в чип, когда звук рвётся; в лог — поле `y2610=`.

Правки: `ymf278b.c` (счётчики + `ymf278b_dbg` o[9]/o[10]), `fm.c`
(`g_dbg_ym2610w`), `cps1_host.cpp` (массив `y[16]`, поля `ovf/rmax/y2610`,
объявление счётчика на file scope — внутри функции `extern "C"` недопустим).

Проверка: `linkcheck: OK`, `h3_bare.bin` = 25 524 300 Б, метка `r1634` в образе
(@0xDE048D). На стенде не проверено.

- Версия: `g_fw_version = "r1634"`.

## r1633 — тишина Psikyo PS3 (OPL4): не инициализирована таблица Precalc

Причина найдена по логу r1632 и коду: `lvl`=37315 (маршруты и громкость в
порядке), `sz`=60211 (ресемплинг 44.1→48 кГц настроен), `mmax`>0 (внутренний
микс чипа живой), а выход нулевой.

- `BurnYMF278BUpdate` (`burn_ymf278b.cpp:110-133`) **всегда** выводит звук через
  `INTERPOLATE4PS_16BIT`, т.е. через таблицу `Precalc`
  (`burn_sound.h:57`, 4096×4 INT16).
- `Precalc` заполняется только в `BurnSoundInit()`, а тот вызывается
  **единственным** местом — `BurnLibInit()` (`burn.cpp:167`).
- Хост `BurnLibInit()` намеренно не вызывает (`cps1_host.cpp:974`: он копирует
  имена драйверов, память под них не нужна) → таблица оставалась нулевой
  в `.bss` (`nm`: `_Precalc` = 0x43ec3ab0, 0x8000 Б, тип `B`), интерполяция
  умножала все сэмплы на 0.
- YM2610 (gunbird) этим не затронут: при `nFMInterpolation == 0` он идёт
  «Normal»-путём (`burn_ym2610.cpp:258`) без `Precalc` — поэтому звук был.

Правка: `cps1_host.cpp` — вызов `BurnSoundInit()` перед `BurnDrvInit()`.
Проверка: `linkcheck: OK`, `h3_bare.bin` = 25 523 916 Б, метка `r1633`
в образе (@0xDE0329), в объекте хоста `bl _Z13BurnSoundInitv` идёт перед
`BurnDrvInit`.

Стенд (владелец, s1945): **звук есть** (по описанию «грязный», но есть).
Лог подтверждает: `nz` 1506..1620 (было 0), `mmax` 162..1701, `sl` до 10,
`irq`/`cmd`/`ack` растут. Арифметика обёртки сходится: `fr` +12126 за кадр
(за 60 кадров это +6664 по модулю 65536), `pos` 0/1 — ресемплинг 44.1→48 кГц
работает штатно. «Грязный» тембр — отдельная задача (качество микса OPL4),
не блокирует.

`VENDOR.md` перенесён владельцем из `h3_bare/cores/cps1/` в `docs/VENDOR.md`;
ссылок на старый путь в документации не было.

- Версия: `g_fw_version = "r1633"`.

## r1632-doc — MEMORY.md актуализирован под реальную карту (документация)

Правка только `docs/MEMORY.md` (код прошивки не изменялся, сборка не
проводилась → `g_fw_version` в `h3_bare/src/main.c` остаётся "r1632").

Что было неактуально и исправлено (снято с `readelf -SW` / `nm -S -n`
`build/h3_bare.elf` свежей сборки r1632):

- `.text/.rodata/.data/.bss` пересняты: `.bss` = 0x41858000..0x44714520
  (≈48.9 МБ), `_bstart1`=0x41858000, `_hend`=0x44714520. Документ содержал
  два разных устаревших значения (`_hend`=0x44BB4C80 и 0x44B50BB0) — оба не
  совпадали с реальным ELF (класс G аудита).
- Свободная зона после `.bss`: 0x44714520..0x4A400000 (≈93 МБ);
- `.noinit_fbn`/`pool_mem`: реальный пул FBNeo = 0x55343900, size 0x06000000
  (96 МБ), конец 0x5B343900; верхняя граница ASSERT по-прежнему 0x5C000000;
- `ZIP_BUF1`/`ZIP_BUF_BIG`/`ZIP_BUF2` (cps1_host.cpp:146-150): ZIP_BUF1 —
  ≤64 МБ (0x50000000), ZIP_BUF_BIG — ≤88 МБ (0x44C00000), ZIP_BUF2 — ≤48 МБ
  (0x5C000000); убраны устаревшие «92 МБ»/«до 0x55C00000»;
- `h3_bare.bin` = 25 523 916 Б;
- вшитые BIOS в `.data`: `puae_kick13_rom`=0x415D8AB0, `puae_wb13_adf`=
  0x41618AB0 (были r1270-значения 0x413A91C0/0x413E91C0);
- пример `nm` в §8 обновлён на r1632.

Оставшееся намеренно НЕ тронуто: `_coherent` (4A400000..4A402E40),
`.swap_buf` (4A420000..4A620000), `.gb_heap` (4A700000..4BF00000),
sbrk-арена (4BF00000..4EF00000), стеки, LPAE-таблицы — совпали с реальной
картой. `.ep_bss` (0x52700000, Size=0) отмечен в отчёте аудита как пустая
секция (класс G) — к удалению в отдельной правке, здесь не менялся.

## r1632 — где теряется звук OPL4 (микс есть, выход нулевой)

Лог r1631 (s1945): `upd` и `rsm` растут (~44 651 сэмпл/с ≈ 44.1 кГц), `mmax`
ненулевой (до 11 331) — **внутренний микс слотов чипа звучит**. Но `nz=0`:
в выходном буфере нули. Значит обрыв между внутренним миксом и буфером хоста:
либо масштаб `vl/vr` (`m_mix_level[m_pcm_l]`) нулевой, либо обёртка читает не
то место (индексация/шаг ресемплинга), либо цикл копирования не выполняется.

Добавлены значения: `lvl` (`m_mix_level[m_pcm_l], m_mix_level[m_pcm_r]` — до
сих пор печатались только индексы `pv`), `sz` (`nSampleSize`, 0 = звук не
выводится), `pos` (`nYMF278BPosition` — сколько сэмплов отрендерено за кадр),
`fr` (`nFractionalPosition` на конец кадра — с него начинается чтение буфера).

- Версия: `g_fw_version = "r1632"`.

## r1631 — замер внутреннего микса OPL4

Лог r1630 по s1945/tengai: `cmd=ack` (команды 68K→Z80 доходят), `irq` растёт
(~100/с — таймеры чипа тикают), `yw` растёт (Z80 пишет в регистры OPL4,
до ~11 000 записей), `sl` > 0 (слоты включаются, до 14), `mem=1` (в RAM OPL4
заливается 1 байт — волны не грузятся), `pv=0,0` (регистр PCM-микса 0xF9 не
пишется; для OPL4 это норма — по даташиту дефолт 0 дБ). Выход при этом строго
нулевой.

То есть цепочка 68K→Z80→регистры чипа жива, ноты включаются, но PCM-движок
даёт нули. Добавлен замер **внутреннего микса** до масштабирования:
`upd` (сколько раз `ymf278b_pcm_update` вызван), `rsm` (сколько сэмплов
отрендерено), `mmax` (максимум |mix| после суммы слотов).
- `mmax=0` → слоты читают нулевые сэмплы/огибающую (ROM/адресация);
- `mmax>0`, выход 0 → виновато масштабирование/буфер обёртки.

Для gunbird в том же логе: `irq` тикает (~520/с), но `yw` замерзает на 4785
через ~20 с — Z80 перестаёт писать в чип; звук идёт рывками с затуханием.
Разбирать после OPL4.

- Версия: `g_fw_version = "r1631"`.

## r1630 — диагностика звуковой цепочки PS3

Стенд r1629: gunbird звучит отрезками и «рычит», s1945/tengai — полная тишина
(`PSI snd: nz=0`), при этом Z80 крутится (~67 460 циклов/кадр), ROM-сеты полные
(10/10, 11/11), `rate=48000 len=810`. Вендорные `d_psikyo.cpp`,
`burn_ymf278b.cpp`, `ymf278b.c` до этой правки были байт-в-байт равны апстриму
`d025cfc`, PCM-ROM (u61.bin, 2 МБ) в таблицах s1945/tengai помечен группой 6 и
уходит в `PsikyoSampleROM02` — как в апстриме.

Добавлены временные счётчики (снять после разбора):
- `d_psikyo.cpp`: `PsikyoDbgCount(0..2)` — команды 68K→Z80
  (`SendSoundCommand`), подтверждения Z80 (`ack`), IRQ от чипа
  (`PsikyoFMIRQHandler`);
- `burn_ymf278b.cpp`: `BurnYMF278BWrites()` — число записей в регистры OPL4;
  `BurnYMF278BDebug()` — байты, залитые в RAM OPL4 (`mem`, `write_memory` —
  заглушка: RAM не поддерживается), активные PCM-слоты (`sl`), уровни
  PCM-микса (`pv` = `m_pcm_l,m_pcm_r`);
- хост: строка
  `PSI snd: len=… nz=… max=… z=… cmd=… ack=… irq=… yw=… mem=… sl=… pv=…`;
- в логе `SND … cores` исправлена ложь `ym2610=stub` → `ym2610=real` (с r1627
  подключён настоящий YM2610).

Как читать: `cmd/ack` — доходит ли команда 68K до Z80 и отвечает ли он;
`irq` — тикают ли таймеры чипа (без них музыкальные движки стоят, а
ADPCM-эффекты играют — похоже на gunbird); `yw` — пишет ли Z80 в чип вообще;
`mem` — заливает ли игра волны в RAM OPL4; `sl/pv` — активны ли слоты и не
замьючен ли PCM-микс.

- Версия: `g_fw_version = "r1630"`.

## r1629 — gunbird в driverlist + диагностика тишины PS3

- `driverlist.h`: добавлен `BurnDrvGunbird` (pDriver 2001) — раньше в меню
  Psikyo был только s1945/tengai, поэтому `gunbird.zip` не подхватывался.
  ROM-таблица `gunbirdRomDesc` сверена с `gunbird.zip`: все 10 файлов
  совпадают по именам и размерам, NODUMP-записи (3020.u19/3021.u69) не нужны
  (r1628).
- Тишина PS3 разобрана по источнику: s1945 и tengai звучат через **YMF278B**
  (OPL4, `BurnYMF278BUpdate`), gunbird — через **YM2610**
  (`BurnYM2610Update`). Вендорные `d_psikyo.cpp`, `burn_ymf278b.cpp`,
  `ymf278b.c` байт-в-байт совпадают с апстримом `d025cfc`; PCM-ROM (u61.bin,
  2 МБ) в таблицах s1945/tengai помечен группой 6 и уходит в
  `PsikyoSampleROM02` — как в апстриме. Точная причина тишины пока не найдена,
  поэтому добавлена диагностика.
- Диагностика: после init — `PSI init: rate=<n> len=<n>` (задал ли драйвер
  `nBurnSoundLen`); раз в секунду в кадровом цикле —
  `PSI snd: len=<n> nz=<ненулевых сэмплов> max=<амплитуда> z=<циклы Z80 за кадр>`
  (выдаёт ли чип звук и жив ли звуковой CPU). После разбора логов лишнее снять.
- Версия: `g_fw_version = "r1629"`.

## r1628 — пропуск BRF_NODUMP в проверке ROM-сета

Стенд: s1945 выкидывал в меню ромов — `MISSING 4-u59.bin`,
`RS s1945: 10 ok / 11 total (1 missing, 0 bad size)`. Файл объявлен драйвером
как `BRF_PRG | BRF_ESS | BRF_NODUMP` (MCU Psikyo, размер 0x1000, CRC 0): он не
дамплен, ни в одном сете не лежит и драйвер его не грузит — `BurnLoadRom` в
`d_psikyo.cpp` использует только слоты 0..9. `check_romset()` пропускал только
`BRF_OPT`, поэтому NODUMP попадал в missing и старт блокировался
(`CPS: s1945 incomplete, not starting`).

- `cps1_host.cpp: check_romset()` — пропуск `BRF_OPT | BRF_NODUMP`.
- ROM-сеты на SD менять не нужно: `s1945.zip` полон, отсутствует только
  недокументированный файл.
- Версия: `g_fw_version = "r1628"`.

