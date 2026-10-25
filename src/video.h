/*
 * video.h - the per-platform half of the screen. gfx.c renders
 * everything into a 320x200 8-bit framebuffer with a 16-colour
 * palette; a video backend (iigs/video_iigs.c, sdl/video_sdl.c) only
 * has to put that buffer on the display.
 */
#ifndef GSAGI_VIDEO_H
#define GSAGI_VIDEO_H

#include "agitypes.h"

int  video_init(void);
void video_shutdown(void);

/*
 * Returns TRUE when it is time to show a new frame (about 60 times a
 * second at most). Backends also use this call to keep the host
 * responsive, e.g. to notice a window-close request, which they report
 * by setting game.quit_prog_now.
 */
int  video_frame_due(void);

/*
 * Shows fb (GFX_WIDTH x GFX_HEIGHT, one palette index per byte) using
 * palette (16 entries of 12-bit 0x0RGB, the IIgs native format).
 * xoff shifts the picture sideways in pixels, for shake.screen.
 */
void video_present(const UINT8 *fb, const UINT16 *palette, int xoff);

/* Time in 1/60 s ticks since startup. */
UINT32 video_ticks(void);

/* Sleeps for roughly one frame. */
void video_wait_frame(void);

#endif /* GSAGI_VIDEO_H */
