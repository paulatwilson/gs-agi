/*
 * keyboard.c - keyboard polling via the GS/OS Event Manager, plus the
 * line editor used for the parser prompt and get.string()/get.num().
 *
 * Event Manager call names/shapes follow the Apple IIGS Toolbox
 * Reference (Event Manager chapter); as with gfx_iigs.c/sound_iigs.c,
 * verify against your installed ORCA/C <event.h> before relying on
 * this to compile unmodified.
 */
#include <string.h>

#include <types.h>
#include <event.h>

#include "agitypes.h"
#include "util.h"
#include "gamestate.h"
#include "gfx_iigs.h"
#include "keyboard.h"
#include "syshooks.h"

static int inited = 0;

void kbd_init(void)
{
	EMStartUp();
	FlushEvents(everyEvent, 0);
	inited = 1;
}

void kbd_shutdown(void)
{
	if (!inited)
		return;
	EMShutDown();
	inited = 0;
}

static int translate_key(EventRecord *ev)
{
	int ch = ev->message & 0xff;

	switch (ch) {
	case 0x0D: return KEY_ENTER;
	case 0x1B: return KEY_ESCAPE;
	case 0x08: return KEY_BACKSPACE;
	case 0x7F: return KEY_BACKSPACE;
	default:   return ch;
	}
}

/*
 * Matches a raw keycode against game.ev_keyp[]'s set.key() bindings
 * (AGI's "controller" test opcode reads these back).
 */
static void dispatch_controllers(int key)
{
	int i;
	for (i = 0; i < MAX_DIRS_KEYEVT; i++) {
		if (game.ev_keyp[i].data == (UINT16)key)
			game.ev_keyp[i].occurred = TRUE;
	}
}

int kbd_poll(void)
{
	EventRecord ev;
	int key;

	if (!GetNextEvent(everyEvent, &ev, 0, NULL))
		return 0;

	if (ev.what != keyDownEvt && ev.what != autoKeyEvt)
		return 0;

	key = translate_key(&ev);
	game.keypress = key;
	dispatch_controllers(key);

	return key;
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
