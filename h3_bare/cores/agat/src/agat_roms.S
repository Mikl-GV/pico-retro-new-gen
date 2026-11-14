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
