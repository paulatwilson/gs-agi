/*
 * sound_sdl.c - SDL2 audio backend for host builds. Each generator is a
 * small oscillator mixed in the audio callback; pitch is a MIDI note
 * number (69 = A440). wave picks the waveform: 0 square, 1 triangle,
 * 2 sawtooth, 3 noise, 4 sine. Volume changes glide, so notes start and
 * stop without clicks and soft waves swell in.
 *
 * If no audio device can be opened the engine still runs, silently.
 */
#include <math.h>

#include <SDL.h>

#include "../agitypes.h"
#include "../sound_hw.h"

#define MAX_VOICES  8
#define SAMPLE_RATE 44100
#define MASTER      0.32

struct voice {
	int    wave;
	double freq;
	double target;		/* volume to glide to, 0..1 */
	double cur;		/* volume now */
	double phase;		/* 0..1 */
	double lp;		/* noise filter state */
};

static SDL_AudioDeviceID device = 0;
static struct voice voices[MAX_VOICES];
static Uint32 last_tick_ms = 0;
static Uint32 tick_remainder = 0;
static Uint32 rng = 0x1234567u;

static double wave_scale(int wave)
{
	switch (wave) {
	case 0:  return 0.22;
	case 1:  return 0.66;
	case 2:  return 0.31;
	case 3:  return 1.6;
	default: return 0.88;
	}
}

/* soft waves swell slowly, hard ones react fast */
static double glide(int wave)
{
	return (wave == 1 || wave == 4) ? 0.00012 : 0.0025;
}

static double osc(struct voice *v)
{
	switch (v->wave) {
	case 0:  return v->phase < 0.5 ? 1.0 : -1.0;
	case 1:  return v->phase < 0.5 ? 4.0 * v->phase - 1.0 : 3.0 - 4.0 * v->phase;
	case 2:  return 2.0 * v->phase - 1.0;
	case 3: {
		double n, k = 6.2831853 * v->freq * 3.0 / SAMPLE_RATE;

		rng = rng * 1664525u + 1013904223u;
		n = ((double)(rng >> 8) / 8388608.0) - 1.0;
		if (k > 1.0)
			k = 1.0;
		v->lp += k * (n - v->lp);
		return v->lp * 2.0;
	}
	default: return sin(6.2831853 * v->phase);
	}
}

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
	Sint16 *out = (Sint16 *)stream;
	int frames = len / (int)sizeof(Sint16);
	int i, v;

	(void)userdata;

	for (i = 0; i < frames; i++) {
		double mix = 0.0;

		for (v = 0; v < MAX_VOICES; v++) {
			struct voice *vo = &voices[v];

			vo->cur += (vo->target - vo->cur) * glide(vo->wave);
			if (vo->cur < 0.0001 && vo->target == 0.0)
				continue;
			mix += osc(vo) * vo->cur * wave_scale(vo->wave);
			vo->phase += vo->freq / SAMPLE_RATE;
			if (vo->phase >= 1.0)
				vo->phase -= 1.0;
		}
		mix *= MASTER;
		if (mix > 1.0) mix = 1.0;
		if (mix < -1.0) mix = -1.0;
		out[i] = (Sint16)(mix * 32767.0);
	}
}

int sndhw_init(void)
{
	SDL_AudioSpec want, have;

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
		return err_OK;	/* run silent */

	SDL_zero(want);
	want.freq = SAMPLE_RATE;
	want.format = AUDIO_S16SYS;
	want.channels = 1;
	want.samples = 1024;
	want.callback = audio_callback;

	device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (device != 0)
		SDL_PauseAudioDevice(device, 0);

	last_tick_ms = SDL_GetTicks();
	return err_OK;
}

void sndhw_shutdown(void)
{
	if (device != 0)
		SDL_CloseAudioDevice(device);
	device = 0;
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void sndhw_note(int gen, int wave_table_id, int pitch, int volume)
{
	struct voice *vo;

	if (gen < 0 || gen >= MAX_VOICES)
		return;
	vo = &voices[gen];

	if (device != 0)
		SDL_LockAudioDevice(device);

	vo->wave = wave_table_id;
	if (pitch == 0 || volume == 0) {
		vo->target = 0.0;
	} else {
		vo->freq = 440.0 * pow(2.0, (pitch - 69) / 12.0);
		vo->target = volume / 255.0;
	}

	if (device != 0)
		SDL_UnlockAudioDevice(device);
}

int sndhw_ticks(void)
{
	Uint32 now = SDL_GetTicks();
	Uint32 elapsed = now - last_tick_ms + tick_remainder;
	int ticks = (int)(elapsed * 60 / 1000);

	last_tick_ms = now;
	/* carry the sub-tick milliseconds so 60 Hz stays exact */
	tick_remainder = elapsed - (Uint32)ticks * 1000 / 60;
	return ticks;
}
