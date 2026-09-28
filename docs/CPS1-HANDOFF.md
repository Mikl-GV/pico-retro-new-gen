# Передача: CPS-1 (Capcom Play System 1) — новый чат / продолжение

Служебный файл-хендовер. Содержит готовый промт для нового чата и весь
накопленный опыт, чтобы продолжение шло корректно.

---

## ПРОМТ ДЛЯ НОВОГО ЧАТА (скопировать целиком)

```
Ты — эксперт по низкоуровневому программированию на Allwinner H3.
Проект: bare-metal мультисистемный эмулятор на Orange Pi Lite (H3, 4×Cortex-A7, 512 МБ DDR3),
каталог /home/mikl/pico-retro-new-gen, ветка main.

ЗАДАЧА: продолжить порт эмулятора аркадной платы Capcom CPS-1 (вектор FinalBurn Neo).
Статус: вендор в h3_bare/cores/cps1/ полностью компилируется и ЛИНКУЕТСЯ на x86 (тест),
но НЕ подключён к основной сборке. Осталось: host-слой, objcopy-переименования,
Makefile/меню, стенд. Полный разбор — docs/CPS1-HANDOFF.md и h3_bare/cores/cps1/VENDOR.md.

СБОРКА: `bash -lc 'make -j16'` (login shell обязателен, нужен shim ~/bin/cygpath).
Тулчейн xPack arm-none-eabi-gcc 15.2.1. Бинарь build/h3_bare.bin → корень h3_bare.bin.
Версию баннера поднимать при КАЖДОЙ сборке: h3_bare/src/main.c — g_fw_version и "build: … r<N>".
Сейчас r0.259 (+ CPS-1 wip в дереве, не в сборке).
ЗАГРУЗКА: бинарник в корень ложить, пользователь сам прошивает.

ПРАВИЛА (обязательны):
- Коммитить/пушить только по явному «да». Без него — локальные правки/сборки.
- Не принимать самовольных решений по поведению — спрашивать.
- sega_pad.c — НЕ ЛАЗИТЬ (тайминги эмпирические).
- Не трогать экраны/TFT и SRAM A1 без отдельного разрешения.
- В Makefile НЕТ отслеживания зависимостей от заголовков: после правки ЛЮБОГО .h
  обязателен `make clean && make` (иначе .o не пересоберутся и бинарь будет старым).
- Правка vendored-обёртки (напр. h3_bare/cores/fuse/fuse/fuse.c через src/fuse/fuse.c)
  НЕ пересобирает объект: удалить build/fz_*.o или touch обёртки; проверять mtime.
- Опыт одноимённых символов (важно!): у vendored-ядер общие имена (m68k/z80/callbacks/
  io_init/…) конфликтуют под -Wl,--allow-multiple-definition → extern привязывается к
  ЧУЖОЙ ячейке → NULL-call / prefetch abort / «залипания» (так падал BK: input_state_cb).
  Лечится objcopy-переименованием в *_rename.sh конкретного ядра. ПРОВЕРКА при интеграции:
  undefined-символы нового ядра ∩ определения не-ядерных объектов = 0.

СОСТОЯНИЕ: работают 19 систем (A2600, A5200, A7800, NES, SMS/GG/MD, GB/GBC, GBA, Lynx, NGP,
SNES, MSX, ColecoVision, PC Engine HuCard, Portfolio, ZX Spectrum, BK-0010/0011M).
CPS-1 — в работе (вендор+линк на x86 ok, не в сборке).

СРОЧНО/В РАБОТЕ: продолжение CPS-1 по docs/CPS1-HANDOFF.md (шаги host/renames/Makefile/меню).
```

---

## Состояние на момент передачи

- Ветка `main` = `origin/main`, дерево чистое; последний коммит — `a3237a8`
  (wip CPS-1: LINK OK на x86). Версия прошивки — **r0.259** (стабильна, собирается).
- CPS-1 вендор лежит в `h3_bare/cores/cps1/` (~4.4 МБ, FBNeo, коммит `d025cfc`),
  **в `OBJ`/Makefile НЕ подключён** — основная прошивка не затронута.

## Что сделано по CPS-1 (x86)

- Скопировано подмножество FBNeo: framework `burn/`, `drv/capcom` (`d_cps1`, `cps*`,
  защита `ps*`), звук/устройства, CPU (`m68k` Musashi, `z80`) + intf-слои.
- Адаптации вендора: `src/tchar.h` (шим), `burn/driverlist.h` (наши 6 игр +
  пустая `sourcefile_table`), сгенерированные `m68kops.c/h` (через `m68kmake`) и
  `ctv.h` (через `ctv_make`).
- Glue-стабы: `src/cps1_stubs.cpp`, `src/cps1_netg.cpp`.
- **Результат: весь набор компилируется и ЛИНКУЕТСЯ на x86** (тестовый exe).

### Флаги сборки CPS-1 (проверены)
`-D__fastcall= -DLSB_FIRST=1 -DEMU_M68K -DFBNEO_DEBUG -fno-strict-aliasing`
- `EMU_M68K` — m68k-бэкенд; `FBNEO_DEBUG` — нужны `SekDbgFetch*Dispatcher` (ссылается `m68kdasm`).
- **`fm.c` компилировать как C** (gcc) — тогда реальные `YM2203*`/`YM_DELTAT_*`.
- `ay8910.c` в этой конфигурации не отдаёт `AY8910*` (guard) — пока стабы (звук позже).
- Генерация: `m68kmake <out>/ <m68k_in.c>` → `m68kops.c/h`; `ctv_make > ctv.h`.

## Осталось (порядок)
1. **host-слой `cps1_host.c`** (по образцу `bk_host.c`/`pce_host.c`):
   - инициализация драйвера по короткому имени игры (BurnDrvInit), ROM-загрузка из
     `/roms/cps1/<игра>/` (папка с сырыми дампами чипов — приоритет) или
     `/roms/cps1/<игра>.zip` (`ZipLoadOneFile` через zlib из `cores/fuse/zlib`);
   - кадр: `pBurnDraw` (RGB565) → `EMU_FB` → `emu_scale`; ввод (usb_kbd + sega_pad);
     ESC-выход; 60 Гц (`emu_throttle`); `emu_prepare()` на входе (сброс памяти).
2. **objcopy-переименования**: m68k против Genesis Plus GX (MD), z80 против GPGX,
   `d_cps1`-специфичные против прочих ядер — по образцу `cores/bk/bk_rename.sh` /
   `cores/gc_rename.sh`. ОБЯЗАТЕЛЬНО проверить: undefined нового ядра ∩ defs не-ядра = 0.
3. **Makefile**: отдельная группа CPS-1 (свои флаги, как `BKFLAGS`/`FUSEFLAGS`),
   цель генерации `m68kops`/`ctv.h`, `cps1_host.o` — подключать только когда соберётся.
4. **Меню**: `cps1` в `systems.h` (ARCADE) → браузер `/roms/cps1/*` (папки и zip).
5. **Стенд**: первая игра (рекомендуется `wof` — Warriors of Fate; также `kod`,
   `unsquad`, `varth`, `willow`, `3wonders`). ROM пользователь кладёт сам.

## Опыт одноимённых переменных (конкретика проекта)
- **BK-0010 (r0.252):** под `--allow-multiple-definition` глобальные `input_state_cb`/
  `environ_cb` и пр. у ядра BK совпали с другими ядрами → extern из `tty-libretro.o`
  привязался к чужой ячейке → `blx 0` → prefetch abort (`P:00000000`). Лечение — добавить
  имена в `bk_rename.sh` (→ `bk_*`). Проверка: `undefined bk-объектов ∩ определения не-bk = 0`.
- Аналогично переименовывались PCE/Fuse/Gearcoleco/GBA (`pce_wrap`, `fuse_*`, `gc_*`, `gpsp_*`).
- Для CPS-1 это ключевой шаг перед подключением в Makefile.

## Чистая сборка (gotchas)
- Нет `-MMD`/`-include *.d` → правка .h без `make clean` не пересоберёт .o. Всегда
  `make clean && make -j16` после правок заголовков (`systems.h`, `bk_host.h` и т.п.).
- Vendored-обёртки (Fuse) — удалять соответствующий `.o`/`touch` обёртки.
- Проверка: `stat -c '%y' build/<obj>` vs `.h`; короткая сборка: `make -j16` не должна
  быть «Цель all не требует выполнения команд», если менялось что-то реальное.

## Ключевые файлы
- `h3_bare/cores/cps1/` (+ `VENDOR.md`) — вендор и стабы CPS-1.
- `h3_bare/cores/bk/bk_rename.sh`, `cores/gc_rename.sh` — образец переименований.
- `h3_bare/cores/bk_host.c`, `pce_host.c` — образец host-слоя.
- `h3_bare/cores/emu.c` (`emu_prepare`, `emu_scale`, `emu_throttle`), `emu.h`.
- `h3_bare/src/main.c` — меню/диалоги, версия.
- `docs/ROADMAP.md` (п.8 CPS-1), `docs/AUDIT-2026-09-27.md`, `docs/ARCHITECTURE.md`.
