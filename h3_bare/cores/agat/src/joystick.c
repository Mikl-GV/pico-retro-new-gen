/*
	Агат-9: джойстик и педали (порт joystick/joystick.c эталона).

	Порты Apple II: $C070 — сброс таймера педалей (чтение и запись),
	$C064/$C065 — статус X/Y (бит 7 держится время, пропорциональное
	положению), $C061/$C062 — кнопки 0/1.

	В эталоне источник — Windows-джойстик (joyGetPos) или мышь. В прошивке
	источник один: pad_scan_combined() (Sega 6-btn OR 8-битная панель),
	значение кладёт host раз в кадр через joystick_set_pad(). Скан I2C
	в ядре не делаем: он дорогой и host всё равно опрашивает пад для
	клавиатурного канала.

	Клавиатура работает независимо: она идёт через $C000/$C010, эти порты
	её не трогают, поэтому «со всего» (клавиатура + пад + кнопки) можно
	управлять одновременно.
*/

#include "common.h"
#include "types.h"
#include "memory.h"
#include "streams.h"
#include "debug.h"
#include "sysconf.h"
#include "runmgr.h"
#include "runmgrint.h"

/* Полный ход педали в тактах 6502 (эталонное значение). */
#define MAX_TIME 2850

struct JOYDATA
{
	int reset_time;                    /* tsc в момент $C070 */
	uint16_t pad;                      /* маска pad_scan_combined */
	struct SYS_RUN_STATE *sr;
};

/* Маска пада на текущий кадр: UP=0x01 DOWN=0x02 LEFT=0x04 RIGHT=0x08
   A=0x10 B=0x20 START=0x80 (см. btn_pad.h). */
int joystick_apply_pad(struct SYS_RUN_STATE *sr, uint16_t pad)
{
	struct JOYDATA *d = (struct JOYDATA*)sr->slots[CONF_JOYSTICK].data;

	if (!d) return -1;
	d->pad = pad;
	return 0;
}

/* Положение педали по маске: 0 — край (влево/вверх), 255 — другой край,
   128 — центр. */
static int joy_pos(struct JOYDATA *d, int no)
{
	if (no == 0) {
		if (d->pad & 0x04) return 0;    /* LEFT */
		if (d->pad & 0x08) return 255;  /* RIGHT */
	} else {
		if (d->pad & 0x01) return 0;    /* UP */
		if (d->pad & 0x02) return 255;  /* DOWN */
	}
	return 128;
}

static int joy_status(struct JOYDATA *d, int no, unsigned dt)
{
	int pos = joy_pos(d, no);

	return (dt < (unsigned)pos * MAX_TIME / 255) ? 0xFF : 0x7F;
}

static int joy_button(struct JOYDATA *d, int no)
{
	if (no == 0) return (d->pad & 0x10) ? 0xFF : 0x7F;   /* A */
	return (d->pad & 0x20) ? 0xFF : 0x7F;                /* B */
}

static void joy_w(word adr, byte data, struct JOYDATA *d)   /* C070-C07F */
{
	(void)adr;
	(void)data;
	d->reset_time = cpu_get_tsc(d->sr);
}

static byte joy_r(word adr, struct JOYDATA *d)              /* C070-C07F */
{
	joy_w(adr, 0, d);
	return empty_read(adr, d);
}

static byte joyb_r(word adr, struct JOYDATA *d)             /* C061-C062 */
{
	return joy_button(d, (adr & 1) ^ 1);
}

static byte joyp_r(word adr, struct JOYDATA *d)             /* C064-C065 */
{
	unsigned clk = (unsigned)(cpu_get_tsc(d->sr) - d->reset_time);

	return joy_status(d, adr & 1, clk);
}

static int joystick_command(struct SLOT_RUN_STATE *st, int cmd, int data, long param)
{
	(void)st;
	(void)cmd;
	(void)data;
	(void)param;
	return 0;
}

static int joystick_term(struct SLOT_RUN_STATE *st)
{
	(void)st;
	return 0;
}

int joystick_init(struct SYS_RUN_STATE *sr, struct SLOT_RUN_STATE *st, struct SLOTCONFIG *cf)
{
	struct JOYDATA *data;

	(void)cf;
	data = calloc(1, sizeof(*data));
	if (!data) return -1;
	data->sr = sr;
	data->pad = 0;

	fill_read_proc(sr->baseio_sel + 7, 1, joy_r, data);
	fill_write_proc(sr->baseio_sel + 7, 1, joy_w, data);
	fill_read_proc(sr->io6_sel + 1, 1, joyb_r, data);
	fill_read_proc(sr->io6_sel + 2, 1, joyb_r, data);
	fill_read_proc(sr->io6_sel + 4, 1, joyp_r, data);
	fill_read_proc(sr->io6_sel + 5, 1, joyp_r, data);

	st->data = data;
	st->free = joystick_term;
	st->command = joystick_command;
	return 0;
}
