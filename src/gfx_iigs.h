/*
 * gfx_iigs.h - Apple IIGS QuickDraw II rendering backend.
 *
 * Owns the 320x200 Super Hi-Res screen: the picture/background layer,
 * the animated-object ("view table") sprites, the status line and
 * the parser input box at the bottom of the screen. logic.c never
 * touches QuickDraw II directly -- it only calls these entry points
 * with resource/object numbers, which is what makes logic.c portable.
 *
 * NOTE ON SCOPE: this module manages the screen surface, palette,
 * text box and sprite *positions* (draw/erase/move, backed by
 * off-screen pixel blit). It does not include an AGI PICTURE/VIEW
 * byte-code decoder (Sierra's vector-picture fill/line language and
 * RLE cel format) -- that is a separate, large resource-format
 * project in its own right and wasn't one of the three modules asked
 * for. gfx_load_picture()/gfx_set_cel() are the seams where that
 * decoder plugs in later: today they clear to a flat color / draw a
 * placeholder box so the engine runs end to end.
 */
#ifndef INTERGELLIUM_GFX_IIGS_H
#define INTERGELLIUM_GFX_IIGS_H

#include "agitypes.h"

#define GFX_WIDTH   320
#define GFX_HEIGHT  200
#define GFX_INPUT_BOX_TOP (GFX_HEIGHT - 16)  /* 2 text rows, 8px each */

int  gfx_init(void);
void gfx_shutdown(void);

/* ---- screen / mode ---- */
void gfx_clear_screen(int color);
void gfx_text_mode(void);
void gfx_graphics_mode(void);
void gfx_flush(void);			/* present back buffer	*/
void gfx_set_text_attribute(int fg, int bg);

/* ---- background picture layer (seam for a future PICTURE decoder) ---- */
void gfx_load_picture(int picnum);
void gfx_draw_picture(int picnum, int clear_first);
void gfx_show_picture(void);
void gfx_show_priority_screen(void);
void gfx_shake_screen(int count);

/* ---- animated objects / sprites ---- */
void gfx_set_view(int obj, int view);
void gfx_set_loop(int obj, int loop);
void gfx_set_cel(int obj, int cel);
void gfx_draw_object(int obj);
void gfx_erase_object(int obj);
void gfx_move_object(int obj);		/* position changed, redraw at x,y */

/* ---- text ---- */
void gfx_print_text(const char *text, int row, int col, int width,
                     int fg, int bg);
void gfx_clear_lines(int top, int bottom, int color);
void gfx_clear_text_rect(int x1, int y1, int x2, int y2, int color);
void gfx_write_status(int score, int max_score, int sound_on);
void gfx_message_box(const char *msg);
int  gfx_selection_box(const char *msg, const char *const *buttons);

/* ---- parser input box (bottom of screen) ---- */
void gfx_draw_input_box(void);
void gfx_update_input_line(const char *buffer, int cursor_pos);
void gfx_write_prompt(void);

/*
 * Lists owned-object names in a simple two-column inventory screen
 * (mirrors Sarien's inv.c). If select is TRUE, lets the player move a
 * highlight with the arrow keys and returns the chosen index on
 * Enter, or -1 on Escape; otherwise it just waits for any key and
 * always returns -1.
 */
int gfx_show_inventory(const char *const *names, int count, int select);

#endif /* INTERGELLIUM_GFX_IIGS_H */
