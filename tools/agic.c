/*
 * agic - compiles AGI-style logic scripts into LOGIC.n resources, and
 * builds the WORDS.TOK dictionary and OBJECT list the parser needs.
 *
 * Usage:
 *   agic [-w words.txt] [-o objects.txt] -d outdir [logicN.agi ...]
 *
 * Each source file logicN.agi becomes outdir/LOGIC.N. -w writes
 * outdir/WORDS.TOK and makes its word groups usable in said(); -o
 * writes outdir/OBJECT and defines each item's name as its number.
 *
 * Script language (see game-src/demo/logic*.agi for examples):
 *   #define name value          value is a number, vN, fN or sN
 *   #include "file"
 *   command(args);              dots in names: new.room(2);
 *   v5 = 3;  v5 += v6;  v5++;   assignment and arithmetic on variables
 *   if (cond) stmt else stmt    cond: tests joined by &&, a ( a || b )
 *                               group, ! to negate; tests are isset(f),
 *                               v == 3, has(item), said(a, b), ...
 *   return();
 * Arguments: numbers, vN (variable), fN (flag), sN (string), defined
 * names, or "string" (becomes a message). When a variable is passed to
 * a command that has a .v form (a name ending _f in opcodes.h) that
 * form is used. position(obj, x, y) with x over 255 compiles to the
 * engine's position16.
 *
 * The opcode tables come from src/opcodes.h, shared with the engine.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../src/opcodes.h"

#ifdef _WIN32
#define strtok_r strtok_s
#define strdup _strdup
#endif

#define MAX_TOKENS  20000
#define MAX_DEFINES 512
#define MAX_MSGS    256
#define MAX_WORDS   512
#define MAX_CODE    65000

enum { T_EOF, T_ID, T_NUM, T_STR, T_PUNCT };

struct token {
	int  type;
	int  num;
	int  line;
	char text[260];
};

static struct token toks[MAX_TOKENS];
static int ntoks, pos;
static const char *cur_file = "?";

struct define { char name[64]; char value[64]; };
static struct define defines[MAX_DEFINES];
static int ndefines;

struct word { char text[32]; int group; };
static struct word words[MAX_WORDS];
static int nwords;
struct group { char name[32]; int id; };
static struct group groups[MAX_WORDS];
static int ngroups;

static unsigned char code[MAX_CODE + 16];
static int clen;
static char msgs[MAX_MSGS][256];
static int nmsgs;

#define ACTION_COUNT ((int)(sizeof(action_names) / sizeof(action_names[0])))
#define X_NAME(name, n) #name,
#define X_NARG(name, n) n,
static const char *const action_names[] = { AGI_ACTIONS(X_NAME) };
static const int action_args[] = { AGI_ACTIONS(X_NARG) };
static const char *const test_names[] = { AGI_TESTS(X_NAME) };
static const int test_args[] = { AGI_TESTS(X_NARG) };
#define TEST_COUNT ((int)(sizeof(test_names) / sizeof(test_names[0])))

static void die(const char *msg, const char *detail)
{
	int line = pos < ntoks ? toks[pos].line : 0;

	fprintf(stderr, "agic: %s:%d: %s%s%s\n", cur_file, line, msg,
		detail ? ": " : "", detail ? detail : "");
	exit(1);
}

/* ------------------------------------------------------------------ */
/* Lexer, with #define and #include handled on the way through        */
/* ------------------------------------------------------------------ */

static char *read_file(const char *path)
{
	FILE *fp = fopen(path, "rb");
	long n;
	char *buf;

	if (fp == NULL) {
		fprintf(stderr, "agic: cannot open %s\n", path);
		exit(1);
	}
	fseek(fp, 0, SEEK_END);
	n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buf = (char *)malloc((size_t)n + 1);
	if (fread(buf, 1, (size_t)n, fp) != (size_t)n)
		exit(1);
	buf[n] = '\0';
	fclose(fp);
	return buf;
}

static void add_token(int type, const char *text, int num, int line)
{
	struct token *t;

	if (ntoks >= MAX_TOKENS - 1) {
		fprintf(stderr, "agic: too many tokens\n");
		exit(1);
	}
	t = &toks[ntoks++];
	t->type = type;
	t->num = num;
	t->line = line;
	strncpy(t->text, text, sizeof(t->text) - 1);
	t->text[sizeof(t->text) - 1] = '\0';
}

static void add_define(const char *name, const char *value)
{
	if (ndefines >= MAX_DEFINES) {
		fprintf(stderr, "agic: too many defines\n");
		exit(1);
	}
	strncpy(defines[ndefines].name, name, 63);
	strncpy(defines[ndefines].value, value, 63);
	ndefines++;
}

static void dir_of(const char *path, char *out)
{
	const char *slash = strrchr(path, '/');

	if (slash == NULL) {
		out[0] = '\0';
		return;
	}
	memcpy(out, path, (size_t)(slash - path + 1));
	out[slash - path + 1] = '\0';
}

static void lex(const char *path)
{
	char *src = read_file(path), *p = src;
	char dir[512];
	int line = 1;

	dir_of(path, dir);
	while (*p) {
		char c = *p;

		if (c == '\n') { line++; p++; continue; }
		if (isspace((unsigned char)c)) { p++; continue; }
		if (c == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
		if (c == '/' && p[1] == '*') {
			p += 2;
			while (*p && !(p[0] == '*' && p[1] == '/')) { if (*p == '\n') line++; p++; }
			if (*p) p += 2;
			continue;
		}
		if (c == '#') {
			char word[32], name[64], value[64];
			int n;

			p++;
			if (sscanf(p, "%31s%n", word, &n) != 1) { fprintf(stderr, "agic: %s:%d: bad directive\n", path, line); exit(1); }
			p += n;
			if (strcmp(word, "define") == 0) {
				if (sscanf(p, " %63s %63s%n", name, value, &n) != 2) { fprintf(stderr, "agic: %s:%d: bad #define\n", path, line); exit(1); }
				add_define(name, value);
				p += n;
			} else if (strcmp(word, "include") == 0) {
				char file[256], full[800];

				if (sscanf(p, " \"%255[^\"]\"%n", file, &n) != 1) { fprintf(stderr, "agic: %s:%d: bad #include\n", path, line); exit(1); }
				p += n;
				snprintf(full, sizeof(full), "%s%s", dir, file);
				lex(full);
			} else {
				fprintf(stderr, "agic: %s:%d: unknown directive #%s\n", path, line, word);
				exit(1);
			}
			continue;
		}
		if (isdigit((unsigned char)c)) {
			char *end;
			long v = strtol(p, &end, 10);
			char text[32];

			snprintf(text, sizeof(text), "%ld", v);
			add_token(T_NUM, text, (int)v, line);
			p = end;
			continue;
		}
		if (isalpha((unsigned char)c) || c == '_') {
			char text[128];
			int n = 0;

			while ((isalnum((unsigned char)*p) || *p == '_' || *p == '.') && n < 127)
				text[n++] = *p++;
			/* a trailing dot belongs to the sentence, not the name */
			while (n > 0 && text[n - 1] == '.') { n--; p--; }
			text[n] = '\0';
			add_token(T_ID, text, 0, line);
			continue;
		}
		if (c == '"') {
			char text[256];
			int n = 0;

			p++;
			while (*p && *p != '"') {
				if (*p == '\\' && p[1]) {
					p++;
					text[n++] = *p == 'n' ? '\n' : *p;
					p++;
				} else {
					text[n++] = *p++;
				}
				if (n > 250) { fprintf(stderr, "agic: %s:%d: string too long\n", path, line); exit(1); }
			}
			if (*p == '"') p++;
			text[n] = '\0';
			{
				struct token *t;

				add_token(T_STR, text, 0, line);
				t = &toks[ntoks - 1];
				(void)t;
			}
			continue;
		}
		{
			static const char *two[] = { "==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=", "*=", "/=" };
			char text[3] = { c, 0, 0 };
			int i;

			for (i = 0; i < 12; i++)
				if (p[0] == two[i][0] && p[1] == two[i][1]) {
					text[1] = p[1];
					p++;
					break;
				}
			p++;
			add_token(T_PUNCT, text, 0, line);
		}
	}
	free(src);
}

/* ------------------------------------------------------------------ */
/* Token helpers and name resolution                                  */
/* ------------------------------------------------------------------ */

static struct token *peek(void) { return &toks[pos]; }
static int is_punct(const char *s) { return toks[pos].type == T_PUNCT && strcmp(toks[pos].text, s) == 0; }
static int is_id(const char *s) { return toks[pos].type == T_ID && strcmp(toks[pos].text, s) == 0; }

static void expect(const char *s)
{
	if (!is_punct(s))
		die("expected", s);
	pos++;
}

struct val { int num; int isvar; };

static struct val resolve_name(const char *name, int depth)
{
	struct val v;
	int i;

	if (depth > 8)
		die("define loop", name);
	for (i = ndefines - 1; i >= 0; i--) {
		if (strcmp(defines[i].name, name) == 0) {
			const char *val = defines[i].value;

			if (isdigit((unsigned char)val[0])) {
				v.num = atoi(val);
				v.isvar = 0;
				return v;
			}
			return resolve_name(val, depth + 1);
		}
	}
	if (strlen(name) >= 2 && isdigit((unsigned char)name[1])) {
		char kind = (char)tolower((unsigned char)name[0]);
		char *end;
		long n = strtol(name + 1, &end, 10);

		if (*end == '\0' && (kind == 'v' || kind == 'f' || kind == 's')) {
			v.num = (int)n;
			v.isvar = kind == 'v';
			return v;
		}
	}
	die("unknown name", name);
	v.num = v.isvar = 0;
	return v;
}

static int add_message(const char *text)
{
	int i;

	for (i = 0; i < nmsgs; i++)
		if (strcmp(msgs[i], text) == 0)
			return i + 1;
	if (nmsgs >= MAX_MSGS)
		die("too many messages", NULL);
	strncpy(msgs[nmsgs], text, 255);
	return ++nmsgs;
}

static void emit(int b)
{
	if (clen >= MAX_CODE)
		die("logic too large", NULL);
	code[clen++] = (unsigned char)b;
}

static void put16(int at, int v)
{
	code[at] = (unsigned char)(v & 0xFF);
	code[at + 1] = (unsigned char)((v >> 8) & 0xFF);
}

/* Reads one argument; returns its value and whether it was a variable. */
static struct val parse_arg(void)
{
	struct token *t = peek();
	struct val v;

	if (t->type == T_NUM) {
		v.num = t->num;
		v.isvar = 0;
		pos++;
	} else if (t->type == T_STR) {
		v.num = add_message(t->text);
		v.isvar = 0;
		pos++;
	} else if (t->type == T_ID) {
		v = resolve_name(t->text, 0);
		pos++;
	} else {
		die("expected an argument", t->text);
		v.num = v.isvar = 0;
	}
	return v;
}

static int find_name(const char *const *list, int count, const char *name)
{
	int i;

	for (i = 0; i < count; i++)
		if (strcmp(list[i], name) == 0)
			return i;
	return -1;
}

static void dots_to_underscores(char *s)
{
	for (; *s; s++)
		if (*s == '.')
			*s = '_';
}

/* ------------------------------------------------------------------ */
/* Said(): word lookup                                                */
/* ------------------------------------------------------------------ */

static int word_id(const char *text)
{
	int i;

	for (i = 0; i < nwords; i++)
		if (strcmp(words[i].text, text) == 0)
			return groups[words[i].group].id;
	return -1;
}

static int group_id(const char *name)
{
	int i;

	if (strcmp(name, "anyword") == 0) return 1;
	if (strcmp(name, "rest") == 0) return 9999;
	for (i = 0; i < ngroups; i++)
		if (strcmp(groups[i].name, name) == 0)
			return groups[i].id;
	return -1;
}

static void parse_said(void)
{
	int n = 0, countpos, id;

	emit(0x0E);
	countpos = clen;
	emit(0);
	expect("(");
	while (!is_punct(")")) {
		struct token *t = peek();

		if (t->type == T_STR) {
			id = word_id(t->text);
			if (id < 0)
				die("word not in the dictionary", t->text);
		} else if (t->type == T_ID) {
			id = group_id(t->text);
			if (id < 0)
				die("unknown word group", t->text);
		} else {
			die("expected a word", t->text);
			id = 0;
		}
		pos++;
		emit(id & 0xFF);
		emit((id >> 8) & 0xFF);
		n++;
		if (is_punct(","))
			pos++;
	}
	expect(")");
	code[countpos] = (unsigned char)n;
}

/* ------------------------------------------------------------------ */
/* Conditions                                                         */
/* ------------------------------------------------------------------ */

static void emit_test(int index, struct val *args, int n)
{
	if (n != test_args[index])
		die("wrong number of arguments for test", test_names[index]);
	emit(index + 1);
	{
		int i;

		for (i = 0; i < n; i++) {
			if (args[i].num < 0 || args[i].num > 255)
				die("argument out of range", test_names[index]);
			emit(args[i].num);
		}
	}
}

/* one test: a call like isset(f3), or a comparison like v3 == 5 */
static void parse_test(void)
{
	struct token *t = peek();
	char name[128];
	struct val args[8];
	int n = 0, index;

	if (t->type != T_ID)
		die("expected a test", t->text);

	if (strcmp(t->text, "said") == 0) {
		pos++;
		parse_said();
		return;
	}

	if (toks[pos + 1].type == T_PUNCT && strcmp(toks[pos + 1].text, "(") == 0) {
		int base;

		strncpy(name, t->text, sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
		dots_to_underscores(name);
		if (strcmp(name, "have_key") == 0)
			strcpy(name, "keypressed");
		pos += 2;
		while (!is_punct(")")) {
			if (n >= 8)
				die("too many arguments", NULL);
			args[n++] = parse_arg();
			if (is_punct(","))
				pos++;
		}
		expect(")");
		if (strcmp(name, "isset") == 0 && n == 1 && args[0].isvar)
			strcpy(name, "issetv");
		base = find_name(test_names, TEST_COUNT, name);
		if (base < 0)
			die("unknown test", t->text);
		index = base;
		emit_test(index, args, n);
		return;
	}

	/* comparison: vA op (number | vB) */
	{
		struct val lhs = parse_arg(), rhs;
		char op[4];
		int negate = 0, kind;

		if (!lhs.isvar)
			die("a comparison must start with a variable", NULL);
		if (peek()->type != T_PUNCT)
			die("expected a comparison operator", peek()->text);
		strncpy(op, peek()->text, 3);
		op[3] = '\0';
		pos++;
		rhs = parse_arg();

		if (strcmp(op, "==") == 0) kind = 0;
		else if (strcmp(op, "!=") == 0) { kind = 0; negate = 1; }
		else if (strcmp(op, "<") == 0) kind = 1;
		else if (strcmp(op, ">") == 0) kind = 2;
		else if (strcmp(op, "<=") == 0) { kind = 2; negate = 1; }	/* not greater */
		else if (strcmp(op, ">=") == 0) { kind = 1; negate = 1; }	/* not less */
		else { die("unknown comparison", op); kind = 0; }

		if (negate)
			emit(0xFD);
		{
			static const char *const base[3] = { "equal", "less", "greater" };
			char full[32];
			struct val a[2];

			snprintf(full, sizeof(full), "%s%c", base[kind], rhs.isvar ? 'v' : 'n');
			a[0] = lhs;
			a[1] = rhs;
			index = find_name(test_names, TEST_COUNT, full);
			emit_test(index, a, 2);
		}
	}
}

/* a test, optionally negated */
static void parse_term(void)
{
	if (is_punct("!")) {
		pos++;
		emit(0xFD);
	}
	parse_test();
}

/* Finds out whether the condition ahead is a bare "a || b" list. */
static int bare_or(void)
{
	int depth = 0, i = pos, seen_or = 0, seen_and = 0;

	for (; i < ntoks; i++) {
		struct token *t = &toks[i];

		if (t->type != T_PUNCT)
			continue;
		if (strcmp(t->text, "(") == 0) depth++;
		else if (strcmp(t->text, ")") == 0) {
			if (depth == 0)
				break;
			depth--;
		} else if (depth == 0 && strcmp(t->text, "||") == 0) seen_or = 1;
		else if (depth == 0 && strcmp(t->text, "&&") == 0) seen_and = 1;
	}
	if (seen_or && seen_and)
		die("mix && and || only with a ( ) group around the || part", NULL);
	return seen_or;
}

static void parse_or_group(void)
{
	emit(0xFC);
	for (;;) {
		parse_term();
		if (is_punct("||")) { pos++; continue; }
		break;
	}
	emit(0xFC);
}

static void parse_condition(void)
{
	if (bare_or()) {
		parse_or_group();
		return;
	}
	for (;;) {
		if (is_punct("(") ) {
			/* a ( ) group is an or-list; a lone test in parens is fine too */
			pos++;
			parse_or_group();
			expect(")");
		} else {
			parse_term();
		}
		if (is_punct("&&")) { pos++; continue; }
		break;
	}
}

/* ------------------------------------------------------------------ */
/* Statements                                                         */
/* ------------------------------------------------------------------ */

static void parse_stmt(void);

static void parse_call(void)
{
	char name[128], full[132];
	struct val args[12];
	int n = 0, i, index, anyvar = 0;

	strncpy(name, peek()->text, sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	pos += 2;	/* name and ( */
	while (!is_punct(")")) {
		if (n >= 12)
			die("too many arguments", NULL);
		args[n] = parse_arg();
		if (args[n].isvar)
			anyvar = 1;
		n++;
		if (is_punct(","))
			pos++;
	}
	expect(")");

	dots_to_underscores(name);
	if (strcmp(name, "return") == 0) {
		emit(0);
		return;
	}

	/* position(obj, x, y) with x past 255 needs the 16-bit form */
	if (strcmp(name, "position") == 0 && n == 3 && !anyvar && args[1].num > 255) {
		emit(find_name(action_names, ACTION_COUNT, "position16") + 1);
		emit(args[0].num);
		emit(args[1].num & 0xFF);
		emit((args[1].num >> 8) & 0xFF);
		emit(args[2].num);
		return;
	}

	index = -1;
	if (anyvar) {
		snprintf(full, sizeof(full), "%s_f", name);
		index = find_name(action_names, ACTION_COUNT, full);
	}
	if (index < 0)
		index = find_name(action_names, ACTION_COUNT, name);
	if (index < 0)
		die("unknown command", name);
	if (n != action_args[index])
		die("wrong number of arguments for", name);

	emit(index + 1);
	for (i = 0; i < n; i++) {
		if (args[i].num < 0 || args[i].num > 255)
			die("argument out of range for", name);
		emit(args[i].num);
	}
}

static void parse_assignment(void)
{
	struct val lhs = parse_arg();
	char op[4];
	const char *name;

	if (!lhs.isvar)
		die("only variables can be assigned", NULL);
	if (peek()->type != T_PUNCT)
		die("expected an assignment", peek()->text);
	strncpy(op, peek()->text, 3);
	op[3] = '\0';
	pos++;

	if (strcmp(op, "++") == 0 || strcmp(op, "--") == 0) {
		emit(find_name(action_names, ACTION_COUNT,
			op[0] == '+' ? "increment" : "decrement") + 1);
		emit(lhs.num);
		return;
	}
	{
		struct val rhs = parse_arg();
		const char *base = strcmp(op, "=") == 0 ? "assign" :
			strcmp(op, "+=") == 0 ? "add" :
			strcmp(op, "-=") == 0 ? "sub" :
			strcmp(op, "*=") == 0 ? "mul_" :
			strcmp(op, "/=") == 0 ? "div_" : NULL;
		char full[32];

		if (base == NULL)
			die("unknown assignment operator", op);
		snprintf(full, sizeof(full), "%s%c", base, rhs.isvar ? 'v' : 'n');
		name = full;
		if (rhs.num < 0 || rhs.num > 255)
			die("value out of range", NULL);
		emit(find_name(action_names, ACTION_COUNT, name) + 1);
		emit(lhs.num);
		emit(rhs.num);
	}
}

static void parse_if(void)
{
	int sizepos, start, has_else;

	pos++;	/* if */
	expect("(");
	emit(0xFF);
	parse_condition();
	emit(0xFF);
	expect(")");
	sizepos = clen;
	emit(0); emit(0);
	start = clen;
	parse_stmt();

	has_else = is_id("else");
	if (has_else) {
		int elsepos, estart;

		pos++;
		emit(0xFE);
		elsepos = clen;
		emit(0); emit(0);
		estart = clen;
		parse_stmt();
		put16(elsepos, clen - estart);
		put16(sizepos, estart - start);
	} else {
		put16(sizepos, clen - start);
	}
}

static void parse_stmt(void)
{
	struct token *t = peek();

	if (t->type == T_PUNCT && strcmp(t->text, "{") == 0) {
		pos++;
		while (!is_punct("}")) {
			if (peek()->type == T_EOF)
				die("missing }", NULL);
			parse_stmt();
		}
		pos++;
		return;
	}
	if (t->type == T_PUNCT && strcmp(t->text, ";") == 0) {
		pos++;
		return;
	}
	if (t->type != T_ID)
		die("expected a statement", t->text);

	if (strcmp(t->text, "if") == 0) {
		parse_if();
		return;
	}
	if (strcmp(t->text, "return") == 0) {
		pos++;
		if (is_punct("(")) { pos++; expect(")"); }
		emit(0);
		expect(";");
		return;
	}
	if (toks[pos + 1].type == T_PUNCT && strcmp(toks[pos + 1].text, "(") == 0)
		parse_call();
	else
		parse_assignment();
	expect(";");
}

/* ------------------------------------------------------------------ */
/* Output files                                                       */
/* ------------------------------------------------------------------ */

static void write_logic(const char *outdir, int n)
{
	char path[512];
	unsigned char *out;
	int msgbytes = 0, textlen = 0, i, total, mstart, off;
	FILE *fp;
	static const char key[] = "Avis Durgan";

	for (i = 0; i < nmsgs; i++)
		textlen += (int)strlen(msgs[i]) + 1;
	msgbytes = 3 + nmsgs * 2 + textlen;
	total = 2 + clen + msgbytes;
	out = (unsigned char *)calloc(1, (size_t)total);

	out[0] = (unsigned char)(clen & 0xFF);
	out[1] = (unsigned char)(clen >> 8);
	memcpy(out + 2, code, (size_t)clen);

	mstart = 2 + clen;
	out[mstart] = (unsigned char)nmsgs;
	{
		int mend = nmsgs * 2 + textlen;

		out[mstart + 1] = (unsigned char)(mend & 0xFF);
		out[mstart + 2] = (unsigned char)(mend >> 8);
	}
	/* pointers are relative to the byte after the count, text follows them */
	off = nmsgs * 2;
	for (i = 0; i < nmsgs; i++) {
		int rel = off + 2;
		int len = (int)strlen(msgs[i]) + 1, k;
		unsigned char *text = out + mstart + 3 + off;

		out[mstart + 3 + i * 2] = (unsigned char)(rel & 0xFF);
		out[mstart + 3 + i * 2 + 1] = (unsigned char)(rel >> 8);
		memcpy(text, msgs[i], (size_t)len);
		for (k = 0; k < len; k++)
			text[k] ^= (unsigned char)key[(off - nmsgs * 2 + k) % 11];
		off += len;
	}

	snprintf(path, sizeof(path), "%s/LOGIC.%d", outdir, n);
	fp = fopen(path, "wb");
	if (fp == NULL) { perror(path); exit(1); }
	fwrite(out, 1, (size_t)total, fp);
	fclose(fp);
	printf("%s: %d bytes of code, %d messages\n", path, clen, nmsgs);
	free(out);
}

static int cmp_words(const void *a, const void *b)
{
	return strcmp(((const struct word *)a)->text, ((const struct word *)b)->text);
}

static void load_words(const char *path)
{
	char *src = read_file(path), *line, *save;
	int next_id = 2;

	for (line = strtok_r(src, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char *semi = strchr(line, ';'), *colon, *tok, *save2;

		if (semi) *semi = '\0';
		colon = strchr(line, ':');
		if (colon == NULL) {
			if (strspn(line, " \t\r") == strlen(line))
				continue;
			fprintf(stderr, "agic: %s: expected 'group: word word ...'\n", path);
			exit(1);
		}
		*colon = '\0';
		{
			char gname[32];
			int g;

			if (sscanf(line, " %31s", gname) != 1) { fprintf(stderr, "agic: %s: missing group name\n", path); exit(1); }
			g = ngroups++;
			strcpy(groups[g].name, gname);
			groups[g].id = strcmp(gname, "ignore") == 0 ? 0 : next_id++;
			for (tok = strtok_r(colon + 1, " \t\r", &save2); tok; tok = strtok_r(NULL, " \t\r", &save2)) {
				if (nwords >= MAX_WORDS) { fprintf(stderr, "agic: too many words\n"); exit(1); }
				strncpy(words[nwords].text, tok, 31);
				words[nwords].group = g;
				nwords++;
			}
		}
	}
	free(src);
}

static void write_words(const char *outdir)
{
	unsigned char out[8192];
	int len = 52, letter, i, offsets[26];
	char path[512];
	FILE *fp;

	qsort(words, (size_t)nwords, sizeof(words[0]), cmp_words);
	/* group index must survive the sort: groups[] is unchanged, words[].group still points into it */
	memset(out, 0, sizeof(out));
	for (letter = 0; letter < 26; letter++) {
		const char *prev = "";
		int first = 1;

		offsets[letter] = -1;
		for (i = 0; i < nwords; i++) {
			const char *w = words[i].text;
			int shared = 0, n = (int)strlen(w), k;

			if (w[0] - 'a' != letter)
				continue;
			if (offsets[letter] < 0)
				offsets[letter] = len;
			if (!first)
				while (prev[shared] && prev[shared] == w[shared] && shared < n)
					shared++;
			first = 0;
			out[len++] = (unsigned char)shared;
			for (k = shared; k < n; k++)
				out[len++] = (unsigned char)(w[k] ^ (k == n - 1 ? 0xFF : 0x7F));
			out[len++] = (unsigned char)(groups[words[i].group].id >> 8);
			out[len++] = (unsigned char)(groups[words[i].group].id & 0xFF);
			prev = w;
		}
	}
	/* a letter with no words points at the next letter that has some, or the end */
	for (letter = 25, i = len; letter >= 0; letter--) {
		if (offsets[letter] < 0)
			offsets[letter] = i;
		else
			i = offsets[letter];
	}
	for (letter = 0; letter < 26; letter++) {
		out[letter * 2] = (unsigned char)(offsets[letter] >> 8);
		out[letter * 2 + 1] = (unsigned char)(offsets[letter] & 0xFF);
	}
	snprintf(path, sizeof(path), "%s/WORDS.TOK", outdir);
	fp = fopen(path, "wb");
	if (fp == NULL) { perror(path); exit(1); }
	fwrite(out, 1, (size_t)len, fp);
	fclose(fp);
	printf("%s: %d bytes, %d words\n", path, len, nwords);
}

static void load_and_write_objects(const char *path, const char *outdir)
{
	char *src = read_file(path), *line, *save;
	char names[64][64];
	int rooms[64], n = 1, i;
	unsigned char out[4096];
	int len, table_end, namepos;
	FILE *fp;
	char outpath[512];

	strcpy(names[0], "?");
	rooms[0] = 0;
	for (line = strtok_r(src, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char ident[64], disp[64];
		int room;
		char *semi = strchr(line, ';');

		if (semi) *semi = '\0';
		if (sscanf(line, " %63s %d \"%63[^\"]\"", ident, &room, disp) != 3) {
			if (strspn(line, " \t\r") == strlen(line))
				continue;
			fprintf(stderr, "agic: %s: expected: name room \"Display name\"\n", path);
			exit(1);
		}
		if (n >= 64) { fprintf(stderr, "agic: too many objects\n"); exit(1); }
		strcpy(names[n], disp);
		rooms[n] = room;
		{
			char num[16];

			snprintf(num, sizeof(num), "%d", n);
			add_define(ident, num);
		}
		n++;
	}
	free(src);

	table_end = 3 + n * 3;
	len = table_end;
	memset(out, 0, sizeof(out));
	out[0] = (unsigned char)((n * 3) & 0xFF);
	out[1] = (unsigned char)((n * 3) >> 8);
	out[2] = 16;	/* animated objects */
	namepos = table_end;
	for (i = 0; i < n; i++) {
		int rel = namepos - 3;

		out[3 + i * 3] = (unsigned char)(rel & 0xFF);
		out[3 + i * 3 + 1] = (unsigned char)(rel >> 8);
		out[3 + i * 3 + 2] = (unsigned char)rooms[i];
		strcpy((char *)out + namepos, names[i]);
		namepos += (int)strlen(names[i]) + 1;
	}
	len = namepos;
	snprintf(outpath, sizeof(outpath), "%s/OBJECT", outdir);
	fp = fopen(outpath, "wb");
	if (fp == NULL) { perror(outpath); exit(1); }
	fwrite(out, 1, (size_t)len, fp);
	fclose(fp);
	printf("%s: %d bytes, %d objects\n", outpath, len, n - 1);
}

int main(int argc, char **argv)
{
	const char *words_file = NULL, *objects_file = NULL, *outdir = ".";
	int i;

	for (i = 1; i < argc && argv[i][0] == '-'; i += 2) {
		if (i + 1 >= argc) { fprintf(stderr, "agic: %s needs a value\n", argv[i]); return 2; }
		if (strcmp(argv[i], "-w") == 0) words_file = argv[i + 1];
		else if (strcmp(argv[i], "-o") == 0) objects_file = argv[i + 1];
		else if (strcmp(argv[i], "-d") == 0) outdir = argv[i + 1];
		else { fprintf(stderr, "agic: unknown option %s\n", argv[i]); return 2; }
	}

	if (words_file) {
		load_words(words_file);
		write_words(outdir);
	}
	if (objects_file)
		load_and_write_objects(objects_file, outdir);

	for (; i < argc; i++) {
		const char *base = strrchr(argv[i], '/');
		int n;

		base = base ? base + 1 : argv[i];
		if (sscanf(base, "logic%d", &n) != 1) {
			fprintf(stderr, "agic: %s: name must be logicN.agi\n", argv[i]);
			return 2;
		}
		ntoks = 0;
		clen = 0;
		nmsgs = 0;
		cur_file = argv[i];
		lex(argv[i]);
		add_token(T_EOF, "", 0, toks[ntoks ? ntoks - 1 : 0].line);
		pos = 0;
		while (peek()->type != T_EOF)
			parse_stmt();
		emit(0);	/* every logic ends with a return */
		write_logic(outdir, n);
	}
	return 0;
}
