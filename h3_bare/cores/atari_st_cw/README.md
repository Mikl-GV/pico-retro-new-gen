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

- `cw_host.c`: диски `.st/.msa` с SD через FAT (`cw_disk_*`), вшитый EmuTOS
  (`tos_data.S` → `st/mem.c`), кадр COL4 320×200 / MONO → XRGB8888 в
  `.noinit_fbn` → DE2 GSU (960×600, центр по 1024), на выходе —
  `h3_de2_ui_restore()`. Точка входа диспетчера — `emu_run_atari_st_cw`.
- `sound/`: YM2149 (`AY8910.c` из EMULib + `cw_sound.c`): `Loop8910` для
  огибающей, генерация 44100 Гц в I2S по кадру 50 Гц.
- `cw_input.c`: USB HID → ST scancodes (`IkbdKeyPress/Release`); тачпад/тач →
  `IkbdMouseMotion` (дельты, EMA, при отсутствии относительного источника —
  абсолютная позиция); кнопки — `usb_pad_buttons` (бит0 ЛКМ, бит1 ПКМ).
- `cw_rename.sh` переименовывает глобалы ядра в `cw_*` (коллизии с msx и др.).
- Ограничения: только `.st`/`.msa`, диск A только для чтения; `.stx` и
  пользовательский TOS не поддерживаются (вшитый EmuTOS).

## Лицензия

GNU GPL v2 — см. `LICENSE.txt`.
