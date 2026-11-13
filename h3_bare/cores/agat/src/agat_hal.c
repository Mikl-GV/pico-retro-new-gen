/*
	r1297: HAL-слой ядра Agat Emulator для bare-metal H3.

	Замещает Win32/ресурсные зависимости ядра: файловые потоки, локализацию,
	логи, thread-safe очередь, реестр run-state, окно вывода и слоты.
	Здесь ровно то, что нужно линковке и запуску модели Агат-9: видеовыход,
	звук и разбор образов дисков добавляются отдельными шагами.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "common.h"
#include "types.h"
#include "sysconf.h"
#include "list.h"
#include "tsq.h"
#include "streams.h"
#include "localize.h"
#include "debug.h"
#include "runmgr.h"
#include "runmgrint.h"
#include "memory.h"
#include "videow.h"
#include "video/videoint.h"
#include "sound/soundint.h"

struct GLOBAL_CONFIG g_config = { EMUL_FLAGS_DEFAULT };

/*=== Файловые потоки: тонкая обёртка над stdio (newlib/fatfs) =============*/

struct _STREAM
{
	FILE*f;
	/* r1298: поток поверх буфера в памяти — так образ диска, выбранный
	   в ROM-браузере, попадает в FDD без файловой системы. */
	const unsigned char*mem;
	unsigned len;
	unsigned pos;
};

/* Образ диска, переданный host-слоем (emu_run_agat). */
static const unsigned char*ag_disk_image = NULL;
static unsigned ag_disk_size = 0;
static char ag_disk_name[64] = { 0 };

void agat_set_disk_image(const void*data, unsigned size, const char*name)
{
	ag_disk_image = (const unsigned char*)data;
	ag_disk_size = size;
	ag_disk_name[0] = 0;
	if (name) {
		unsigned i;

		for (i = 0; i + 1 < sizeof(ag_disk_name) && name[i]; i++) {
			ag_disk_name[i] = name[i];
		}
		ag_disk_name[i] = 0;
	}
}

static int ag_is_disk_name(const char_t*name)
{
	return ag_disk_image && name && ag_disk_name[0] && !strcmp(name, ag_disk_name);
}

static ISTREAM* ag_mem_stream(void)
{
	ISTREAM*s = (ISTREAM*)malloc(sizeof(*s));

	if (!s) return NULL;
	s->f = NULL;
	s->mem = ag_disk_image;
	s->len = ag_disk_size;
	s->pos = 0;
	return s;
}

ISTREAM* isfopen(const char_t*name)
{
	FILE*f;
	ISTREAM*s;

	if (!name) return NULL;
	if (ag_is_disk_name(name)) return ag_mem_stream();
	f = fopen(name, "rb");
	if (!f) return NULL;
	s = (ISTREAM*)malloc(sizeof(*s));
	if (!s) { fclose(f); return NULL; }
	s->f = f;
	s->mem = NULL;
	s->len = 0;
	s->pos = 0;
	return s;
}

/* Ресурсы Windows в прошивке отсутствуют: образы берутся файлами с SD. */
ISTREAM* isfopen_res(const char_t*rname)
{
	(void)rname;
	return NULL;
}

void isclose(ISTREAM*f)
{
	if (!f) return;
	if (f->f) fclose(f->f);
	free(f);
}

int isread(ISTREAM*s, void*p, int nb)
{
	unsigned n;

	if (!s || !p || nb <= 0) return -1;
	if (s->mem) {
		n = s->len - s->pos;
		if ((unsigned)nb < n) n = (unsigned)nb;
		memcpy(p, s->mem + s->pos, n);
		s->pos += n;
		return (int)n;
	}
	return (int)fread(p, 1, (size_t)nb, s->f);
}

int isseek(ISTREAM*s, long pos, int where)
{
	int w;

	if (!s) return -1;
	if (s->mem) {
		long base = (where == SSEEK_SET) ? 0 : (where == SSEEK_END) ? (long)s->len : (long)s->pos;
		long np = base + pos;

		if (np < 0 || (unsigned long)np > s->len) return -1;
		s->pos = (unsigned)np;
		return 0;
	}
	w = (where == SSEEK_SET) ? SEEK_SET : (where == SSEEK_END) ? SEEK_END : SEEK_CUR;
	return fseek(s->f, pos, w);
}

long istell(ISTREAM*s)
{
	if (!s) return -1;
	return s->mem ? (long)s->pos : ftell(s->f);
}

OSTREAM* osfopen(const char_t*name)
{
	FILE*f;
	OSTREAM*s;

	if (!name) return NULL;
	f = fopen(name, "wb");
	if (!f) return NULL;
	s = (OSTREAM*)malloc(sizeof(*s));
	if (!s) { fclose(f); return NULL; }
	s->f = f;
	s->mem = NULL;
	s->len = 0;
	s->pos = 0;
	return s;
}

int oswrite(OSTREAM*s, const void*p, int nb)
{
	if (!s || !p || nb <= 0) return -1;
	return (int)fwrite(p, 1, (size_t)nb, s->f);
}

void osclose(OSTREAM*f)
{
	if (!f) return;
	if (f->f) fclose(f->f);
	free(f);
}

int osseek(OSTREAM*s, int pos, int where)
{
	int w;

	if (!s) return -1;
	w = (where == SSEEK_SET) ? SEEK_SET : (where == SSEEK_END) ? SEEK_END : SEEK_CUR;
	return fseek(s->f, pos, w);
}

IOSTREAM* iosfopen(const char_t*name)
{
	FILE*f;
	IOSTREAM*s;

	if (!name) return NULL;
	/* Образ из ROM-браузера только на чтение: rw-открытие не даём,
	   open_fdd1 сам переключится на isfopen (ro). */
	if (ag_is_disk_name(name)) return NULL;
	f = fopen(name, "r+b");
	if (!f) f = fopen(name, "w+b");
	if (!f) return NULL;
	s = (IOSTREAM*)malloc(sizeof(*s));
	if (!s) { fclose(f); return NULL; }
	s->f = f;
	s->mem = NULL;
	s->len = 0;
	s->pos = 0;
	return s;
}

void iosclose(IOSTREAM*f)
{
	if (!f) return;
	if (f->f) fclose(f->f);
	free(f);
}

int iosread(IOSTREAM*s, void*p, int nb)
{
	unsigned n;

	if (!s || !p || nb <= 0) return -1;
	if (s->mem) {
		n = s->len - s->pos;
		if ((unsigned)nb < n) n = (unsigned)nb;
		memcpy(p, s->mem + s->pos, n);
		s->pos += n;
		return (int)n;
	}
	return (int)fread(p, 1, (size_t)nb, s->f);
}

int ioswrite(IOSTREAM*s, const void*p, int nb)
{
	if (!s || !p || nb <= 0) return -1;
	if (s->mem) return -1;
	return (int)fwrite(p, 1, (size_t)nb, s->f);
}

int iosseek(IOSTREAM*s, int pos, int where)
{
	int w;

	if (!s) return -1;
	if (s->mem) {
		long base = (where == SSEEK_SET) ? 0 : (where == SSEEK_END) ? (long)s->len : (long)s->pos;
		long np = base + pos;

		if (np < 0 || (unsigned long)np > s->len) return -1;
		s->pos = (unsigned)np;
		return 0;
	}
	w = (where == SSEEK_SET) ? SEEK_SET : (where == SSEEK_END) ? SEEK_END : SEEK_CUR;
	return fseek(s->f, pos, w);
}

/*=== Локализация: строки ресурсов Windows ядру недоступны ================*/

int localize_init(void) { return 0; }
int localize_set_lang(LPCTSTR lang) { (void)lang; return 0; }
int localize_set_lib(HMODULE lib) { (void)lib; return 0; }
void localize_term(void) { }

LPCTSTR localize_str(int modid, int strid, LPTSTR buf, int bufsize)
{
	(void)modid;
	(void)strid;
	if (buf && bufsize > 0) buf[0] = 0;
	return buf;
}

HMODULE localize_get_lib(void) { return NULL; }
HMODULE localize_get_def_lib(void) { return NULL; }

/*=== Логи ================================================================*/

void debug_init(void) { }
void debug_term(void) { }

void logprint(int l, const char_t*fmt, ...)
{
	va_list ap;

	(void)l;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

void errprint(const char_t*fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

int WIN_ERROR(int err, const char_t*fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	return err;
}

/*=== Thread-safe queue: CPU Агата однопоточный, мьютекс не нужен =========*/

int tsq_init(struct TSQ *tsq)
{
	if (!tsq) return -1;
	list_init(&tsq->queue);
	tsq->mutex = NULL;
	return 0;
}

void tsq_uninit(struct TSQ *tsq)
{
	(void)tsq;
}

struct LIST_NODE* tsq_take(struct TSQ *tsq)
{
	struct LIST_NODE*n;

	if (!tsq || list_empty(&tsq->queue)) return NULL;
	n = tsq->queue.next;
	list_remove(n);
	return n;
}

/*=== Реестр run-state ====================================================*/

/* Текущая система: в прошивке одновременно живёт одна модель. */
static struct SYS_RUN_STATE*ag_cur_state = NULL;

struct SYS_RUN_STATE* agat_get_state(void)
{
	return ag_cur_state;
}

int set_run_state_ptr(LPCTSTR name, struct SYS_RUN_STATE*st)
{
	if (!st) return -1;
	st->name = name;
	ag_cur_state = st;
	return 0;
}

unsigned or_run_state_flags(LPCTSTR name, unsigned flags)
{
	(void)name;
	return flags;
}

unsigned and_run_state_flags(LPCTSTR name, unsigned flags)
{
	(void)name;
	return flags;
}

/*=== Видеовыход: кадр рисуется ядром в свой буфер, окно Windows не нужно =*/

/* Ядро пишет 4bpp-индекс: 1 байт = 2 пикселя по 4 бита, палитра 16 цветов
   (индексы Apple II/Агат). Размер кадра DOUBLE_X/DOUBLE_Y: 40*16 x 25*16. */
#define AGAT_FB_W 640
#define AGAT_FB_H 400
#define AGAT_FB_PITCH (AGAT_FB_W / 2)

/* Палитра Apple II (16 цветов) в RGB565 — базовая для Агат-9. */
static const unsigned short agat_pal565[16] = {
	0x0000, /*  0 black      */
	0x7908, /*  1 magenta    */
	0x21AF, /*  2 dark blue  */
	0xF1DF, /*  3 purple     */
	0x0AC8, /*  4 dark green */
	0x8410, /*  5 grey       */
	0x1D5F, /*  6 medium blue*/
	0x5FFF, /*  7 light blue */
	0x5A00, /*  8 brown      */
	0xFB63, /*  9 orange     */
	0xA514, /* 10 light grey */
	0xF9FF, /* 11 pink       */
	0x0680, /* 12 green      */
	0xFFE0, /* 13 yellow     */
	0x7E9F, /* 14 aqua       */
	0xFFFF, /* 15 white      */
};

int init_video_window(struct SYS_RUN_STATE*sr)
{
	if (!sr) return -1;
	if (!sr->bmp_bits) {
		sr->bmp_bits = malloc((size_t)AGAT_FB_PITCH * AGAT_FB_H);
		if (!sr->bmp_bits) return -1;
		memset(sr->bmp_bits, 0, (size_t)AGAT_FB_PITCH * AGAT_FB_H);
	}
	sr->bmp_pitch = AGAT_FB_PITCH;
	sr->v_size.width = AGAT_FB_W;
	sr->v_size.height = AGAT_FB_H;
	return 0;
}

void set_video_size(struct SYS_RUN_STATE*sr, int w, int h)
{
	if (!sr) return;
	sr->v_size.width = (uint32_t)w;
	sr->v_size.height = (uint32_t)h;
}

int term_video_window(struct SYS_RUN_STATE*sr)
{
	if (!sr) return 0;
	free(sr->bmp_bits);
	sr->bmp_bits = NULL;
	return 0;
}

int invalidate_video_window(struct SYS_RUN_STATE*sr, RECT *r)
{
	struct VIDEO_STATE*vs;

	if (!sr || !r) return 0;
	vs = get_video_state(sr);
	if (vs) UnionRect(&vs->inv_area, &vs->inv_area, r);
	return 0;
}

/* Развернуть 4bpp-индекс кадра в RGB565-буфер заданной ширины/высоты.
   Вызывается host-слоем на каждом кадре; масштаб — целочисленный кратный. */
int agat_video_expand(unsigned short*fb, int fb_w, int fb_h, int scale)
{
	struct SYS_RUN_STATE*sr = agat_get_state();
	const unsigned char*src;
	int x, y, dx, dy;

	if (!sr || !sr->bmp_bits || !fb || scale < 1) return -1;
	src = (const unsigned char*)sr->bmp_bits;

	for (y = 0; y < AGAT_FB_H; y++) {
		const unsigned char*line = src + y * sr->bmp_pitch;

		for (x = 0; x < AGAT_FB_W; x += 2) {
			unsigned short c0 = agat_pal565[line[x >> 1] & 0x0F];
			unsigned short c1 = agat_pal565[(line[x >> 1] >> 4) & 0x0F];

			for (dx = 0; dx < 2 * scale; dx++) {
				int ox = x * scale + dx;

				if (ox >= fb_w) break;
				for (dy = 0; dy < scale; dy++) {
					int oy = y * scale + dy;

					if (oy >= fb_h) break;
					fb[oy * fb_w + ox] = (dx < scale) ? c0 : c1;
				}
			}
		}
	}
	return 0;
}

void enable_ints(struct SYS_RUN_STATE*sr)
{
	if (sr) sr->ints_enabled = 1;
}

void disable_ints(struct SYS_RUN_STATE*sr)
{
	if (sr) sr->ints_enabled = 0;
}

int is_keyb_english(struct SYS_RUN_STATE*sr)
{
	(void)sr;
	return 0;
}

/* Apple-1 не входит в сборку прошивки (только Агат-9/Apple II). */
void flash_system_1(struct SYS_RUN_STATE*sr)
{
	(void)sr;
}

void apaint_apple1(struct VIDEO_STATE*vs, dword addr, RECT*r)
{
	(void)vs;
	(void)addr;
	(void)r;
}

/*=== Модели, которых нет в прошивке ======================================*/

int init_system_1(struct SYS_RUN_STATE*sr) { (void)sr; return -1; }
int init_system_2e(struct SYS_RUN_STATE*sr) { (void)sr; return -1; }
int init_system_aa(struct SYS_RUN_STATE*sr) { (void)sr; return -1; }

/*=== Слоты расширения: карты не эмулируются, слот остаётся пустым ========*/
/* r1298: возвращаем 0, а не -1. Конфиг Агат-9 по умолчанию ставит в слоты
   карты (xram9 и др.), и -1 срывал init_system_state → «Agat: init failed».
   Пустой слот безопасен: регистрация io-портов живёт внутри init карты. */

#define AGAT_SLOT_STUB(fn) \
	int fn(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*st, struct SLOTCONFIG*sc) \
	{ \
		(void)sr; \
		(void)sc; \
		if (st) st->data = NULL; \
		return 0; \
	}

AGAT_SLOT_STUB(a2ramcard_init)
AGAT_SLOT_STUB(aci_init)
AGAT_SLOT_STUB(applemouse_init)
AGAT_SLOT_STUB(cms_init)
AGAT_SLOT_STUB(extramaa_init)
AGAT_SLOT_STUB(extromaa_init)
AGAT_SLOT_STUB(mockingboard_init)
AGAT_SLOT_STUB(mouse9_init)
AGAT_SLOT_STUB(nippelclock_init)
AGAT_SLOT_STUB(nippelmouse_init)
AGAT_SLOT_STUB(noslotclock_init)
AGAT_SLOT_STUB(printer9_init)
AGAT_SLOT_STUB(printera_init)
AGAT_SLOT_STUB(printeraa_init)
AGAT_SLOT_STUB(psrom7_init)
AGAT_SLOT_STUB(saturnmem_init)
AGAT_SLOT_STUB(softcard_init)
AGAT_SLOT_STUB(tape_init)
AGAT_SLOT_STUB(thunderclock_init)
AGAT_SLOT_STUB(xram7_init)
AGAT_SLOT_STUB(xram9_init)
AGAT_SLOT_STUB(joystick_init)

/*=== Звуковые драйверы Windows (ACM/DirectSound/Beep) не поддерживаются ==*/

struct SOUNDPROC soundacm = { NULL, NULL, NULL, NULL };
struct SOUNDPROC soundbeep = { NULL, NULL, NULL, NULL };
struct SOUNDPROC soundds = { NULL, NULL, NULL, NULL };

/*=== Прочее ==============================================================*/

/* Вшитые образы Агат-9 (см. agat_roms.S): ресурсы Windows в прошивке
   заменены таблицей по номеру ресурса из sysconf.c. */
extern const unsigned char agat_rom_monitor9[2048];
extern const unsigned char agat_rom_agathe9[2048];

enum {
	AGAT_RES_AGATHE9 = 2,	/* FNTS\AGATHE9.FNT */
	AGAT_RES_MONITOR9 = 12,	/* ROMS\MONITOR9.ROM */
};

int load_buf_res(int no, void*buf, int len)
{
	const unsigned char*src = NULL;
	int sz = 0;

	switch (no) {
	case AGAT_RES_MONITOR9:
		src = agat_rom_monitor9;
		sz = 2048;
		break;
	case AGAT_RES_AGATHE9:
		src = agat_rom_agathe9;
		sz = 2048;
		break;
	default:
		break;
	}
	if (!buf || len <= 0) return -1;
	if (!src) {
		memset(buf, 0, (size_t)len);
		return -1;
	}
	if (len > sz) {
		memcpy(buf, src, (size_t)sz);
		memset((unsigned char*)buf + sz, 0, (size_t)(len - sz));
	} else {
		memcpy(buf, src, (size_t)len);
	}
	return 0;
}

const char* sys_get_parameter(const char*name)
{
	(void)name;
	return NULL;
}

void cancel_input_file(struct SYS_RUN_STATE*sr)
{
	(void)sr;
}

BOOL PlaySound(LPCTSTR s, HANDLE h, DWORD f)
{
	(void)s;
	(void)h;
	(void)f;
	return TRUE;
}

BOOL MessageBeep(UINT u)
{
	(void)u;
	return TRUE;
}
