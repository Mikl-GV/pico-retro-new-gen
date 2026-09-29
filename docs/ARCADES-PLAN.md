# План: аркадные семейства плат из FBNeo в bare-metal H3

Обновлено: r0.382. Основа — каталог docs/FBNEO-ROMS.md (375 сетов, из них в
прошивке драйверы пока у 2 + Toaplan/Cave). Паттерн порта — как CPS/Toaplan/Cave.

## Принципы

- **Семейство** = группа плат FBNeo по общему видео/CPU (подпапка `burn/drv/<family>`).
- Вендор копируем **выборочно** (только нужные файлы семейства), не весь FBNeo.
- Интеграция по отработанной схеме:
  1. файлы в `h3_bare/cores/cps1/src/burn/drv/<family>/`
  2. секция объектов в Makefile (`c1*_` по семейству) + rename через `cps1_rename.sh`
  3. регистрация драйверов в `src/burn/driverlist.h` + pDriver[]
  4. host-ветка в `cps1_host.cpp`: `g_*` по `HARDWARE_PREFIX`, размеры кадра
     «как родное железо» (кадр ядра, rot=0), ввод через toa_input_cache-кэш
  5. systems.h/меню: пункт или общий корень; справка/About/Arcade-guide
  6. тест на железе (лог `CAV:`-стиля, экран, ввод, волны)

- CPU-база уже в дереве: 68K (m68k), Z80, NEC V25/V30, TMS32010, Z180, **M6805**
  (пригодится для Arkanoid). НЕТ: 6809, 6502, SH-2, SH-4.

## Приоритет семейств

| # | Семейство | CPU | Игры из каталога FBNEO-ROMs | Сложность | Ценность |
|---|-----------|-----|------------------------------|:---------:|:--------:|
| 1 | **Sega System 16** | 68K+Z80 | Shinobi, Altered Beast, Golden Axe, Fantasy Zone, Alien Storm, Shadow Dancer, Quartet, Espial | 🟡 средне | высокая |
| 2 | **Taito 68K (F2/F3)** | 68K(+Z80/M6805) | Bubble Bobble, Rainbow Islands, Arkanoid(+M6805), Rastan, NewZealand Story, Toki, Liquid Kids | 🟡 средне | высокая |
| 3 | **Data East 68K** | 68K+Z80 | Robocop 1-2, Bad Dudes, Sly Spy, Heavy Barrel, Karnov, Edward Randy, Boogie Wings | 🟡 средне | высокая |
| 4 | **Psikyo 68K** | 68K | Strikers 1945, Sengoku Blade, Gunbird | 🟠 высоко | средняя |
| 5 | **Midway 68K** | 68K | Mortal Kombat 1-3, NBA Jam, Smash TV | 🔴 видео T/V-unit | средняя |
| 6 | **Konami/Namco/Capcom ранние** | 6809/6502/custom | 1942, Commando, G'nG, Galaga, R-Type, Contra, … | 🔴 нужны CPU 6809/6502 | высокая по количеству |

## Вехи

- **V1 — Инфраструктура** (частично есть): универсальная host-ветка для любого
  семейства (папка, размеры, ввод, лог `SYS:`). Обобщить CAV-ветку.
- **V2 — Sega System 16 (пилот)** — драйверы + видео System 16 + host:
  Shinobi / Golden Axe / Altered Beast / Fantasy Zone.
- **V3 — Taito 68K** — основные + Arkanoid (M6805-протектор уже есть в дереве).
- **V4 — Data East 68K**.
- **V5 — Psikyo → Midway** (по остатку сил).
- **V6 — Новые CPU (6809/6502)** — отдельная веха: порт ядер, стабы, отладка.
- **V7 — Звук аркад в I2S** (YM2151/msm6295/AY8910 настоящий; сейчас стабы).

## Оценки

- Среднее семейство: 3–6 тыс. строк вендора + 1–2 дня интеграции по паттерну.
- Прошивка сейчас 12.7 МБ — место под 3–4 семейства есть.
- Звук остаётся off до V7 (как у CPS/Toaplan).

## Текущий статус

- r0.383-388: **V2 Sega System 16 подключена** (System 16A/16B/18: shinobi, goldnaxe, altbeast, fantzone, astorm, shdancer…; настоящие звуковые ядра YM2612/YM2413/YM2151/rf5c68/segapcm/dac/upd7759 + CPU i8051/i8039, host-ветка `S16:`, система `segasys`).
- r0.377-378: Cave-68K подключена (donpachi/ddonpach/esprade/guwange/feversos/…, система `cave`).
- r0.385-387: звуковой манифест `SND <sys> cores: …` во всех host-слоях (звук НЕ задействован, идёт послойное подключение).
- r0.382: справки/About/Arcade-guide актуальны.
- Следующий: **V3 Taito 68K**.

## Риски

- Защитные чипы (MCU) — частично покрыто (M6805, M68705).
- Видео Midway T-unit / System 32 — возможны просадки.
- Объём вендора контролируем только выборкой файлов семейства.