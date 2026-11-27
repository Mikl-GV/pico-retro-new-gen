# ARC_PROFILE — программа снятия характеристик аркад (r1736, актуально r1741)

Цель: получать по каждой игре одинаково читаемые цифры `fps`, `emu`, `run`,
`scan`, `dcs`, `snd`, `draw`, `render`, `68k` — чтобы выбирать оптимизацию по
фактам, а не на глаз. Одна итерация — одна система.

## 1. Что выдаёт прошивка

Раз в 120 кадров (≈2 с) в UART печатаются:

- человекочитаемые `CPS: perf …` / `IRM: perf …` (нулевые поля не печатаются);
- машинная строка:

```
PROF,<sys>,<game>,<fps*10>,<emu_us>,<run_us>,<scan_us>,<dcs_us>,<snd_us>,<draw_us>,<render_us>,<cpu68_us>
```

Поля — **средние микросекунды на кадр**; `fps*10` — фактический fps × 10.
`sys` — корень `/roms/<система>`, `game` — имя набора. Для Irem `run` = CPU M72,
`scan`/`dcs` = 0; для Midway `run`/`scan`/`dcs` — TMS34010/сканлайн/DCS.

## 2. Как снять

1. Прошить `r1740` (или новее), запустить игру, дать прогреться 5–10 с
   (первый `PROF` отбрасывается парсером).
2. Играть/крутить демо 60–120 с (30–60 строк `PROF`).
3. Сохранить UART-лог (например, `arc.log`).
4. Разобрать:

```powershell
powershell -ExecutionPolicy Bypass -File tools/arc_profile.ps1 -Log arc.log
powershell -ExecutionPolicy Bypass -File tools/arc_profile.ps1 -Log arc.log -Csv docs/arc_profile.csv
```

`-SkipFirst N` — сколько первых строк игры отбросить (по умолчанию 1).

## 3. Как читать

| Поле | Смысл | Что делать |
|---|---|---|
| `FPS` | фактический fps | сравнить с паспортным (60/59.6/54.7) |
| `Emu_us` | `BurnDrvFrame` целиком | если ≈ период — упёрлись в эмуляцию |
| `Run_us` | основной CPU (68k/V30/TMS) | кандидат на Cyclone/оптимизацию ядра |
| `Scan_us` | сканлайн/видео драйвера | оптимизировать рендер слоёв |
| `Dcs_us` | DCS/ADSP (Midway) | speedhack/локальный DSP |
| `Snd_us` | синтез звука | QSound/YM/OKI, целочисленный микс |
| `Draw_us` | сборка кадра драйвером | тайлы/спрайты, `memset` карт |
| `Render_us` | конверсия+DE2 | обычно 1–3 мс; если больше — смотреть GSU |
| `Headroom_us` | период − (`Emu`+`Render`) | **цель > 0**, желательно ≥ 1000 мкс |
| `Load_pct` | загрузка кадра | > 100 % — просадка fps |
| `Bottleneck` | max из run/scan/dcs/snd/draw/render/emu | с чего начинать |

## 4. Матрица замеров (по 1–3 игры на систему)

| Система | Игры | Папка | Паспорт | Что смотреть |
|---|---|---|---|---|
| CPS-1 | `sf2`, `dino`, `punisher` | `/roms/cps1` | 60.0 | `run`, `snd`, `draw` |
| CPS-2 | `sfa3`, `ssf2t`, `ddsom` | `/roms/cps2` | 59.6 | `snd` (QSound), `draw` (спрайты) |
| Neo Geo | `mslug`, `kof98`, `mslug5` | `/roms/neogeo` | 59.2 | `run`, `draw` (полосы/зум), память |
| Sega System 16 | `shinobi`, `goldnaxe`, `cotton` | `/roms/segasys` | 59.6 | `run`, `draw` |
| Toaplan | `batsugun`, `bgaregga`, `truxton` | `/roms/toaplan` | 60.0 | `run`, `draw` |
| Cave 68K | `donpachi`, `esprade`, `guwange` | `/roms/cave` | 59.6 | `run`, `draw` |
| Psikyo PS3 | `s1945`, `tengai`, `gunbird` | `/roms/psikyo` | 60.0 | `snd` (YM2610/YMF278), `run` |
| Data East DEC0 | `robocop`, `baddudes`, `karnov` | `/roms/dataeast` | 60.0 | `draw`, `run` (h6280) |
| Irem M72 | `rtype`, `dbreed`, `imgfight` | `/roms/irem` | 60.0 | `run` (V30), `draw` |
| Midway T-unit | `mk`, `mk2` | `/roms/midway` | 54.7 | `run` (TMS34010), `dcs` |
| Namco | `pacman`, `galaga`, `mappy` | `/roms/namco` | 60.0 | `run` |
| Taito | `rastan`, `bublbobl`, `arkanoid` | `/roms/taito` | 60.0 | `run`, `draw` |

## 5. Критерии приёмки итерации

- `Headroom_us > 0` и `Load_pct < 100` на целевой игре.
- Для «лёгких» систем — `FPS` = паспорт (60/59.6), `Headroom ≥ 1000 мкс`.
- Для Midway — цель 50+ fps (54.7 в идеале), сначала `dcs`/`run`.
- Любая оптимизация принимается только если:
  1) `PROF` до/после на одной и той же игре и сцене;
  2) целевое поле (`run`/`draw`/`snd`/`dcs`) уменьшилось;
  3) `FPS`/`Headroom` не ухудшились на соседних системах (регресс-прогон 1–2 игры).

## 6. Куда складывать

- Сырые логи — вне репозитория (например, `C:\logs\arc\r<версия>-<игра>.log`).
- Сводка — `docs/arc_profile.csv` (если решим коммитить) или в HANDOVER.
- В HANDOVER — одна строка на итерацию: игра, `fps`, `emu`, узкое место, что
  изменено, что стало.

## 7. Ограничения

- `PROF` — средние за 120 кадров; короткие пики не видны.
- Первый отчёт после старта содержит прогрев — отбрасывается (`-SkipFirst`).
- Для игр с паспортной частотой ≠ 60 (`mk` 54.7, S16 59.6) `Load_pct`
  считается от фактического fps, а не от 60 — сравнивать `Headroom`.
- Если `render` внезапно вырос — проверить, не включился ли программный путь
  вместо DE2 GSU.

## 8. Замеры r1739–r1740

| Игра | Система | Ядро | fps | `emu`, мс | Вывод |
|---|---|---|---:|---:|---|
| Dino | CPS-1 | Cyclone | 59.6 | 8.1–10.0 | OK, `render` ~1.0 |
| SF2 | CPS-1 | Cyclone | 59.6 | 4.4–7.2 | самый быстрый |
| Punisher | CPS-1 | Cyclone | 59.6 | 7.6–9.3 | QSound |
| Goldnaxe | Sega S16B | Cyclone | 45.4→41.3 | 19.0–22.3 | следующая цель; нужны stage-таймеры |

Сырые логи — вне репозитория; сюда попадают только сводки.
