#include "defines.h"
#include "intl.h"

void covox_init() {
	covox_val = 0;
	covox_age = ~0;
}

int covox_read(c_addr addr, d_word *word)
{
	*word = 0;	/* pulldown */
	return OK;
}

int covox_write(c_addr addr, d_word word)
{
	covox_val = word & 0xFF;
	covox_age = 0;
	return OK;
}

int covox_bwrite(c_addr addr, d_byte byte) {
	d_word offset = addr & 1;
	if (offset == 0) {
		covox_val = byte;
	} else {
		covox_val = 0;
	}
	covox_age = 0;
	return OK;
}

