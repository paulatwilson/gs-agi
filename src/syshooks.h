/*
 * syshooks.h - the one tiny seam logic.c uses to reach "the rest of
 * the app" without knowing it exists: a single idle-pump call so
 * blocking opcodes (get.string, shake.screen, the keypressed test)
 * stay responsive instead of freezing the toolbox event loop.
 * Implemented in main.c.
 */
#ifndef GSAGI_SYSHOOKS_H
#define GSAGI_SYSHOOKS_H

#include "agitypes.h"

void sys_idle_pump(void);

/*
 * Loads logic resource n for the room the game just entered (and any
 * picture/view/sound resources that room's logic.0 handler expects
 * to already be resident). main.c owns the on-disk resource naming
 * convention, so logic.c reaches it through this hook instead of
 * building GS/OS pathnames itself.
 */
int sys_load_room(int n);

/*
 * Builds the path of resource file name (e.g. "PIC.1") inside the game
 * directory into out, which holds outsize bytes. SDL: the directory
 * given on the command line, default "game/". IIgs: "GAME:".
 */
void sys_game_path(char *out, int outsize, const char *name);

/* Time in 1/60 s ticks since startup. */
UINT32 sys_ticks(void);

#endif /* GSAGI_SYSHOOKS_H */
