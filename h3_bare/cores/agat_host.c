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

#include "agat/src/sysconf.h"
#include "agat/src/runmgr.h"
#include "agat/src/runmgrint.h"
#include "agat/src/cpu/cpuint.h"
#include "agat/src/video/videoint.h"

/* Объекты ядра проходят agat_rename.sh: все внешние имена получают префикс
   agat_, поэтому host зовёт их по именам с префиксом. */
struct SYS_RUN_STATE* agat_get_state(void);
int agat_video_expand(unsigned short*fb, int fb_w, int fb_h, int scale);
int agat_reset_config(struct SYSCONFIG*c, int systype);
void agat_set_disk_image(const void*data, unsigned size, const char*name);
struct SYS_RUN_STATE* agat_init_system_state(struct SYSCONFIG*c, void*hmain, const char*name);
int agat_free_system_state(struct SYS_RUN_STATE*sr);
int agat_cpu_run_cycles(struct CPU_STATE*cs, long long cycles);
void agat_video_repaint_screen(struct VIDEO_STATE*vs);

#define AGAT_FRAME_W 640
#define AGAT_FRAME_H 400
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
	if (rom_name) {
		int i;

		for (i = 0; i < NCONFTYPES; i++) {
			struct SLOTCONFIG *sc = &cfg.slots[i];

			if (sc->dev_type == DEV_FDD_SHUGART || sc->dev_type == DEV_FDD_TEAC) {
				strncpy(sc->cfgstr[CFG_STR_DRV_IMAGE1], rom_name, CFGSTRLEN - 1);
				sc->cfgstr[CFG_STR_DRV_IMAGE1][CFGSTRLEN - 1] = 0;
				break;
			}
		}
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

	emu_set_period_us(AGAT_FRAME_US);
	emu_throttle_reset();
	emu_esc_hold_reset();

	for (;;) {
		agat_cpu_run_cycles(cs, AGAT_CYCLES_PER_FRAME);
		agat_video_repaint_screen(vs);
		agat_video_expand(frame, AGAT_FRAME_W, AGAT_FRAME_H, 1);
		emu_scale_rgb565_src_aspect(frame, AGAT_FRAME_W, AGAT_FRAME_H,
		                            AGAT_FRAME_W, 4, 3);
		fb_flush();
		emu_throttle();
		if (emu_esc_hold()) break;
	}

	agat_free_system_state(sr);
	emu_audio_stop();
	fb_clear();
	fb_flush();
}
