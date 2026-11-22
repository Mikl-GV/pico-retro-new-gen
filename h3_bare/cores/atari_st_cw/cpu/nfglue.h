/*
 * nfglue.h - NatFeat glue for the Castaway emulator
 * 
 * Copyright (c) 2002, 03 Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See http://www.gnu.org/copyleft/gpl.html
 */

#ifndef NFGLUE_H
#define NFGLUE_H

/* types */

typedef char nf_byte_t;
typedef unsigned char nf_ubyte_t;

typedef short nf_word_t;
typedef unsigned short nf_uword_t;

typedef long nf_long_t;
typedef unsigned long nf_ulong_t;

typedef nf_ulong_t nf_addr_t;
#define nf_get_a(a) nf_get_l(a)
#define nf_set_a(a,v) nf_set_l(a,v)

#endif /* NFGLUE_H */
