/*
	Agat Emulator version 1.19
	Copyright (c) NOP, nnop@newmail.ru
*/

#include "runmgr.h"
#include "runmgrint.h"
#include "localize.h"

#include "resource.h"

static byte keyb_clear_r(word adr, struct SYS_RUN_STATE*sr);	// C010-C01F
static void keyb_clear_w(word adr, byte d, struct SYS_RUN_STATE*sr);	// C010-C01F

int init_systems(struct SYS_RUN_STATE*sr)
{
	fill_read_proc(sr->base_mem, BASEMEM_NBLOCKS - 6, empty_read, NULL);
	fill_read_proc(sr->base_mem + BASEMEM_NBLOCKS - 6, 6, empty_read_addr, NULL);
	fill_write_proc(sr->base_mem, BASEMEM_NBLOCKS, empty_write, NULL);
	fill_read_proc(sr->baseio_sel, 16, empty_read, NULL);
	fill_write_proc(sr->baseio_sel, 16, empty_write, NULL);
	fill_read_proc(sr->io_sel, 8, empty_read, NULL);
	fill_write_proc(sr->io_sel, 8, empty_write, NULL);
	fill_read_proc(sr->io6_sel, 16, empty_read, NULL);
	fill_write_proc(sr->io6_sel, 16, empty_write, NULL);

	fill_read_proc(sr->base_mem + 24, 1, io_read, sr->io_sel);
	fill_write_proc(sr->base_mem + 24, 1, io_write, sr->io_sel);
	fill_read_proc(sr->io_sel, 1, baseio_read, sr->baseio_sel);
	fill_write_proc(sr->io_sel, 1, baseio_write, sr->baseio_sel);
	fill_read_proc(sr->baseio_sel + 6, 1, io6_read, sr->io6_sel);
	fill_write_proc(sr->baseio_sel + 6, 1, io6_write, sr->io6_sel);

	fill_read_proc(sr->baseio_sel + 0, 1, keyb_read, sr);
	fill_read_proc(sr->io6_sel + 3, 1, keyb_reg_read, sr);
	fill_read_proc(sr->baseio_sel + 1, 1, keyb_clear_r, sr);
	fill_write_proc(sr->baseio_sel + 1, 1, keyb_clear_w, sr);

	return 0;
}


// ===================================================================

void io6_write(word adr, byte data, struct MEM_PROC*io6_sel) // c060-c06f
{
	int ind = adr & 0x0F;
	mem_proc_write(adr, data, io6_sel + ind);
}

byte io6_read(word adr, struct MEM_PROC*io6_sel) // c060-c06f
{
	int ind = adr & 0x0F;
	return mem_proc_read(adr, io6_sel + ind);
}

void io_write(word adr, byte data, struct MEM_PROC*io_sel) // C000-C7FF
{
	int ind = (adr>>8) & 7;
	mem_proc_write(adr, data, io_sel + ind);
}

byte io_read(word adr, struct MEM_PROC*io_sel) // C000-C7FF
{
	int ind = (adr>>8) & 7;
	return mem_proc_read(adr, io_sel + ind);
}

void baseio_write(word adr, byte data, struct MEM_PROC*baseio_sel) // C000-C0FF
{
	int ind = (adr>>4) & 0x0F;
	mem_proc_write(adr, data, baseio_sel + ind);
}

byte baseio_read(word adr, struct MEM_PROC*baseio_sel)	// C000-C0FF
{
	int ind = (adr>>4) & 0x0F;
	return mem_proc_read(adr, baseio_sel + ind);
}

///////////////////////////////////////////////////////////////////////

byte keyb_clear_r(word adr, struct SYS_RUN_STATE*sr)	// C010-C01F
{
	keyb_clear(sr);
	return empty_read(adr, NULL);
}

void keyb_clear_w(word adr, byte d, struct SYS_RUN_STATE*sr)	// C010-C01F
{
	keyb_clear(sr);
}







//////////////////////////////////////////////////////////////

byte keyb_preview(word adr, struct SYS_RUN_STATE*sr)
{
	if (sr->input_data) {
		if (!sr->input_cntr) return 1;
		--sr->input_cntr;
		return 0;
	}
	return sr->cur_key>>7;
}

byte keyb_read(word adr, struct SYS_RUN_STATE*sr)	// C000-C00F
{
	if (sr->input_data && sr->input_size && ! sr->cur_key) {
		byte ch;
		int n;
		if (sr->input_cntr) { -- sr->input_cntr; goto ret; }
		sr->input_cntr = 10;
		n = isread(sr->input_data, &ch, 1);
//		printf("isread = %i, ch = %x: pos = %i, size = %i\n", n, ch, sr->input_pos, sr->input_size);
		if (n != 1) {
			MessageBeep(0);
			cancel_input_file(sr);
		} else {
			sr->input_pos += n;
			if (sr->input_recode) {
				ch |= 0x80;
				switch (ch) {
				case 0x8A: ch = 0x8d; break;
				}
			}
			if (ch) {
				sr->input_hbit = ch >> 7;
				sr->cur_key = ch | 0x80;
			}
			if (sr->input_pos == sr->input_size) {
				cancel_input_file(sr);
			} else {
				int pct = sr->input_pos * 100 / (sr->input_size - 1);
				static int lpct = -1;
				if (pct != lpct) {
					TCHAR bufs[2][128];
					lpct = pct;
					wsprintf(bufs[1], localize_str(LOC_VIDEO, 210, bufs[0], sizeof(bufs[0])), pct);
					system_command(sr, SYS_COMMAND_SET_STATUS_TEXT, -1, (long)bufs[1]);
				}	
			}
		}
	}
ret:
//	system_command(sr, SYS_COMMAND_DUMPCPUREGS, 0, 0);
//	if (sr->cur_key&0x80) printf("cur_key = %x\n", sr->cur_key);
	return sr->cur_key;
}

byte keyb_reg_read(word adr, struct SYS_RUN_STATE*sr)	// C063
{
	return sr->keyreg & sr->keymask;
}

void keyb_clear(struct SYS_RUN_STATE*sr)	// C010-C01F
{
	if (sr->cursystype == SYSTEM_9) sr->cur_key &= ~0x80;
	else sr->cur_key = 0;
}

