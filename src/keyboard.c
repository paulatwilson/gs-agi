/*
 * keyboard.c - the portable half of keyboard input: set.key()
 * controller matching and the blocking line editor used by
 * get.string()/get.num(). The platform half (kbd_init, kbd_poll,
 * kbd_direction) is in iigs/keyboard_iigs.c or sdl/keyboard_sdl.c.
 */
#include "agitypes.h"
#include "util.h"
#include "gamestate.h"
#include "gfx.h"
#include "keyboard.h"
#include "syshooks.h"

/*
 * Matches a raw keycode against game.ev_keyp[]'s set.key() bindings
 * (AGI's "controller" test opcode reads these back).
 */
void kbd_dispatch_controllers(int key)
{
	int i;

	for (i = 0; i < MAX_DIRS_KEYEVT; i++) {
		if (game.ev_keyp[i].data == (UINT16)key)
			game.ev_keyp[i].occurred = TRUE;
	}
}

int kbd_wait_key(void)
{
	int key;

	do {
		sys_idle_pump();
		key = kbd_poll();
	} while (key == 0 && !game.quit_prog_now);

	return key;
}

int kbd_read_line(char *buffer, int maxlen, int row, int col)
{
	int len = 0;
	int key;

	buffer[0] = '\0';
	gfx_update_input_line(buffer, 0);

	for (;;) {
		sys_idle_pump();
		if (game.quit_prog_now)
			return FALSE;
		key = kbd_poll();
		if (key == 0)
			continue;

		if (key == KEY_ENTER) {
			break;
		} else if (key == KEY_ESCAPE) {
			buffer[0] = '\0';
			return FALSE;
		} else if (key == KEY_BACKSPACE) {
			if (len > 0)
				buffer[--len] = '\0';
		} else if (key >= 0x20 && key < 0x7F && len < maxlen) {
			buffer[len++] = (char)key;
			buffer[len] = '\0';
		}

		gfx_update_input_line(buffer, len);
	}

	(void)row;
	(void)col;
	return TRUE;
}
