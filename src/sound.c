/*
 * sound.c - SOUND.n loader and note sequencer for AGI's sound() opcode.
 *
 * SOUND RESOURCE FORMAT: Sierra's PC/PCjr AGI sound resources encode
 * three-voice square-wave tone sequences, tuned for that hardware --
 * not what you want to hand a wavetable chip. Rather than silently
 * mis-decode the original format, this module expects sound resources
 * pre-converted to the flat note-list described in audio.h.
 *
 * Two sounds can play together, one music and one effect, each on its
 * own range of generators. Playback is delegated to the platform's
 * sndhw_* backend (sound_hw.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agitypes.h"
#include "util.h"
#include "gamestate.h"
#include "audio.h"
#include "sound_hw.h"
#include "syshooks.h"

#define MAX_GENERATORS 4	/* per sound */
#define MAX_NOTES      64

#define KIND_EFFECT 0
#define KIND_MUSIC  1

struct note {
	UINT8  pitch;
	UINT8  volume;
	UINT16 duration;
};

struct generator_track {
	UINT8       wave;
	int         num_notes;
	struct note notes[MAX_NOTES];
	int         pos;
	UINT16      ticks_left;
};

struct slot {
	struct generator_track tracks[MAX_GENERATORS];
	int num_tracks;
	int playing;
	int flag_on_done;
	int base;			/* first generator this slot owns */
};

static struct slot slots[2] = { { {{0}}, 0, 0, -1, 0 }, { {{0}}, 0, 0, -1, 4 } };
static int sound_enabled = 1;

static void silence(struct slot *sl);

int sound_init(void)
{
	return sndhw_init();
}

void sound_shutdown(void)
{
	sound_stop();
	sndhw_shutdown();
}

static int read_u16(FILE *fp, UINT16 *out)
{
	UINT8 b[2];

	if (fread(b, 1, 2, fp) != 2)
		return 0;
	*out = lohi_getword(b);
	return 1;
}

/* Loads the tracks of a file into its slot. Returns the slot, or -1. */
static int load_sound(const char *path)
{
	FILE *fp = fopen(path, "rb");
	UINT8 kind;
	UINT16 gen_count;
	struct slot *sl;
	int g;

	if (fp == NULL)
		return -1;
	if (fread(&kind, 1, 1, fp) != 1 || !read_u16(fp, &gen_count)) {
		fclose(fp);
		return -1;
	}
	sl = &slots[kind == KIND_MUSIC ? 1 : 0];
	silence(sl);	/* a new sound replaces the one of its kind */
	if (gen_count > MAX_GENERATORS)
		gen_count = MAX_GENERATORS;

	for (g = 0; g < gen_count; g++) {
		struct generator_track *t = &sl->tracks[g];
		UINT16 note_count;
		int i;

		if (fread(&t->wave, 1, 1, fp) != 1 || !read_u16(fp, &note_count))
			break;
		if (note_count > MAX_NOTES)
			note_count = MAX_NOTES;

		for (i = 0; i < note_count; i++) {
			UINT8 pv[2];
			UINT16 dur;

			if (fread(pv, 1, 2, fp) != 2 || !read_u16(fp, &dur))
				break;
			t->notes[i].pitch = pv[0];
			t->notes[i].volume = pv[1];
			t->notes[i].duration = dur;
		}
		t->num_notes = i;
		t->pos = 0;
		t->ticks_left = 0;
	}
	sl->num_tracks = g;
	fclose(fp);
	return kind == KIND_MUSIC ? 1 : 0;
}

static void silence(struct slot *sl)
{
	int g;

	for (g = 0; g < sl->num_tracks; g++)
		sndhw_note(sl->base + g, sl->tracks[g].wave, 0, 0);
	sl->playing = 0;
	sl->num_tracks = 0;
}

void sound_start(int resnum, int on_done_flag)
{
	char name[16];
	char path[256];
	struct slot *sl;
	int which;

	if (!sound_enabled)
		return;

	sprintf(name, "SOUND.%d", resnum);
	sys_game_path(path, sizeof(path), name);

	/* time spent idle must not be played back as a burst of ticks */
	if (!slots[0].playing && !slots[1].playing)
		(void)sndhw_ticks();

	which = load_sound(path);
	if (which < 0)
		return;
	sl = &slots[which];

	sl->flag_on_done = on_done_flag;
	if (on_done_flag >= 0)
		setflag(on_done_flag, FALSE);
	sl->playing = sl->num_tracks > 0;
}

void sound_stop_effects(void)
{
	silence(&slots[0]);
}

void sound_stop(void)
{
	silence(&slots[0]);
	silence(&slots[1]);
}

static void step_slot(struct slot *sl)
{
	int g;
	int all_done = 1;

	for (g = 0; g < sl->num_tracks; g++) {
		struct generator_track *t = &sl->tracks[g];

		if (t->pos >= t->num_notes)
			continue;

		if (t->ticks_left == 0) {
			struct note *n = &t->notes[t->pos];

			if (n->pitch == 0)
				sndhw_note(sl->base + g, t->wave, 0, 0);
			else
				sndhw_note(sl->base + g, t->wave, n->pitch,
					n->volume);
			t->ticks_left = n->duration;
			t->pos++;
		} else {
			t->ticks_left--;
		}

		all_done = all_done && (t->pos >= t->num_notes) && t->ticks_left == 0;
	}

	if (all_done) {
		int flag = sl->flag_on_done;

		silence(sl);
		sl->flag_on_done = -1;
		if (flag >= 0)
			setflag(flag, TRUE);
	}
}

void sound_service(void)
{
	int ticks, s;

	if (!slots[0].playing && !slots[1].playing)
		return;

	ticks = sndhw_ticks();
	if (ticks > 30)
		ticks = 30;
	for (; ticks > 0; ticks--)
		for (s = 0; s < 2; s++)
			if (slots[s].playing)
				step_slot(&slots[s]);
}

int sound_is_enabled(void)
{
	return sound_enabled;
}

void sound_set_enabled(int on)
{
	sound_enabled = on;
	if (!on)
		sound_stop();
}
