/*
 * nfglue.c - NatFeat glue for the Castaway emulator
 *
 * Copyright (c) 2002, 03 Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See http://www.gnu.org/copyleft/gpl.html
 */

#include "config.h"
#ifndef NATFEAT

#include "68000.h"
#include "op68k.h"

void            Op7300(void)    /* NATFEAT */
{
    ExceptionGroup1(ILLINSTR);
}

#else /* NATFEAT */

#include "68000.h"
#include "op68k.h"
#include "nf.h"
#include "nfglue.h"

/* get/set for individual types */
nf_byte_t  nf_get_b (nf_addr_t addr)
{ 
  return GetMemB(addr);
}
nf_ubyte_t nf_get_ub(nf_addr_t addr)
{ 
  return GetMemB(addr);
}
nf_word_t  nf_get_w (nf_addr_t addr)
{ 
  return GetMemW(addr);
}
nf_uword_t nf_get_uw(nf_addr_t addr)
{ 
  return GetMemW(addr);
}
nf_long_t  nf_get_l (nf_addr_t addr)
{ 
  return GetMemL(addr);
}
nf_ulong_t nf_get_ul(nf_addr_t addr)
{ 
  return GetMemL(addr);
}

/* write the value in 68k memory; may not return if bus error occurs */
void nf_set_b (nf_addr_t addr, nf_byte_t  value)
{ 
  SetMemB(addr,value);
}
void nf_set_ub(nf_addr_t addr, nf_ubyte_t value)
{ 
  SetMemB(addr,value);
}
void nf_set_w (nf_addr_t addr, nf_word_t  value)
{ 
  SetMemW(addr,value);
}
void nf_set_uw(nf_addr_t addr, nf_uword_t value)
{ 
  SetMemW(addr,value);
}
void nf_set_l (nf_addr_t addr, nf_long_t  value)
{ 
  SetMemL(addr,value);
}
void nf_set_ul(nf_addr_t addr, nf_ulong_t value)
{ 
  SetMemL(addr,value);
}


/* these can be implemented using the other above, or using more direct
 * access to the emulator internals 
 * - This is a quick and dirty implementation, so I just take the default, 
 * slow method.
 */

void nf_get_mem(void *buf, nf_addr_t addr, nf_long_t size)
{
  char *b = buf;
  while(--size >= 0) 
    *b++ = nf_get_b(addr++);
}

void nf_set_mem(nf_addr_t addr, void *buf, nf_long_t size)
{
  char *b = buf;
  while(--size >= 0) 
    nf_set_b(addr++, *b++);
}

/* returns the length of the string in 68k memory */
nf_long_t nf_strlen(nf_addr_t addr)
{
  nf_long_t n = 0;
  while(nf_get_b(addr++)) n++;
  return n;
}

/* copies in 68k memory the supplied string. */
void nf_set_str(nf_addr_t addr, char *s)
{
  while(*s) nf_set_b(addr++, *s++);
  nf_set_b(addr, 0);
}

/* same as nf_get_str or nf_set_str but the string is truncated to fit 
 * in size bytes (ending null included) 
 */
void nf_get_nstr(char *buf, nf_addr_t addr, nf_long_t size)
{
  while(--size > 0) {
    if((*buf++ = nf_get_b(addr++)) == 0) return;    
  }
  *buf = 0;
}
  
void nf_set_nstr(nf_addr_t addr, char *s, nf_long_t size)
{
  while(--size > 0 && *s) {
    nf_set_b(addr++, *s++);
  }
  nf_set_b(addr, 0);
}


/* error messages */
#include <stdio.h>
void nf_verror(const char *fmt, va_list ap)
{
  vfprintf(stderr, fmt, ap);
}

void nf_error(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  nf_verror(fmt, ap);
  va_end(ap);
}

/*
 * the natfeat opcodes.
 */

void            Op7300(void)    /* NATFEAT */
{
    long a7;
    long d0;
    /*  nf_error("Op7300(inst = 0x%04x)\n", inst); */
    switch(inst) {
    case 0x7300:
        a7 = GetARegL(7);
	/* nf_error("a7 = 0x%08lx)\n", (long)a7); */
        d0 = nf_get_id(a7);
        /* nf_error("d0 = 0x%08lx)\n", (long)d0); */
        SetRegL(0, d0);
	break;
    case 0x7301:
        a7 = GetARegL(7);
	d0 = nf_call(a7, GetS() ? 0 : 1);
        SetRegL(0, d0);
	break;
    default:
        ExceptionGroup1(ILLINSTR);
	break;
    }
}

#endif /* NATFEAT */
