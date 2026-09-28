# CPS-1 (Capcom Play System 1) — вендор для H3 bare-metal

Источник: **FinalBurn Neo** — https://github.com/finalburnneo/FBNeo
Коммит: `d025cfc7965474e60094d8d34ab5ff6b91121382` (2026-09-28)

## Что здесь (подмножество FBNeo под CPS-1)

Скопировано из `src/` FBNeo (включается по мере порта):

| Путь здесь | Из FBNeo | Назначение |
|---|---|---|
| `src/burn/*.cpp,h` | `src/burn/` | framework burn (burn, burn_memory, burn_sound, burn_bitmap, burn_pal, tiles_generic, tilemap_generic, load, timer, hiscore, cheat, …) |
| `src/burn/drv/capcom/` | `src/burn/drv/capcom/` | драйверы Capcom: `d_cps1.cpp` (цель), `cps*.cpp` (mem/draw/pal/obj/run/scr/rw), `ps*.cpp` (защита FBNeo), `cpsr/cpsrd/cpst/ctv`, `d_cps2`/`d_mitchell`/`kabuki`/`qs*`/`fcrash_snd`/`sf2mdt_snd` (пока лишние, не подключаются) |
| `src/burn/snd/` | `src/burn/snd/` | звук: `ay8910`, `burn_ym2151`, `ym2151`, `burn_ym2203`, `msm5205`, `msm6295`, `samples` |
| `src/burn/devices/` | `src/burn/devices/` | `eeprom`, `i2ceeprom`, `timekpr` |
| `src/cpu/m68k/` | `src/cpu/m68k/` | Motorola 68000 (Musashi) — CPU платы |
| `src/cpu/z80/` | `src/cpu/z80/` | Z80 — звуковой CPU / вторичный |

Итого ~4.4 МБ, 105 файлов.

## Статус: порт подключён к прошивке (r0.260)

**В `Makefile`/`OBJ` подключено** — CPS-1 входит в основную сборку (коммит `9cb1e89`,
r0.260, бинарь 10 446 156 Б). Host-слой `h3_bare/cores/cps1_host.cpp`:
выбор драйвера по короткому имени игры, чтение ROM-сета из папки
`/roms/cps1/<игра>/` или zip, цикл `BurnDrvFrame`, кадр RGB565 384×224 → HDMI FB,
ввод P1 (ремап `REMAP_PLAT_CPS1`) + Sega-пад + P2 хардкод, ESC-выход, 60 Гц.
Символьные коллизии с другими ядрами (m68k против GPGX, YM2612 против gpgx sound)
сняты `cps1_rename.sh` (`m68k*`→`c1m68k*`, `YM2612*`→`c1YM2612*`) — правило
проверки (undefined нового ядра ∩ определения не-ядер = 0) выполнено.
`m68kops.c/h` генерируются на сборке нативным `m68kmake` в `build/` (в git не лежат).

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
Приоритет — папке. Игры-мишени: `wof`, `kod`, `unsquad`, `varth`, `willow`, `3wonders`.
