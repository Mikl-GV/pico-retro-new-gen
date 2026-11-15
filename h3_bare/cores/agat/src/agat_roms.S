/*
	r1297: вшитые образы Агат-9.
	Ресурсы Windows (LoadLibrary/FindResource) в bare-metal недоступны,
	поэтому ROM-монитор и знакогенератор лежат в .rodata прошивки
	и отдаются ядру через load_buf_res() по номерам ресурсов:
	  12 — ROM\MONITOR9.ROM  (2 КБ, адрес C800-FFFF)
	   2 — FNTS\AGATHE9.FNT  (2 КБ, знакогенератор)
*/

	.section .rodata
	.balign 4

	.global agat_rom_monitor9
agat_rom_monitor9:
	.incbin "h3_bare/cores/agat/rom/monitor9.rom"

	.balign 4
	.global agat_rom_agathe9
agat_rom_agathe9:
	.incbin "h3_bare/cores/agat/rom/agathe9.fnt"

	.balign 4
	.global agat_rom_shugart9
agat_rom_shugart9:
	.incbin "h3_bare/cores/agat/rom/shugart9.rom"

	.balign 4
	.global agat_rom_teac
agat_rom_teac:
	.incbin "h3_bare/cores/agat/rom/teac.rom"

	/* r1360: Apple II+ (Apple2_Plus.rom из эталона AppleWin, совпадает
	   байт в байт с APPLE2.ROM Agat Emulator). */
	.balign 4
	.global agat_rom_apple2p
agat_rom_apple2p:
	.incbin "h3_bare/cores/agat/rom/apple2p.rom"

	/* Знакогенератор Apple II (APPLE2.FNT эталона Agat Emulator). */
	.balign 4
	.global agat_rom_apple2_fnt
agat_rom_apple2_fnt:
	.incbin "h3_bare/cores/agat/rom/apple2.fnt"

	/* Контроллер Disk II, 16 секторов (DISK2.rom эталона AppleWin,
	   совпадает с SHUGARTA.ROM Agat Emulator). */
	.balign 4
	.global agat_rom_shugarta
agat_rom_shugarta:
	.incbin "h3_bare/cores/agat/rom/shugarta.rom"
