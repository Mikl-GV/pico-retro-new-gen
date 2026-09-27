#include "defines.h"
#include "conf.h"
#include <stdio.h>
#include "intl.h"

#define MAX_SOUND_AGE	~0	/* always play */ 

unsigned io_sound_freq = 44100;

/* Called after every instruction */
void sound_flush() {
	if (fullspeed && io_sound_age >= io_max_sound_age && covox_age >= io_max_sound_age) {
		platform_sound_flush();
		return;
	}
	while (ticks >= io_sound_count) {
		if (io_sound_age < 1000)
			sound_write_sample (io_sound_val + covox_val << 4);
		else
			sound_write_sample ((covox_val << 4) + synth_next());
		io_sound_age++;
		if (io_sound_age == io_max_sound_age) {
			platform_sound_flush();
		}
		covox_age++;
		io_sound_count += io_sound_pace;
	}
}

void sound_init() {
	static int init_done = 0;
	if (!nflag)
		return;
	if (init_done) {
		io_sound_age = io_max_sound_age;
		sound_discard();
		return;
	}

	platform_sound_init();

	io_sound_pace = TICK_RATE/(io_sound_freq + 0.0);
	io_max_sound_age = MAX_SOUND_AGE;
	io_sound_age = MAX_SOUND_AGE;	/* in io_sound_pace's since last change */
	init_done = 1;
}
