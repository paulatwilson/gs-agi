/*
 * util.c - see util.h.
 */
#include "agitypes.h"
#include "util.h"
#include "gamestate.h"

#define CRYPT_KEY "Avis Durgan"

UINT8 lohi_getbyte(UINT8 *p)
{
	return p[0];
}

UINT16 lohi_getword(UINT8 *p)
{
	return (UINT16)p[0] | ((UINT16)p[1] << 8);
}

UINT32 lohi_getdword(UINT8 *p)
{
	return (UINT32)p[0] | ((UINT32)p[1] << 8) |
	       ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

int getflag(int f)
{
	int byte = f >> 3;
	int bit  = f & 7;

	if (byte < 0 || byte >= MAX_FLAGS)
		return 0;

	return (game.flags[byte] & (1 << bit)) != 0;
}

void setflag(int f, int v)
{
	int byte = f >> 3;
	int bit  = f & 7;

	if (byte < 0 || byte >= MAX_FLAGS)
		return;

	if (v)
		game.flags[byte] |= (UINT8)(1 << bit);
	else
		game.flags[byte] &= (UINT8)~(1 << bit);
}

int getvar(int v)
{
	if (v < 0 || v >= MAX_VARS)
		return 0;
	return game.vars[v];
}

void setvar(int v, int val)
{
	if (v < 0 || v >= MAX_VARS)
		return;
	game.vars[v] = (UINT8)(val & 0xff);
}

void decrypt(UINT8 *mem, int len)
{
	static const char key[] = CRYPT_KEY;
	int klen = sizeof(key) - 1;
	int i;

	for (i = 0; i < len; i++)
		mem[i] ^= (UINT8)key[i % klen];
}
