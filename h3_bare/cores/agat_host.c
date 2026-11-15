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
#include "btn_pad.h"

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
/* r1344: маска пада в модуль джойстика (src/joystick.c). */
int agat_joystick_apply_pad(struct SYS_RUN_STATE*sr, uint16_t pad);
/* r1360: набор RGB565 — 0 Агат-9, 1 Apple II (PALETTE\APPLE2.PAL). */
void agat_set_apple2_palette(int on);

/* r1345: hex-печать для диагностики образа. Кастомный printf добавляет
   лишние нули к %02X/%04X, поэтому числа переводим в строку сами. */
static void ag_hex(char *d, unsigned v, int nib)
{
	static const char hx[] = "0123456789ABCDEF";
	int i;

	for (i = nib - 1; i >= 0; i--) {
		d[i] = hx[v & 15u];
		v >>= 4;
	}
	d[nib] = 0;
}

/* r1341: HID usage → код клавиатуры Агат-9. Источник — файл раскладки
   эталона app/keyb/default.bin (1024 б = раскладки ЛАТ/РУС × NORM/SHIFT/
   CTRL/EXT), который эмулятор реально грузит; свериться можно выгрузкой:
   `[LAT/NORM]`, `[LAT/EXT]`.
   Ключевое отличие от прежней таблицы: у Агата клавиатурный код — это
   7-битный КОИ-7 плюс бит 7 (строб), и НЕСКОЛЬКО разных клавиш дают один
   и тот же код:
     ЗБ и ←      → $88 (оба $08 с битом строба) — поэтому Backspace «двигает»;
     ВК          → $8D (CR);
     ТАБ         → $89 (HT);
     ПРОБЕЛ      → $A0 ($20 | строб);
     АР2/Esc     → $9B;
     ← → ↑ ↓     → $88 $95 $99 $9A (строка EXT; $95 = NAK, как Ctrl+U);
     цифровой блок → $91 $92 $93 $94 $9C $9D $9E $9F $81 $82;
     Ф1..Ф3      → $84..$86.
   Без Shift буквы идут ВЕРХНИМ регистром, с Shift — нижним (Apple II/Агат).
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
	case 0x28: return 0x8D;                      /* Enter/ВК */
	case 0x29: return 0x9B;                      /* Esc/АР2 */
	case 0x2A: return 0x88;                      /* Backspace/ЗБ (= ←, см. ниже) */
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
	case 0x4A: return 0x8B;                      /* Home (столбец 1) */
	case 0x4B: return 0x92;                      /* PgUp */
	case 0x4C: return 0x82;                      /* Delete */
	case 0x4D: return 0x9D;                      /* End */
	case 0x4E: return 0x9F;                      /* PgDn */
	/* r1341: стрелки — как в keyb/default.bin эталона (строка EXT) и как
	   их сравнивают игры: Right $95, Left $88 (= ЗБ), Down $9A, Up $99.
	   Прежние $9C/$93/$9E/$91 — это коды цифрового блока (см. KP ниже). */
	case 0x4F: return 0x95;                      /* Right */
	case 0x50: return 0x88;                      /* Left */
	case 0x51: return 0x9A;                      /* Down */
	case 0x52: return 0x99;                      /* Up */
	/* Цифровой блок: у Агата он отдельный, коды как у эталона. */
	case 0x5D: return 0x94;                      /* KP5 (центр) */
	case 0x5A: return 0x9E;                      /* KP2 (вниз) */
	case 0x5C: return 0x93;                      /* KP4 (влево) */
	case 0x5E: return 0x9C;                      /* KP6 (вправо) */
	case 0x60: return 0x91;                      /* KP8 (вверх) */
	default:   return 0;
	}
}

/* r1360: HID usage → ASCII для Apple II+. Машина читает $C000: младшие
   7 бит — код, бит 7 — строб. Apple II+ не имеет нижнего регистра, буквы
   всегда заглавные; Shift меняет только цифры и символы. Enter = CR,
   Esc = $1B, Backspace = $08, Tab = $09. Стрелки отдаём как Ctrl-H/U/J/K —
   именно эти коды понимает монитор Apple II при редактировании строки. */
static int apple2_hid_to_code(uint8_t hid, int shift)
{
	if (hid >= 0x04 && hid <= 0x1D) {            /* A..Z — только верхний */
		return 'A' + (hid - 0x04);
	}
	if (hid >= 0x1E && hid <= 0x26) {            /* 1..9 */
		static const char digits[] = "!@#$%^&*(";

		return shift ? digits[hid - 0x1E] : ('1' + (hid - 0x1E));
	}
	if (hid == 0x27) return shift ? ')' : '0';
	switch (hid) {
	case 0x28: return 0x0D;                      /* Enter  */
	case 0x29: return 0x1B;                      /* Esc    */
	case 0x2A: return 0x08;                      /* Delete */
	case 0x2B: return 0x09;                      /* Tab    */
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
	case 0x4F: return 0x15;                      /* Right = Ctrl-U */
	case 0x50: return 0x08;                      /* Left  = Ctrl-H */
	case 0x51: return 0x0A;                      /* Down  = Ctrl-J */
	case 0x52: return 0x0B;                      /* Up    = Ctrl-K */
	default:   return 0;
	}
}

/* Положить код в регистр клавиатуры ядра: старший бит — строб нажатия,
   keyb_clear() его снимает (SYSTEM_9 сохраняет сам код). */
static void agat_inject(struct SYS_RUN_STATE*sr, int ch)
{
	if (!ch) return;
	sr->input_hbit = ch >> 7;
	sr->cur_key = ch | 0x80;
}

/* Клавиатура: usb_kbd_get_raw() отдаёт КОМПАКТНЫЙ список удерживаемых
   HID-кодов (без байтов отчёта), модификаторы — отдельно из
   usb_kbd_get_mods(). Прежний код читал список как сырой отчёт: брал
   Shift из кода первой клавиши (отсюда «вводит не то») и начинал разбор
   с индекса 2 (отсюда «не на каждое нажатие реакция»).
   Автоповтор — как WM_KEYDOWN в эталоне: удержание 0.5 с, затем ~25 Гц. */
static void agat_keyboard_map(struct SYS_RUN_STATE*sr, const uint8_t *keys, int n,
                              int (*map)(uint8_t, int))
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
		int ch = map((uint8_t)fresh, shift);

		rep_code = (uint8_t)fresh;
		rep_frames = 0;
		agat_inject(sr, ch);
	} else if (rep_code && held) {
		if (++rep_frames >= 25 && (rep_frames & 1) == 0) {
			agat_inject(sr, map(rep_code, shift));
		}
	} else if (!held) {
		rep_code = 0;
		rep_frames = 0;
	}

	for (i = 0; i < 8; i++) prev[i] = (i < n) ? keys[i] : 0;
}

static void agat_keyboard(struct SYS_RUN_STATE*sr, const uint8_t *keys, int n)
{
	agat_keyboard_map(sr, keys, n, agat_hid_to_code);
}

static void apple2_keyboard(struct SYS_RUN_STATE*sr, const uint8_t *keys, int n)
{
	agat_keyboard_map(sr, keys, n, apple2_hid_to_code);
}

/* r1344: пад и кнопки дублируются в клавиатуру машины. Игры, читающие
   $C000, про джойстик $C070 не знают, поэтому направления дают коды
   стрелок, A/B — пробел, Start — ВК. Порт джойстика при этом тоже получает
   маску (agat_joystick_apply_pad), так что работают оба канала сразу:
   клавиатура, Sega-пад и 8-битная панель. */
static int agat_bit_code(uint16_t bit)
{
	switch (bit) {
	case 0x01: return 0x99;                      /* UP */
	case 0x02: return 0x9A;                      /* DOWN */
	case 0x04: return 0x88;                      /* LEFT */
	case 0x08: return 0x95;                      /* RIGHT */
	case 0x10:                                   /* A */
	case 0x20: return ' ';                       /* B */
	case 0x80: return 0x8D;                      /* START = ВК/Enter */
	default:   return 0;
	}
}

static void agat_pad_keys(struct SYS_RUN_STATE*sr, uint16_t pad)
{
	static const uint16_t order[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x80 };
	static uint16_t prev, rep_bit, rep_frames;
	uint16_t fresh = (uint16_t)(pad & ~prev);
	uint16_t bit = 0;
	int code = 0;
	unsigned i;

	for (i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
		if (fresh & order[i]) { bit = order[i]; break; }
	}
	if (bit) {
		rep_bit = bit;
		rep_frames = 0;
		code = agat_bit_code(bit);
	} else if (rep_bit && (pad & rep_bit)) {
		/* Автоповтор как у клавиатуры: 0.5 с, затем ~25 Гц. */
		if (++rep_frames >= 25 && (rep_frames & 1) == 0) code = agat_bit_code(rep_bit);
	} else {
		rep_bit = 0;
		rep_frames = 0;
	}
	agat_inject(sr, code);
	prev = pad;
}

/* r1360: Apple II — пад работает через порт джойстика ($C061/$C062/$C064/
   $C065), дублировать направления в клавиатуру не нужно. В клавиатуру
   отдаём только Start: он жмёт Enter (запуск с диска, меню BASIC). */
static void apple2_pad_keys(struct SYS_RUN_STATE*sr, uint16_t pad)
{
	static uint16_t prev;

	if ((pad & 0x80) && !(prev & 0x80)) agat_inject(sr, 0x0D);
	prev = pad;
}

#define AGAT_FRAME_W 640
#define AGAT_FRAME_H 512
/* 1 МГц 6502, PAL 50 Гц -> 20000 тактов на кадр. */
#define AGAT_CYCLES_PER_FRAME 20000
#define AGAT_FRAME_US 20000
/* Apple II: 1.0205 МГц, NTSC 60 Гц -> ~17008 тактов на кадр. */
#define APPLE2_CYCLES_PER_FRAME 17030
#define APPLE2_FRAME_US 16667

/* r1360: общий прогон для Агат-9 и Apple II+. Обе машины — 6502 и
   видеоядро Agat Emulator (VIDEO_MODE_APPLE). Отличия: тип системы
   (SYSTEM_9 / SYSTEM_P), вшитый набор ПЗУ, палитра, частота кадра и
   раскладка клавиатуры. */
static void agat_run_common(int systype, int apple2, const char *label,
                            const uint8_t* rom, uint32_t size, const char* rom_name)
{
	static uint16_t frame[AGAT_FRAME_W * AGAT_FRAME_H];
	struct SYSCONFIG cfg;
	struct SYS_RUN_STATE *sr;
	struct CPU_STATE *cs;
	struct VIDEO_STATE *vs;
	uint32_t cycles_per_frame = apple2 ? APPLE2_CYCLES_PER_FRAME : AGAT_CYCLES_PER_FRAME;
	uint32_t frame_us = apple2 ? APPLE2_FRAME_US : AGAT_FRAME_US;

	emu_prepare();
	snd_manifest(label, "beeper");

	agat_reset_config(&cfg, systype);
	agat_set_apple2_palette(apple2);
	/* Образ, выбранный в ROM-браузере, монтируем в первый дисковод:
	   isfopen() отдаёт его потоком из памяти (ФС на bare-metal нет). */
	agat_set_disk_image(rom, size, rom_name);
	/* Звук: драйверы Windows (MMSystem/DSound) в прошивке не работают,
	   ставим «нет звука» — иначе sound.c дёргает чужой SOUNDPROC. */
	cfg.slots[CONF_SOUND].dev_type = DEV_NOSOUND;

	if (apple2) {
		/* Apple II+: ПЗУ Applesoft вшито (ресурс 14), знакогенератор —
		   ресурс 3, ПЗУ контроллера Disk II — ресурс 24. Карты слотов
		   (Language Card, принтер) в прошивке не эмулируются, поэтому
		   слоты 0/1 отключаем: пусть машина будет честной 48K. */
		cfg.slots[CONF_ROM].cfgint[CFG_INT_ROM_RES] = 14;
		cfg.slots[CONF_SLOT0].dev_type = DEV_NULL;
		cfg.slots[CONF_SLOT1].dev_type = DEV_NULL;
		if (rom_name) {
			int ok = 0;
			size_t ln = strlen(rom_name);

			if (size == 143360u) ok = 1;                 /* .dsk/.do/.po */
			else if (ln >= 4 && !strcasecmp(rom_name + ln - 4, ".nib") &&
			         size && (size % 6656u) == 0) ok = 1;
			else if (ln >= 4 && !strcasecmp(rom_name + ln - 4, ".woz") &&
			         size > 12) ok = 1;
			if (ok) {
				strncpy(cfg.slots[CONF_SLOT6].cfgstr[CFG_STR_DRV_IMAGE1],
				        rom_name, CFGSTRLEN - 1);
				cfg.slots[CONF_SLOT6].cfgstr[CFG_STR_DRV_IMAGE1][CFGSTRLEN - 1] = 0;
				printf("Apple II: образ %s (%u б)\n", rom_name, size);
			} else {
				printf("Apple II: %s (%u б) — не .dsk/.do/.po (143360),"
				       " не .nib и не .woz; дисковод отключён\n", rom_name, size);
				cfg.slots[CONF_SLOT6].dev_type = DEV_NULL;
			}
		}
	} else if (rom_name) {
		/* r1345: контроллер выбирается по формату образа, а не только по
		   расширению. Геометрии разные и несовместимые:
		     TEAC    (слот 5) — 21 сектор × 256 б на дорожку, линейно,
		                        образ 840 КБ (160×5376 = 860160 + метка);
		     Shugart (слот 6) — 16 секторов со скосом DOS/ProDOS (140 КБ =
		                        35×16×256) либо nibble-дорожки по 6656 б
		                        (35 дорожек = 232960).
		   Образ не в своём слоте читается мусором, а ПЗУ карты TEAC на
		   $C55F (`LDA $C086,X / BMI`) вечно ждёт байт от привода — стенд
		   висел на pc=C55F. Поэтому неподходящий слот отключаем (DEV_NULL),
		   а несовместимый размер честно сообщаем и не монтируем.
		   r1345: 840-КБ образы часто длиннее на метку в конце (GAV_GAV —
		   128 байт имени), поэтому хвост до 512 б отрезаем, а не отказываем. */
		enum { AG_FMT_NONE, AG_FMT_TEAC, AG_FMT_SHUGART };
		int fmt = AG_FMT_NONE;
		const char *fmt_txt = "формат не поддержан";
		uint32_t core = size, tail = 0;
		int is_nib = 0;
		size_t ln = strlen(rom_name);
		int i;

		if (size > 5376u && (size % 5376u) != 0) {
			uint32_t t = size % 5376u;

			if (t <= 512u) { core = size - t; tail = t; }
		}

		if (ln >= 4 && !strcasecmp(rom_name + ln - 4, ".nib")) {
			is_nib = 1;
			if (size && (size % 6656u) == 0) {
				fmt = AG_FMT_SHUGART;
				fmt_txt = "SHUGART, nibble 6656";
			} else {
				printf("Agat: .nib %u б не кратен 6656 — не поддержано\n", size);
			}
		} else if (ln >= 4 && !strcasecmp(rom_name + ln - 4, ".aim")) {
			if (size && (size % (6464u * 2u)) == 0) {
				fmt = AG_FMT_TEAC;
				fmt_txt = "TEAC, aim";
			} else {
				printf("Agat: .aim %u б не кратен %u — не поддержано\n", size, 6464u * 2u);
			}
		} else if (core == 143360u) {
			/* 140 КБ: 16 секторов, порядок DOS — это Shugart. */
			fmt = AG_FMT_SHUGART;
			fmt_txt = "SHUGART, 140 КБ (16 секторов)";
		} else if (core && (core % 5376u) == 0) {
			/* 840 КБ и прочие образы по 21 сектору — TEAC. */
			fmt = AG_FMT_TEAC;
			fmt_txt = "TEAC, 21 сектор";
		} else {
			printf("Agat: размер %u б не соответствует ни 140 КБ (143360),"
			       " ни 840 КБ (кратно 5376) — не поддержано\n", size);
		}

		for (i = 0; i < NCONFTYPES; i++) {
			struct SLOTCONFIG *sc = &cfg.slots[i];
			int want;

			if (sc->dev_type != DEV_FDD_SHUGART && sc->dev_type != DEV_FDD_TEAC) continue;
			want = (fmt == AG_FMT_TEAC) ? DEV_FDD_TEAC : DEV_FDD_SHUGART;
			if (fmt == AG_FMT_NONE || sc->dev_type != want) {
				sc->dev_type = DEV_NULL;   /* чужой контроллер не подключаем */
				continue;
			}
			strncpy(sc->cfgstr[CFG_STR_DRV_IMAGE1], rom_name, CFGSTRLEN - 1);
			sc->cfgstr[CFG_STR_DRV_IMAGE1][CFGSTRLEN - 1] = 0;
		}
		printf("Agat: образ %s (%u б) — %s\n", rom_name, size, fmt_txt);
		if (tail) printf("Agat: хвост %u б (метка диска) игнорируется\n", tail);
		if (fmt == AG_FMT_SHUGART && !is_nib) {
			char h[4][3];
			unsigned k;

			printf("Agat: %u дорожек × 16 секторов × 256\n", core / 4096u);
			for (k = 0; k < 4; k++) ag_hex(h[k], rom[k], 2);
			printf("Agat: загрузочный сектор %s %s %s %s%s\n",
			       h[0], h[1], h[2], h[3],
			       (rom[0] >= 1u && rom[0] <= 16u) ? "" : "  ← не похож на загрузочный");
		} else if (fmt == AG_FMT_TEAC && !is_nib) {
			char h[4][3];
			unsigned k;

			printf("Agat: %u дорожек × 21 сектор × 256\n", core / 5376u);
			for (k = 0; k < 4; k++) ag_hex(h[k], rom[k], 2);
			printf("Agat: загрузочный сектор %s %s %s %s%s\n",
			       h[0], h[1], h[2], h[3],
			       (rom[0] >= 1u && rom[0] <= 21u) ? "" : "  ← не похож на загрузочный");
		} else if (fmt == AG_FMT_SHUGART && is_nib) {
			/* Оценка GCR: сколько адресных и данных полей на дорожке 0.
			   Ноль адресных — образ не nibble-GCR, карта его не прочтёт. */
			unsigned a = 0, d = 0, k;
			unsigned n = (size < 6656u) ? size : 6656u;

			for (k = 0; k + 2 < n; k++) {
				if (rom[k] == 0xD5 && rom[k + 1] == 0xAA) {
					if (rom[k + 2] == 0x96) a++;
					else if (rom[k + 2] == 0xAD) d++;
				}
			}
			printf("Agat: %u дорожек × 6656; дорожка 0: адресных полей %u,"
			       " данных %u%s\n", size / 6656u, a, d,
			       a ? "" : "  ← не GCR-дорожка");
		}
	}
	sr = agat_init_system_state(&cfg, NULL, label);
	if (!sr) {
		printf("%s: init failed\n", label);
		return;
	}

	cs = (struct CPU_STATE*)sr->slots[CONF_CPU].data;
	vs = get_video_state(sr);
	if (!cs || !vs) {
		printf("%s: cpu/video state missing\n", label);
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
	/* r1340: канарейка за образом ПЗУ — только при ошибке (переполнение
	   буфера ПЗУ ломало загрузку молча). */
	if (agat_rom_canary_bad() > 0) printf("%s: ROM canary BAD\n", label);
	printf("%s: init ok\n", label);

	emu_set_period_us(frame_us);
	emu_throttle_reset();
	emu_esc_hold_reset();

	for (;;) {
		uint8_t keys[6];
		int fw = AGAT_FRAME_W, fh = AGAT_FRAME_H;

		/* Кэш последнего отчёта клавиатуры обновляется здесь: emu_esc_hold()
		   читает только его, без этого ESC с клавиатуры не виден. */
		{
			int nk = usb_kbd_get_raw(keys, 6);
			uint16_t pad = pad_scan_combined();

			if (apple2) {
				apple2_keyboard(sr, keys, nk);
				agat_joystick_apply_pad(sr, pad);
				apple2_pad_keys(sr, pad);
			} else {
				agat_keyboard(sr, keys, nk);
				agat_joystick_apply_pad(sr, pad);
				agat_pad_keys(sr, pad);
			}
		}
		agat_cpu_run_cycles(cs, cycles_per_frame);
		agat_video_repaint_screen(vs);
		/* Размер кадра задаёт видеоядро (Агат-9: 512x512, Apple II:
		   560x384) — берём его, иначе expand пишет за пределы буфера. */
		agat_video_size(&fw, &fh);
		if (fw > AGAT_FRAME_W) fw = AGAT_FRAME_W;
		if (fh > AGAT_FRAME_H) fh = AGAT_FRAME_H;
		if (fw <= 0 || fh <= 0) { fw = AGAT_FRAME_W; fh = AGAT_FRAME_H; }
		agat_video_expand(frame, fw, fh, 1);
		emu_scale_rgb565_src_aspect(frame, fw, fh, fw, 4, 3);
		fb_flush();
		emu_throttle();
		if (emu_esc_hold()) break;
	}

	agat_free_system_state(sr);
	emu_audio_stop();
	fb_clear();
	fb_flush();
}

void emu_run_agat(const uint8_t* rom, uint32_t size, const char* rom_name)
{
	agat_run_common(SYSTEM_9, 0, "Agat", rom, size, rom_name);
}

void emu_run_apple2(const uint8_t* rom, uint32_t size, const char* rom_name)
{
	agat_run_common(SYSTEM_P, 1, "Apple II", rom, size, rom_name);
}
