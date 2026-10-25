/*
 * video_iigs.c - Super Hi-Res video backend: turns on the 320x200
 * SHR mode, loads palette 0 and converts gfx.c's 8-bit framebuffer to
 * the 4-bit-per-pixel SHR layout (160 bytes per line, left pixel in
 * the high nibble) at $E1:2000.
 *
 * Written without ORCA/C or the real Apple IIGS headers in hand: check
 * the memory addresses and the <misctool.h> tick call against your
 * install before trusting it, like the rest of src/iigs/.
 */
#include <types.h>
#include <misctool.h>

#include "../agitypes.h"
#include "../gamestate.h"
#include "../gfx.h"
#include "../video.h"

#define SHR_PIXELS   ((volatile UINT8 *)0xE12000L)
#define SHR_SCB      ((volatile UINT8 *)0xE19D00L)
#define SHR_PALETTE  ((volatile UINT16 *)0xE19E00L)
#define NEWVIDEO     ((volatile UINT8 *)0xE0C029L)

#define FRAME_TICKS  3		/* GetTick() runs at 60 Hz: show 20 fps */

static UINT8   old_newvideo;
static UINT32  last_present = 0;

int video_init(void)
{
	int y;

	old_newvideo = *NEWVIDEO;
	*NEWVIDEO = 0xC1;		/* SHR on, linearise memory, shadow on */

	for (y = 0; y < GFX_HEIGHT; y++)
		SHR_SCB[y] = 0;		/* every line: 320 mode, palette 0 */

	return err_OK;
}

void video_shutdown(void)
{
	*NEWVIDEO = old_newvideo;
}

int video_frame_due(void)
{
	return GetTick() - last_present >= FRAME_TICKS;
}

void video_present(const UINT8 *fb, const UINT16 *palette, int xoff)
{
	int x, y;

	for (x = 0; x < 16; x++)
		SHR_PALETTE[x] = palette[x];

	for (y = 0; y < GFX_HEIGHT; y++) {
		const UINT8 *src = fb + (UINT32)y * GFX_WIDTH;
		volatile UINT8 *dst = SHR_PIXELS + (UINT32)y * 160;

		for (x = 0; x < GFX_WIDTH; x += 2)
			dst[x >> 1] = (UINT8)((src[x] << 4) | (src[x + 1] & 15));
	}

	(void)xoff;	/* no cheap horizontal shift here; shake is a no-op */
	last_present = GetTick();
}

UINT32 video_ticks(void)
{
	return GetTick();
}

void video_wait_frame(void)
{
	UINT32 start = GetTick();

	while (GetTick() - start < 1)
		;
}
