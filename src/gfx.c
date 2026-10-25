/*
 * gfx.c - the portable renderer behind gfx.h. Everything is drawn into
 * one 320x200 8-bit framebuffer with the game's 16-colour palette, then
 * handed to the platform's video backend (video.h). No platform code
 * lives here, so the IIgs and host builds look identical.
 *
 * ORCA/C ints are 16 bits, and 320 * 200 does not fit in one, so all
 * pixel offsets are computed in UINT32 through row_ptr().
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "agitypes.h"
#include "gamestate.h"
#include "gfx.h"
#include "keyboard.h"
#include "video.h"
#include "pic.h"
#include "view.h"
#include "syshooks.h"
#include "font8x8.h"

#define CHAR_W 8
#define CHAR_H 8

#define INPUT_ROW_TOP  (GFX_INPUT_BOX_TOP)
#define INPUT_ROW_TEXT (GFX_INPUT_BOX_TOP + 4)

static UINT8  *fb = NULL;
static int     shake_x = 0;
static int     frame_dirty = 1;	/* objects changed: recompose before showing */

/* The current picture's three layers (see resfmt.h). */
static UINT8  *pic_vis = NULL;
static UINT8  *pic_pri = NULL;
static UINT8  *pic_ctl = NULL;

#define PLAY_TOP     8		/* first row below the status line   */
#define PLAY_BOTTOM  GFX_INPUT_BOX_TOP	/* first row of the input box */

/* The demo palette, 12-bit 0x0RGB (native IIgs format). */
static const UINT16 palette[16] = {
	0x000, 0x9DF, 0x911, 0x38B, 0x7C4, 0x4A3, 0x261, 0xFD2,
	0x852, 0x531, 0xAAA, 0x777, 0x444, 0xFC9, 0xD33, 0xFFF
};

static UINT8 *row_ptr(int y)
{
	return fb + (UINT32)y * GFX_WIDTH;
}

/* ---- framebuffer primitives ---- */

static void fill_rect(int x1, int y1, int x2, int y2, int color)
{
	int y;

	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 > GFX_WIDTH) x2 = GFX_WIDTH;
	if (y2 > GFX_HEIGHT) y2 = GFX_HEIGHT;
	if (x1 >= x2)
		return;

	for (y = y1; y < y2; y++)
		memset(row_ptr(y) + x1, color & 0x0F, (size_t)(x2 - x1));
}

static void frame_rect(int x1, int y1, int x2, int y2, int color)
{
	fill_rect(x1, y1, x2, y1 + 1, color);
	fill_rect(x1, y2 - 1, x2, y2, color);
	fill_rect(x1, y1, x1 + 1, y2, color);
	fill_rect(x2 - 1, y1, x2, y2, color);
}

static void draw_string(const char *s, int x, int y, int color)
{
	for (; *s; s++, x += CHAR_W) {
		const char *glyph = font8x8_basic[(*s & 0x7F)];
		int gy, gx;

		for (gy = 0; gy < CHAR_H; gy++) {
			if (y + gy < 0 || y + gy >= GFX_HEIGHT)
				continue;
			for (gx = 0; gx < CHAR_W; gx++) {
				int px = x + gx;

				if (!(glyph[gy] & (1 << gx)))
					continue;
				if (px < 0 || px >= GFX_WIDTH)
					continue;
				row_ptr(y + gy)[px] = (UINT8)(color & 0x0F);
			}
		}
	}
}

/* ---- life cycle ---- */

int gfx_init(void)
{
	fb      = (UINT8 *)malloc((size_t)GFX_WIDTH * GFX_HEIGHT);
	pic_vis = (UINT8 *)calloc((size_t)GFX_WIDTH * GFX_HEIGHT, 1);
	pic_pri = (UINT8 *)calloc((size_t)GFX_WIDTH * GFX_HEIGHT, 1);
	pic_ctl = (UINT8 *)calloc((size_t)PIC_CTL_BYTES, 1);
	if (fb == NULL || pic_vis == NULL || pic_pri == NULL || pic_ctl == NULL) {
		free(fb); free(pic_vis); free(pic_pri); free(pic_ctl);
		fb = pic_vis = pic_pri = pic_ctl = NULL;
		return err_NotEnoughMemory;
	}

	if (video_init() != err_OK) {
		free(fb); free(pic_vis); free(pic_pri); free(pic_ctl);
		fb = pic_vis = pic_pri = pic_ctl = NULL;
		return err_Unknown;
	}

	game.gfx_mode = TRUE;
	game.color_fg = 0;
	game.color_bg = 15;

	gfx_clear_screen(0);
	gfx_draw_input_box();
	video_present(fb, palette, 0);
	return err_OK;
}

#ifndef __ORCAC__
/*
 * Host-only test aid: writes the current frame as a binary PPM. If
 * GSAGI_DUMP names a file, the final frame is written there on exit;
 * GSAGI_SCRIPT (keyboard_sdl.c) can also dump mid-run.
 */
void gfx_dump_frame(const char *path)
{
	FILE *fp;
	int x, y;

	if (fb == NULL || path == NULL || (fp = fopen(path, "wb")) == NULL)
		return;
	fprintf(fp, "P6\n%d %d\n255\n", GFX_WIDTH, GFX_HEIGHT);
	for (y = 0; y < GFX_HEIGHT; y++) {
		for (x = 0; x < GFX_WIDTH; x++) {
			UINT16 c = palette[row_ptr(y)[x] & 15];

			fputc(((c >> 8) & 15) * 17, fp);
			fputc(((c >> 4) & 15) * 17, fp);
			fputc((c & 15) * 17, fp);
		}
	}
	fclose(fp);
}
#endif

void gfx_shutdown(void)
{
	if (fb == NULL)
		return;
#ifndef __ORCAC__
	gfx_dump_frame(getenv("GSAGI_DUMP"));
#endif
	video_shutdown();
	free(fb); free(pic_vis); free(pic_pri); free(pic_ctl);
	fb = pic_vis = pic_pri = pic_ctl = NULL;
}

/* ---- screen / mode ---- */

void gfx_clear_screen(int color)
{
	fill_rect(0, 0, GFX_WIDTH, GFX_HEIGHT, color);
}

void gfx_text_mode(void)
{
	gfx_clear_screen(game.color_bg & 0x08 ? game.color_bg : 0);
}

void gfx_graphics_mode(void)
{
	gfx_clear_screen(0);
}

static void compose(void);

/* Brings the screen up to date so a box drawn on it is not wiped later. */
static void settle(void)
{
	if (frame_dirty)
		compose();
}

void gfx_flush(void)
{
	if (fb == NULL)
		return;
	if (frame_dirty)
		compose();
	if (video_frame_due())
		video_present(fb, palette, shake_x);
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
	/* Pictures are small and decoded on demand by gfx_draw_picture(). */
}

/* Reads PIC.n into a fresh buffer; returns NULL if it is missing. */
static UINT8 *read_picture(int picnum, UINT32 *len)
{
	char name[16], path[256];
	FILE *fp;
	long flen;
	UINT8 *mem;

	sprintf(name, "PIC.%d", picnum);
	sys_game_path(path, sizeof(path), name);
	fp = fopen(path, "rb");
	if (fp == NULL)
		return NULL;
	fseek(fp, 0, SEEK_END);
	flen = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	mem = (UINT8 *)malloc((size_t)flen);
	if (mem != NULL && fread(mem, 1, (size_t)flen, fp) != (size_t)flen) {
		free(mem);
		mem = NULL;
	}
	fclose(fp);
	*len = (UINT32)flen;
	return mem;
}

static void picture_to_screen(void)
{
	int y;

	for (y = PLAY_TOP; y < PLAY_BOTTOM; y++)
		memcpy(row_ptr(y), pic_vis + (UINT32)y * GFX_WIDTH, GFX_WIDTH);
}

void gfx_draw_picture(int picnum, int clear_first)
{
	struct pic_layers layers;
	UINT8 *data;
	UINT32 len;

	if (clear_first) {
		memset(pic_vis, 0, (size_t)GFX_WIDTH * GFX_HEIGHT);
		memset(pic_pri, 0, (size_t)GFX_WIDTH * GFX_HEIGHT);
		memset(pic_ctl, 0, (size_t)PIC_CTL_BYTES);
	}

	data = read_picture(picnum, &len);
	if (data == NULL) {
		char label[32];

		sprintf(label, "[picture %d]", picnum);
		draw_string(label, 8, 8, 15);
		return;
	}

	layers.vis = pic_vis;
	layers.pri = pic_pri;
	layers.ctl = pic_ctl;
	pic_draw(data, len, &layers);
	free(data);

	picture_to_screen();
	frame_dirty = 1;
}

int gfx_ctl_at(int x, int y)
{
	return pic_ctl_get(pic_ctl, x, y);
}

int gfx_priority_at(int x, int y)
{
	if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT)
		return 0;
	return pic_pri[(UINT32)y * GFX_WIDTH + (UINT32)x];
}

void gfx_show_picture(void)
{
	frame_dirty = 1;
	gfx_flush();
}

void gfx_show_priority_screen(void)
{
	gfx_show_picture();
}

void gfx_shake_screen(int count)
{
	int i;

	for (i = 0; i < count; i++) {
		shake_x = (i & 1) ? 4 : -4;
		video_present(fb, palette, shake_x);
		video_wait_frame();
	}
	shake_x = 0;
	video_present(fb, palette, 0);
}

/* ---- animated objects ---- */

/*
 * Objects are not drawn on the screen as they change. gfx_draw_object()
 * and friends only mark the frame dirty; compose() then rebuilds the
 * play area from the picture and every drawn object, back to front by
 * baseline row. A sprite pixel is hidden where the picture's priority
 * layer holds a baseline lower on the screen than the object's own, so
 * a walker passes behind a tree whose base is below her feet.
 */
void gfx_set_view(int obj, int view)  { (void)obj; (void)view; frame_dirty = 1; }
void gfx_set_loop(int obj, int loop)  { (void)obj; (void)loop; frame_dirty = 1; }
void gfx_set_cel(int obj, int cel)    { (void)obj; (void)cel;  frame_dirty = 1; }

void gfx_draw_object(int obj)  { (void)obj; frame_dirty = 1; }
void gfx_erase_object(int obj) { (void)obj; frame_dirty = 1; }
void gfx_move_object(int obj)  { (void)obj; frame_dirty = 1; }

static void blit_object(const struct vt_entry *v)
{
	struct view_cel c;
	int pct, dw, dh, left, top, dx, dy;

	if (!view_cel(v->current_view, v->current_loop, v->current_cel, &c))
		return;

	pct = view_scale_at(v->current_view, v->y_pos);
	dw = (int)(((long)c.w * pct + 50) / 100);
	dh = (int)(((long)c.h * pct + 50) / 100);
	if (dw < 1) dw = 1;
	if (dh < 1) dh = 1;
	left = v->x_pos - (int)(((long)c.ax * pct + 50) / 100);
	top  = v->y_pos - (int)(((long)c.ay * pct + 50) / 100);

	for (dy = 0; dy < dh; dy++) {
		int py = top + dy;
		int sy = (int)((((long)dy * 2 + 1) * c.h) / ((long)dh * 2));
		const UINT8 *src = c.pixels + (UINT32)sy * c.w;

		if (py < PLAY_TOP || py >= PLAY_BOTTOM)
			continue;
		for (dx = 0; dx < dw; dx++) {
			int px = left + dx;
			int sx = (int)((((long)dx * 2 + 1) * c.w) / ((long)dw * 2));
			UINT8 pix = src[sx];

			if (pix == VIEW_TRANSPARENT || px < 0 || px >= GFX_WIDTH)
				continue;
			if (pic_pri[(UINT32)py * GFX_WIDTH + (UINT32)px] > v->y_pos)
				continue;
			row_ptr(py)[px] = (UINT8)(pix & 0x0F);
		}
	}
}

static void compose(void)
{
	static const struct vt_entry *order[MAX_VIEWTABLE];
	const struct vt_entry *v;
	int n = 0, i, j;

	picture_to_screen();

	for_each_vt_entry(v) {
		if (!(v->flags & VT_DRAWN))
			continue;
		/* insertion sort by baseline, keeping table order on ties */
		for (i = n; i > 0 && order[i - 1]->y_pos > v->y_pos; i--)
			order[i] = order[i - 1];
		order[i] = v;
		n++;
	}
	for (j = 0; j < n; j++)
		blit_object(order[j]);

	frame_dirty = 0;
}

/* ---- text ---- */

static void draw_text_row(const char *text, int row, int col, int fg, int bg)
{
	int x = col * CHAR_W;
	int y = row * CHAR_H;

	fill_rect(x, y, GFX_WIDTH, y + CHAR_H, bg);
	draw_string(text, x, y, fg);
}

void gfx_print_text(const char *text, int row, int col, int width, int fg, int bg)
{
	(void)width;
	draw_text_row(text, row, col, fg, bg);
}

void gfx_clear_lines(int top, int bottom, int color)
{
	fill_rect(0, top * CHAR_H, GFX_WIDTH, (bottom + 1) * CHAR_H, color);
}

void gfx_clear_text_rect(int x1, int y1, int x2, int y2, int color)
{
	fill_rect(x1, y1, x2 + 1, y2 + 1, color);
}

void gfx_write_status(int score, int max_score, int sound_on)
{
	char buf[48];

	if (!game.status_line)
		return;

	sprintf(buf, "Score:%d of %d  Sound:%s", score, max_score,
		sound_on ? "on" : "off");
	fill_rect(0, 0, GFX_WIDTH, CHAR_H, 15);
	draw_text_row(buf, 0, 1, 0, 15);
}

static int msg_icon_view = -1;

void gfx_set_message_icon(int view)
{
	msg_icon_view = view;
}

#define MSG_LEFT   36
#define MSG_RIGHT  284
#define MSG_LINES  16

/* Greedy word wrap into lines of at most width characters. */
static int wrap_text(const char *msg, int width, char lines[][40])
{
	int n = 0, len = 0;
	const char *p = msg;
	char word[40];

	lines[0][0] = '\0';
	while (*p) {
		int wl = 0;

		if (*p == '\n') {
			if (++n >= MSG_LINES) return MSG_LINES;
			lines[n][0] = '\0';
			len = 0;
			p++;
			continue;
		}
		if (*p == ' ') {
			p++;
			continue;
		}
		while (*p && *p != ' ' && *p != '\n' && wl < 39)
			word[wl++] = *p++;
		word[wl] = '\0';

		if (len > 0 && len + 1 + wl > width) {
			if (++n >= MSG_LINES) return MSG_LINES;
			lines[n][0] = '\0';
			len = 0;
		}
		if (len > 0) {
			lines[n][len++] = ' ';
			lines[n][len] = '\0';
		}
		strcpy(lines[n] + len, word);
		len += wl;
	}
	return n + 1;
}

static void draw_icon(int view, int cx, int cy)
{
	struct view_cel c;
	int x, y;

	if (!view_cel(view, 0, 0, &c))
		return;
	for (y = 0; y < c.h; y++) {
		for (x = 0; x < c.w; x++) {
			UINT8 pix = c.pixels[(UINT32)y * c.w + x];
			int px = cx - c.w / 2 + x, py = cy - c.h / 2 + y;

			if (pix != VIEW_TRANSPARENT && px >= 0 && px < GFX_WIDTH &&
			    py >= 0 && py < GFX_HEIGHT)
				row_ptr(py)[px] = (UINT8)(pix & 0x0F);
		}
	}
}

void gfx_message_box(const char *msg)
{
	static char lines[MSG_LINES][40];
	int icon = msg_icon_view;
	int n, h, top, k;

	settle();
	msg_icon_view = -1;
	n = wrap_text(msg, icon >= 0 ? 22 : 29, lines);
	h = 14 + n * 10 + 16;
	if (icon >= 0 && h < 48)
		h = 48;
	top = PLAY_TOP + (PLAY_BOTTOM - PLAY_TOP - h) / 2;

	fill_rect(MSG_LEFT, top, MSG_RIGHT, top + h, 15);
	frame_rect(MSG_LEFT, top, MSG_RIGHT, top + h, 0);
	for (k = 0; k < n; k++)
		draw_string(lines[k], MSG_LEFT + 8, top + 8 + k * 10, 0);
	if (icon >= 0)
		draw_icon(icon, 256, top + 26);
	draw_string("[any key]", MSG_LEFT + 8, top + h - 12, 0);

	video_present(fb, palette, 0);
	kbd_wait_key();
	gfx_show_picture();
}

int gfx_selection_box(const char *msg, const char *const *buttons)
{
	int n = 0, sel = 0, key;

	while (buttons[n] != NULL)
		n++;
	if (n == 0)
		return -1;

	settle();
	for (;;) {
		int i;

		fill_rect(30, 60, 290, 140, 15);
		frame_rect(30, 60, 290, 140, 0);
		draw_string(msg, 38, 70, 0);

		for (i = 0; i < n; i++) {
			char line[40];

			sprintf(line, "%c %s", i == sel ? '>' : ' ', buttons[i]);
			draw_string(line, 38, 90 + i * 10, 0);
		}

		video_present(fb, palette, 0);
		key = kbd_wait_key();
		if (game.quit_prog_now)
			return n - 1;
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
	fill_rect(0, INPUT_ROW_TOP, GFX_WIDTH, GFX_HEIGHT, 15);
	frame_rect(0, INPUT_ROW_TOP, GFX_WIDTH, GFX_HEIGHT, 0);
}

void gfx_update_input_line(const char *buffer, int cursor_pos)
{
	char line[MAX_INPUTLEN + 4];

	(void)cursor_pos;
	strcpy(line, "> ");
	strncat(line, buffer, MAX_INPUTLEN - 1);
	strcat(line, "_");

	draw_text_row(line, (INPUT_ROW_TEXT / CHAR_H), 0, 0, 15);
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
		gfx_message_box("You are carrying nothing.");
		return select ? 0xff : -1;
	}

	if (!select) {
		char text[300] = "You are carrying:";

		for (i = 0; i < count && strlen(text) + strlen(names[i]) + 2 < sizeof(text); i++) {
			strcat(text, "\n");
			strcat(text, names[i]);
		}
		gfx_message_box(text);
		return -1;
	}

	settle();
	gfx_clear_screen(15);
	draw_text_row("You are carrying:", 0, 1, 0, 15);

	for (i = 0; i < count; i++)
		draw_text_row(names[i], 2 + i, i % 2 ? 20 : 1, 0, 15);

	if (!select) {
		video_present(fb, palette, 0);
		kbd_wait_key();
		gfx_show_picture();
		return -1;
	}

	draw_text_row("Enter to select, Esc to cancel", 24, 1, 0, 15);

	for (;;) {
		for (i = 0; i < count; i++)
			draw_text_row(names[i], 2 + i, i % 2 ? 20 : 1,
				i == sel ? 15 : 0, i == sel ? 0 : 15);

		video_present(fb, palette, 0);
		key = kbd_wait_key();
		if (game.quit_prog_now)
			return 0xff;
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
