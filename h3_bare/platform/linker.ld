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

  /* r0.417 (O1): пул FBNeo 96 МБ (.noinit_fbn, burn_memory.cpp) — NOLOAD,
   * стартовая обнулялка BSS его не покрывает (экономия ~96 МБ записи при
   * загрузке; каждый блок обнуляется при выдаче в _BurnMalloc).
   * Секция стоит сразу после .bss и ДО кучи — адреса кучи/_hend/.coherent
   * остаются ровно прежними (пул занимает место бывшего хвоста BSS). */
  .noinit_fbn (NOLOAD) : {
    *(.noinit_fbn)
  }

  /* Куча (bump-пул Snes9x/Handy/binjgb) — резерв ПОСЛЕ BSS.
   * Размер кучи НЕ влияет на глобалы (.bss).
   * 24 МБ: SNES-ядро при init занимает ~11.1 МБ (RAM/SRAM/VRAM/BSRAM/ROM
   * 8.5МБ/TileCache/GFX), до арены меню 0x4F000000 запас >185 МБ. */
  . = ALIGN(32);
  _gb_heap_start = . ;
  . = . + (24 * 1024 * 1024);
  _gb_heap_end = . ;

  /* Старт свободной памяти. ВНИМАНИЕ (r725): _sbrk() НЕ растёт отсюда!
   * libc_min.c:190 стартует кучу с libh3_coherent_region + 1MB (0x4A500000),
   * чтобы не наехать на uncached-окно .coherent и L1-таблицу страниц.
   * _hend здесь — маркер конца образа/кучи, используется только как граница;
   * реальный старт кучи newlib — см. SBRK_START в libc_min.c. */
  _hend = . ;

  /* Когерентная область для DMA (объявлена extern в h3.h как
   * libh3_coherent_region[1MB]). NOLOAD — не занимает место в образе,
   * только резервирует адреса; стартует сразу за кучей (_hend),
   * выровнена по 1 МБ как требует когерентный DMA-буфер.
   * Символ libh3_coherent_region = НАЧАЛО области (наш extern-символ).
   * r0.417 (M13 аудита): раньше брался `. - 1 МБ`, т.е. адрес ПОСЛЕ
   * .coherent-объектов (0x4AC00720), а не начало — работало только из-за
   * округления адреса в mmu_mark_uncached; любая прямая арифметика
   * (буферы ОТ базы области) уезжала. Теперь = _coherent_start.
   * OHCI-структуры (ED/TD/HCCA, буферы) лежат в начале области и помечаются
   * uncached через MMU — см. mmu_mark_uncached. _coherent_start/_coherent_end
   * — для zero-init в startup.S (NOLOAD секцию BSS-цикл не покрывает). */
  . = ALIGN(0x100000);
  .libh3_coherent (NOLOAD) : {
    _coherent_start = .;
    *(.coherent)
    _coherent_end = .;
    /* r776: DMA-путь звука упразднён (r773) — секция .dma_buf (р769-р772)
     * удалена вместе с драйвером h3_dma.c. У uncached-окна остаётся 1 МБ
     * физически (0x4A400000..0x4A500000, mmu_mark_uncached по 0x4A400000),
     * L1-таблица страниц живёт в 0x4A40C000..0x4A410000 (startup.S r722). */
  }
  libh3_coherent_region = _coherent_start;

  /* r725: ASSERT — .coherent-объекты обязаны поместиться ДО L1-таблицы
   * страниц, которую startup.S копирует в 0x4A40C000..0x4A410000 (r722).
   * Если _coherent_end перевалит за 0x4A40C000, startup затрёт таблицу —
   * MMU-деградация/зависания. Новых .coherent-объектов в хвост
   * 0x4A40C000+ не класть (там L1-таблица). */
  ASSERT(_coherent_end <= 0x4A40C000, "ERROR: .coherent objects overflow into L1 page table region")

  /* Арена меню — фиксированный адрес, гарантированно вне BSS/кучи и до ROM_BUF. */
  _menu_arena = 0x4F000000;
}