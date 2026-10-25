/*
 * viewc - compiles a VIEW text script into a VIEW.n resource.
 *
 * Usage: viewc input.view output.bin
 *
 * Script syntax; ';' starts a comment:
 *   scaled <top_y> <bottom_y> <min_percent>    optional, once, first
 *   loop                                       starts a new loop
 *   cel <w> <h> <anchor_x> <anchor_y>          starts a new cel, then h
 *                                              rows of w characters
 * Pixel characters: 0-9 A-F are palette indices, '.' is transparent.
 * The format itself is defined in src/resfmt.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../src/resfmt.h"

#define MAX_LOOPS 16
#define MAX_CELS  32

struct cel {
	int w, h, ax, ay;
	unsigned char *px;
};

struct loop {
	int ncels;
	struct cel cels[MAX_CELS];
};

static struct loop loops[MAX_LOOPS];
static int nloops = 0;
static int flags = 0, top_y = 0, bottom_y = 0, min_pct = 100;
static int lineno = 0;

static void die(const char *msg)
{
	fprintf(stderr, "viewc: line %d: %s\n", lineno, msg);
	exit(1);
}

static void put16(unsigned char *buf, int off, int v)
{
	buf[off] = (unsigned char)(v & 0xFF);
	buf[off + 1] = (unsigned char)((v >> 8) & 0xFF);
}

int main(int argc, char **argv)
{
	FILE *fp;
	char buf[1024];
	struct cel *cur = NULL;
	int rows_left = 0, row = 0;
	unsigned char *out;
	int total, i, j, off;

	if (argc != 3) {
		fprintf(stderr, "usage: viewc input.view output.bin\n");
		return 2;
	}
	fp = fopen(argv[1], "r");
	if (fp == NULL) {
		perror(argv[1]);
		return 1;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		char *semi;

		lineno++;
		buf[strcspn(buf, "\r\n")] = '\0';
		if (rows_left > 0) {
			int x;

			if ((int)strlen(buf) != cur->w)
				die("pixel row has the wrong width");
			for (x = 0; x < cur->w; x++) {
				char ch = buf[x];

				if (ch == '.')
					cur->px[row * cur->w + x] = VIEW_TRANSPARENT;
				else if (isxdigit((unsigned char)ch))
					cur->px[row * cur->w + x] = (unsigned char)
						(isdigit((unsigned char)ch) ? ch - '0' :
						toupper((unsigned char)ch) - 'A' + 10);
				else
					die("bad pixel character");
			}
			row++;
			rows_left--;
			continue;
		}
		semi = strchr(buf, ';');
		if (semi)
			*semi = '\0';
		{
			char *tok = strtok(buf, " \t");

			if (tok == NULL)
				continue;
			if (strcmp(tok, "scaled") == 0) {
				flags |= VIEW_SCALED;
				top_y = atoi(strtok(NULL, " \t"));
				bottom_y = atoi(strtok(NULL, " \t"));
				min_pct = atoi(strtok(NULL, " \t"));
			} else if (strcmp(tok, "loop") == 0) {
				if (nloops >= MAX_LOOPS)
					die("too many loops");
				nloops++;
			} else if (strcmp(tok, "cel") == 0) {
				struct loop *lp;

				if (nloops == 0)
					die("cel before loop");
				lp = &loops[nloops - 1];
				if (lp->ncels >= MAX_CELS)
					die("too many cels");
				cur = &lp->cels[lp->ncels++];
				cur->w = atoi(strtok(NULL, " \t"));
				cur->h = atoi(strtok(NULL, " \t"));
				cur->ax = atoi(strtok(NULL, " \t"));
				cur->ay = atoi(strtok(NULL, " \t"));
				if (cur->w < 1 || cur->w > 255 || cur->h < 1 || cur->h > 255)
					die("cel size out of range");
				cur->px = (unsigned char *)malloc((size_t)cur->w * cur->h);
				rows_left = cur->h;
				row = 0;
			} else {
				die("unknown command");
			}
		}
	}
	fclose(fp);
	if (rows_left > 0)
		die("file ends inside a cel");

	/* layout: header, loop offset table, then each loop with its cels */
	total = VIEW_HEADER_SIZE + nloops * 2;
	for (i = 0; i < nloops; i++) {
		total += 1 + loops[i].ncels * 2;
		for (j = 0; j < loops[i].ncels; j++)
			total += 4 + loops[i].cels[j].w * loops[i].cels[j].h;
	}
	out = (unsigned char *)calloc(1, (size_t)total);
	out[0] = (unsigned char)flags;
	out[1] = (unsigned char)nloops;
	put16(out, 2, top_y);
	put16(out, 4, bottom_y);
	out[6] = (unsigned char)min_pct;

	off = VIEW_HEADER_SIZE + nloops * 2;
	for (i = 0; i < nloops; i++) {
		int cel_off;

		put16(out, VIEW_HEADER_SIZE + i * 2, off);
		out[off] = (unsigned char)loops[i].ncels;
		cel_off = off + 1 + loops[i].ncels * 2;
		for (j = 0; j < loops[i].ncels; j++) {
			struct cel *c = &loops[i].cels[j];

			put16(out, off + 1 + j * 2, cel_off);
			out[cel_off] = (unsigned char)c->w;
			out[cel_off + 1] = (unsigned char)c->h;
			out[cel_off + 2] = (unsigned char)c->ax;
			out[cel_off + 3] = (unsigned char)c->ay;
			memcpy(out + cel_off + 4, c->px, (size_t)c->w * c->h);
			cel_off += 4 + c->w * c->h;
		}
		off = cel_off;
	}

	fp = fopen(argv[2], "wb");
	if (fp == NULL) {
		perror(argv[2]);
		return 1;
	}
	fwrite(out, 1, (size_t)total, fp);
	fclose(fp);
	printf("%s: %d bytes, %d loops\n", argv[2], total, nloops);
	return 0;
}
