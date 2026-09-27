#include "defines.h"
#include "emu2149.h"

unsigned char synth_reg;
#define SOUND_FREQ 44100

PSG * psg;

void synth_init() {
	synth_reg = ~0;
	if (!psg) {
		PSG_init(3579545, SOUND_FREQ);
		psg=PSG_new();
	}
	PSG_reset(psg);
	// PSG_setVolumeMode(psg,2);
}

int synth_read(c_addr addr, d_word *word)
{
	// *word = PSG_readReg(psg, synth_reg) ^ 0xFF;
	*word = 0; // BK does not read from AY
	return OK;
}

int synth_write(c_addr addr, d_word word)
{
	// Writing register address
	synth_reg = (word & 0xF) ^ 0xF;
	return OK;
}

int synth_bwrite(c_addr addr, d_byte byte) {
	// Writing data; what happens if the address is odd?
	PSG_writeReg(psg, synth_reg, byte ^ 0xFF);
	return OK;
}

int
synth_next() {
	int a = psg ? PSG_calc(psg) : 0; 
	return a;
}
