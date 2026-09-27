#include "defines.h"
#include "SDL/SDL.h"
#include "scr.h"
#include <libintl.h>
#define _(String) gettext (String)

SDL_Surface * screen, *rl_screen;
flag_t fullscreen = 0;
int cur_shift = 0;
int cur_width = 0;	/* 0 = narrow, !0 = wide */
int horsize = 512, vertsize = 512;

/* Scan lines */
// The scan lines must be in 8-bit palettized form, as they have
// to be re-blitted to the screen with their new meaning if the
// palette changes.

unsigned char dirty[1024];
// There are 256 scan lines in each of 2 buffers
SDL_Surface * lines[512];

#define Black {0,0,0}
#define Red {255,0,0}
#define Green {0,255,0}
#define Blue {0,0,255}
#define Yellow {255,255,0}
#define Cyan {0,255,255}
#define Magenta {255,0,255}
#define Brown {128,0,0}
#define Violet {192,0,192}
#define DarkRed {192,0,0}
#define LightBlue {128,0,255}
#define White {255,255,255}

/*
 * The colors are ordered in the HW order, not in the "Basic" order.
 * To convert, switch colors 1 and 3.
 */
SDL_Color palettes[16][5] = {
{ Black, Blue, Green, Red, White },   /* BK0010 Colors */
{ Black, Yellow, Magenta, Red, White },
{ Black, Cyan, Blue, Magenta, White },
{ Black, Green, Cyan, Yellow, White },
{ Black, Magenta, Cyan, White, White },
{ Black, White, White, White, White },
{ Black, DarkRed, Brown, Red, White },
{ Black, Green, Cyan, Yellow, White },
{ Black, Violet, LightBlue, Magenta, White },
{ Black, Yellow, LightBlue, DarkRed, White },  /* Almost the same as the next one */
{ Black, Yellow, Violet, Red, White },
{ Black, Cyan, Yellow, Red, White },          /* CSI-DOS colors */
{ Black, Red, Green, Cyan, White },
{ Black, Cyan, Yellow, White, White },
{ Black, Yellow, Green, White, White },
{ Black, Cyan, Green, White, White },
};

unsigned scr_dirty = 0;

unsigned char active_palette, active_page;
unsigned char half_frame = 0;

// It is unlikely that we get a hardware surface, but one can hope
#define SCREEN_FLAGS    \
	(SDL_HWSURFACE|SDL_HWACCEL|SDL_ASYNCBLIT|SDL_ANYFORMAT|SDL_HWPALETTE|(fullscreen ? SDL_FULLSCREEN : SDL_RESIZABLE))

Uint16 horbase[513], vertbase[257];

/*
 * Set the pixel at (x, y) to the given value
 * NOTE: We never put pixels to the actual screen surface, no locking
 * is necessary.
 */
static inline void putpixels(SDL_Surface * s, int x, Uint32 pixel)
{
    Uint8 *p = (Uint8 *)s->pixels + horbase[x];
    int n = horbase[x+1] - horbase[x];
    do *p++ = pixel; while (--n);
}

static void Refresh_screen(void)
{ 
	SDL_Surface* tmp;
	tmp = SDL_DisplayFormat(screen);
	SDL_SoftStretch(tmp, NULL, rl_screen, NULL);
	SDL_FreeSurface(tmp);
	SDL_Flip(rl_screen);	
}

void Quit_SDL(void)
{ 
	if (screen) SDL_FreeSurface(screen);
	if (rl_screen) SDL_FreeSurface(rl_screen);
	SDL_Quit();
}

/*
 * Flushes a word into a statically defined scan line;
 * scrolling and line doubling are done during blitting. bufno is 0 or 1,
 * address is relative to the video buffer.
 */
int scr_write(int bufno, c_addr addr, d_word wrd)
{
	int offset, dest_x, dest_y;
	SDL_Surface * s;
	if (!cflag) {
		int i;
		offset = addr * 8;
		dest_y = offset / 512;
		dest_x = offset & 511;
		i = 256*bufno+dest_y;
		s = lines[i];
		if (!dirty[i]) {
			dirty[i] = 1;
			scr_dirty++;
		}
		for (i = 16; i; i--, dest_x++, wrd >>= 1) {
			putpixels(s, dest_x, (wrd & 1) << 2);
		}
	} else {
		int i;
		offset = addr * 4;
		dest_y = offset / 256;
		dest_x = offset & 255;
		i = 256*bufno+dest_y;
		s = lines[i];
		if (!dirty[i]) {
			dirty[i] = 1;
			scr_dirty++;
		}
		for(i = 8; i; i--, dest_x++, wrd >>= 2) {
			putpixels(s, dest_x, wrd & 3);
		}
	}
	return OK;
}

/* In the bk_icon array, each word is in RGBA format;
 * A = 0 is transparent, A = 255 is opaque
 */

extern Uint32 bk_icon_width, bk_icon_height, bk_icon[];

static Uint8 *
compute_icon_mask() {
        int i;
	static Uint8 * mask = 0;
	if (mask) return mask;

	mask = calloc(1, bk_icon_width*bk_icon_height/8);
	if (!mask) return NULL;

        for ( i=0; i<bk_icon_width*bk_icon_height; ) {
                /* More than half opaque is opaque */
                if ( (bk_icon[i] & 0xFF) >= 128)
                        mask[i/8] |= 0x01;
                ++i;
                if ( (i%8) != 0 )
                        mask[i/8] <<= 1;
        }
        return mask;
}

/* BK-0010 screen refresh - no palettes */
static void scr_refresh_bk0010(unsigned shift, unsigned full);

/* BK-0011 screen refresh - single size, lossy interlacing */
static void scr_refresh_bk0011_1(unsigned shift, unsigned full);

/* BK-0011 screen refresh - double size, exact interlacing */
static void scr_refresh_bk0011_2(unsigned shift, unsigned full);

void (*scr_refresh)(unsigned, unsigned);

void
setup_bases() {
    int i;
    int horpix = (cflag ? 256 : 512);
    for (i = 0; i <= horpix; i++) {
	horbase[i] = i * horsize / horpix;
    }
    for (i = 0; i <= 256; i++) {
	vertbase[i] = i * vertsize / 256;
    }
}

void
bk_scr_init() {

    static char init_done = 0;
    int i;
    Uint32 bpp, rmask, gmask, bmask;
    if (init_done) return;
    init_done = 1;

    SDL_WM_SetIcon(SDL_CreateRGBSurfaceFrom(bk_icon, bk_icon_width,
			bk_icon_height, 32, bk_icon_width*4,
			0xff000000, 0xff0000, 0xff00, 0xff),
	compute_icon_mask());
	
	rl_screen = SDL_SetVideoMode(480, 272, 16, SCREEN_FLAGS);
    if (rl_screen == NULL) {
        fprintf(stderr, _("Couldn't set up video: %s\n"),
                        SDL_GetError());
        exit(1);
    }
	screen = SDL_CreateRGBSurface(SDL_SWSURFACE, horsize, vertsize, 32, 0, 0, 0, 0);

    /* Translation disabled because of an SDL bug */
    if (bkmodel == 0) {
    	SDL_WM_SetCaption("BK-0010", "BK-0010"); }
    else {
    	SDL_WM_SetCaption("BK-0011M", "BK-0011M"); 
    }

    scr_common_init();

    if (screen->format->BitsPerPixel == 8) {
	SDL_SetColors(screen, palettes[active_palette], 0, 5);
    }

    /* Create palettized surfaces for scan lines for the highest possible
     * resolution (so far width 1024). 
     */
    for (i = 0; i < 512; i++) {
	lines[i] = SDL_CreateRGBSurface(SDL_SWSURFACE, 1024, 1,
		8, 0, 0, 0, 0);
	if (!lines[i]) {
		fprintf(stderr, "Couldn't set up video: %s\n",
			SDL_GetError());
		exit(1);
	}
	SDL_SetPalette(lines[i], SDL_LOGPAL,
		palettes[active_palette], 0, 5);
	dirty[i] = 0;
    }
    SDL_ShowCursor(SDL_DISABLE);
    scr_refresh = bkmodel ?
	vertsize != 256  ? scr_refresh_bk0011_2 :
		scr_refresh_bk0011_1 :
	scr_refresh_bk0010;
    if (horsize < 512) {
	    cflag = 1;
    }
    setup_bases();
}

/*
 * Switches to the new screen size, or, if called with (0, 0),
 * switches between color and B/W.
 */
scr_switch(int hsize, int vsize) {
    int i;
    int old_hor = horsize, old_vert = vertsize;
    // It is ok to go beyond 1024x1024, but a smaller screen is
    // returned to no less than 512x256
    char restore = 0;
    if (hsize | vsize) {
	horsize = hsize;
	vertsize = vsize;
	/* Allow horizontal size down to 256 in color mode */
	if (horsize < 512) {
	       if (cflag && horsize < 256) {
		       restore = 1;
		       horsize = 256;
	       } else if (!cflag) {
		       restore = 1;
		       horsize = 512;
	       }
	}
	if (vertsize < 256) { restore = 1; vertsize = 256; }
	if (horsize > 1024) horsize = 1024;
	if (vertsize > 1024) vertsize = 1024;
    } else {
	cflag = !cflag;
	if (!cflag && horsize < 512) {
		restore = 1;
		horsize = 512;
	}
    }

    if (restore || old_hor != horsize || old_vert != vertsize) { 
	screen = SDL_SetVideoMode(horsize, vertsize, 0, SCREEN_FLAGS);	
    }

    setup_bases();   

    /* Re-flush both video pages */
    for (i = 0; i < 040000; i+=2) {
	scr_write(0, i, ram[1][i >> 1]);
	scr_write(1, i, ram[7][i >> 1]);
    }
    if (bkmodel) scr_refresh = vertsize != 256 ? scr_refresh_bk0011_2 : scr_refresh_bk0011_1;

}

/*
 * Screen refresh for BK-0010 does not need to know about buffers or
 * palettes.
 */
static void
scr_refresh_bk0010(unsigned shift, unsigned full) 
{
	static SDL_Surface* tmp;
	int blit_all = shift != cur_shift || cur_width != full;
	int i;

	/* If more than a few lines changed, no point
	 * doing separate UpdateRect's for each line.
	 */
	int update_all = blit_all || scr_dirty >= 4;
	int width = horsize;
	int nlines = full ? 256 : 64;
	static SDL_Rect srcrect = {0, 0, 0, 1};
	static SDL_Rect dstrect = {0, 0, 0, 0};
	srcrect.w = width;
	for (i = 0; i < nlines; i++) {
		int line = (i + shift) & 0xFF;
		SDL_Surface * l = lines[line];
		dstrect.y = vertbase[i];
		int vertstretch = vertbase[i+1]-vertbase[i];
		if (dirty[line] | blit_all) {
			int n = vertstretch-1;
			do {
				SDL_BlitSurface(l, &srcrect, screen, &dstrect);
				dstrect.y++;
			} while (n--);
			if (!update_all) {
				Refresh_screen();
				//SDL_UpdateRect(screen, 0, vertbase[i], width, vertstretch);
			}
		}
	}
	// Only the first 256 lines are used
	memset(dirty, 0, 256);
	if (!full && cur_width) {
		/* Black out the low 3/4 of the screen */
		dstrect.x = 0; dstrect.y = vertbase[64];
		dstrect.w = width;
		dstrect.h = vertbase[256]-vertbase[64];
		SDL_FillRect(screen, &dstrect, 0);
	}
	cur_width = full;
	cur_shift = shift;
	if (update_all) 
	{
		Refresh_screen();
	}
	scr_dirty = 0;
}

/*
 * For single-sized BK-0011 screen we do crude interlacing:
 * even lines are taken from the first half-frame,
 * odd lines - from the second.
 */
static void
scr_refresh_bk0011_1(unsigned shift, unsigned full) {
	int blit_all = change_req || shift != cur_shift || cur_width != full;
	int i;

	/* If more than a few lines changed, no point
	 * doing separate UpdateRect's for each line.
	 */
	int update_all = blit_all || scr_dirty >= 4;
	int width = horsize;
	int do_palette = change_req || shift != cur_shift;
	int nlines = full ? 256 : 64;
	static SDL_Rect srcrect = {0, 0, 0, 1};
	static SDL_Rect dstrect = {0, 0, 0, 0};
	srcrect.w = width;
	for (i = 0; i < nlines; i++) {
		int line = (i + shift) & 0xFF;
		unsigned physline = 256*req_page[2*i+(i&1)] + line;
		SDL_Surface * l = lines[physline];
		dstrect.y = i;
		if (dirty[physline] | blit_all) {
			if (do_palette) {
				SDL_SetPalette(l, SDL_LOGPAL,
					palettes[req_palette[2*i+(i&1)]], 0, 5);
			}
			SDL_BlitSurface(l, &srcrect, screen, &dstrect);
			if (!update_all) {
				//SDL_UpdateRect(screen, 0, dstrect.y, width, 1);
				Refresh_screen();
			}
		}
	}
	memset(dirty, 0, 1024);
	if (!full && cur_width) {
		/* Black out the low 3/4 of the screen */
		dstrect.x = 0; dstrect.y = 64;
		dstrect.w = width;
		dstrect.h = 192;
		SDL_FillRect(screen, &dstrect, 0);
	}
	cur_width = full;
	cur_shift = shift;
	if (update_all) {
		Refresh_screen();
	}
	scr_dirty = 0;
	change_req >>= 1;
}

static void
scr_refresh_bk0011_2(unsigned shift, unsigned full) {
	int blit_all = change_req || shift != cur_shift || cur_width != full;
	int i;

	/* If more than a few lines changed, no point
	 * doing separate UpdateRect's for each line.
	 */
	int update_all = blit_all || scr_dirty >= 4;
	int width = horsize;
	int do_palette = change_req || shift != cur_shift;
	int nlines = full ? vertsize : vertbase[64];
	static SDL_Rect srcrect = {0, 0, 512, 1};
	srcrect.w = width;
	static SDL_Rect dstrect = {0, 0, 0, 0};
	int j;
	for (i = 0, j = 0; i < nlines; i++) {
		// The next line is the reverse mapping of vertbase
		if (vertbase[j+1] == i) j++;
		int line = (j + shift) & 0xFF;
		unsigned physline = 256*req_page[2*j+(i&1)]+line;
		SDL_Surface * l = lines[physline];
		dstrect.y = i;
		if (dirty[physline] | blit_all) {
			if (do_palette) {
				SDL_SetPalette(l, SDL_LOGPAL,
					palettes[req_palette[2*j+(i&1)]], 0, 5);
			}
			SDL_BlitSurface(l, &srcrect, screen, &dstrect);
			if (!update_all) {
				//SDL_UpdateRect(screen, 0, dstrect.y, width, 1);
				Refresh_screen();
			}
		}
	}
	memset(dirty, 0, vertsize);
	if (!full && cur_width) {
		/* Black out the low 3/4 of the screen */
		dstrect.x = 0; dstrect.y = vertbase[64];
		dstrect.w = width;
		dstrect.h = vertsize - dstrect.y;
		SDL_FillRect(screen, &dstrect, 0);
	}
	cur_width = full;
	cur_shift = shift;
	if (update_all) {
		Refresh_screen();
	}
	scr_dirty = 0;
	change_req >>= 1;
}

void scr_flush() {
	if (scr_dirty || change_req)
		scr_refresh((tty_scroll - 0330) & 0377, tty_scroll & 01000);
}


void maybe_scr_flush() {
       static Uint32 last_screen_update = 0;
       /* In full speed, update every 40 real ms */

       if (fullspeed) {
               Uint32 cur_sdl_ticks = SDL_GetTicks();
               if (cur_sdl_ticks - last_screen_update >= 40) {
                       last_screen_update = cur_sdl_ticks;
                       scr_flush();
               }
       } else {
               scr_flush();
       }
}
