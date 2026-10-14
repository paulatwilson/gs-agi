/*
 * main.c - application skeleton: brings up the GS/OS toolsets,
 * drives the text-parser main loop, and shuts everything back down
 * cleanly.
 *
 * This is deliberately a simple, turn-based cadence (run the logic,
 * then block for one typed line) rather than a true real-time frame
 * loop -- there is no motion-stepper module in this engine yet (see
 * gfx_iigs.h) for animated objects to need polling between
 * keystrokes. When that lands, swap the blocking kbd_read_line() call
 * below for non-blocking kbd_poll() inside the same while loop; every
 * other piece here stays the same.
 *
 * Tool Locator/Memory Manager call names follow the Apple IIGS
 * Toolbox Reference; verify against your ORCA/C <locator.h>/
 * <memory.h> before trusting this to link unmodified -- see the
 * same note in gfx_iigs.c/sound_iigs.c/keyboard.c.
 */
#include <string.h>
#include <stdio.h>

#include <types.h>
#include <locator.h>
#include <memory.h>

#include "agitypes.h"
#include "util.h"
#include "gamestate.h"
#include "vocab.h"
#include "object.h"
#include "logic.h"
#include "gfx_iigs.h"
#include "sound_iigs.h"
#include "keyboard.h"
#include "syshooks.h"

struct agi_game game;

#define GAME_DIR  "GAME:"	/* GS/OS pathname prefix for resources */

static int startup_toolsets(void)
{
	TLStartUp();
	MMStartUp();

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
	MMShutDown();
	TLShutDown();
}

/* ---- the one filesystem-naming seam logic.c reaches through ---- */
int sys_load_room(int n)
{
	char path[64];
	sprintf(path, GAME_DIR "LOGIC.%d", n);
	return logic_load(n, path);
}

void sys_idle_pump(void)
{
	sound_service();
	gfx_flush();
}

static int load_game_resources(void)
{
	if (vocab_load(GAME_DIR "WORDS.TOK") != err_OK) {
		gfx_message_box("Can't load WORDS.TOK");
		return err_BadFileOpen;
	}

	if (object_load(GAME_DIR "OBJECT") != err_OK) {
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

static void main_loop(void)
{
	char line[MAX_INPUTLEN];

	while (!game.quit_prog_now) {
		logic_interpret_cycle();

		gfx_write_status(getvar(V_score), getvar(V_max_score),
			sound_is_enabled());
		gfx_flush();

		if (game.quit_prog_now)
			break;

		if (game.input_enabled && game.input_mode == INPUT_NORMAL) {
			gfx_write_prompt();
			if (kbd_read_line(line, sizeof(line) - 1, 0, 0)) {
				strncpy(game.input_buffer, line, MAX_INPUTLEN - 1);
				vocab_parse_input(game.input_buffer);
			}
		} else {
			sys_idle_pump();
		}
	}
}

int main(void)
{
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
