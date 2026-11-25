# CPS-1 (Capcom Play System 1) — вендор для H3 bare-metal

Источник: **FinalBurn Neo** — https://github.com/finalburnneo/FBNeo
Коммит: `d025cfc7965474e60094d8d34ab5ff6b91121382` (2026-09-28)

## Что здесь (подмножество FBNeo под CPS-1)

Скопировано из `src/` FBNeo (включается по мере порта):

| Путь здесь | Из FBNeo | Назначение |
|---|---|---|
| `src/burn/*.cpp,h` | `src/burn/` | framework burn (burn, burn_memory, burn_sound, burn_bitmap, burn_pal, tiles_generic, tilemap_generic, load, timer, hiscore, cheat, …) |
| `src/burn/drv/capcom/` | `src/burn/drv/capcom/` | драйверы Capcom: `d_cps1.cpp` (цель), `d_cps2.cpp` (CPS-2, подключён r0.272), `cps*.cpp` (mem/draw/pal/obj/run/scr/rw), `ps*.cpp` (защита FBNeo), `cpsr/cpsrd/cpst/ctv`, `kabuki`/`qs*`/`fcrash_snd`/`sf2mdt_snd` (не подключаются) |
| `src/burn/snd/` | `src/burn/snd/` | звук: `ay8910`, `burn_ym2151`, `ym2151`, `burn_ym2203`, `msm5205`, `msm6295`, `samples` |
| `src/burn/devices/` | `src/burn/devices/` | `eeprom`, `i2ceeprom`, `timekpr` |
| `src/cpu/m68k/` | `src/cpu/m68k/` | Motorola 68000 (Musashi) — CPU платы |
| `src/cpu/z80/` | `src/cpu/z80/` | Z80 — звуковой CPU / вторичный |

Итого ~4.4 МБ, 105 файлов (CPS-1; после добавления NEOGEO/Cave/Toaplan
подкаталоги `drv/neogeo|drv/cave|drv/toaplan` — существенно больше).

## Статус: порт подключён к прошивке (r0.318)

**В `Makefile`/`OBJ` подключено** — CPS-1/2 и NEOGEO входят в основную сборку
(бинарь r0.318 — 11 853 772 Б; 1491 драйвер: 427 CPS-1 + 377 CPS-2 + 687 NEOGEO).
Host-слой `h3_bare/cores/cps1_host.cpp`:
выбор драйвера по короткому имени игры, чтение ROM-сета из папки
`/roms/cps1/<игра>/` или zip, цикл `BurnDrvFrame`, кадр RGB565 384×224 → HDMI FB,
ввод P1 (ремап `REMAP_PLAT_CPS1`) + Sega-пад + P2 хардкод, ESC-выход, 60 Гц.
Символьные коллизии с другими ядрами (m68k против GPGX, YM2612 против gpgx sound)
сняты `cps1_rename.sh` (`m68k*`→`c1m68k*`, `YM2612*`→`c1YM2612*`) — правило
проверки (undefined нового ядра ∩ определения не-ядер = 0) выполнено.
`m68kops.c/h` генерируются на сборке нативным `m68kmake` в `build/` (в git не лежат).

### CPS-2 (r0.272+) и стабильность (r0.275–r0.281)

- **CPS-2 подключён** (r0.272): `d_cps2.cpp` в сборке, 377 драйверов CPS-2;
  `driverlist.h` объединён — 804 драйвера (427 CPS-1 + 377 CPS-2),
  `nBurnDrvCount = CPS_DRV_COUNT`. Host параметризован корнем ROM:
  `emu_run_cps1` → `/roms/cps1`, `emu_run_cps2` → `/roms/cps2`.
  Требование драйверов CPS-2: файл `*.key` (20 Б, `CPS2_ENCRYPTION_KEY`) в сете.
- **Вшитая проверка полноты ROM-сета** (r0.275): перед запуском сверяются имя
  и размер каждого активного слота (BRF_OPT/PLD исключены) по папке и zip
  игры/родителя; при missing/bad — список в UART и `ROM set incomplete`, игра
  не стартует. При `init failed` печатается список требуемых ROM.
- **BurnMalloc bump-пул 96 МБ в BSS** (r0.277; был 24) — крупные CPS-2
  (ddsom: gfx ~28 МБ) помещаются; сбрасывается каждым `BurnDrvInit`.
- **zip-буферы** (r0.318): основной 0x50000000 (92 МБ), родителя/BIOS
  0x5C000000 (48 МБ) — крупные сеты (NEOGEO ~84 МБ, CPS-2 ddsom ~33 МБ)
  запускаются из архива.
- **Ввод**: Sega-пад через `usb_pad_update()/usb_pad_get()` (кэш 12 мс +
  антидребезг 3 скана, r0.279); клавиатура — `usb_kbd_get_raw` в host-слоях,
  удержание не рвётся (кэш не сбрасывается при тишине отчётов, r0.284).
  Coin — S, Start — Enter/1.

### Промежуточный итог (2026-09-28): ядра CPU компилируются

Проверено отдельно от основной сборки (x86-прогон в /tmp/gigatool/cps1_build):
- **m68k (Musashi)**: `m68kcpu.c` + `m68kdasm.c` + сгенерированные `m68kops.c/h` — компилируются.
  Генерация таблицы: `m68kmake.c` (нативный) → `m68kmake <out>/ <m68k_in.c>` (1966 обработчиков).
  `m68kops.c/h` в git НЕ кладутся (генерируются на сборке).
- **z80**: `z80.cpp` компилируется (только warnings write-strings).
- Ключевые флаги сборки: `-DLSB_FIRST=1 -D__fastcall=` (иначе z80.h падает на `__fastcall`),
  includes: `burn/`, `burn/devices`, `cpu/…`, корень вендора (`src/tchar.h`).
- Адаптации вендора: `src/tchar.h` (шим FBNeo-TCHAR), `burn/devices/joyprocess.*`
  (тянет `burnint.h`).

### Прогресс: весь драйвер CPS-1 + framework компилируются (x86-прогон)

До-вендорено: `cpu/m68000_intf.*`, `cpu/z80_intf.*`, `cpu/m68000_debug.h`,
`burn/snd/fm.{c,h}` (+ `ymdeltat.h`, `biquad.h`), `burn/drv/capcom/ctv.h`
(генерируется нативным `ctv_make.cpp` — сгенерированный файл коммитим для
простоты, как и `m68kops` при сборке можно перегенерировать). Дособрано:
`cheat.cpp`, `hiscore.cpp`, `z80ctc/z80daisy/z80pio`.

Компилируются (43+ объектов): m68k (Musashi+m68kops), z80, burn framework,
звук/устройства, весь `drv/capcom` (включая `d_cps1`), `cps_config`.
Адаптации: `burn/driverlist.h` (только наши игры + пустая `sourcefile_table`).

**Осталось (что должен дать host-слой — список undefined из линковки x86):**
1. **Звуковое ядро**: `AY8910*`, `YM2203*`, `YM_DELTAT_ADPCM_*` (fm.c/ay8910.c
   не дают эти символы в текущей конфигурации; нужен либо под-вендор ymfm-ядра,
   либо включение FM-конфига). Звук на первом этапе — off → допустимы стабы.
2. **burn_debug/OS glue** (стабы): `Debug_*` ×13, `SekDbg*`/`ZetDbg*`, `szApp*Path`,
   `MovieInfo`, `Reinitialise`, `is_netgame_or_recording`, `TCHARToANSI`.
3. **Ввод/аналог** (стабы/реальные): `ProcessAnalog`, `AnalogDeadZone`,
   `nInputIntfMouseDivider`, `nSocd`.
4. **IPS-патчи** (стабы): `IpsApplyPatches`, `bDoIpsPatch`, `nIpsMemExpLen`.
5. **ZIP**: `ZipLoadOneFile` (реальный — zlib из Fuse, чтение `/roms/cps1/*.zip`).
6. Мелочи: `pDataRomDesc`, `pRDI`, `bDrvOkay`, `BurnYM2203/2608/2610/2612UpdateRequest`,
   `nDrvOkay` и т.п.

После стабов — ядро линкуется на x86; затем интеграция в H3 (objcopy-переименования,
`cps1_host.c`, Makefile).

### Итог этапа (x86): **LINK OK**

Полный набор слинкован (тестовый exe на хосте). Рабочие флаги сборки:
`-D__fastcall= -DLSB_FIRST=1 -DEMU_M68K -DFBNEO_DEBUG`
(`EMU_M68K` — выбор m68k-бэкенда; `FBNEO_DEBUG` — определения `SekDbgFetch*Dispatcher`,
на которые ссылается `m68kdasm`), плюс `-fno-strict-aliasing`.

Важно: **`fm.c` компилировать как C** (gcc, не g++) — тогда он даёт реальные
`YM2203*`/`YM_DELTAT_*`; как C++ получаются C++-имена и всё рассыпается. `ay8910.c`
в этой конфигурации не отдаёт `AY8910*` (guard) — пока стабы.

Glue-стабы `src/cps1_stubs.cpp` входят в основную сборку как `c1x_stubs.o`
(Debug_* флаги, OS-пути `szApp*`, `pRDI/pDataRomDesc`, `Reinitialise`, IPS,
`AnalogDeadZone`/`ProcessAnalog`, `TCHARToANSI`, `ZipLoadOneFile`,
`MovieInfo`, `AY8910*` (звук off), `BurnYM2608/2610/2612UpdateRequest`,
`DebugSnd_AY8910Initted`, `DebugTrackerExit`, `clock()`) и
`src/cps1_netg.cpp` (`c1x_netg.o`: `is_netgame_or_recording`).

Сделано (r0.260): host-слой `cps1_host.cpp` (папка/zip, кадр → HDMI FB, ввод, ESC),
objcopy-переименования `cps1_rename.sh` (m68k/YM2612), подключение в Makefile,
меню `cps1` (READY). Осталось: стенд (на железе), по необходимости звук.

## Что ещё нужно (актуально)

1. ~~Host-слой~~ — **сделан** (`cps1_host.cpp`).
2. ~~Чтение ROM~~ — **сделано**: папка или zip (`ZipExtract` + zlib из Fuse).
3. ~~Совместимость/стабы~~ — **сделано** (`cps1_stubs.cpp`, `cps1_netg.cpp`).
4. ~~objcopy-переименования~~ — **сделано** (`cps1_rename.sh`: `m68k*`→`c1m68k*`, `YM2612*`→`c1YM2612*`).
5. ~~Память~~ — **сделано**: буферы из newlib bake-кучи (`_hend`), сброс `emu_prepare()`.
6. ~~Видео~~ — **сделано**: `pBurnDraw` → прямой ресайз в HDMI FB (как bk_host).
7. ~~Меню~~ — **сделано**: `systems.h` READY, браузер папок и zip.
8. **Звук** (PSG/YM2151/QSound → I2S) — на будущее.
9. **Стенд** — первая проверка на железе с реальными ROM.

## ROM-формат для SD

```
/roms/cps1/<игра>/   ← папка с сырыми дампами чипов (имена из MAME-сета, не менять)
/roms/cps1/<игра>.zip ← альтернатива (распакуется zlib)
```
Приоритет — папке. Игры-мишени CPS: `wof`, `kod`, `unsquad`, `varth`, `willow`,
`3wonders`. Плюс CPS-2 и NEOGEO (MVS/AES), см. `driverlist.h`.

## Namco (r1610)

Добавлено семейство Namco — отдельный корень ROM `/roms/namco` (система
`namco` в меню). Скопировано из того же коммита `d025cfc`:

| Путь здесь | Содержимое |
|---|---|
| `src/burn/drv/namco/` | `d_galaxian` + `gal_gfx`/`gal_run`/`gal_sound`/`gal_stars`/`cclimber_audio`, `d_pacman`, `d_rallyx`, `d_galaga`, `d_gaplus`, `d_mappy`, `d_pacland`, `d_namcos1`, `d_namcos86`, `d_skykid`, `d_namcos2` |
| `src/cpu/m6809/`, `src/cpu/m6809_intf.*` | Motorola 6809 (Mappy, Orange Pi-совместимые платы, System 1/86/2) |
| `src/cpu/m6800/`, `src/cpu/m6800_intf.*` | M6800/HD63701 (вспомогательный CPU Namco) |
| `src/cpu/s2650/`, `src/cpu/s2650_intf.*` | Signetics 2650 (старые galaxian-платы) |
| `src/burn/snd/namco_snd.*` | WSG (8-канальный волновой синтез Namco) |
| `src/burn/snd/sn76496.*`, `flt_rc.*`, `digitalk.*`, `c140.*`, `timeplt_snd.*` | PSG/DAC/фильтры, C140 (System 2), Rally X-звук |
| `src/burn/devices/namcoio.*`, `earom.*`, `c169.*`, `namco_c45.*` | ввод-контроллеры, EAROM, ROZ-контроллеры System 2 |

`ay8910.c` теперь реальный в сборке (`CPS1_SNDC`), заглушки AY8910 в
`src/cps1_stubs.cpp` удалены — иначе Pac-Man/Galaga читали бы вход через
пустой PSG-порт. Единственный владелец `Debug*`-флагов — `burn/debug_track.cpp`.

ROM для Namco: `/roms/namco/<игра>.zip` (или папка), имя = short name
(`pacman`, `galaga`, `mappy`, `rthunder`, `dsaber`, …), см. `driverlist.h`.

## Taito (r1623)

Семейство Taito — отдельный корень ROM `/roms/taito` (система `taito` в меню).
Копия из того же коммита `d025cfc`:

| Путь здесь | Содержимое |
|---|---|
| `src/burn/drv/taito/` | `d_taitomisc` (Rastan, Rainbow Islands, Volfied), `d_bublbobl` (Bubble Bobble), `d_arkanoid` (Arkanoid) |
| там же | общие обвязки: `taito.cpp/.h`, `taito_ic.*`, `cchip.*` (C-Chip), `tc0100scn`, `tc0110pcr`, `tc0140syt`, `tc0150rod`, `tc0180vcu`, `tc0220ioc`, `tc0280grd`, `tc0360pri`, `tc0480scp`, `tc0510nio`, `tc0640fio`, `pc080sn`, `pc090oj` |
| `src/cpu/upd7810/`, `src/cpu/upd7810_intf.*` | NEC uPD7810 — MCU «C-Chip» (Rainbow Islands); интерфейс внутри `upd7810.cpp`, 7810ops/7810tbl включаются в него |
| `src/burn/snd/burn_ym3526.*` | YM3526 (OPL) — звук Bubble Bobble |

CPU M68K/Z80/M6805 для этих плат уже были в дереве. `taito_ic.cpp` — общий
контейнер IC: он ссылается на все `tc*`/`pc*`/`cchip`, поэтому набор файлов
копируется целиком (пригодится для F2/F3 и других плат Taito).

ROM: `/roms/taito/<игра>.zip`, имя = short name (`rastan`, `rbisland`,
`volfied`, `bublbobl`, `arkanoid`).

## Data East (r1626)

Семейство Data East — отдельный корень ROM `/roms/dataeast` (система `dataeast`
в меню). Взято из того же коммита `d025cfc`:

| Путь здесь | Содержимое |
|---|---|
| `src/burn/drv/dataeast/d_dec0.cpp` | DEC0: `robocop`, `baddudes`, `hbarrel` (видео/таймеры внутри драйвера) |
| `src/burn/drv/dataeast/d_karnov.cpp` | `karnov` (68K+6502, MCU 8751 через mcs51) |
| `src/cpu/m6502/` + `src/cpu/m6502_intf.*` | Motorola 6502 (DEC0, Karnov); t*.c/ops*.h включаются внутрь `m6502.cpp` |
| `src/cpu/h6280/` + `src/cpu/h6280_intf.*` | Hudson HuC6280 (DEC0); `tblh6280.c` внутри `h6280.cpp` |
| `src/cpu/i8051/mcs51.*` | MCS-51 (8751 в Karnov) — был в дереве, объект `c1cpu_mcs51.o` |

Звук — уже собранные `msm6295`, `burn_ym2203`, `burn_ym3812`, `burn_ym3526`.
Общий `deco16ic` (для darkseal/robocop2 и др.) НЕ копировался — эти игры в
следующей порции вместе с `deco146`/`timer`.

ROM: `/roms/dataeast/<игра>.zip`, имя = short name (`robocop`, `baddudes`,
`hbarrel`, `karnov`).

## Psikyo (r1627)

Семейство Psikyo PS3 — отдельный корень ROM `/roms/psikyo` (система `psikyo`
в меню). Взято из того же коммита `d025cfc`:

| Путь здесь | Содержимое |
|---|---|
| `src/burn/drv/psikyo/d_psikyo.cpp` | PS3: `s1945` (Strikers 1945 World), `tengai` (+ gunbird/gunbirdj и клоны, если положить сеты) |
| `src/burn/drv/psikyo/psikyo_sprite.cpp`, `psikyo_tile.cpp`, `psikyo_palette.cpp` | общий рендер PS3 |
| `src/burn/drv/psikyo/psikyo_tile_func.h`, `psikyo_tile_func_table.h` | **сгенерированы** скриптом `src/dep/scripts/psikyo_tile_func.pl` (в апстриме их нет) |
| `src/burn/snd/burn_ym2610.cpp` | YM2610 (OPNB через fm.c) — теперь настоящий, не стаб |
| `src/burn/snd/burn_ymf278b.cpp` + `ymf278b.c/.h` | YMF278B (часть игр PS3/PS4) |

CPU: M68K и Z80 уже в дереве. Звук NEOGEO с r1627 идёт через тот же реальный
YM2610 (стабы из `neostubs.cpp` удалены).

ROM: `/roms/psikyo/<игра>.zip`, имя = short name (`s1945`, `tengai`, `gunbird`,
`gunbirdj`, …).

## Правки эталона, сделанные портом

Правило (`docs/AUDIT/SUMMARY.md`): любая переделка кода FBNeo фиксируется
здесь — иначе следующий перенос вернёт поведение эталона.

### Звук: мягкий лимитер до клипа (r1655)

| Файл | Правка | Зачем |
|---|---|---|
| `src/burn/burn_sound.h` | `BURN_SND_SOFT` — мягкий лимитер (порог 0.7 FS, колено 22937) | штатный `BURN_SND_CLIP` срезает ±32767 с обрывом; Psikyo давал до 91747 после микса ×2.80 |
| `src/burn/snd/burn_ym2610.cpp` | `INT32 bYM2610SoftLimit` (0); в двух местах `BURN_SND_CLIP` → `bYM2610SoftLimit ? BURN_SND_SOFT : BURN_SND_CLIP` | включается только для Psikyo из `cps1_host.cpp` |
| `src/burn/snd/burn_ymf278b.cpp` | `INT32 bYMF278BSoftLimit` (0); так же вокруг микса ×2.80 | то же |

Уровни микса (`d_psikyo.cpp`: YM2610 1.00, YMF278B 2.80, PCM 1.20) не
менялись — они штатные, сверены с апстримом `d025cfc`.

### Точный темп звукового кадра (r1656)

| Файл | Правка | Зачем |
|---|---|---|
| `src/burn/burn.cpp` | `nBurnSoundLen` пересчитывается в `BurnDrvFrame` из накопителя остатка (`s_snd_acc`, шаг `rate*100/fps`) | постоянная длина 810 при 59.29 Гц давала +25 пар/с: кольцо i2s медленно заполнялось, продюсер ждал |

`BurnSetRefreshRate` сбрасывает накопитель. Средний темп — ровно
`nBurnSoundRate`, длина кадра чередуется 809/810.

## Актуальность эталона (проверка 2026-11-25)

Сверено по репозиторию `finalburnneo/FBNeo`: наш коммит `d025cfc` (2026-09-28)
существует; master ушёл вперёд на 494 коммита (`b454793`, 2026-11-25).
Правки после `d025cfc` в путях, которые держит порт:

| Путь | Коммиты | Нужно порту |
|---|---|---|
| `drv/psikyo/d_psikyo.cpp` | `2eddde4` big-endian (PS1/PS4) | нет: ARM little-endian, PS3 |
| `drv/psikyo/d_psikyosh.cpp` | `01bdf40`, `2c6dac3` (PS4/SH2) | нет: PS4 не подключён |
| `snd/burn_ym2610`, `fm.c`, `msm6295`, `ymdeltat`, `burn_ym2151` | `eeddae3`, `e1c4caa` — save-state/rollback | нет: save-state/rollback не используются |
| `snd/samples` | `af5e474` mp3/flac | нет |
| `burn/burn.cpp` | строки, артворк, румбл, headless | нет |
| `drv/capcom/*` | `419854d` fix raster, `0f02e70` старт с vblank, наборы CPS2 | **единственное по делу**: растеризация CPS1 и −1 кадр задержки ввода; брать по жалобе |
| `cpu/z80` | `699f4f0` и др. — рефакторинг API | нет |

Побайтовая сверка наших копий с `d025cfc` (2026-11-25): `d_psikyo.cpp`,
`fm.c`, `ymf278b.c`, `cps_run.cpp` — совпадают; `burn_ym2610.cpp` (+5/−4),
`burn_ymf278b.cpp` (+9/−2), `burn.cpp` (+14) — только описанные выше правки
порта. Вывод: «рычание» и клип не связаны с версией эталона — уровни микса
штатные, правок обычного (не save-state) звукового пути после `d025cfc` нет.
