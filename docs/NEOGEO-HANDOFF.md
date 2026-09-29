# Передача: NEOGEO (и аркады: Cave/Toaplan) — промт для нового чата

Служебный файл-хендовер после r0.318. Скопируй ПРОМТ целиком в новый чат
(вместе с правилами из п. docs/CPS1-HANDOFF.md — они общие).

---

## ПРОМТ ДЛЯ НОВОГО ЧАТА (скопировать целиком)

```
Ты — эксперт по низкоуровневому программированию на Allwinner H3.
Проект: bare-metal мультисистемный эмулятор на Orange Pi Lite (H3, 4×Cortex-A7),
каталог /home/mikl/pico-retro-new-gen, ветка main.

СБОРКА: `bash -lc 'make clean && make -j16'` (login shell; нужен shim ~/bin/cygpath).
Тулчейн xPack arm-none-eabi-gcc 15.2.1. Бинарь build/h3_bare.bin -> корень h3_bare.bin.
Версию баннера поднимать при КАЖДОЙ сборке: h3_bare/src/main.c (g_fw_version/banner).
Сейчас r0.318. ВНИМАНИЕ: после правки ЛЮБОГО .h обязателен clean && make.

СТАТУС: работают 19 систем + CPS-1/CPS-2 (FBNeo). CPS-1/2 в порядке.
NEOGEO — в работе: подключён (menu READY, BIOS neogeo.zip/неогео/, 687 драйверов),
но НЕ играется: первый кадр зависает в главном цикле кадра (
сек on второй итерации — IRQ/палитру мы починили, осталось что-то ещё).

ЗАДАЧА: доделать NEOGEO (см. docs/NEOGEO-HANDOFF.md) и после — Cave/Toaplan
(в дереве h3_bare/cores/cps1/src/burn/drv/{cave,toaplan}, но отключены из OBJ —
требуют CPU sh4/nec). Полный контекст и хендовер: docs/NEOGEO-HANDOFF.md.
```

---

## Состояние на передачу (r0.318)

- Ветка main синхронна до r0.317 (r0.318 собран, не коммичен — по правилу без «да»).
- NEOGEO вшит: 1491 драйвер в driverlist (CPS1 427 + CPS2 377 + NEO 687),
  systems.h neogeo READY, host-обёртка emu_run_neogeo -> run_cps("/roms/neogeo").
- BIOS: ищется в /roms/neogeo/neogeo/ (папка) и neogeo.zip (BRF_BIOS-путь).
- Zip-буфер 92 МБ (ZIP_BUF1 0x50000000, parent/bios 0x5C000000, ZIP_MAX2 48 МБ).
- Клавиатура: кэш без таймаута (по avatto-i8-pro-hid.md: у донгла НЕТ повторов,
  отпускание = отчёт с обнулённым кодом). Zалипание/замирание клавы — не доделано
  (событийная схема по документу — следующий шаг после NEOGEO).
- ПРОГРЕСС-БАР: load_progress/load_tick — работает; проверка до 256 слотов;
  пустые слоты не break (важно для NEO: BIOS на индексах 128+).

## Симптом (NEOGEO, r0.318)

2020bb: RS 13 ok, started (zip), затем:
FR 0, NF-ST, NF-B, NF-C, NF-X 1, NF-E — и зависание (второй проход главного
цикла кадра, внутри NeoSekRun/после; IRQ `NF-I` НЕ появляется).

## Что уже сделано/известно (не дублировать, идти дальше)

- Крэш P:0 (BL в BurnTimerUpdate) — ПОФИКШЕН: NULL-guard pTimerOverCallback
  и dummy pCPURunEnd (timer.cpp). Это было раньше (симптом ушёл).
- NeoPalette NULL — ПОФИКШЕН: в NeoInit добавлены NeoInitPalette()+NeoSetPalette()
  (r0.318). Симптом из первой итерации ушёл; хвост остался:
  зависание на ВТОРОЙ итерации главного while (NF-X 1 → NF-E → тишина).
- Маркеры в neo_run.cpp: NF-ST/NF-B/NF-C/NF-X n (каждые 100)/NF-I (в IRQ-блоке)/
  NF-E (после NeoSekRun) — ВРЕМЕННЫЕ, помечены DBG-TEMP (убрать после фикса).
- NeoNDA: подозревается, что первый проход выполнил NeoSekRun, вторая итерация
  входит в IRQ-блок/NeoSekRun и не возвращается: IRQ (VBlank/scanline-IRQ,
  nIRQControl&0x10) не взводится — периферия NEO (регистры) не реагирует.
  Искать: инициализация карты памяти NEO (SekMapMemory), обработчики в neo_rw/
  neo_run (NeoMemRead/NeoMemWrite), и NGEO-CPU-IRQ настройки в NeoInitCommon.
- x86-стенд NEOGEO почти готов: /tmp/gigatool/x86neo (объекты + neo_test).
  На хосте краш был в NeoClearScreen при пустой палитре; после фикса палитры
  тест НЕ пересобран — пересобрать x86-объект neo_palette.cpp + tmain_neo,
  прогнать: позволит дизассемблировать/дебажить быстрее.

## Направления
1. NEOGEO: почему IRQ не приходит (маркер NF-I пуст) — карта памяти/регистры.
2. После стабильного кадра: убрать NF-маркеры, проверить ввод/звук (звук off).
3. Клавиатура: событийная схема по avatto-i8-pro-hid.md (в /avatto-i8-pro-hid.md;
   лежит в корне — это ДОКУМЕНТ ПО ДОНГЛУ, не трогать как источник).
4. Cave/Toaplan: в дереве; OBJ отключён (c1v_/c1t_); требуют CPU sh4/nec
   (d_cv1k, d_batsugun и т.п.). После NEOGEO.
5. /tmp/gigatool/fbneo (клон FBNeo ~большой) — ВРЕМЕННЫЙ, почистить после завершения.
```