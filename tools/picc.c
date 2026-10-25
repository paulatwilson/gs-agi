/*
 * picc - compiles a PIC text script into a PIC.n resource.
 *
 * Usage: picc input.pic output.bin
 *
 * Script syntax, one command per line; ';' starts a comment:
 *   vis <0-15|off>     visual colour
 *   pri <0-199|off>    priority (baseline row)
 *   ctl <0-3|off>      control value
 *   poly x y x y ...   filled polygon (up to 32 points)
 *   line x y x y ...   connected lines
 *   loop x y x y ...   closed outline
 *   ellipse cx cy rx ry
 *   flood x y
 *   dot x y
 * The format itself is defined in src/resfmt.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../src/resfmt.h"

static unsigned char out[65536];
static int outlen = 0;
static int lineno = 0;

static void die(const char *msg)
{
	fprintf(stderr, "picc: line %d: %s\n", lineno, msg);
	exit(1);
}

static void emit(int b)
{
	if (outlen >= (int)sizeof(out))
		die("picture too large");
	out[outlen++] = (unsigned char)b;
}

static void emit16(int v)
{
	if (v < -32768 || v > 32767)
		die("coordinate out of range");
	emit(v & 0xFF);
	emit((v >> 8) & 0xFF);
}

static int num(const char *tok)
{
	char *end;
	long v = strtol(tok, &end, 10);

	if (*end != '\0')
		die("expected a number");
	return (int)v;
}

static int value_or_off(const char *tok, int max)
{
	int v;

	if (strcmp(tok, "off") == 0)
		return PIC_OFF;
	v = num(tok);
	if (v < 0 || v > max)
		die("value out of range");
	return v;
}

static void points(int op, char **tok, int ntok)
{
	int i, n;

	if (ntok < 2 || (ntok & 1))
		die("expected x y pairs");
	n = ntok / 2;
	if (n > PIC_MAX_POINTS)
		die("too many points");
	emit(op);
	emit(n);
	for (i = 0; i < ntok; i++)
		emit16(num(tok[i]));
}

static void command(char **tok, int ntok)
{
	const char *c = tok[0];

	if (strcmp(c, "vis") == 0 && ntok == 2) {
		emit(PIC_VIS); emit(value_or_off(tok[1], 15));
	} else if (strcmp(c, "pri") == 0 && ntok == 2) {
		emit(PIC_PRI); emit(value_or_off(tok[1], 199));
	} else if (strcmp(c, "ctl") == 0 && ntok == 2) {
		emit(PIC_CTL); emit(value_or_off(tok[1], 3));
	} else if (strcmp(c, "poly") == 0) {
		points(PIC_POLY, tok + 1, ntok - 1);
	} else if (strcmp(c, "line") == 0) {
		points(PIC_LINE, tok + 1, ntok - 1);
	} else if (strcmp(c, "loop") == 0) {
		points(PIC_LOOP, tok + 1, ntok - 1);
	} else if (strcmp(c, "ellipse") == 0 && ntok == 5) {
		int rx = num(tok[3]), ry = num(tok[4]);

		if (rx < 0 || rx > 255 || ry < 0 || ry > 255)
			die("radius out of range");
		emit(PIC_ELLIPSE);
		emit16(num(tok[1])); emit16(num(tok[2]));
		emit(rx); emit(ry);
	} else if (strcmp(c, "flood") == 0 && ntok == 3) {
		emit(PIC_FLOOD); emit16(num(tok[1])); emit16(num(tok[2]));
	} else if (strcmp(c, "dot") == 0 && ntok == 3) {
		emit(PIC_DOT); emit16(num(tok[1])); emit16(num(tok[2]));
	} else {
		die("unknown command or wrong argument count");
	}
}

int main(int argc, char **argv)
{
	FILE *fp;
	char buf[1024];

	if (argc != 3) {
		fprintf(stderr, "usage: picc input.pic output.bin\n");
		return 2;
	}
	fp = fopen(argv[1], "r");
	if (fp == NULL) {
		perror(argv[1]);
		return 1;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		char *tok[80];
		int ntok = 0;
		char *p, *semi;

		lineno++;
		semi = strchr(buf, ';');
		if (semi)
			*semi = '\0';
		for (p = strtok(buf, " \t\r\n"); p; p = strtok(NULL, " \t\r\n")) {
			if (ntok >= 80)
				die("too many tokens on a line");
			tok[ntok++] = p;
		}
		if (ntok)
			command(tok, ntok);
	}
	fclose(fp);
	emit(PIC_END);

	fp = fopen(argv[2], "wb");
	if (fp == NULL) {
		perror(argv[2]);
		return 1;
	}
	fwrite(out, 1, (size_t)outlen, fp);
	fclose(fp);
	printf("%s: %d bytes\n", argv[2], outlen);
	return 0;
}
