/*
 * keyboard_iigs.c - keyboard polling via the GS/OS Event Manager.
 *
 * Event Manager call names/shapes follow the Apple IIGS Toolbox
 * Reference (Event Manager chapter); as with gfx_iigs.c/sound_iigs.c,
 * verify against your installed ORCA/C <event.h> before relying on
 * this to compile unmodified.
 */
#include <types.h>
#include <event.h>

#include "../agitypes.h"
#include "../keyboard.h"
#include "../gamestate.h"

static int inited = 0;
static int heading = 0;	/* sticky direction set by arrow-key taps */

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
	case 0x7F: return KEY_BACKSPACE;	/* the delete key */
	case 0x08: return KEY_LEFT;
	case 0x15: return KEY_RIGHT;
	case 0x0A: return KEY_DOWN;
	case 0x0B: return KEY_UP;
	default:   return ch;
	}
}

int kbd_direction(void)
{
	return heading;
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
	{
		int dir = key == KEY_UP ? 1 : key == KEY_RIGHT ? 3 :
			key == KEY_DOWN ? 5 : key == KEY_LEFT ? 7 : 0;

		if (dir)
			heading = (heading == dir) ? 0 : dir;
		else if (key == ' ')
			heading = 0;
	}
	game.keypress = key;
	kbd_dispatch_controllers(key);

	return key;
}
