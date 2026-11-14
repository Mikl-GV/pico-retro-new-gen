/*
	r1297: host-слой ядра Agat Emulator (Агат-9) для MultiTool Retro H3.

	Ядро эмулирует 6502 синхронно (cpu_run_cycles), кадр 40x25 знакомест
	DOUBLE_X/DOUBLE_Y = 640x400, палитра 16 цветов (Apple II/Агат).
	Здесь: создание системы, покадровый прогон, вывод в HDMI.
*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "emu.h"
#include "fb_text.h"
#include "usb_kbd.h"

#include "agat/src/sysconf.h"
#include "agat/src/runmgr.h"
#include "agat/src/runmgrint.h"
#include "agat/src/cpu/cpuint.h"
#include "agat/src/video/videoint.h"

/* Объекты ядра проходят agat_rename.sh: все внешние имена получают префикс
   agat_, поэтому host зовёт их по именам с префиксом. */
struct SYS_RUN_STATE* agat_get_state(void);
int agat_video_expand(unsigned short*fb, int fb_w, int fb_h, int scale);
void agat_video_size(int *w, int *h);
int agat_reset_config(struct SYSCONFIG*c, int systype);
void agat_set_disk_image(const void*data, unsigned size, const char*name);
struct SYS_RUN_STATE* agat_init_system_state(struct SYSCONFIG*c, void*hmain, const char*name);
int agat_free_system_state(struct SYS_RUN_STATE*sr);
int agat_cpu_run_cycles(struct CPU_STATE*cs, long long cycles);
int agat_cpu_cmd(struct CPU_STATE*cs, int cmd, int data, long param);
/* r1333: состояние канарейки за образом ПЗУ (0 — цела, -1 — нет данных). */
int agat_rom_canary_bad(void);
void agat_video_repaint_screen(struct VIDEO_STATE*vs);
unsigned char* agat_ramptr(struct SYS_RUN_STATE*sr);
int agat_system_command(struct SYS_RUN_STATE*sr, int id, int data, long param);

/* r1317: своя hex-печать для стендовой диагностики Агата. Кастомный
   printf добавляет лишние нули к %02X/%04X, поэтому векторы и стек
   форматируются вручную и выводятся только как строки. */
static void ag_hx(char *d, unsigned v, int nib)
{
	static const char hx[] = "0123456789ABCDEF";
	int i;

	for (i = nib - 1; i >= 0; i--) {
		d[i] = hx[v & 15u];
		v >>= 4;
	}
	d[nib] = 0;
}

/* r1338: HID usage → код клавиатуры Агат-9. Раскладка повторяет эталон
   (emulator-refs/agat/src/keyb.c, LAYOUT_LAT): без Shift буквы идут ВЕРХНИМ
   регистром, с Shift — нижним (Apple II/Агат; прежний код делал наоборот).
   Спец-клавиши — родные коды Агата: Enter $8D, Backspace $88, Tab $89,
   Up $91, Down $9E, Left $93, Right $9C, Home $99, End $9D, PgUp $92,
   PgDn $9F, Insert $81, Delete $82, F1..F3 $84..$86.
   Кириллицу (CP866 $81..$9A у эталона на УПР+буква) сознательно НЕ отдаём:
   машина стоит в ЛАТ, и любой русский код превращал латинский ввод в
   «русские буквы». Вернём отдельным явным переключателем, когда понадобится
   набор русского текста. */
static int agat_hid_to_code(uint8_t hid, int shift)
{
	if (hid >= 0x04 && hid <= 0x1D) {            /* A..Z */
		int ch = 'A' + (hid - 0x04);

		return shift ? (ch - 'A' + 'a') : ch;
	}
	if (hid >= 0x1E && hid <= 0x26) {            /* 1..9 */
		static const char digits[] = "!@#$%^&*(";

		return shift ? digits[hid - 0x1E] : ('1' + (hid - 0x1E));
	}
	if (hid == 0x27) return shift ? ')' : '0';
	switch (hid) {
	case 0x28: return 0x8D;                      /* Enter */
	case 0x29: return shift ? '~' : '`';         /* Esc машины */
	case 0x2A: return 0x88;                      /* Backspace */
	case 0x2B: return 0x89;                      /* Tab */
	case 0x2C: return ' ';
	case 0x2D: return shift ? '_' : '-';
	case 0x2E: return shift ? '+' : '=';
	case 0x2F: return shift ? '{' : '[';
	case 0x30: return shift ? '}' : ']';
	case 0x31: return shift ? '|' : '\\';
	case 0x32: return shift ? '~' : '#';
	case 0x33: return shift ? ':' : ';';
	case 0x34: return shift ? '"' : '\'';
	case 0x35: return shift ? '~' : '`';
	case 0x36: return shift ? '<' : ',';
	case 0x37: return shift ? '>' : '.';
	case 0x38: return shift ? '?' : '/';
	case 0x3A: return 0x84;                      /* F1 */
	case 0x3B: return 0x85;                      /* F2 */
	case 0x3C: return 0x86;                      /* F3 */
	case 0x49: return 0x81;                      /* Insert */
	case 0x4A: return 0x99;                      /* Home */
	case 0x4B: return 0x92;                      /* PgUp */
	case 0x4C: return 0x82;                      /* Delete */
	case 0x4D: return 0x9D;                      /* End */
	case 0x4E: return 0x9F;                      /* PgDn */
	case 0x4F: return 0x9C;                      /* Right */
	case 0x50: return 0x9E;                      /* Down */
	case 0x51: return 0x91;                      /* Up */
	case 0x52: return 0x93;                      /* Left */
	default:   return 0;
	}
}

/* Положить код в регистр клавиатуры ядра: старший бит — строб нажатия,
   keyb_clear() его снимает (SYSTEM_9 сохраняет сам код). */
static int agat_last_key;                        /* последний код — для лога */

static void agat_inject(struct SYS_RUN_STATE*sr, int ch)
{
	if (!ch) return;
	agat_last_key = ch;
	sr->input_hbit = ch >> 7;
	sr->cur_key = ch | 0x80;
}

/* Клавиатура: usb_kbd_get_raw() отдаёт КОМПАКТНЫЙ список удерживаемых
   HID-кодов (без байтов отчёта), модификаторы — отдельно из
   usb_kbd_get_mods(). Прежний код читал список как сырой отчёт: брал
   Shift из кода первой клавиши (отсюда «вводит не то») и начинал разбор
   с индекса 2 (отсюда «не на каждое нажатие реакция»).
   Автоповтор — как WM_KEYDOWN в эталоне: удержание 0.5 с, затем ~25 Гц. */
static void agat_keyboard(struct SYS_RUN_STATE*sr, const uint8_t *keys, int n)
{
	static uint8_t prev[8];
	static uint8_t rep_code;
	static uint16_t rep_frames;
	uint8_t mods = usb_kbd_get_mods();
	int shift = (mods & 0x22) != 0;               /* LShift|RShift */
	int i, j;
	int fresh = 0;
	int held = 0;

	if (n > 8) n = 8;

	for (i = 0; i < n; i++) {
		uint8_t c = keys[i];

		if (!c) continue;
		if (c == rep_code) held = 1;
		for (j = 0; j < 8; j++) if (prev[j] == c) break;
		if (j == 8 && !fresh) fresh = c;          /* нажата в этом кадре */
	}

	if (fresh) {
		int ch = agat_hid_to_code((uint8_t)fresh, shift);
		char hh[3], hm[3], hk[3];

		rep_code = (uint8_t)fresh;
		rep_frames = 0;
		/* r1338: по одной строке на нажатие — видно и HID, и модификаторы,
		   и код, ушедший в машину (жалобы «вводит не то»). */
		ag_hx(hh, (unsigned)fresh, 2);
		ag_hx(hm, (unsigned)mods, 2);
		ag_hx(hk, (unsigned)(ch & 0xFF), 2);
		printf("Agat: key HID=%s mods=%s -> %s\n", hh, hm, hk);
		agat_inject(sr, ch);
	} else if (rep_code && held) {
		if (++rep_frames >= 25 && (rep_frames & 1) == 0) {
			agat_inject(sr, agat_hid_to_code(rep_code, shift));
		}
	} else if (!held) {
		rep_code = 0;
		rep_frames = 0;
	}

	for (i = 0; i < 8; i++) prev[i] = (i < n) ? keys[i] : 0;
}

#define AGAT_FRAME_W 640
#define AGAT_FRAME_H 512
/* 1 МГц 6502, PAL 50 Гц -> 20000 тактов на кадр. */
#define AGAT_CYCLES_PER_FRAME 20000
#define AGAT_FRAME_US 20000

void emu_run_agat(const uint8_t* rom, uint32_t size, const char* rom_name)
{
	static uint16_t frame[AGAT_FRAME_W * AGAT_FRAME_H];
	struct SYSCONFIG cfg;
	struct SYS_RUN_STATE *sr;
	struct CPU_STATE *cs;
	struct VIDEO_STATE *vs;

	(void)rom;
	(void)size;

	emu_prepare();
	snd_manifest("agat", "beeper");

	agat_reset_config(&cfg, SYSTEM_9);
	/* Образ, выбранный в ROM-браузере, монтируем в первый дисковод:
	   isfopen() отдаёт его потоком из памяти (ФС на bare-metal нет). */
	agat_set_disk_image(rom, size, rom_name);
	/* Звук: драйверы Windows (MMSystem/DSound) в прошивке не работают,
	   ставим «нет звука» — иначе sound.c дёргает чужой SOUNDPROC. */
	cfg.slots[CONF_SOUND].dev_type = DEV_NOSOUND;
	if (rom_name) {
		int i;
		int nib = 0;
		size_t ln;

		/* r1335: формат образа определяет контроллер. Nibble-образы
		   (.nib) читает только Shugart (слот 6); секторные .dsk/.aim —
		   только TEAC (слот 5). Если оставить оба, монитор всегда берёт
		   TEAC и .nib перестают грузиться. */
		ln = strlen(rom_name);
		if (ln >= 4 && !strcasecmp(rom_name + ln - 4, ".nib")) nib = 1;

		for (i = 0; i < NCONFTYPES; i++) {
			struct SLOTCONFIG *sc = &cfg.slots[i];

			if (sc->dev_type != DEV_FDD_SHUGART && sc->dev_type != DEV_FDD_TEAC) continue;
			if (nib && sc->dev_type == DEV_FDD_TEAC) {
				sc->dev_type = DEV_NULL;   /* nibble: TEAC не подключаем */
				continue;
			}
			strncpy(sc->cfgstr[CFG_STR_DRV_IMAGE1], rom_name, CFGSTRLEN - 1);
			sc->cfgstr[CFG_STR_DRV_IMAGE1][CFGSTRLEN - 1] = 0;
		}
		printf("Agat: образ %s — контроллер %s\n", rom_name, nib ? "SHUGART (nib)" : "TEAC (dsk/aim)");
	}
	sr = agat_init_system_state(&cfg, NULL, "agat");
	if (!sr) {
		printf("Agat: init failed\n");
		return;
	}

	cs = (struct CPU_STATE*)sr->slots[CONF_CPU].data;
	vs = get_video_state(sr);
	if (!cs || !vs) {
		printf("Agat: cpu/video state missing\n");
		agat_free_system_state(sr);
		return;
	}

	/* Без потока CPU никто не делал сброс: PC оставался 0, ядро крутило
	   мусор по нулевому адресу (стенд: чёрный экран без выхода по ESC).
	   HRESET берёт вектор из FFFC вшитого ПЗУ при первой инструкции. */
	agat_cpu_cmd(cs, SYS_COMMAND_HRESET, 0, 0);
	/* Растровый таймер видео: в оригинале его ставит GUI при старте системы.
	   Без него raster-блоки не переключаются — на экране мусор из цветных
	   пикселей, а не кадр. Два таймера (чёт/нечет) с периодом N_RBINT_DELAY_9. */
	/* Растровые таймеры ставит сам видеояд инициализатор (set_rb_count:
	   20000 тактов на кадр и 20000/N_RBINT_9 на блок) — здесь их не трогаем,
	   иначе перезаписываем корректные значения. */
	printf("Agat: init ok\n");

	emu_set_period_us(AGAT_FRAME_US);
	emu_throttle_reset();
	emu_esc_hold_reset();

	for (;;) {
		static int dbg;
		uint8_t keys[6];
		int fw = AGAT_FRAME_W, fh = AGAT_FRAME_H;

		/* Кэш последнего отчёта клавиатуры обновляется здесь: emu_esc_hold()
		   читает только его, без этого ESC с клавиатуры не виден. */
		{
			int nk = usb_kbd_get_raw(keys, 6);

			agat_keyboard(sr, keys, nk);
		}
		agat_cpu_run_cycles(cs, AGAT_CYCLES_PER_FRAME);
		agat_video_repaint_screen(vs);
		/* Размер кадра задаёт видеоядро (Агат-9: 512x512) — берём его,
		   иначе expand пишет за пределы буфера. */
		agat_video_size(&fw, &fh);
		if (fw > AGAT_FRAME_W) fw = AGAT_FRAME_W;
		if (fh > AGAT_FRAME_H) fh = AGAT_FRAME_H;
		agat_video_expand(frame, fw, fh, 1);
		emu_scale_rgb565_src_aspect(frame, fw, fh, fw, 4, 3);
		fb_flush();
		emu_throttle();
		/* r1338: краткая телеметрия вместо прежних дампов RAM/векторов
		   (они своё отработали). nz — ненулевые байты кадра: если 0,
		   машина не рисует; base/vm — текущий растровый блок; cn —
		   канарейка ПЗУ; k — последний введённый код клавиши. */
		if (++dbg <= 3 || dbg == 10 || dbg == 50 || (dbg % 500) == 0) {
			unsigned nz = 0;
			unsigned k;
			const unsigned char *vp = (const unsigned char*)sr->bmp_bits;
			char kh[3];

			for (k = 0; k < 320u * 32u; k++) nz += (vp[k] != 0);
			ag_hx(kh, (unsigned)(agat_last_key & 0xFF), 2);
			printf("Agat: frames=%d pc=%04X instr=%lu rb=%d/%d vt=%d base=%06X nz=%u vm=%02X cn=%d k=%s\n",
			       dbg, cs->dbg_pc, cs->instr_total, vs->rbi, vs->n_rb,
			       vs->rb_cur.vtype, vs->rb_cur.base_addr[0], nz,
			       vs->rb_cur.vmode, agat_rom_canary_bad(), kh);
		}
		if (emu_esc_hold()) break;
	}

	agat_free_system_state(sr);
	emu_audio_stop();
	fb_clear();
	fb_flush();
}
