# План: аркадные семейства плат из FBNeo в bare-metal H3

Вендор — `h3_bare/cores/cps1/` (подмножество FBNeo). Подключено: CPS-1, CPS-2,
NEOGEO / MVS, Sega System 16, Toaplan 1 (68K), Cave (68K), сводный FB Neo
(корень `/roms/fbneo`). Каталог ROM по системам — `docs/FBNEO-ROMS.md`.

## Принципы порта (отработанный паттерн)

1. Файлы семейства — в `h3_bare/cores/cps1/src/burn/drv/<family>/` (выборочно).
2. Секция объектов в Makefile (`c1*_` по семейству) + objcopy-переименование
   `cps1_rename.sh` (снятие коллизий m68k*/YM*/... с другими ядрами).
3. Регистрация драйверов в `src/burn/driverlist.h` + pDriver[].
4. Host-ветка в `cps1_host.cpp`: `g_*` по `HARDWARE_PREFIX`, размеры кадра «как
   родное железо» (rot=0), ввод через `toa_input_cache`-кэш.
5. `systems.h`/меню/справка/About/Arcade-guide.
6. Тест на стенде (экран, ввод, звук).

## Статус (r776)

- **Звук аркад подключён** (r607): nBurnSoundRate=48000, pBurnSoundOut=cps_snd,
  драйверы рендерят звук только при pBurnSoundOut != NULL. Все семейства — со
  звуком на I2S.
- Готово: Sega System 16 (r0.383), Cave 68K (r0.377), Toaplan (r0.360), CPS-1/2,
  NEOGEO (r0.32x).
- CPU-база: 68K (m68k), Z80, NEC V25/V30, TMS32010, Z180, M6805, i8051/i8039.
  НЕТ: 6809, 6502, SH-2, SH-4.

## Очередь семейств

| # | Семейство | CPU | Игры | Сложность |
|---|-----------|-----|------|:---------:|
| 1 | **Taito 68K (F2/F3)** | 68K(+Z80/M6805) | Bubble Bobble, Rainbow Islands, Arkanoid, Rastan, NewZealand Story, Toki | 🟡 |
| 2 | **Data East 68K** | 68K+Z80 | Robocop 1-2, Bad Dudes, Sly Spy, Heavy Barrel, Karnov, Boogie Wings | 🟡 |
| 3 | **Psikyo 68K** | 68K | Strikers 1945, Sengoku Blade, Gunbird | 🟠 |
| 4 | **Midway 68K** | 68K | Mortal Kombat 1-3, NBA Jam, Smash TV | 🔴 (T-unit) |
| 5 | **Konami/Namco/Capcom ранние** | 6809/6502 | 1942, Commando, G'nG, Galaga, R-Type | 🔴 (нужны CPU) |

## Риски

- Защитные чипы (MCU) — частично покрыто (M6805, M68705).
- Видео Midway T-unit / System 32 — возможны просадки.
- Объём вендора — только выборкой файлов семейства.