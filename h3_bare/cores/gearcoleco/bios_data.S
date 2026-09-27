// Coleco OS-7 BIOS (8KB) — вшит в прошивку (coleco_bios.bin, CRC32 0x3AA93EF3).
.globl coleco_bios_data
.data
coleco_bios_data:
  .incbin "coleco_bios.bin"
