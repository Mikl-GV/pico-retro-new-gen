// kickstart_data.S — вшитые образы Amiga для PUAE.
//
// Правило владельца: BIOS/ROM вшиты в образ прошивки, с SD не подгружаются.
//  - bios/kick34005.A500 — Kickstart 1.3, 256 КБ (первая половина kick13.rom;
//    вторая половина исходного файла побайтно идентична, поэтому не нужна);
//  - bios/wb13.adf — Workbench 1.3 rev 34.34 (A500-A2000, Disk 1), 901120 б:
//    системный диск для пункта меню «Amiga: Workbench 1.3».
// Символы *_end нужны host'у, чтобы получить размеры буферов.
.globl _puae_kick13_rom
.globl puae_kick13_rom
.globl _puae_kick13_rom_end
.globl puae_kick13_rom_end
.globl _puae_wb13_adf
.globl puae_wb13_adf
.globl _puae_wb13_adf_end
.globl puae_wb13_adf_end

.data
_puae_kick13_rom:
puae_kick13_rom:
  .incbin "bios/kick34005.A500"
_puae_kick13_rom_end:
puae_kick13_rom_end:

.balign 4
_puae_wb13_adf:
puae_wb13_adf:
  .incbin "bios/wb13.adf"
_puae_wb13_adf_end:
puae_wb13_adf_end:

#if defined(__linux__) && defined(__ELF__)
  .section .note.GNU-stack,"",%progbits
#endif
