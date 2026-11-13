/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	sound_acm - sound output via ACM library [incomplete]
*/

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include "soundint.h"
#include "debug.h"

#pragma comment(lib,"winmm")

//#define DEBUG_SOUND

#ifdef DEBUG_SOUND
#define Sprintf(...) logprint(5, __VA_ARGS__)
#else
#define Sprintf(...)
#endif

//#define Qprintf(...) printf(__VA_ARGS__)
#define Qprintf(...)

#define FIRST_DELAY 100

#define N_BUFFERS 16

#ifdef ACM_OUTPUT_8BIT
typedef unsigned char sample_t;
#define MIN_SAMPLE 0x00
#define MAX_SAMPLE 0xFF
#define fill_smps memset
#else
typedef short sample_t;
#define MIN_SAMPLE ((sample_t)0x8001)
#define MAX_SAMPLE ((sample_t)0x7FFF)
static __inline void fill_smps(sample_t*buf, sample_t smp, int nsmp)
{
	for (; nsmp; --nsmp, ++buf) *buf = smp;
}
#endif

#define XOR_SAMPLE (MAX_SAMPLE ^ MIN_SAMPLE)
#define MID_SAMPLE ((MIN_SAMPLE + MAX_SAMPLE) >> 1)

struct ACM_DATA;

enum {
	ACM_INACT, // inactive state
	ACM_PREPARE, // prefill the buffer
	ACM_PLAY // playing
};

struct ACM_DATA
{
	struct SYS_RUN_STATE*sr;
	HWAVEOUT out;

	int	cur_state;

	sample_t cur_val;
	long	prev_tick, prev_change;
	int	freq;
	int	timer_id;

	WAVEHDR hdrs[N_BUFFERS];

	sample_t *all_buf;
	int	all_len;
	int	buf_len;

	double  flen, fsum; // fractional length and summ of weighted sample values

	HANDLE	th;
	DWORD	th_id;
	HANDLE	event;

	int wr_buf, pl_buf;
	int cur_ofs;

	CRITICAL_SECTION crit;
};

static int set_timer(struct ACM_DATA*p);

static void CALLBACK out_proc(
	HWAVEOUT hwo,      
	UINT uMsg,         
	DWORD dwInstance,  
	DWORD dwParam1,    
	DWORD dwParam2
);

static void acm_change_state(struct ACM_DATA*p, int new_state)
{
	int i;
	if (new_state == p->cur_state || !p->out) return;
	Sprintf("change state %d->%d\n", p->cur_state, new_state);
	switch (new_state) {
	case ACM_INACT:
		Sprintf("stopping audio...\n");
		waveOutReset(p->out);
		p->wr_buf = p->pl_buf = 0;
		break;
	case ACM_PREPARE:
		if (p->cur_state != ACM_INACT) {
			Sprintf("unable to prepare from state %d\n", p->cur_state);
			return;
		}
		Sprintf("preparing audio...\n");
		set_timer(p);
		p->wr_buf = p->pl_buf = 0;
		p->fsum = p->flen = 0;
		p->cur_ofs = 0;
		break;
	case ACM_PLAY:
		if (p->cur_state != ACM_PREPARE) {
			Sprintf("unable to play from state %d\n", p->cur_state);
			return;
		}
		Sprintf("playing with %d buffer(s)\n", p->pl_buf);
		for (i = 0; i < p->pl_buf; ++i) {
			if (ACM_FAILED(waveOutWrite(p->out, p->hdrs + i, sizeof(p->hdrs[i])), TEXT("waveout write"))) break;
		}
		break;
	}
	p->cur_state = new_state;
}

static void post_cur_buffer(struct ACM_DATA*p, int force);

#define LOCK(p) \
	EnterCriticalSection(&(p)->crit);

#define UNLOCK(p) \
	LeaveCriticalSection(&(p)->crit);


static int get_t()
{
	static int msec;
	int r = GetTickCount() - msec;
	if (!msec) { msec = r; return 0; }
	return r;
}

static int get_msec_from_len(struct ACM_DATA*p, int len)
{
	return len / sizeof(sample_t) * 1000 / p->freq;
}


static int get_len_from_msec(struct ACM_DATA*p, int msec)
{
	return msec * sizeof(sample_t) * p->freq / 1000;
}

static DWORD CALLBACK sound_thread(LPVOID par);
static VOID CALLBACK sound_proc(HWAVEOUT hWaveOut, UINT nMessage, DWORD_PTR nInstance, DWORD_PTR nParameter1, DWORD_PTR nParameter2);


static void* sound_init(struct SOUNDPARAMS*par)
{
	struct ACM_DATA*p;
	WAVEFORMATEX fmt;
	int i, j;

	p = calloc(1, sizeof(*p));
	if (!p) goto err;

	p->cur_state = ACM_INACT;

	p->sr = par->sr;
	p->cur_val = MIN_SAMPLE;

	p->all_len = par->buflen * par->freq / 11025 * 2;
	p->buf_len = p->all_len / N_BUFFERS;
	p->freq = par->freq;
	p->all_buf = malloc(p->all_len * sizeof(sample_t));
	fill_smps(p->all_buf, p->cur_val, p->all_len);

	printf("sound freq = %d; buflen = %d (%d)\n", p->freq, p->all_len, p->buf_len);

	InitializeCriticalSection(&p->crit);


	fmt.wFormatTag = WAVE_FORMAT_PCM;
	fmt.nChannels = 1;
	fmt.nSamplesPerSec = p->freq;
	fmt.nBlockAlign = sizeof(sample_t);
	fmt.wBitsPerSample = sizeof(sample_t) * 8;
	fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

	if (ACM_FAILED(waveOutOpen(&p->out,WAVE_MAPPER,&fmt,(DWORD)sound_proc,(DWORD)p,CALLBACK_FUNCTION | WAVE_ALLOWSYNC),TEXT("opening wave out"))) {
		p->out = NULL;
		return p;
	}

	for (i = 0; i < N_BUFFERS; ++i) {
		p->hdrs[i].dwBufferLength = p->buf_len * sizeof(sample_t);
		p->hdrs[i].lpData = (void*)(p->all_buf + p->buf_len * i);
		p->hdrs[i].dwFlags = 0;
//		if (i == 0) p->hdrs[i].dwFlags |= WHDR_BEGINLOOP;
//		if (i == N_BUFFERS - 1) p->hdrs[i].dwFlags |= WHDR_ENDLOOP;
		p->hdrs[i].dwUser = i;
		if (ACM_FAILED(waveOutPrepareHeader(p->out, p->hdrs + i, sizeof(p->hdrs[i])), TEXT("preparing waveout header"))) { ++i; goto err1; }
	}

	p->event = CreateEvent(NULL, FALSE, FALSE, NULL);
	p->th = CreateThread(NULL, 0, sound_thread, p, 0, &p->th_id);
	SetThreadPriority(p->th, THREAD_PRIORITY_TIME_CRITICAL);

	Sprintf("acm: sound initialized\n");
	return p;
err1:
	for (j = 0; j < i; ++j) {
		waveOutUnprepareHeader(p->out, p->hdrs + j, sizeof(p->hdrs[j]));
	}
	waveOutClose(p->out);
	free(p->all_buf);
err0:
	free(p);
err:
	Sprintf("acm: sound initialization failed\n");
	return NULL;
}

static void sound_term(struct ACM_DATA*p)
{
	HWAVEOUT out;
	if (!p) return;
	out = p->out;
	LOCK(p);
	p->out = NULL;
	if (p->timer_id) timeKillEvent(p->timer_id);
	UNLOCK(p);
	if (out) {
		int i;
		waveOutReset(out);
		for (i = 0; i < N_BUFFERS; ++i) {
			waveOutUnprepareHeader(out, p->hdrs + i, sizeof(p->hdrs[i]));
		}
		waveOutClose(out);
	}
	SetEvent(p->event);
	WaitForSingleObject(p->th, INFINITE);
	CloseHandle(p->event);
	CloseHandle(p->th);
	DeleteCriticalSection(&p->crit);
	free(p->all_buf);
	free(p);
	Sprintf("acm: sound uninitialized\n");
}

static void sound_write_int(struct ACM_DATA*p, sample_t val, int nsmp)
{
	Sprintf("acm: sound_write %d, count = %d, cur_ofs = %d/%d, pl_buf = %d\n", val, nsmp, p->cur_ofs, p->buf_len, p->pl_buf);
	while (nsmp > 0) {
		int sz = nsmp;
		if (((p->pl_buf + 1) % N_BUFFERS) == p->wr_buf) {
			Sprintf("sound buffer overflow!\n");
			return;
		}
		if (sz + p->cur_ofs > p->buf_len) sz = p->buf_len - p->cur_ofs;
		Sprintf("fill: buf %d, ofs %d size %d val %d\n", p->pl_buf, p->cur_ofs, sz, val);
		fill_smps(p->all_buf + p->pl_buf * p->buf_len + p->cur_ofs, val, sz);
		nsmp -= sz;
		p->cur_ofs += sz;
		if (p->cur_ofs == p->buf_len) {
			if (p->cur_state == ACM_PLAY) {
				Sprintf("acm: post buffer %d (current playing %d)\n", p->pl_buf, p->wr_buf);
				if (ACM_FAILED(waveOutWrite(p->out, p->hdrs + p->pl_buf, sizeof(p->hdrs[p->pl_buf])), TEXT("waveout write"))) break;
			}
			p->cur_ofs = 0;
			p->pl_buf = (p->pl_buf + 1) % N_BUFFERS;
			if (p->cur_state == ACM_PREPARE && p->pl_buf == N_BUFFERS / 2) {
				acm_change_state(p, ACM_PLAY);
			}
		}
	}
//	Sprintf("acm: end sound_write\n");
}

static void sound_write(struct ACM_DATA*p, sample_t val, int nsmp)
{
	LOCK(p);
	if (!p->out) {
		UNLOCK(p);
		return;
	}
	sound_write_int(p, val, nsmp);
	UNLOCK(p);
}


static int sound_data(struct ACM_DATA*p, int val, long t, long f)
{
	double fsmp;//, mult = 0.99;
	if (!p || !p->out) return -1;
	if (val == SOUND_TOGGLE) val = (p->cur_val ^ XOR_SAMPLE);
	if (p->cur_state == ACM_INACT) {
		p->prev_tick = t;
		p->prev_change = t;
		p->cur_val = val;
		acm_change_state(p, ACM_PREPARE);
		return 0;
	}


	fsmp = (t - p->prev_tick) * (double)p->freq/1000.0/(double)f;// * mult;
	if (fsmp > p->all_len) fsmp = p->all_len;

	Sprintf("acm: sound_data: %d, tsc %lu, prev %lu, msec %g, fsmp %g\n", val, t, p->prev_tick, (t - p->prev_tick) / (double)f, fsmp);
//	printf("delta_t = %i, nsmp = %i\n", t - p->prev_tick, nsmp);
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
	if (p->cur_val != val) p->prev_change = t;
	p->cur_val = val;
	return 0;
}


static void CALLBACK finish_timer(UINT uID, UINT uMsg, DWORD dwUser, DWORD dw1, DWORD dw2)
{
	struct ACM_DATA*p = (struct ACM_DATA*)dwUser;
	Sprintf("acm: finish_timer\n");
	LOCK(p);
	sound_data(p, p->cur_val, cpu_get_tsc(p->sr), cpu_get_freq(p->sr));
	acm_change_state(p, ACM_PLAY);
	UNLOCK(p);
}

static int set_timer(struct ACM_DATA*p)
{
	Qprintf("%i: set timer for %i msec\n", get_t(), FIRST_DELAY);
	p->timer_id = timeSetEvent(FIRST_DELAY, 0, finish_timer, 
		(DWORD)p, TIME_ONESHOT);
	if (!p->timer_id) {
		fprintf(stderr, "timer: set event failed\n");
	}
	return 0;
}


static int sound_event(struct ACM_DATA*p, void*d, void*par)
{
	if (p->cur_state == ACM_INACT) return 0;
	Sprintf("cur buffer %d: play buffer %d, cur_ofs %d: prev_change = %lu, prev_tick = %lu\n", p->wr_buf, p->pl_buf, p->cur_ofs, p->prev_change, p->prev_tick);
	if (p->prev_tick - p->prev_change > 1000000)  {
		Sprintf("end play\n");
		acm_change_state(p, ACM_INACT);
	} else {
		long t = cpu_get_tsc(p->sr);
		int nsmp = (t - p->prev_tick) * p->freq / 1000.0 / cpu_get_freq(p->sr);
		if (nsmp > p->buf_len / 4) {
			Sprintf("add %d samples...\n", nsmp);
			sound_write_int(p, p->cur_val, nsmp);
			p->prev_tick = t;
		}
		if (((p->wr_buf + 1) % N_BUFFERS) == p->pl_buf || ((p->wr_buf + 2) % N_BUFFERS) == p->pl_buf || 
			p->wr_buf == p->pl_buf) {
			Sprintf("boosting cpu...\n");
			system_command(p->sr, SYS_COMMAND_BOOST, 30000, 0);
		}
	}
	return 1;
}


static DWORD CALLBACK sound_thread(LPVOID par)
{
	struct ACM_DATA*p = par;
	int ms = p->buf_len * 1000 / p->freq / 4;
	for (;;) {
		WaitForSingleObject(p->event, ms);
		if (!p->out) break;
		LOCK(p);
		sound_event(p, NULL, NULL);
		UNLOCK(p);
	}
	return 0;
}

static VOID CALLBACK sound_proc(HWAVEOUT hWaveOut, UINT nMessage, DWORD_PTR nInstance, DWORD_PTR nParameter1, DWORD_PTR nParameter2)
{
	struct ACM_DATA*p = (void*)nInstance;
        switch(nMessage) {
	case WOM_DONE:
		p->wr_buf = (p->wr_buf + 1) % N_BUFFERS;
		Sprintf("done: state = %d, wr buffer = %d, pl buffer = %d\n", p->cur_state, p->wr_buf, p->pl_buf);
		SetEvent(p->event);
		break;
	}
}


struct SOUNDPROC soundacm={ sound_init, sound_term, sound_data, sound_event};
