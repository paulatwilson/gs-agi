/*
 * view.h - VIEW.n loader and cel lookup (format in resfmt.h).
 */
#ifndef GSAGI_VIEW_H
#define GSAGI_VIEW_H

#include "agitypes.h"
#include "gamestate.h"

#define MAX_VIEWS 16

struct view_cel {
	int           w, h;
	int           ax, ay;		/* anchor pixel, from the top left */
	const UINT8  *pixels;		/* w*h bytes; VIEW_TRANSPARENT = none */
};

/* Loads VIEW.n if it is not resident yet. */
int   view_load(int n);
void  view_unload_all(void);

int   view_num_loops(int n);
int   view_num_cels(int n, int loop);

/* Looks up one cel. Returns FALSE if the view/loop/cel does not exist. */
int   view_cel(int n, int loop, int cel, struct view_cel *out);

/*
 * Scale factor in percent (1-100) for a view drawn with its baseline at
 * row y. 100 for views that are not VIEW_SCALED.
 */
int   view_scale_at(int n, int y);

/*
 * Refreshes the derived fields of an object after its view, loop, cel
 * or position changed: num_loops, num_cels and the on-screen size
 * (x_size, y_size) at the current perspective scale.
 */
void  view_update_object(struct vt_entry *v);

#endif /* GSAGI_VIEW_H */
