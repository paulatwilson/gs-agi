/*
 * syshooks.h - the one tiny seam logic.c uses to reach "the rest of
 * the app" without knowing it exists: a single idle-pump call so
 * blocking opcodes (get.string, shake.screen, the keypressed test)
 * stay responsive instead of freezing the toolbox event loop.
 * Implemented in main.c.
 */
#ifndef INTERGELLIUM_SYSHOOKS_H
#define INTERGELLIUM_SYSHOOKS_H

void sys_idle_pump(void);

/*
 * Loads logic resource n for the room the game just entered (and any
 * picture/view/sound resources that room's logic.0 handler expects
 * to already be resident). main.c owns the on-disk resource naming
 * convention, so logic.c reaches it through this hook instead of
 * building GS/OS pathnames itself.
 */
int sys_load_room(int n);

#endif /* INTERGELLIUM_SYSHOOKS_H */
