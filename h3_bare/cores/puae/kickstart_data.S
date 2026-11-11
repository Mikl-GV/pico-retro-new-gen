// kickstart_data.S — вшитый Kickstart 1.3 (rev 34.5, A500) для PUAE.
//
// Правило владельца: BIOS/ROM вшиты в образ прошивки, с SD не подгружаются.
// Файл bios/kick34005.A500 — 256 КБ (первая половина kick13.rom; вторая
// половина исходного файла побайтно идентична, поэтому не нужна).
// Символ puae_kick13_rom_end нужен host'у, чтобы получить размер буфера.
.globl _puae_kick13_rom
.globl puae_kick13_rom
.globl _puae_kick13_rom_end
.globl puae_kick13_rom_end

.data
_puae_kick13_rom:
puae_kick13_rom:
  .incbin "bios/kick34005.A500"
_puae_kick13_rom_end:
puae_kick13_rom_end:

#if defined(__linux__) && defined(__ELF__)
  .section .note.GNU-stack,"",%progbits
#endif
