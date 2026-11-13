/*
	Floppy image extractor
	Copyright (c) NOP, nnop@newmail.ru
*/

//#define FDD1_DEBUG 1

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


static void fdd_code_fm(byte b, byte*r)
{
	r[0] = (b >> 1) | 0xAA;
	r[1] = b | 0xAA;
}

static void fdd_decode_fm(const byte*r, byte*b)
{
	*b = (r[1] & 0x55) | ((r[0] &0x55) << 1);
}

static byte fdd_check_sum(const byte*b, int n)
{
	register byte r = 0;
	for (;n; n--, b++) r^=*b;
	return r;
}

static int next_sector(const unsigned char*TrackData, int TrackLen, int ofs, int *nrot)
{
	static const byte hdr[] = {0xD5, 0xAA, 0x96};
	int n = 0, i, off0 = -1, i0;
	for (i = 0; i < TrackLen; ++i, ++ofs) {
		if (ofs == TrackLen) { ofs = 0; ++*nrot; }
//		dprintf("compare track[%i]=%02X and %02X\n", ofs, TrackData[ofs], hdr[n]);
		if (TrackData[ofs] != hdr[n]) {
			if (n) { ofs = off0; i = i0; }
			n = 0;
		} else {
			if (!n) { off0 = ofs; i0 = i; }
			++n;
			if (n == sizeof(hdr)) return off0;
		}
	}
	return -1;
}


static int next_data(const unsigned char*TrackData, int TrackLen, int maxl, int ofs, int *nrot)
{
	static const byte hdr[] = {0xD5, 0xAA, 0xAD};
	int n = 0, i, off0 = -1, i0;
	for (i = 0; i < TrackLen; ++i, ++ofs) {
		if (ofs == TrackLen) { ofs = 0; ++*nrot; }
		if (TrackData[ofs] != hdr[n]) {
			if (n) { ofs = off0; i = i0; }
			n = 0;
		} else {
			if (!n) { off0 = ofs; i0 = i; }
			++n;
			if (n == sizeof(hdr)) return off0;
		}
	}
	return -1;
}


static int decode_ts(const byte*data, byte vtscs[4])
{
	int i;
	for (i = 0; i < 4; ++i, data += 2) {
		fdd_decode_fm(data, vtscs + i);
	}
	if (fdd_check_sum(vtscs, 3) != vtscs[3]) return -1;
	return 0;
}


static const byte CodeTabl[0x40]={
	0x96,0x97,0x9A,0x9B,0x9D,0x9E,0x9F,0xA6,0xA7,0xAB,0xAC,0xAD,0xAE,0xAF,0xB2,0xB3,
	0xB4,0xB5,0xB6,0xB7,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,0xCB,0xCD,0xCE,0xCF,0xD3,
	0xD6,0xD7,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,0xE5,0xE6,0xE7,0xE9,0xEA,0xEB,0xEC,
	0xED,0xEE,0xEF,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF
};

static const byte DecodeTabl[0x80]={
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x02,0x03,0x00,0x04,0x05,0x06,
	0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x08,0x00,0x00,0x00,0x09,0x0A,0x0B,0x0C,0x0D,
	0x00,0x00,0x0E,0x0F,0x10,0x11,0x12,0x13,0x00,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1B,0x00,0x1C,0x1D,0x1E,
	0x00,0x00,0x00,0x1F,0x00,0x00,0x20,0x21,0x00,0x22,0x23,0x24,0x25,0x26,0x27,0x28,
	0x00,0x00,0x00,0x00,0x00,0x29,0x2A,0x2B,0x00,0x2C,0x2D,0x2E,0x2F,0x30,0x31,0x32,
	0x00,0x00,0x33,0x34,0x35,0x36,0x37,0x38,0x00,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F
};

static const byte ren[]={ // dos sector -> file order
	0x00,0x07,0x0E,0x06,0x0D,0x05,0x0C,0x04,
	0x0B,0x03,0x0A,0x02,0x09,0x01,0x08,0x0F
};

static const byte ren1[]={ // file order -> dos sector
	0x00,0x0D,0x0B,0x09,0x07,0x05,0x03,0x01,
	0x0E,0x0C,0x0A,0x08,0x06,0x04,0x02,0x0F
};


static const byte pren[]={// dos sector -> file order
	0x00,0x08,0x01,0x09,0x02,0x0A,0x03,0x0B,
	0x04,0x0C,0x05,0x0D,0x06,0x0E,0x07,0x0F
};

static const byte pren1[]={
	0x00,0x02,0x04,0x06,0x08,0x0A,0x0C,0x0E,
	0x01,0x03,0x05,0x07,0x09,0x0B,0x0D,0x0F
};


static void fdd_code_mfm(const byte*b, byte*d)
{

	memset(d, 0, 0x157);

	__asm {
		pushad
		mov	esi, b
		mov	edi, d
		mov	ebx, 2h
	l2:	mov	ecx, 55h
	l1:	dec	bl
		mov	al, [esi+ebx]
		shr	al, 1
		rcl	byte ptr [edi+ecx], 1
		shr	al, 1
		rcl	byte ptr [edi+ecx], 1
		mov	byte ptr [edi+56h+ebx], al
		dec	ecx
		jns	l1
		or	ebx, ebx
		jne	l2


		xor	al, al
		xor	ecx, ecx
		xor	ebx, ebx
	l4:	mov	ah, [edi+ecx]
		mov	bl, ah
		xor	bl, al
		mov	al, ah
		mov	bl, CodeTabl[ebx]
		mov	[edi+ecx], bl
		inc	ecx
		cmp	ecx, 156h
		jne	l4
		mov	bl, al
		mov	bl, CodeTabl[ebx]
		mov	[edi+ecx], bl
		popad
	}
}


static int fdd_decode_mfm(byte*s, byte*d)
{
	int res = 0;
	memset(d, 0, 0x100);
	__asm {
		pushad
		mov	esi, s
		mov	edi, d
		xor	eax, eax
		xor	ebx, ebx
		xor	ecx, ecx
l1:		mov	bl, [esi + ecx]
		and	bl, 7Fh
		mov	bl, DecodeTabl[ebx]
		mov	ah, bl
		xor	ah, al
		mov	[esi + ecx], ah
		mov	al, ah
		inc	ecx
		cmp	ecx, 156h
		jne	l1
		mov	bl, al
		mov	bl, CodeTabl[ebx]
		cmp	[esi + ecx], bl
		je	cs_ok
		mov	res, -1
		jmp	fin

cs_ok:		xor	ebx, ebx
l2:		xor	ecx, ecx
l3:		mov	al, [esi + 56h + ebx]
		shr	byte ptr [esi + ecx], 1
		rcl	al, 1
		shr	byte ptr [esi + ecx], 1
		rcl	al, 1
		mov	[edi + ebx], al
		inc	bl
		jz	fin
		inc	cl
		cmp	cl, 56h
		jne	l3
		jmp	short l2
fin:
		popad
	}
	return res;
}


static int copy_sect(const unsigned char*TrackData, int TrackLen, byte*buf, int ofs, int len, int *nrot)
{
	int ninc = 0;;
	for (;len; --len, ++ofs, ++buf) {
		if (ofs == TrackLen) { ofs = 0; ++*nrot; ++ninc; if (ninc == 2) break;  }
		*buf = TrackData[ofs];
		if (!(*buf & 0x80)) { --buf; ++len; }
	}
	return ofs;
}

static void dump_buf(byte*data, int len)
{
	char buf[1024];
	char *p;
	const unsigned char *d = data;
	int n, ofs = 0;
	dprintf("dump(%i):\n",len);
	while (len) {
		const unsigned char *d1 = d;
		int m, m1;
		p = buf;
		n = 16;
		m = n;
		for (; n && len; --n, --len, ++d) {
			p += sprintf(p, "%02X ", *d);
		}
		m1 = n;
		for (; n; --n) {
			p += sprintf(p, "__ ");
		}
		for (; m > m1; --m, ++d1) {
			int c = *d1;
			if (c <' ') c = '.';
			p += sprintf(p, "%c", c);
		}
		for (; m; --m) {
			p += sprintf(p, " ");
		}
		*p = 0;
		dprintf("%04x: %s\n", ofs, buf);
		ofs += 16;
	}
}

static int fdd_extract_track(const unsigned char*TrackData, int TrackLen, FILE*out, int t)
{
	int sec_found[FDD_SECTOR_COUNT];
	int nfound  = 0, ofs, nrot = 0;
	int lt = 0;
	memset(sec_found, 0, sizeof(sec_found));
	for (ofs = next_sector(TrackData, TrackLen, 0, &nrot); ofs != -1 && nrot < 2; 
					ofs = next_sector(TrackData, TrackLen, ofs + 1, &nrot)) {
		byte vtscs[4];
		byte shdr[14];
		byte sbuf[343+6], buf[FDD_SECTOR_DATA_SIZE];
		int ofs1, r;
		dprintf("ofs = %i (%04X)\n", ofs, ofs);
		ofs = copy_sect(TrackData, TrackLen, shdr, ofs, 14, &nrot);
		if (decode_ts(shdr + 3, vtscs)) {
			fprintf(stderr, "invalid VTS checksum!\n");
			continue;
		}
		if (shdr[11] != 0xDE/* || shdr[12] != 0xAA*/) {
			fprintf(stderr, "invalid VTS tail %02X %02X!\n", shdr[11], shdr[12]);
			continue;
		}
		dprintf("vts: vol=%i track=%i sec=%i\n", vtscs[0], vtscs[1], vtscs[2]);
		ofs1 = next_data(TrackData, TrackLen, 20, ofs, &nrot);
		if (ofs1 == -1) {
			fprintf(stderr, "no sector data prefix!\n");
			continue;
		}
		ofs = ofs1;
		ofs = copy_sect(TrackData, TrackLen, sbuf, ofs, 343+6, &nrot);
		if (sbuf[0] != 0xD5 || sbuf[1] != 0xAA || sbuf[2] != 0xAD) {
			fprintf(stderr, "invalid sector data prefix!\n");
			ofs = ofs1 + 1;
			continue;
		}
		if (sbuf[343+3] != 0xDE/* || sbuf[343+4] != 0xAA*/) {
			fprintf(stderr, "invalid sector data tail %02X %02X!\n", sbuf[343+3], sbuf[343+4]);
			ofs = ofs1 + 1;
			continue;
		}
		if (vtscs[2] > FDD_SECTOR_COUNT) {
			dprintf("error: wrong sector number %i\n", vtscs[2]);
		}
		if (sec_found[vtscs[2]]) {
			dprintf("info: sector %i already saved\n", vtscs[2]);
			continue;
		}
		dump_buf(sbuf + 3, 0x157);
		r = fdd_decode_mfm(sbuf + 3, buf);
		if (r) {
			dprintf("decode_mfm = %i\n", r);
			continue;
		}
		
//		dump_buf(sbuf + 3, 0x157);
		dump_buf(buf, 256);
//		dprintf("buf: %x %x %x %x %x (%s)\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf);
		fseek(out, (vtscs[1]*FDD_SECTOR_COUNT + (t?pren[vtscs[2]]:ren[vtscs[2]])) *
			FDD_SECTOR_DATA_SIZE, SEEK_SET);
		if (fwrite(buf, 1, FDD_SECTOR_DATA_SIZE, out)!=FDD_SECTOR_DATA_SIZE) {
			dprintf("can't write sector!\n");
			return -1;
		}
		sec_found[vtscs[2]] = 1;
		++ nfound;
		lt = vtscs[1];
		if (nfound == FDD_SECTOR_COUNT && nrot > 1) break;
	}
	if (nfound != FDD_SECTOR_COUNT) {
		fprintf(stderr, "track %i: missing %i sector(s)\n", lt, FDD_SECTOR_COUNT - nfound);
	} else {
		printf("track %i: done\n", lt);
	}	
	return 0;
}


static int fdd_extract_image(FILE*in, FILE*out, int t)
{
	unsigned char tbuf[NIBBLE_TRACK_LEN];
	int trk = 0;
	int r = 0;
	for (trk = 0; trk < MAX_TRACK_COUNT; ++trk) {
		int nr;
		nr = fread(tbuf, 1, sizeof(tbuf), in);
		if (nr != sizeof(tbuf)) {
			dprintf("fdd_extract_image: track read returned %i bytes for track %i\n", nr, trk);
			break;
		}
		r = fdd_extract_track(tbuf, sizeof(tbuf), out, t);
		if (r) break;
	}
	printf("image: done\n");
	return r;
}

int main(int argc, const char*argv[])
{
	FILE*in, *out;
	int r;
	if (argc != 3) {
		fprintf(stderr, "usage: %s nib_in dsk_out\n", argv[0]);
		return 10;
	}
	in = fopen(argv[1], "rb");
	if (!in) {
		perror(argv[1]);
		return 9;
	}
	out = fopen(argv[2], "wb");
	if (!out) {
		perror(argv[2]);
		return 8;
	}
	r = fdd_extract_image(in, out, 0);
	fclose(out);
	fclose(in);
	return r;
}
