/*
 * gfx_iigs.c - QuickDraw II backend for the 320x200 Super Hi-Res
 * screen: background/picture layer, sprite blit, status line and the
 * parser input box.
 *
 * Toolbox call names and argument shapes below follow the Apple IIGS
 * Toolbox Reference (QuickDraw II chapters) as documented, but this
 * was written without the real ORCA/C "GSOS"/"QuickDraw.h" headers in
 * front of me to compile against -- check call signatures against
 * your ORCA/C install (Tool Locator version differences between
 * System 5.x and 6.x toolsets are the usual source of mismatches)
 * before trusting this to link clean on the first try.
 */
#include <string.h>
#include <stdio.h>

#include <types.h>
#include <memory.h>
#include <quickdraw.h>
#include <qdaux.h>
#include <locator.h>
#include <misctool.h>

#include "agitypes.h"
#include "gamestate.h"
#include "gfx_iigs.h"
#include "keyboard.h"

#define INPUT_ROW_TOP  (GFX_INPUT_BOX_TOP)
#define INPUT_ROW_TEXT (GFX_INPUT_BOX_TOP + 4)

static GrafPortPtr game_port;
static Rect        screen_rect;
static int         inited = 0;

/* ---- life cycle ---- */

int gfx_init(void)
{
	screen_rect.h1 = 0;
	screen_rect.v1 = 0;
	screen_rect.h2 = GFX_WIDTH;
	screen_rect.v2 = GFX_HEIGHT;

	InitGraf((Pointer)&qd1);		/* set up QuickDraw II globals	*/
	game_port = NewGrafPort(NULL, &screen_rect);
	if (game_port == NULL)
		return err_NotEnoughMemory;

	SetPort(game_port);
	SetSolidPenPat(0);
	SetGrafProcPtr(NULL);

	game.gfx_mode = TRUE;
	game.color_fg = 0;
	game.color_bg = 15;
	inited = 1;

	gfx_clear_screen(0);
	gfx_draw_input_box();
	return err_OK;
}

void gfx_shutdown(void)
{
	if (!inited)
		return;
	CloseGrafPort(game_port);
	inited = 0;
}

/* ---- screen / mode ---- */

void gfx_clear_screen(int color)
{
	SetSolidPenPat((Word)color);
	PaintRect(&screen_rect);
	SetSolidPenPat((Word)game.color_fg);
}

void gfx_text_mode(void)
{
	/* Leave the SHR port in place and just blank it; this engine
	 * draws its own text rows rather than dropping to the GS/OS
	 * 80-column firmware text mode. */
	gfx_clear_screen(game.color_bg & 0x08 ? game.color_bg : 0);
}

void gfx_graphics_mode(void)
{
	gfx_clear_screen(0);
}

void gfx_flush(void)
{
	/* Single-buffered GrafPort: QuickDraw II calls land on screen
	 * immediately, so there is nothing to swap here. The seam is
	 * kept so a double-buffered page-flip can drop in later
	 * without touching callers. */
}

void gfx_set_text_attribute(int fg, int bg)
{
	game.color_fg = fg;
	game.color_bg = bg;
}

/* ---- background picture layer ---- */

void gfx_load_picture(int picnum)
{
	(void)picnum;
	/* Seam for the AGI PICTURE vector decoder (not part of this
	 * engine yet -- see gfx_iigs.h). */
}

void gfx_draw_picture(int picnum, int clear_first)
{
	char label[32];

	if (clear_first)
		gfx_clear_screen(0);

	sprintf(label, "[picture %d]", picnum);
	MoveTo(8, 8);
	DrawString(c2pstr(label));
}

void gfx_show_picture(void)
{
	gfx_flush();
}

void gfx_show_priority_screen(void)
{
	/* No priority buffer without a picture decoder; just bounce
	 * back to the normal screen. */
	gfx_show_picture();
}

void gfx_shake_screen(int count)
{
	int i;
	Rect r = screen_rect;

	for (i = 0; i < count; i++) {
		r.h1 = (i & 1) ? 4 : -4;
		r.h2 = r.h1 + GFX_WIDTH;
		SetPortRect(game_port, &r);
		gfx_flush();
	}
	r.h1 = 0;
	r.h2 = GFX_WIDTH;
	SetPortRect(game_port, &r);
	gfx_flush();
}

/* ---- animated objects ---- */

void gfx_set_view(int obj, int view)  { (void)obj; (void)view; }
void gfx_set_loop(int obj, int loop)  { (void)obj; (void)loop; }
void gfx_set_cel(int obj, int cel)    { (void)obj; (void)cel;  }

void gfx_draw_object(int obj)
{
	struct vt_entry *v = &game.view_table[obj];
	Rect r;

	r.h1 = v->x_pos;
	r.v1 = (Word)(v->y_pos - (v->y_size > 0 ? v->y_size : 8));
	r.h2 = (Word)(r.h1 + (v->x_size > 0 ? v->x_size : 8));
	r.v2 = v->y_pos;

	SetSolidPenPat((Word)(obj & 0x0F));
	FrameRect(&r);
	SetSolidPenPat((Word)game.color_fg);
}

void gfx_erase_object(int obj)
{
	struct vt_entry *v = &game.view_table[obj];
	Rect r;

	r.h1 = v->x_pos2;
	r.v1 = (Word)(v->y_pos2 - (v->y_size > 0 ? v->y_size : 8));
	r.h2 = (Word)(r.h1 + (v->x_size > 0 ? v->x_size : 8));
	r.v2 = v->y_pos2;

	SetSolidPenPat(0);
	PaintRect(&r);
	SetSolidPenPat((Word)game.color_fg);
}

void gfx_move_object(int obj)
{
	gfx_erase_object(obj);
	gfx_draw_object(obj);
}

/* ---- text ---- */

static void draw_text_row(const char *text, int row, int col, int fg, int bg)
{
	Rect r;

	r.h1 = (Word)(col * 4);
	r.v1 = (Word)(row * 8);
	r.h2 = GFX_WIDTH;
	r.v2 = (Word)(r.v1 + 8);

	SetSolidPenPat((Word)bg);
	PaintRect(&r);

	MoveTo(r.h1, (Word)(r.v1 + 7));
	SetSolidPenPat((Word)fg);
	DrawString(c2pstr((char *)text));
}

void gfx_print_text(const char *text, int row, int col, int width, int fg, int bg)
{
	(void)width;
	draw_text_row(text, row, col, fg, bg);
}

void gfx_clear_lines(int top, int bottom, int color)
{
	Rect r;
	r.h1 = 0;
	r.h2 = GFX_WIDTH;
	r.v1 = (Word)(top * 8);
	r.v2 = (Word)((bottom + 1) * 8);

	SetSolidPenPat((Word)color);
	PaintRect(&r);
	SetSolidPenPat((Word)game.color_fg);
}

void gfx_clear_text_rect(int x1, int y1, int x2, int y2, int color)
{
	Rect r;
	r.h1 = (Word)x1;
	r.v1 = (Word)y1;
	r.h2 = (Word)(x2 + 1);
	r.v2 = (Word)(y2 + 1);

	SetSolidPenPat((Word)color);
	PaintRect(&r);
	SetSolidPenPat((Word)game.color_fg);
}

void gfx_write_status(int score, int max_score, int sound_on)
{
	char buf[48];

	if (!game.status_line)
		return;

	sprintf(buf, "Score:%d of %d  Sound:%s", score, max_score,
		sound_on ? "on" : "off");
	draw_text_row(buf, 0, 1, 0, 15);
}

void gfx_message_box(const char *msg)
{
	Rect r;

	r.h1 = 40; r.v1 = 70;
	r.h2 = 280; r.v2 = 130;

	SetSolidPenPat(15);
	PaintRect(&r);
	SetSolidPenPat(0);
	FrameRect(&r);

	MoveTo((Word)(r.h1 + 8), (Word)(r.v1 + 20));
	DrawString(c2pstr((char *)msg));

	MoveTo((Word)(r.h1 + 8), (Word)(r.v2 - 10));
	DrawString(c2pstr("Press a key to continue"));

	kbd_wait_key();
	gfx_show_picture();
}

int gfx_selection_box(const char *msg, const char *const *buttons)
{
	int n = 0, sel = 0, key;
	Rect r;

	while (buttons[n] != NULL)
		n++;
	if (n == 0)
		return -1;

	r.h1 = 30; r.v1 = 60;
	r.h2 = 290; r.v2 = 140;

	for (;;) {
		int i;

		SetSolidPenPat(15);
		PaintRect(&r);
		SetSolidPenPat(0);
		FrameRect(&r);

		MoveTo((Word)(r.h1 + 8), (Word)(r.v1 + 16));
		DrawString(c2pstr((char *)msg));

		for (i = 0; i < n; i++) {
			char line[40];
			sprintf(line, "%c %s", i == sel ? '>' : ' ', buttons[i]);
			MoveTo((Word)(r.h1 + 8), (Word)(r.v1 + 36 + i * 10));
			DrawString(c2pstr(line));
		}

		key = kbd_wait_key();
		if (key == KEY_UP && sel > 0)
			sel--;
		else if (key == KEY_DOWN && sel < n - 1)
			sel++;
		else if (key == KEY_ENTER)
			break;
		else if (key == KEY_ESCAPE) {
			sel = n - 1;
			break;
		}
	}

	gfx_show_picture();
	return sel;
}

/* ---- parser input box ---- */

void gfx_draw_input_box(void)
{
	Rect r;
	r.h1 = 0;
	r.v1 = INPUT_ROW_TOP;
	r.h2 = GFX_WIDTH;
	r.v2 = GFX_HEIGHT;

	SetSolidPenPat(15);
	PaintRect(&r);
	SetSolidPenPat(0);
	FrameRect(&r);
	SetSolidPenPat((Word)game.color_fg);
}

void gfx_update_input_line(const char *buffer, int cursor_pos)
{
	char line[MAX_INPUTLEN + 2];

	(void)cursor_pos;
	strcpy(line, "> ");
	strncat(line, buffer, MAX_INPUTLEN - 1);

	draw_text_row(line, (INPUT_ROW_TEXT / 8), 0, 0, 15);
}

void gfx_write_prompt(void)
{
	gfx_update_input_line("", 0);
}

/* ---- inventory screen ---- */

int gfx_show_inventory(const char *const *names, int count, int select)
{
	int sel = 0, key, i;

	if (count == 0) {
		gfx_message_box("You are not carrying anything.");
		return select ? 0xff : -1;
	}

	gfx_clear_screen(15);
	draw_text_row("You are carrying:", 0, 1, 0, 15);

	for (i = 0; i < count; i++)
		draw_text_row(names[i], 2 + i, i % 2 ? 20 : 1, 0, 15);

	if (!select) {
		kbd_wait_key();
		gfx_show_picture();
		return -1;
	}

	draw_text_row("Enter to select, Esc to cancel", 24, 1, 0, 15);

	for (;;) {
		SetSolidPenPat(15);
		for (i = 0; i < count; i++)
			draw_text_row(names[i], 2 + i, i % 2 ? 20 : 1,
				i == sel ? 15 : 0, i == sel ? 0 : 15);

		key = kbd_wait_key();
		switch (key) {
		case KEY_UP:    if (sel >= 2) sel -= 2; break;
		case KEY_DOWN:  if (sel + 2 < count) sel += 2; break;
		case KEY_LEFT:  if (sel % 2 == 1) sel--; break;
		case KEY_RIGHT: if (sel % 2 == 0 && sel + 1 < count) sel++; break;
		case KEY_ENTER:
			gfx_show_picture();
			return sel;
		case KEY_ESCAPE:
			gfx_show_picture();
			return 0xff;
		default:
			break;
		}
	}
}
