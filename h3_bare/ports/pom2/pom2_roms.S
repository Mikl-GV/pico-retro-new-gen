/* r1402: ПЗУ Apple //e (16 КБ: внутреннее ПЗУ/IO $C000-$CFFF плюс
   $D000-$FFFF). Вшито в прошивку, как и ПЗУ ][+ (правило владельца: BIOS
   живёт в образе, с SD не подгружается). Источник — apple2e.rom эталона
   Agat Emulator (совпадает с Apple2e.rom AppleWin). */
	.section .rodata
	.balign 4
	.global pom2_rom_apple2e
pom2_rom_apple2e:
	.incbin "h3_bare/cores/agat/rom/apple2e.rom"
