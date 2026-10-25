/*
 * view.c - VIEW.n loader. Whole files stay resident once loaded; the
 * demo's views total a few tens of kilobytes.
 */
#include <stdio.h>
#include <stdlib.h>

#include "agitypes.h"
#include "util.h"
#include "gamestate.h"
#include "resfmt.h"
#include "syshooks.h"
#include "view.h"

struct view {
	UINT8  *data;
	UINT32  len;
};

static struct view views[MAX_VIEWS];

int view_load(int n)
{
	char name[16], path[256];
	FILE *fp;
	long flen;
	UINT8 *mem;

	if (n < 0 || n >= MAX_VIEWS)
		return err_BadResource;
	if (views[n].data != NULL)
		return err_OK;

	sprintf(name, "VIEW.%d", n);
	sys_game_path(path, sizeof(path), name);
	fp = fopen(path, "rb");
	if (fp == NULL)
		return err_BadFileOpen;

	fseek(fp, 0, SEEK_END);
	flen = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (flen < VIEW_HEADER_SIZE) {
		fclose(fp);
		return err_BadResource;
	}
	mem = (UINT8 *)malloc((size_t)flen);
	if (mem == NULL || fread(mem, 1, (size_t)flen, fp) != (size_t)flen) {
		free(mem);
		fclose(fp);
		return mem == NULL ? err_NotEnoughMemory : err_BadFileOpen;
	}
	fclose(fp);

	views[n].data = mem;
	views[n].len = (UINT32)flen;
	return err_OK;
}

void view_unload_all(void)
{
	int n;

	for (n = 0; n < MAX_VIEWS; n++) {
		free(views[n].data);
		views[n].data = NULL;
		views[n].len = 0;
	}
}

static const UINT8 *resident(int n)
{
	if (n < 0 || n >= MAX_VIEWS)
		return NULL;
	if (views[n].data == NULL && view_load(n) != err_OK)
		return NULL;
	return views[n].data;
}

int view_num_loops(int n)
{
	const UINT8 *d = resident(n);

	return d == NULL ? 0 : d[1];
}

static const UINT8 *loop_ptr(int n, int loop)
{
	const UINT8 *d = resident(n);
	UINT32 off;

	if (d == NULL || loop < 0 || loop >= d[1])
		return NULL;
	off = lohi_getword((UINT8 *)d + VIEW_HEADER_SIZE + loop * 2);
	return off < views[n].len ? d + off : NULL;
}

int view_num_cels(int n, int loop)
{
	const UINT8 *l = loop_ptr(n, loop);

	return l == NULL ? 0 : l[0];
}

int view_cel(int n, int loop, int cel, struct view_cel *out)
{
	const UINT8 *l = loop_ptr(n, loop);
	const UINT8 *d = resident(n);
	UINT32 off;
	const UINT8 *c;

	if (l == NULL || cel < 0 || cel >= l[0])
		return FALSE;
	off = lohi_getword((UINT8 *)l + 1 + cel * 2);
	if (off + 4 > views[n].len)
		return FALSE;
	c = d + off;
	out->w = c[0];
	out->h = c[1];
	out->ax = c[2];
	out->ay = c[3];
	out->pixels = c + 4;
	return off + 4 + (UINT32)out->w * out->h <= views[n].len;
}

int view_scale_at(int n, int y)
{
	const UINT8 *d = resident(n);
	int top, bottom, min;

	if (d == NULL || !(d[0] & VIEW_SCALED))
		return 100;
	top = (SINT16)lohi_getword((UINT8 *)d + 2);
	bottom = (SINT16)lohi_getword((UINT8 *)d + 4);
	min = d[6];
	if (y <= top || bottom <= top)
		return min;
	if (y >= bottom)
		return 100;
	return min + (int)((long)(100 - min) * (y - top) / (bottom - top));
}

void view_update_object(struct vt_entry *v)
{
	struct view_cel c;
	int pct;

	v->num_loops = (UINT8)view_num_loops(v->current_view);
	v->num_cels = (UINT8)view_num_cels(v->current_view, v->current_loop);
	if (!view_cel(v->current_view, v->current_loop, v->current_cel, &c)) {
		v->x_size = v->y_size = 0;
		return;
	}
	pct = view_scale_at(v->current_view, v->y_pos);
	v->x_size = (SINT16)(((long)c.w * pct + 50) / 100);
	v->y_size = (SINT16)(((long)c.h * pct + 50) / 100);
	if (v->x_size < 1) v->x_size = 1;
	if (v->y_size < 1) v->y_size = 1;
}
