/*
 * sound_iigs.c - Sound Toolset backend driving the Ensoniq DOC chip
 * for AGI's sound() opcode: short SFX stings and background-music
 * note sequences.
 *
 * SOUND RESOURCE FORMAT: Sierra's PC/PCjr AGI sound resources encode
 * three-voice square-wave tone sequences, tuned for that hardware --
 * not what you want to hand the DOC's wavetable oscillators. Rather
 * than silently mis-decode the original format, this module expects
 * sound resources pre-converted to a flat note-list the DOC can play
 * directly:
 *
 *   UINT16 generator_count
 *   repeated per generator: { UINT8 wave_table_id, UINT16 note_count }
 *   repeated per note: { UINT8 pitch, UINT8 volume, UINT16 duration_ticks }
 *   (pitch 0 == rest)
 *
 * See tools/ (not included here) for a one-time converter from a
 * Sierra .SND resource to this layout.
 *
 * Toolbox call names below are the Sound Tool Set entry points as
 * documented in the Apple IIGS Toolbox Reference, Vol. 2 -- written
 * without the real ORCA/C headers at hand, so check these against
 * <sound.h> from your toolset before trusting the link to go clean.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <types.h>
#include <sound.h>
#include <locator.h>

#include "agitypes.h"
#include "util.h"
#include "gamestate.h"
#include "sound_iigs.h"

#define MAX_GENERATORS 8
#define MAX_NOTES      64

struct note {
	UINT8  pitch;
	UINT8  volume;
	UINT16 duration;
};

struct generator_track {
	UINT8       wave_table_id;
	int         num_notes;
	struct note notes[MAX_NOTES];
	int         pos;
	UINT16      ticks_left;
};

static struct generator_track tracks[MAX_GENERATORS];
static int  num_tracks = 0;
static int  playing = 0;
static int  flag_on_done = -1;
static int  sound_enabled = 1;

int sound_init(void)
{
	SoundToolStartUp();
	SetSoundVolume(192);	/* 0-255, leaves headroom for SFX+music */
	return err_OK;
}

void sound_shutdown(void)
{
	sound_stop();
	SoundToolShutDown();
}

static void load_track_from_file(const char *path)
{
	FILE *fp = fopen(path, "rb");
	UINT16 gen_count;
	int g;

	num_tracks = 0;
	if (fp == NULL)
		return;

	fread(&gen_count, sizeof(UINT16), 1, fp);
	if (gen_count > MAX_GENERATORS)
		gen_count = MAX_GENERATORS;

	for (g = 0; g < gen_count; g++) {
		UINT16 note_count;
		int i;

		fread(&tracks[g].wave_table_id, 1, 1, fp);
		fread(&note_count, sizeof(UINT16), 1, fp);
		if (note_count > MAX_NOTES)
			note_count = MAX_NOTES;

		for (i = 0; i < note_count; i++) {
			UINT8 pv[2];
			UINT16 dur;
			fread(pv, 1, 2, fp);
			fread(&dur, sizeof(UINT16), 1, fp);
			tracks[g].notes[i].pitch = pv[0];
			tracks[g].notes[i].volume = pv[1];
			tracks[g].notes[i].duration = dur;
		}
		tracks[g].num_notes = i;
		tracks[g].pos = 0;
		tracks[g].ticks_left = 0;
	}

	num_tracks = g;
	fclose(fp);
}

void sound_start(int resnum, int on_done_flag)
{
	char path[32];

	if (!sound_enabled)
		return;

	sprintf(path, "SOUND.%d", resnum);
	load_track_from_file(path);

	flag_on_done = on_done_flag;
	if (flag_on_done >= 0)
		setflag(flag_on_done, FALSE);

	playing = num_tracks > 0;
}

void sound_stop(void)
{
	int g;

	for (g = 0; g < num_tracks; g++)
		DOCWrite((Word)(g * 8), 0);	/* silence each generator */

	playing = 0;
	num_tracks = 0;
}

void sound_service(void)
{
	int g;
	int all_done = 1;

	if (!playing)
		return;

	for (g = 0; g < num_tracks; g++) {
		struct generator_track *t = &tracks[g];

		if (t->pos >= t->num_notes)
			continue;

		if (t->ticks_left == 0) {
			struct note *n = &t->notes[t->pos];

			if (n->pitch == 0) {
				DOCWrite((Word)(g * 8), 0);	/* rest: volume 0 */
			} else {
				DOCWrite((Word)(g * 8 + 1), n->pitch);
				DOCWrite((Word)(g * 8), n->volume);
			}
			t->ticks_left = n->duration;
			t->pos++;
		} else {
			t->ticks_left--;
		}

		all_done = all_done && (t->pos >= t->num_notes);
	}

	if (all_done) {
		playing = 0;
		if (flag_on_done >= 0) {
			setflag(flag_on_done, TRUE);
			flag_on_done = -1;
		}
	}
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
