/*
 * gamestate.h - the engine-wide game state, shared by vocab.c, logic.c
 * and object.c. No SDL, no host OS types: everything here is plain
 * ANSI C sized for the 65816 (agitypes.h).
 *
 * Trimmed from Sarien's struct agi_game (src/agi.h): dropped the
 * mouse/menu/console-debugger/save-dialog fields that belonged to the
 * SDL front end, and the raw resource-directory bookkeeping that
 * belongs to the GS/OS resource loader, not to the logic core.
 */
#ifndef GSAGI_GAMESTATE_H
#define GSAGI_GAMESTATE_H

#include "agitypes.h"

/* ---- AGI standard variables (same indices as the original engine) --- */
enum {
	V_cur_room = 0,
	V_prev_room,
	V_border_touch_ego,
	V_score,
	V_border_code,
	V_border_touch_obj,
	V_ego_dir,
	V_max_score,
	V_free_pages,
	V_word_not_found,
	V_time_delay,
	V_seconds,
	V_minutes,
	V_hours,
	V_days,
	V_joystick_sensitivity,
	V_ego_view_resource,
	V_agi_err_code,
	V_agi_err_code_info,
	V_key,
	V_computer,
	V_window_reset,
	V_soundgen,
	V_volume,
	V_max_input_chars,
	V_sel_item,
	V_monitor
};

/* ---- AGI standard flags ---- */
enum {
	F_ego_water = 0,
	F_ego_invisible,
	F_entered_cli,
	F_ego_touched_p2,
	F_said_accepted_input,
	F_new_room_exec,
	F_restart_game,
	F_script_blocked,
	F_joy_sensitivity,
	F_sound_on,
	F_logic_zero_firsttime,
	F_restore_just_ran,
	F_status_selects_items,
	F_menus_work,
	F_output_mode,
	F_auto_restart
};

/* ---- input modes ---- */
enum {
	INPUT_NORMAL = 1,
	INPUT_GETSTRING,
	INPUT_MENU,
	INPUT_NONE
};

/* ---- a word of player input, resolved against the dictionary ---- */
struct agi_word {
	int  id;
	char word[MAX_WORD_LEN];
};

/* ---- one inventory/room object ---- */
struct agi_object {
	int  location;			/* room number, or EGO_OWNED	*/
	char name[MAX_OBJNAMELEN];
};

/* ---- one loaded logic script ---- */
struct agi_logic {
	UINT8  *data;			/* raw bytecode, malloc'd	*/
	int     size;			/* end-of-code pointer		*/
	int     sIP;			/* scan-start IP (set.scan.start) */
	int     cIP;			/* current IP			*/
	int     num_texts;
	char  **texts;			/* pointers into data's message block */
	BOOL_T  loaded;
};

/*
 * One entry in the animated-object table ("view table" in AGI terms).
 * This engine does not itself decode AGI view/cel resource bytes --
 * that belongs to gfx_iigs.c, which keeps its own cel cache keyed by
 * current_view/current_loop/current_cel. logic.c only ever pokes the
 * numbers; it never looks at pixels.
 */
struct vt_entry {
	UINT8   entry;

	SINT16  x_pos, y_pos;
	SINT16  x_pos2, y_pos2;	/* previous position, for erase	*/
	SINT16  x_size, y_size;

	UINT8   current_view;
	UINT8   current_loop, num_loops;
	UINT8   current_cel, num_cels;

	UINT8   step_size;
	UINT16  step_frac;		/* sub-pixel remainder, 1/256 px */
	UINT8   step_time, step_time_count;
	UINT8   cycle_time, cycle_time_count;
	UINT8   direction;

#define MOTION_NORMAL      0
#define MOTION_WANDER      1
#define MOTION_FOLLOW_EGO  2
#define MOTION_MOVE_OBJ    3
	UINT8   motion;

#define CYCLE_NORMAL       0
#define CYCLE_END_OF_LOOP  1
#define CYCLE_REV_LOOP     2
#define CYCLE_REVERSE      3
	UINT8   cycle;

	UINT8   priority;

#define VT_DRAWN           0x0001
#define VT_IGNORE_BLOCKS   0x0002
#define VT_FIXED_PRIORITY  0x0004
#define VT_IGNORE_HORIZON  0x0008
#define VT_UPDATE          0x0010
#define VT_CYCLING         0x0020
#define VT_ANIMATED        0x0040
#define VT_ON_WATER        0x0100
#define VT_IGNORE_OBJECTS  0x0200
#define VT_UPDATE_POS      0x0400
#define VT_ON_LAND         0x0800
#define VT_DONTUPDATE      0x1000
#define VT_FIX_LOOP        0x2000
#define VT_ADJ_EGO_XY      0x8000
	UINT16  flags;

	UINT8   parm1, parm2, parm3, parm4;
};

#define for_each_vt_entry(x) \
	for ((x) = game.view_table; (x) < &game.view_table[MAX_VIEWTABLE]; (x)++)

/* ---- a controller (keyboard "set.key" binding) event ---- */
struct agi_event {
	UINT16 data;
	UINT8  occurred;
};

#define MAX_DIRS_KEYEVT 32

/*
 * The whole engine, minus anything that is strictly presentation
 * (screen buffers live in gfx_iigs.c; sound channel state lives in
 * sound_iigs.c). This struct is deliberately small enough to sit in
 * one static far-data segment.
 */
struct agi_game {
	UINT8  flags[MAX_FLAGS];
	UINT8  vars[MAX_VARS];

	int    horizon;
	int    line_status, line_user_input, line_min_print;

	char   input_buffer[MAX_INPUTLEN];
	char   echo_buffer[MAX_INPUTLEN];
	int    cursor_pos;
	int    keypress;

	int    input_mode;
	int    input_enabled;
	int    lognum;			/* current logic number		*/

	int    player_control;
	int    quit_prog_now;
	int    status_line;
	int    clock_enabled;
	int    exit_all_logics;
	int    has_prompt;

	int    gfx_mode;		/* TRUE = graphics, FALSE = text	*/
	int    color_fg, color_bg;
	char   cursor_char;
	int    picture_shown;
	int    has_window;

	struct agi_word   ego_words[MAX_WORDS];
	int               num_ego_words;

	unsigned int      num_objects;

	struct agi_event  ev_keyp[MAX_DIRS_KEYEVT];

	char   strings[MAX_STRINGS + 1][MAX_STRINGLEN];

	struct agi_logic  logics[MAX_DIRS];

	struct vt_entry   view_table[MAX_VIEWTABLE];

	int    simple_save;
};

extern struct agi_game game;

#endif /* GSAGI_GAMESTATE_H */
