/*
 * vocab.h - the AGI text parser: dictionary lookup and sentence
 * tokenizing. Platform-independent; no SDL, no keyboard polling here
 * (that's keyboard.c's job -- it hands vocab.c a finished line).
 */
#ifndef GSAGI_VOCAB_H
#define GSAGI_VOCAB_H

#include "agitypes.h"

/* Loads a WORDS.TOK-format dictionary resource. */
int  vocab_load(const char *path);
void vocab_unload(void);

/*
 * Looks up one word starting at *word*. On success returns its
 * dictionary id and sets *matched_len* to how many characters of
 * *word* belong to it; returns -1 if the word isn't in the
 * dictionary.
 */
int vocab_find_word(const char *word, int *matched_len);

/*
 * Tokenizes a typed sentence (mutates the buffer in place, same as
 * the original) into game.ego_words[]/game.num_ego_words, and raises
 * F_entered_cli / V_word_not_found exactly as AGI logic scripts
 * expect.
 */
void vocab_parse_input(char *sentence);

#endif /* GSAGI_VOCAB_H */
