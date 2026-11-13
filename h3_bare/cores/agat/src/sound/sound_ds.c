/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	sound_ds - sound output via DirectSound library [incomplete]
*/

#include "soundint.h"
#include "debug.h"
#include <dsound.h>
#include <assert.h>

//#define DEBUG_SOUND

#pragma comment(lib,"dsound")

//#define SFREQ 22050
//#define BUF_NSMP 300
//#define SDELAY (BUF_NSMP*1000/SFREQ)
#define INIT_STR(s) { memset(&(s),0,sizeof(s)); (s).dwSize=sizeof(s); }

//#define SOUND_BUFFER_SIZE BUF_NSMP

typedef short sample_t;

#define MIN_SAMPLE -32000
#define MAX_SAMPLE 32000
#define XOR_SAMPLE (MAX_SAMPLE ^ MIN_SAMPLE)
#define MID_SAMPLE 0

#ifdef DEBUG_SOUND
#define Sprintf(x) printf x
#else
#define Sprintf(x)
#endif

struct SNDPACK
{
	sample_t val;
	int count;
};

#define BUF_SIZE 1000
#define SMPBUF_SIZE 256

struct DS_DATA
{
	struct SYS_RUN_STATE*sr;
	IDirectSound*sound;
	IDirectSoundBuffer*buffer;
	IDirectSoundNotify*notify;
	DSBPOSITIONNOTIFY  positions[2];
	int freq;
	int buflen, bufsmp;

	int cur_val;
	int cursmp, prevsmp;
	double  flen, fsum; // fractional length and summ of weighted sample values
	long	prev_tick;

	HWND video_w;

	int playing;
	int part;
	HANDLE hsem;
	HANDLE hthread;
	DWORD tid;
	int term;


	sample_t smpbuf[SMPBUF_SIZE];
	int smpbufofs;

	struct SNDPACK buf[BUF_SIZE];
	int wpos, rpos;
};



static void memsetw(void*p, short val, int nb)
{
	short*pw = p;
	for (; nb >= sizeof(val); nb -= sizeof(val)) *pw++ = val;
}

static void fill_buffer(struct DS_DATA*p, int ofs, int nb, sample_t val)
{
	LPVOID pt[2];
	DWORD sz[2];
	if (!nb) return;
	if (IDirectSoundBuffer_Lock(p->buffer,ofs,nb,pt,sz,pt+1,sz+1,0)!=DS_OK) {
		Sprintf(("fill: ds lock failed!\n"));
		return;
	}
	if (pt[0]) memsetw(pt[0], val, sz[0]);
	if (pt[1]) memsetw(pt[1], val, sz[1]);
	if (DS_FAILED(IDirectSoundBuffer_Unlock(p->buffer,pt[0],sz[0],pt[1],sz[1]),TEXT("unlocking buffer"))) {
		Sprintf(("fill: ds unlock failed!\n"));
		return;
	}
}

static void write_buffer(struct DS_DATA*p, int ofs, int nb, const sample_t*v)
{
	LPVOID pt[2];
	DWORD sz[2];
	if (!nb) return;
	if (IDirectSoundBuffer_Lock(p->buffer,ofs,nb,pt,sz,pt+1,sz+1,0)!=DS_OK) {
		Sprintf(("fill: ds lock failed!\n"));
		return;
	}
	if (pt[0]) memcpy(pt[0], v, sz[0]);
	v = (const sample_t*)((const char*)v + sz[0]);
	if (pt[1]) memcpy(pt[1], v, sz[1]);
	if (DS_FAILED(IDirectSoundBuffer_Unlock(p->buffer,pt[0],sz[0],pt[1],sz[1]),TEXT("unlocking buffer"))) {
		Sprintf(("fill: ds unlock failed!\n"));
		return;
	}
}

static void clear_buffer(struct DS_DATA*p, sample_t val)
{
	static LPVOID pt[2];
	static DWORD sz[2];
	if (DS_FAILED(IDirectSoundBuffer_Lock(p->buffer,0,0,pt,sz,pt+1,sz+1,DSBLOCK_ENTIREBUFFER),TEXT("locking buffer"))) {
		Sprintf(("clear: ds lock failed!\n"));
		return;
	}
	if (pt[0]) memsetw(pt[0],val,sz[0]);
	if (pt[1]) memsetw(pt[1],val,sz[1]);
	if (DS_FAILED(IDirectSoundBuffer_Unlock(p->buffer,pt[0],sz[0],pt[1],sz[1]),TEXT("unlocking buffer"))) {
		Sprintf(("clear: ds unlock failed!\n"));
		return;
	}
}


static int flushsmpbuf(struct DS_DATA*p, int cur_ofs)
{
	int r;
	if (!p->smpbufofs) return cur_ofs;
	write_buffer(p, cur_ofs, p->smpbufofs * sizeof(sample_t), p->smpbuf);
	cur_ofs += p->smpbufofs * sizeof(sample_t);
	if (cur_ofs > p->buflen) cur_ofs -= p->buflen;
	p->smpbufofs = 0;
	return cur_ofs;
}

static int addsmpbuf(struct DS_DATA*p, struct SNDPACK*sp)
{
	int n = sp->count;
	if (n + p->smpbufofs > SMPBUF_SIZE) n = SMPBUF_SIZE - p->smpbufofs;
	if (n) memsetw(p->smpbuf + p->smpbufofs, sp->val, n * sizeof(sample_t));
	p->smpbufofs += n;
	sp->count -= n;
	return n;
}


static DWORD CALLBACK sound_proc(struct DS_DATA*p)
{
	int timeremains = INFINITE;
	int cur_ofs = 0;
	struct SNDPACK pack;
	while (!p->term) {
		DWORD nb, t0;
		int r;
		if ((timeremains != INFINITE) && timeremains < 10) timeremains = 10;
		Sprintf(("snd: waiting for packet for %i msecs\n", timeremains));
#ifdef DEBUG_SOUND
		t0 = GetTickCount();
#endif
		r = WaitForSingleObject(p->hsem, timeremains);
		if (p->term) break;
#ifdef DEBUG_SOUND
		Sprintf(("snd: wait time: %i msecs\n", GetTickCount() - t0));
		Sprintf(("snd: wait result: %s\n", (r == WAIT_TIMEOUT)?"timeout":"done"));
#endif
		if (r == WAIT_TIMEOUT) {
			cur_ofs = flushsmpbuf(p, cur_ofs);
			if (!p->playing && cur_ofs <= p->buflen / 4) {
				if (timeremains < 300) {
					timeremains *= 2;
					continue;
				}
			}
			if (!p->playing && cur_ofs) {
				timeremains = cur_ofs * 1000 / p->freq / sizeof(sample_t);
				IDirectSoundBuffer_SetCurrentPosition(p->buffer, 0);
				Sprintf(("snd: playing remaining buffer for %i msecs after timeout\n", timeremains));
				IDirectSoundBuffer_Play(p->buffer, 0, 0, DSBPLAY_LOOPING);
				p->playing = 1;
				continue;
			}
			Sprintf(("snd: stopping playing\n"));
			IDirectSoundBuffer_Stop(p->buffer);
			timeremains = INFINITE;
			cur_ofs = 0;
			IDirectSoundBuffer_SetCurrentPosition(p->buffer, 0);
			p->playing = 0;
			p->prevsmp = -1;
		} else {
			int ofs;
			int nb;
			int rr = p->rpos;
			DWORD pl_cur, wr_cur;
			pack = p->buf[p->rpos];
			++rr;
			if (rr == BUF_SIZE) rr = 0;
			p->rpos = rr;
//			ReadFile(p->pipe[0], &pack, sizeof(pack), &nb, NULL);
			nb = pack.count * sizeof(sample_t);
			Sprintf(("snd: pack: count = %i, sample = %x\n", pack.count, pack.val&0xFFFF));
			if (pack.count == 1) addsmpbuf(p, &pack);
			if (!pack.count) continue;
			ofs = cur_ofs;
			if (!p->playing) {
				pl_cur = 0;
				if (!cur_ofs && !p->smpbufofs) {
					clear_buffer(p, pack.val);
					Sprintf(("snd: clearing whole stopped buffer with value %x\n", pack.val));
				}	
			} else {
				IDirectSoundBuffer_GetCurrentPosition(p->buffer, &pl_cur, &wr_cur);
//				printf("snd: current buffer position: pl %i, wr %i, cur_ofs %i\n", pl_cur, wr_cur, cur_ofs);
				pl_cur = wr_cur;
				if (pl_cur < p->buflen/2 && (ofs < pl_cur || ofs + nb > p->buflen)) {
//					fill_buffer(p, pl_cur, p->buflen/2 - pl_cur, pack.val);
					ResetEvent(p->positions[1].hEventNotify);
//					printf("snd: waiting second event\n");
			                WaitForSingleObject(p->positions[1].hEventNotify, INFINITE);
					Sprintf(("snd: after waiting second event\n"));
					ResetEvent(p->positions[0].hEventNotify);
					ResetEvent(p->positions[1].hEventNotify);
				}
				if (p->term) break;
				if (pl_cur > p->buflen/2 && (ofs > p->buflen/2 || ofs + nb > p->buflen/2)) {
//					fill_buffer(p, pl_cur, p->buflen - pl_cur, pack.val);
					ResetEvent(p->positions[0].hEventNotify);
//					printf("snd: waiting first event\n");
			                WaitForSingleObject(p->positions[0].hEventNotify, INFINITE);
					Sprintf(("snd: after waiting first event\n"));
					ResetEvent(p->positions[0].hEventNotify);
					ResetEvent(p->positions[1].hEventNotify);
				}
				if (p->term) break;
				timeremains = 1000;
			}
			Sprintf(("snd: filling buffer: ofs = %x, nb = %i, val = %x\n", ofs, nb, pack.val&0xFFFF));
			ofs = flushsmpbuf(p, ofs);
			fill_buffer(p, ofs, nb, pack.val);
			cur_ofs = ofs + nb;
			if (cur_ofs > p->buflen) cur_ofs -= p->buflen;
			Sprintf(("snd: increased curofs = %x\n", cur_ofs));
			{
				int n = (cur_ofs - pl_cur);
				if (n < 0) n += p->buflen;
				timeremains = n * 1000 / p->freq / sizeof(sample_t);
			}
			if (!p->playing && cur_ofs > p->buflen / 2) {
				IDirectSoundBuffer_SetCurrentPosition(p->buffer, 0);
				Sprintf(("snd: starting play from start of buffer\n"));
				IDirectSoundBuffer_Play(p->buffer, 0, 0, DSBPLAY_LOOPING);
				p->playing = 1;
			}
		}
	}
	return 0;
}



static struct DS_DATA* sound_init(struct SOUNDPARAMS*par)
{
	struct DS_DATA*p;
	DWORD tid;
	DSBUFFERDESC de;
	static WAVEFORMATEX fmt;

	p = calloc(1, sizeof(*p));
	if (!p) return NULL;

	p->sr = par->sr;
	p->prevsmp = -1;
	p->cursmp = 0;
	p->cur_val = MIN_SAMPLE;
	p->buflen = par->buflen * 4;
	p->bufsmp = p->buflen / sizeof(sample_t);
	p->freq = par->freq;
	p->video_w = par->w;

	INIT_STR(de);
	memset(&fmt,0,sizeof(fmt));
	fmt.wFormatTag=WAVE_FORMAT_PCM;
	fmt.nChannels=1;
	fmt.nSamplesPerSec=par->freq;
	fmt.nAvgBytesPerSec=par->freq * sizeof(sample_t);
	fmt.nBlockAlign=sizeof(sample_t);
	fmt.wBitsPerSample=sizeof(sample_t)*8;

	if (DS_FAILED(DirectSoundCreate(NULL,&p->sound,NULL),TEXT("initializing Direct Sound"))) goto err0;
	if (DS_FAILED(IDirectSound_SetCooperativeLevel(p->sound,par->w,
		DSSCL_NORMAL),TEXT("setting cooperative level"))) {
		goto err1;
	}
//	de.dwFlags=DSBCAPS_PRIMARYBUFFER|DSBCAPS_CTRLVOLUME|DSBCAPS_GETCURRENTPOSITION2;
 	de.lpwfxFormat=&fmt;
	de.dwFlags=DSBCAPS_CTRLPOSITIONNOTIFY|DSBCAPS_GETCURRENTPOSITION2;
	de.dwBufferBytes=p->buflen;
	if (DS_FAILED(IDirectSound_CreateSoundBuffer(p->sound,&de,&p->buffer,NULL),TEXT("Creating sound buffer"))) {
		goto err1;
	}
	if (DS_FAILED(IDirectSoundBuffer_QueryInterface(p->buffer, &IID_IDirectSoundNotify, &p->notify),TEXT("Creating notification interface"))) {
		goto err2;
	}
	p->positions[0].hEventNotify = CreateEvent(NULL, TRUE, FALSE, NULL);
	p->positions[0].dwOffset = 0;
	p->positions[1].hEventNotify = CreateEvent(NULL, TRUE, FALSE, NULL);
	p->positions[1].dwOffset = p->buflen >> 1;
	IDirectSoundNotify_SetNotificationPositions(p->notify, 2, p->positions);
	p->playing = 0;
	p->part = 0;
	p->hsem = CreateSemaphore(NULL, 0, 100*sizeof(struct SNDPACK), NULL);
	p->hthread = CreateThread(NULL, 0, sound_proc, p, 0, &p->tid);
	SetThreadPriority(p->hthread, THREAD_PRIORITY_TIME_CRITICAL);
	return p;
err3:
	IDirectSoundNotify_Release(p->notify);
err2:
	IDirectSoundBuffer_Release(p->buffer);
err1:
	IDirectSound_Release(p->sound);
err0:
	free(p);
	return NULL;
}

static void sound_term(struct DS_DATA*p)
{
	if (!p) return;
	p->term = 1;
	ReleaseSemaphore(p->hsem, 1, NULL);
	WaitForSingleObject(p->hthread, 1000);
	TerminateThread(p->hthread, 0);
	CloseHandle(p->hthread);
	CloseHandle(p->hsem);
	CloseHandle(p->positions[0].hEventNotify);
	CloseHandle(p->positions[1].hEventNotify);
	if (p->notify) IDirectSoundNotify_Release(p->notify);
	if (p->buffer) IDirectSoundBuffer_Release(p->buffer);
	if (p->sound) IDirectSound_Release(p->sound);
	free(p);
}             

static int sound_write(struct DS_DATA*p, int val, int nsmp)
{
	struct SNDPACK pack = {val, nsmp};
	DWORD nb;
	int n = p->wpos;
	if (!nsmp) return 1;
//	printf("sound_write: %i, %i\n", val, nsmp);
	++n;
	if (n == BUF_SIZE) n = 0;
	while (n == p->rpos) {
		if (cpu_get_fast(p->sr)) return 0;
		msleep(30);
	}	
	p->buf[n] = pack;
	p->wpos = n;
/*	do {
		if (!WriteFile(p->pipe[1], &pack, sizeof(pack), &nb, NULL)) return -1;
	} while (nb != sizeof(pack));*/
	ReleaseSemaphore(p->hsem, 1, NULL);
	return 0;
}

static int sound_data(struct DS_DATA*p, int val, long t, long f)
{
	int maxnsmp = p->bufsmp;
	double fsmp, mult = 1.01;
	if (!p || !p->sound) return -1;
	if (val == SOUND_TOGGLE) val = (p->cur_val ^= XOR_SAMPLE);
	else p->cur_val = val;
	if (!p->playing) {
		system_command(p->sr, SYS_COMMAND_BOOST, 100, 0);
	}
	if (t < p->prev_tick || !p->prev_tick) {
		p->prev_tick = t - 1;
		fsmp = 1;
	} else {
		fsmp = (t - p->prev_tick) * (double)p->freq/1000.0/(double)f * mult;
	}
//	printf("delta_t = %i, fsmp = %g, flen = %g, fsum = %g\n", t - p->prev_tick, fsmp, p->flen, p->fsum);
	if (fsmp > maxnsmp) { fsmp = 1; }
	if (p->flen) {
		if (p->flen + fsmp > 1) {
			double t = 1 - p->flen;
			p->fsum += ((double)val - MID_SAMPLE) / (double)(MAX_SAMPLE - MID_SAMPLE) * t;
			sound_write(p, p->fsum * (double)(MAX_SAMPLE - MID_SAMPLE) + MID_SAMPLE, 1);
			fsmp -= t;
			p->fsum = 0;
			p->flen = 0;
		} else {
			p->fsum += ((double)val - MID_SAMPLE) / (double)(MAX_SAMPLE - MID_SAMPLE) * fsmp;
			p->flen += fsmp;
			fsmp = 0;
			goto fin;
		}
	}
	// at this point flen = 0 always
	if (fsmp > 1) {
		sound_write(p, val, (int)fsmp);
		fsmp -= (int)fsmp;
	}
	// at this point fsmp < 1 always
	p->fsum = ((double)val - MID_SAMPLE) / (double)(MAX_SAMPLE - MID_SAMPLE) * fsmp;
	p->flen = fsmp;
fin:
	p->prev_tick = t;
	return 0;
}

struct SOUNDPROC soundds={ sound_init, sound_term, sound_data };
