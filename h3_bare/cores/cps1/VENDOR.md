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

## Статус: порт в работе, СБОРКА НЕ ЗАТРОНУТА

**В `Makefile`/`OBJ` НЕ подключено** — текущая прошивка собирается как прежде.
Подключу к сборке, когда подмножество скомпилируется (правило проекта: сборка
остаётся зелёной; вендор кладём отдельно).

## Что ещё нужно для порта

1. **Host-слой `cps1_host.c`** по образцу `bk_host.c`/`pce_host.c`: ROM-загрузка, кадр → EMU_FB, ввод, ESC, frame-loop.
2. **Чтение ROM**: папка `/roms/cps1/<игра>/` с сырыми дампами чипов (приоритет) или `/roms/cps1/<игра>.zip` (zlib уже есть в дереве Fuse, `h3_bare/cores/fuse/zlib`).
3. **Совместимость**: FBNeo тянет libretro-common/OS-stdlib — нужен минимальный шим (как `fuse_stubs.c`/`msx_compat.c`), убрать/заглушить `file_*`, `dynhuff`, CD и пр.
4. **objcopy-переименования**: символы `m68k` конфликтуют с Genesis Plus GX (MD), Z80 — с GPGX; переименовать по образцу `bk_rename.sh`/`gc_rename.sh`.
5. **Память**: буферы ROM/RAM платы — из `_gb_heap` (bump), сброс на входе (`emu_prepare`).
6. **Видео**: FBNeo рисует в `nBurnPitch`-буфер RGB565 → копировать в EMU_FB → `emu_scale`.
7. **Меню**: пункт `cps1` (systems.h, ARCADE) → браузер `/roms/cps1/*` (папки и zip).

## ROM-формат для SD

```
/roms/cps1/<игра>/   ← папка с сырыми дампами чипов (имена из MAME-сета, не менять)
/roms/cps1/<игра>.zip ← альтернатива (распакуется zlib)
```
Приоритет — папке. Игры-мишени: `wof`, `kod`, `unsquad`, `varth`, `willow`, `3wonders`.
