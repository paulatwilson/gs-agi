/*
 * motion.c - see motion.h.
 *
 * Positions are 16-bit pixels; every product that can pass 32767 is
 * done in long. Walkers are blocked by the picture's control layer, by
 * the top of the walkable ground (the horizon) and by other drawn
 * objects' feet.
 */
#include <string.h>

#include "agitypes.h"
#include "util.h"
#include "gamestate.h"
#include "gfx.h"
#include "view.h"
#include "motion.h"

#define FOOT_ROWS 6	/* height of an object's footprint, for collisions */

/* AGI directions: 1 up, 2 up-right, 3 right, 4 down-right, 5 down, ... */
static const signed char dir_dx[9] = { 0,  0, 1, 1, 1, 0, -1, -1, -1 };
static const signed char dir_dy[9] = { 0, -1, -1, 0, 1, 1, 1, 0, -1 };

/* loop to show when walking in each direction, for views with 4 loops */
static const signed char loop_for_dir[9] = { 0, 3, 0, 0, 0, 2, 1, 1, 1 };

static void touch_border(int idx, int code)
{
	if (idx == 0) {
		setvar(V_border_touch_ego, code);
	} else {
		setvar(V_border_code, code);
		setvar(V_border_touch_obj, idx);
	}
}

static int hits_object(int idx, int x, int y, int half)
{
	const struct vt_entry *o;
	int i;

	for (i = 0; i < MAX_VIEWTABLE; i++) {
		int ox0, ox1, oy0, oy1;

		if (i == idx)
			continue;
		o = &game.view_table[i];
		if ((o->flags & (VT_ANIMATED | VT_DRAWN)) != (VT_ANIMATED | VT_DRAWN))
			continue;
		if (o->flags & VT_IGNORE_OBJECTS)
			continue;
		ox0 = o->x_pos - o->x_size / 2;
		ox1 = o->x_pos + o->x_size / 2;
		oy0 = o->y_pos - FOOT_ROWS;
		oy1 = o->y_pos;
		if (x + half >= ox0 && x - half <= ox1 && y >= oy0 && y <= oy1)
			return TRUE;
	}
	return FALSE;
}

/*
 * Tries to put object idx at (nx, ny). Returns TRUE if it moved.
 * Walking into an edge reports a border touch and stays put; walking
 * onto a trigger or water control line moves and raises that flag.
 */
static int try_place(struct vt_entry *v, int idx, int nx, int ny)
{
	int pct = view_scale_at(v->current_view, ny);
	int e = (int)(((long)5 * pct + 50) / 100);
	int c, k;

	if (e < 2)
		e = 2;

	if (nx < MOTION_EDGE_LEFT)  { touch_border(idx, 4); return FALSE; }
	if (nx > MOTION_EDGE_RIGHT) { touch_border(idx, 2); return FALSE; }
	if (ny > MOTION_BOTTOM)     { touch_border(idx, 3); return FALSE; }
	if (ny < game.horizon && !(v->flags & VT_IGNORE_HORIZON)) {
		touch_border(idx, 1);
		return FALSE;
	}

	if (!(v->flags & VT_IGNORE_BLOCKS)) {
		int trigger = FALSE, water = FALSE;

		for (k = -1; k <= 1; k++) {
			c = gfx_ctl_at(nx + k * e, ny);
			if (c == 1)
				return FALSE;
			if (c == 2)
				trigger = TRUE;
			else if (c == 3)
				water = TRUE;
		}
		if (idx == 0) {
			if (trigger)
				setflag(F_ego_touched_p2, TRUE);
			if (water)
				setflag(F_ego_water, TRUE);
		}
	}

	if (!(v->flags & VT_IGNORE_OBJECTS) && hits_object(idx, nx, ny, e))
		return FALSE;

	v->x_pos = (SINT16)nx;
	v->y_pos = (SINT16)ny;
	return TRUE;
}

/* How many whole pixels a step of step_size covers at this depth. */
static int step_pixels(struct vt_entry *v)
{
	int pct = view_scale_at(v->current_view, v->y_pos);
	UINT32 acc = v->step_frac + ((UINT32)v->step_size * pct * 256UL) / 100UL;
	int n = (int)(acc >> 8);

	v->step_frac = (UINT16)(acc & 0xFF);
	return n;
}

static int step_normal(struct vt_entry *v, int idx)
{
	int n, moved = 0;

	if (v->direction < 1 || v->direction > 8)
		return 0;
	for (n = step_pixels(v); n > 0; n--) {
		if (!try_place(v, idx, v->x_pos + dir_dx[v->direction],
				v->y_pos + dir_dy[v->direction]))
			break;
		moved = 1;
	}
	return moved;
}

static int step_move_obj(struct vt_entry *v, int idx)
{
	int tx = v->parm1, ty = v->parm2;
	int dx = tx - v->x_pos, dy = ty - v->y_pos;
	int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
	int n = step_pixels(v), mx, my, moved = 0;

	if (adx == 0 && ady == 0)
		n = 0;

	if (n > 0) {
		if (adx >= ady) {
			mx = n < adx ? n : adx;
			my = ady ? (int)((long)ady * mx / adx) : 0;
		} else {
			my = n < ady ? n : ady;
			mx = adx ? (int)((long)adx * my / ady) : 0;
		}
		moved = try_place(v, idx, v->x_pos + (dx < 0 ? -mx : mx),
			v->y_pos + (dy < 0 ? -my : my));
	}

	if (v->x_pos == tx && v->y_pos == ty) {
		/* arrived: hand control back and tell the script */
		v->step_size = v->parm3;
		v->motion = MOTION_NORMAL;
		v->direction = 0;
		setflag(v->parm4, TRUE);
		if (idx == 0)
			game.player_control = TRUE;
	}
	return moved;
}

static void cycle_cel(struct vt_entry *v)
{
	int last = v->num_cels - 1;

	if (v->num_cels < 2)
		return;
	switch (v->cycle) {
	case CYCLE_NORMAL:
		v->current_cel = (UINT8)(v->current_cel >= last ? 0 : v->current_cel + 1);
		break;
	case CYCLE_END_OF_LOOP:
		if (v->current_cel < last)
			v->current_cel++;
		if (v->current_cel >= last) {
			setflag(v->parm1, TRUE);
			v->flags &= (UINT16)~VT_CYCLING;
			v->cycle = CYCLE_NORMAL;
		}
		break;
	case CYCLE_REV_LOOP:
		if (v->current_cel > 0)
			v->current_cel--;
		if (v->current_cel == 0) {
			setflag(v->parm1, TRUE);
			v->flags &= (UINT16)~VT_CYCLING;
			v->cycle = CYCLE_NORMAL;
		}
		break;
	case CYCLE_REVERSE:
		v->current_cel = (UINT8)(v->current_cel == 0 ? last : v->current_cel - 1);
		break;
	}
}

void motion_update(void)
{
	int i;

	setflag(F_ego_touched_p2, FALSE);
	setflag(F_ego_water, FALSE);

	for (i = 0; i < MAX_VIEWTABLE; i++) {
		struct vt_entry *v = &game.view_table[i];
		int moved = 0;

		if ((v->flags & (VT_ANIMATED | VT_DRAWN)) != (VT_ANIMATED | VT_DRAWN))
			continue;
		if (!(v->flags & VT_UPDATE))
			continue;

		if (i == 0 && game.player_control && v->motion == MOTION_NORMAL)
			v->direction = (UINT8)getvar(V_ego_dir);

		v->x_pos2 = v->x_pos;
		v->y_pos2 = v->y_pos;

		if (v->step_time_count > 1) {
			v->step_time_count--;
		} else {
			v->step_time_count = v->step_time ? v->step_time : 1;
			if (v->motion == MOTION_MOVE_OBJ)
				moved = step_move_obj(v, i);
			else if (v->motion == MOTION_NORMAL)
				moved = step_normal(v, i);
		}

		if (v->direction && !(v->flags & VT_FIX_LOOP) && v->num_loops >= 4)
			v->current_loop = (UINT8)loop_for_dir[v->direction];

		if (v->flags & VT_CYCLING) {
			if (i == 0 && !moved) {
				/* the walker stands on her first cel when still */
				v->current_cel = 0;
				v->cycle_time_count = v->cycle_time ? v->cycle_time : 1;
			} else if (v->cycle_time_count > 1) {
				v->cycle_time_count--;
			} else {
				v->cycle_time_count = v->cycle_time ? v->cycle_time : 1;
				cycle_cel(v);
			}
		}

		view_update_object(v);
		gfx_move_object(i);
	}
}
