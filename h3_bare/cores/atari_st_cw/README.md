# atari_st_cw — ядро Castaway (Atari ST) для MultiTool

Замена старого порта Hatari/WinUAE (решение владельца, r1564: `docs/PITFALLS.md` §22).

## Источник

- Оригинальный Castaway (Martin Doering, Joachim Hoenig). CVS-репозиторий проекта:
  https://sourceforge.net/p/castaway/code/ ; снапшот
  https://sourceforge.net/code-snapshots/cvs/c/ca/castaway.zip (скачано 2026-11-22).
- Из RCS (`*,v`) извлечены head-ревизии (80 файлов), взяты: `cpu/`, `st/`,
  `config.h`, `doc-reference.txt`, `LICENSE.txt`.
- НЕ взято: `sdl/` (UI), `data/`, `disks/`, `roms/`, `tools/`, `Attic/`, `obj/`.
- Форк `github.com/agranlund/castaway` рассмотрен и **отклонён**: CPU-цикл и
  op-функции там на inline m68k-asm (a5/d5, `jmp_table`) — на ARM не портируется.

## Правки

- `config.h`: endianness — для ARM добавлен `WORDS_LITTLE_ENDIAN` (H3 — LE).
  В `st/mem.c` x86-asm (`rolw`/`bswap`) закрыт `#if defined(i386)` — на ARM не
  активен, работает C-ветка `ReadW/ReadL/WriteW/WriteL`.

## Host-слой

- `cw_host.c`: образы `.st` с SD через FAT (`cw_disk_*`, только чтение),
  вшитый EmuTOS (`tos_data.S` → `st/mem.c`), кадр COL4 320×200 / COL2 640×200 /
  MONO 640×400 → XRGB8888 в `.noinit_fbn` → DE2 GSU (960×600, центр по 1024).
  F12 — hot-swap диска A: через общий `disk_swap.c` (список `.st` из папки
  образа, без сброса машины). Выход: `h3_de2_ui_restore()`, `emu_audio_stop()`,
  `fb_clear()/fb_flush()`. Точка входа диспетчера — `emu_run_atari_st_cw`
  (первым `emu_prepare()`, иначе повторный вход ломается — PITFALLS §23).
- `sound/`: YM2149 (`AY8910.c` из EMULib + `cw_sound.c`): `Loop8910` для
  огибающей, генерация 44100 Гц в I2S по кадру 50 Гц.
- `cw_input.c`: USB HID → ST scancodes set-1 (`IkbdKeyPress/Release`),
  модификаторы — отдельным байтом отчёта (`usb_kbd_get_mods`); тачпад/тач →
  `IkbdMouseMotion` (дельты + EMA, при отсутствии относительного источника —
  абсолютная позиция), кнопки мыши: ЛКМ = 2, ПКМ = 1 (нумерация IKBD).
  Джойстик ST0: плата/Sega-пад (`pad_scan_combined`, 50 Гц) + клавиатура как
  в NES (стрелки, Z = A, X = B; fire — любая кнопка действия пада,
  START → Space). Состояние сессии сбрасывается в `cw_input_init()`,
  IKBD/мышь/джойстик — в `cw_ikbd_reset()` (st/ikbd.c).
- `cw_rename.sh` переименовывает глобалы ядра в `cw_*` (коллизии с msx и др.).
- Ограничения: только `.st`; `.msa` (нужна распаковка) и `.stx` (Pasti,
  трековый формат) не поддерживаются, как и пользовательский TOS (вшитый EmuTOS).

## Лицензия

GNU GPL v2 — см. `LICENSE.txt`.
