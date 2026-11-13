/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	keyb - keyboard emulation
*/

#ifndef AG_KEYB_H
#define AG_KEYB_H

enum {
	KEYMAP_NORMAL,
	KEYMAP_SHIFT,
	KEYMAP_CTRL,
	KEYMAP_EXT,

	N_KEYMAPS
};

enum {
	LAYOUT_LAT,
	LAYOUT_RUS,

	N_LAYOUTS
};

struct KEYMAP
{
	unsigned char data[N_LAYOUTS][N_KEYMAPS][0x80];
};

extern struct KEYMAP keymap_default;

int decode_key(unsigned long keydata, struct KEYMAP*map, int layout);

int keymap_load(const char*fname, struct KEYMAP*map);

#endif /* AG_KEYB_H */
