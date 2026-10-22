/*
 * object.h - inventory / room-object system. Platform-independent
 * tracking of "where is object N" plus the OBJECT resource loader.
 * Screen presentation of the inventory screen is gfx_iigs.c's job.
 */
#ifndef GSAGI_OBJECT_H
#define GSAGI_OBJECT_H

#include "agitypes.h"

int   object_load(const char *path);
void  object_unload(void);

void  object_set_location(unsigned int n, int location);
int   object_get_location(unsigned int n);
const char *object_name(unsigned int n);

unsigned int object_count(void);

#endif /* GSAGI_OBJECT_H */
