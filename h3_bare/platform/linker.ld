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

  /* Конец образа = конец BSS. Куча/свободная DRAM начинается отсюда. */
  _hend = . ;
}