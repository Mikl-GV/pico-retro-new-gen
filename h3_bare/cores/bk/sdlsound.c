#include "defines.h"
#include "conf.h"
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <SDL/SDL_mutex.h>
#include <libintl.h>
#define _(String) gettext (String)

#define SOUND_BUFSIZE 1024
#define SAMPLE_SIZE 2

typedef struct {
	short * buf;
	int inptr, outptr;
} sound_buf_t;

static sound_buf_t sound_buf;
static int io_sound_chans;

static SDL_sem * sem;

static void callback(void * dummy, Uint8 * outbuf, int len)
{
        int len_samples = len / SAMPLE_SIZE / io_sound_chans;
        int normalized = sound_buf.outptr % SOUND_BUFSIZE;
        int prewrap = SOUND_BUFSIZE - normalized;
        if (prewrap > len_samples)
          prewrap = len_samples;
	memcpy(outbuf, &sound_buf.buf[normalized * io_sound_chans],
               prewrap * SAMPLE_SIZE * io_sound_chans);
        memcpy(outbuf + prewrap * SAMPLE_SIZE * io_sound_chans, &sound_buf.buf[0],
               (len_samples - prewrap) * SAMPLE_SIZE * io_sound_chans);
        sound_buf.outptr += len_samples;
	SDL_SemPost(sem);
}

static void sound_finish() {
	/* release the write thread so it can terminate */
	SDL_PauseAudio(1); 
	SDL_DestroySemaphore(sem);
}

void platform_sound_flush() {
	while (sound_buf.inptr - sound_buf.outptr > SOUND_BUFSIZE / 2) {
		SDL_SemWait(sem);
	}
}

void sound_write_sample(short val) {
	int i;
	if (sound_buf.buf == NULL)
	  return;
        for (i = 0; i < io_sound_chans; i++)
          sound_buf.buf[(sound_buf.inptr % SOUND_BUFSIZE) * io_sound_chans + i] = val;
        sound_buf.inptr++;
	if (sound_buf.inptr >= sound_buf.outptr + SOUND_BUFSIZE) {
		platform_sound_flush();
	}
}

void sound_discard() {
	sound_buf.outptr = sound_buf.inptr;
}

void platform_sound_init() {
	int iarg, i;

	if (fullspeed) {
		io_max_sound_age = SOUND_BUFSIZE;
		/* otherwise UINT_MAX */
	}

	fprintf(stderr, _("sound_init called\n"));

	if (-1 == SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		fprintf(stderr, _("Failed to initialize audio subsystem\n"));
	}

        SDL_AudioSpec desired = {
          .format = 16,
          .channels = 1,
          .freq = io_sound_freq,
          .samples = SOUND_BUFSIZE / 2,
          .callback = callback
        };
        SDL_AudioSpec obtained;

	if (-1 == SDL_OpenAudio(&desired, &obtained)) {
		fprintf(stderr, _("Failed to initialize sound, freq %d, %d samples\n"), io_sound_freq, SOUND_BUFSIZE);
		nflag = 0;
		return;
	}

        fprintf (stderr, "Got %d channels, %d Hz, %d samples\n",
                 obtained.channels, obtained.freq, obtained.samples);
        io_sound_freq = obtained.freq;
        io_sound_chans = obtained.channels;

	sem = SDL_CreateSemaphore(2);

        sound_buf.inptr = 0;
        sound_buf.outptr = 0;
        sound_buf.buf = malloc(SOUND_BUFSIZE * SAMPLE_SIZE * io_sound_chans);
	if (!sound_buf.buf) {
		fprintf(stderr, _("Failed to allocate sound buffers\n"));
		exit(1);
	}

	atexit(sound_finish);
	SDL_PauseAudio(0);
}
