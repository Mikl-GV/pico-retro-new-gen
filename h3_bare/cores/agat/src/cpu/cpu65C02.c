/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	cpu65C02 - emulation of microprocessor 65C02
*/

#include "cpuint.h"

#define STACK_START 0x100

#define ADDR_IRQ	0xFFFE
#define ADDR_RES	0xFFFC
#define ADDR_NMI	0xFFFA


struct STATE_65C02
{
	struct SYS_RUN_STATE*sr;

	byte a,x,y,s,f;
	word pc;

	word ea;
	byte ints_req;
	int  addt;
};

#define CMD_ILL		1
#define CMD_NEW		2

#include <stdio.h>

static void dumpregs(struct STATE_65C02*st);

struct CMD_65C02
{
	char*adr_name, *cmd_name;
	void(*adr)(struct STATE_65C02*st);
	void(*printadr)(struct STATE_65C02*st, FILE*out);
	void(*cmd)(struct STATE_65C02*st);
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


static void push_stack(struct STATE_65C02 *st, byte b);
static byte pop_stack(struct STATE_65C02 *st);
static byte fetch_cmd_byte(struct STATE_65C02 *st);
static word mem_read_word(struct STATE_65C02 *st, word addr);

static void push_stack_w(struct STATE_65C02 *st, word w);
static word pop_stack_w(struct STATE_65C02 *st);
static word fetch_cmd_word(struct STATE_65C02 *st);
static void mem_write_word_page(struct STATE_65C02 *st, word addr,word d);
static word mem_read_word_page(struct STATE_65C02 *st, word addr);



static void push_stack(struct STATE_65C02 *st, byte b)
{
//	printf("push_stack %02X\n", b);
	mem_write((word)(STACK_START+st->s),b, st->sr);
	--st->s;
}


static byte  pop_stack(struct STATE_65C02 *st)
{
	byte r;
	++st->s;
	r = mem_read((word)(STACK_START+st->s), st->sr);
//	printf("pop_stack = %02X\n", r);
	return r;
}

byte fetch_cmd_byte(struct STATE_65C02 *st)
{
	return mem_read(st->pc++, st->sr);
}

static word mem_read_word(struct STATE_65C02 *st, word addr)
{
	return (word)mem_read(addr,st->sr)|(((word)mem_read((word)(addr+1),st->sr))<<8);
}




static void push_stack_w(struct STATE_65C02 *st, word w)
{
//	printf("push_stack_w %02X\n", w);
	st->s--;
	mem_write_word_page(st, (word)(STACK_START+st->s),w);
	st->s--;
}

static word pop_stack_w(struct STATE_65C02 *st)
{
	word r;
	st->s++;
	r=mem_read_word_page(st, (word)(STACK_START+st->s));
	st->s++;
//	printf("pop_stack_w = %02X\n", r);
	return r;
}

static word fetch_cmd_word(struct STATE_65C02 *st)
{
	byte b=fetch_cmd_byte(st);
	return b|(fetch_cmd_byte(st)<<8);
}

static word mem_read_word_page(struct STATE_65C02 *st,word addr)
{
	word res=mem_read(addr, st->sr);
	(*(byte*)&addr)++;
	return res|(mem_read(addr, st->sr)<<8);
}


static void mem_write_word_page(struct STATE_65C02 *st,word addr,word d)
{
	mem_write(addr,(byte)d, st->sr);
	(*(byte*)&addr)++;
	mem_write(addr,(byte)(d>>8), st->sr);
}

static void check_flags_log(struct STATE_65C02 *st, byte b)
{
	st->f&=~(FLAG_Z|FLAG_N);
	if (!b) st->f|=FLAG_Z;
	if (b&0x80) st->f|=FLAG_N;
}

/////////////////////////////////////////////////////////

static word add_timing_page(struct STATE_65C02*st, word a, int ofs)
{
	word r = a + ofs;
	if ((r&~0xFF) != (a&~0xFF)) ++st->addt;
	return r;
}

static void ea_get_imm(struct STATE_65C02 *st)
{
	st->ea=st->pc;
	st->pc++;
}

static void ea_get_abs(struct STATE_65C02*st)
{
	st->ea=fetch_cmd_word(st);
}

static void ea_get_zp(struct STATE_65C02*st)
{
	st->ea=fetch_cmd_byte(st);
}

static void ea_get_zpx(struct STATE_65C02*st)
{
	st->ea=(byte)(fetch_cmd_byte(st)+st->x);
}

static void ea_get_zpy(struct STATE_65C02*st)
{
	st->ea=(byte)(fetch_cmd_byte(st)+st->y);
}

static void ea_get_absx(struct STATE_65C02*st)
{
	word ad=add_timing_page(st, fetch_cmd_word(st), st->x);
	st->ea=ad;
}

static void ea_get_absy(struct STATE_65C02*st)
{
	word ad=add_timing_page(st, fetch_cmd_word(st), st->y);
	st->ea=ad;
}


static void ea_get_ind_zp(struct STATE_65C02*st)
{
	register byte addr=fetch_cmd_byte(st);
	st->ea=mem_read_word_page(st, addr);
}

static void ea_get_indx(struct STATE_65C02*st)
{
	register byte addr=fetch_cmd_byte(st)+st->x;
	st->ea=mem_read_word_page(st, addr);
}

static void ea_get_ind16x(struct STATE_65C02*st)
{
	register word addr=fetch_cmd_word(st)+st->x;
	st->ea=mem_read_word(st, addr);
}

static void ea_get_indy(struct STATE_65C02*st)
{
	register byte addr=fetch_cmd_byte(st);
	st->ea=add_timing_page(st, mem_read_word_page(st, addr), st->y);
}

static void ea_get_ind(struct STATE_65C02*st)
{
	register word addr=fetch_cmd_word(st);
	st->ea=mem_read_word(st, addr);
}
//////////////////////////////////////////////////////

static void ea_show_imm(struct STATE_65C02*st, FILE*out)
{
	fprintf(out,"#%02X",mem_read(st->pc, st->sr));
}

static void ea_show_impl(struct STATE_65C02*st, FILE*out)
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

static word xmem_read_word(struct STATE_65C02*st, word a)
{
	word w;
//	if (a >= 0xBFFF && a < 0xC800) return 0xFFFF;
	st->sr->in_debug = 1;
	w = mem_read_word(st, a);
	st->sr->in_debug = 0;
	return w;
}

static word xmem_read_word_page(struct STATE_65C02*st, word a)
{
	word w;
//	if (a >= 0xC000 && a < 0xC800) return 0xFFFF;
	st->sr->in_debug = 1;
	w = mem_read_word_page(st, a);
	st->sr->in_debug = 0;
	return w;
}


static void ea_show__acc(struct STATE_65C02*st, FILE*out)
{
	fprintf(out," A");
}

static void ea_show_rel(struct STATE_65C02*st, FILE*out)
{
	word a=(signed char)xmem_read(st->pc, st->sr)+(st->pc+1);
	fprintf(out," %04X",a);
}


static void ea_show_abs(struct STATE_65C02*st, FILE*out)
{
	word a=xmem_read_word(st, st->pc);
	fprintf(out," %04X (%02X)",a,xmem_read(a, st->sr));
}

static void ea_show_zp(struct STATE_65C02*st, FILE*out)
{
	byte a=xmem_read(st->pc, st->sr);
	fprintf(out," %02X (%02X)",a,xmem_read(a, st->sr));
}

static void ea_show_zpx(struct STATE_65C02*st, FILE*out)
{
	byte a=xmem_read(st->pc, st->sr);
	fprintf(out," %02X,X (%02X)",a,xmem_read((word)(a+st->x),st->sr));
}

static void ea_show_zpy(struct STATE_65C02*st, FILE*out)
{
	byte a=xmem_read(st->pc, st->sr);
	fprintf(out," %02X,Y (%02X)",a,xmem_read((word)(a+st->y), st->sr));
}

static void ea_show_absx(struct STATE_65C02*st, FILE*out)
{
	word a=xmem_read_word(st, st->pc);
	fprintf(out," %04X,X (%02X)",a,xmem_read((word)(a+st->x), st->sr));
}

static void ea_show_absy(struct STATE_65C02*st, FILE*out)
{
	word a=xmem_read_word(st, st->pc);
	fprintf(out," %04X,Y (%02X)",a,xmem_read((word)(a+st->y),st->sr));
}


static void ea_show_ind_zp(struct STATE_65C02*st, FILE*out)
{
	byte a=xmem_read(st->pc,st->sr);
	word adr = xmem_read_word_page(st, a);
	fprintf(out," (%02X) ([%04X]=%02X)",a,adr,xmem_read(adr, st->sr));
}

static void ea_show_indx(struct STATE_65C02*st, FILE*out)
{
	byte a=xmem_read(st->pc,st->sr);
	word adr = xmem_read_word_page(st, a + st->x);
	fprintf(out," (%02X,X) ([%04X]=%02X)",a,adr,xmem_read(adr, st->sr));
}

static void ea_show_ind16x(struct STATE_65C02*st, FILE*out)
{
	word a=xmem_read_word(st, st->pc);
	word adr = xmem_read_word_page(st, a + st->x);
	fprintf(out," (%04X,X) ([%04X]=%02X)",a,adr,xmem_read(adr, st->sr));
}

static void ea_show_indy(struct STATE_65C02*st, FILE*out)
{
	byte a=xmem_read(st->pc,st->sr);
	word adr = xmem_read_word_page(st, a) + st->y;
	fprintf(out," (%02X),Y ([%04X]=%02X)",a,adr,xmem_read(adr, st->sr));
}

static void ea_show_ind(struct STATE_65C02*st, FILE*out)
{
	word a=xmem_read_word(st,st->pc);
	word adr = xmem_read_word_page(st, a);
	fprintf(out," (%04X) ([%04X]=%02X)",a,adr,xmem_read(adr, st->sr));
}


////////////////////////////////////////////////////////////


static void op_stp(struct STATE_65C02 *st)
{
	st->pc--;
}

static void op_wai(struct STATE_65C02 *st)
{
	st->pc--;
}

static void op_adc(struct STATE_65C02 *st)
{
	byte b=mem_read(st->ea, st->sr);
	word w;
	byte b1,b2,b3;

	w=(word)st->a+b;
	if (st->f&FLAG_C) w++;

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
			st->f|=FLAG_C;
		} else {
			st->f&=~FLAG_C;
		}
//		logprint(0,"adc decimal: %x+%x=%x",st->a,b,w);
	} else {
		if (w&0x100) st->f|=FLAG_C; else st->f&=~FLAG_C;
	}
	b1=st->a&0x80;
	b2=b&0x80;
	b3=w&0x80;
	st->a=(byte)w;
	if (~(b1^b2)&(b1^b3)&0x80) st->f|=FLAG_V; else st->f&=~FLAG_V;
	check_flags_log(st, st->a);
}

static void op_and(struct STATE_65C02 *st)
{
	check_flags_log(st, st->a&=mem_read(st->ea, st->sr));
}

static void op_asl_acc(struct STATE_65C02 *st)
{
	word w=st->a;
	w<<=1;
	st->f&=~(FLAG_Z|FLAG_N|FLAG_C);
	if (w&0x100) st->f|=FLAG_C;
	st->a=(byte)w;
	check_flags_log(st, st->a);
}

static void op_asl_mem(struct STATE_65C02 *st)
{
	word w=mem_read(st->ea, st->sr);
	w<<=1;
	st->f&=~(FLAG_Z|FLAG_N|FLAG_C);
	if (w&0x100) st->f|=FLAG_C;
	mem_write(st->ea,(byte)w, st->sr);
	check_flags_log(st, (byte)w);
}

#define GEN_BBR(nbit) static void op_bbr##nbit(struct STATE_65C02 *st)\
{\
	byte a = fetch_cmd_byte(st);\
	word b = mem_read(a, st->sr);\
	if (b & (1<<nbit)) { \
		st->pc++; \
	} else { \
		st->pc+=(signed char)fetch_cmd_byte(st); \
	} \
}

GEN_BBR(0)
GEN_BBR(1)
GEN_BBR(2)
GEN_BBR(3)
GEN_BBR(4)
GEN_BBR(5)
GEN_BBR(6)
GEN_BBR(7)

#define GEN_BBS(nbit) static void op_bbs##nbit(struct STATE_65C02 *st)\
{\
	byte a = fetch_cmd_byte(st);\
	word b = mem_read(a, st->sr);\
	if (b & (1<<nbit)) { \
		st->pc+=(signed char)fetch_cmd_byte(st); \
	} else { \
		st->pc++; \
	} \
}

GEN_BBS(0)
GEN_BBS(1)
GEN_BBS(2)
GEN_BBS(3)
GEN_BBS(4)
GEN_BBS(5)
GEN_BBS(6)
GEN_BBS(7)


static void op_bcc(struct STATE_65C02 *st)
{
	if (st->f&FLAG_C) {
		st->pc++;
	} else {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	}
}

static void op_bcs(struct STATE_65C02 *st)
{
	if (st->f&FLAG_C) {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	} else {
		st->pc++;
	}
}

static void op_beq(struct STATE_65C02 *st)
{
	if (st->f&FLAG_Z) {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	} else {
		st->pc++;
	}
}

static void op_bit(struct STATE_65C02 *st)
{
	byte b=mem_read(st->ea, st->sr);
	st->f&=~(FLAG_Z|FLAG_N|FLAG_V);
	if (!(st->a&b)) st->f|=FLAG_Z;
	st->f|=(b&(FLAG_N|FLAG_V));
}



static void op_bmi(struct STATE_65C02 *st)
{
	if (st->f&FLAG_N) {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	} else {
		st->pc++;
	}
}

static void op_bne(struct STATE_65C02 *st)
{
	if (st->f&FLAG_Z) {
		st->pc++;
	} else {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	}
}


static void op_bpl(struct STATE_65C02 *st)
{
	if (st->f&FLAG_N) {
		st->pc++;
	} else {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	}
}

static void op_bra(struct STATE_65C02 *st)
{
	st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
}

static void op_brk(struct STATE_65C02 *st)
{
	push_stack_w(st, (word)(st->pc + 1));
	push_stack(st, st->f | FLAG_B);
	st->f|=FLAG_I;
	st->f&=~FLAG_D;
	st->pc=mem_read_word(st, ADDR_IRQ);
}

static void op_bvc(struct STATE_65C02 *st)
{
	if (st->f&FLAG_V) {
		st->pc++;
	} else {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	}
}

static void op_bvs(struct STATE_65C02 *st)
{
	if (st->f&FLAG_V) {
		st->pc= add_timing_page(st, st->pc, (signed char)fetch_cmd_byte(st));
		++st->addt;
	} else {
		st->pc++;
	}
}

static void op_clc(struct STATE_65C02 *st)
{
	st->f&=~FLAG_C;
}

static void op_cld(struct STATE_65C02 *st)
{
	st->f&=~FLAG_D;
}

static void op_cli(struct STATE_65C02 *st)
{
	st->f&=~FLAG_I;
}

static void op_clv(struct STATE_65C02 *st)
{
	st->f&=~FLAG_V;
}

static void op_cmp(struct STATE_65C02 *st)
{
	byte w=mem_read(st->ea, st->sr);
	if (st->a<w) st->f&=~FLAG_C; else st->f|=FLAG_C;
	check_flags_log(st, (byte)(st->a-w));
}

static void op_cpx(struct STATE_65C02 *st)
{
	byte w=mem_read(st->ea, st->sr);
	if (st->x<w) st->f&=~FLAG_C; else st->f|=FLAG_C;
	check_flags_log(st, (byte)(st->x-w));
}

static void op_cpy(struct STATE_65C02 *st)
{
	byte w=mem_read(st->ea, st->sr);
	if (st->y<w) st->f&=~FLAG_C; else st->f|=FLAG_C;
	check_flags_log(st, (byte)(st->y-w));
}

static void op_dec(struct STATE_65C02 *st)
{
	byte b=mem_read(st->ea, st->sr);
	check_flags_log(st, --b);
	mem_write(st->ea,b, st->sr);
}


static void op_dec_acc(struct STATE_65C02 *st)
{
	check_flags_log(st, --st->a);
}

static void op_dex(struct STATE_65C02 *st)
{
	check_flags_log(st, --st->x);
}

static void op_dey(struct STATE_65C02 *st)
{
	check_flags_log(st, --st->y);
}

static void op_eor(struct STATE_65C02 *st)
{
	check_flags_log(st, st->a^=mem_read(st->ea, st->sr));
}

static void op_inc(struct STATE_65C02 *st)
{
	byte b=mem_read(st->ea, st->sr);
	check_flags_log(st, ++b);
	mem_write(st->ea,b, st->sr);
}


static void op_inc_acc(struct STATE_65C02 *st)
{
	check_flags_log(st, ++st->a);
}

static void op_inx(struct STATE_65C02 *st)
{
	check_flags_log(st, ++st->x);
}

static void op_iny(struct STATE_65C02 *st)
{
	check_flags_log(st, ++st->y);
}

static void op_jmp(struct STATE_65C02 *st)
{
	st->pc=st->ea;
}

static void op_jsr(struct STATE_65C02 *st)
{
	push_stack_w(st, (word)(st->pc-1));
	st->pc=st->ea;
}

static void op_lda(struct STATE_65C02 *st)
{
	check_flags_log(st, st->a=mem_read(st->ea, st->sr));
}

static void op_ldx(struct STATE_65C02 *st)
{
	check_flags_log(st, st->x=mem_read(st->ea, st->sr));
}

static void op_ldy(struct STATE_65C02 *st)
{
	check_flags_log(st, st->y=mem_read(st->ea, st->sr));
}

static void op_lsr_acc(struct STATE_65C02 *st)
{
	st->f&=~(FLAG_N|FLAG_Z|FLAG_C);
	if (st->a&1) st->f|=FLAG_C;
	st->a>>=1;
	if (!st->a) st->f|=FLAG_Z;
}

static void op_lsr_mem(struct STATE_65C02 *st)
{
	byte d=mem_read(st->ea, st->sr);
	st->f&=~(FLAG_N|FLAG_Z|FLAG_C);
	if (d&1) st->f|=FLAG_C;
	d>>=1;
	if (!d) st->f|=FLAG_Z;
	mem_write(st->ea,d, st->sr);
}

static void op_nop(struct STATE_65C02 *st)
{
}

static void op_ora(struct STATE_65C02 *st)
{
	check_flags_log(st, st->a|=mem_read(st->ea, st->sr));
}

static void op_pha(struct STATE_65C02 *st)
{
	push_stack(st, st->a);
}

static void op_php(struct STATE_65C02 *st)
{
	push_stack(st, st->f);
}

static void op_phx(struct STATE_65C02 *st)
{
	push_stack(st, st->x);
}

static void op_phy(struct STATE_65C02 *st)
{
	push_stack(st, st->y);
}

static void op_pla(struct STATE_65C02 *st)
{
	check_flags_log(st, st->a=pop_stack(st));
}

static void op_plx(struct STATE_65C02 *st)
{
	st->x=pop_stack(st);
	check_flags_log(st, st->x);
}

static void op_ply(struct STATE_65C02 *st)
{
	st->y=pop_stack(st);
	check_flags_log(st, st->y);
}

static void op_plp(struct STATE_65C02 *st)
{
	st->f=pop_stack(st)|FLAG_1;
}

static void op_rol_acc(struct STATE_65C02 *st)
{
	word b=st->a;
	b<<=1;
	if (st->f&FLAG_C) b|=1;
	if (b&0x100) st->f|=FLAG_C; else st->f&=~FLAG_C;
//	puts("ror_acc");
	st->a=(byte)b;
	check_flags_log(st, st->a);
}

static void op_rol_mem(struct STATE_65C02 *st)
{
	word b=mem_read(st->ea, st->sr);
	b<<=1;
	if (st->f&FLAG_C) b|=1;
	if (b&0x100) st->f|=FLAG_C; else st->f&=~FLAG_C;
//	printf("rol_mem %x\n",st->ea);
	mem_write(st->ea,(byte)b, st->sr);
	check_flags_log(st, (byte)b);
}


static void op_ror_acc(struct STATE_65C02 *st)
{
	word b=st->a;
	if (st->f&FLAG_C) b|=0x100;
	if (b&1) st->f|=FLAG_C; else st->f&=~FLAG_C;
	b>>=1;
//	puts("ror_acc");
	st->a=(byte)b;
	check_flags_log(st, st->a);
}

static void op_ror_mem(struct STATE_65C02 *st)
{
	word b=mem_read(st->ea, st->sr);
	if (st->f&FLAG_C) b|=0x100;
	if (b&1) st->f|=FLAG_C; else st->f&=~FLAG_C;
	b>>=1;
//	printf("ror_mem %x\n",st->ea);
	mem_write(st->ea,(byte)b, st->sr);
	check_flags_log(st, (byte)b);
}

static void op_rti(struct STATE_65C02 *st)
{
	st->f=pop_stack(st);
	st->pc=pop_stack_w(st);
}

#define GEN_RMB(nbit) static void op_rmb##nbit(struct STATE_65C02 *st)\
{\
	word b=mem_read(st->ea, st->sr);\
	mem_write(st->ea, b & ~(1<<nbit), st->sr);\
}

GEN_RMB(0)
GEN_RMB(1)
GEN_RMB(2)
GEN_RMB(3)
GEN_RMB(4)
GEN_RMB(5)
GEN_RMB(6)
GEN_RMB(7)

#define GEN_SMB(nbit) static void op_smb##nbit(struct STATE_65C02 *st)\
{\
	word b=mem_read(st->ea, st->sr);\
	mem_write(st->ea, b | (1<<nbit), st->sr);\
}

GEN_SMB(0)
GEN_SMB(1)
GEN_SMB(2)
GEN_SMB(3)
GEN_SMB(4)
GEN_SMB(5)
GEN_SMB(6)
GEN_SMB(7)


static void op_rts(struct STATE_65C02 *st)
{
//	word l=st->pc;
        st->pc=pop_stack_w(st)+1;
//	printf("rts: %x=>%x\n",l-1,st->pc);
}


static void op_sbc(struct STATE_65C02 *st)
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
			st->f&=~FLAG_C;
		} else {
			st->f|=FLAG_C;
		}
//		logprint(0,"sbc decimal: %02x-%02x-%x=%02x",st->a,b,(st->f&FLAG_C)?0:1,w&0xFF);
	} else {
		if (w&0xFF00) st->f&=~FLAG_C; else st->f|=FLAG_C;
	}
	b1=st->a&0x80;
	b2=b&0x80;
	b3=(st->f&FLAG_N)?0x80:0;
	if ((b1^b2)&(b1^b3)&0x80) st->f|=FLAG_V; else st->f&=~FLAG_V;
//	logprint(0, "sbc binary: %x-%x=%x\n",st->a,b,w);
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

static void op_sec(struct STATE_65C02 *st)
{
	st->f|=FLAG_C;
}

static void op_sed(struct STATE_65C02 *st)
{
	st->f|=FLAG_D;
//	abort(struct STATE_65C02 *st);
}

static void op_sei(struct STATE_65C02 *st)
{
	st->f|=FLAG_I;
}

static void op_sta(struct STATE_65C02 *st)
{
	mem_write(st->ea,st->a, st->sr);
}

static void op_stx(struct STATE_65C02 *st)
{
	mem_write(st->ea,st->x, st->sr);
}

static void op_sty(struct STATE_65C02 *st)
{
	mem_write(st->ea,st->y, st->sr);
}

static void op_stz(struct STATE_65C02 *st)
{
	mem_write(st->ea, 0, st->sr);
}

static void op_tax(struct STATE_65C02 *st)
{
	check_flags_log(st, st->x=st->a);
}

static void op_tay(struct STATE_65C02 *st)
{
	check_flags_log(st, st->y=st->a);
}

static void op_tya(struct STATE_65C02 *st)
{
	check_flags_log(st, st->a=st->y);
}

static void op_tsx(struct STATE_65C02 *st)
{
	check_flags_log(st, st->x=st->s);
}

static void op_txa(struct STATE_65C02 *st)
{
	check_flags_log(st, st->a=st->x);
}

static void op_txs(struct STATE_65C02 *st)
{
	check_flags_log(st, st->s=st->x);
}



static void op_tsb(struct STATE_65C02 *st)
{
	word b=mem_read(st->ea, st->sr);
	if (b & st->a) st->f &= ~FLAG_Z;
	else st->f |= FLAG_Z;
	mem_write(st->ea, b | st->a, st->sr);
}

static void op_trb(struct STATE_65C02 *st)
{
	word b=mem_read(st->ea, st->sr);
	if (b & st->a) st->f &= ~FLAG_Z;
	else st->f |= FLAG_Z;
	mem_write(st->ea, b & ~st->a, st->sr);
}


////////////////////////////////////////////////////////////

#define ea_get_impl 0
#define ea_get_rel 0
#define ea_get__acc 0


#define MAKE_COMMAND(cmd,addr,ticks) { #addr, #cmd, ea_get_##addr, ea_show_##addr, op_##cmd,ticks, 0 }
#define MAKE_COMMAND_ILL() { "ill", "ill", NULL, NULL, NULL, 0, CMD_ILL }
#define MAKE_COMMAND_NEW(cmd,addr,ticks) { #addr, #cmd, ea_get_##addr, ea_show_##addr, op_##cmd,ticks, CMD_NEW }

static struct CMD_65C02 cmds[256]=
{
  MAKE_COMMAND(brk,impl,7),       //00
  MAKE_COMMAND(ora,indx,6),       //01
  MAKE_COMMAND_ILL(),             //02
  MAKE_COMMAND_ILL(),             //03
  MAKE_COMMAND_NEW(tsb,zp,5),     //04
  MAKE_COMMAND(ora,zp,3),         //05
  MAKE_COMMAND(asl_mem,zp,5),     //06
  MAKE_COMMAND_NEW(rmb0,zp,2),    //07
  MAKE_COMMAND(php,impl,3),       //08
  MAKE_COMMAND(ora,imm,2),        //09
  MAKE_COMMAND(asl_acc,_acc,2),   //0A
  MAKE_COMMAND_ILL(),             //0B
  MAKE_COMMAND_NEW(tsb,abs,6),    //0C
  MAKE_COMMAND(ora,abs,4),        //0D
  MAKE_COMMAND(asl_mem,abs,6),    //0E
  MAKE_COMMAND_NEW(bbr0,rel,2),   //0F

  MAKE_COMMAND(bpl,rel,2),        //10
  MAKE_COMMAND(ora,indy,5),       //11
  MAKE_COMMAND_NEW(ora,ind_zp,5), //12
  MAKE_COMMAND_ILL(),             //13
  MAKE_COMMAND_NEW(trb,zp,5),     //14
  MAKE_COMMAND(ora,zpx,4),        //15
  MAKE_COMMAND(asl_mem,zpx,6),    //16
  MAKE_COMMAND_NEW(rmb1,zp,2),    //17
  MAKE_COMMAND(clc,impl,2),       //18
  MAKE_COMMAND(ora,absy,4),       //19
  MAKE_COMMAND_NEW(inc_acc,_acc,2),//1A
  MAKE_COMMAND_ILL(),             //1B
  MAKE_COMMAND_NEW(trb,abs,6),    //1C
  MAKE_COMMAND(ora,absx,4),       //1D
  MAKE_COMMAND(asl_mem,absx,7),   //1E
  MAKE_COMMAND_NEW(bbr1,rel,2),   //1F

  MAKE_COMMAND(jsr,abs,6),        //20
  MAKE_COMMAND(and,indx,6),       //21
  MAKE_COMMAND_ILL(),             //22
  MAKE_COMMAND_ILL(),             //23
  MAKE_COMMAND(bit,zp,3),         //24
  MAKE_COMMAND(and,zp,3),         //25
  MAKE_COMMAND(rol_mem,zp,5),     //26
  MAKE_COMMAND_NEW(rmb2,zp,2),    //27
  MAKE_COMMAND(plp,impl,4),       //28
  MAKE_COMMAND(and,imm,2),        //29
  MAKE_COMMAND(rol_acc,_acc,2),   //2A
  MAKE_COMMAND_ILL(),             //2B
  MAKE_COMMAND(bit,abs,4),        //2C
  MAKE_COMMAND(and,abs,4),        //2D
  MAKE_COMMAND(rol_mem,abs,6),    //2E
  MAKE_COMMAND_NEW(bbr2,rel,2),   //2F

  MAKE_COMMAND(bmi,rel,2),        //30
  MAKE_COMMAND(and,indy,5),       //31
  MAKE_COMMAND_NEW(and,ind_zp,5), //32
  MAKE_COMMAND_ILL(),             //33
  MAKE_COMMAND_NEW(bit,zpx,4),    //34
  MAKE_COMMAND(and,zpx,4),        //35
  MAKE_COMMAND(rol_mem,zpx,6),    //36
  MAKE_COMMAND_NEW(rmb3,zp,2),    //37
  MAKE_COMMAND(sec,impl,2),       //38
  MAKE_COMMAND(and,absy,4),       //39
  MAKE_COMMAND_NEW(dec_acc,_acc,2),//3A
  MAKE_COMMAND_ILL(),             //3B
  MAKE_COMMAND_NEW(bit,absx,4),   //3C
  MAKE_COMMAND(and,absx,3),       //3D
  MAKE_COMMAND(rol_mem,absx,7),   //3E
  MAKE_COMMAND_NEW(bbr3,rel,2),   //3F

  MAKE_COMMAND(rti,impl,6),       //40
  MAKE_COMMAND(eor,indx,6),       //41
  MAKE_COMMAND_ILL(),       	  //42
  MAKE_COMMAND_ILL(),             //43
  MAKE_COMMAND_ILL(),             //44
  MAKE_COMMAND(eor,zp,3),         //45
  MAKE_COMMAND(lsr_mem,zp,5),     //46
  MAKE_COMMAND_NEW(rmb4,zp,2),    //47
  MAKE_COMMAND(pha,impl,3),       //48
  MAKE_COMMAND(eor,imm,2),        //49
  MAKE_COMMAND(lsr_acc,_acc,2),   //4A
  MAKE_COMMAND_ILL(),             //4B
  MAKE_COMMAND(jmp,abs,3),        //4C
  MAKE_COMMAND(eor,abs,4),        //4D
  MAKE_COMMAND(lsr_mem,abs,6),    //4E
  MAKE_COMMAND_NEW(bbr4,rel,2),   //4F

  MAKE_COMMAND(bvc,rel,2),        //50
  MAKE_COMMAND(eor,indy,5),       //51
  MAKE_COMMAND_NEW(eor,ind_zp,5), //52
  MAKE_COMMAND_ILL(),             //53
  MAKE_COMMAND_ILL(),             //54
  MAKE_COMMAND(eor,zpx,4),        //55
  MAKE_COMMAND(lsr_mem,zpx,6),    //56
  MAKE_COMMAND_NEW(rmb5,zp,2),    //57
  MAKE_COMMAND(cli,impl,2),       //58
  MAKE_COMMAND(eor,absy,4),       //59
  MAKE_COMMAND_NEW(phy,impl,3),   //5A
  MAKE_COMMAND_ILL(),             //5B
  MAKE_COMMAND_ILL(),             //5C
  MAKE_COMMAND(eor,absx,4),       //5D
  MAKE_COMMAND(lsr_mem,absx,7),   //5E
  MAKE_COMMAND_NEW(bbr5,rel,2),   //5F

  MAKE_COMMAND(rts,impl,6),       //60
  MAKE_COMMAND(adc,indx,6),       //61
  MAKE_COMMAND_ILL(),             //62
  MAKE_COMMAND_ILL(),             //63
  MAKE_COMMAND_NEW(stz,zp,3),     //64
  MAKE_COMMAND(adc,zp,3),         //65
  MAKE_COMMAND(ror_mem,zp,5),     //66
  MAKE_COMMAND_NEW(rmb6,zp,2),    //67
  MAKE_COMMAND(pla,impl,4),       //68
  MAKE_COMMAND(adc,imm,2),        //69
  MAKE_COMMAND(ror_acc,_acc,2),   //6A
  MAKE_COMMAND_ILL(),             //6B
  MAKE_COMMAND(jmp,ind,5),        //6C
  MAKE_COMMAND(adc,abs,4),        //6D
  MAKE_COMMAND(ror_mem,abs,6),    //6E
  MAKE_COMMAND_NEW(bbr6,rel,2),   //6F

  MAKE_COMMAND(bvs,rel,2),        //70
  MAKE_COMMAND(adc,indy,5),       //71
  MAKE_COMMAND_NEW(adc,ind_zp,5), //72
  MAKE_COMMAND_ILL(),             //73
  MAKE_COMMAND_NEW(stz,zpx,4),    //74
  MAKE_COMMAND(adc,zpx,4),        //75
  MAKE_COMMAND(ror_mem,zpx,6),    //76
  MAKE_COMMAND_NEW(rmb7,zp,2),    //77
  MAKE_COMMAND(sei,impl,2),       //78
  MAKE_COMMAND(adc,absy,4),       //79
  MAKE_COMMAND_NEW(ply,impl,4),   //7A
  MAKE_COMMAND_ILL(),             //7B
  MAKE_COMMAND_NEW(jmp,ind16x,4), //7C
  MAKE_COMMAND(adc,absx,4),       //7D
  MAKE_COMMAND(ror_mem,absx,7),   //7E
  MAKE_COMMAND_NEW(bbr7,rel,2),   //7F

  MAKE_COMMAND_NEW(bra,rel,2),    //80
  MAKE_COMMAND(sta,indx,6),       //81
  MAKE_COMMAND_ILL(),             //82
  MAKE_COMMAND_ILL(),             //83
  MAKE_COMMAND(sty,zp,3),         //84
  MAKE_COMMAND(sta,zp,3),         //85
  MAKE_COMMAND(stx,zp,3),         //86
  MAKE_COMMAND_NEW(smb0,zp,2),    //87
  MAKE_COMMAND(dey,impl,2),       //88
  MAKE_COMMAND_NEW(bit,imm,2),    //89
  MAKE_COMMAND(txa,impl,2),       //8A
  MAKE_COMMAND_ILL(),             //8B
  MAKE_COMMAND(sty,abs,4),        //8C
  MAKE_COMMAND(sta,abs,4),        //8D
  MAKE_COMMAND(stx,abs,4),        //8E
  MAKE_COMMAND_NEW(bbs0,rel,5),   //8F

  MAKE_COMMAND(bcc,rel,2),        //90
  MAKE_COMMAND(sta,indy,6),       //91
  MAKE_COMMAND_NEW(sta,ind_zp,5), //92
  MAKE_COMMAND_ILL(),             //93
  MAKE_COMMAND(sty,zpx,4),        //94
  MAKE_COMMAND(sta,zpx,4),        //95
  MAKE_COMMAND(stx,zpy,4),        //96
  MAKE_COMMAND_NEW(smb1,zp,2),    //97
  MAKE_COMMAND(tya,impl,2),       //98
  MAKE_COMMAND(sta,absy,5),       //99
  MAKE_COMMAND(txs,impl,2),       //9A
  MAKE_COMMAND_ILL(),             //9B
  MAKE_COMMAND_NEW(stz,abs,4),    //9C
  MAKE_COMMAND(sta,absx,5),       //9D
  MAKE_COMMAND_NEW(stz,absx,5),   //9E
  MAKE_COMMAND_NEW(bbs1,rel,2),   //9F

  MAKE_COMMAND(ldy,imm,2),        //A0
  MAKE_COMMAND(lda,indx,6),       //A1
  MAKE_COMMAND(ldx,imm,2),        //A2
  MAKE_COMMAND_ILL(),             //A3
  MAKE_COMMAND(ldy,zp,3),         //A4
  MAKE_COMMAND(lda,zp,3),         //A5
  MAKE_COMMAND(ldx,zp,3),         //A6
  MAKE_COMMAND_NEW(smb2,zp,2),    //A7
  MAKE_COMMAND(tay,impl,2),       //A8
  MAKE_COMMAND(lda,imm,2),        //A9
  MAKE_COMMAND(tax,impl,2),       //AA
  MAKE_COMMAND_ILL(),             //AB
  MAKE_COMMAND(ldy,abs,4),        //AC
  MAKE_COMMAND(lda,abs,4),        //AD
  MAKE_COMMAND(ldx,abs,4),        //AE
  MAKE_COMMAND_NEW(bbs2,rel,2),   //AF

  MAKE_COMMAND(bcs,rel,2),        //B0
  MAKE_COMMAND(lda,indy,5),       //B1
  MAKE_COMMAND_NEW(lda,ind_zp,5), //B2
  MAKE_COMMAND_ILL(),             //B3
  MAKE_COMMAND(ldy,zpx,4),        //B4
  MAKE_COMMAND(lda,zpx,4),        //B5
  MAKE_COMMAND(ldx,zpy,4),        //B6
  MAKE_COMMAND_NEW(smb3,zp,2),    //B7
  MAKE_COMMAND(clv,impl,2),       //B8
  MAKE_COMMAND(lda,absy,4),       //B9
  MAKE_COMMAND(tsx,impl,2),       //BA
  MAKE_COMMAND_ILL(),             //BB
  MAKE_COMMAND(ldy,absx,4),       //BC
  MAKE_COMMAND(lda,absx,4),       //BD
  MAKE_COMMAND(ldx,absy,4),       //BE
  MAKE_COMMAND_NEW(bbs3,rel,2),   //BF

  MAKE_COMMAND(cpy,imm,2),        //C0
  MAKE_COMMAND(cmp,indx,6),       //C1
  MAKE_COMMAND_ILL(),             //C2
  MAKE_COMMAND_ILL(),             //C3
  MAKE_COMMAND(cpy,zp,3),         //C4
  MAKE_COMMAND(cmp,zp,3),         //C5
  MAKE_COMMAND(dec,zp,5),         //C6
  MAKE_COMMAND_NEW(smb4,zp,2),    //C7
  MAKE_COMMAND(iny,impl,2),       //C8
  MAKE_COMMAND(cmp,imm,2),        //C9
  MAKE_COMMAND(dex,impl,2),       //CA
  MAKE_COMMAND_NEW(wai,impl,2),   //CB
  MAKE_COMMAND(cpy,abs,4),        //CC
  MAKE_COMMAND(cmp,abs,4),        //CD
  MAKE_COMMAND(dec,abs,6),        //CE
  MAKE_COMMAND_NEW(bbs4,rel,2),   //CF

  MAKE_COMMAND(bne,rel,2),        //D0
  MAKE_COMMAND(cmp,indy,5),       //D1
  MAKE_COMMAND_NEW(cmp,ind_zp,5), //D2
  MAKE_COMMAND_ILL(),             //D3
  MAKE_COMMAND_ILL(),             //D4
  MAKE_COMMAND(cmp,zpx,4),        //D5
  MAKE_COMMAND(dec,zpx,6),        //D6
  MAKE_COMMAND_NEW(smb5,zp,2),    //D7
  MAKE_COMMAND(cld,impl,2),       //D8
  MAKE_COMMAND(cmp,absy,4),       //D9
  MAKE_COMMAND_NEW(phx,impl,3),   //DA
  MAKE_COMMAND_NEW(stp,impl,7),   //DB
  MAKE_COMMAND_ILL(),             //DC
  MAKE_COMMAND(cmp,absx,4),       //DD
  MAKE_COMMAND(dec,absx,7),       //DE
  MAKE_COMMAND_NEW(bbs5,rel,2),   //DF

  MAKE_COMMAND(cpx,imm,2),        //E0
  MAKE_COMMAND(sbc,indx,6),       //E1
  MAKE_COMMAND_ILL(),             //E2
  MAKE_COMMAND_ILL(),             //E3
  MAKE_COMMAND(cpx,zp,3),         //E4
  MAKE_COMMAND(sbc,zp,3),         //E5
  MAKE_COMMAND(inc,zp,5),         //E6
  MAKE_COMMAND_NEW(smb6,zp,2),    //E7
  MAKE_COMMAND(inx,impl,2),       //E8
  MAKE_COMMAND(sbc,imm,2),        //E9
  MAKE_COMMAND(nop,impl,2),       //EA
  MAKE_COMMAND_ILL(),             //EB
  MAKE_COMMAND(cpx,abs,4),        //EC
  MAKE_COMMAND(sbc,abs,4),        //ED
  MAKE_COMMAND(inc,abs,6),        //EE
  MAKE_COMMAND_NEW(bbs6,rel,2),   //EF

  MAKE_COMMAND(beq,rel,2),        //F0
  MAKE_COMMAND(sbc,indy,5),       //F1
  MAKE_COMMAND_NEW(sbc,ind_zp,5), //F2
  MAKE_COMMAND_ILL(),             //F3
  MAKE_COMMAND_ILL(),             //F4
  MAKE_COMMAND(sbc,zpx,4),        //F5
  MAKE_COMMAND(inc,zpx,6),        //F6
  MAKE_COMMAND_NEW(smb7,zp,2),    //F7
  MAKE_COMMAND(sed,impl,2),       //F8
  MAKE_COMMAND(sbc,absy,4),       //F9
  MAKE_COMMAND_NEW(plx,impl,4),   //FA
  MAKE_COMMAND_ILL(),             //FB
  MAKE_COMMAND_ILL(),             //FC
  MAKE_COMMAND(sbc,absx,4),       //FD
  MAKE_COMMAND(inc,absx,7),       //FE
  MAKE_COMMAND_NEW(bbs7,rel,2)    //FF
};

extern int cpu_debug;


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


static void op_disassemble(struct STATE_65C02*st, struct CMD_65C02*c)
{
	fprintf(stdout, "%04X\t%s\t", st->pc - 1, c->cmd_name);
	if (c->printadr) c->printadr(st, stdout);
	fprintf(stdout, "\n");
}

static int exec_65c02(struct CPU_STATE*cs)
{
	struct STATE_65C02*st = cs->state;
	struct CMD_65C02*c;
	int b;
	int n = 0;

//	if (cpu_debug) dumpregs(st);

	if (st->ints_req&INT_NMI) {
		push_stack_w(st, (word)(st->pc));
		push_stack(st, st->f & ~FLAG_B);
		st->f|=FLAG_I;
		st->pc=mem_read_word(st, ADDR_NMI);
		st->ints_req&=~INT_NMI;
		st->f &= ~FLAG_D;
//		puts("nmi");
		return 7;
	} else 	if (st->ints_req&INT_RESET) {
		st->pc=mem_read_word(st, ADDR_RES);
		st->ints_req&=~INT_RESET;
		st->f |= FLAG_I | FLAG_1;
		st->f &= ~FLAG_D;
		puts("reset");
		return 6;
	} else if ((st->ints_req&INT_IRQ)&&!(st->f&FLAG_I)) {
		push_stack_w(st, (word)(st->pc));
		push_stack(st, st->f & ~FLAG_B);
		st->f|=FLAG_I;
		st->pc=mem_read_word(st, ADDR_IRQ);
		st->ints_req&=~INT_IRQ;
		st->f &= ~FLAG_D;
//		puts("irq");
		return 7;
	}
	if (cpu_debug) {
		printf("PC: %04X; A: %02X; X: %02X; Y: %02X; F: %02X [%8s]\n", st->pc, st->a, st->x, st->y, st->f, get_flags(st->f));
	}

	b=fetch_cmd_byte(st);
	c=cmds+b;
	if ((!cs->undoc) && (!c->cmd || (c->flags & CMD_ILL))) {
		op_disassemble(st, c);
		printf("undocumented command: %02X (%s %s)\n", b, c->cmd_name, c->adr_name);
		return -1;
	}
//	if (cpu_debug/* || (c->flags & CMD_NEW)*/) {
//		op_disassemble(st, c);
//	}
	st->addt = 0;
	if (c->adr) c->adr(st);
	if (c->cmd) c->cmd(st); else n++;
	if (c->ticks >= 6) st->addt = 0;
	n += c->ticks + st->addt;
	return n;
}

static void intr_65c02(struct CPU_STATE*cs, int r)
{
	struct STATE_65C02*st = cs->state;
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

static void free_65c02(struct CPU_STATE*cs)
{
	free(cs->state);
}


static int save_65c02(struct CPU_STATE*cs, OSTREAM *out)
{
	struct STATE_65C02*st = cs->state;
	WRITE_FIELD(out, *st);
	return 0;
}

static int load_65c02(struct CPU_STATE*cs, ISTREAM *in)
{
	struct STATE_65C02*st = cs->state, st0;
	READ_FIELD(in, st0);
	st0.sr = st->sr;
	*st = st0;
	return 0;
}


static void dumpregs(struct STATE_65C02*st)
{
	logprint(0,TEXT("%04X-   A=%02X X=%02X Y=%02X P=%02X [%s] S=%02X"),
		st->pc,
		st->a, 	st->x, 	st->y,
		st->f,	get_flags(st->f),
		st->s);
}

static int get_regs(struct STATE_65C02*st, struct REGS_6502* regs)
{
	regs->A = st->a;
	regs->X = st->x;
	regs->Y = st->y;
	regs->S = st->s;
	regs->F = st->f;
	regs->PC = st->pc;
	return 1;
}


static int set_regs(struct STATE_65C02*st, int mask, const struct REGS_6502* regs)
{
	if (mask & (1<<REG6502_A)) st->a = regs->A;
	if (mask & (1<<REG6502_X)) st->x = regs->X;
	if (mask & (1<<REG6502_Y)) st->y = regs->Y;
	if (mask & (1<<REG6502_S)) st->s = regs->S;
	if (mask & (1<<REG6502_F)) st->f = regs->F;
	if (mask & (1<<REG6502_PC)) st->pc = regs->PC;
	return 1;
}

static int get_reg(struct STATE_65C02*st, int reg, void*val)
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


static int set_reg(struct STATE_65C02*st, int reg, long val)
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


int cmd_65c02(struct CPU_STATE*cs, int cmd, int data, long param)
{
	struct STATE_65C02*st = cs->state;
	switch (cmd) {
	case SYS_COMMAND_DUMPCPUREGS:
		dumpregs(st);
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

int init_cpu_65c02(struct CPU_STATE*cs)
{
	struct STATE_65C02*st;
	st = calloc(1, sizeof(*st));
	st->sr = cs->sr;
	st->f = FLAG_I | FLAG_1;
	st->s = rand();
	st->ints_req = INT_RESET;
	cs->exec_op = exec_65c02;
	cs->intr = intr_65c02;
	cs->free = free_65c02;
	cs->save = save_65c02;
	cs->load = load_65c02;
	cs->cmd = cmd_65c02;
	cs->state = st;
	return 0;
}
