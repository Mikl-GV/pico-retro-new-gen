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
   * Не трогаем (р744/r0.417): адреса не влияют на жёсткие зоны ниже. */
  .noinit_fbn (NOLOAD) : {
    *(.noinit_fbn)
  }

  /* r779: конец ОБРАЗА (не кучи). _hend здесь — только граница «образ+BSS»;
   * реальная куча эмуляторов и sbrk вынесены ЗА uncached-зону (см. ниже),
   * поэтому рост кода/BSS не сдвигает жёсткие адреса 0x4A400000 (uncached)
   * и 0x4A40C000 (L1-таблица). */
  _hend = . ;

  /* Когерентная область (uncached) — VMA ЖЁСТКО 0x4A400000 (main.c передаёт
   * именно её в mmu_mark_uncached; startup.S копирует L1-таблицу в
   * 0x4A40C000). Ни пул, ни куча на неё больше не влияют. */
  .libh3_coherent 0x4A400000 (NOLOAD) : {
    _coherent_start = .;
    *(.coherent)
    _coherent_end = .;
  }
  libh3_coherent_region = _coherent_start;
  ASSERT(_coherent_end <= 0x4A40C000, "ERROR: .coherent objects overflow into L1 page table region")

  /* r779: куча эмуляторов — ЗА uncached-зоной (VMA 0x4A700000), 24 МБ.
   * Раньше была плавающей после .bss — рост кода сдвигал её конец на
   * жёсткие адреса и линкер падал. Теперь куча жёстко на 0x4A700000,
   * рост кода/BSS её не касается. NOLOAD: 0 байт до первого malloc. */
  .gb_heap 0x4A700000 (NOLOAD) : {
    _gb_heap_start = .;
    . = . + (24 * 1024 * 1024);
    _gb_heap_end = .;
  }

  /* newlib sbrk — сразу над кучей, до _menu_arena. */
  /* libc_min.c: SBRK_START берёт libh3_coherent_region+1MB (0x4A500000) —
     пересмотреть позже; см. TODO r779. */

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
}