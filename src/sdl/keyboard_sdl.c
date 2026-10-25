/*
 * keyboard_sdl.c - SDL2 keyboard polling for host builds. Printable
 * characters come from SDL_TEXTINPUT (so layouts and shift work);
 * editing keys come from SDL_KEYDOWN; the walking direction is read
 * from the arrow keys' held state.
 *
 * Test aid: GSAGI_SCRIPT drives the keyboard without a human, e.g.
 *   GSAGI_SCRIPT="500:+right;1500:-right;1600:text=look tree;1700:enter;3000:quit"
 * Each entry is <milliseconds since start>:<action>. Actions: +up -up
 * +down -down +left -left +right -right (hold/release an arrow),
 * text=<chars>, enter, esc, dump=<file.ppm> (save the frame), quit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "../agitypes.h"
#include "../gamestate.h"
#include "../keyboard.h"
#include "../gfx.h"

#define QUEUE_SIZE 64
#define MAX_SCRIPT 128

static int queue[QUEUE_SIZE];
static int q_head = 0;
static int q_count = 0;

struct step {
	Uint32 at;
	char   action[300];
};
static struct step script[MAX_SCRIPT];
static int nscript = 0, next_step = 0;
static int script_held = 0;		/* bit 0 up, 1 right, 2 down, 3 left */

static void push_key(int key)
{
	if (q_count == QUEUE_SIZE)
		return;
	queue[(q_head + q_count) % QUEUE_SIZE] = key;
	q_count++;
}

static void load_script(void)
{
	const char *env = getenv("GSAGI_SCRIPT");
	char *copy, *tok;

	if (env == NULL)
		return;
	copy = strdup(env);
	for (tok = strtok(copy, ";"); tok && nscript < MAX_SCRIPT; tok = strtok(NULL, ";")) {
		char *colon = strchr(tok, ':');

		if (colon == NULL)
			continue;
		*colon = '\0';
		script[nscript].at = (Uint32)atoi(tok);
		strncpy(script[nscript].action, colon + 1, sizeof(script[0].action) - 1);
		nscript++;
	}
	free(copy);
}

static void run_script(void)
{
	Uint32 now = SDL_GetTicks();

	while (next_step < nscript && script[next_step].at <= now) {
		const char *a = script[next_step++].action;
		int bit = 0;

		if (strstr(a, "up"))    bit = 1;
		if (strstr(a, "right")) bit = 2;
		if (strstr(a, "down"))  bit = 4;
		if (strstr(a, "left"))  bit = 8;

		if ((a[0] == '+' || a[0] == '-') && bit) {
			if (a[0] == '+') script_held |= bit; else script_held &= ~bit;
		} else if (strncmp(a, "text=", 5) == 0) {
			const char *p;

			for (p = a + 5; *p; p++)
				push_key(*p);
		} else if (strcmp(a, "enter") == 0) {
			push_key(KEY_ENTER);
		} else if (strcmp(a, "esc") == 0) {
			push_key(KEY_ESCAPE);
		} else if (strncmp(a, "dump=", 5) == 0) {
			gfx_dump_frame(a + 5);
		} else if (strcmp(a, "quit") == 0) {
			game.quit_prog_now = TRUE;
		}
	}
}

void kbd_init(void)
{
	SDL_StartTextInput();
	load_script();
}

void kbd_shutdown(void)
{
	SDL_StopTextInput();
}

static void handle_event(SDL_Event *ev)
{
	const char *p;

	switch (ev->type) {
	case SDL_QUIT:
		game.quit_prog_now = TRUE;
		break;
	case SDL_TEXTINPUT:
		for (p = ev->text.text; *p; p++) {
			if ((unsigned char)*p >= 0x20 && (unsigned char)*p < 0x7F)
				push_key(*p);
		}
		break;
	case SDL_KEYDOWN:
		switch (ev->key.keysym.sym) {
		case SDLK_RETURN:
		case SDLK_KP_ENTER:  push_key(KEY_ENTER);     break;
		case SDLK_ESCAPE:    push_key(KEY_ESCAPE);    break;
		case SDLK_BACKSPACE:
		case SDLK_DELETE:    push_key(KEY_BACKSPACE); break;
		case SDLK_UP:        push_key(KEY_UP);        break;
		case SDLK_DOWN:      push_key(KEY_DOWN);      break;
		case SDLK_LEFT:      push_key(KEY_LEFT);      break;
		case SDLK_RIGHT:     push_key(KEY_RIGHT);     break;
		default:             break;
		}
		break;
	default:
		break;
	}
}

int kbd_direction(void)
{
	static const int table[16] = {	/* up=1 right=2 down=4 left=8 */
		0, 1, 3, 2, 5, 0, 4, 3, 7, 8, 0, 1, 6, 7, 5, 0
	};
	const Uint8 *k;
	int mask;

	SDL_PumpEvents();
	run_script();
	k = SDL_GetKeyboardState(NULL);
	mask = script_held;
	if (k[SDL_SCANCODE_UP])    mask |= 1;
	if (k[SDL_SCANCODE_RIGHT]) mask |= 2;
	if (k[SDL_SCANCODE_DOWN])  mask |= 4;
	if (k[SDL_SCANCODE_LEFT])  mask |= 8;
	/* table maps the four bits to AGI's 1-8; opposite keys cancel */
	return table[mask & 15];
}

int kbd_poll(void)
{
	SDL_Event ev;
	int key;

	run_script();
	while (SDL_PollEvent(&ev))
		handle_event(&ev);

	if (q_count == 0) {
		SDL_Delay(1);	/* callers spin on this; don't burn a core */
		return 0;
	}

	key = queue[q_head];
	q_head = (q_head + 1) % QUEUE_SIZE;
	q_count--;

	game.keypress = key;
	kbd_dispatch_controllers(key);
	return key;
}
