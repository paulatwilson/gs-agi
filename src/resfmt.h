/*
 * resfmt.h - byte layouts of this engine's own resource files. Shared
 * by the engine's decoders (pic.c, ...) and the host-side authoring
 * tools in tools/, so an encoder and its decoder cannot drift apart.
 *
 * All multi-byte values are little-endian. Never fread() a struct.
 */
#ifndef GSAGI_RESFMT_H
#define GSAGI_RESFMT_H

#define PIC_W 320
#define PIC_H 200

/*
 * PIC.n - a vector picture: a stream of drawing commands in real
 * 320x200 screen coordinates. Every drawing command writes to up to
 * three layers at once, as selected by the PIC_VIS / PIC_PRI / PIC_CTL
 * commands (PIC_OFF leaves that layer alone):
 *
 *   visual    one 4-bit palette index per pixel
 *   priority  one byte per pixel: the baseline row of the thing drawn
 *             there. A walker whose feet are above (smaller y than)
 *             that row is hidden behind it. 0 means "nothing here".
 *   control   2 bits per pixel: 0 free, 1 blocks walking, 2 trigger
 *             (sets flag ego_touched_p2 while the walker's feet are on
 *             it, which scripts use for exits), 3 water (sets flag
 *             ego_water)
 *
 * Commands (arguments follow the command byte):
 *   PIC_VIS  v            visual colour 0-15, or PIC_OFF
 *   PIC_PRI  v            priority value 0-199, or PIC_OFF
 *   PIC_CTL  v            control value 0-3, or PIC_OFF
 *   PIC_LINE n, n points  connected line segments
 *   PIC_LOOP n, n points  as PIC_LINE, closed back to the first point
 *   PIC_POLY n, n points  filled polygon
 *   PIC_ELLIPSE cx cy rx ry   filled ellipse (cx cy int16, rx ry byte)
 *   PIC_FLOOD x y         flood fill on the visual layer only
 *   PIC_DOT x y           single pixel
 *   PIC_END               end of picture
 * A point is x (int16) then y (int16).
 */
#define PIC_OFF      0xFF

#define PIC_END      0x00
#define PIC_VIS      0x01
#define PIC_PRI      0x02
#define PIC_CTL      0x03
#define PIC_LINE     0x10
#define PIC_LOOP     0x11
#define PIC_POLY     0x12
#define PIC_ELLIPSE  0x13
#define PIC_FLOOD    0x14
#define PIC_DOT      0x15

#define PIC_MAX_POINTS 32

#define PIC_CTL_BYTES  ((PIC_W / 4) * PIC_H)	/* 2 bits per pixel */

/*
 * VIEW.n - animated-object art: loops (directions) of cels (frames).
 * Pixels are one palette index per byte; VIEW_TRANSPARENT is see-through.
 *
 *   header (8 bytes)
 *     0      flags: VIEW_SCALED = scale with depth (see below)
 *     1      number of loops
 *     2-3    int16 scale_top_y     row where scale is scale_min percent
 *     4-5    int16 scale_bottom_y  row where scale is 100 percent
 *     6      scale_min percent (1-100)
 *     7      reserved
 *   then UINT16 offsets (from the start of the file), one per loop
 *   loop: byte cel count, then UINT16 offsets (from file start) per cel
 *   cel:  byte width, byte height, byte anchor_x, byte anchor_y,
 *         then width*height pixel bytes, row by row from the top.
 *         The anchor is the pixel that sits on the object's position
 *         (x_pos, y_pos): its horizontal centre and its baseline.
 *
 * A VIEW_SCALED view is resampled at draw time: full size at
 * scale_bottom_y and down to scale_min percent at scale_top_y, which is
 * the perspective shrink toward the horizon.
 */
#define VIEW_SCALED       0x01
#define VIEW_TRANSPARENT  0xFF
#define VIEW_HEADER_SIZE  8

#endif /* GSAGI_RESFMT_H */
