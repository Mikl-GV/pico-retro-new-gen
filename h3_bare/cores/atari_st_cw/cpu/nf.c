/*
 * nf.c - a simple Native function services implementation 
 *
 * Copyright (C) 2002, 03 Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See http://www.gnu.org/copyleft/gpl.html
 */

#include "config.h"
#ifndef NATFEAT

#include "nf.h"

/* just a stub */
int nf_init(void) 
{ 
  /* error while initing */
  return 1; 
}

/* called by a native implementation module to register native functions */
nf_long_t nf_register_func(const char * name, nf_func_t func, int user_allowed)
{
  /* error while registering */
  return 0;
}

#else /* NATFEAT */

/*============================ GENERIC HANDLER ==============================*/

/* Here is the generic part.
 * the Castaway-specific functions are at the end of this file.
 */

#include <string.h>
#include "nf.h"

#define NFDBG 0


#define SUB_ID_SHIFT 20
#define SUB_ID_MASK ((1 << SUB_ID_SHIFT) - 1)

/*
 * A simple table for native functions.
 */

#define MAXFUNCS 100
#define MAXNAME 40
static struct item {
  nf_func_t func;
  char name[MAXNAME];
  int user_allowed;
} table[MAXFUNCS];
static int numfuncs;

static int find(const char *name)
{
  int i;
  for(i = 0 ; i < numfuncs ; i++) {
    if(!strncmp(name, table[i].name, MAXNAME)) 
      return i+1;
  }
  return 0;
}

/* called by a native implementation module to register native functions */
nf_long_t nf_register_func(const char * name, nf_func_t func, int user_allowed)
{
  struct item *p;
  int id;

  if(find(name)) {
    nf_error("native function '%s' already registered\n", name);
    return 0;
  }
  if(numfuncs >= MAXFUNCS) {
    nf_error("too many native functions registered\n");
    return 0;
  }
  p = &table[numfuncs++];
  strncpy(p->name, name, MAXNAME);
  p->func = func;
  p->user_allowed = user_allowed;
  return numfuncs;
}

/* called by the CPU core. Parameters:
 * sp - the value of the stack pointer 
 * returns the long value that goes in register D0
 */
nf_long_t nf_get_id(nf_addr_t sp)
{
  char name[MAXNAME];
  nf_get_nstr(name, nf_get_a(sp+4), MAXNAME);
#if NFDBG
  nf_error("nf_get_id(\"%s\")\n", name);
#endif
  return find(name) << SUB_ID_SHIFT;
}

/* called by the CPU core. Parameters:
 * sp - the value of the stack pointer 
 * user_mode - non-zero if the CPU is in user mode
 * returns the long value that goes in register D0
 */
nf_long_t nf_call(nf_addr_t sp, int user_mode)
{
  nf_long_t id = nf_get_l(sp + 4);
  int upper_part = id >> SUB_ID_SHIFT;
  struct item *p;

#if NFDBG
  nf_error("nf_call(sp = %08lx, %s)\n", (long)sp, user_mode ? "user": "super");
#endif

  if(upper_part <= 0 || upper_part > numfuncs) {
    nf_error("nf_call: unknown ID %08lx at %08lx\n", id, nf_get_ul(sp));
    return 0;
  }
  p = &table[upper_part-1];
  if(user_mode && ! p->user_allowed) {
    nf_error("NatFeat %s not allowed in user mode\n", p->name);
    return 0;
  }
  return p->func(id & SUB_ID_MASK, sp + 8);
}



/*======================= CASTAWAY-SPECIFIC PART ============================*/

/*
 * Native feature basic set.
 */

#include <stdio.h>
#include <stdlib.h>

/*
 * "NF_NAME" :
 *  void getName(char *buffer, unsigned long size)
 *     subID = 0
 *     fills 'buffer' with a null-terminated string representing the
 *     emulator name. At most 'size' bytes will be written in the
 *     buffer (which means that the string will be truncated if it
 *     contains more than size - 1 characters).
 *     the string should contain only printable ASCII characters
 *     (ASCII 32 to 126 inclusive).
 *
 *  void getFullName(char *buffer, unsigned long size)
 *     subID = 1
 *     similar to getName, but the string is the emulator name plus 
 *     its version (and anything else that might be printed together 
 *     with the emulator name)
 */

extern char version[];

nf_long_t nf_name(nf_long_t sub_id, nf_addr_t sp)
{
  nf_addr_t addr = nf_get_a(sp);
  nf_ulong_t size = nf_get_ul(sp+4);
  char *str;
  switch(sub_id) {
  case 0:
    str = "Castaway";
    break;
  case 1:
    str = version;
    break;
  default:
    nf_error("NF_NAME unknown sub-id %d\n", sub_id);
    return 0;
  }
  nf_set_nstr(addr, str, size);
  return strlen(str);
}

/*
 * "NF_VERSION"
 *   long getVersion(void)
 *     returns the version of the native feature framework, hex encoded
 *     (upper word = major number, lower word = minor number). This document
 *     describes native features version 1.0 (hex encoding 0x00010000).
 */

nf_long_t nf_version(nf_long_t sub_id, nf_addr_t sp)
{
  return 0x00010000;
}

/*
 * "NF_STDERR"
 *   sub-id 0:
 *   unsigned long output(const char *string);
 *     emits a null-terminated string of printable ASCII chars on a
 *     particular output stream, intended for use to display debug 
 *     messages. Returns the number of chars printed. Characters
 *     not among the set of ASCII chars 9 (horizontal tabulation '\t'), 
 *     10, 13, (both meaning '\n' newline), 32-126 will have 
 *     implementation defined representations.
 */

nf_long_t nf_stderr(nf_long_t sub_id, nf_addr_t sp)
{
  char buf[100];
  unsigned int i = 0;
  nf_ulong_t n = 0;
  nf_addr_t addr = nf_get_a(sp);
  char c;
  while( (c = nf_get_b(addr++)) != 0) {
    if(c == 10 || c == 13) c = '\n';
    buf[i++] = c;
    if(i >= sizeof(buf)-1) {
      buf[i] = 0;
      n += i;
      fprintf(stderr, "%s", buf);
      i = 0;
    }
  }
  if(i) {
    n += i;
    buf[i] = 0;
    fprintf(stderr, "%s", buf);
  }
  return n;
}

/*
 * "NF_SHUTDOWN"
 */

nf_long_t nf_shutdown(nf_long_t sub_id, nf_addr_t sp)
{
  /* apparently atexit() is used to clean up things... */
  exit(0);
  /* should not return */
  return 0;
}

/* called by the emulator to initialise the nf manager; non-zero means error.*/
int nf_init(void)
{
  numfuncs = 0;
  nf_register_func("NF_VERSION", nf_version, 1);
  nf_register_func("NF_NAME", nf_name, 1);
  nf_register_func("NF_STDERR", nf_stderr, 1);
  nf_register_func("NF_SHUTDOWN", nf_shutdown, 1);
#if NFDBG
  nf_error("nf_init()\n");
#endif
  return 0;
}

#endif /* NATFEAT */
