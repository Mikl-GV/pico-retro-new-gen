#define LSB_FIRST
/** M6502: portable 6502 emulator ****************************/
/**                                                         **/
/**                         M6502.c                         **/
/**                                                         **/
/** This file contains implementation for 6502 CPU. Don't   **/
/** forget to provide Rd6502(), Wr6502(), Loop6502(), and   **/
/** possibly Op6502() functions to accomodate the emulated  **/
/** machine's architecture.                                 **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 1996-2002                 **/
/**               Alex Krasivsky  1996                      **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/   
/**     changes to this file.                               **/
/*************************************************************/

#include "M6502.h"
#include "Tables.h"
#include <stdio.h>

/** INLINE ***************************************************/
/** Different compilers inline C functions differently.     **/
/*************************************************************/
#ifdef __GNUC__
#define INLINE inline
#else
#define INLINE static
#endif

/** System-Dependent Stuff ***********************************/
/** This is system-dependent code put here to speed things  **/
/** up. It has to stay inlined to be fast.                  **/
/*************************************************************/
#ifdef INES
#define FAST_RDOP
extern byte *Page[];
INLINE byte Op6502(register word A) { return(Page[A>>13][A&0x1FFF]); }
#endif

/** FAST_RDOP ************************************************/
/** With this #define not present, Rd6502() should perform  **/
/** the functions of Rd6502().                              **/
/*************************************************************/
#ifndef FAST_RDOP
#define Op6502(R,A) Rd6502(R,A)
#endif

/** Addressing Methods ***************************************/
/** These macros calculate and return effective addresses.  **/
/*************************************************************/
#define MC_Ab(Rg)	M_LDWORD(Rg)
#define MC_Zp(Rg)       Rg.W=Op6502(R,R->PC.W++)
#define MC_Zx(Rg)       Rg.W=(byte)(Op6502(R,R->PC.W++)+R->X)
#define MC_Zy(Rg)       Rg.W=(byte)(Op6502(R,R->PC.W++)+R->Y)
#define MC_Ax(Rg)	M_LDWORD(Rg);Rg.W+=R->X
#define MC_Ay(Rg)	M_LDWORD(Rg);Rg.W+=R->Y
#define MC_Ix(Rg)       K.W=(byte)(Op6502(R,R->PC.W++)+R->X); \
			Rg.B.l=Op6502(R,K.W++);Rg.B.h=Op6502(R,K.W)
#define MC_Iy(Rg)       K.W=Op6502(R,R->PC.W++); \
			Rg.B.l=Op6502(R,K.W++);Rg.B.h=Op6502(R,K.W); \
			Rg.W+=R->Y

/** Reading From Memory **************************************/
/** These macros calculate address and read from it.        **/
/*************************************************************/
#define MR_Ab(Rg)	MC_Ab(J);Rg=Rd6502(R,J.W)
#define MR_Im(Rg)	Rg=Op6502(R,R->PC.W++)
#define	MR_Zp(Rg)	MC_Zp(J);Rg=Rd6502(R,J.W)
#define MR_Zx(Rg)	MC_Zx(J);Rg=Rd6502(R,J.W)
#define MR_Zy(Rg)	MC_Zy(J);Rg=Rd6502(R,J.W)
#define	MR_Ax(Rg)	MC_Ax(J);Rg=Rd6502(R,J.W)
#define MR_Ay(Rg)	MC_Ay(J);Rg=Rd6502(R,J.W)
#define MR_Ix(Rg)	MC_Ix(J);Rg=Rd6502(R,J.W)
#define MR_Iy(Rg)	MC_Iy(J);Rg=Rd6502(R,J.W)

/** Writing To Memory ****************************************/
/** These macros calculate address and write to it.         **/
/*************************************************************/
#define MW_Ab(Rg)	MC_Ab(J);Wr6502(R,J.W,Rg)
#define MW_Zp(Rg)	MC_Zp(J);Wr6502(R,J.W,Rg)
#define MW_Zx(Rg)	MC_Zx(J);Wr6502(R,J.W,Rg)
#define MW_Zy(Rg)	MC_Zy(J);Wr6502(R,J.W,Rg)
#define MW_Ax(Rg)	MC_Ax(J);Wr6502(R,J.W,Rg)
#define MW_Ay(Rg)	MC_Ay(J);Wr6502(R,J.W,Rg)
#define MW_Ix(Rg)	MC_Ix(J);Wr6502(R,J.W,Rg)
#define MW_Iy(Rg)	MC_Iy(J);Wr6502(R,J.W,Rg)

/** Modifying Memory *****************************************/
/** These macros calculate address and modify it.           **/
/*************************************************************/
#define MM_Ab(Cmd)	MC_Ab(J);I=Rd6502(R,J.W);Cmd(I);Wr6502(R,J.W,I)
#define MM_Zp(Cmd)	MC_Zp(J);I=Rd6502(R,J.W);Cmd(I);Wr6502(R,J.W,I)
#define MM_Zx(Cmd)	MC_Zx(J);I=Rd6502(R,J.W);Cmd(I);Wr6502(R,J.W,I)
#define MM_Ax(Cmd)	MC_Ax(J);I=Rd6502(R,J.W);Cmd(I);Wr6502(R,J.W,I)

/** Other Macros *********************************************/
/** Calculating flags, stack, jumps, arithmetics, etc.      **/
/*************************************************************/
#define M_FL(Rg)	R->P=(R->P&~(Z_FLAG|N_FLAG))|ZNTable[Rg]
#define M_LDWORD(Rg)	Rg.B.l=Op6502(R,R->PC.W++);Rg.B.h=Op6502(R,R->PC.W++)

#define M_PUSH(Rg)	Wr6502(R,0x0100|R->S,Rg);R->S--
#define M_POP(Rg)	R->S++;Rg=Op6502(R,0x0100|R->S)
#define M_JR		R->PC.W+=(offset)Op6502(R,R->PC.W)+1;R->ICount--

#ifdef NO_DECIMAL

#define M_ADC(Rg) \
  K.W=R->A+Rg+(R->P&C_FLAG); \
  R->P&=~(N_FLAG|V_FLAG|Z_FLAG|C_FLAG); \
  R->P|=(~(R->A^Rg)&(R->A^K.B.l)&0x80? V_FLAG:0)| \
        (K.B.h? C_FLAG:0)|ZNTable[K.B.l]; \
  R->A=K.B.l

/* Warning! C_FLAG is inverted before SBC and after it */
#define M_SBC(Rg) \
  K.W=R->A-Rg-(~R->P&C_FLAG); \
  R->P&=~(N_FLAG|V_FLAG|Z_FLAG|C_FLAG); \
  R->P|=((R->A^Rg)&(R->A^K.B.l)&0x80? V_FLAG:0)| \
        (K.B.h? 0:C_FLAG)|ZNTable[K.B.l]; \
  R->A=K.B.l

#else /* NO_DECIMAL */

#define M_ADC(Rg) \
  if(R->P&D_FLAG) \
  { \
    K.B.l=(R->A&0x0F)+(Rg&0x0F)+(R->P&C_FLAG); \
    if(K.B.l>9) K.B.l+=6; \
    K.B.h=(R->A>>4)+(Rg>>4)+(K.B.l>15? 1:0); \
    R->A=(K.B.l&0x0F)|(K.B.h<<4); \
    R->P=(R->P&~C_FLAG)|(K.B.h>15? C_FLAG:0); \
  } \
  else \
  { \
    K.W=R->A+Rg+(R->P&C_FLAG); \
    R->P&=~(N_FLAG|V_FLAG|Z_FLAG|C_FLAG); \
    R->P|=(~(R->A^Rg)&(R->A^K.B.l)&0x80? V_FLAG:0)| \
          (K.B.h? C_FLAG:0)|ZNTable[K.B.l]; \
    R->A=K.B.l; \
  }

/* Warning! C_FLAG is inverted before SBC and after it */
#define M_SBC(Rg) \
  if(R->P&D_FLAG) \
  { \
    K.B.l=(R->A&0x0F)-(Rg&0x0F)-(~R->P&C_FLAG); \
    if(K.B.l&0x10) K.B.l-=6; \
    K.B.h=(R->A>>4)-(Rg>>4)-((K.B.l&0x10)>>4); \
    if(K.B.h&0x10) K.B.h-=6; \
    R->A=(K.B.l&0x0F)|(K.B.h<<4); \
    R->P=(R->P&~C_FLAG)|(K.B.h>15? 0:C_FLAG); \
  } \
  else \
  { \
    K.W=R->A-Rg-(~R->P&C_FLAG); \
    R->P&=~(N_FLAG|V_FLAG|Z_FLAG|C_FLAG); \
    R->P|=((R->A^Rg)&(R->A^K.B.l)&0x80? V_FLAG:0)| \
          (K.B.h? 0:C_FLAG)|ZNTable[K.B.l]; \
    R->A=K.B.l; \
  }

#endif /* NO_DECIMAL */

#define M_CMP(Rg1,Rg2) \
  K.W=Rg1-Rg2; \
  R->P&=~(N_FLAG|Z_FLAG|C_FLAG); \
  R->P|=ZNTable[K.B.l]|(K.B.h? 0:C_FLAG)
#define M_BIT(Rg) \
  R->P&=~(N_FLAG|V_FLAG|Z_FLAG); \
  R->P|=(Rg&(N_FLAG|V_FLAG))|(Rg&R->A? 0:Z_FLAG)

#define M_AND(Rg)	R->A&=Rg;M_FL(R->A)
#define M_ORA(Rg)	R->A|=Rg;M_FL(R->A)
#define M_EOR(Rg)	R->A^=Rg;M_FL(R->A)
#define M_INC(Rg)	Rg++;M_FL(Rg)
#define M_DEC(Rg)	Rg--;M_FL(Rg)

#define M_ASL(Rg)	R->P&=~C_FLAG;R->P|=Rg>>7;Rg<<=1;M_FL(Rg)
#define M_LSR(Rg)	R->P&=~C_FLAG;R->P|=Rg&C_FLAG;Rg>>=1;M_FL(Rg)
#define M_ROL(Rg)	K.B.l=(Rg<<1)|(R->P&C_FLAG); \
			R->P&=~C_FLAG;R->P|=Rg>>7;Rg=K.B.l; \
			M_FL(Rg)
#define M_ROR(Rg)	K.B.l=(Rg>>1)|(R->P<<7); \
			R->P&=~C_FLAG;R->P|=Rg&C_FLAG;Rg=K.B.l; \
			M_FL(Rg)

/** Reset6502() **********************************************/
/** This function can be used to reset the registers before **/
/** starting execution with Run6502(). It sets registers to **/
/** their initial values.                                   **/
/*************************************************************/
void Reset6502(M6502 *R)
{
  R->A=R->X=R->Y=0x00;
  R->P=Z_FLAG|R_FLAG|I_FLAG;
  R->S=0xFF;
  R->PC.B.l=Rd6502(R,0xFFFC);
  R->PC.B.h=Rd6502(R,0xFFFD);   
  R->ICount=R->IPeriod;
  R->IRequest=INT_NONE;
  R->AfterCLI=0;
}

/** Exec6502() ***********************************************/
/** This function will execute a single 6502 opcode. It     **/
/** will then return next PC, and current register values   **/
/** in R.                                                   **/
/*************************************************************/
word Exec6502(M6502 *R)
{
  register pair J,K;
  register byte I;

  I=Op6502(R,R->PC.W++);
  R->ICount-=Cycles[I];
  switch(I)
  {
#include "Codes.h"
  }

  /* We are done */
  return Cycles[I];
}

/** Int6502() ************************************************/
/** This function will generate interrupt of a given type.  **/
/** INT_NMI will cause a non-maskable interrupt. INT_IRQ    **/
/** will cause a normal interrupt, unless I_FLAG set in R.  **/
/*************************************************************/
void Int6502(M6502 *R,byte Type)
{
  register pair J;

  if((Type==INT_NMI)||((Type==INT_IRQ)&&!(R->P&I_FLAG)))
  {
    R->ICount-=7;
    M_PUSH(R->PC.B.h);
    M_PUSH(R->PC.B.l);
    M_PUSH(R->P&~B_FLAG);
    R->P&=~D_FLAG;
    if(R->IAutoReset&&(Type==R->IRequest)) R->IRequest=INT_NONE;
    if(Type==INT_NMI) J.W=0xFFFA; else { R->P|=I_FLAG;J.W=0xFFFE; }
    R->PC.B.l=Rd6502(R,J.W++);
    R->PC.B.h=Rd6502(R,J.W);
  }
}

#include "cpuint.h"

void Wr6502(register M6502 *R, register word Addr,register byte Value)
{
	mem_write(Addr, Value, R->User);
}

byte Rd6502(register M6502 *R, register word Addr)
{
	return mem_read(Addr, R->User);
}

byte Patch6502(register byte Op,register M6502 *R)
{
	return 0;
}

static int exec_M6502(struct CPU_STATE*cs)
{
	M6502*st = cs->state;
	if (st->IR&1) { Reset6502(st); st->IR&=~1; }
	else if (st->IR&2) { Int6502(st, INT_NMI); st->IR&=~2; }
	else if (st->IR&4) { Int6502(st, INT_IRQ); }
	return Exec6502(st);
}

static void intr_M6502(struct CPU_STATE*cs, int r)
{
	M6502*st = cs->state;
	switch (r) {
	case CPU_INTR_RESET:
	case CPU_INTR_HRESET:
		st->IR|=1;
		break;
	case CPU_INTR_NMI:
		st->IR|=2;
		break;
	case CPU_INTR_IRQ:
		st->IR|=4;
		break;
	case CPU_INTR_NOIRQ:
		st->IR &= ~4;
		break;
	case CPU_INTR_NONMI:
		st->IR &= ~2;
		break;
	}
}

static void free_M6502(struct CPU_STATE*cs)
{
	free(cs->state);
}

static int save_M6502(struct CPU_STATE*cs, OSTREAM *out)
{
	M6502*st = cs->state;
	WRITE_FIELD(out, *st);
	return 0;
}

static int load_M6502(struct CPU_STATE*cs, ISTREAM *in)
{
	M6502*st = cs->state;
	READ_FIELD(in, *st);
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
	M6502*st = cs->state;
	logprint(0,TEXT("%04X-   A=%02X X=%02X Y=%02X P=%02X [%s] S=%02X"),
		st->PC.W,
		st->A, 	st->X, 	st->Y,
		st->P,	get_flags(st->P),
		st->S);
}

static int get_regs(M6502*st, struct REGS_6502* regs)
{
	regs->A = st->A;
	regs->X = st->X;
	regs->Y = st->Y;
	regs->S = st->S;
	regs->F = st->P;
	regs->PC = st->PC.W;
	return 1;
}

static int set_regs(M6502*st, int mask, const struct REGS_6502* regs)
{
	if (mask & (1<<REG6502_A)) st->A = regs->A;
	if (mask & (1<<REG6502_X)) st->X = regs->X;
	if (mask & (1<<REG6502_Y)) st->Y = regs->Y;
	if (mask & (1<<REG6502_S)) st->S = regs->S;
	if (mask & (1<<REG6502_F)) st->P = regs->F;
	if (mask & (1<<REG6502_PC)) st->PC.W = regs->PC;
	return 1;
}

static int get_reg(M6502*st, int reg, void*val)
{
	switch (reg) {
	case REG6502_A:
		*(byte*)val = st->A;
		break;
	case REG6502_X:
		*(byte*)val = st->X;
		break;
	case REG6502_Y:
		*(byte*)val = st->Y;
		break;
	case REG6502_S:
		*(byte*)val = st->S;
		break;
	case REG6502_F:
		*(byte*)val = st->P;
		break;
	case REG6502_PC:
		*(word*)val = st->PC.W;
		break;
	default:
		return -1;
	}
	return 1;
}


static int set_reg(M6502*st, int reg, long val)
{
	switch (reg) {
	case REG6502_A:
		st->A = (byte)val;
		break;
	case REG6502_X:
		st->X = (byte)val;
		break;
	case REG6502_Y:
		st->Y = (byte)val;
		break;
	case REG6502_S:
		st->S = (byte)val;
		break;
	case REG6502_F:
		st->P = (byte)val;
		break;
	case REG6502_PC:
		st->PC.W = (word)val;
		break;
	default:
		return -1;
	}
	return 1;
}

int cmd_M6502(struct CPU_STATE*cs, int cmd, int data, long param)
{
	M6502*st = cs->state;
	switch (cmd) {
	case SYS_COMMAND_DUMPCPUREGS:
		dumpregs(cs);
		return 1;
	case SYS_COMMAND_EXEC:
		st->PC.W = (word)param;
		return 0;
	case SYS_COMMAND_HRESET:
		st->S = rand();
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

int init_cpu_M6502(struct CPU_STATE*cs)
{
	M6502 *st;
	st = calloc(1, sizeof(*st));
	st->User = cs->sr;
	st->P = I_FLAG | R_FLAG;
	st->S = rand();
	cs->exec_op = exec_M6502;
	cs->intr = intr_M6502;
	cs->free = free_M6502;
	cs->save = save_M6502;
	cs->load = load_M6502;
	cs->cmd = cmd_M6502;
	cs->state = st;
	intr_M6502(cs, CPU_INTR_HRESET);
	return 0;
}
