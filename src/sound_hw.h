/*
 * sound_hw.h - the per-platform half of the sound system. sound.c owns
 * the SOUND.n loader and note sequencer; each backend implements
 * these to make a generator actually sound.
 */
#ifndef GSAGI_SOUND_HW_H
#define GSAGI_SOUND_HW_H

int  sndhw_init(void);
void sndhw_shutdown(void);

/* Sets generator gen to play pitch at volume; volume 0 silences it. */
void sndhw_note(int gen, int wave_table_id, int pitch, int volume);

/* Returns how many 1/60 s ticks have passed since the last call. */
int  sndhw_ticks(void);

#endif /* GSAGI_SOUND_HW_H */
