/*
 * pic.h - decoder for PIC.n vector pictures (format in resfmt.h).
 */
#ifndef GSAGI_PIC_H
#define GSAGI_PIC_H

#include "agitypes.h"
#include "resfmt.h"

/*
 * The three layers a picture draws into. vis and pri hold PIC_W x PIC_H
 * bytes each; ctl holds PIC_CTL_BYTES (2 bits per pixel). The caller
 * owns the memory and clears it before drawing a fresh picture.
 */
struct pic_layers {
	UINT8 *vis;
	UINT8 *pri;
	UINT8 *ctl;
};

/* Draws one picture over whatever the layers already hold. */
int   pic_draw(const UINT8 *data, UINT32 len, const struct pic_layers *layers);

/* Reads the control value (0-3) at a pixel; off-screen reads as 1. */
UINT8 pic_ctl_get(const UINT8 *ctl, int x, int y);

#endif /* GSAGI_PIC_H */
