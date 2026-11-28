# ARCADE_PLAN — аркадные системы: анализ, статус, план

Дата: 2026-11-27. База: прошивка **r1734**, стенд Orange Pi Lite (Allwinner H3).
Цель: достойное быстродействие аркад (паспортная частота драйвера, чаще 60 Гц)
на достижимых системах; раз за итерацию — одна система.

> Все цифры разделены на **измерено на стенде** (HANDOVER, `CPS: perf`/`IRM: perf`),
> **внешний ориентир** (MAME 0.250 на Raspberry Pi 2B, A7 0.9 ГГц) и **оценка**.
> Оценки помечены явно; там, где данных нет, так и написано.

## 0. Краткий вывод

1. **Аркадный слой уже единый и рабочий:** все системы идут через один host
   `run_cps()` (`h3_bare/cores/cps1_host.cpp`), ядро — вендорный FBNeo
   (`h3_bare/cores/cps1`, коммит d025cfc), 2024 драйвера в `driverlist.h`.
2. **Лёгкие системы (CPS-1, Namco, Taito, Sega System 16, Data East, Irem M72,
   Toaplan-классика) — на границе 60 Гц или выше.** Их нужно не переписывать,
   а измерить и точечно добить рендер/звук.
3. **Тяжёлые системы (CPS-2, Neo Geo, Cave 68K, Midway T-unit) — 25–80 %
   от 60 Гц.** Главный резерв — не «разнести по ядрам», а ускорить 68000
   (Cyclone) и видеоконвейер конкретного драйвера.
4. **Многоядерность не даёт универсального ускорения.** MAME официально
   отказался разносить CPU-ядра по потокам (совместимость), WinUAE выносит
   только чипсет. Наш собственный эксперимент r1694 (видеоконвейер на CPU1)
   проиграл синхронной NEON-конверсии на core0 в 8–16 раз. Разумно оставить
   CPU1 для TFT и CPU2 для звука, а не дробить 68000/Z80.
5. **Первый практический шаг — Cyclone 68000** для систем с одним 68000
   (CPS-1, Toaplan, Data East, Cave, CPS-2), как экспериментальная ветка с
   откатом на Musashi. Ожидание 1.3–2× по CPU, но не «бесплатно»: byte-swap,
   self-modifying code, несколько 68K в Sega System 16/18 и 68020 в Psikyo.
6. **Отсутствующие наборы** (`dkong`, `invaders`, `ddonpach`, `batrider`,
   `grindstm`, `agallet`, `sailormn`, `2020bb`, `tophuntr`, `mmatrix`) не имеют
   исходников драйверов в дереве — это не «забыли строку в driverlist», а
   отдельная задача вендоринга/порта.

## 1. Железо и бюджет кадра

| Параметр | Значение |
|---|---|
| SoC | Allwinner H3, 4×Cortex-A7 (ARMv7-A, NEON), ~1.0–1.2 ГГц |
| Память | 512 МБ DDR3 (2×K4B2G16), DRAM 0x40000000..0x60000000 |
| Видео | DE2 + GSU (аппаратный скейлер), HDMI 1024×600 |
| Звук | I2S0, 48 кГц стерео; кольцо в SRAM A1, долив на CPU2 |
| Вторичные ядра | CPU1 — TFT; DCS (MK/MK2) — локально на core0 (r1735); CPU2 — долив I2S |
| Бюджет 60 Гц | 16 667 мкс/кадр |
| Бюджет 54.7 Гц (MK) | 18 282 мкс/кадр |
| Бюджет 59.6 Гц (S16) | 16 779 мкс/кадр |

Текущее распределение: **core0** — весь `BurnDrvFrame()` (CPU, видео, звук),
рендер и ввод; **CPU1** — TFT-меню, видеоконвейер выключен
(`k_vidw_enable=0`); **CPU2** — только долив кольца в TX FIFO.

## 2. Что уже реализовано

### 2.1 Один host на все аркады

- `run_cps(root, rom, size, rom_name)` — общий путь для CPS-1/2, Neo Geo,
  Sega System 16, Toaplan, Cave, Namco, Taito, Data East, Psikyo, Midway, Irem,
  сводного FB Neo. `emu_run_*` только подставляют корень `/roms/<система>`.
- ROM-сет читается из папки (приоритет) или zip: `BurnExtLoadRom`, `check_romset`,
  родительский zip, BIOS `neogeo.zip`/`neogeo/`.
- FBNeo-ядро: Musashi 68000, Z80, V30/V33, TMS34010, ADSP2100, M6809/HD6309,
  M6502/HuC6280, i8751/M6805 и звуковые чипы (YM2151, YM2610, YM3812, YMF278B,
  OKI6295, QSound, DCS и др.).
- Видео: драйвер рисует RGB565 в `g_frame` (384×320 с запасом), host
  конвертирует NEON в XRGB8888 и отдаёт DE2 GSU; `render` обычно 1–3 мс.
- Звук: драйвер синтезирует 48 кГц стерео на core0, блоком уходит в I2S-кольцо.
- Ввод: раскладки по семействам (CPS, Neo Geo, Toaplan, Sega, Namco, Taito,
  Data East, Psikyo, Midway, Irem), DIP-дефолты, Sega-геймпад/пад.
- Тайминг: `emu_period_us` подменяется частотой драйвера (`nBurnFPS`), затем
  возвращается; это убирает биение фаз (S16 59.6, MK 54.7).

### 2.2 Покрытие ROM-сетов (факт r1734)

Сверено с `driverlist.h` по именам наборов; «поддержано» = драйвер есть в списке.

| Система | Папка | Сетов | Поддержано | Чего нет |
|---|---|---:|---:|---|
| CPS-1 | `roms/cps1` | 29 | 29 | — |
| CPS-2 | `roms/cps2` | 37 | 36 | `mmatrix` |
| Neo Geo MVS | `roms/neogeo` | 101 | 99 | `2020bb`, `tophuntr` |
| Sega System 16/18 | `roms/segasys` | 16 | 16 | — |
| Toaplan | `roms/toaplan` | 17 | 15 | `batrider`, `grindstm` |
| Cave 68K | `roms/cave` | 10 | 7 | `agallet`, `ddonpach`, `sailormn` |
| Psikyo PS3 | `roms/psikyo` | 3 | 3 | — |
| Data East | `roms/dataeast` | 4 | 4 | — |
| Irem M72 | `roms/irem` | 7 | 7 | — |
| Midway T-unit | `roms/midway` | 2 | 2 | — |
| Namco | `roms/namco` | 22 | 22 | — |
| Taito | `roms/taito` | 5 | 5 | — |
| Arcade (мелочь) | `roms/arcade` | 2 | 0 | `dkong`, `invaders` — нет системы и драйверов |
| FB Neo (сводный) | `roms/fbneo` | 333 | 25 | остальные — чужие платы; 25 — дубли перенесённых систем |

Важно: у отсутствующих наборов **нет исходников драйверов** в `cores/cps1/src`
(проверено по символам `BurnDrv*`), поэтому «добавить в driverlist» недостаточно —
нужен вендоринг файлов драйвера и, возможно, новых CPU/устройств.

`roms/fbneo`: 25 поддержанных — это в основном дубли `segasys`/`irem`/`toaplan`/
`cave`/`midway` (`altbeast`, `astorm`, `rtype`, `mk`, `mk2`, `mazinger` и т.д.).
Остальные 308 наборов в браузере видны, но запускаться не будут — стоит либо
фильтровать список по `driverlist`, либо убрать их с SD.

### 2.3 Память

| Зона | Размер | Комментарий |
|---|---:|---|
| Пул FBNeo `pool_mem` | 108 МиБ (0x55343900, 0x06C00000) | ROM-данные, декапы, рабочие буферы драйверов |
| `.noinit_fbn` целиком | ≈127 МиБ (0x54000000..0x5BF43900) | пул + кадровые буферы ядер, не обнуляется |
| `ZIP_BUF_BIG` | 88 МБ (0x44C00000) | zip крупных сетов |
| `ZIP_BUF1` | 64 МБ (0x50000000) | zip до 64 МБ |
| `ZIP_BUF2` | 48 МБ (0x5C000000) | родитель/BIOS |

Крупнейшие zip: Neo Geo `kof2003` 80.2 МБ, `svc` 79.4, `kof2002` 78.7 — влезают
в `ZIP_BUF_BIG` (88 МБ). CPS-2 максимум ~22.4 МБ. Риск — не zip, а
**распакованные ROM-данные в пуле 108 МиБ** для поздних Neo Geo; высокую воду
пула нужно измерить (лог/счётчик), иначе возможен отказ инициализации на
`mslug5`/`kof2003`.

### 2.4 Многоядерность сейчас

- **CPU1:** TFT-меню; видеоконвейер выключен: r1694 показал 10.6 мс без
  D-cache и 21.5 мс с D-cache+обслуживанием против **1.3 мс** синхронной
  NEON-конверсии на core0.
- **CPU2:** только долив SRAM-кольца в I2S TX FIFO; синтез звука — на core0.
- **DCS (Midway):** с r1735 снова локально на core0; многоядерный вынос
  (почта/воркер CPU1) удалён как тупиковый.

## 3. Факты быстродействия

### 3.1 Измерено на стенде (HANDOVER, разные r)

| Игра | Система | fps | `emu`, мс | Что видно |
|---|---|---:|---:|---|
| Mortal Kombat | Midway T-unit | 42–54.7 | 14.8–20.9 | TMS34010 + DCS; в бою ~42 |
| Mortal Kombat II | Midway T-unit | 25–37 | 30.7–37.9 | DCS 16.6–22.9 мс; вынос на CPU1 давал 36–37, удалён в r1735 |
| Robocop | Data East DEC0 | 52 → 56–57.4 | ~17.5 | `draw` 9.6–10.2 мс до r1705/06 |
| Bad Dudes | Data East DEC0 | 37.3 | 25.0 | `render` 0.8 мс — узкое место не вывод |
| Karnov | Data East | 60 | 8.1–8.6 | запас есть |
| Dbreed | Irem M72 | 45–53 | — | `cpu` 14.7–17.1 мс |
| R-Type | Irem M72 | 48–55 | — | `cpu` 15–16 мс |
| Dino | CPS-1 | 59.6 | 8.1–10.0 | Cyclone r1739; `render` 1.0 мс |
| SF2 | CPS-1 | 59.6 | 4.4–7.2 | Cyclone r1739; самый быстрый из трёх |
| Punisher | CPS-1 | 59.6 | 7.6–9.3 | Cyclone r1739; QSound |
| Goldnaxe | Sega System 16B | 45.4 → 41.3 | 19.0–22.3 | Cyclone r1740; CPU/видео/звук внутри `emu` не разделены |
| CPS-2 / Neo Geo | — | **замеров ещё нет** | — | следующая проверка после S16B |

### 3.2 Внешний ориентир: MAME 0.250, Raspberry Pi 2B (A7 0.9 ГГц)

Процент от нативной частоты (100 % = в темпе; запас на кадр — больше 100 %).

| Игра/плата | % на A7 | Вывод |
|---|---:|---|
| `sf2` (CPS-1) | 97 | ровно в темпе |
| `ffight`, `ghouls` (CPS-1) | 111–115 | 60 с малым запасом |
| `kof94`, `maglord`, `wjammers` (Neo Geo) | 61–62 | ~37 fps, нужен ~1.6× |
| `batsugun`, `bgaregga` (Toaplan) | 42–50 | тяжело |
| `outzone` (Toaplan) | 81 | на грани |
| `donpachi` / `ddonpach` (Cave 68K) | 98 / 102 | в темпе на A7 (MAME) |
| `mk` (Midway) | 48 | ~26–29 fps |
| CPS-2 (`sfa2`, `sfa3`, `ssf2t`) | A53: 124–130 | оценка A7: 70–80 % |
| Cave (`esprade`, `guwange`) | A53: 142–143 | оценка A7: 85–90 % |
| Psikyo SH-2 (`s1945ii/iii`) | A53: 17–19 | **не про PS3**: PS3 — 68EC020, эти цифры к ней не применять |

Оговорки: это MAME, а не FBNeo; FBNeo обычно быстрее на тех же платах, поэтому
цифры — нижняя граница. Точных публичных fps FBNeo на A7 по играм не найдено.

### 3.3 Cyclone 68000 — что известно

- Это **не dynarec**, а развёрнутый ARM-ассемблерный интерпретатор: обработчик на
  каждую комбинацию опкод/режим адресации, генерируется на этапе сборки.
- Поддерживает **только 68000** (без 68010/68020), требует **byte-swap** памяти,
  исполняет код напрямую из памяти; при self-modifying code нужен явный сброс
  I-cache/D-cache и `checkpc` при переключении банков.
- В libretro-FBNeo на ARM включается (`USE_CYCLONE=1`), но мейнтейнеры FBNeo
  считают его **deprecated**: ломает часть игр, savestate, системы с несколькими
  68K (Sega System 16/18/24), имеет краши на RPi 3B.
- Ожидаемый выигрыш по CPU — **1.3–2×** (точных публичных замеров нет; по опыту
  PicoDrive/mame2000). Для CPS-1/Toaplan/DEC/Cave это главный рычаг; для CPS-2
  риск ниже (один 68000, но QSound); для Sega16 — высокий (несколько 68K);
  для Psikyo PS3 — **неприменим** (68EC020).

### 3.4 Многоядерность — что известно

- MAME: запрос «multithread CPU cores» закрыт как **not planned** — смена
  планировщика затронет каждый драйвер, «compatibility definitely not».
- MAME `-mt` — только финальный блит («tiny speedup»); WinUAE 6 выносит
  чипсет Denise/Lisa, но не CPU.
- Cortex-A7: L1 когерентен внутри кластера через SCU, но при выключенном MMU/
  кэше у вторичного ядра обмен идёт через некэшируемую память/явные операции;
  цена `DCCMVAC`/инвалидации на A7 публично не измерена.
- Наш r1694: перенос конверсии кадра на CPU1 проиграл из-за PL310 по одной
  линии и объёма кадра. Вывод: **не дробить CPU между ядрами**, выносить только
  блокирующий I/O и, возможно, крупные звуковые блоки.

## 4. Таблица «система — ром — возможность — реализовано»

| Система | Ромы (сеты) | Примеры | Реализовано | Факт/оценка | 60 fps? | План |
|---|---|---|---:|---|---|---|
| **CPS-1** | 29/29 | `sf2`, `dino`, `punisher`, `ffight`, `ghouls` | да | замеров нет; MAME A7 97–115 % | **да** | baseline; Cyclone-пилот; добить рендер при <60 |
| **CPS-2** | 36/37 (`mmatrix` нет) | `sfa3`, `ssf2t`, `ddsom`, `avsp`, `progear` | да | оценка 70–80 % (QSound+спрайты) | **на грани** | QSound/спрайты, Cyclone, замер; `mmatrix` — вендоринг |
| **Neo Geo** | 99/101 (`2020bb`, `tophuntr` нет) | `mslug`, `kof98`, `garou`, `blazstar`, `pulstar` | да | MAME A7 61 %; FBNeo быстрее, оценка 70–90 % | **ранние/средние — да, поздние — риск** | спрайтовые полосы/зум, ADPCM, память, Cyclone (SMC-риск) |
| **Sega System 16/18** | 16/16 | `shinobi`, `goldnaxe`, `altbeast`, `fantzone`, `cotton` | да | замеров нет; лёгкое железо | **да** | baseline; проверить FD1094/MCU; Cyclone не спешить (мульти-68K) |
| **Toaplan** | 15/17 (`batrider`, `grindstm` нет) | `batsugun`, `bgaregga`, `truxton`, `outzone`, `dogyuun` | да | MAME A7: 42–102 % | **классика — да, Batsugun/BGaregga — нет** | Cyclone; рендер; вендоринг `batrider` |
| **Cave 68K** | 7/10 (`agallet`, `ddonpach`, `sailormn` нет) | `donpachi`, `esprade`, `guwange`, `feversos`, `mazinger` | да | A53 142 % → A7 ~50–60 % (оценка) | **на грани/ниже** | Cyclone + спрайты; `ddonpach` — вендоринг; самый тяжёлый |
| **Psikyo PS3** | 3/3 | `s1945`, `tengai`, `gunbird` | да | 68EC020 16 МГц; Cyclone неприменим | **да, после оптимизации** | Musashi-020, звук YM2610/YMF278, рендер; замер |
| **Data East DEC0** | 4/4 | `robocop`, `baddudes`, `hbarrel`, `karnov` | да | 56–57 / 37.3 / 60 fps | **да, кроме Baddudes** | добить DEC0-рендер/CPU h6280; профиль `draw` |
| **Irem M72** | 7/7 | `rtype`, `rtype2`, `imgfight`, `xmultipl`, `dbreed` | да | 45–55 fps | **на грани** | рендер/CPU V30; профиль; цель 60 |
| **Midway T-unit** | 2/2 | `mk`, `mk2` | да | 42–54.7 / 25–37 fps | **нет; цель 50+** | TMS34010, DCS speedhack; DCS локально, кадр 54.7; сложно |
| **Namco** | 22/22 | `pacman`, `galaga`, `galaxian`, `mappy`, `rthunder` | да | замеров нет; лёгкие | **да** | baseline; фильтр fbneo-мусора |
| **Taito** | 5/5 | `rastan`, `rbisland`, `volfied`, `bublbobl`, `arkanoid` | да | замеров нет; лёгкие | **да** | baseline |
| **Arcade** | 0/2 | `dkong`, `invaders` | **нет** | — | — | добавить систему+драйверы или убрать папку |

## 5. Разбор по системам

### 5.1 CPS-1 — эталонный кандидат

- Железо: 68000 10 МГц (DASH 12), Z80 3.58 МГц, YM2151+OKI, 384×224.
- Сейчас: 29/29 драйверов, единый host; звук и видео уже оптимизированы
  (NEON-конверсия 1.3 мс, DE2 GSU; `render` 1–3 мс).
- Ожидание: 60 Гц на всех 29 сетах; у MAME A7 97–115 %, FBNeo должен быть выше.
- План: снять baseline по 5 играм (`sf2`, `dino`, `punisher`, `ffight`, `ghouls`);
  если есть <60 — профиль `run/snd/draw`; **здесь же пилот Cyclone** (один 68000,
  нет SMC в ROM) с откатом на Musashi по флагу.

### 5.2 CPS-2 — QSound и спрайты

- Железо: 68000 11.8 МГц, Z80 8 МГц, QSound (QS1000A), 384×224, до ~900 спрайтов.
- Сейчас: 36 драйверов, `cps2_crpt` (дешифровка), QSound (`qs*.cpp`) в сборке.
- Оценка: 70–80 % от 60 Гц на A7 (MAME A53 124–130 % × коэффициент A7/A53 ~0.6);
  FBNeo может быть выше, но QSound — тяжёлый.
- План: профиль `snd` (QSound) и `draw`; целочисленный миксер/без float; проверить
  `cps2_crpt` на всех 36; Cyclone (один 68000, но проверить SMC/ключи);
  `mmatrix` — только после вендоринга драйвера.

### 5.3 Neo Geo — спрайтовый конвейер и память

- Железо: 68000 12 МГц, Z80 4 МГц, YM2610 (FM+ADPCM), 320×224, спрайтовые
  «полосы» с зумом; ROM до ~80 МБ (zip).
- Сейчас: 99 драйверов, BIOS `neogeo.zip`/папка, звук YM2610; крупные сеты
  (`kof2003` 80.2 МБ) влезают в `ZIP_BUF_BIG` 88 МБ.
- Оценка: MAME A7 61 %; FBNeo обычно быстрее → 70–90 % для ранних/средних;
  поздние (`mslug5`, `kof2003`, `svc`) — риск 45–55 %.
- План: профиль `run/draw/snd`; оптимизировать спрайтовые полосы/зум
  (`neo_sprite.cpp`) и ADPCM; измерить высокую воду пула 108 МиБ; Cyclone —
  только после проверки SMC (Neo Geo активно пишет в RAM); `2020bb`/`tophuntr` —
  вендоринг, не приоритет.

### 5.4 Sega System 16/18

- Железо: 68000 10 МГц, Z80 4–5 МГц, YM2151 (+uPD7759/RF5c68), 320×224.
- Сейчас: 16 драйверов, включая FD1094/i8751-защиту (декрипт/MCU).
- Факт r1740 (Cyclone): `goldnaxe` 45.4 → 41.3 fps, `emu` 19.0–22.3 мс,
  `render` 1.0 мс. Это хуже «лёгкой» системы; CPU/видео/звук внутри `emu`
  пока не разделены, подозреваемые — i8751/mcs51, FD1094-декрипт и спрайтовый draw.
- Сделано r1742: stage-таймеры в `System16BFrame` (CPU/звук/MCU/draw),
  `S16: perf` и поля `PROF`. Ждём лог `goldnaxe`: если доминирует draw —
  оптимизировать `System16BUpdate`; если mcs51 — `Simulate8751`/`mcs51Run`;
  если 68000 — сравнить Musashi/Cyclone и решать про per-system ядро.

### 5.5 Toaplan

- Железо: 68000 7–10 МГц, Z80/HD647180, YM3812, 4 tile-слоя, спрайты до 128×128.
- Сейчас: 15 драйверов; `batsugun`, `bgaregga` тяжелее классики; `donpachi`/
  `ddonpach` относятся к Cave.
- Факт: MAME A7 `batsugun` 42 %, `bgaregga` 50 %, `outzone` 81 %; `donpachi` 98 %.
- План: измерить `batsugun`/`bgaregga`/`truxton`/`outzone`; Cyclone (один 68000)
  — главный кандидат после CPS-1; рендер 4 слоёв; `batrider` — вендоринг
  (Raizing, отдельный драйвер), `grindstm` — позже.

### 5.6 Cave 68K

- Железо: 68000 16 МГц, YMZ280B/OKI, 240×384 (TATE), плотный danmaku.
- Сейчас: 7 драйверов (`donpachi`, `esprade`, `guwange`, `feversos`, `mazinger`,
  `metmqstr`, `gaia`); нет `ddonpach`, `agallet`, `sailormn`.
- Оценка: A53 142–143 % (MAME) → A7 ~50–60 %; 16 МГц 68K — самый тяжёлый 68K.
- План: Cyclone + оптимизация спрайтов/тайлов; TATE-вывод; начать с `donpachi`/
  `esprade`/`guwange`; `ddonpach` — вендоринг после успеха на трёх.

### 5.7 Psikyo PS3

- Железо: 68EC020 16 МГц (32-битные данные), Z80A 8 МГц + YM2610/YMF278,
  320×224, PIC16C57 MCU.
- Сейчас: 3 драйвера (`s1945`, `tengai`, `gunbird`), YM2610/YMF278 с мягким
  лимитом (r1651), MCU-таблицы.
- Cyclone **неприменим** (только 68000, а тут 68020). Резерв — Musashi-020,
  звук и рендер.
- План: замер `s1945`/`tengai`/`gunbird`; профиль `snd` (OPL4/YM2610) и `draw`;
  оптимизировать аудио; цель 60.

### 5.8 Data East DEC0

- Железо: 68000 10 МГц, M6502/HuC6280, YM2203/YM3812/OKI, 256×240.
- Факт: `robocop` 56–57.4 (после r1705/06), `baddudes` 37.3 (`emu` 25.0),
  `karnov` 60.
- План: профиль `baddudes` (`run`/`draw`/`snd`); DEC0-слои уже резались по окну,
  но `draw` у robocop был 10 мс — проверить остаток; цель 60 для всех 4.

### 5.9 Irem M72

- Железо: V30 8 МГц (шина 32/4), Z80 3.58 МГц, YM2151+DAC, 384×256, i8751.
- Факт: `rtype` 48–55, `dbreed` 45–53; `cpu` 14.7–17.1 мс.
- План: профиль `cpu` (V30/Z80) и `draw`; оптимизировать M72-рендер/сканлайн;
  цель 60; `hharry`/`loht` проверить отдельно.

### 5.10 Midway T-unit

- Железо: TMS34010 6.25 МГц, DCS ADSP-2105 10 МГц (MK2) / M6809+YM2151+OKI (MK1),
  400×254 @54.7 Гц.
- Факт: MK1 42–54.7, MK2 25–37; DCS 16.6–22.9 мс, вынос DCS на CPU1 давал
  36–37 fps, но с таймаутами и откатами; удалён в r1735 (DCS снова на core0).
- План: (а) `dcs_speedhack` как в `mame2003_midway` (если совместимо с
  FBNeo-драйвером); (б) оптимизировать TMS34010 (пиксельные операции,
  `-O3` уже есть); (в) точный период 54.7 Гц.
  Цель — 50+ fps, не обязательно 60; это самая тяжёлая система.

### 5.11 Namco / Taito

- Namco: 22 сета (System 1/86/2, Pac-Man/Galaxian), 6809/6502/Z80, WSG/YM.
- Taito: 5 сетов (Rastan/Rainbow/Arkanoid/Bubble Bobble/Volfied), 68000/6809/Z80.
- Ожидание: 60 Гц; это лёгкие платы. План: baseline, проверить звук/ввод,
  ничего не трогать без факта просадки.

### 5.12 `roms/arcade` (dkong, invaders)

- В `systems.h` нет системы `arcade`; драйверов `dkong`/`invaders` нет в дереве.
- Решение: либо добавить отдельную систему и вендорить драйверы (Z80/6502,
  лёгкие — реально), либо убрать папку с SD и из меню. Приоритет низкий.

## 6. Сквозные улучшения

### 6.1 Cyclone 68000 — экспериментальная ветка

- Исходники вендорены: `h3_bare/cores/cps1/src/cpu/cyclone/` — генератор
  notaz/cyclone68000 (HEAD `fe89d4e`, 15 файлов, GPLv2/MAME) и готовый
  `Cyclone.S` (1.85 МБ) из libretro-FBNeo; Makefile собирает именно его.
- **Реализовано (r1739, проверено на стенде; r1740 — чистка логов):**
  `c68000_intf.cpp` (Sek-совместимый интерфейс из libretro-FBNeo) без shadow:
  FBNeo при `-DLSB_FIRST` хранит 68k-слова в native-endian, Cyclone читает их
  напрямую (`ldrh`), fetch-страницы мапятся на оригинальные буферы.
  `make ARC_CYCLONE=1` собирает Cyclone-вариант; по умолчанию — Musashi.
- r1738 с byte-swap shadow давал чёрный экран: reset-вектор читался из
  переставленной тени, PC попадал в данные. Shadow отменён в r1739.
- Проверено: Musashi 26 339 764 Б, Cyclone 26 777 100 Б (r1742), `linkcheck: OK`;
  в Cyclone-ELF есть `c1m68k_checkpc` и `CycloneRun`.
- Стенд: CPS-1 (`dino`/`sf2`/`punisher`) — 59.6 fps, `emu` 4.4–10.0 мс;
  S16B (`goldnaxe`) — 45 fps, причина выясняется (см. §5.4).
- Ограничения пилота: только 68000 (Psikyo 68EC020 — Musashi); код из RAM без
  `MAP_FETCH` уходит в trap-страницу.
- Порядок: CPS-1 (пилот, успешно) → Sega System 16B (следующая) → Data East →
  Toaplan → Cave → CPS-2. Откат на Musashi сборкой без флага.

### 6.2 Рендер

- Профиль `draw` по драйверам; цель — ≤3 мс.
- DEC0: остаток после r1705/06; CPS-2/Neo Geo: спрайтовые конвейеры; Cave:
  тайлы/спрайты; Toaplan: 4 слоя.
- Не трогать NEON-конверсию core0 (1.3 мс) и DE2 GSU; не возвращать видеоконвейер
  на CPU1 (r1694).

### 6.3 Звук

- Профиль `snd`; QSound (CPS-2), YM2610 (Neo Geo/Psikyo), DCS (Midway),
  YM3812 (Toaplan), OKI/ADPCM.
- Целочисленная арифметика, без float в горячем пути; мягкий клип уже есть.
- CPU2 — только для крупных независимых блоков (ADPCM/PSG), не для мелких
  вызовов; иначе барьеры/латентность съедят выигрыш.

### 6.4 Многоядерность

- Оставить CPU1: TFT; CPU2: I2S-долив; DCS — на core0.
- Не дробить 68000/Z80 между ядрами (MAME: not planned; Haze: «amazingly slow»).
- Можно вынести блокирующий I/O (SD/USB-энумерация) на CPU1/CPU2, чтобы core0
  не ждал; это уже частично сделано (`usb_hc_full_recover`).
- Видеоконвейер CPU1 не возвращать.

### 6.5 Память и загрузка ROM

- Добавить лог высокой воды `pool_mem` после `BurnDrvInit` (использовано/свободно)
  и проверить поздние Neo Geo (`mslug5`, `kof2003`, `svc`).
- После загрузки zip освобождать/переиспользовать `ZIP_BUF_BIG` (если драйвер
  уже скопировал данные) — проверить по коду FBNeo; иначе 80 МБ висят.
- Крупные сеты — папкой, не zip, если zip >88 МБ (сейчас максимум 80.2 МБ —
  влезает, но запас мал).

### 6.6 Frame pacing и методика

- Для каждой игры — `CPS: perf`/`IRM: perf`: fps, `emu` (run/scan/dcs/snd/draw),
  `render`; цель — `emu + render < период драйвера`.
- Тест-матрица: по 5 игр на систему; фиксировать версию, игру, fps, узкое место.
- Паспортная частота: 60.00 CPS, 59.6 S16, 54.7 MK, 59.19 Neo Geo (проверять
  `nBurnFPS` в логе `CPS: throttle`).

## 7. Порядок работ (одна система за итерацию)

| Этап | Система | Что делаем | Критерий |
|---|---|---|---|
| 0 | Все | Baseline-замер по 1–2 играм на систему, таблица | Есть факты, а не догадки |
| 1 | CPS-1 | Baseline + Cyclone-пилот (флаг/откат) | 60 fps, +15 % или отказ |
| 2 | CPS-2 | QSound/спрайты, замер, при успехе Cyclone | 55–60 fps на `sfa3`/`ssf2t` |
| 3 | Neo Geo | Спрайты/ADPCM, память, Cyclone (риск) | 60 fps ранние, 50+ поздние |
| 4 | Data East | `baddudes` профиль/рендер | 60 fps все 4 |
| 5 | Irem M72 | `cpu`/рендер | 60 fps `rtype`/`dbreed` |
| 6 | Sega S16 | Baseline/проверка MCU | 60 fps |
| 7 | Toaplan | Cyclone, 4 слоя | 60 fps `batsugun`/`bgaregga` |
| 8 | Cave 68K | Cyclone + спрайты | 60 fps `donpachi`/`esprade`/`guwange` |
| 9 | Psikyo | Musashi-020, звук | 60 fps 3 сета |
| 10 | Midway | TMS34010, DCS speedhack (DCS локально) | 50+ fps MK/MK2 |
| 11 | Namco/Taito | Baseline | 60 fps |
| 12 | Arcade | `dkong`/`invaders` — добавить или убрать | Решение владельца |

## 8. Риски и открытые вопросы

1. **Cyclone:** byte-swap, SMC, несколько 68K, 68020; FBNeo считает deprecated.
   Без пилота на CPS-1 и отката не внедрять.
2. **Neo Geo память:** 108 МиБ пула может не хватить поздним сетам; нужен замер.
3. **CPS-2 QSound:** точность/скорость; возможно, потребуется отдельный
   оптимизированный миксер.
4. **DCS локально:** после r1735 DSP снова на core0; стенд не прогонялся —
   проверить MK/MK2 (звук, fps) до дальнейших правок.
5. **Отсутствующие драйверы:** нужен вендоринг исходников, а не только
   `driverlist`; объём работ по каждому — отдельная оценка.
6. **Точность:** ускорения не должны ломать совместимость; DIP/ROM-сеты
   проверять на реальных играх.
7. **`roms/fbneo`:** 308 неподдержанных zip засоряют браузер; фильтровать или
   убрать.
8. **Стенд:** все выводы по fps — только после прогона на Orange Pi Lite;
   текущие оценки не заменяют замер.

## 9. Источники

Внутренние:
- `docs/AUDIT_2026-11-27_LAYERS.md` — послойный аудит (память, USB, диски, звук).
- `docs/HANDOVER.md` — фактические замеры r1660–r1733.
- `docs/MEMORY.md` — карта памяти и пул FBNeo.
- `docs/FBNEO-ROMS.md` — каталог ROM-сетов.
- `h3_bare/cores/cps1_host.cpp`, `driverlist.h`, `Makefile` — реализация.

Внешние (ключевые):
- MAME 0.250 benchmark, Raspberry Pi 2B (A7 0.9 ГГц), StickFreaks CSV:
  https://docs.google.com/spreadsheets/d/1IE5BTmFioLn8l6UViwXrn-nRj0nYYotlLhB9mE0fySc/
- MAME 0.250 benchmark, Pi 3B+/4B (A53), All-Killer CSV:
  https://docs.google.com/spreadsheets/d/1xFTeFZcMbPferfEz9_sIlwiooZOvsVwzKJhsiF-W-88/
- Методика бенчмарка MAME (danmons):
  https://github.com/danmons/mame_raspberrypi_cross_compile/blob/main/benchmark/README.md
- FBNeo libretro: Makefile rpi2 (`USE_CYCLONE`), README/FAQ:
  https://github.com/libretro/FBNeo
- FBNeo issue #13964 (MAME, «multithread CPU cores» — not planned):
  https://github.com/mamedev/mame/issues/13964
- MAME `-mt` (Haze):
  https://mamedev.emulab.it/haze/2013/10/05/mame-and-mt-multithread-public-service-announcement
- Cyclone 68000 (notaz): https://github.com/notaz/cyclone68000
- FBNeo issue #2339 (Cyclone crashes RPi 3B):
  https://github.com/finalburnneo/FBNeo/issues/2339
- mame2003_midway (DCS speedhack):
  https://github.com/libretro/mame2003_midway
- PiFBA (Cyclone + DrZ80): https://github.com/RetroPie/pifba
- GnGeo-Pi: https://github.com/ymartel06/GnGeo-Pi
- Cortex-A7 MPCore TRM (SCU/L2): Arm, Cortex-A7 MPCore Technical Reference Manual.
- Libretro Docs: FBNeo, MAME 2003-Plus, Arcade Getting Started.
