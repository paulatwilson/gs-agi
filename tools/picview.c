/*
 * picview - renders compiled PIC.n files to a PPM image so pictures can
 * be checked without running the game. It uses the engine's own
 * decoder (src/pic.c), so what it shows is what the game will draw.
 *
 * Usage: picview [-pri|-ctl] out.ppm pic1.bin [pic2.bin ...]
 *   -pri  show the priority layer (brighter = lower on screen)
 *   -ctl  show the control layer (red blocks walking, green exits)
 * Later pictures draw over earlier ones, like overlay.pic.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/agitypes.h"
#include "../src/gamestate.h"
#include "../src/pic.h"

struct agi_game game;	/* util.c expects the engine global; unused here */

static const UINT16 palette[16] = {
	0x000, 0x9DF, 0x911, 0x38B, 0x7C4, 0x4A3, 0x261, 0xFD2,
	0x852, 0x531, 0xAAA, 0x777, 0x444, 0xFC9, 0xD33, 0xFFF
};

int main(int argc, char **argv)
{
	static UINT8 vis[PIC_W * PIC_H], pri[PIC_W * PIC_H], ctl[PIC_CTL_BYTES];
	struct pic_layers L;
	int mode = 0, a = 1, x, y;
	FILE *fp;

	L.vis = vis; L.pri = pri; L.ctl = ctl;
	if (a < argc && strcmp(argv[a], "-pri") == 0) { mode = 1; a++; }
	else if (a < argc && strcmp(argv[a], "-ctl") == 0) { mode = 2; a++; }
	if (argc - a < 2) {
		fprintf(stderr, "usage: picview [-pri|-ctl] out.ppm pic.bin ...\n");
		return 2;
	}
	{
		const char *outname = argv[a++];

		for (; a < argc; a++) {
			static UINT8 data[65536];
			size_t n;

			fp = fopen(argv[a], "rb");
			if (fp == NULL) { perror(argv[a]); return 1; }
			n = fread(data, 1, sizeof(data), fp);
			fclose(fp);
			if (pic_draw(data, (UINT32)n, &L) != err_OK) {
				fprintf(stderr, "%s: bad picture\n", argv[a]);
				return 1;
			}
		}
		fp = fopen(outname, "wb");
		if (fp == NULL) { perror(outname); return 1; }
	}
	fprintf(fp, "P6\n%d %d\n255\n", PIC_W, PIC_H);
	for (y = 0; y < PIC_H; y++) {
		for (x = 0; x < PIC_W; x++) {
			UINT32 i = (UINT32)y * PIC_W + (UINT32)x;
			UINT16 c = palette[vis[i] & 15];
			int r = ((c >> 8) & 15) * 17, g = ((c >> 4) & 15) * 17, b = (c & 15) * 17;

			if (mode == 1) {
				r = g = b = pri[i];
			} else if (mode == 2) {
				int v = pic_ctl_get(ctl, x, y);

				if (v == 1) { r = 255; g /= 3; b /= 3; }
				else if (v == 2) { g = 255; r /= 3; b /= 3; }
			}
			fputc(r, fp); fputc(g, fp); fputc(b, fp);
		}
	}
	fclose(fp);
	return 0;
}
