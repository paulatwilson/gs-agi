/*
 * util.h - little-endian byte helpers, var/flag accessors, and the
 * Sierra AGI text-message XOR decrypt. Pulled out of Sarien's
 * sarien.h/global.c; no platform dependency at all.
 */
#ifndef GSAGI_UTIL_H
#define GSAGI_UTIL_H

#include "agitypes.h"

UINT8  lohi_getbyte(UINT8 *p);
UINT16 lohi_getword(UINT8 *p);
UINT32 lohi_getdword(UINT8 *p);

int  getflag(int f);
void setflag(int f, int v);
int  getvar(int v);
void setvar(int v, int val);

/* XORs resource bytes against the Sierra "Avis Durgan" key stream. */
void decrypt(UINT8 *mem, int len);

#endif /* GSAGI_UTIL_H */
