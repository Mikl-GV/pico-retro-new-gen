/* h3_bare/platform/linker.ld — для Allwinner H3, загрузка в 0x40000000.
 * Подхватывает .text* (включая .text.startup), .data, .bss.
 * Framebuffer кладём отдельно в конец DRAM (в коде). */
SECTIONS
{
  . = 0x40000000;
  _hend = . ;
  .text : {
    KEEP(*(.vectors))
    KEEP(*(.text))
    *(.text*)
    . = ALIGN(4);
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
  .ARM.extab   : { *(.ARM.extab* .gnu.linkonce.armextab.*) }
   PROVIDE_HIDDEN (__exidx_start = .);
  .ARM.exidx   : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) }
   PROVIDE_HIDDEN (__exidx_end = .);
  .data :  {
    *(.data*)
  }
  .bss : {
    _bstart1 = . ;
    *(.bss); *(COMMON);
    _bend1 = . ;
  }
  .uncached 0x41000000 : {
    _bstart2 = . ;
    *(UNCACHED);
    _bend2 = . ;
  }
}