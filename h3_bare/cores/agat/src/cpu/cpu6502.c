/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	cpu6502 - emulation of microprocessor
*/

#include "cpuint.h"

#define STACK_START 0x100

#define ADDR_IRQ	0xFFFE
#define ADDR_RES	0xFFFC
#define ADDR_NMI	0xFFFA


struct STATE_6502_SAV
{
	struct SYS_RUN_STATE*sr;

	byte a,x,y,s,f;
	word pc;

	word ea;
	byte ints_req;
};

struct STATE_6502
{
	struct SYS_RUN_STATE*sr;

	byte a,x,y,s,f;
	word pc;

	word ea;
	byte ints_req;
// additional section
	int  addt;
};

#define CMD_ILL		1

#include <stdio.h>

struct CMD_6502
{
	char*adr_name, *cmd_name;
	void(*adr)(struct STATE_6502*st);
	void(*printadr)(struct STATE_6502*st, FILE*out);
	void(*cmd)(struct STATE_6502*st);
	int ticks;
	unsigned flags;
};


#define INT_IRQ	1
#define INT_NMI	2
#define INT_RESET 4

#define FLAG_C	1
#define FLAG_Z	2
#define FLAG_I	4
#define FLAG_D	8
#define FLAG_B	16
#define FLAG_1	32
#define FLAG_V	64
#define FLAG_N	128


static void push_stack(struct STATE_6502 *st, byte b);
static byte pop_stack(struct STATE_6502 *st);
static byte fetch_cmd_byte(struct STATE_6502 *st);
static word mem_read_word(struct STATE_6502 *st, word addr);

static void push_stack_w(struct STATE_6502 *st, word w);
static word pop_stack_w(struct STATE_6502 *st);
static word fetch_cmd_word(struct STATE_6502 *st);
static void mem_write_word_page(struct STATE_6502 *st, word addr,word d);
static word mem_read_word_page(struct STATE_6502 *st, word addr);


static void push_stack(struct STATE_6502 *st, byte b)
{
//	printf("push_stack %02X\n", b);
	mem_write((word)(STACK_START+st->s),b, st->sr);
	--st->s;
}


static byte  pop_stack(struct STATE_6502 *st)
{
	byte r;
	++st->s;
	r = mem_read((word)(STACK_START+st->s), st->sr);
//	printf("pop_stack = %02X\n", r);
	return r;
}

byte fetch_cmd_byte(struct STATE_6502 *st)
{
	return mem_read(st->pc++, st->sr);
}

static word mem_read_word(struct STATE_6502 *st, word addr)
{
	return (word)mem_read(addr,st->sr)|(((word)mem_read((word)(addr+1),st->sr))<<8);
}


static void push_stack_w(struct STATE_6502 *st, word w)
{
//	printf("push_stack_w %02X\n", w);
	st->s--;
	mem_write_word_page(st, (word)(STACK_START+st->s),w);
	st->s--;
}

static word pop_stack_w(struct STATE_6502 *st)
{
	word r;
	st->s++;
	r=mem_read_word_page(st, (word)(STACK_START+st->s));
	st->s++;
//	printf("pop_stack_w = %02X\n", r);
	return r;
}

static word fetch_cmd_word(struct STATE_6502 *st)
{
	byte b=fetch_cmd_byte(st);
	return b|(fetch_cmd_byte(st)<<8);
}

static word mem_read_word_page(struct STATE_6502 *st,word addr)
{
	word res=mem_read(addr, st->sr);
	(*(byte*)&addr)++;
	return res|(mem_read(addr, st->sr)<<8);
}


static void mem_write_word_page(struct STATE_6502 *st,word addr,word d)
{
	mem_write(addr,(byte)d, st->sr);
	(*(byte*)&addr)++;
	mem_write(addr,(byte)(d>>8), st->sr);
}

static void check_flags_log(struct STATE_6502 *st, byte b)
{
	st->f&=~(FLAG_Z|FLAG_N);
	if (!b) st->f|=FLAG_Z;
	if (b&0x80) st->f|=FLAG_N;
}

/////////////////////////////////////////////////////////


static word add_timing_page(struct STATE_6502*st, word a, int ofs)
{
	word r = a + ofs;
	if ((r&~0xFF) != (a&~0xFF)) ++st->addt;
	return r;
}

static void ea_get_imm(struct STATE_6502 *st)
{
	st->ea=st->pc;
	st->pc++;
}

static void ea_get_abs(struct STATE_6502*st)
{
	st->ea=fetch_cmd_word(st);
}

static void ea_get_zp(struct STATE_6502*st)
{
	st->ea=fetch_cmd_byte(st);
}

static void ea_get_zpx(struct STATE_6502*st)
{
	st->ea=(byte)(fetch_cmd_byte(st)+st->x);
}

static void ea_get_zpy(struct STATE_6502*st)
{
	st->ea=(byte)(fetch_cmd_byte(st)+st->y);
}

static void ea_get_absx(struct STATE_6502*st)
{
	st->ea = add_timing_page(st, fetch_cmd_word(st), st->x);
}

static void ea_get_absy(struct STATE_6502*st)
{
	st->ea = add_timing_page(st, fetch_cmd_word(st), st->y);
}

static void ea_get_indx(struct STATE_6502*st)
{
	register byte addr=fetch_cmd_byte(st)+st->x;
	st->ea=mem_read_word_page(st, addr);
}

static void ea_get_indy(struct STATE_6502*st)
{
	register byte addr=fetch_cmd_byte(st);
	st->ea=add_timing_page(st, mem_read_word_page(st, addr), st->y);
}

static void ea_get_ind(struct STATE_6502*st)
{
	register word addr=fetch_cmd_word(st);
	st->ea=mem_read_word_page(st, addr);
}
//////////////////////////////////////////////////////

static void ea_show_imm(struct STATE_6502*st, FILE*out)
{
	fprintf(out,"#%02X",mem_read(st->pc, st->sr));
}

static void ea_show_impl(struct STATE_6502*st, FILE*out)
{
}

static byte xmem_read(word a, struct SYS_RUN_STATE*sr)
{
	byte b;
	sr->in_debug = 1;
//	if (a >= 0xC000 && a < 0xC800) return 0xFF;
	b = mem_read(a, sr);
	sr->in_debug = 0;
	return b;
}

static word xmem_read_word(struct STATE_6502*st, word a)
{
	word w;
//	if (a >= 0xBFFF && a < 0xC800) return 0xFFFF;
	st->sr->in_debug = 1;
	w = mem_read_word(st, a);
	st->sr->in_debug = 0;
	return w;
}

static word xmem_read_word_page(struct STATE_6502*st, word a)
{
	word w;
//	if (a >= 0xC000 && a < 0xC800) return 0xFFFF;
	st->sr->in_debug = 1;
	w = mem_read_word_page(st, a);
	st->sr->in_debug = 0;
	return w;
}


static void ea_show__acc(struct STATE_6502*st, FILE*out)
{
	fprintf(out," A");
}

static void ea_show_rel(struct STATE_6502*st, FILE*out)
{
	word a=(signed char)mem_read(st->pc, st->sr)+(st->pc+1);
	fprintf(out," %04X",a);
}


static void ea_show_abs(struct STATE_6502*st, FILE*out)
{
	word a=mem_read_word(st, st->pc);
	fprintf(out," %04X ([%04X]=%02X)",a,a,xmem_read(a, st->sr));
}

static void ea_show_zp(struct STATE_6502*st, FILE*out)
{
	byte a=mem_read(st->pc, st->sr);
	fprintf(out," %02X ([%02X]=%02X)",a,a,xmem_read(a, st->sr));
}

static void ea_show_zpx(struct STATE_6502*st, FILE*out)
{
	byte a=mem_read(st->pc, st->sr);
	fprintf(out," %02X,X ([%02X]=%02X)",a,(byte)(a+st->x), xmem_read((byte)(a+st->x),st->sr));
}

static void ea_show_zpy(struct STATE_6502*st, FILE*out)
{
	byte a=mem_read(st->pc, st->sr);
	fprintf(out," %02X,Y ([%02X]=%02X)",a,(byte)(a+st->y), xmem_read((byte)(a+st->y), st->sr));
}

static void ea_show_absx(struct STATE_6502*st, FILE*out)
{
	word a=mem_read_word(st, st->pc);
	fprintf(out," %04X,X ([%04X]=%02X)",a,(word)(a+st->x), xmem_read((word)(a+st->x), st->sr));
}

static void ea_show_absy(struct STATE_6502*st, FILE*out)
{
	word a=mem_read_word(st, st->pc);
	fprintf(out," %04X,Y ([%04X]=%02X)",a,(word)(a+st->y), xmem_read((word)(a+st->y),st->sr));
}

static void ea_show_indx(struct STATE_6502*st, FILE*out)
{
	byte a=mem_read(st->pc,st->sr);
	word adr = mem_read_word_page(st, a + st->x);
	fprintf(out," (%02X,X) ([%04X]=%02X)",a,adr,xmem_read(adr, st->sr));
}

static void ea_show_indy(struct STATE_6502*st, FILE*out)
{
	byte a=mem_read(st->pc,st->sr);
	word adr = mem_read_word_page(st, a) + st->y;
	fprintf(out," (%02X),Y ([%04X]=%02X)",a,adr,xmem_read(adr, st->sr));
}

static void ea_show_ind(struct STATE_6502*st, FILE*out)
{
	word a=mem_read_word(st,st->pc);
	word adr = mem_read_word_page(st, a);
	fprintf(out," (%04X) ([%04X]=%02X)",a,adr,xmem_read(adr, st->sr));
}


////////////////////////////////////////////////////////////


static void op_hlt(struct STATE_6502 *st)
{
//	printf("hlt at %x\n",st->pc-1);
	st->pc--;
//	usleep(10000);
}


static void op_adc(struct STATE_6502 *st)
{
	byte b=mem_read(st->ea, st->sr);
	word w;
	byte b1,b2,b3;

	w=(word)st->a+b;
	if (st->f&FLAG_C) w++;
	check_flags_log(st, (byte)w);

	if (st->f&FLAG_D) {
		w = (st->a & 0x0F) + (b & 0x0F);
		if (st->f & FLAG_C) ++ w;
		if (w > 9) {
			w += 0x10 - 10;
		}
		w += (st->a & 0xF0) + (b & 0xF0);
		if (w&0x80) st->f |= FLAG_N; else st->f &= ~FLAG_N;
		if (w >= 0xA0) {
			w+=0x100-0xA0;
		}
//		logprint(0,"adc decimal: %02x+%02x+%x=%02x",st->a,b,(st->f&FLAG_C)?1:0,w&0xFF);
	}
	b1=st->a&0x80;
	b2=b&0x80;
	b3=(st->f&FLAG_N)?0x80:0;
	if ((~(b1^b2))&(b1^b3)&0x80) st->f|=FLAG_V; else st->f&=~FLAG_V;
	if (w&0x100) st->f|=FLAG_C; else st->f&=~FLAG_C;
/*	if (st->f & FLAG_D) {
		printf("adc: %02X + %02X = %02X (C=%i, V=%i, N=%i, Z=%i)\n",
			st->a, b, w & 0xFF,
			(st->f&FLAG_C)?1:0,
			(st->f&FLAG_V)?1:0,
			(st->f&FLAG_N)?1:0,
			(st->f&FLAG_Z)?1:0);
	}*/
	st->a=(byte)w;
}

static void op_and(struct STATE_6502 *st)
{
	check_flags_log(st, st->a&=mem_read(st->ea, st->sr));
}

static void op_asl_acc(struct STATE_6502 *st)
{
	word w=st->a;
	w<<=1;
	st->f&=~(FLAG_Z|FLAG_N|FLAG_C);
	if (w&0x100) st->f|=FLAG_C;
	st->a=(byte)w;
	check_flags_log(st, st->a);
}

static void op_asl_mem(struct STATE_6502 *st)
{
	word w=mem_read(st->ea, st->sr);
	w<<=1;
	st->f&=~(FLAG_Z|FLAG_N|FLAG_C);
	if (w&0x100) st->f|=FLAG_C;
	mem_write(st->ea,(byte)w, st->sr);
	check_flags_log(st, (byte)w);
}

static void op_bcc(struct STATE_6502 *st)
{
	if (st->f&FLAG_C) {
		st->pc++;
	} else {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	}
}

static void op_bcs(struct STATE_6502 *st)
{
	if (st->f&FLAG_C) {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	} else {
		st->pc++;
	}
}

static void op_beq(struct STATE_6502 *st)
{
	if (st->f&FLAG_Z) {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	} else {
		st->pc++;
	}
}

static void op_bit(struct STATE_6502 *st)
{
	byte b=mem_read(st->ea, st->sr);
	st->f&=~(FLAG_Z|FLAG_N|FLAG_V);
	if (!(st->a&b)) st->f|=FLAG_Z;
	st->f|=(b&(FLAG_N|FLAG_V));
}

static void op_bmi(struct STATE_6502 *st)
{
	if (st->f&FLAG_N) {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	} else {
		st->pc++;
	}
}

static void op_bne(struct STATE_6502 *st)
{
	if (st->f&FLAG_Z) {
		st->pc++;
	} else {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	}
}


static void op_bpl(struct STATE_6502 *st)
{
	if (st->f&FLAG_N) {
		st->pc++;
	} else {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	}
}

static void op_brk(struct STATE_6502 *st)
{
	push_stack_w(st, (word)(st->pc + 1));
	push_stack(st, st->f | FLAG_B);
	st->f|=FLAG_I;
	st->pc=mem_read_word(st, ADDR_IRQ);
}

static void op_bvc(struct STATE_6502 *st)
{
	if (st->f&FLAG_V) {
		st->pc++;
	} else {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	}
}

static void op_bvs(struct STATE_6502 *st)
{
	if (st->f&FLAG_V) {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	} else {
		st->pc++;
	}
}

static void op_clc(struct STATE_6502 *st)
{
	st->f&=~FLAG_C;
}

static void op_cld(struct STATE_6502 *st)
{
	st->f&=~FLAG_D;
}

static void op_cli(struct STATE_6502 *st)
{
	st->f&=~FLAG_I;
}

static void op_clv(struct STATE_6502 *st)
{
	st->f&=~FLAG_V;
}

static void op_cmp(struct STATE_6502 *st)
{
	byte w=mem_read(st->ea, st->sr);
	if (st->a<w) st->f&=~FLAG_C; else st->f|=FLAG_C;
	check_flags_log(st, (byte)(st->a-w));
}

static void op_cpx(struct STATE_6502 *st)
{
	byte w=mem_read(st->ea, st->sr);
	if (st->x<w) st->f&=~FLAG_C; else st->f|=FLAG_C;
	check_flags_log(st, (byte)(st->x-w));
}

static void op_cpy(struct STATE_6502 *st)
{
	byte w=mem_read(st->ea, st->sr);
	if (st->y<w) st->f&=~FLAG_C; else st->f|=FLAG_C;
	check_flags_log(st, (byte)(st->y-w));
}

static void op_dec(struct STATE_6502 *st)
{
	byte b=mem_read(st->ea, st->sr);
	check_flags_log(st, --b);
	mem_write(st->ea,b, st->sr);
}

static void op_dex(struct STATE_6502 *st)
{
	check_flags_log(st, --st->x);
}

static void op_dey(struct STATE_6502 *st)
{
	check_flags_log(st, --st->y);
}

static void op_eor(struct STATE_6502 *st)
{
	check_flags_log(st, st->a^=mem_read(st->ea, st->sr));
}

static void op_inc(struct STATE_6502 *st)
{
	byte b=mem_read(st->ea, st->sr);
	check_flags_log(st, ++b);
	mem_write(st->ea,b, st->sr);
}

static void op_inx(struct STATE_6502 *st)
{
	check_flags_log(st, ++st->x);
}

static void op_iny(struct STATE_6502 *st)
{
	check_flags_log(st, ++st->y);
}

static void op_jmp(struct STATE_6502 *st)
{
	st->pc=st->ea;
}

static void op_jsr(struct STATE_6502 *st)
{
	push_stack_w(st, (word)(st->pc-1));
	st->pc=st->ea;
}

static void op_lda(struct STATE_6502 *st)
{
	check_flags_log(st, st->a=mem_read(st->ea, st->sr));
}

static void op_ldx(struct STATE_6502 *st)
{
	check_flags_log(st, st->x=mem_read(st->ea, st->sr));
}

static void op_ldy(struct STATE_6502 *st)
{
	check_flags_log(st, st->y=mem_read(st->ea, st->sr));
}

static void op_lsr_acc(struct STATE_6502 *st)
{
	st->f&=~(FLAG_N|FLAG_Z|FLAG_C);
	if (st->a&1) st->f|=FLAG_C;
	st->a>>=1;
	if (!st->a) st->f|=FLAG_Z;
}

static void op_lsr_mem(struct STATE_6502 *st)
{
	byte d=mem_read(st->ea, st->sr);
	st->f&=~(FLAG_N|FLAG_Z|FLAG_C);
	if (d&1) st->f|=FLAG_C;
	d>>=1;
	if (!d) st->f|=FLAG_Z;
	mem_write(st->ea,d, st->sr);
}

static void op_nop(struct STATE_6502 *st)
{
}

static void op_ora(struct STATE_6502 *st)
{
	check_flags_log(st, st->a|=mem_read(st->ea, st->sr));
}

static void op_pha(struct STATE_6502 *st)
{
	push_stack(st, st->a);
}

static void op_php(struct STATE_6502 *st)
{
	push_stack(st, st->f);
}

static void op_pla(struct STATE_6502 *st)
{
	check_flags_log(st, st->a=pop_stack(st));
}

static void op_plp(struct STATE_6502 *st)
{
	st->f=pop_stack(st)|FLAG_1;
}

static void op_rol_acc(struct STATE_6502 *st)
{
	word b=st->a;
	b<<=1;
	if (st->f&FLAG_C) b|=1;
	if (b&0x100) st->f|=FLAG_C; else st->f&=~FLAG_C;
//	puts("ror_acc");
	st->a=(byte)b;
	check_flags_log(st, st->a);
}

static void op_rol_mem(struct STATE_6502 *st)
{
	word b=mem_read(st->ea, st->sr);
	b<<=1;
	if (st->f&FLAG_C) b|=1;
	if (b&0x100) st->f|=FLAG_C; else st->f&=~FLAG_C;
//	printf("rol_mem %x\n",st->ea);
	mem_write(st->ea,(byte)b, st->sr);
	check_flags_log(st, (byte)b);
}


static void op_ror_acc(struct STATE_6502 *st)
{
	word b=st->a;
	if (st->f&FLAG_C) b|=0x100;
	if (b&1) st->f|=FLAG_C; else st->f&=~FLAG_C;
	b>>=1;
//	puts("ror_acc");
	st->a=(byte)b;
	check_flags_log(st, st->a);
}

static void op_ror_mem(struct STATE_6502 *st)
{
	word b=mem_read(st->ea, st->sr);
	if (st->f&FLAG_C) b|=0x100;
	if (b&1) st->f|=FLAG_C; else st->f&=~FLAG_C;
	b>>=1;
//	printf("ror_mem %x\n",st->ea);
	mem_write(st->ea,(byte)b, st->sr);
	check_flags_log(st, (byte)b);
}

static void op_rti(struct STATE_6502 *st)
{
	st->f=pop_stack(st);
	st->pc=pop_stack_w(st);
}

static void op_rts(struct STATE_6502 *st)
{
//	word l=st->pc;
        st->pc=pop_stack_w(st)+1;
//	printf("rts: %x=>%x\n",l-1,st->pc);
}


static void op_sbc(struct STATE_6502 *st)
{
	word b=mem_read(st->ea, st->sr);
	word w;
	byte b1,b2,b3;

	w=(word)st->a-b;
	if (!(st->f&FLAG_C)) --w;
	check_flags_log(st, (byte)w);

	if (st->f&FLAG_D) {
		word t = (st->a & 0x0F) - (b & 0x0F);
		if (!(st->f & FLAG_C)) -- t;
		t &= 0x1F;
		if (t > 9) {
			w -= 0x10 - 10;
		}
		if (w&0x80) st->f |= FLAG_N; else st->f &= ~FLAG_N;
		if ((w&0x1F0) > 0x90) {
			w -= 0x100 - 0xA0;
		}
//		logprint(0,"sbc decimal: %02x-%02x-%x=%02x",st->a,b,(st->f&FLAG_C)?0:1,w&0xFF);
	}
	b1=st->a&0x80;
	b2=b&0x80;
	b3=(st->f&FLAG_N)?0x80:0;
	if ((b1^b2)&(b1^b3)&0x80) st->f|=FLAG_V; else st->f&=~FLAG_V;
//	logprint(0, "sbc binary: %x-%x=%x\n",st->a,b,w);
	if (w&0xFF00) st->f&=~FLAG_C; else st->f|=FLAG_C;
/*	if (st->f & FLAG_D) {
		printf("sbc: %02X - %02X = %02X (C=%i, V=%i, N=%i, Z=%i)\n",
			st->a, b, w & 0xFF,
			(st->f&FLAG_C)?1:0,
			(st->f&FLAG_V)?1:0,
			(st->f&FLAG_N)?1:0,
			(st->f&FLAG_Z)?1:0);
	}*/
	st->a=(byte)w;
}

static void op_sec(struct STATE_6502 *st)
{
	st->f|=FLAG_C;
}

static void op_sed(struct STATE_6502 *st)
{
	st->f|=FLAG_D;
//	abort(struct STATE_6502 *st);
}

static void op_sei(struct STATE_6502 *st)
{
	st->f|=FLAG_I;
}

static void op_sta(struct STATE_6502 *st)
{
	mem_write(st->ea,st->a, st->sr);
}

static void op_stx(struct STATE_6502 *st)
{
	mem_write(st->ea,st->x, st->sr);
}

static void op_sty(struct STATE_6502 *st)
{
	mem_write(st->ea,st->y, st->sr);
}

static void op_tax(struct STATE_6502 *st)
{
	check_flags_log(st, st->x=st->a);
}

static void op_tay(struct STATE_6502 *st)
{
	check_flags_log(st, st->y=st->a);
}

static void op_tya(struct STATE_6502 *st)
{
	check_flags_log(st, st->a=st->y);
}

static void op_tsx(struct STATE_6502 *st)
{
	check_flags_log(st, st->x=st->s);
}

static void op_txa(struct STATE_6502 *st)
{
	check_flags_log(st, st->a=st->x);
}

static void op_txs(struct STATE_6502 *st)
{
	check_flags_log(st, st->s=st->x);
}

////////////////////////////////////////////////////////////

// illegal opcodes from http://www.oxyron.de/html/opcodes02.html
/*
Opcode	imp	imm	zp	zpx 	zpy	izx	izy	abs	abx 	aby	ind	rel	Function 	N	V	B	D	I	Z	C
SLO	 	 	$07	$17 	 	$03	$13	$0F	$1F 	$1B	 	 	{adr}:={adr}*2 A:=A or {adr} 	*	 	 	 	 	*	*
RLA	 	 	$27	$37 	 	$23	$33	$2F	$3F 	$3B	 	 	{adr}:={adr}rol A:=A and {adr} 	*	 	 	 	 	*	*
SRE	 	 	$47	$57 	 	$43	$53	$4F	$5F 	$5B	 	 	{adr}:={adr}/2 A:=A exor {adr} 	*	 	 	 	 	*	*
RRA	 	 	$67	$77 	 	$63	$73	$6F	$7F 	$7B	 	 	{adr}:={adr}ror A:=A adc {adr} 	*	*	 	 	 	*	*
SAX	 	 	$87	  	$97	$83	 	$8F	  	 	 	 	{adr}:=A&X 	 	 	 	 	 	 	 
LAX	 	 	$A7	  	$B7	$A3	$B3	$AF	  	$BF	 	 	A,X:={adr} 	*	 	 	 	 	*	 
DCP	 	 	$C7	$D7 	 	$C3	$D3	$CF	$DF 	$DB	 	 	{adr}:={adr}-1 A-{adr} 	*	 	 	 	 	*	*
ISC	 	 	$E7	$F7 	 	$E3	$F3	$EF	$FF 	$FB	 	 	{adr}:={adr}+1 A:=A-{adr} 	*	*	 	 	 	*	*
ANC	 	$0B	 	  	 	 	 	 	  	 	 	 	A:=A&#{imm} 	*	 	 	 	 	*	*
ANC	 	$2B	 	  	 	 	 	 	  	 	 	 	A:=A&#{imm} 	*	 	 	 	 	*	*
ALR	 	$4B	 	  	 	 	 	 	  	 	 	 	A:=(A&#{imm})*2 	*	 	 	 	 	*	*
ARR	 	$6B	 	  	 	 	 	 	  	 	 	 	A:=(A&#{imm})/2 	*	*	 	 	 	*	*
XAA?	 	$8B	 	  	 	 	 	 	  	 	 	 	A:=X&#{imm} 	*	 	 	 	 	*	 
LAX?	 	$AB	 	  	 	 	 	 	  	 	 	 	A,X:=#{imm} 	*	 	 	 	 	*	 
AXS	 	$CB	 	  	 	 	 	 	  	 	 	 	X:=A&X-#{imm} 	*	 	 	 	 	*	*
SBC	 	$EB	 	  	 	 	 	 	  	 	 	 	A:=A-#{imm} 	*	*	 	 	 	*	*
AHXü	 	 	 	  	 	 	$93	 	  	$9F	 	 	{adr}:=A&X&H 	 	 	 	 	 	 	 
SHYü	 	 	 	  	 	 	 	 	$9C 	 	 	 	{adr}:=Y&H 	 	 	 	 	 	 	 
SHXü	 	 	 	  	 	 	 	 	  	$9E	 	 	{adr}:=X&H 	 	 	 	 	 	 	 
TASü	 	 	 	  	 	 	 	 	  	$9B	 	 	S:=A&X {adr}:=S&H 	 	 	 	 	 	 	 
LAS	 	 	 	  	 	 	 	 	  	$BB	 	 	A,X,S:={adr}&S 	*	 	 	 	 	*	 
*/

static void op_slo(struct STATE_6502 *st) // aso
{
	op_asl_mem(st);
	op_ora(st);
}

static void op_rla(struct STATE_6502 *st)
{
	op_rol_mem(st);
	op_and(st);
}

static void op_sre(struct STATE_6502 *st)
{
	op_lsr_mem(st);
	op_eor(st);
}

static void op_rra(struct STATE_6502 *st)
{
	op_ror_mem(st);
	op_adc(st);
}

static void op_sax(struct STATE_6502 *st)
{
	mem_write(st->ea,st->a&st->x, st->sr);
}


static void op_dcp(struct STATE_6502 *st)
{
	op_dec(st);
	op_cmp(st);
}

static void op_isc(struct STATE_6502 *st)
{
	op_inc(st);
	op_sbc(st);
}

static void op_anc(struct STATE_6502 *st)
{
	op_and(st);
	if (st->f&FLAG_N) st->f |= FLAG_C;
}


static void op_alr(struct STATE_6502 *st)
{
	op_and(st);
	op_lsr_acc(st);
}


static void op_arr(struct STATE_6502 *st)
{
	op_and(st);
	op_ror_acc(st);
}


static void op_xaa(struct STATE_6502 *st)
{
	op_txa(st);
	op_and(st);
}


static void op_lax(struct STATE_6502 *st)
{
	op_lda(st);
	op_tax(st);
}


static void op_axs(struct STATE_6502 *st)
{
	word w;
	w=st->a-mem_read(st->ea, st->sr);
	if (w&0xFF00) st->f&=~FLAG_C; else st->f|=FLAG_C;
	st->x=(byte)w;
	check_flags_log(st, st->x);
}

static void op_ahx(struct STATE_6502 *st)
{
	byte b = st->a & st->x & (st->ea>>7);
	mem_write(st->ea, b, st->sr);
	check_flags_log(st, b);
}

static void op_shy(struct STATE_6502 *st)
{
	byte b = st->y & (st->ea>>7);
	mem_write(st->ea, b, st->sr);
	check_flags_log(st, b);
}

static void op_shx(struct STATE_6502 *st)
{
	byte b = st->x & (st->ea>>7);
	mem_write(st->ea, b, st->sr);
	check_flags_log(st, b);
}

static void op_tas(struct STATE_6502 *st)
{
	byte b;
	st->s = st->x & st->a;
	b = st->s & (st->ea>>7);
	mem_write(st->ea, b, st->sr);
}

static void op_las(struct STATE_6502 *st)
{
	byte b = mem_read(st->ea, st->sr);
	check_flags_log(st, st->a = st->x = st->s = b & st->s);
}

////////////////////////////////////////////////////////////

#define ea_get_impl 0
#define ea_get_rel 0
#define ea_get__acc 0


#define MAKE_COMMAND(cmd,addr,ticks) { #addr, #cmd, ea_get_##addr, ea_show_##addr, op_##cmd,ticks, 0 }
#define MAKE_COMMAND_ILL(cmd,addr,ticks) { #addr"*", #cmd, ea_get_##addr, ea_show_##addr, op_##cmd,ticks, CMD_ILL  }
static struct CMD_6502 cmds[256]=
{
  MAKE_COMMAND(brk,impl,8),
  MAKE_COMMAND(ora,indx,6),       //01
  MAKE_COMMAND_ILL(hlt,impl,2),       //02 KIL
  MAKE_COMMAND_ILL(slo,indx,8),       //03
  MAKE_COMMAND_ILL(nop,zp,3),         //04
  MAKE_COMMAND(ora,zp,3),         //05
  MAKE_COMMAND(asl_mem,zp,5),     //06
  MAKE_COMMAND_ILL(slo,zp,5),         //07
  MAKE_COMMAND(php,impl,3),       //08
  MAKE_COMMAND(ora,imm,2),        //09
  MAKE_COMMAND(asl_acc,_acc,2),   //0A
  MAKE_COMMAND_ILL(anc,imm,2),        //0B
  MAKE_COMMAND_ILL(nop,abs,4),        //0C
  MAKE_COMMAND(ora,abs,4),        //0D
  MAKE_COMMAND(asl_mem,abs,6),    //0E
  MAKE_COMMAND_ILL(slo,abs,6),        //0F

  MAKE_COMMAND(bpl,rel,2),        //10
  MAKE_COMMAND(ora,indy,5),       //11
  MAKE_COMMAND_ILL(hlt,impl,2),       //12 KIL
  MAKE_COMMAND_ILL(slo,indy,8),       //13
  MAKE_COMMAND_ILL(nop,zpx,4),        //14
  MAKE_COMMAND(ora,zpx,4),        //15
  MAKE_COMMAND(asl_mem,zpx,6),    //16
  MAKE_COMMAND_ILL(slo,zpx,6),        //17
  MAKE_COMMAND(clc,impl,2),       //18
  MAKE_COMMAND(ora,absy,4),       //19
  MAKE_COMMAND_ILL(nop,impl,2),       //1A
  MAKE_COMMAND_ILL(slo,absy,7),       //1B
  MAKE_COMMAND_ILL(nop,absx,4),       //1C
  MAKE_COMMAND(ora,absx,4),       //1D
  MAKE_COMMAND(asl_mem,absx,7),   //1E
  MAKE_COMMAND_ILL(slo,absx,7),       //1F

  MAKE_COMMAND(jsr,abs,6),        //20
  MAKE_COMMAND(and,indx,6),       //21
  MAKE_COMMAND_ILL(hlt,impl,2),       //22 KIL
  MAKE_COMMAND_ILL(rla,indx,8),       //23
  MAKE_COMMAND(bit,zp,3),         //24
  MAKE_COMMAND(and,zp,3),         //25
  MAKE_COMMAND(rol_mem,zp,5),     //26
  MAKE_COMMAND_ILL(rla,zp,5),         //27
  MAKE_COMMAND(plp,impl,4),       //28
  MAKE_COMMAND(and,imm,2),        //29
  MAKE_COMMAND(rol_acc,_acc,2),   //2A
  MAKE_COMMAND_ILL(anc,imm,2),        //2B
  MAKE_COMMAND(bit,abs,4),        //2C
  MAKE_COMMAND(and,abs,4),        //2D
  MAKE_COMMAND(rol_mem,abs,6),    //2E
  MAKE_COMMAND_ILL(rla,abs,6),        //2F

  MAKE_COMMAND(bmi,rel,2),        //30
  MAKE_COMMAND(and,indy,5),       //31
  MAKE_COMMAND_ILL(hlt,impl,2),       //32 KIL
  MAKE_COMMAND_ILL(rla,indy,8),             //33
  MAKE_COMMAND_ILL(nop,zpx,4),              //34
  MAKE_COMMAND(and,zpx,4),        //35
  MAKE_COMMAND(rol_mem,zpx,6),        //36
  MAKE_COMMAND_ILL(rla,zpx,6),              //37
  MAKE_COMMAND(sec,impl,2),       //38
  MAKE_COMMAND(and,absy,4),       //39
  MAKE_COMMAND_ILL(nop,impl,2),              //3A
  MAKE_COMMAND_ILL(rla,absy,7),              //3B
  MAKE_COMMAND_ILL(nop,absx,4),              //3C
  MAKE_COMMAND(and,absx,3),       //3D
  MAKE_COMMAND(rol_mem,absx,7),       //3E
  MAKE_COMMAND_ILL(rla,absx,7),              //3F

  MAKE_COMMAND(rti,impl,6),       //40
  MAKE_COMMAND(eor,indx,6),       //41
  MAKE_COMMAND_ILL(hlt,impl,2),       //42 KIL
  MAKE_COMMAND_ILL(sre,indx,8),              //43
  MAKE_COMMAND_ILL(nop,zp,3),              //44
  MAKE_COMMAND(eor,zp,3),         //45
  MAKE_COMMAND(lsr_mem,zp,5),         //46
  MAKE_COMMAND_ILL(sre,zp,5),              //47
  MAKE_COMMAND(pha,impl,3),       //48
  MAKE_COMMAND(eor,imm,2),        //49
  MAKE_COMMAND(lsr_acc,_acc,2),        //4A
  MAKE_COMMAND_ILL(alr,imm,2),              //4B
  MAKE_COMMAND(jmp,abs,3),        //4C
  MAKE_COMMAND(eor,abs,4),        //4D
  MAKE_COMMAND(lsr_mem,abs,6),        //4E
  MAKE_COMMAND_ILL(sre,abs,6),              //4F

  MAKE_COMMAND(bvc,rel,2),        //50
  MAKE_COMMAND(eor,indy,5),       //51
  MAKE_COMMAND_ILL(hlt,impl,2),       //52 KIL
  MAKE_COMMAND_ILL(sre,indy,8),              //53
  MAKE_COMMAND_ILL(nop,zpx,4),              //54
  MAKE_COMMAND(eor,zpx,4),        //55
  MAKE_COMMAND(lsr_mem,zpx,6),        //56
  MAKE_COMMAND_ILL(sre,zpx,6),              //57
  MAKE_COMMAND(cli,impl,2),       //58
  MAKE_COMMAND(eor,absy,4),       //59
  MAKE_COMMAND_ILL(nop,impl,2),              //5A
  MAKE_COMMAND_ILL(sre,absy,7),              //5B
  MAKE_COMMAND_ILL(nop,absx,4),              //5C
  MAKE_COMMAND(eor,absx,4),       //5D
  MAKE_COMMAND(lsr_mem,absx,7),       //5E
  MAKE_COMMAND_ILL(sre,absx,7),              //5F

  MAKE_COMMAND(rts,impl,6),       //60
  MAKE_COMMAND(adc,indx,6),       //61
  MAKE_COMMAND_ILL(hlt,impl,2),       //62 KIL
  MAKE_COMMAND_ILL(rra,indx,8),              //63
  MAKE_COMMAND_ILL(nop,zp,3),              //64
  MAKE_COMMAND(adc,zp,3),         //65
  MAKE_COMMAND(ror_mem,zp,5),         //66
  MAKE_COMMAND_ILL(rra,zp,5),              //67
  MAKE_COMMAND(pla,impl,4),       //68
  MAKE_COMMAND(adc,imm,2),        //69
  MAKE_COMMAND(ror_acc,_acc,2),        //6A
  MAKE_COMMAND_ILL(arr,imm,2),              //6B
  MAKE_COMMAND(jmp,ind,5),        //6C
  MAKE_COMMAND(adc,abs,4),        //6D
  MAKE_COMMAND(ror_mem,abs,6),        //6E
  MAKE_COMMAND_ILL(rra,abs,6),              //6F

  MAKE_COMMAND(bvs,rel,2),        //70
  MAKE_COMMAND(adc,indy,5),       //71
  MAKE_COMMAND_ILL(hlt,impl,2),       //72 KIL
  MAKE_COMMAND_ILL(rra,indy,8),              //73
  MAKE_COMMAND_ILL(nop,zpx,4),              //74
  MAKE_COMMAND(adc,zpx,4),        //75
  MAKE_COMMAND(ror_mem,zpx,6),        //76
  MAKE_COMMAND_ILL(rra,zpx,6),              //77
  MAKE_COMMAND(sei,impl,2),       //78
  MAKE_COMMAND(adc,absy,4),       //79
  MAKE_COMMAND_ILL(nop,impl,2),              //7A
  MAKE_COMMAND_ILL(rra,absy,7),              //7B
  MAKE_COMMAND_ILL(nop,absx,4),              //7C
  MAKE_COMMAND(adc,absx,4),       //7D
  MAKE_COMMAND(ror_mem,absx,7),       //7E
  MAKE_COMMAND_ILL(rra,absx,7),              //7F

  MAKE_COMMAND_ILL(nop,imm,2),              //80
  MAKE_COMMAND(sta,indx,6),       //81
  MAKE_COMMAND_ILL(nop,imm,2),       //82
  MAKE_COMMAND_ILL(sax,indx,6),              //83
  MAKE_COMMAND(sty,zp,3),         //84
  MAKE_COMMAND(sta,zp,3),         //85
  MAKE_COMMAND(stx,zp,3),         //86
  MAKE_COMMAND_ILL(sax,zp,3),              //87
  MAKE_COMMAND(dey,impl,2),       //88
  MAKE_COMMAND_ILL(nop,imm,2),              //89
  MAKE_COMMAND(txa,impl,2),       //8A
  MAKE_COMMAND_ILL(xaa,imm,2),              //8B
  MAKE_COMMAND(sty,abs,4),        //8C
  MAKE_COMMAND(sta,abs,4),        //8D
  MAKE_COMMAND(stx,abs,4),        //8E
  MAKE_COMMAND_ILL(sax,abs,4),              //8F

  MAKE_COMMAND(bcc,rel,2),        //90
  MAKE_COMMAND(sta,indy,6),       //91
  MAKE_COMMAND_ILL(hlt,impl,2),       //92 KIL
  MAKE_COMMAND_ILL(ahx,indy,6),              //93
  MAKE_COMMAND(sty,zpx,4),        //94
  MAKE_COMMAND(sta,zpx,4),        //95
  MAKE_COMMAND(stx,zpy,4),        //96
  MAKE_COMMAND_ILL(sax,zpy,4),              //97
  MAKE_COMMAND(tya,impl,2),       //98
  MAKE_COMMAND(sta,absy,5),       //99
  MAKE_COMMAND(txs,impl,2),       //9A
  MAKE_COMMAND_ILL(tas,absy,5),              //9B
  MAKE_COMMAND_ILL(shy,absx,5),              //9C
  MAKE_COMMAND(sta,absx,5),       //9D
  MAKE_COMMAND_ILL(shx,absy,5), 		//9E
  MAKE_COMMAND_ILL(ahx,absy,5),              //9F

  MAKE_COMMAND(ldy,imm,2),        //A0
  MAKE_COMMAND(lda,indx,6),       //A1
  MAKE_COMMAND(ldx,imm,2),        //A2
  MAKE_COMMAND_ILL(lax,indx,6),              //A3
  MAKE_COMMAND(ldy,zp,3),         //A4
  MAKE_COMMAND(lda,zp,3),         //A5
  MAKE_COMMAND(ldx,zp,3),         //A6
  MAKE_COMMAND_ILL(lax,zp,3),              //A7
  MAKE_COMMAND(tay,impl,2),       //A8
  MAKE_COMMAND(lda,imm,2),        //A9
  MAKE_COMMAND(tax,impl,2),       //AA
  MAKE_COMMAND_ILL(lax,imm,2),              //AB
  MAKE_COMMAND(ldy,abs,4),        //AC
  MAKE_COMMAND(lda,abs,4),        //AD
  MAKE_COMMAND(ldx,abs,4),        //AE
  MAKE_COMMAND_ILL(lax,abs,4),              //AF

  MAKE_COMMAND(bcs,rel,2),        //B0
  MAKE_COMMAND(lda,indy,5),       //B1
  MAKE_COMMAND_ILL(hlt,impl,2),       //B2 KIL
  MAKE_COMMAND_ILL(lax,indy,5),              //B3
  MAKE_COMMAND(ldy,zpx,4),        //B4
  MAKE_COMMAND(lda,zpx,4),        //B5
  MAKE_COMMAND(ldx,zpy,4),        //B6
  MAKE_COMMAND_ILL(lax,zpy,4),              //B7
  MAKE_COMMAND(clv,impl,2),       //B8
  MAKE_COMMAND(lda,absy,4),       //B9
  MAKE_COMMAND(tsx,impl,2),       //BA
  MAKE_COMMAND_ILL(las,absy,4),              //BB
  MAKE_COMMAND(ldy,absx,4),       //BC
  MAKE_COMMAND(lda,absx,4),       //BD
  MAKE_COMMAND(ldx,absy,4),       //BE
  MAKE_COMMAND_ILL(lax,absy,4),              //BF

  MAKE_COMMAND(cpy,imm,2),        //C0
  MAKE_COMMAND(cmp,indx,6),       //C1
  MAKE_COMMAND_ILL(nop,imm,2),       //C2
  MAKE_COMMAND_ILL(dcp,indx,8),              //C3
  MAKE_COMMAND(cpy,zp,3),         //C4
  MAKE_COMMAND(cmp,zp,3),         //C5
  MAKE_COMMAND(dec,zp,5),         //C6
  MAKE_COMMAND_ILL(dcp,zp,5),              //C7
  MAKE_COMMAND(iny,impl,2),       //C8
  MAKE_COMMAND(cmp,imm,2),        //C9
  MAKE_COMMAND(dex,impl,2),       //CA
  MAKE_COMMAND_ILL(axs,imm,2),              //CB
  MAKE_COMMAND(cpy,abs,4),        //CC
  MAKE_COMMAND(cmp,abs,4),        //CD
  MAKE_COMMAND(dec,abs,6),        //CE
  MAKE_COMMAND_ILL(dcp,abs,6),              //CF

  MAKE_COMMAND(bne,rel,2),        //D0
  MAKE_COMMAND(cmp,indy,5),       //D1
  MAKE_COMMAND_ILL(hlt,impl,2),       //D2 KIL
  MAKE_COMMAND_ILL(dcp,indy,8),              //D3
  MAKE_COMMAND_ILL(nop,zpx,4),              //D4
  MAKE_COMMAND(cmp,zpx,4),        //D5
  MAKE_COMMAND(dec,zpx,6),        //D6
  MAKE_COMMAND_ILL(dcp,zpx,6),              //D7
  MAKE_COMMAND(cld,impl,2),       //D8
  MAKE_COMMAND(cmp,absy,4),       //D9
  MAKE_COMMAND_ILL(nop,impl,2),              //DA
  MAKE_COMMAND_ILL(dcp,absy,7),              //DB
  MAKE_COMMAND_ILL(nop,absx,4),              //DC
  MAKE_COMMAND(cmp,absx,4),       //DD
  MAKE_COMMAND(dec,absx,7),       //DE
  MAKE_COMMAND_ILL(dcp,absx,7),              //DF

  MAKE_COMMAND(cpx,imm,2),        //E0
  MAKE_COMMAND(sbc,indx,4),       //E1
  MAKE_COMMAND_ILL(nop,imm,2),       //E2
  MAKE_COMMAND_ILL(isc,indx,8),              //E3
  MAKE_COMMAND(cpx,zp,3),         //E4
  MAKE_COMMAND(sbc,zp,3),         //E5
  MAKE_COMMAND(inc,zp,5),         //E6
  MAKE_COMMAND_ILL(isc,zp,5),              //E7
  MAKE_COMMAND(inx,impl,2),       //E8
  MAKE_COMMAND(sbc,imm,2),        //E9
  MAKE_COMMAND(nop,impl,2),       //EA
  MAKE_COMMAND_ILL(sbc,imm,2),              //EB
  MAKE_COMMAND(cpx,abs,4),        //EC
  MAKE_COMMAND(sbc,abs,4),        //ED
  MAKE_COMMAND(inc,abs,6),        //EE
  MAKE_COMMAND_ILL(isc,abs,6),              //EF

  MAKE_COMMAND(beq,rel,2),        //F0
  MAKE_COMMAND(sbc,indy,5),       //F1
  MAKE_COMMAND_ILL(hlt,impl,2),       //F2 KIL
  MAKE_COMMAND_ILL(isc,indy,8),              //F3
  MAKE_COMMAND_ILL(nop,zpx,4),              //F4
  MAKE_COMMAND(sbc,zpx,4),        //F5
  MAKE_COMMAND(inc,zpx,6),        //F6
  MAKE_COMMAND_ILL(isc,zpx,6),              //F7
  MAKE_COMMAND(sed,impl,2),       //F8
  MAKE_COMMAND(sbc,absy,4),       //F9
  MAKE_COMMAND_ILL(nop,impl,2),              //FA
  MAKE_COMMAND_ILL(isc,absy,7),              //FB
  MAKE_COMMAND_ILL(nop,absx,4),              //FC
  MAKE_COMMAND(sbc,absx,4),       //FD
  MAKE_COMMAND(inc,absx,7),       //FE
  MAKE_COMMAND_ILL(isc,absx,7)              //FF
};

int cpu_debug = 0;
static void dumpregs(struct CPU_STATE*cs);

static void op_disassemble(struct STATE_6502*st, struct CMD_6502*c)
{
	fprintf(stdout, "%04X\t%s\t", st->pc - 1, c->cmd_name);
	c->printadr(st, stdout);
	fprintf(stdout, "\n");
}

static int exec_6502(struct CPU_STATE*cs)
{
	struct STATE_6502*st = cs->state;
	struct CMD_6502*c;
	int b;
	int n = 0;


//	if (st->pc == 0x6A18) cpu_debug = 1;
	if (cpu_debug) dumpregs(cs);

	if (st->ints_req&INT_NMI) {
		push_stack_w(st, (word)(st->pc));
		push_stack(st, st->f & ~FLAG_B);
		st->f|=FLAG_I;
		st->pc=mem_read_word(st, ADDR_NMI);
		st->ints_req&=~INT_NMI;
//		puts("nmi");
		return 7;
	} else 	if (st->ints_req&INT_RESET) {
		st->pc=mem_read_word(st, ADDR_RES);
		st->ints_req&=~INT_RESET;
		st->f=FLAG_Z | FLAG_I | FLAG_1;
//		puts("reset");
		return 6;
	} else if ((st->ints_req&INT_IRQ)&&!(st->f&FLAG_I)) {
		push_stack_w(st, (word)(st->pc));
		push_stack(st, st->f & ~FLAG_B);
		st->f|=FLAG_I;
		st->pc=mem_read_word(st, ADDR_IRQ);
//		st->ints_req&=~INT_IRQ;
//		puts("irq");
		return 7;
	}

	b=fetch_cmd_byte(st);
	c=cmds+b;
	if (!c->cmd) {
		return -1;
	}
	if (cpu_debug) op_disassemble(st, c);
	if ((!cs->undoc) && (c->flags & CMD_ILL)) {
		printf("undocumented command: %02X (%s %s)\n", b, c->cmd_name, c->adr_name);
		return -1;
	}
	st->addt = 0;
	if (c->adr) c->adr(st);
	if (c->cmd) c->cmd(st); else n++;
	if (c->ticks >= 6) st->addt = 0;
	n += c->ticks + st->addt;
//	fprintf(stderr, "n = %i (%i+%i); ", n, c->ticks, st->addt);
	return n;
}

static void intr_6502(struct CPU_STATE*cs, int r)
{
	struct STATE_6502*st = cs->state;
	switch (r) {
	case CPU_INTR_RESET:
	case CPU_INTR_HRESET:
		st->ints_req |= INT_RESET;
		break;
	case CPU_INTR_NMI:
		st->ints_req |= INT_NMI;
		break;
	case CPU_INTR_IRQ:
		st->ints_req |= INT_IRQ;
		break;
	case CPU_INTR_NOIRQ:
		st->ints_req &= ~INT_IRQ;
		break;
	case CPU_INTR_NONMI:
		st->ints_req &= ~INT_NMI;
		break;
	}
}

static void free_6502(struct CPU_STATE*cs)
{
	free(cs->state);
}


static int save_6502(struct CPU_STATE*cs, OSTREAM *out)
{
	struct STATE_6502_SAV sav;
	struct STATE_6502*st = cs->state;
	memcpy(&sav, st, sizeof(sav));
	WRITE_FIELD(out, sav);
	return 0;
}

static int load_6502(struct CPU_STATE*cs, ISTREAM *in)
{
	struct STATE_6502_SAV st0;
	struct STATE_6502*st = cs->state;
	READ_FIELD(in, st0);
	st0.sr = st->sr;
	memcpy(st, &st0, sizeof(st0));
	return 0;
}


static const char*get_flags(byte b)
{
	static const char flags[]="NV1BDIZC";
	static char buf[9]="--------";
	const char *p=flags;
	char *t=buf;
	int i;
	for (i=8;i;i--,p++,t++,b<<=1) if (b&0x80) *t=*p; else *t='-';
	return buf;

}


static void dumpregs(struct CPU_STATE*cs)
{
	struct STATE_6502*st = cs->state;
	logprint(0,TEXT("%04X-   A=%02X X=%02X Y=%02X P=%02X [%s] S=%02X"),
		st->pc,
		st->a, 	st->x, 	st->y,
		st->f,	get_flags(st->f),
		st->s);
}

static int get_regs(struct STATE_6502*st, struct REGS_6502* regs)
{
	regs->A = st->a;
	regs->X = st->x;
	regs->Y = st->y;
	regs->S = st->s;
	regs->F = st->f;
	regs->PC = st->pc;
	return 1;
}


static int set_regs(struct STATE_6502*st, int mask, const struct REGS_6502* regs)
{
	if (mask & (1<<REG6502_A)) st->a = regs->A;
	if (mask & (1<<REG6502_X)) st->x = regs->X;
	if (mask & (1<<REG6502_Y)) st->y = regs->Y;
	if (mask & (1<<REG6502_S)) st->s = regs->S;
	if (mask & (1<<REG6502_F)) st->f = regs->F;
	if (mask & (1<<REG6502_PC)) st->pc = regs->PC;
	return 1;
}

static int get_reg(struct STATE_6502*st, int reg, void*val)
{
	switch (reg) {
	case REG6502_A:
		*(byte*)val = st->a;
		break;
	case REG6502_X:
		*(byte*)val = st->x;
		break;
	case REG6502_Y:
		*(byte*)val = st->y;
		break;
	case REG6502_S:
		*(byte*)val = st->s;
		break;
	case REG6502_F:
		*(byte*)val = st->f;
		break;
	case REG6502_PC:
		*(word*)val = st->pc;
		break;
	default:
		return -1;
	}
	return 1;
}


static int set_reg(struct STATE_6502*st, int reg, long val)
{
	switch (reg) {
	case REG6502_A:
		st->a = (byte)val;
		break;
	case REG6502_X:
		st->x = (byte)val;
		break;
	case REG6502_Y:
		st->y = (byte)val;
		break;
	case REG6502_S:
		st->s = (byte)val;
		break;
	case REG6502_F:
		st->f = (byte)val;
		break;
	case REG6502_PC:
		st->pc = (word)val;
		break;
	default:
		return -1;
	}
	return 1;
}

int cmd_6502(struct CPU_STATE*cs, int cmd, int data, long param)
{
	struct STATE_6502*st = cs->state;
	switch (cmd) {
	case SYS_COMMAND_DUMPCPUREGS:
		dumpregs(cs);
		return 1;
	case SYS_COMMAND_EXEC:
		st->pc = (word)param;
		return 0;
	case SYS_COMMAND_HRESET:
		st->s = rand();
		break;
	case SYS_COMMAND_GETREGS6502:
		return get_regs(st, (struct REGS_6502*)param);
	case SYS_COMMAND_SETREGS6502:
		return set_regs(st, data, (const struct REGS_6502*)param);
	case SYS_COMMAND_GETREG:
		return get_reg(st, data, (void*)param);
	case SYS_COMMAND_SETREG:
		return set_reg(st, data, param);
	}
	return 0;
}

int init_cpu_6502(struct CPU_STATE*cs)
{
	struct STATE_6502*st;
	st = calloc(1, sizeof(*st));
	st->sr = cs->sr;
	st->f = FLAG_I | FLAG_1;
	st->s = rand();
	st->ints_req = INT_RESET;
	cs->exec_op = exec_6502;
	cs->intr = intr_6502;
	cs->free = free_6502;
	cs->save = save_6502;
	cs->load = load_6502;
	cs->cmd = cmd_6502;
	cs->state = st;
	return 0;
}
