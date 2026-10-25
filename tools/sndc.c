/*
 * sndc - compiles a SOUND text script into a SOUND.n resource.
 *
 * Usage: sndc input.snd output.bin
 *
 * Script syntax; ';' starts a comment:
 *   kind music|effect
 *   gen square|triangle|saw|noise|sine      starts a generator
 *   note <pitch> <volume> <ticks>           pitch is a MIDI note number,
 *                                           0 is a rest; volume 0-255;
 *                                           ticks are 1/60 s
 * The format is described in src/audio.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GEN   8
#define MAX_NOTES 256

struct note { int pitch, vol, ticks; };
struct gen { int wave; int n; struct note notes[MAX_NOTES]; };

static struct gen gens[MAX_GEN];
static int ngens = 0, kind = 0, lineno = 0;

static void die(const char *m)
{
	fprintf(stderr, "sndc: line %d: %s\n", lineno, m);
	exit(1);
}

int main(int argc, char **argv)
{
	FILE *fp;
	char buf[256];
	unsigned char out[65536];
	int len = 0, g, i;

	if (argc != 3) {
		fprintf(stderr, "usage: sndc input.snd output.bin\n");
		return 2;
	}
	fp = fopen(argv[1], "r");
	if (fp == NULL) { perror(argv[1]); return 1; }
	while (fgets(buf, sizeof(buf), fp)) {
		char *semi = strchr(buf, ';'), *a, *b, *c;

		lineno++;
		if (semi) *semi = '\0';
		a = strtok(buf, " \t\r\n");
		if (a == NULL)
			continue;
		b = strtok(NULL, " \t\r\n");
		if (strcmp(a, "kind") == 0 && b) {
			kind = strcmp(b, "music") == 0;
		} else if (strcmp(a, "gen") == 0 && b) {
			static const char *const names[] = { "square", "triangle", "saw", "noise", "sine" };
			int w;

			if (ngens >= MAX_GEN) die("too many generators");
			for (w = 0; w < 5 && strcmp(b, names[w]) != 0; w++)
				;
			if (w == 5) die("unknown wave");
			gens[ngens].wave = w;
			gens[ngens].n = 0;
			ngens++;
		} else if (strcmp(a, "note") == 0 && b) {
			struct gen *gp;

			c = strtok(NULL, " \t\r\n");
			if (ngens == 0 || c == NULL) die("note needs a generator and 3 numbers");
			gp = &gens[ngens - 1];
			if (gp->n >= MAX_NOTES) die("too many notes");
			gp->notes[gp->n].pitch = atoi(b);
			gp->notes[gp->n].vol = atoi(c);
			gp->notes[gp->n].ticks = atoi(strtok(NULL, " \t\r\n"));
			gp->n++;
		} else {
			die("unknown command");
		}
	}
	fclose(fp);

	out[len++] = (unsigned char)kind;
	out[len++] = (unsigned char)(ngens & 0xFF);
	out[len++] = (unsigned char)(ngens >> 8);
	for (g = 0; g < ngens; g++) {
		out[len++] = (unsigned char)gens[g].wave;
		out[len++] = (unsigned char)(gens[g].n & 0xFF);
		out[len++] = (unsigned char)(gens[g].n >> 8);
		for (i = 0; i < gens[g].n; i++) {
			out[len++] = (unsigned char)gens[g].notes[i].pitch;
			out[len++] = (unsigned char)gens[g].notes[i].vol;
			out[len++] = (unsigned char)(gens[g].notes[i].ticks & 0xFF);
			out[len++] = (unsigned char)(gens[g].notes[i].ticks >> 8);
		}
	}
	fp = fopen(argv[2], "wb");
	if (fp == NULL) { perror(argv[2]); return 1; }
	fwrite(out, 1, (size_t)len, fp);
	fclose(fp);
	printf("%s: %d bytes\n", argv[2], len);
	return 0;
}
