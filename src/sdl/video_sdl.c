/*
 * video_sdl.c - SDL2 video backend for host builds (macOS, Linux,
 * Windows). Converts gfx.c's 8-bit framebuffer to a streaming texture.
 *
 * SHR pixels are not square (the IIgs shows 320x200 on a 4:3 display),
 * so the texture is stretched to 320x240 logical units and SDL scales
 * that to the window.
 */
#include <stdio.h>

#include <SDL.h>

#include "../agitypes.h"
#include "../gamestate.h"
#include "../gfx.h"
#include "../video.h"

#define DISPLAY_HEIGHT 240	/* 4:3 display for the 320x200 SHR image */
#define WINDOW_SCALE   3
#define FRAME_MS       16

static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *texture;
static Uint32        last_present = 0;

int video_init(void)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return err_Unknown;
	}

	window = SDL_CreateWindow("GS/AGI",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		GFX_WIDTH * WINDOW_SCALE, DISPLAY_HEIGHT * WINDOW_SCALE,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (window == NULL)
		goto fail;

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
	if (renderer == NULL)
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	if (renderer == NULL)
		goto fail;

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	SDL_RenderSetLogicalSize(renderer, GFX_WIDTH, DISPLAY_HEIGHT);

	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, GFX_WIDTH, GFX_HEIGHT);
	if (texture == NULL)
		goto fail;

	return err_OK;

fail:
	fprintf(stderr, "SDL video: %s\n", SDL_GetError());
	video_shutdown();
	return err_Unknown;
}

void video_shutdown(void)
{
	if (texture)  SDL_DestroyTexture(texture);
	if (renderer) SDL_DestroyRenderer(renderer);
	if (window)   SDL_DestroyWindow(window);
	texture = NULL;
	renderer = NULL;
	window = NULL;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

int video_frame_due(void)
{
	Uint32 now;

	/* keep the window responsive and notice a close request even
	 * while nothing is polling the keyboard */
	SDL_PumpEvents();
	if (SDL_HasEvent(SDL_QUIT))
		game.quit_prog_now = TRUE;

	now = SDL_GetTicks();
	return now - last_present >= FRAME_MS;
}

void video_present(const UINT8 *fb, const UINT16 *palette, int xoff)
{
	Uint32 argb[16];
	void *pixels;
	int pitch, x, y;
	SDL_Rect dst;

	if (texture == NULL)
		return;

	for (x = 0; x < 16; x++)
		argb[x] = 0xFF000000u |
			((Uint32)((palette[x] >> 8) & 15) * 17 << 16) |
			((Uint32)((palette[x] >> 4) & 15) * 17 << 8) |
			((Uint32)(palette[x] & 15) * 17);

	if (SDL_LockTexture(texture, NULL, &pixels, &pitch) == 0) {
		for (y = 0; y < GFX_HEIGHT; y++) {
			Uint32 *row = (Uint32 *)((Uint8 *)pixels + y * pitch);
			const UINT8 *src = fb + (UINT32)y * GFX_WIDTH;

			for (x = 0; x < GFX_WIDTH; x++)
				row[x] = argb[src[x] & 15];
		}
		SDL_UnlockTexture(texture);
	}

	dst.x = xoff;
	dst.y = 0;
	dst.w = GFX_WIDTH;
	dst.h = DISPLAY_HEIGHT;

	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, &dst);
	SDL_RenderPresent(renderer);
	last_present = SDL_GetTicks();
}

UINT32 video_ticks(void)
{
	Uint32 ms = SDL_GetTicks();

	return (UINT32)(ms / 50 * 3 + (ms % 50) * 3 / 50);	/* ms * 60 / 1000 */
}

void video_wait_frame(void)
{
	SDL_Delay(FRAME_MS);
}
