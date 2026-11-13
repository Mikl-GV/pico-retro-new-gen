/*
	Agat Emulator version 1.4
	Copyright (c) NOP, nnop@newmail.ru
	mockingboard - emulation of Apple2's sound effects card
*/

#include "types.h"
#include "memory.h"
#include "streams.h"
#include "debug.h"
#include "sysconf.h"
#include "runmgr.h"
#include "runmgrint.h"

#include <dsound.h>
#include <assert.h>


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define _USE_MATH_DEFINES
#include <math.h>

#include "syslib.h"
#include "debug.h"

#include "localize.h"

#pragma comment(lib,"dsound")

//#define DEBUG_SOUND
#ifdef DEBUG_SOUND
#define Sprintf(x) printf x
#else
#define Sprintf(x)
#endif



#define INIT_STR(s) { memset(&(s),0,sizeof(s)); (s).dwSize=sizeof(s); }

#define NCHANS 2
#define NGENS 3
#define NBUFS (2*NCHANS*NGENS)


#define NREGS 16

#define MB_ORB		0x00
#define MB_ORA		0x01
#define MB_DDRB		0x02
#define MB_DDRA		0x03
#define MB_CNT1L	0x04
#define MB_CNT1H	0x05
#define MB_LATCH1L 	0x06
#define MB_LATCH1H 	0x07
#define MB_CNT2L	0x08
#define MB_CNT2H	0x09
#define MB_SR		0x0A
#define MB_ACR 		0x0B
#define MB_PCR		0x0C
#define MB_IFR 		0x0D
#define MB_IER 		0x0E
#define MB_ORAX		0x0F



typedef short sample_t;


struct SOUND_STATE
{
	struct SLOT_RUN_STATE*st;
	IDirectSound*sound;
	IDirectSoundBuffer*buffers[NBUFS];

	int freq, freq0, amp0, ampn;
	int buflen, bufsmp;
	HWND video_w;

	byte ddrs[NCHANS][2];
	byte psg_data[NCHANS][15];
	int  psg_curreg[NCHANS];
	byte psg_regs[NCHANS][NREGS];
	int  upd_regs[NCHANS][NREGS];
	byte t2l_l[NCHANS];
	long timer_start[2][NCHANS];

	long long envelope_time[NCHANS];
	long long envelope_delay[NCHANS];
	byte old_env[NCHANS][NGENS];

	int cpu_freq_khz;
};


static void mb_callback(struct SOUND_STATE*ss, int channel, int no);



void fill_buf_sine(IDirectSoundBuffer*ds, int len, double a, double f)
{
	LPVOID pt[2];
	DWORD sz[2];
	if (IDirectSoundBuffer_Lock(ds, 0, len, pt + 0, sz + 0, pt + 1, sz + 1, 0)!=DS_OK) {
		Sprintf(("fill_sine: ds lock failed!\n"));
		return;
	}
	if (pt[1]) {
		puts("pt[1] should be NULL");
	} else {
		int ns = sz[0] / sizeof(sample_t);
		double v = 0;
		sample_t*s = pt[0];
		int nl = (int)(f * ns / (2 * M_PI));
		f = nl * 2 * M_PI / ns;
		for (; ns; --ns, ++s, v += f) {
			double ss = sin(v);
			double s1 = pow(fabs(ss), 4.0);
			if (ss < 0) s1 = -s1;
			*s = (sample_t)(a * s1);
		}
	}
	if (DS_FAILED(IDirectSoundBuffer_Unlock(ds, pt[0], sz[0], pt[1], sz[1]),TEXT("unlocking buffer"))) {
		Sprintf(("fill_sine: ds unlock failed!\n"));
		return;
	}
	
}



void fill_buf_noise(IDirectSoundBuffer*ds, int len, double a, double f)
{
	LPVOID pt[2];
	DWORD sz[2];
	if (IDirectSoundBuffer_Lock(ds, 0, len, pt + 0, sz + 0, pt + 1, sz + 1, 0)!=DS_OK) {
		Sprintf(("fill_noise: ds lock failed!\n"));
		return;
	}
	if (pt[1]) {
		puts("pt[1] should be NULL");
	} else {
		int ns = sz[0] / sizeof(sample_t);
		double v = 0;
		sample_t*s = pt[0];
		for (; ns; --ns, ++s, v += f) {
			*s = (sample_t)(a * rand() / RAND_MAX);
		}
	}
	if (DS_FAILED(IDirectSoundBuffer_Unlock(ds, pt[0], sz[0], pt[1], sz[1]),TEXT("unlocking buffer"))) {
		Sprintf(("fill_noise: ds unlock failed!\n"));
		return;
	}
	
}



static int sound_init(struct SOUND_STATE*ss)
{
	WAVEFORMATEX fmt;
	int i, j;

	ss->buflen = 32768;
	ss->bufsmp = ss->buflen / sizeof(sample_t);
	ss->freq = 44100;
	ss->freq0 = 1000;
	{
		int nt = ss->bufsmp * ss->freq0 / ss->freq;
		ss->freq0 = nt * ss->freq / ss->bufsmp;
	}
	ss->amp0 = 32767;
	ss->ampn = 3000;
	ss->video_w = ss->st->sr->video_w;
	
	if (DS_FAILED(DirectSoundCreate(NULL,&ss->sound,NULL),TEXT("initializing Direct Sound"))) goto err0;
	if (DS_FAILED(IDirectSound_SetCooperativeLevel(ss->sound, ss->video_w,
		DSSCL_NORMAL),TEXT("setting cooperative level"))) {
		goto err1;
	}

	memset(&fmt, 0, sizeof(fmt));
	fmt.wFormatTag = WAVE_FORMAT_PCM;
	fmt.nChannels = 1;
	fmt.nSamplesPerSec = ss->freq;
	fmt.nAvgBytesPerSec = ss->freq * sizeof(sample_t) * fmt.nChannels;
	fmt.nBlockAlign = sizeof(sample_t) * fmt.nChannels;
	fmt.wBitsPerSample = sizeof(sample_t) * 8;

	for (i = 0; i < NBUFS; ++i) {
		DSBUFFERDESC de;
		INIT_STR(de);
	 	de.lpwfxFormat=&fmt;
		de.dwFlags=DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_STATIC;
		de.dwBufferBytes=ss->buflen;
		if (DS_FAILED(IDirectSound_CreateSoundBuffer(ss->sound,&de,ss->buffers + i,NULL),TEXT("Creating sound buffer"))) {
			goto err2;
		}
		if (i < NCHANS*NGENS) {
			fill_buf_sine(ss->buffers[i], ss->buflen, ss->amp0, 2.0*M_PI*ss->freq0/ss->freq);
		} else {
			fill_buf_noise(ss->buffers[i], ss->buflen, ss->ampn, 2.0*M_PI*ss->freq0/ss->freq);
		}	
		IDirectSoundBuffer_SetVolume(ss->buffers[i], -10000);
		IDirectSoundBuffer_SetPan(ss->buffers[i], (i&1)?10000:-10000);
	}
	return 0;
err2:
	for (j = 0; j < i; ++j) {
		IDirectSoundBuffer_Release(ss->buffers[j]);
	}
err1:
	IDirectSound_Release(ss->sound);
err0:
	return -1;
}

static int sound_term(struct SOUND_STATE*ss)
{
	int i;
	for (i = 0; i < NBUFS; ++i) {
		if (ss->buffers[i]) IDirectSoundBuffer_Release(ss->buffers[i]);
	}
	if (ss->sound) IDirectSound_Release(ss->sound);
	return 0;
}








                                                                                       	




static int mb_term(struct SLOT_RUN_STATE*st)
{
	struct SOUND_STATE*ss = st->data;
	sound_term(ss);
	free(ss);
	return 0;
}

static int mb_save(struct SLOT_RUN_STATE*st, OSTREAM*out)
{
	struct SOUND_STATE*ss = st->data;
	WRITE_ARRAY(out, ss->ddrs);
	WRITE_ARRAY(out, ss->psg_data);
	WRITE_ARRAY(out, ss->psg_curreg);
	WRITE_ARRAY(out, ss->psg_regs);

	return 0;
}

static void start_timer(int channel, int no, struct SOUND_STATE*ss);
static void psg_write_reg(int channel, byte reg, byte data, struct SOUND_STATE *ss);
static void psg_update_regs(int channel, struct SOUND_STATE *ss);

#define ENVELOPE_PERIOD 1000

static int mb_load(struct SLOT_RUN_STATE*st, ISTREAM*in)
{
	struct SOUND_STATE*ss = st->data;
	int i, j;

	READ_ARRAY(in, ss->ddrs);
	READ_ARRAY(in, ss->psg_data);
	READ_ARRAY(in, ss->psg_curreg);
	READ_ARRAY(in, ss->psg_regs);

	for (j = 0; j < NCHANS; ++j) {
		for (i = 0; i < NREGS; ++i) {
			psg_write_reg(j, i, ss->psg_regs[j][i], ss);
		}
	}
	psg_update_regs(0, ss);
	psg_update_regs(1, ss);
	start_timer(0, 0, ss);
	start_timer(1, 0, ss);
	system_command(ss->st->sr, SYS_COMMAND_SET_CPUTIMER, ENVELOPE_PERIOD, DEF_CPU_TIMER_ID(ss->st) | 2);
	return 0;
}

static void psg_reset(int channel, struct SOUND_STATE*ss);

static void mb_pause(struct SOUND_STATE*ss, int stop)
{
	int i, j;
	for (i = 0; i < NBUFS; ++i) {
		if (ss->buffers[i]) {
			if (stop) IDirectSoundBuffer_Stop(ss->buffers[i]);
		}
	}
	if (!stop) {
		for (j = 0; j < NCHANS; ++j) {
			for (i = 0; i < NREGS; ++i) {
				psg_write_reg(j, i, ss->psg_regs[j][i], ss);
			}
		}
	}
}

static int mb_command(struct SLOT_RUN_STATE*st, int cmd, int data, long param)
{
	struct SOUND_STATE*ss = st->data;
	switch (cmd) {
	case SYS_COMMAND_STOP:
		mb_pause(ss, 1);
		return 0;
	case SYS_COMMAND_START:
		mb_pause(ss, 0);
		return 0;
	case SYS_COMMAND_RESET:
		return 0;
	case SYS_COMMAND_HRESET:
		system_command(ss->st->sr, SYS_COMMAND_SET_CPUTIMER, 0, DEF_CPU_TIMER_ID(ss->st));
		psg_reset(0, ss);
		psg_reset(1, ss);
		return 0;
	case SYS_COMMAND_INIT_DONE:
		ss->cpu_freq_khz = cpu_get_freq(st->sr);
		system_command(ss->st->sr, SYS_COMMAND_SET_CPUTIMER, ENVELOPE_PERIOD, DEF_CPU_TIMER_ID(ss->st) | 2);
		return 0;
	case SYS_COMMAND_CPUTIMER:
		if ((param&~7L) == DEF_CPU_TIMER_ID(ss->st)) {
			mb_callback(ss, param & 3, (param & 4) ? 1: 0);
			return 1;
		}
		return 0;
	}
	return 0;
}


static void update_gen_enable(int channel, byte data, struct SOUND_STATE *ss);

static void psg_reset(int channel, struct SOUND_STATE*ss)
{
	int i;
	Sprintf(("psg_reset on channel %i\n", channel));
	memset(ss->psg_regs[channel], 0, sizeof(ss->psg_regs[channel]));
	for (i = channel; i < NBUFS; i+=2) {
		IDirectSoundBuffer_Stop(ss->buffers[i]);
		IDirectSoundBuffer_SetVolume(ss->buffers[i], 0);
		IDirectSoundBuffer_SetFrequency(ss->buffers[i], ss->freq);
	}
}

static void psg_inactive(int channel, struct SOUND_STATE*ss)
{
//	Sprintf(("psg_inactive on channel %i\n", channel));
}


static void update_gen_freq(int channel, int gen, byte d1, byte d2, struct SOUND_STATE *ss)
{
	int div = ((d2 & 0x0F)<<12) | (((unsigned) d1)<<4);
	double f1;
	if (!div) return;
	f1 = ss->cpu_freq_khz * 1000.0 / div;
	Sprintf(("freq[%i,%i] = %g Hz (%04X)\n", channel, gen, f1, div));
	IDirectSoundBuffer_SetFrequency(ss->buffers[gen*2+channel], (DWORD)(f1*ss->freq/ss->freq0));
}


static void update_ng_freq(int channel, byte d1, struct SOUND_STATE *ss)
{
	int div = (d1 & 0x1F)<<4;
	double f1;
	int i;
	if (!div) return;
	f1 = ss->cpu_freq_khz * 1000.0 / div;
	Sprintf(("freq_ng[%i] = %g Hz\n", channel, f1));
	for (i = 0; i < NGENS; ++i) {
		IDirectSoundBuffer_SetFrequency(ss->buffers[(i+NGENS)*2+channel], (DWORD)(f1*ss->freq/ss->freq0));
	}
}

static void update_gen_enable(int channel, byte data, struct SOUND_STATE *ss)
{
	if ((data & 4) == 0) {
		Sprintf(("enable C-%i\n", channel));
		IDirectSoundBuffer_Play(ss->buffers[4|channel], 0, 0, DSBPLAY_LOOPING);
	} else {
		Sprintf(("disable C-%i\n", channel));
		IDirectSoundBuffer_Stop(ss->buffers[4|channel]);
	}
	if ((data & 2) == 0) {
		Sprintf(("enable B-%i\n", channel));
		IDirectSoundBuffer_Play(ss->buffers[2|channel], 0, 0, DSBPLAY_LOOPING);
	} else {
		Sprintf(("disable B-%i\n", channel));
		IDirectSoundBuffer_Stop(ss->buffers[2|channel]);
	}
	if ((data & 1) == 0) {
		Sprintf(("enable A-%i\n", channel));
		IDirectSoundBuffer_Play(ss->buffers[0|channel], 0, 0, DSBPLAY_LOOPING);
	} else {
		Sprintf(("disable A-%i\n", channel));
		IDirectSoundBuffer_Stop(ss->buffers[0|channel]);
	}
	if ((data & 32) == 0) {
		Sprintf(("enable C-noise %i\n", channel));
		IDirectSoundBuffer_Play(ss->buffers[10|channel], 0, 0, DSBPLAY_LOOPING);
	} else {
		Sprintf(("disable C-noise %i\n", channel));
		IDirectSoundBuffer_Stop(ss->buffers[10|channel]);
	}
	if ((data & 16) == 0) {
		Sprintf(("enable B-noise %i\n", channel));
		IDirectSoundBuffer_Play(ss->buffers[8|channel], 0, 0, DSBPLAY_LOOPING);
	} else {
		Sprintf(("disable B-noise %i\n", channel));
		IDirectSoundBuffer_Stop(ss->buffers[8|channel]);
	}
	if ((data & 8) == 0) {
		Sprintf(("enable A-noise %i\n", channel));
		IDirectSoundBuffer_Play(ss->buffers[6|channel], 0, 0, DSBPLAY_LOOPING);
	} else {
		Sprintf(("disable A-noise %i\n", channel));
		IDirectSoundBuffer_Stop(ss->buffers[6|channel]);
	}
}

void update_gen_amp(int channel, int gen, byte data, struct SOUND_STATE *ss)
{
	Sprintf(("update_gen_amp[%i,%i] = %i\n", channel, gen, data));
	if (data & 16) { // envelope
		return;
//		data = 0;
	}
	{
		static const int levels[16] = {
			-10000, -5000, -4000, -2000, -1000, -500, -400, -200,
			-100, -50, -40, -20, -10, -5, -4, 0
		};
		int lev;
		data &= 15;
		lev = levels[data];//((15 - data) * -10000)/15;
		Sprintf(("new sound level %i (%02X) on channel %i:%i\n", lev, data, channel, gen));
		IDirectSoundBuffer_SetVolume(ss->buffers[channel + gen*2], lev);
		IDirectSoundBuffer_SetVolume(ss->buffers[channel + (gen + NGENS)*2], lev);
	}
}

void update_env_freq(int channel, byte d1, byte d2, struct SOUND_STATE *ss)
{
	int div = (((unsigned)d2)<<16) | (((unsigned) d1)<<8);
	ss->envelope_delay[channel] = div;
	ss->envelope_time[channel] = 0;
	if (!div) return;
	Sprintf(("freq_env[%i] = %g Hz\n", channel, ss->cpu_freq_khz * 1000.0 / div));
}

void update_env_shape(int channel, byte data, struct SOUND_STATE *ss)
{
	ss->envelope_time[channel] = 0;
}

static void psg_update_regs(int channel, struct SOUND_STATE *ss)
{
	int reg;
	for (reg = 0; reg < 15; ++reg) {
		if (!ss->upd_regs[channel][reg]) continue;
		switch (reg) {
		case 0: case 1:
			update_gen_freq(channel, 0, ss->psg_regs[channel][0], ss->psg_regs[channel][1]&15, ss);
			ss->upd_regs[channel][0] = 0;
			ss->upd_regs[channel][1] = 0;
			break;
		case 2: case 3:
			update_gen_freq(channel, 1, ss->psg_regs[channel][2], ss->psg_regs[channel][3]&15, ss);
			ss->upd_regs[channel][2] = 0;
			ss->upd_regs[channel][3] = 0;
			break;
		case 4: case 5:
			update_gen_freq(channel, 2, ss->psg_regs[channel][4], ss->psg_regs[channel][5]&15, ss);
			ss->upd_regs[channel][4] = 0;
			ss->upd_regs[channel][5] = 0;
			break;
		case 11: case 12:
			update_env_freq(channel, ss->psg_regs[channel][11], ss->psg_regs[channel][12], ss);
			ss->upd_regs[channel][11] = 0;
			ss->upd_regs[channel][12] = 0;
			break;
		default:
			ss->upd_regs[channel][reg] = 0;
		}
	}
}

static void psg_write_reg(int channel, byte reg, byte data, struct SOUND_STATE *ss)
{
//	Sprintf(("psg_write_reg[%i][%i]=%i\n", channel, reg, data));
	if (ss->psg_regs[channel][reg] != data) {
		ss->psg_regs[channel][reg] = data;
		ss->upd_regs[channel][reg] = 1;
	}
	switch (reg) {
	case 6:
		update_ng_freq(channel, data&0x1F, ss);
		break;
	case 7:
		update_gen_enable(channel, data, ss);
		break;
	case 8:
		update_gen_amp(channel, 0, data, ss);
		break;
	case 9:
		update_gen_amp(channel, 1, data, ss);
		break;
	case 10:
		update_gen_amp(channel, 2, data, ss);
		break;
	case 13:
		update_env_shape(channel, data, ss);
		break;
	}
}

static void psg_write(int channel, struct SOUND_STATE*ss)
{
//	Sprintf(("psg_write %02x to reg %i on channel %i\n", ss->psg_data[channel][MB_ORA], ss->psg_curreg[channel], channel));
	psg_write_reg(channel, ss->psg_curreg[channel] % NREGS, ss->psg_data[channel][MB_ORA], ss);
}

static void psg_select(int channel, struct SOUND_STATE*ss)
{
//	Sprintf(("psg_select %i on channel %i\n", ss->psg_data[channel][MB_ORA], channel));
	ss->psg_curreg[channel] = ss->psg_data[channel][MB_ORA];
}

static void psg_command(int channel, byte cmd, struct SOUND_STATE*ss)
{
	switch (cmd) {
	case 0: psg_reset(channel, ss); break;
	case 4: psg_inactive(channel, ss); break;
	case 6: psg_write(channel, ss); break;
	case 7: psg_select(channel, ss); break;
	}
}

static void psg_data(int channel, byte data, struct SOUND_STATE*ss)
{
//	Sprintf(("psg_data %x on channel %i\n", data, channel));
}


static void fix_ifr(int channel, struct SOUND_STATE*ss)
{
	if ((ss->psg_data[channel][MB_IFR] &= 0x7F) & (ss->psg_data[channel][MB_IER] & 0x7F)) {
		ss->psg_data[channel][MB_IFR] |= 0x80;
	}
}


static void start_timer(int channel, int no, struct SOUND_STATE*ss)
{
	int div;
	if (no) {
		if (!(ss->psg_data[channel][MB_IER] & 0x20)) return;
	} else {
		if (!(ss->psg_data[channel][MB_IER] & 0x40)) return;
	}
	if (!no) {
		div = ss->psg_data[channel][MB_LATCH1L] | (ss->psg_data[channel][MB_LATCH1H]<<8);
	} else {
		div = ss->psg_data[channel][MB_CNT2L] | (ss->psg_data[channel][MB_CNT2H]<<8);
	}
	Sprintf(("T%i latch: %02X%02X; div = %i\n", no + 1, ss->psg_data[channel][MB_LATCH1H], ss->psg_data[channel][MB_LATCH1L], div));
	system_command(ss->st->sr, SYS_COMMAND_SET_CPUTIMER, div, DEF_CPU_TIMER_ID(ss->st) | channel | (no << 2));
}

static void stop_timer(int channel, int no, struct SOUND_STATE*ss)
{
	system_command(ss->st->sr, SYS_COMMAND_SET_CPUTIMER, 0, DEF_CPU_TIMER_ID(ss->st) | channel | (no << 2));
}


static void psg_ifr(int channel, byte data, struct SOUND_STATE*ss)
{
//	printf("psg_ifr %x on channel %i\n", data, channel);
	data = (~data) | 0x80;
	ss->psg_data[channel][MB_IFR] &= data;
	fix_ifr(channel, ss);
	//if (!(data&0x80)) 
//	system_command(ss->st->sr, SYS_COMMAND_NOIRQ, 0, 0);
}

static void psg_ier(int channel, byte data, struct SOUND_STATE*ss)
{
//	printf("psg_ier %x on channel %i\n", data, channel);
	if (data & 0x80) {
		data &= 0x7F;
		ss->psg_data[channel][MB_IER] |= data;
		fix_ifr(channel, ss);
		start_timer(channel, 0, ss);
		start_timer(channel, 1, ss);
	} else {
		data ^= 0x7F;
		ss->psg_data[channel][MB_IER] &= data;
		stop_timer(channel, 0, ss);
		stop_timer(channel, 1, ss);
	}
}

static void psg_pcr(int channel, byte data, struct SOUND_STATE*ss)
{
}


static void env_callback_chan(struct SOUND_STATE*ss, int channel)
{
	int pattern = ss->psg_regs[channel][13];
	long long div = ss->envelope_delay[channel];
	int gen;
	long long vol;

	psg_update_regs(channel, ss);

	ss->envelope_time[channel] += ENVELOPE_PERIOD;
	if (!div) return;
	if ((!(pattern & 8) || (pattern & 1)) && (ss->envelope_time[channel] > div)) {
//		ss->envelope_time[channel] = div + 1;
		vol = (pattern&2)?div:0;
	} else {
		long long xdiv = div;
		if (pattern & 2) xdiv <<= 1; // ALT
		vol = ss->envelope_time[channel] % xdiv;
		if (vol > div) vol = xdiv - vol;
		if (!(pattern & 4)) vol = div - vol;
	}
//	printf("envelope[%i]: time=%lli, div=%lli, pattern=%x, vol=%lli\n", channel, ss->envelope_time[channel], div, pattern, vol);
	for (gen = 0; gen < NGENS; ++gen) {
		if (ss->psg_regs[channel][8 + gen] & 16) {
			if (ss->old_env[channel][gen] != vol) {
				int lev = (int)(((div - vol) * -10000)/div);
//				printf("envelope[%i]: setting level %i on generator %i\n", channel, lev, gen);
				IDirectSoundBuffer_SetVolume(ss->buffers[channel + gen * 2], lev);
				IDirectSoundBuffer_SetVolume(ss->buffers[channel + (gen + NGENS)*2], lev);
			}
		}
	}
}

static void env_callback(struct SOUND_STATE*ss)
{
	env_callback_chan(ss, 0);
	env_callback_chan(ss, 1);
}

static void mb_callback(struct SOUND_STATE*ss, int channel, int no)
{
	if (channel == 2) {
		env_callback(ss);
		return;
	}
	Sprintf(("mockingboard interrupt (%i, %i, %i)\n", cpu_get_tsc(ss->st->sr), channel, no));
	if (no) {
		ss->psg_data[channel][MB_IFR] |= 0x20;
	} else {
		ss->psg_data[channel][MB_IFR] |= 0x40;
		ss->psg_data[channel][MB_CNT1L] = ss->psg_data[channel][MB_LATCH1L];
		ss->psg_data[channel][MB_CNT1H] = ss->psg_data[channel][MB_LATCH1H];
	}
	fix_ifr(channel, ss);
//	printf("mockingboard irq\n");
	system_command(ss->st->sr, SYS_COMMAND_IRQ, 10, 0);
	if (no == 0 && ss->psg_data[channel][MB_ACR]&0x40) { // free running
		int div = ss->psg_data[channel][MB_LATCH1L] | (ss->psg_data[channel][MB_LATCH1H]<<8);
		system_command(ss->st->sr, SYS_COMMAND_SET_CPUTIMER, div, DEF_CPU_TIMER_ID(ss->st) | channel | (no << 2));
		ss->timer_start[no][channel] = cpu_get_tsc(ss->st->sr);
	} else { // one shot
		system_command(ss->st->sr, SYS_COMMAND_SET_CPUTIMER, 0, DEF_CPU_TIMER_ID(ss->st) | channel | (no << 2));
	}
}

static void mb_rom_w(word adr, byte data, struct SOUND_STATE*ss) // CX00-CXFF
{
	int chan = 0;
	if (adr & 0x80) chan = 1;

//	Sprintf(("mockingboard: rom write %x, %02x\n", adr, data));
	switch (adr & 0x0F) {
	case MB_IFR: psg_ifr(chan, data, ss); break;
	case MB_IER: psg_ier(chan, data, ss); break;
	case MB_CNT1L: // timer 1 counter l
		break;
	case MB_CNT2L: // timer 2 counter l
		break;
	case MB_ORAX: adr = MB_ORA; // no break
	default:
//		Sprintf(("mockingboard data write[%i][%i] = %02x\n", chan, adr&0x0F, data));
		ss->psg_data[chan][adr&0x0F] = data;
	}

	switch (adr & 0x0F) {
	case MB_ORB: psg_command(chan, data & ss->psg_data[chan][MB_DDRB], ss); break;
	case MB_ORA: psg_data(chan, data & ss->psg_data[chan][MB_DDRA], ss); break;
	case MB_DDRB: break;
	case MB_DDRA: break;
	case MB_CNT1L:
		ss->psg_data[chan][MB_LATCH1L] = data;
		break;
	case MB_CNT1H: // timer 1 counter h
		ss->psg_data[chan][MB_CNT1L] = ss->psg_data[chan][MB_LATCH1L];
		ss->psg_data[chan][MB_LATCH1H] = data;
		ss->psg_data[chan][MB_IFR] &= ~0x40;
		ss->timer_start[chan][0] = cpu_get_tsc(ss->st->sr);
		fix_ifr(chan, ss);
		start_timer(chan, 0, ss);
		break;
	case MB_LATCH1L: // timer 1 latch l
		break;
	case MB_LATCH1H: // timer 1 latch h
		ss->psg_data[chan][MB_IFR] &= ~0x40;
		fix_ifr(chan, ss);
		break;
	case MB_CNT2L: // timer 2 counter l
		ss->t2l_l[chan] = data;
		break;
	case MB_CNT2H: // timer 2 counter h
		ss->psg_data[chan][MB_CNT2L] = ss->t2l_l[chan];
		ss->psg_data[chan][MB_IFR] &= ~0x20;
		ss->timer_start[chan][1] = cpu_get_tsc(ss->st->sr);
		fix_ifr(chan, ss);
		start_timer(chan, 1, ss);
		break;
	case MB_ACR: // ACR
		break;
	case MB_PCR: psg_pcr(chan, data, ss); break;
	case MB_IFR: break;
	case MB_IER: break;
	default:
		Sprintf(("mockingboard: rom write %x, %02x\n", adr, data));
	}
}


static void upd_counters(int channel, struct SOUND_STATE*ss)
{
	long t = cpu_get_tsc(ss->st->sr);
	int v;
	v = ss->psg_data[channel][MB_LATCH1L] | (ss->psg_data[channel][MB_LATCH1H]<<8);
	v -= t - ss->timer_start[0][channel];
	ss->psg_data[channel][MB_CNT1L] = v & 0xFF;
	ss->psg_data[channel][MB_CNT1H] = v >> 8;

	v = ss->psg_data[channel][MB_CNT2L] | (ss->psg_data[channel][MB_CNT2H]<<8);
	v -= t - ss->timer_start[1][channel];
	ss->psg_data[channel][MB_CNT2L] = v & 0xFF;
	ss->psg_data[channel][MB_CNT2H] = v >> 8;
	ss->timer_start[1][channel] = t;
}

static byte mb_rom_r(word adr, struct SOUND_STATE*ss) // CX00-CXFF
{
	int chan = 0;
	Sprintf(("mockingboard: rom read %x\n", adr));
	if (adr & 0x80) chan = 1;
	upd_counters(chan, ss);
	switch (adr & 0x0F) {
	case MB_CNT1L: 
		ss->psg_data[chan][MB_IFR] &= ~0x40;
		fix_ifr(chan, ss);
		break;
	case MB_CNT2L: 
		ss->psg_data[chan][MB_IFR] &= ~0x20;
		fix_ifr(chan, ss);
		break;
	case MB_ORAX: adr = MB_ORA; break;
	default:
		Sprintf(("mockingboard: rom read %x\n", adr));
	}
	return ss->psg_data[chan][adr&0x0F];
}



static void mb_io_w(word adr, byte data, struct SOUND_STATE*ss) // C0X0-C0XF
{
	Sprintf(("mockingboard: io write %x, %02x\n", adr, data));
}


static byte mb_io_r(word adr, struct SOUND_STATE*ss) // C0X0-C0XF
{
	Sprintf(("mockingboard: io read %x\n", adr));
	return empty_read(adr, ss);
}


int  mockingboard_init(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*st, struct SLOTCONFIG*cf)
{
	struct SOUND_STATE*ss;

	puts("in mockingboard_init");

	ss = calloc(1, sizeof(*ss));
	if (!ss) return -1;

	ss->st = st;

	if (sound_init(ss) < 0) { free(ss); return -1; }

	st->data = ss;
	st->free = mb_term;
	st->command = mb_command;
	st->load = mb_load;
	st->save = mb_save;

	fill_rw_proc(st->io_sel, 1, mb_rom_r, mb_rom_w, ss);
	fill_rw_proc(st->baseio_sel, 1, mb_io_r, mb_io_w, ss);

	return 0;
}
