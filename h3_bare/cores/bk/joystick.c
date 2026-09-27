#include "defines.h"

int joystick_read(c_addr addr, d_word *word) {
        *word = platform_joystick_get_state();
	return OK;
}

void
joystick_init() {
	platform_joystick_init();
}

int joystick_write(c_addr addr, d_word word) {
	return OK;
}

int joystick_bwrite(c_addr addr, d_byte byte) {
	return OK;
}
