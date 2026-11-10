/* h3_bare/platform/linker.ld — для Allwinner H3, загрузка в 0x40000000.
 * Секции идут строго подряд: .text -> .ARM.extab/.exidx -> .data -> .bss,
 * между ними явные ALIGN(4). Жёстких магических адресов внутри образа нет.
 * _hend — конец образа (старт свободной памяти/кучи), определяется ПОСЛЕ BSS.
 * Framebuffer (0x5F900000), ROM-буфер (0x50000000), EMU_FB (0x5F800000)
 * и стек (0x60000000) живут вне образа — в конце DRAM, не пересекаются.
 */
SECTIONS
{
  . = 0x40000000;

  .text : {
    KEEP(*(.vectors))
    KEEP(*(.text))
    *(.text*)
    . = ALIGN(4);
    __preinit_array_start = .;
    KEEP (*(.preinit_array))
    __preinit_array_end = .;

    . = ALIGN(4);
    __init_array_start = .;
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array))
    __init_array_end = .;
  }

  . = ALIGN(4);
  .rodata : {
    *(.rodata*)
  }

  . = ALIGN(4);
  .ARM.extab   : { *(.ARM.extab* .gnu.linkonce.armextab.*) }
  PROVIDE_HIDDEN (__exidx_start = .);
  .ARM.exidx   : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) }
  PROVIDE_HIDDEN (__exidx_end = .);

  . = ALIGN(4);
  .data :  {
    *(.data*)
  }

  . = ALIGN(4);
  .bss : {
    _bstart1 = . ;
    *(.bss); *(.bss.*); *(COMMON);
    . = ALIGN(4);
    _bend1 = . ;
  }

  /* Пул FBNeo 96 МБ (.noinit_fbn) — NOLOAD, плавающий сразу после .bss.
   * r1191: закреплён на 0x54000000 (было «плавает после .bss»): после
   * добавления PUAE (Amiga) обычный .bss вырос на ~66 МБ, и плавающий пул
   * начал перекрывать когерентную зону 0x4A400000 (линкер: overlaps).
   * Зона 0x54000000..0x5C000000 свободна: выше .fs_bss (конец 0x54000000),
   * ниже ZIP_BUF2 аркад (0x5C000000). Пул NOLOAD — startup.S его не
   * обнуляет (r0.417), обнуление при выдаче делает BurnMalloc.
   * CPS1 и прочие системы одновременно не запускаются (единый владелец). */
  .noinit_fbn 0x54000000 (NOLOAD) : {
    *(.noinit_fbn)
  }
  ASSERT(SIZEOF(.noinit_fbn) <= 0x08000000, "ERROR: .noinit_fbn > 128 МБ (0x54000000..0x5C000000)")

  /* r779: конец ОБРАЗА (не кучи). _hend здесь — только граница «образ+BSS»;
   * реальная куча эмуляторов и sbrk вынесены ЗА uncached-зону (см. ниже),
   * поэтому рост кода/BSS не сдвигает жёсткие адреса 0x4A400000 (uncached)
   * и 0x4A40C000 (L1-таблица). */
  _hend = . ;

  /* Когерентная область — VMA ЖЁСТКО 0x4A400000 (DMA-структуры OHCI, звуковое
   * кольцо, флаги CPU2). Атрибуты НЕ меняются: mmu_mark_uncached с r1206 —
   * намеренный no-op (non-cacheable ломает OHCI), область остаётся write-back,
   * когерентность держат cache_clean/invalidate и SRAM A1. LPAE-таблицы —
   * 0x4A40C000 (L0) и 0x4A410000+ (L1), копирует startup.S. Ни пул, ни куча
   * на неё больше не влияют. */
  .libh3_coherent 0x4A400000 (NOLOAD) : {
    _coherent_start = .;
    *(.coherent)
    _coherent_end = .;
  }
  libh3_coherent_region = _coherent_start;
  ASSERT(_coherent_end <= 0x4A40C000, "ERROR: .coherent objects overflow into L1 page table region")

  /* r1009: кольцо звука удалено из linker.ld — данные и индексы живут в
   * SRAM A1 (r957: L=0x1000, R=0x3000, 4096 пар), секция .ring_buf была пустой
   * (NOLOAD, ноль объектов) и не использовалась. */

  /* r779: куча эмуляторов — ЗА uncached-зоной (VMA 0x4A700000), 24 МБ.
   * Раньше была плавающей после .bss — рост кода сдвигал её конец на
   * жёсткие адреса и линкер падал. Теперь куча жёстко на 0x4A700000,
   * рост кода/BSS её не касается. NOLOAD: 0 байт до первого malloc. */
  .gb_heap 0x4A700000 (NOLOAD) : {
    _gb_heap_start = .;
    . = . + (24 * 1024 * 1024);
    _gb_heap_end = .;
  }

  /* newlib sbrk — сразу над кучей: SBRK_START = _gb_heap_end = 0x4BF00000,
   * SBRK_LIMIT = 0x4EF00000 (резерв под _menu_arena 0x4F000000) — libc_min.c. */

  /* Арена меню — фиксированный адрес, гарантированно вне BSS/кучи и до ROM_BUF. */
  _menu_arena = 0x4F000000;

  /* r778: BSS Atari 800 вынесен в свободную зону 0x5F000000 — после zip-зоны
   * FBNeo (ZIP_BUF2 0x5C000000..0x5F000000), до EMU_FB 0x5F800000. Не
   * пересекается ни с ROM_BUF, ни с zip, ни с пулом, ни с EMU_FB.
   * Обнуляется startup.S. */
  .a8_bss 0x5F000000 (NOLOAD) : {
    _a8_bss_start = .;
    *(.a8_bss)
    . = ALIGN(4);
    _a8_bss_end = .;
  }
  /* r1156 (E11): верхняя граница — EMU_FB 0x5F800000 (рост BSS Atari 800
   * не должен «наехать» на кадровый буфер эмуляторов). */
  ASSERT(_a8_bss_end <= 0x5F800000, "ERROR: .a8_bss overflow (0x5F000000..0x5F800000 — до EMU_FB)")

  /* r1010: файловый слой прошивки (.fs_bss) — НЕ в общем BSS.
   * Слоты g_files (448 байт×16) и g_rom_* жили в .bss рядом с данными
   * ядра VICE; при инициализации/автозапуске C64 ядро перезаписывало
   * их (лог: «fopen ok … ptr=42ED2A30» → «fread EOF … used=мусор» →
   * «Cannot read image header»). Выносим в отдельную NOLOAD-секцию после
   * .vice_bss: VICE-ядро туда не пишет. Обнуляется startup.S (как .a8_bss). */
  .fs_bss 0x53E00000 (NOLOAD) : {
    _fs_bss_start = .;
    *(.fs_bss)
    . = ALIGN(4);
    _fs_bss_end = .;
  }
  ASSERT(_fs_bss_end <= 0x54000000, "ERROR: .fs_bss overflow (0x53E00000..0x54000000 резерв 2 МБ)")

  /* r974: BSS ядра VICE (.vice_bss, ~31 МБ) — в свободной зоне над ROM_BUF
   * (0x51800000..0x52000000 свободно ~0.5 МБ — мало), поэтому на 0x52000000:
   * между концом ROM_BUF/zip-буфера аркад (0x51800000/0x5C000000) и zip-зоной
   * FBNeo. Аркады и C64 параллельно не работают (владелец: единовременный
   * запуск), зона переиспользуется. NOT перекрывается ничем ниже.
   * Обнуляется startup.S. */
  .vice_bss 0x52000000 (NOLOAD) : {
    _vice_bss_start = .;
    *(.vice_bss)
    . = ALIGN(4);
    _vice_bss_end = .;
  }
  /* r1156 (E11): верхняя граница — начало .fs_bss (0x53E00000). Рост BSS VICE
   * не должен перекрывать слоты файлового слоя (симптом r1010: fopen ok →
   * fread мусор → «Cannot read image header»). */
  ASSERT(_vice_bss_end <= 0x53E00000, "ERROR: .vice_bss overflow (0x52000000..0x53E00000 — до .fs_bss)")
}