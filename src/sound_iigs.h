/*
 * sound_iigs.h - Apple IIGS Sound Toolset backend, driving the
 * Ensoniq DOC chip through the standard Sound Manager (not direct
 * $C0Ex port twiddling, so it behaves under GS/OS and coexists with
 * the rest of the toolbox).
 *
 * logic.c only knows "play sound resource N, and set flag F when it
 * finishes" / "stop whatever is playing" -- it has no idea an
 * Ensoniq DOC or a wavetable even exists.
 */
#ifndef GSAGI_SOUND_IIGS_H
#define GSAGI_SOUND_IIGS_H

#include "agitypes.h"

int  sound_init(void);
void sound_shutdown(void);

/*
 * Starts AGI sound resource n playing (SFX or a music track encoded
 * as DOC generator notes); flag_on_done is set TRUE the moment
 * playback ends, same contract as AGI's sound() opcode.
 */
void sound_start(int resnum, int flag_on_done);
void sound_stop(void);

/* Services the DOC oscillators; call once per main-loop tick. */
void sound_service(void);

int  sound_is_enabled(void);
void sound_set_enabled(int on);

#endif /* GSAGI_SOUND_IIGS_H */
