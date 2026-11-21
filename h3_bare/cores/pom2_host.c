/*
	r1388: слой Apple II на ядре POM2 (h3_bare/ports/pom2) вместо agat-ядра.

	Агат остаётся на agat_host.c — здесь только Apple II. ПЗУ Apple II+
	(12 КБ) вшито в прошивку (agat_rom_apple2p, agat_roms.S), образ диска
	приходит байтами из rom_browser: файловой системы на железе нет.
*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "btn_pad.h"
#include "emu.h"
#include "emulog.h"
#include "fb_text.h"
#include "h3_hs_timer.h"
#include "i2s.h"
#include "usb_kbd.h"
#include "pom2_h3.h"

extern const uint8_t agat_rom_apple2p[];
extern const uint8_t pom2_rom_apple2e[];
extern int apple2_hid_to_code(uint8_t hid, int shift);

#define APPLE2P_ROM_SIZE 12288
#define APPLE2E_ROM_SIZE 16384

/* r1402: модель машины для следующего запуска: 0 = Apple ][+ (48 КБ,
   ПЗУ 12 КБ), 1 = Apple //e (128 КБ: 64 КБ main + 64 КБ aux, ПЗУ 16 КБ).
   Ставится диалогом слоя (apple2_source_dialog) ДО emu_run_apple2. */
static int s_apple2_model;

void emu_set_apple2_model(int model)
{
	s_apple2_model = model ? 1 : 0;
}

/* Коды клавиш Apple II, удерживаемые в прошлом кадре: новые дают нажатие,
   пропавшие — отпускание (POM2 различает физическое удержание и символ,
   см. pom2_h3_key). */
static uint8_t s_codes[6];
static int     s_codes_n;

/* Звук: динамик Apple II приходит ПАРАМИ (L,R) — формат i2s_push_block. */
static int16_t s_audio[2 * 1024];

/* r1394: горячий свап дискеты по F12 — общий механизм disk_swap.c
   (emu_disk_poll раз в кадр ДО машины, иначе F12 уйдёт в эмулятор).
   Ядру нужны БАЙТЫ: файловой системы на железе нет, поэтому in_memory. */
static int apple2_disk_insert(int drive, const char* path,
                              const uint8_t* data, uint32_t size,
                              const char* name)
{
	(void)path;
	const int rc = pom2_h3_insert_disk(drive, data, size, name);
	if (rc != 0)
		printf("pom2: disk insert failed rc=%d drive=%d name=%s\n",
		       rc, drive, name ? name : "-");
	else
		printf("pom2: disk insert ok drive=%d size=%u name=%s\n",
		       drive, (unsigned)size, name ? name : "-");
	return rc;
}

static const emu_disk_ops_t s_apple2_disk_ops = {
	2,                       /* drives: Disk II — два привода */
	"dsk;do;po;nib;woz",     /* exts      */
	1,                       /* in_memory */
	apple2_disk_insert
};

void emu_run_apple2_pom2(const uint8_t* disk, uint32_t disk_size,
                         const char* name)
{
	/* r1391: старт эмулятора — как у всех host-слоёв. emu_prepare()
	   обнуляет newlib-кучу (она bump-аллокатор без free: без сброса каждый
	   запуск терял несколько МБ и на пятом `new Memory()` падал в аборт),
	   сбрасывает кольцо I2S и состояние ESC. */
	emu_prepare();
	const int iie = s_apple2_model;
	emulog_begin(iie ? "A2E" : "A2P");
	emulog_field(iie ? "A2E" : "A2P", "rom",
	             "%s, %u B", (disk && disk_size) ? (name ? name : "?") : "BASIC",
	             (unsigned)disk_size);

	const uint8_t* rom = iie ? pom2_rom_apple2e : agat_rom_apple2p;
	const unsigned rom_size = iie ? APPLE2E_ROM_SIZE : APPLE2P_ROM_SIZE;

	/* r1404: стартовая строка — по ней видно, дошёл ли запуск до ядра
	   (между `apple2: run` в браузере и этой строкой падений быть не должно). */
	printf("pom2: start model=%d disk=%u name=%s\n",
	       iie, (unsigned)disk_size, name ? name : "-");

	const int rc = pom2_h3_boot(rom, rom_size, iie,
	                            disk, disk_size, name);
	if (rc != 0) {
		printf("pom2: boot failed rc=%d model=%d rom=%u disk=%u name=%s\n",
		       rc, iie, rom_size, (unsigned)disk_size,
		       name ? name : "-");
		emulog_field(iie ? "A2E" : "A2P", "init", "FAILED: boot");
		emulog_end(iie ? "A2E" : "A2P");
		return;
	}
	emulog_field(iie ? "A2E" : "A2P", "video", "280x192");
	emulog_field(iie ? "A2E" : "A2P", "machine", "%s", iie ? "Apple IIe (65C02)" : "Apple II+ (6502)");
	emulog_field(iie ? "A2E" : "A2P", "init", "OK");

	emu_esc_hold_reset();
	s_codes_n = 0;
	emu_disk_register(&s_apple2_disk_ops);

	for (;;) {
		emu_disk_poll();   /* F12 — до кадра, иначе уйдёт в машину */
		pom2_h3_frame();

		int w = 0, h = 0;
		const uint32_t* px = pom2_h3_pixels(&w, &h);
		if (px && w > 0 && h > 0) {
			/* Целый множитель из 280×192 (×3 = 840×576 в 1024×600):
			   дробный nearest рвал 7-пиксельные столбцы символов.
			   r1547: GSU-путь откатан — нижняя часть кадра «плыла» по
			   горизонтали, программный путь стабилен. */
			emu_show_rgba_int(px, w, h, w);
			fb_flush();
		}

		/* Звук кадра: динамик Apple II -> пара (L,R) в кольцо I2S. */
		const int na = pom2_h3_audio(s_audio, 1024);
		if (na > 0) i2s_push_block(s_audio, (uint32_t)na);

		emu_throttle();

		/* Клавиатура: собираем коды Apple II этого кадра и сравниваем с
		   прошлым — новые жмут, пропавшие отпускают. */
		uint8_t keys[6];
		const int nk = usb_kbd_get_raw(keys, 6);
		const uint8_t mods = usb_kbd_get_mods();
		const int shift = (mods & 0x22) != 0;   /* LShift | RShift */
		uint8_t codes[6];
		int nc = 0;
		for (int i = 0; i < nk && nc < 6; i++) {
			const int ch = apple2_hid_to_code(keys[i], shift);
			if (ch > 0) codes[nc++] = (uint8_t)ch;
		}
		for (int i = 0; i < nc; i++) {
			int was = 0;
			for (int j = 0; j < s_codes_n; j++)
				if (s_codes[j] == codes[i]) { was = 1; break; }
			if (!was) pom2_h3_key(codes[i], 1);
		}
		for (int j = 0; j < s_codes_n; j++) {
			int now = 0;
			for (int i = 0; i < nc; i++)
				if (codes[i] == s_codes[j]) { now = 1; break; }
			if (!now) pom2_h3_key(s_codes[j], 0);
		}
		memcpy(s_codes, codes, (size_t)nc);
		s_codes_n = nc;

		/* Джойстик: паддлы 0/1 — оси (2/3 их повторяют), кнопки 0/1 — A/B;
		   стрелки и пробел дублируют пад, как у остальных ядер. */
		const uint16_t pad = pad_scan_combined();
		uint8_t paddles[4] = { 128, 128, 128, 128 };
		int b0 = (pad & 0x10) != 0;             /* A     */
		int b1 = (pad & 0x20) != 0;             /* B     */
		if (pad & 0x04) paddles[0] = 0;         /* Left  */
		if (pad & 0x08) paddles[0] = 255;       /* Right */
		if (pad & 0x01) paddles[1] = 0;         /* Up    */
		if (pad & 0x02) paddles[1] = 255;       /* Down  */
		for (int i = 0; i < nk; i++) {
			if (keys[i] == 0x50) paddles[0] = 0;      /* Left  */
			if (keys[i] == 0x4F) paddles[0] = 255;    /* Right */
			if (keys[i] == 0x52) paddles[1] = 0;      /* Up    */
			if (keys[i] == 0x51) paddles[1] = 255;    /* Down  */
			if (keys[i] == 0x2C) b0 = 1;              /* Space */
		}
		paddles[2] = paddles[0];
		paddles[3] = paddles[1];
		pom2_h3_paddles(paddles, b0, b1);

		if (emu_esc_hold()) break;
	}

	emu_disk_register(0);   /* callback смотрит на статику порта — снять */
	pom2_h3_shutdown();
	emu_audio_stop();
	fb_clear();
	fb_flush();
	emulog_end(iie ? "A2E" : "A2P");
}
