/*
 * motion.h - the per-cycle object stepper: walking, cel cycling, edge
 * and obstacle detection. Mirrors what AGI's update_objects() does,
 * for this engine's centre/baseline-anchored 320x200 objects.
 */
#ifndef GSAGI_MOTION_H
#define GSAGI_MOTION_H

/* The centre of a walker may reach these columns; past them is an edge. */
#define MOTION_EDGE_LEFT   6
#define MOTION_EDGE_RIGHT  313

/* Last row a baseline can sit on (the input box starts below it). */
#define MOTION_BOTTOM      183

/* Runs one game cycle's worth of movement and animation. */
void motion_update(void);

#endif /* GSAGI_MOTION_H */
