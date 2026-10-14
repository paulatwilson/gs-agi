/*
 * vocab.c - AGI dictionary (WORDS.TOK) lookup and sentence tokenizer.
 *
 * Extracted from Sarien's src/words.c. The dictionary search itself
 * (vocab_find_word) is resource-format logic, not host logic, so it
 * is carried over byte-for-byte; what changed getting this onto a
 * 65816:
 *
 *   - load/unload use ANSI stdio (fopen/fread), which ORCA/C backs
 *     with GS/OS directly -- no SDL_RWops, no host path munging.
 *   - the old my_strndup() per-word heap allocation is gone. Every
 *     recognized word is copied into the fixed MAX_WORD_LEN buffer
 *     already sitting in game.ego_words[]; nothing here calls
 *     malloc(), so there is no per-sentence heap churn to fragment
 *     the 65816's banked memory.
 *   - no mouse/menu/debug-console hooks (those lived in words.c only
 *     because dictionary_words() shared a file with the SDL build).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "agitypes.h"
#include "util.h"
#include "gamestate.h"
#include "vocab.h"

static UINT8  *words      = NULL;
static UINT32  words_flen = 0;

int vocab_load(const char *path)
{
	FILE  *fp;
	long   flen;
	UINT8 *mem;

	words = NULL;
	words_flen = 0;

	fp = fopen(path, "rb");
	if (fp == NULL)
		return err_BadFileOpen;

	fseek(fp, 0, SEEK_END);
	flen = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	mem = (UINT8 *)malloc((size_t)flen + 1);
	if (mem == NULL) {
		fclose(fp);
		return err_NotEnoughMemory;
	}

	fread(mem, 1, (size_t)flen, fp);
	fclose(fp);

	words = mem;
	words_flen = (UINT32)flen;

	return err_OK;
}

void vocab_unload(void)
{
	if (words != NULL) {
		free(words);
		words = NULL;
		words_flen = 0;
	}
}

/*
 * Sierra's dictionary format buckets words by first letter: a 26-entry
 * header of 2-byte offsets, then for each letter a run of entries
 * shaped as:
 *   <n-shared-chars byte> <xor'd chars...> <last-char ^ 0xFF> <id:2 lohi>
 * Two adjacent entries share a prefix; each entry only stores the
 * *new* suffix on top of the previous entry's text. That's what the
 * mchr/p[0] dance below is doing.
 */
int vocab_find_word(const char *word, int *matched_len)
{
	int    mchr = 0;
	int    len, fchr, id = -1;
	UINT8 *p, *q;

	*matched_len = 0;

	if (words == NULL)
		return -1;

	if (word[0] < 'a' || word[0] > 'z')
		return -1;

	fchr = word[0] - 'a';
	len = (int)strlen(word);

	/* dictionary offsets are stored big-endian in the file */
	p = words;
	p += ((UINT16)words[2 * fchr] << 8) | words[2 * fchr + 1];
	q = words + words_flen;

	while (p[0] >= mchr) {
		if (p[0] == mchr) {
			p++;
			while ((p[0] ^ word[mchr]) == 0x7F && mchr < len) {
				mchr++;
				p++;
			}
			if ((p[0] ^ word[mchr]) == 0xFF && mchr < len) {
				mchr++;
				if (word[mchr] == 0 || word[mchr] == ' ') {
					id = ((UINT16)p[1] << 8) | p[2];
					*matched_len = mchr;
				}
			}
		}

		if (p >= q)
			return -1;

		while (p[0] < 0x80)
			p++;
		p += 3;
	}

	return id;
}

static void copy_word(char *dst, const char *src, int n)
{
	if (n >= MAX_WORD_LEN)
		n = MAX_WORD_LEN - 1;
	memcpy(dst, src, (size_t)n);
	dst[n] = '\0';
}

void vocab_parse_input(char *sentence)
{
	char *p, *q;
	int   wid, wlen;

	game.num_ego_words = 0;

	for (p = sentence; p != NULL && *p != '\0' &&
	     getvar(V_word_not_found) == 0 &&
	     game.num_ego_words < MAX_WORDS; ) {

		if (*p == ' ')
			p++;
		if (*p == '\0')
			break;

		wid = vocab_find_word(p, &wlen);

		if (wid == -1) {
			/* unrecognized word: report it and stop */
			copy_word(game.ego_words[game.num_ego_words].word,
				p, (int)strlen(p));
			game.ego_words[game.num_ego_words].id = 19999;
			setvar(V_word_not_found, 1 + game.num_ego_words);
			game.num_ego_words++;
			p += strlen(p);
			q = NULL;
		} else if (wid == 0) {
			/* word 0 ("a", "the", ...) is ignored entirely */
			p += wlen;
			q = NULL;
		} else {
			game.ego_words[game.num_ego_words].id = wid;
			copy_word(game.ego_words[game.num_ego_words].word,
				p, wlen);
			game.num_ego_words++;
			q = p;
			p += wlen;
		}

		if (p != NULL && *p != '\0') {
			*p = '\0';
			p++;
		}

		if (q != NULL) {
			for (; *q != '\0' && *q != ' '; q++)
				;
			if (*q != '\0') {
				*q = '\0';
				q++;
			}
		}
	}

	if (game.num_ego_words > 0) {
		setflag(F_entered_cli, TRUE);
		setflag(F_said_accepted_input, FALSE);
	}
}
