/*
 * main.c - application skeleton: brings up the platform, runs the
 * real-time game cycle, and shuts everything back down.
 *
 * Every V_time_delay * 2 ticks of 1/60 s the loop sets the ego's
 * direction from the keyboard, runs the logic scripts and then steps
 * the animated objects (motion.c). Between cycles it keeps the screen
 * and sound serviced and reads typed keys without blocking; Enter
 * parses the input line. kbd_read_line() is only used by get.string
 * and get.num.
 *
 * Tool Locator/Memory Manager call names follow the Apple IIGS
 * Toolbox Reference; they are unverified against a real ORCA/C
 * install (see README).
 */
#include <string.h>
#include <stdio.h>

#ifdef __ORCAC__
#include <types.h>
#include <locator.h>
#include <memory.h>
#endif

#include "agitypes.h"
#include "util.h"
#include "gamestate.h"
#include "vocab.h"
#include "object.h"
#include "logic.h"
#include "motion.h"
#include "gfx.h"
#include "audio.h"
#include "video.h"
#include "keyboard.h"
#include "syshooks.h"

struct agi_game game;

#ifdef __ORCAC__
#define DEFAULT_GAME_DIR  "GAME:"	/* GS/OS pathname prefix for resources */
#else
#define DEFAULT_GAME_DIR  "game/"
#endif

static char game_dir[200] = DEFAULT_GAME_DIR;

static int startup_toolsets(void)
{
#ifdef __ORCAC__
	TLStartUp();
	MMStartUp();
#endif

	if (gfx_init() != err_OK)
		return err_NotEnoughMemory;

	kbd_init();

	if (sound_init() != err_OK)
		return err_NotEnoughMemory;

	return err_OK;
}

static void shutdown_toolsets(void)
{
	sound_shutdown();
	kbd_shutdown();
	gfx_shutdown();
#ifdef __ORCAC__
	MMShutDown();
	TLShutDown();
#endif
}

/* ---- the seams core code reaches the app through (syshooks.h) ---- */
void sys_game_path(char *out, int outsize, const char *name)
{
	int dl = (int)strlen(game_dir);
	int nl = (int)strlen(name);

	if (dl + nl >= outsize) {
		out[0] = '\0';
		return;
	}
	memcpy(out, game_dir, dl);
	memcpy(out + dl, name, nl + 1);
}

UINT32 sys_ticks(void)
{
	return video_ticks();
}

int sys_load_room(int n)
{
	char name[16], path[256];

	sprintf(name, "LOGIC.%d", n);
	sys_game_path(path, sizeof(path), name);
	return logic_load(n, path);
}

void sys_idle_pump(void)
{
	sound_service();
	gfx_flush();
}

static int load_game_resources(void)
{
	char path[256];

	sys_game_path(path, sizeof(path), "WORDS.TOK");
	if (vocab_load(path) != err_OK) {
		gfx_message_box("Can't load WORDS.TOK");
		return err_BadFileOpen;
	}

	sys_game_path(path, sizeof(path), "OBJECT");
	if (object_load(path) != err_OK) {
		gfx_message_box("Can't load OBJECT");
		return err_BadFileOpen;
	}

	if (sys_load_room(0) != err_OK) {
		gfx_message_box("Can't load LOGIC.0");
		return err_BadFileOpen;
	}

	return err_OK;
}

static void init_game_state(void)
{
	memset(&game, 0, sizeof(game));

	setvar(V_computer, 0);
	setvar(V_soundgen, 1);
	setvar(V_monitor, 0x03);
	setvar(V_max_input_chars, MAX_INPUTLEN - 2);
	setvar(V_time_delay, 2);

	setflag(F_sound_on, TRUE);
	setflag(F_new_room_exec, TRUE);
	setflag(F_logic_zero_firsttime, TRUE);

	game.clock_enabled = TRUE;
	game.status_line = TRUE;
	game.input_mode = INPUT_NORMAL;
	game.input_enabled = TRUE;
	game.line_user_input = 22;
	game.color_fg = 0;
	game.color_bg = 15;
}

/* Feeds one typed key to the parser's input line. */
static void input_key(int key, int *len)
{
	if (!game.input_enabled || game.input_mode != INPUT_NORMAL)
		return;

	if (key == KEY_ENTER) {
		if (*len > 0) {
			game.input_buffer[*len] = '\0';
			vocab_parse_input(game.input_buffer);
			*len = 0;
			game.input_buffer[0] = '\0';
		}
	} else if (key == KEY_ESCAPE) {
		*len = 0;
		game.input_buffer[0] = '\0';
	} else if (key == KEY_BACKSPACE) {
		if (*len > 0)
			game.input_buffer[--*len] = '\0';
	} else if (key >= 0x20 && key < 0x7F && *len < getvar(V_max_input_chars)) {
		game.input_buffer[(*len)++] = (char)key;
		game.input_buffer[*len] = '\0';
	} else {
		return;
	}
	gfx_update_input_line(game.input_buffer, *len);
}

static void main_loop(void)
{
	UINT32 last = sys_ticks();
	int len = 0;
	int key;

	game.input_buffer[0] = '\0';

	while (!game.quit_prog_now) {
		UINT32 now = sys_ticks();
		UINT32 period = (UINT32)getvar(V_time_delay) * 2;

		while ((key = kbd_poll()) != 0)
			input_key(key, &len);

		if (now - last >= period) {
			last = now;

			if (game.player_control)
				setvar(V_ego_dir, kbd_direction());

			logic_interpret_cycle();
			if (game.quit_prog_now)
				break;
			motion_update();

			gfx_write_status(getvar(V_score), getvar(V_max_score),
				sound_is_enabled());
			if (game.input_enabled && game.input_mode == INPUT_NORMAL)
				gfx_update_input_line(game.input_buffer, len);
		}

		sys_idle_pump();
	}
}

int main(int argc, char **argv)
{
	if (argc > 1 && strlen(argv[1]) < sizeof(game_dir) - 2) {
		int n;

		strcpy(game_dir, argv[1]);
		n = (int)strlen(game_dir);
		if (game_dir[n - 1] != '/' && game_dir[n - 1] != ':'
				&& game_dir[n - 1] != '\\') {
			game_dir[n] = '/';
			game_dir[n + 1] = '\0';
		}
	}

	if (startup_toolsets() != err_OK)
		return 1;

	init_game_state();

	if (load_game_resources() != err_OK) {
		shutdown_toolsets();
		return 1;
	}

	main_loop();

	logic_unload_all();
	object_unload();
	vocab_unload();

	shutdown_toolsets();

	return 0;
}
