#include "videoint.h"
#include "common.h"
#include <stdio.h>

static int load_font_res(int no, void*buf, int sz)
{
	return load_buf_res(no, buf, sz);
}

static int video_term(struct SLOT_RUN_STATE*st)
{
	free(st->data);
	return 0;
}

static int video_save(struct SLOT_RUN_STATE*st, OSTREAM*out)
{
	struct VIDEO_STATE*vs = st->data;

	WRITE_FIELD(out, vs->video_mode);

	WRITE_ARRAY(out, vs->rb);
	WRITE_FIELD(out, vs->n_rb);
	WRITE_FIELD(out, vs->rbi);
	WRITE_FIELD(out, vs->rb_enabled);
	WRITE_FIELD(out, vs->rb_cur);

	WRITE_FIELD(out, vs->pal);

	WRITE_FIELD(out, vs->ainf.text_mode);
	WRITE_FIELD(out, vs->ainf.combined);
	WRITE_FIELD(out, vs->ainf.page);
	WRITE_FIELD(out, vs->ainf.hgr);

	if (st->sr->cursystype == SYSTEM_E) {
		WRITE_FIELD(out, vs->cur_font);
		WRITE_FIELD(out, vs->ainf.text80);
		WRITE_FIELD(out, vs->ainf.dhgr);
	}

	return 0;
}

static int video_load(struct SLOT_RUN_STATE*st, ISTREAM*in)
{
	struct VIDEO_STATE*vs = st->data;

	READ_FIELD(in, vs->video_mode);

	READ_ARRAY(in, vs->rb);
	READ_FIELD(in, vs->n_rb);
	READ_FIELD(in, vs->rbi);
	READ_FIELD(in, vs->rb_enabled);
	READ_FIELD(in, vs->rb_cur);

	READ_FIELD(in, vs->pal);

	READ_FIELD(in, vs->ainf.text_mode);
	READ_FIELD(in, vs->ainf.combined);
	READ_FIELD(in, vs->ainf.page);
	READ_FIELD(in, vs->ainf.hgr);

	if (st->sr->cursystype == SYSTEM_E) {
		READ_FIELD(in, vs->cur_font);
		READ_FIELD(in, vs->ainf.text80);
		READ_FIELD(in, vs->ainf.dhgr);
	}

	video_update_mode(vs);
	video_repaint_screen(vs);
	return 0;
}

static int video_command(struct SLOT_RUN_STATE*st, int cmd, int data, long param)
{
	struct VIDEO_STATE*vs = st->data;
	switch (cmd) {
	case SYS_COMMAND_RESET:
		disable_ints(vs->sr);
		return 0;
	case SYS_COMMAND_HRESET:
		disable_ints(vs->sr);
		video_set_pal(&vs->pal, 0);
		vs->ainf.videoterm = vs->ainf.text80 = vs->ainf.dhgr = 0;
//		videosel(0);
		return 0;
	case SYS_COMMAND_FLASH:
		video_flash_text(vs);
		return 0;
	case SYS_COMMAND_REPAINT:
		video_repaint_screen(vs);
		return 0;
	case SYS_COMMAND_TOGGLE_MONO:
		video_set_mono(vs, 1, 1);
		return 0;
	case SYS_COMMAND_CPUTIMER:
		if ((param|1) == (DEF_CPU_TIMER_ID((vs->sr->slots + CONF_CHARSET))|1)) {
			video_timer(vs, param & 1);
			return 1;
		}
		break;
		
	}
	return 0;
}

static void videosel_w(word adr, byte data, struct VIDEO_STATE*vs) // C700-C7FF
{
	videosel(vs, adr&0xFF);
}

static byte videosel_r(word adr, struct VIDEO_STATE*vs) // C700-C7FF
{
	return videosel(vs, adr&0xFF);
}

static void vsel_ap_w(word adr, byte data, struct VIDEO_STATE*vs) // C050-C05F
{
	vsel_ap(vs, adr);
}

static byte vsel_ap_r(word adr, struct VIDEO_STATE*vs) // C050-C05F
{
	vsel_ap(vs, adr);
	if (adr == 0xC058) return rand();
	return empty_read(adr, NULL);
}


void enable_ints_w(word adr,byte data, struct VIDEO_STATE*vs) // C040-C04F
{
//	printf("enable ints_w: %x\n",adr);
	enable_ints(vs->sr);
}

byte enable_ints_r(word adr, struct VIDEO_STATE*vs) // C040-C04F
{
//	printf("enable ints_r: %x\n",adr);
	enable_ints(vs->sr);
	return empty_read_addr(adr, vs);
}



void disable_ints_w(word adr,byte data, struct VIDEO_STATE*vs) // C050-C05F
{
//	printf("disable ints_w: %x\n",adr);
	disable_ints(vs->sr);
}

byte disable_ints_r(word adr, struct VIDEO_STATE*vs) // C050-C05F
{
//	printf("disable ints_r: %x\n",adr);
	disable_ints(vs->sr);
	return empty_read_addr(adr, vs);
}


void disable_ints9_w(word adr,byte data, struct VIDEO_STATE*vs) // C020-C02F
{
//	printf("disable ints_w: %x\n",adr);
	disable_ints(vs->sr);
	mem_proc_write(adr, data, vs->st->service_procs + 0);
}

byte disable_ints9_r(word adr, struct VIDEO_STATE*vs) // C020-C02F
{
//	printf("disable ints_r: %x\n",adr);
	disable_ints(vs->sr);
	return mem_proc_read(adr, vs->st->service_procs + 0);
}


/*
byte toggle_ints_r(word adr, struct VIDEO_STATE*vs) // C050-C05F
{
	printf("toggle ints_r: %x\n",adr);
	toggle_ints(vs->sr);
	return empty_read(adr, vs);
}

void toggle_ints_w(word adr,byte data, struct VIDEO_STATE*vs) // C050-C05F
{
	printf("toggle ints_w: %x\n",adr);
	toggle_ints(vs->sr);
}
*/

static void set_palette_w(word adr,byte data, struct VIDEO_STATE*vs) // C050-C05F, agat 9
{
//	printf("set palette: %x",adr);
	if (adr&8) video_set_palette(vs, adr&7);
	else vsel_ap_w(adr, data, vs);
}


static byte set_palette_r(word adr, struct VIDEO_STATE*vs) // C050-C05F, agat 9
{
//	printf("get palette: %x",adr);
	if (adr&8) video_set_palette(vs, adr&7);
	else vsel_ap_r(adr, vs);
	return (adr&8&&!(adr&4))?0:0xFF;//empty_read(adr,d);
}


int set_rb_count(struct VIDEO_STATE*vs, int n, int nint)
{
	struct RASTER_BLOCK*rb;
	int i;

	if (vs->n_rb == n) return 1;

	vs->n_rb = n;
	for (i = n, rb = vs->rb; i; --i, ++rb) {
		rb->vmode = -1;
		rb->vtype = -1;
		rb->prev_base[0] = -1;
		rb->prev_base[1] = -1;
	}
	if (nint) {
		int d = 1000000 / vs->video_freq;
		system_command(vs->sr, SYS_COMMAND_SET_CPUTIMER, d, DEF_CPU_TIMER_ID((vs->sr->slots + CONF_CHARSET)));
		system_command(vs->sr, SYS_COMMAND_SET_CPUTIMER, d / nint, DEF_CPU_TIMER_ID((vs->sr->slots + CONF_CHARSET)) | 1);
	} else { // kill timers
		system_command(vs->sr, SYS_COMMAND_SET_CPUTIMER, 0, DEF_CPU_TIMER_ID((vs->sr->slots + CONF_CHARSET)));
		system_command(vs->sr, SYS_COMMAND_SET_CPUTIMER, 0, DEF_CPU_TIMER_ID((vs->sr->slots + CONF_CHARSET)) | 1);
	}
	return 0;
}

int video_init_rb(struct VIDEO_STATE*vs)
{
	switch (vs->sr->cursystype) {
	case SYSTEM_7:
		vs->video_freq = 50;
		set_rb_count(vs, vs->rb_enabled?N_RB_7:1, N_RBINT_7);
		break;
	case SYSTEM_9:
		vs->video_freq = 50;
		switch (vs->video_mode) {
		case VIDEO_MODE_AGAT:
			set_rb_count(vs, vs->rb_enabled?N_RB_9:1, N_RBINT_9);
			return 0;
		}
	case SYSTEM_1:
	case SYSTEM_A:
	case SYSTEM_P:
		vs->video_freq = 60;
		set_rb_count(vs, 1, 0);
		break;
	case SYSTEM_AA:
		vs->video_freq = 60;
		set_rb_count(vs, vs->rb_enabled?16:1, 20);
		break;
	case SYSTEM_E:
		vs->video_freq = 60;
		set_rb_count(vs, 1, 4);
		break;
	}
	return 0;
}

const byte*video_get_font(struct SYS_RUN_STATE*sr)
{
	struct SLOT_RUN_STATE*st = sr->slots + CONF_CHARSET;
	struct VIDEO_STATE*vs = st->data;
	if (vs->cur_font < 0) return NULL;
	return vs->font[vs->cur_font][0];
}

int video_select_font(struct SYS_RUN_STATE*sr, int fnt)
{
	struct SLOT_RUN_STATE*st = sr->slots + CONF_CHARSET;
	struct VIDEO_STATE*vs = st->data;
	if (fnt >= 0) fnt %= vs->num_fonts; else fnt = -1;
	if (vs->cur_font == fnt) return vs->cur_font;
	vs->cur_font = fnt;
	//if (vs->is_text_mode) 
	video_repaint_screen(vs);
	return vs->cur_font;
}


int video_get_flash(struct SYS_RUN_STATE*sr)
{
	struct SLOT_RUN_STATE*st = sr->slots + CONF_CHARSET;
	struct VIDEO_STATE*vs = st->data;
	return vs->pal.flash_mode;
}

int  video_init(struct SYS_RUN_STATE*sr)
{
	ISTREAM*s;
	struct VIDEO_STATE*vs;
	struct SLOT_RUN_STATE*st = sr->slots + CONF_CHARSET;
	const struct SLOTCONFIG*ch = st->sc;

	puts("video_init");

	vs = calloc(1, sizeof(*vs));
	if (!vs) return -1;


	vs->sr = sr;
	vs->st = st;
	vs->video_mode = VIDEO_MODE_INVALID;
	vs->ainf.hgr = 1;
	vs->pal.prev_pal = -1;
	vs->rb_enabled = 1;

	fill_rw_proc(st->service_procs, N_SERVICE_PROCS, empty_read_addr, empty_write, vs);

	vs->num_fonts = ch->cfgint[CFG_INT_ROM_SIZE] >> 11;

	printf("font file: %s, font size: %i bytes\n",
		ch->cfgstr[CFG_STR_ROM], ch->cfgint[CFG_INT_ROM_SIZE]);
	s=isfopen(ch->cfgstr[CFG_STR_ROM]);
	if (!s) {
		load_font_res(ch->cfgint[CFG_INT_ROM_RES], vs->font[0][0], ch->cfgint[CFG_INT_ROM_SIZE]);
	} else {
		if (isread(s,vs->font[0],ch->cfgint[CFG_INT_ROM_SIZE])!=ch->cfgint[CFG_INT_ROM_SIZE]) {
			WIN_ERROR(GetLastError(),TEXT("can't read font file"));
		}
		isclose(s);
	}

	st->data = vs;
	st->free = video_term;
	st->command = video_command;
	st->load = video_load;
	st->save = video_save;

	video_set_pal(&vs->pal, 0);
	video_init_rb(vs);
	switch (sr->cursystype) {
	case SYSTEM_7:
		video_set_mode(vs, VIDEO_MODE_AGAT);
		videosel(vs, 0);
		fill_read_proc(sr->io_sel + 7, 1, videosel_r, vs);
		fill_write_proc(sr->io_sel + 7, 1, videosel_w, vs);
		fill_rw_proc(sr->baseio_sel + 4, 1, enable_ints_r, enable_ints_w, vs);
		fill_rw_proc(sr->baseio_sel + 5, 1, disable_ints_r, disable_ints_w, vs);
		break;
	case SYSTEM_9:
		video_set_mode(vs, VIDEO_MODE_AGAT);
		videosel(vs, 0);
		fill_read_proc(sr->io_sel + 7, 1, videosel_r, vs);
		fill_write_proc(sr->io_sel + 7, 1, videosel_w, vs);
		fill_rw_proc(sr->baseio_sel + 2, 1, disable_ints9_r, disable_ints9_w, vs);
		fill_rw_proc(sr->baseio_sel + 4, 1, enable_ints_r, enable_ints_w, vs);
		fill_rw_proc(sr->baseio_sel + 5, 1, set_palette_r, set_palette_w, vs);
		goto l1;
		break;
	case SYSTEM_A:
	case SYSTEM_P:
	case SYSTEM_E:
		video_set_mode(vs, VIDEO_MODE_APPLE);
		update_video_ap(vs);
		fill_read_proc(sr->baseio_sel + 5, 1, vsel_ap_r, vs);
		fill_write_proc(sr->baseio_sel + 5, 1, vsel_ap_w, vs);
	l1:
		vs->ainf.page = 0;
		video_set_mono(vs, 0, sr->config->slots[CONF_MONITOR].dev_type == DEV_MONO);
		break;
	case SYSTEM_1:
		video_set_mode(vs, VIDEO_MODE_APPLE_1);
		set_video_active_range(vs, 0, 0, 1);
		set_video_type(vs, 12);
		break;
	case SYSTEM_AA:
		video_set_mode(vs, VIDEO_MODE_ACORN);
		videosel_aa(vs, 0);
		break;
	}
	if (sr->config->systype == SYSTEM_8A) {
		vs->cur_font = 1;
	}
	return 0;
}

int video_get_flags(struct SYS_RUN_STATE*sr, word addr)
{
	struct SLOT_RUN_STATE*st = sr->slots + CONF_CHARSET;
	struct VIDEO_STATE*vs = st->data;

	switch (addr) {
	case 0xC019:
//		printf("rbi = %i\n", vs->rbi);
		return (vs->rbi<0)?0x80:0x00;
	case 0xC01A:
		return vs->ainf.text_mode?0x80:0x00;
	case 0xC01B:
		return vs->ainf.combined?0x80:0x00;
	case 0xC01C:
		return vs->ainf.page?0x80:0x00;
	case 0xC01D:
		return vs->ainf.hgr?0x80:0x00;
	case 0xC01E:
		return vs->cur_font?0x80:0x00;
	case 0xC01F:
		return vs->ainf.text80?0x80:0x00;
	}
	return 0x00;
}

int video_select_80col(struct SYS_RUN_STATE*sr, int set)
{
	struct SLOT_RUN_STATE*st = sr->slots + CONF_CHARSET;
	struct VIDEO_STATE*vs = st->data;
	if (vs->ainf.text80 == set) return 1;
	vs->ainf.text80 = set;
	update_video_ap(vs);
	video_update_mode(vs);
	return 0;
}

void video_mem_access(struct SYS_RUN_STATE*sr)
{
	struct SLOT_RUN_STATE*st = sr->slots + CONF_CHARSET;
	struct VIDEO_STATE*vs = st->data;
	if (sr->sync_update) return;
	++ vs->mem_access;
	++ vs->tot_access;
	if (vs->mem_access > MEM_ACCESS_LIMIT || vs->tot_access > VIDEO_UPDATE_LIMIT) {
		vs->mem_access = 0;
		vs->tot_access = 0;
		if (vs->inv_area.left < vs->inv_area.right) {
			invalidate_video_window(vs->sr, &vs->inv_area);
			vs->inv_area.left = vs->inv_area.right = vs->inv_area.top = vs->inv_area.bottom = 0;
		}
	}
}
