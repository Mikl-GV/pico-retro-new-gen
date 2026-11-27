// m68k_core.h -- 68000 core selection for arcade builds (r1738, Cyclone pilot).
//
// Default: Musashi (the tested FBNeo core). Cyclone is enabled only with an
// explicit build flag:
//
//   make ARC_CYCLONE=1
//
// The Cyclone path uses libretro-FBNeo's Sek-compatible c68000_intf.cpp plus a
// byte-swapped fetch-page shadow (see c68000_intf.cpp), because Cyclone runs
// opcodes directly from memory. Musashi remains the fallback: build without the
// flag.
//
// Limitations of the pilot: 68000 only (Psikyo 68EC020 stays Musashi), Sega
// System 16 multi-68K is not validated, and large ROM shadows may not fit the
// FBNeo pool on Neo Geo/CPS-2.

#ifndef M68K_CORE_H
#define M68K_CORE_H

#if defined(ARC_CYCLONE) && (ARC_CYCLONE != 0)
#define ARC_M68K_CYCLONE 1
#else
#define ARC_M68K_CYCLONE 0
#endif

#endif
