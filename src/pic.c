/*
 * pic.c - PIC.n vector picture decoder.
 *
 * Written for the 65816: 16-bit int, so every pixel offset and every
 * product that can pass 32767 is computed in 32-bit (long) arithmetic,
 * and there are no floating-point operations. The ellipse and polygon
 * fills use rounded integer maths. The flood fill uses an explicit,
 * fixed-size stack instead of recursion.
 */
#include <string.h>

#include "agitypes.h"
#include "util.h"
#include "pic.h"

#define FLOOD_STACK 256

struct pen {
	const struct pic_layers *L;
	int vis, pri, ctl;
};

static long floor_div(long a, long b)
{
	long q = a / b;

	if ((a % b != 0) && ((a < 0) != (b < 0)))
		q--;
	return q;
}

static UINT32 isqrt32(UINT32 v)
{
	UINT32 r = 0, bit = (UINT32)1 << 30;

	while (bit > v)
		bit >>= 2;
	while (bit != 0) {
		if (v >= r + bit) {
			v -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return r;
}

UINT8 pic_ctl_get(const UINT8 *ctl, int x, int y)
{
	UINT32 i;

	if (x < 0 || x >= PIC_W || y < 0 || y >= PIC_H)
		return 1;
	i = (UINT32)y * (PIC_W / 4) + (UINT32)(x >> 2);
	return (UINT8)((ctl[i] >> ((x & 3) << 1)) & 3);
}

static void ctl_put(UINT8 *ctl, int x, int y, int v)
{
	UINT32 i = (UINT32)y * (PIC_W / 4) + (UINT32)(x >> 2);
	int shift = (x & 3) << 1;

	ctl[i] = (UINT8)((ctl[i] & ~(3 << shift)) | ((v & 3) << shift));
}

static void put(struct pen *p, int x, int y)
{
	UINT32 i;

	if (x < 0 || x >= PIC_W || y < 0 || y >= PIC_H)
		return;
	i = (UINT32)y * PIC_W + (UINT32)x;
	if (p->vis != PIC_OFF)
		p->L->vis[i] = (UINT8)p->vis;
	if (p->pri != PIC_OFF)
		p->L->pri[i] = (UINT8)p->pri;
	if (p->ctl != PIC_OFF)
		ctl_put(p->L->ctl, x, y, p->ctl);
}

static void hspan(struct pen *p, int x1, int x2, int y)
{
	int x;

	for (x = x1; x <= x2; x++)
		put(p, x, y);
}

static void line(struct pen *p, int x0, int y0, int x1, int y1)
{
	int dx = x1 > x0 ? x1 - x0 : x0 - x1;
	int dy = y1 > y0 ? y0 - y1 : y1 - y0;	/* -|y1 - y0| */
	int sx = x0 < x1 ? 1 : -1;
	int sy = y0 < y1 ? 1 : -1;
	int e = dx + dy, e2;

	for (;;) {
		put(p, x0, y0);
		if (x0 == x1 && y0 == y1)
			break;
		e2 = 2 * e;
		if (e2 >= dy) { e += dy; x0 += sx; }
		if (e2 <= dx) { e += dx; y0 += sy; }
	}
}

static void fill_poly(struct pen *p, const int *px, const int *py, int n)
{
	int minY = 32767, maxY = -32768, i, j, k, y, m;
	int xs[PIC_MAX_POINTS];

	for (i = 0; i < n; i++) {
		if (py[i] < minY) minY = py[i];
		if (py[i] > maxY) maxY = py[i];
	}
	if (minY < 0) minY = 0;
	if (maxY > PIC_H - 1) maxY = PIC_H - 1;

	for (y = minY; y <= maxY; y++) {
		m = 0;
		for (i = 0; i < n; i++) {
			int x1, y1, x2, y2;
			long num, den;

			j = (i + 1) % n;
			x1 = px[i]; y1 = py[i]; x2 = px[j]; y2 = py[j];
			if (!((y1 <= y && y2 > y) || (y2 <= y && y1 > y)))
				continue;
			num = (long)(y - y1) * (x2 - x1);
			den = (long)(y2 - y1);
			if (den < 0) { num = -num; den = -den; }
			/* x1 + num/den, rounded half up */
			xs[m++] = (int)(x1 + floor_div(2 * num + den, 2 * den));
		}
		/* insertion sort: m is tiny */
		for (i = 1; i < m; i++) {
			int v = xs[i];

			for (k = i - 1; k >= 0 && xs[k] > v; k--)
				xs[k + 1] = xs[k];
			xs[k + 1] = v;
		}
		for (k = 0; k + 1 < m; k += 2) {
			int x;

			for (x = xs[k]; x < xs[k + 1]; x++)
				put(p, x, y);
		}
	}
}

static void fill_ellipse(struct pen *p, int cx, int cy, int rx, int ry)
{
	int y;

	if (ry <= 0) {
		hspan(p, cx - rx, cx + rx, cy);
		return;
	}
	for (y = cy - ry; y <= cy + ry; y++) {
		long dy = y - cy;
		UINT32 t = (UINT32)((long)ry * ry - dy * dy);
		UINT32 dx4 = isqrt32(16UL * (UINT32)((long)rx * rx) * t /
			(UINT32)((long)ry * ry));

		hspan(p, cx - (int)((dx4 + 1) >> 2), cx + (int)((dx4 + 2) >> 2), y);
	}
}

/* ---- flood fill: scanline, with a bounded stack of seeds ---- */

struct seed {
	int x;
	int y;
};

static struct seed stack[FLOOD_STACK];
static int sp;

static void push(int x, int y)
{
	if (sp < FLOOD_STACK) {
		stack[sp].x = x;
		stack[sp].y = y;
		sp++;
	}
}

static void flood(struct pen *p, int sx, int sy)
{
	UINT8 *vis = p->L->vis;
	UINT8 old, nu;

	if (p->vis == PIC_OFF || sx < 0 || sx >= PIC_W || sy < 0 || sy >= PIC_H)
		return;
	old = vis[(UINT32)sy * PIC_W + (UINT32)sx];
	nu = (UINT8)p->vis;
	if (old == nu)
		return;

	sp = 0;
	push(sx, sy);
	while (sp > 0) {
		int x, y, l, r, i;
		UINT8 *row;

		sp--;
		x = stack[sp].x;
		y = stack[sp].y;
		row = vis + (UINT32)y * PIC_W;
		if (row[x] != old)
			continue;

		l = x;
		while (l > 0 && row[l - 1] == old)
			l--;
		r = x;
		while (r < PIC_W - 1 && row[r + 1] == old)
			r++;
		for (i = l; i <= r; i++)
			row[i] = nu;

		for (i = l; i <= r; i++) {
			if (y > 0 && vis[(UINT32)(y - 1) * PIC_W + (UINT32)i] == old) {
				push(i, y - 1);
				while (i <= r && vis[(UINT32)(y - 1) * PIC_W + (UINT32)i] == old)
					i++;
			}
		}
		for (i = l; i <= r; i++) {
			if (y < PIC_H - 1 && vis[(UINT32)(y + 1) * PIC_W + (UINT32)i] == old) {
				push(i, y + 1);
				while (i <= r && vis[(UINT32)(y + 1) * PIC_W + (UINT32)i] == old)
					i++;
			}
		}
	}
}

/* ---- the command stream ---- */

static int rd16(const UINT8 *p)
{
	return (SINT16)lohi_getword((UINT8 *)p);
}

int pic_draw(const UINT8 *data, UINT32 len, const struct pic_layers *layers)
{
	struct pen pen;
	UINT32 ip = 0;
	int px[PIC_MAX_POINTS], py[PIC_MAX_POINTS];

	pen.L = layers;
	pen.vis = pen.pri = pen.ctl = PIC_OFF;

	while (ip < len) {
		UINT8 op = data[ip++];
		int n, i;

		switch (op) {
		case PIC_END:
			return err_OK;
		case PIC_VIS:
		case PIC_PRI:
		case PIC_CTL:
			if (ip + 1 > len) return err_BadResource;
			n = data[ip++];
			if (op == PIC_VIS)
				pen.vis = n == PIC_OFF ? PIC_OFF : n & 15;
			else if (op == PIC_PRI)
				pen.pri = n;
			else
				pen.ctl = n == PIC_OFF ? PIC_OFF : n & 3;
			break;
		case PIC_LINE:
		case PIC_LOOP:
		case PIC_POLY:
			if (ip + 1 > len) return err_BadResource;
			n = data[ip++];
			if (n > PIC_MAX_POINTS || ip + (UINT32)n * 4 > len)
				return err_BadResource;
			for (i = 0; i < n; i++, ip += 4) {
				px[i] = rd16(data + ip);
				py[i] = rd16(data + ip + 2);
			}
			if (op == PIC_POLY) {
				fill_poly(&pen, px, py, n);
			} else {
				for (i = 0; i + 1 < n; i++)
					line(&pen, px[i], py[i], px[i + 1], py[i + 1]);
				if (op == PIC_LOOP && n > 1)
					line(&pen, px[n - 1], py[n - 1], px[0], py[0]);
			}
			break;
		case PIC_ELLIPSE:
			if (ip + 6 > len) return err_BadResource;
			fill_ellipse(&pen, rd16(data + ip), rd16(data + ip + 2),
				data[ip + 4], data[ip + 5]);
			ip += 6;
			break;
		case PIC_FLOOD:
			if (ip + 4 > len) return err_BadResource;
			flood(&pen, rd16(data + ip), rd16(data + ip + 2));
			ip += 4;
			break;
		case PIC_DOT:
			if (ip + 4 > len) return err_BadResource;
			put(&pen, rd16(data + ip), rd16(data + ip + 2));
			ip += 4;
			break;
		default:
			return err_BadResource;
		}
	}
	return err_OK;
}
