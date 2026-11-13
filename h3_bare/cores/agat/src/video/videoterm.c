#include "videoint.h"
#include "common.h"
#include <stdio.h>

#define VIDEOTERM_ROM_SIZE 1024
#define VIDEOTERM_ROM_OFFSET 0x300
#define VIDEOTERM_RAM_SIZE 2048
#define VIDEOTERM_PAGE_SIZE 512
#define VIDEOTERM_NUM_REGS 32
#define VIDEOTERM_FONT_FILE_SIZE 2048
#define VIDEOTERM_FONT_SIZE 4096


struct VIDEOTERM_STATE
{
	struct SLOT_RUN_STATE*st;
	struct VIDEO_STATE *vs;

	byte regs[VIDEOTERM_NUM_REGS];
	byte cur_reg;

	word rom_size;
	byte rom[VIDEOTERM_ROM_SIZE];
	byte ram[VIDEOTERM_RAM_SIZE];
	word ram_offset;
	byte font[VIDEOTERM_FONT_SIZE];
};

static void vterm_update_reg(int rno, struct VIDEOTERM_STATE*vts);


static int videoterm_term(struct SLOT_RUN_STATE*st)
{
	free(st->data);
	return 0;
}

static int videoterm_save(struct SLOT_RUN_STATE*st, OSTREAM*out)
{
	struct VIDEOTERM_STATE*vts = st->data;
	struct VIDEO_STATE*vs = vts->vs;

	WRITE_ARRAY(out, vts->regs);
	WRITE_FIELD(out, vts->cur_reg);
	WRITE_ARRAY(out, vts->ram);
	WRITE_FIELD(out, vts->ram_offset);
	WRITE_FIELD(out, vs->ainf.videoterm);

	return 0;
}

static int videoterm_load(struct SLOT_RUN_STATE*st, ISTREAM*in)
{
	struct VIDEOTERM_STATE*vts = st->data;
	struct VIDEO_STATE*vs = vts->vs;
	int i;

	READ_ARRAY(in, vts->regs);
	READ_FIELD(in, vts->cur_reg);
	READ_ARRAY(in, vts->ram);
	READ_FIELD(in, vts->ram_offset);
	READ_FIELD(in, vs->ainf.videoterm);

	vs->vinf.ram = vts->ram;
	vs->vinf.ram_size = VIDEOTERM_RAM_SIZE;
	for (i = 0; i < VIDEOTERM_NUM_REGS; ++i) {
		vterm_update_reg(i, vts);
	}
	return 0;
}

static int videoterm_command(struct SLOT_RUN_STATE*st, int cmd, int data, long param)
{
	struct VIDEOTERM_STATE*vts = st->data;
	struct VIDEO_STATE*vs = st->sr->slots[CONF_CHARSET].data;
	switch (cmd) {
	case SYS_COMMAND_RESET:
		return 0;
	case SYS_COMMAND_HRESET:
		memset(vts->ram, 0, sizeof(vts->ram));
		memset(vts->regs, 0, sizeof(vts->regs));
		vts->ram_offset = 0;
		vs->vinf.cur_ofs = 0;
		vs->vinf.ram_ofs = 0;
		return 0;
	case SYS_COMMAND_FLASH:
		return 0;
	case SYS_COMMAND_REPAINT:
		return 0;
	case SYS_COMMAND_TOGGLE_MONO:
		return 0;
	case SYS_COMMAND_INIT_DONE:
		vts->vs = vs;
		vs->vinf.ram = vts->ram;
		vs->vinf.ram_size = VIDEOTERM_RAM_SIZE;
		vs->vinf.font = vts->font;
		vs->vinf.char_size[0] = 8;
		vs->vinf.char_size[1] = 9;
		vs->vinf.scr_size[0] = 80;
		vs->vinf.scr_size[1] = 24;
		vs->vinf.char_scl[0] = 1;
#ifdef DOUBLE_Y
		vs->vinf.char_scl[1] = 2;
#else
		vs->vinf.char_scl[1] = 1;
#endif
		return 0;
	}
	return 0;
}

static void videoterm_xrom_w(word adr, byte data, struct VIDEOTERM_STATE*vts); // C800-CFFF
static byte videoterm_xrom_r(word adr, struct VIDEOTERM_STATE*vts); // C800-CFFF

static void videoterm_rom_select(struct VIDEOTERM_STATE*vts)
{
//	printf("videoterm_rom_select %i\n", (0xC800 >> BASEMEM_BLOCK_SHIFT));
	enable_slot_xio(vts->st, 1);
}

static void videoterm_rom_unselect(struct VIDEOTERM_STATE*vts)
{
//	printf("videoterm_rom_select %i\n", (0xC800 >> BASEMEM_BLOCK_SHIFT));
	enable_slot_xio(vts->st, 0);
}

static void videoterm_xrom_w(word adr, byte data, struct VIDEOTERM_STATE*vts) // C800-CFFF
{
	adr &= 0x7FF;
	if (adr == 0x7FF) videoterm_rom_unselect(vts);
	if (adr >= vts->rom_size) {
		adr -= vts->rom_size;
		if (adr < VIDEOTERM_PAGE_SIZE) {
			vts->ram[vts->ram_offset + adr] = data;
			vid_invalidate_addr(vts->st->sr, 0x10000 + adr);
//			printf("videoterm_mem_w: %03X: %02X\n", vts->ram_offset + adr, data);
		}
	}
}

static byte videoterm_xrom_r(word adr, struct VIDEOTERM_STATE*vts) // C800-CFFF
{
	adr &= 0x7FF;
	if (adr == 0x7FF) videoterm_rom_unselect(vts);
	if (adr >= vts->rom_size) {
		adr -= vts->rom_size;
		if (adr < VIDEOTERM_PAGE_SIZE) {
//			printf("videoterm_mem_r: %03X: %02X\n", vts->ram_offset + adr, vts->ram[vts->ram_offset + adr]);
			return vts->ram[vts->ram_offset + adr];
		} else return empty_read(adr, NULL);
	} else return vts->rom[adr & (vts->rom_size - 1)];
}

static void videoterm_rom_w(word adr, byte data, struct VIDEOTERM_STATE*vts) // CX00-CXFF
{
	videoterm_rom_select(vts);
}

static byte videoterm_rom_r(word adr, struct VIDEOTERM_STATE*vts) // CX00-CXFF
{
	videoterm_rom_select(vts);
	return vts->rom[(adr & 0xFF) + VIDEOTERM_ROM_OFFSET];
}

static void vtermsel_io_touch(word adr, struct VIDEOTERM_STATE*vts) // C0X0-C0XF
{
	vts->ram_offset = ((adr >> 2) & 3) * VIDEOTERM_PAGE_SIZE;
//	printf("videoterm_ram_offset (%X) = %03X\n", adr, vts->ram_offset);
}

static void videoterm_update(struct VIDEOTERM_STATE*vts, struct VIDEO_STATE*vs)
{
	if (vs->ainf.videoterm && vs->ainf.text_mode) {
//		printf("set video size: %ix%i\n", vs->vinf.scr_size[0]*vs->vinf.char_size[0], vs->vinf.scr_size[1]*vs->vinf.char_size[1]);
		set_video_size(vs->sr, vs->vinf.scr_size[0]*vs->vinf.char_size[0]*vs->vinf.char_scl[0], vs->vinf.scr_size[1]*vs->vinf.char_size[1]*vs->vinf.char_scl[1]);
	}
}

static void vterm_update_reg(int rno, struct VIDEOTERM_STATE*vts)
{
	struct VIDEO_STATE*vs = vts->vs;
	byte data = vts->regs[rno];
	word lofs = vs->vinf.ram_ofs & (vs->vinf.ram_size - 1);
	word lcur = vs->vinf.cur_ofs & (vs->vinf.ram_size - 1);
	byte*cofs = (byte*)&vs->vinf.cur_ofs;
	byte*bofs = (byte*)&vs->vinf.ram_ofs;


	switch (rno) {
	case 1:
//		printf("scr_size[0] = %i\n", data);
		vs->vinf.scr_size[0] = data;
		videoterm_update(vts, vs);
		break;
	case 6:
//		printf("scr_size[1] = %i\n", data);
		vs->vinf.scr_size[1] = data;
		videoterm_update(vts, vs);
		break;
	case 9:
//		printf("char_height[1] = %i\n", data);
		vs->vinf.char_size[1] = (data & 0x0F) + 1;
		videoterm_update(vts, vs);
		break;
	case 10:
		vs->vinf.cur_size[0] = data;
		videoterm_update(vts, vs);
		break;
	case 11:
		vs->vinf.cur_size[1] = data;
		video_repaint_screen(vs);
		break;
	case 12:
		bofs[1] = data;
		video_repaint_screen(vs);
		break;
	case 13:
		bofs[0] = data;
		video_repaint_screen(vs);
		break;
	case 14:
		cofs[1] = data;
		vid_invalidate_addr(vts->st->sr, 0x10000 + lcur);
		vid_invalidate_addr(vts->st->sr, 0x10000 + vs->vinf.cur_ofs & (vs->vinf.ram_size - 1));
		break;
	case 15:
		cofs[0] = data;
		vid_invalidate_addr(vts->st->sr, 0x10000 + lcur);
		vid_invalidate_addr(vts->st->sr, 0x10000 + vs->vinf.cur_ofs & (vs->vinf.ram_size - 1));
		break;
	}
//	printf("videoterm: reg[%i] = %x; ofs = %x; cofs = %x; csize = %i,%i\n", 
//		rno, data, vs->vinf.ram_ofs, vs->vinf.cur_ofs, vs->vinf.cur_size[0], vs->vinf.cur_size[1]);
}

static void vterm_write_reg(int rno, byte data, struct VIDEOTERM_STATE*vts)
{
	if (vts->regs[rno] == data) return;
	vts->regs[rno] = data;
	vterm_update_reg(rno, vts);
}

static void vtermsel_io_w(word adr, byte data, struct VIDEOTERM_STATE*vts) // C0X0-C0XF
{
//	printf("videoterm_io_w: %01X: %02X\n", adr & 0x0F, data);
	if (adr & 1) {
		vterm_write_reg(vts->cur_reg, data, vts);
	} else {
		vts->cur_reg = data & (VIDEOTERM_NUM_REGS - 1);
	}
	vtermsel_io_touch(adr, vts);
}

static byte vtermsel_io_r(word adr, struct VIDEOTERM_STATE*vts) // C0X0-C0XF
{
//	printf("videoterm_io_r: %01X: %02X\n", adr & 0x0F, 0xFF);
	vtermsel_io_touch(adr, vts);
	return vts->regs[vts->cur_reg];
}


int  videoterm_init(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*st, struct SLOTCONFIG*cf)
{
	ISTREAM*rom;
	struct VIDEOTERM_STATE*vts;

	puts("in videoterm_init");

	vts = calloc(1, sizeof(*vts));
	if (!vts) return -1;

	vts->st = st;

	vts->rom_size = VIDEOTERM_ROM_SIZE;


	rom = isfopen(cf->cfgstr[CFG_STR_ROM]);
	if (!rom) {
		load_buf_res(cf->cfgint[CFG_INT_ROM_RES], vts->rom, vts->rom_size);
	} else {
		isread(rom, vts->rom, vts->rom_size);
		isclose(rom);
	}

	memset(vts->font, 0xFF, sizeof(vts->font));

	rom = isfopen(cf->cfgstr[CFG_STR_ROM2]);
	if (!rom) {
		puts("videoterm font load failed!");
	} else {
		isread(rom, vts->font, sizeof(vts->font));
		isclose(rom);
	}

	if (cf->cfgstr[CFG_STR_ROM3][0]) {
		rom = isfopen(cf->cfgstr[CFG_STR_ROM3]);
		if (!rom) {
			puts("videoterm xfont load failed!");
		} else {
			isread(rom, vts->font + VIDEOTERM_FONT_FILE_SIZE, VIDEOTERM_FONT_FILE_SIZE);
			isclose(rom);
		}
	}

	st->data = vts;
	st->free = videoterm_term;
	st->command = videoterm_command;
	st->load = videoterm_load;
	st->save = videoterm_save;

	fill_rw_proc(st->io_sel, 1, videoterm_rom_r, videoterm_rom_w, vts);
	fill_rw_proc(st->baseio_sel, 1, vtermsel_io_r, vtermsel_io_w, vts);
	fill_rw_proc(&st->xio_sel, 1, videoterm_xrom_r, videoterm_xrom_w, vts);

	return 0;
}
