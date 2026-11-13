/*
	Floppy image creator
	Copyright (c) NOP, nnop@newmail.ru
*/

#define FDD1_DEBUG 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned long  dword;


#define MAX_TRACK_COUNT 37

#define FDD_TRACK_COUNT 34
#define FDD_SECTOR_COUNT 16
#define FDD_SECTOR_DATA_SIZE 256
#define NIBBLE_TRACK_LEN 6656

#ifdef FDD1_DEBUG
#define dprintf(...) printf(__VA_ARGS__)
#define dputs(s) puts(s)
#else
#define dprintf(...)
#define dputs(s)
#endif

int fdd_generate_image(int sz, int dt, FILE*out)
{
	int nt, ns;
	int tlen, fill;
	int raw_ss;
	int t;
	void*buf;
	switch (sz) {
	case 0:
		nt = 34;
		ns = 16;
		raw_ss = 0x1A0;
		break;
	case 1:
		nt = 160;
		ns = 21;
		raw_ss = 0x11A;
		break;
	}
	switch (dt) {
	case 0:
		tlen = ns * 256;
		fill = 0;
		break;
	case 1:
		tlen = ns * raw_ss;
		fill = 0x00;
		break;
	case 2:
		tlen = 6464 * 2;
		fill = 0;
		break;
	}
	buf = malloc(tlen);
	if (!buf) return 20;
	memset(buf, fill, tlen);
	for (t = 0; t < nt; ++t) {
		int r;
		r = fwrite(buf, 1, tlen, out);
		if (r != tlen) {
			fprintf(stderr, "error: failed to write track\n");
			free(buf);
			return 21;
		}
	}
	free(buf);
	return 0;
}

int main(int argc, const char*argv[])
{
	FILE*in, *out;
	int sz = -1, dt = -1;
	int r;
	if (argc != 4) {
		fprintf(stderr, "usage: %s size type fname\n\tsize:\t140, 800\n\ttype:\tdsk, nib, aim\n", argv[0]);
		return 10;
	}
	if (!_stricmp(argv[1], "136")) sz = 0;
	else if (!_stricmp(argv[1], "140")) sz = 0;
	else if (!_stricmp(argv[1], "800")) sz = 1;
	else { fprintf(stderr, "error: invalid size specifier %s\n", argv[1]); return 9; }

	if (!_stricmp(argv[2], "dsk")) dt = 0;
	else if (!_stricmp(argv[2], "nib")) dt = 1;
	else if (!_stricmp(argv[2], "aim")) dt = 2;
	else { fprintf(stderr, "error: invalid image type specifier %s\n", argv[2]); return 8; }

	out = fopen(argv[3], "wb");
	if (!out) {
		perror(argv[2]);
		return 7;
	}
	r = fdd_generate_image(sz, dt, out);
	fclose(out);
	if (r) fprintf(stderr, "%s: failed: %i\n", argv[3], r);
	else printf("%s: done\n", argv[3]);
	return r;
}
