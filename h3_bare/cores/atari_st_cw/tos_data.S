// tos_data.S — вшитый EmuTOS (192 КиБ) для ядра Castaway (atari_st_cw).
//
// EmuTOS — свободная GPLv2-ОС (кода Atari в ней нет), официальные бинарники
// с emutos.sourceforge.io (1.4, etos192us.img). Правило владельца: BIOS/ROM
// вшиты в прошивку (.incbin), с SD не подгружаются.
.globl cw_tos_emutos
.globl cw_tos_emutos_end

.data
cw_tos_emutos:
  .incbin "bios/etos192us.img"
cw_tos_emutos_end:

#if defined(__linux__) && defined(__ELF__)
  .section .note.GNU-stack,"",%progbits
#endif
