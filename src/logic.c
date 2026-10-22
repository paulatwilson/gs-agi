/*
 * logic.c - the AGI bytecode logic interpreter.
 *
 * Extracted and merged from three Sarien files that together made up
 * one subsystem:
 *   - src/logic.c    decode_logic()/unload_logic() (message-block
 *                     unpacking for a loaded LOGIC resource)
 *   - src/op_cmd.c    run_logic() and the ~183 action opcodes
 *   - src/op_test.c   test_if_code() and the test opcodes used by
 *                     "if" statements
 *
 * Every opcode that originally reached into graphics.c/sprite.c/
 * menu.c/savegame.c/keyboard.c now calls through gfx_iigs.h,
 * sound_iigs.h or keyboard.h instead -- those three headers are the
 * entire platform surface this file touches. Nothing here includes
 * a QuickDraw, Sound Manager or GS/OS header.
 *
 * Deliberately out of scope (see gfx_iigs.h/README): the AGI
 * PICTURE/VIEW resource decoders and the per-frame motion stepper
 * (motion.c in the original). Opcodes that exist only to drive those
 * subsystems still update their bookkeeping fields (vt_entry flags,
 * parms) exactly as before so scripts don't break when those
 * subsystems land later -- they just don't animate anything yet.
 */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

#include "agitypes.h"
#include "util.h"
#include "gamestate.h"
#include "logic.h"
#include "object.h"
#include "vocab.h"
#include "gfx_iigs.h"
#include "sound_iigs.h"
#include "keyboard.h"
#include "syshooks.h"

/* ------------------------------------------------------------------ */
/* Opcode argument-byte counts (from Sarien's logic_names_test/_cmd). */
/* ------------------------------------------------------------------ */

static const UINT8 test_num_args[19] = {
	0, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 5, 1, 0, 0, 2, 5, 5, 5
};

static const UINT8 cmd_num_args[183] = {
	0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1,	/* 0x00-0x0F */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1,	/* 0x10-0x1F */
	1, 1, 0, 1, 1, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 2,	/* 0x20-0x2F */
	2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1,	/* 0x30-0x3F */
	1, 1, 1, 1, 1, 3, 1, 1, 1, 2, 1, 2, 2, 1, 1, 2,	/* 0x40-0x4F */
	2, 5, 5, 3, 1, 1, 2, 2, 1, 1, 4, 0, 1, 1, 1, 2,	/* 0x50-0x5F */
	2, 2, 1, 2, 0, 1, 1, 3, 3, 3, 0, 0, 1, 2, 1, 3,	/* 0x60-0x6F */
	0, 0, 2, 5, 2, 1, 2, 0, 0, 3, 7, 7, 0, 0, 0, 0,	/* 0x70-0x7F */
	0, 1, 3, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1,	/* 0x80-0x8F */
	1, 0, 0, 3, 3, 0, 3, 4, 4, 1, 5, 2, 1, 2, 0, 1,	/* 0x90-0x9F */
	1, 0, 1, 0, 0, 2, 2, 2, 2, 0, 1, 0, 0, 0, 1, 1,	/* 0xA0-0xAF */
	0, 1, 0, 4, 2, 0, 0				/* 0xB0-0xB6 */
};

#define NUM_CMD_OPCODES ((int)(sizeof(cmd_num_args) / sizeof(cmd_num_args[0])))

/* ------------------------------------------------------------------ */
/* Resource loading: unpack a raw LOGIC resource into bytecode + text */
/* ------------------------------------------------------------------ */

static int decode_logic(int n, UINT8 *mem, UINT32 flen)
{
	struct agi_logic *lg = &game.logics[n];
	UINT32 mstart, mend;
	UINT8  mc;
	UINT8 *m0;
	UINT32 i;

	lg->data = mem;
	lg->size = lohi_getword(mem) + 2;

	mstart = lohi_getword(mem) + 2;
	if (mstart + 3 > flen) {
		lg->num_texts = 0;
		lg->texts = NULL;
		lg->sIP = lg->cIP = 2;
		lg->loaded = TRUE;
		return err_OK;
	}

	mc = mem[mstart];
	mend = lohi_getword(mem + mstart + 1);
	m0 = mem + mstart + 3;

	if (mc > 0 && mend > (UINT32)(mc << 1))
		decrypt(m0 + (mc << 1), (int)(mend - (mc << 1)));

	lg->num_texts = mc;
	lg->sIP = 2;
	lg->cIP = 2;

	if (mc == 0) {
		lg->texts = NULL;
	} else {
		lg->texts = (char **)calloc((size_t)mc, sizeof(char *));
		if (lg->texts == NULL) {
			free(mem);
			lg->data = NULL;
			return err_NotEnoughMemory;
		}
		for (i = 0; i < mc; i++) {
			UINT16 off = lohi_getword(m0 + i * 2);
			lg->texts[i] = off ? (char *)m0 + off - 2 : "";
		}
	}

	lg->loaded = TRUE;
	return err_OK;
}

int logic_load(int n, const char *path)
{
	FILE  *fp;
	long   flen;
	UINT8 *mem;
	int    ec;

	if (n < 0 || n >= MAX_DIRS)
		return err_BadResource;

	logic_unload(n);

	fp = fopen(path, "rb");
	if (fp == NULL)
		return err_BadFileOpen;

	fseek(fp, 0, SEEK_END);
	flen = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	mem = (UINT8 *)malloc((size_t)flen);
	if (mem == NULL) {
		fclose(fp);
		return err_NotEnoughMemory;
	}

	fread(mem, 1, (size_t)flen, fp);
	fclose(fp);

	ec = decode_logic(n, mem, (UINT32)flen);
	if (ec != err_OK)
		free(mem);

	return ec;
}

void logic_unload(int n)
{
	struct agi_logic *lg;

	if (n < 0 || n >= MAX_DIRS)
		return;

	lg = &game.logics[n];
	if (lg->loaded) {
		free(lg->data);
		if (lg->texts != NULL)
			free(lg->texts);
	}
	lg->data = NULL;
	lg->texts = NULL;
	lg->num_texts = 0;
	lg->sIP = lg->cIP = 2;
	lg->loaded = FALSE;
}

void logic_unload_all(void)
{
	int n;
	for (n = 0; n < MAX_DIRS; n++)
		logic_unload(n);
}

/* ------------------------------------------------------------------ */
/* new.room()                                                        */
/* ------------------------------------------------------------------ */

static void logic_new_room(int n)
{
	struct vt_entry *v;
	int i = 0;

	sound_stop();

	for_each_vt_entry(v) {
		v->entry = (UINT8)i++;
		v->flags &= (UINT16)~(VT_ANIMATED | VT_DRAWN);
		v->flags |= VT_UPDATE;
		v->step_time = v->step_time_count = 1;
		v->cycle_time = v->cycle_time_count = 1;
		v->step_size = 1;
	}

	game.player_control = TRUE;
	game.horizon = 36;
	setvar(V_prev_room, getvar(V_cur_room));
	setvar(V_cur_room, n);
	setvar(V_border_touch_obj, 0);
	setvar(V_border_code, 0);

	sys_load_room(n);

	setflag(F_new_room_exec, TRUE);
	game.exit_all_logics = TRUE;
}

/* ------------------------------------------------------------------ */
/* Action opcodes                                                     */
/* ------------------------------------------------------------------ */

static struct agi_logic *cur_logic;

#define ip   (cur_logic->cIP)
#define _v   (game.vars)
#define p0   (p[0])
#define p1   (p[1])
#define p2   (p[2])
#define p3   (p[3])
#define p4   (p[4])
#define p5   (p[5])
#define p6   (p[6])
#define vt   (game.view_table[p0])

#define cmd(x) static void cmd_##x(UINT8 *p)

static void clamp_position(struct vt_entry *v)
{
	if (v->x_pos < 0)
		v->x_pos = 0;
	if (v->x_pos + v->x_size > GFX_WIDTH)
		v->x_pos = (SINT16)(GFX_WIDTH - v->x_size);
	if (v->y_pos < game.horizon)
		v->y_pos = (SINT16)game.horizon;
	if (v->y_pos >= GFX_HEIGHT)
		v->y_pos = GFX_HEIGHT - 1;
}

cmd(increment)   { if (_v[p0] != 0xff) ++_v[p0]; }
cmd(decrement)   { if (_v[p0] != 0) --_v[p0]; }
cmd(assignn)     { _v[p0] = p1; }
cmd(assignv)     { _v[p0] = _v[p1]; }
cmd(addn)        { _v[p0] = (UINT8)(_v[p0] + p1); }
cmd(addv)        { _v[p0] = (UINT8)(_v[p0] + _v[p1]); }
cmd(subn)        { _v[p0] = (UINT8)(_v[p0] - p1); }
cmd(subv)        { _v[p0] = (UINT8)(_v[p0] - _v[p1]); }
cmd(lindirectv)  { _v[_v[p0]] = _v[p1]; }
cmd(rindirect)   { _v[p0] = _v[_v[p1]]; }
cmd(lindirectn)  { _v[_v[p0]] = p1; }
cmd(set)         { setflag(p0, TRUE); }
cmd(reset)       { setflag(p0, FALSE); }
cmd(toggle)      { setflag(p0, !getflag(p0)); }
cmd(set_v)       { setflag(_v[p0], TRUE); }
cmd(reset_v)     { setflag(_v[p0], FALSE); }
cmd(toggle_v)    { setflag(_v[p0], !getflag(_v[p0])); }
cmd(new_room)    { logic_new_room(p0); }
cmd(new_room_f)  { logic_new_room(_v[p0]); }
cmd(load_logic)  { /* resident set lazily loaded via sys_load_room */ }
cmd(load_logic_f){ }
cmd(call) {
	int old_cIP, old_lognum;

	old_cIP = cur_logic->cIP;
	old_lognum = game.lognum;

	logic_run(p0);

	game.lognum = old_lognum;
	cur_logic = &game.logics[game.lognum];
	cur_logic->cIP = old_cIP;
}
cmd(call_f)      { cmd_call(&_v[p0]); }
cmd(load_pic)    { gfx_load_picture(_v[p0]); }
cmd(draw_pic)    { gfx_draw_picture(_v[p0], TRUE); game.picture_shown = 0; }
cmd(show_pic)    { gfx_show_picture(); game.picture_shown = 1; }
cmd(discard_pic) { }
cmd(overlay_pic) { gfx_draw_picture(_v[p0], FALSE); game.picture_shown = 0; }
cmd(show_pri_screen) { gfx_show_priority_screen(); }
cmd(load_view)   { }
cmd(load_view_f) { }
cmd(discard_view){ }
cmd(animate_obj) {
	if (vt.flags & VT_ANIMATED)
		return;
	vt.flags = VT_ANIMATED | VT_UPDATE | VT_CYCLING;
	vt.motion = MOTION_NORMAL;
	vt.cycle = CYCLE_NORMAL;
	vt.direction = 0;
}
cmd(unanimate_all) {
	int i;
	for (i = 0; i < MAX_VIEWTABLE; i++)
		game.view_table[i].flags &= (UINT16)~(VT_ANIMATED | VT_DRAWN);
}
cmd(draw) {
	if (vt.flags & VT_DRAWN)
		return;
	if (vt.x_size <= 0 || vt.y_size <= 0)
		return;
	vt.flags |= VT_UPDATE | VT_DRAWN;
	clamp_position(&vt);
	vt.x_pos2 = vt.x_pos;
	vt.y_pos2 = vt.y_pos;
	gfx_draw_object(p0);
}
cmd(erase) {
	if (~vt.flags & VT_DRAWN)
		return;
	gfx_erase_object(p0);
	vt.flags &= (UINT16)~VT_DRAWN;
}
cmd(position)    { vt.x_pos = vt.x_pos2 = (SINT16)p1; vt.y_pos = vt.y_pos2 = (SINT16)p2; }
cmd(position_f)  { vt.x_pos = vt.x_pos2 = (SINT16)_v[p1]; vt.y_pos = vt.y_pos2 = (SINT16)_v[p2]; }
cmd(get_posn)    { _v[p1] = (UINT8)vt.x_pos; _v[p2] = (UINT8)vt.y_pos; }
cmd(reposition) {
	SINT8 dx = (SINT8)_v[p1], dy = (SINT8)_v[p2];

	vt.flags |= VT_UPDATE_POS;

	if (dx < 0 && vt.x_pos < -dx)
		vt.x_pos = 0;
	else
		vt.x_pos = (SINT16)(vt.x_pos + dx);

	if (dy < 0 && vt.y_pos < -dy)
		vt.y_pos = 0;
	else
		vt.y_pos = (SINT16)(vt.y_pos + dy);

	clamp_position(&vt);
}
cmd(set_view)     { vt.current_view = p1; }
cmd(set_view_f)   { vt.current_view = _v[p1]; }
cmd(set_loop)     { vt.current_loop = p1; gfx_set_loop(p0, p1); }
cmd(set_loop_f)   { vt.current_loop = _v[p1]; gfx_set_loop(p0, _v[p1]); }
cmd(fix_loop)     { vt.flags |= VT_FIX_LOOP; }
cmd(release_loop) { vt.flags &= (UINT16)~VT_FIX_LOOP; }
cmd(set_cel)      { vt.current_cel = p1; gfx_set_cel(p0, p1); vt.flags &= (UINT16)~VT_DONTUPDATE; }
cmd(set_cel_f)    { vt.current_cel = _v[p1]; gfx_set_cel(p0, _v[p1]); vt.flags &= (UINT16)~VT_DONTUPDATE; }
cmd(last_cel)     { _v[p1] = vt.num_cels ? (UINT8)(vt.num_cels - 1) : 0; }
cmd(current_cel)  { _v[p1] = vt.current_cel; }
cmd(current_loop) { _v[p1] = vt.current_loop; }
cmd(current_view) { _v[p1] = vt.current_view; }
cmd(number_of_loops) { _v[p1] = vt.num_loops; }
cmd(set_priority)   { vt.flags |= VT_FIXED_PRIORITY; vt.priority = p1; }
cmd(set_priority_f) { vt.flags |= VT_FIXED_PRIORITY; vt.priority = _v[p1]; }
cmd(release_priority) { vt.flags &= (UINT16)~VT_FIXED_PRIORITY; }
cmd(get_priority) { _v[p1] = vt.priority; }
cmd(stop_update)  { vt.flags &= (UINT16)~VT_UPDATE; }
cmd(start_update) { vt.flags |= VT_UPDATE; }
cmd(force_update) { }
cmd(ignore_horizon)  { vt.flags |= VT_IGNORE_HORIZON; }
cmd(observe_horizon) { vt.flags &= (UINT16)~VT_IGNORE_HORIZON; }
cmd(set_horizon)     { game.horizon = p0; }
cmd(object_on_water)   { vt.flags |= VT_ON_WATER; }
cmd(object_on_land)    { vt.flags |= VT_ON_LAND; }
cmd(object_on_anything){ vt.flags &= (UINT16)~(VT_ON_WATER | VT_ON_LAND); }
cmd(ignore_objs)    { vt.flags |= VT_IGNORE_OBJECTS; }
cmd(observe_objs)   { vt.flags &= (UINT16)~VT_IGNORE_OBJECTS; }
cmd(distance) {
	struct vt_entry *v0 = &game.view_table[p0];
	struct vt_entry *v1 = &game.view_table[p1];
	int d;

	if ((v0->flags & VT_DRAWN) && (v1->flags & VT_DRAWN)) {
		int x1 = v0->x_pos + v0->x_size / 2;
		int x2 = v1->x_pos + v1->x_size / 2;
		int dx = x1 > x2 ? x1 - x2 : x2 - x1;
		int dy = v0->y_pos > v1->y_pos ? v0->y_pos - v1->y_pos
		                                : v1->y_pos - v0->y_pos;
		d = dx + dy;
		if (d > 0xfe)
			d = 0xfe;
	} else {
		d = 0xff;
	}
	_v[p2] = (UINT8)d;
}
cmd(stop_cycling)  { vt.flags &= (UINT16)~VT_CYCLING; }
cmd(start_cycling) { vt.flags |= VT_CYCLING; }
cmd(normal_cycle)  { vt.cycle = CYCLE_NORMAL; vt.flags |= VT_CYCLING; }
cmd(end_of_loop)   { vt.cycle = CYCLE_END_OF_LOOP; vt.flags |= (VT_DONTUPDATE | VT_UPDATE | VT_CYCLING); vt.parm1 = p1; setflag(p1, FALSE); }
cmd(reverse_cycle) { vt.cycle = CYCLE_REVERSE; vt.flags |= VT_CYCLING; }
cmd(reverse_loop)  { vt.cycle = CYCLE_REV_LOOP; vt.flags |= (VT_DONTUPDATE | VT_UPDATE | VT_CYCLING); vt.parm1 = p1; setflag(p1, FALSE); }
cmd(cycle_time)    { vt.cycle_time = vt.cycle_time_count = _v[p1]; }
cmd(stop_motion) {
	vt.direction = 0;
	vt.motion = MOTION_NORMAL;
	if (p0 == 0) {
		_v[V_ego_dir] = 0;
		game.player_control = FALSE;
	}
}
cmd(start_motion) {
	vt.motion = MOTION_NORMAL;
	if (p0 == 0) {
		_v[V_ego_dir] = 0;
		game.player_control = TRUE;
	}
}
cmd(step_size) { vt.step_size = _v[p1]; }
cmd(step_time) { vt.step_time = vt.step_time_count = _v[p1]; }
cmd(move_obj) {
	vt.motion = MOTION_MOVE_OBJ;
	vt.parm1 = p1;
	vt.parm2 = p2;
	vt.parm3 = vt.step_size;
	vt.parm4 = p4;
	if (p3 != 0)
		vt.step_size = p3;
	setflag(p4, FALSE);
	vt.flags |= VT_UPDATE;
	if (p0 == 0)
		game.player_control = FALSE;
}
cmd(move_obj_f) {
	vt.motion = MOTION_MOVE_OBJ;
	vt.parm1 = _v[p1];
	vt.parm2 = _v[p2];
	vt.parm3 = vt.step_size;
	vt.parm4 = p4;
	if (_v[p3] != 0)
		vt.step_size = _v[p3];
	setflag(p4, FALSE);
	vt.flags |= VT_UPDATE;
	if (p0 == 0)
		game.player_control = FALSE;
}
cmd(follow_ego) {
	vt.motion = MOTION_FOLLOW_EGO;
	vt.parm1 = p1 > vt.step_size ? p1 : vt.step_size;
	vt.parm2 = p2;
	vt.parm3 = 0xff;
	setflag(p2, FALSE);
	vt.flags |= VT_UPDATE;
}
cmd(wander) {
	if (p0 == 0)
		game.player_control = FALSE;
	vt.motion = MOTION_WANDER;
	vt.flags |= VT_UPDATE;
}
cmd(normal_motion) { vt.motion = MOTION_NORMAL; }
cmd(set_dir)       { vt.direction = _v[p1]; }
cmd(get_dir)       { _v[p1] = vt.direction; }
cmd(ignore_blocks)  { vt.flags |= VT_IGNORE_BLOCKS; }
cmd(observe_blocks) { vt.flags &= (UINT16)~VT_IGNORE_BLOCKS; }
cmd(block)   { }
cmd(unblock) { }
cmd(get)     { object_set_location(p0, EGO_OWNED); }
cmd(get_f)   { object_set_location(_v[p0], EGO_OWNED); }
cmd(drop)    { object_set_location(p0, 0); }
cmd(put)     { object_set_location(p0, _v[p1]); }
cmd(put_f)   { object_set_location(_v[p0], _v[p1]); }
cmd(get_room_f) { _v[p1] = (UINT8)object_get_location(_v[p0]); }
cmd(load_sound) { }
cmd(sound)      { sound_start(p0, p1); }
cmd(stop_sound) { sound_stop(); }
cmd(print)      { gfx_message_box(cur_logic->texts[p0 ? p0 - 1 : 0]); }
cmd(print_f)    { gfx_message_box(cur_logic->texts[_v[p0] ? _v[p0] - 1 : 0]); }
cmd(display) {
	gfx_print_text(cur_logic->texts[p2 - 1], p0, p1, 40,
		game.color_fg, game.color_bg);
}
cmd(display_f) {
	gfx_print_text(cur_logic->texts[_v[p2] - 1], _v[p0], _v[p1], 40,
		game.color_fg, game.color_bg);
}
cmd(clear_lines) { gfx_clear_lines(p0, p1 ? p1 : p0, p2); }
cmd(text_screen) { game.gfx_mode = FALSE; gfx_text_mode(); }
cmd(graphics) {
	if (!game.gfx_mode) {
		game.gfx_mode = TRUE;
		gfx_graphics_mode();
		gfx_show_picture();
		gfx_write_status(getvar(V_score), getvar(V_max_score),
			sound_is_enabled());
		gfx_write_prompt();
	}
}
cmd(set_cursor_char) {
	game.cursor_char = (p0 && (p0 - 1) < cur_logic->num_texts) ?
		cur_logic->texts[p0 - 1][0] : '_';
}
cmd(set_text_attribute) {
	game.color_fg = p0;
	game.color_bg = p1;
	gfx_set_text_attribute(p0, p1);
}
cmd(shake_screen) { gfx_shake_screen(p0); }
cmd(configure_screen) {
	game.line_min_print = p0;
	game.line_user_input = p1;
	game.line_status = p2;
}
cmd(status_line_on)  { game.status_line = TRUE; gfx_write_status(getvar(V_score), getvar(V_max_score), sound_is_enabled()); }
cmd(status_line_off) { game.status_line = FALSE; }
cmd(set_string) {
	if (p0 > MAX_STRINGS)
		return;
	strncpy(game.strings[p0], cur_logic->texts[p1 - 1], MAX_STRINGLEN - 1);
	game.strings[p0][MAX_STRINGLEN - 1] = '\0';
}
cmd(get_string) {
	int row = p2 > 24 ? 24 : p2;
	int col = p3 > 39 ? 39 : p3;

	if (p0 > MAX_STRINGS)
		return;

	game.input_mode = INPUT_GETSTRING;
	if (cur_logic->texts != NULL && (p1 - 1) < cur_logic->num_texts)
		gfx_print_text(cur_logic->texts[p1 - 1], row, col, 40,
			game.color_fg, game.color_bg);

	kbd_read_line(game.strings[p0], MAX_STRINGLEN - 1, row, col);
	game.input_mode = INPUT_NORMAL;
}
cmd(word_to_string) {
	if (p0 > MAX_STRINGS)
		return;
	strncpy(game.strings[p0], game.ego_words[p1].word, MAX_STRINGLEN - 1);
	game.strings[p0][MAX_STRINGLEN - 1] = '\0';
}
cmd(parse) {
	setvar(V_word_not_found, 0);
	setflag(F_entered_cli, FALSE);
	setflag(F_said_accepted_input, FALSE);
	vocab_parse_input(game.strings[p0]);
}
cmd(get_num) {
	char buf[8];

	game.input_mode = INPUT_GETSTRING;
	if (cur_logic->texts != NULL && (p0 - 1) < cur_logic->num_texts)
		gfx_print_text(cur_logic->texts[p0 - 1], 22, 0, 40,
			game.color_fg, game.color_bg);

	kbd_read_line(buf, sizeof(buf) - 1, 22, 0);
	game.input_mode = INPUT_NORMAL;
	_v[p1] = (UINT8)atoi(buf);
	gfx_clear_lines(22, 22, game.color_bg);
}
cmd(prevent_input) { game.input_mode = INPUT_NONE; game.input_enabled = FALSE; }
cmd(accept_input)  { game.input_mode = INPUT_NORMAL; game.input_enabled = TRUE; }
cmd(set_key) {
	int key;
	if (p2 >= MAX_DIRS_KEYEVT || game.ev_keyp[p2].data != 0)
		return;
	key = 256 * p1 + p0;
	game.ev_keyp[p2].data = (UINT16)key;
	game.ev_keyp[p2].occurred = FALSE;
}
cmd(add_to_pic)   { }
cmd(add_to_pic_f) { }
cmd(status) {
	static const char *names[MAX_OBJECTS];
	unsigned int i, n = 0;

	for (i = 0; i < object_count(); i++) {
		if (object_get_location(i) == EGO_OWNED)
			names[n++] = object_name(i);
	}

	setvar(V_sel_item,
		(UINT8)gfx_show_inventory(names, (int)n,
			getflag(F_status_selects_items)));
}
cmd(save_game) { gfx_message_box("Save game is not implemented yet."); }
cmd(load_game) { gfx_message_box("Load game is not implemented yet."); }
cmd(init_disk) { }
cmd(restart_game) {
	static const char *buttons[] = { "Restart", "Continue", NULL };

	sound_stop();
	if (getflag(F_auto_restart) ||
	    gfx_selection_box(" Restart game, or continue? ", buttons) == 0) {
		game.quit_prog_now = 0xff;
		setflag(F_restart_game, TRUE);
	}
}
cmd(show_obj) { }
cmd(random)   { _v[p2] = (UINT8)(p0 + (rand() % (1 + (p1 - p0)))); }
cmd(program_control) { game.player_control = FALSE; }
cmd(player_control) {
	game.player_control = TRUE;
	game.view_table[0].motion = MOTION_NORMAL;
}
cmd(obj_status_f) { }
cmd(quit) {
	static const char *buttons[] = { "Quit", "Continue", NULL };

	sound_stop();
	if (p0 || gfx_selection_box(" Quit the game, or continue? ", buttons) == 0)
		game.quit_prog_now = TRUE;
}
cmd(show_mem) { gfx_message_box("Enough memory"); }
cmd(pause) {
	static const char *buttons[] = { "Continue", NULL };
	int tmp = game.clock_enabled;

	game.clock_enabled = FALSE;
	gfx_selection_box("  Game is paused.  ", buttons);
	game.clock_enabled = tmp;
}
cmd(echo_line)       { }
cmd(cancel_line)     { }
cmd(init_joy)        { }
cmd(toggle_monitor)  { }
cmd(version)         { gfx_message_box("GS/AGI engine for Apple IIGS"); }
cmd(script_size)     { }
cmd(set_game_id)     { }
cmd(log)             { }
cmd(set_scan_start)   { cur_logic->sIP = cur_logic->cIP; }
cmd(reset_scan_start) { cur_logic->sIP = 2; }
cmd(reposition_to) {
	vt.x_pos = (SINT16)p1;
	vt.y_pos = (SINT16)p2;
	vt.flags |= VT_UPDATE_POS;
	clamp_position(&vt);
}
cmd(reposition_to_f) {
	vt.x_pos = (SINT16)_v[p1];
	vt.y_pos = (SINT16)_v[p2];
	vt.flags |= VT_UPDATE_POS;
	clamp_position(&vt);
}
cmd(trace_on)   { }
cmd(trace_info) { }
cmd(print_at) {
	int n = p0 < 1 ? 1 : p0;
	gfx_print_text(cur_logic->texts[n - 1], p2, p1, 40,
		game.color_fg, game.color_bg);
}
cmd(print_at_v) {
	int n = _v[p0] < 1 ? 1 : _v[p0];
	gfx_print_text(cur_logic->texts[n - 1], p2, p1, 40,
		game.color_fg, game.color_bg);
}
cmd(clear_text_rect) {
	int x1 = p1 * 4, y1 = p0 * 8;
	int x2 = (p3 + 1) * 4 - 1, y2 = (p2 + 1) * 8 - 1;
	gfx_clear_text_rect(x1, y1, x2, y2, p4 ? 15 : 0);
}
cmd(set_upper_left) { }
cmd(set_menu)      { }
cmd(set_menu_item) { }
cmd(submit_menu)   { }
cmd(enable_item)   { }
cmd(disable_item)  { }
cmd(menu_input)    { }
cmd(show_obj_v)    { }
cmd(open_dialogue)  { game.has_window = TRUE; }
cmd(close_dialogue) { game.has_window = FALSE; }
cmd(mul_n) { _v[p0] = (UINT8)(_v[p0] * p1); }
cmd(mul_v) { _v[p0] = (UINT8)(_v[p0] * _v[p1]); }
cmd(div_n) { if (p1) _v[p0] = (UINT8)(_v[p0] / p1); }
cmd(div_v) { if (_v[p1]) _v[p0] = (UINT8)(_v[p0] / _v[p1]); }
cmd(close_window) { game.has_window = FALSE; }
cmd(set_simple)   { game.simple_save = TRUE; }
cmd(push_script)  { }
cmd(pop_script)   { }
cmd(hold_key)     { }
cmd(set_pri_base) { }
cmd(discard_sound){ }
cmd(hide_mouse)   { }
cmd(allow_menu)   { }
cmd(show_mouse)   { }
cmd(fence_mouse)  { }
cmd(mouse_posn)   { }
cmd(release_key)  { }
cmd(adj_ego_move_to_x_y) { game.view_table[0].flags |= VT_ADJ_EGO_XY; }

static void (*const agi_command[183])(UINT8 *) = {
	NULL,                       /* 0x00 return (handled in run loop) */
	cmd_increment,   cmd_decrement,  cmd_assignn,    cmd_assignv,
	cmd_addn,        cmd_addv,       cmd_subn,       cmd_subv,
	cmd_lindirectv,  cmd_rindirect,  cmd_lindirectn, cmd_set,
	cmd_reset,       cmd_toggle,     cmd_set_v,      cmd_reset_v,
	cmd_toggle_v,    cmd_new_room,   cmd_new_room_f, cmd_load_logic,
	cmd_load_logic_f,cmd_call,       cmd_call_f,     cmd_load_pic,
	cmd_draw_pic,    cmd_show_pic,   cmd_discard_pic,cmd_overlay_pic,
	cmd_show_pri_screen, cmd_load_view, cmd_load_view_f, cmd_discard_view,
	cmd_animate_obj, cmd_unanimate_all, cmd_draw,    cmd_erase,
	cmd_position,    cmd_position_f, cmd_get_posn,   cmd_reposition,
	cmd_set_view,    cmd_set_view_f, cmd_set_loop,   cmd_set_loop_f,
	cmd_fix_loop,    cmd_release_loop, cmd_set_cel,  cmd_set_cel_f,
	cmd_last_cel,    cmd_current_cel, cmd_current_loop, cmd_current_view,
	cmd_number_of_loops, cmd_set_priority, cmd_set_priority_f, cmd_release_priority,
	cmd_get_priority,cmd_stop_update, cmd_start_update, cmd_force_update,
	cmd_ignore_horizon, cmd_observe_horizon, cmd_set_horizon, cmd_object_on_water,
	cmd_object_on_land, cmd_object_on_anything, cmd_ignore_objs, cmd_observe_objs,
	cmd_distance,    cmd_stop_cycling, cmd_start_cycling, cmd_normal_cycle,
	cmd_end_of_loop, cmd_reverse_cycle, cmd_reverse_loop, cmd_cycle_time,
	cmd_stop_motion, cmd_start_motion, cmd_step_size, cmd_step_time,
	cmd_move_obj,    cmd_move_obj_f, cmd_follow_ego, cmd_wander,
	cmd_normal_motion, cmd_set_dir,  cmd_get_dir,    cmd_ignore_blocks,
	cmd_observe_blocks, cmd_block,  cmd_unblock,     cmd_get,
	cmd_get_f,       cmd_drop,       cmd_put,        cmd_put_f,
	cmd_get_room_f,  cmd_load_sound, cmd_sound,      cmd_stop_sound,
	cmd_print,       cmd_print_f,    cmd_display,    cmd_display_f,
	cmd_clear_lines, cmd_text_screen, cmd_graphics,  cmd_set_cursor_char,
	cmd_set_text_attribute, cmd_shake_screen, cmd_configure_screen, cmd_status_line_on,
	cmd_status_line_off, cmd_set_string, cmd_get_string, cmd_word_to_string,
	cmd_parse,       cmd_get_num,    cmd_prevent_input, cmd_accept_input,
	cmd_set_key,     cmd_add_to_pic, cmd_add_to_pic_f, cmd_status,
	cmd_save_game,   cmd_load_game,  cmd_init_disk,  cmd_restart_game,
	cmd_show_obj,    cmd_random,     cmd_program_control, cmd_player_control,
	cmd_obj_status_f,cmd_quit,       cmd_show_mem,   cmd_pause,
	cmd_echo_line,   cmd_cancel_line,cmd_init_joy,   cmd_toggle_monitor,
	cmd_version,     cmd_script_size,cmd_set_game_id,cmd_log,
	cmd_set_scan_start, cmd_reset_scan_start, cmd_reposition_to, cmd_reposition_to_f,
	cmd_trace_on,    cmd_trace_info, cmd_print_at,   cmd_print_at_v,
	cmd_discard_view,cmd_clear_text_rect, cmd_set_upper_left, cmd_set_menu,
	cmd_set_menu_item, cmd_submit_menu, cmd_enable_item, cmd_disable_item,
	cmd_menu_input,  cmd_show_obj_v, cmd_open_dialogue, cmd_close_dialogue,
	cmd_mul_n,       cmd_mul_v,      cmd_div_n,      cmd_div_v,
	cmd_close_window,cmd_set_simple, cmd_push_script,cmd_pop_script,
	cmd_hold_key,    cmd_set_pri_base, cmd_discard_sound, cmd_hide_mouse,
	cmd_allow_menu,  cmd_show_mouse, cmd_fence_mouse,cmd_mouse_posn,
	cmd_release_key, cmd_adj_ego_move_to_x_y
};

/* ------------------------------------------------------------------ */
/* Test opcodes, used only inside "if" blocks                         */
/* ------------------------------------------------------------------ */

static int lognum_test;
#define test_ip   (game.logics[lognum_test].cIP)
#define test_code (game.logics[lognum_test].data)

static int timer_hack;

#define test_equal(v1, v2)    (getvar(v1) == (v2))
#define test_less(v1, v2)     (getvar(v1) < (v2))
#define test_greater(v1, v2)  (getvar(v1) > (v2))
#define test_isset(flag)      (getflag(flag))
#define test_has(obj)         (object_get_location(obj) == EGO_OWNED)
#define test_obj_in_room(obj, v) (object_get_location(obj) == getvar(v))

static int test_posn(UINT8 n, UINT8 x1, UINT8 y1, UINT8 x2, UINT8 y2)
{
	struct vt_entry *v = &game.view_table[n];
	return v->x_pos >= x1 && v->y_pos >= y1 &&
	       v->x_pos <= x2 && v->y_pos <= y2;
}

static int test_obj_in_box(UINT8 n, UINT8 x1, UINT8 y1, UINT8 x2, UINT8 y2)
{
	struct vt_entry *v = &game.view_table[n];
	return v->x_pos >= x1 && v->y_pos >= y1 &&
	       (v->x_pos + v->x_size - 1) <= x2 && v->y_pos <= y2;
}

static int test_obj_centre(UINT8 n, UINT8 x1, UINT8 y1, UINT8 x2, UINT8 y2)
{
	struct vt_entry *v = &game.view_table[n];
	int cx = v->x_pos + v->x_size / 2;
	return cx >= x1 && cx <= x2 && v->y_pos >= y1 && v->y_pos <= y2;
}

static int test_obj_right(UINT8 n, UINT8 x1, UINT8 y1, UINT8 x2, UINT8 y2)
{
	struct vt_entry *v = &game.view_table[n];
	int rx = v->x_pos + v->x_size - 1;
	return rx >= x1 && rx <= x2 && v->y_pos >= y1 && v->y_pos <= y2;
}

static int test_controller(UINT8 cont)
{
	if (cont >= MAX_DIRS_KEYEVT)
		return FALSE;
	return game.ev_keyp[cont].occurred;
}

static int test_keypressed(void)
{
	int x = game.keypress;

	game.keypress = 0;
	if (!x)
		sys_idle_pump();

	return x != 0;
}

static int test_compare_strings(UINT8 s1, UINT8 s2)
{
	char ms1[MAX_STRINGLEN], ms2[MAX_STRINGLEN];
	int j, k, l;

	strcpy(ms1, game.strings[s1]);
	strcpy(ms2, game.strings[s2]);

	l = (int)strlen(ms1);
	for (k = 0, j = 0; k < l; k++) {
		switch (ms1[k]) {
		case ' ': case '\t': case '-': case '.':
		case ',': case ':':  case ';': case '!': case '\'':
			break;
		default:
			ms1[j++] = (char)toupper((unsigned char)ms1[k]);
			break;
		}
	}
	ms1[j] = '\0';

	l = (int)strlen(ms2);
	for (k = 0, j = 0; k < l; k++) {
		switch (ms2[k]) {
		case ' ': case '\t': case '-': case '.':
		case ',': case ':':  case ';': case '!': case '\'':
			break;
		default:
			ms2[j++] = (char)toupper((unsigned char)ms2[k]);
			break;
		}
	}
	ms2[j] = '\0';

	return !strcmp(ms1, ms2);
}

static int test_said(UINT8 nwords, UINT8 *cc)
{
	int c, n = game.num_ego_words;
	int z = 0;

	if (getflag(F_said_accepted_input) || !getflag(F_entered_cli))
		return FALSE;

	for (c = 0; nwords && n; c++, nwords--, n--) {
		z = lohi_getword(cc);
		cc += 2;

		switch (z) {
		case 9999:
			nwords = 1;
			break;
		case 1:
			break;
		default:
			if (game.ego_words[c].id != z)
				return FALSE;
			break;
		}
	}

	if (nwords != 0 && z != 9999)
		return FALSE;

	setflag(F_said_accepted_input, TRUE);
	return TRUE;
}

static int test_if_code(int lognum)
{
	int ec = TRUE, retval = TRUE;
	UINT8 op, not_test = FALSE, or_test = FALSE;
	int last_ip;
	UINT8 p[16];

	lognum_test = lognum;

	while (retval && !game.quit_prog_now) {
		last_ip = test_ip;
		op = test_code[test_ip++];
		memcpy(p, test_code + test_ip, sizeof(p));

		switch (op) {
		case 0xFF:
			goto end_test;
		case 0xFD:
			not_test = !not_test;
			continue;
		case 0xFC:
			if (or_test) {
				ec = FALSE;
				retval = FALSE;
				goto end_test;
			}
			or_test = TRUE;
			continue;
		case 0x00:
			goto end_test;
		case 0x01: ec = test_equal(p0, p1); if (p0 == 11) timer_hack++; break;
		case 0x02: ec = test_equal(p0, getvar(p1)); if (p0 == 11 || p1 == 11) timer_hack++; break;
		case 0x03: ec = test_less(p0, p1); if (p0 == 11) timer_hack++; break;
		case 0x04: ec = test_less(p0, getvar(p1)); if (p0 == 11 || p1 == 11) timer_hack++; break;
		case 0x05: ec = test_greater(p0, p1); if (p0 == 11) timer_hack++; break;
		case 0x06: ec = test_greater(p0, getvar(p1)); if (p0 == 11 || p1 == 11) timer_hack++; break;
		case 0x07: ec = test_isset(p0); break;
		case 0x08: ec = test_isset(getvar(p0)); break;
		case 0x09: ec = test_has(p0); break;
		case 0x0A: ec = test_obj_in_room(p0, p1); break;
		case 0x0B: ec = test_posn(p0, p1, p2, p3, p4); break;
		case 0x0C: ec = test_controller(p0); break;
		case 0x0D: ec = test_keypressed(); break;
		case 0x0E:
			ec = test_said(p0, test_code + test_ip + 1);
			test_ip = last_ip + 1 + 1 + p0 * 2;
			break;
		case 0x0F: ec = test_compare_strings(p0, p1); break;
		case 0x10: ec = test_obj_in_box(p0, p1, p2, p3, p4); break;
		case 0x11: ec = test_obj_centre(p0, p1, p2, p3, p4); break;
		case 0x12: ec = test_obj_right(p0, p1, p2, p3, p4); break;
		default:
			ec = FALSE;
			goto end_test;
		}

		if (op <= 0x12)
			test_ip += test_num_args[op];

		if (not_test)
			ec = !ec;
		not_test = FALSE;

		if (or_test && ec) {
			while (test_code[test_ip] != 0xFC) {
				if (test_code[test_ip] == 0x0E) {
					test_ip++;
					test_ip += 1 + test_code[test_ip] * 2;
					continue;
				}
				if (test_code[test_ip] < 0xFC)
					test_ip += test_num_args[test_code[test_ip]];
				test_ip++;
			}
			test_ip++;
			or_test = FALSE;
			retval = TRUE;
		} else {
			retval = or_test ? (retval || ec) : (retval && ec);
		}
	}

end_test:
	if (retval) {
		test_ip += 2;
	} else {
		test_ip = last_ip;
		while (test_code[test_ip] != 0xFF) {
			if (test_code[test_ip] == 0x0E) {
				test_ip++;
				test_ip += test_code[test_ip] * 2 + 1;
			} else if (test_code[test_ip] < 0xFC) {
				test_ip += test_num_args[test_code[test_ip]];
				test_ip++;
			} else {
				test_ip++;
			}
		}
		test_ip++;
		test_ip += lohi_getword(test_code + test_ip) + 2;
	}

	return retval;
}

/* ------------------------------------------------------------------ */
/* The VM loop                                                        */
/* ------------------------------------------------------------------ */

#define CMD_BSIZE 12

int logic_run(int n)
{
	UINT8 op;
	UINT8 p[CMD_BSIZE];
	UINT8 *code;
	int num;

	if (n < 0 || n >= MAX_DIRS || !game.logics[n].loaded)
		return 1;

	game.lognum = n;
	cur_logic = &game.logics[n];
	code = cur_logic->data;
	cur_logic->cIP = cur_logic->sIP;

	timer_hack = 0;

	while (ip < game.logics[n].size && !game.quit_prog_now) {
		op = code[ip++];

		switch (op) {
		case 0xFF:
			test_if_code(n);
			break;
		case 0xFE:
			ip += 2 + (SINT16)lohi_getword(code + ip);
			if (timer_hack > 20) {
				sys_idle_pump();
				timer_hack = 0;
			}
			break;
		case 0x00:
			return 1;
		default:
			if (op >= NUM_CMD_OPCODES) {
				ip = cur_logic->size;
				break;
			}
			num = cmd_num_args[op];
			memset(p, 0, CMD_BSIZE);
			memcpy(p, code + ip, (size_t)num);
			agi_command[op](p);
			ip += num;
		}

		if (game.exit_all_logics)
			break;
	}

	return 0;
}

static void reset_controllers(void)
{
	int i;
	for (i = 0; i < MAX_DIRS_KEYEVT; i++)
		game.ev_keyp[i].occurred = FALSE;
}

void logic_interpret_cycle(void)
{
	game.exit_all_logics = FALSE;

	while (logic_run(0) == 0 && !game.quit_prog_now) {
		setvar(V_word_not_found, 0);
		setvar(V_border_touch_obj, 0);
		setvar(V_border_code, 0);
		setflag(F_entered_cli, FALSE);
		game.exit_all_logics = FALSE;
		reset_controllers();
	}

	reset_controllers();
	setflag(F_new_room_exec, FALSE);
	setflag(F_restart_game, FALSE);
	setflag(F_restore_just_ran, FALSE);
}
