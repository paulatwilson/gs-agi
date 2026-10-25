/*
 * keyboard.h - keyboard input for the text parser: polls the GS/OS
 * Event Manager / toolbox, and runs the line-editor used both for
 * the normal command prompt and for AGI's get.string()/get.num().
 */
#ifndef GSAGI_KEYBOARD_H
#define GSAGI_KEYBOARD_H

#include "agitypes.h"

#define KEY_ENTER   0x0D
#define KEY_ESCAPE  0x1B
#define KEY_BACKSPACE 0x08
#define KEY_UP      0x8B
#define KEY_DOWN    0x8A
#define KEY_LEFT    0x88
#define KEY_RIGHT   0x15

void kbd_init(void);
void kbd_shutdown(void);

/*
 * Walking direction wanted right now, AGI numbering (0 none, 1 up,
 * 2 up-right ... 8 up-left). Host: arrow keys held; IIgs: sticky
 * heading set by arrow-key taps.
 */
int  kbd_direction(void);

/* Marks every set.key() binding that matches key as occurred. */
void kbd_dispatch_controllers(int key);

/* Non-blocking: returns 0 if no key is waiting. */
int  kbd_poll(void);

/*
 * Blocking line editor for get.string()/get.num() and the normal
 * command prompt. Pumps the screen/sound each iteration via
 * sys_idle_pump() (see syshooks.h) so the game doesn't freeze while
 * the player types. Returns FALSE if the player pressed Escape.
 */
int  kbd_read_line(char *buffer, int maxlen, int row, int col);

/* Waits for, consumes, and returns the next keypress. */
int  kbd_wait_key(void);

#endif /* GSAGI_KEYBOARD_H */
