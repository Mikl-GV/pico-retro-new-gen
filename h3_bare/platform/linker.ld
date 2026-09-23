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
    *(.bss); *(COMMON);
    . = ALIGN(4);
    _bend1 = . ;
  }

  /* Куча (bump-пул Snes9x/Handy/binjgb) — резерв ПОСЛЕ BSS.
   * Размер кучи НЕ влияет на глобалы (.bss).
   * 24 МБ: SNES-ядро при init занимает ~11.1 МБ (RAM/SRAM/VRAM/BSRAM/ROM
   * 8.5МБ/TileCache/GFX), до арены меню 0x4F000000 запас >185 МБ. */
  . = ALIGN(32);
  _gb_heap_start = . ;
  . = . + (24 * 1024 * 1024);
  _gb_heap_end = . ;

  /* Старт свободной памяти: _sbrk() (libc_min.c) растёт отсюда вверх.
   * Обязан быть ПОСЛЕ _gb_heap_end, иначе _sbrk наедет на пул кучи. */
  _hend = . ;

  /* Когерентная область для DMA (объявлена extern в h3.h как
   * libh3_coherent_region[1MB]). NOLOAD — не занимает место в образе,
   * только резервирует адреса; стартует сразу за кучей (_hend),
   * выровнена по 1 МБ как требует когерентный DMA-буфер.
   * Символ libh3_coherent_region = начало области (наш extern-символ).
   * OHCI-структуры (ED/TD/HCCA, буферы) кладём в самое начало области и
   * помечаем uncached через MMU — см. .coherent секцию и mmu_mark_uncached.
   * _coherent_start/_coherent_end — для zero-init в startup.S (NOLOAD
   * секцию BSS-цикл не покрывает). */
  . = ALIGN(0x100000);
  .libh3_coherent (NOLOAD) : {
    _coherent_start = .;
    *(.coherent)
    _coherent_end = .;
    . = ALIGN(4);
    . = . + (1 * 1024 * 1024);
  }
  libh3_coherent_region = . - (1 * 1024 * 1024);

  /* Арена меню — фиксированный адрес, гарантированно вне BSS/кучи и до ROM_BUF. */
  _menu_arena = 0x4F000000;
}