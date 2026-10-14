/*
 * object.c - inventory/object location tracking.
 *
 * Extracted from Sarien's src/objects.c (decode_objects/load_objects/
 * object_get_location/object_set_location/object_name). What changed
 * for the IIGS:
 *
 *   - objects[] is a fixed MAX_OBJECTS static array instead of a
 *     calloc'd block of structs that each strdup() their own name.
 *     One static array with inline char[MAX_OBJNAMELEN] names means
 *     one allocation (zero, really -- it's static storage) for the
 *     whole table instead of 1+N heap blocks, which matters on a
 *     machine where the heap can fragment inside a 64K segment.
 *   - AGDS/Amiga OBJECT-format variants are dropped; this targets the
 *     standard 3-byte-padded Sierra OBJECT resource (what the Apple
 *     II/IIGS AGI titles themselves shipped).
 *   - file IO is plain ANSI stdio, which ORCA/C maps to GS/OS.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "agitypes.h"
#include "util.h"
#include "object.h"

#define OBJ_PAD_SIZE 3

static struct {
	int  location;
	char name[MAX_OBJNAMELEN];
} objects[MAX_OBJECTS];

static unsigned int num_objects = 0;

static int decode_objects(UINT8 *mem, UINT32 flen)
{
	unsigned int i, so;
	unsigned int n;

	num_objects = 0;

	if (lohi_getword(mem) > flen)
		decrypt(mem, (int)flen);

	n = lohi_getword(mem) / OBJ_PAD_SIZE;
	if (n > MAX_OBJECTS)
		n = MAX_OBJECTS;
	num_objects = n;

	for (i = 0, so = OBJ_PAD_SIZE; i < num_objects; i++, so += OBJ_PAD_SIZE) {
		UINT32 offset = lohi_getword(mem + so) + OBJ_PAD_SIZE;

		objects[i].location = mem[so + 2];

		if (offset < flen) {
			size_t maxlen = flen - offset;
			if (maxlen > MAX_OBJNAMELEN - 1)
				maxlen = MAX_OBJNAMELEN - 1;
			strncpy(objects[i].name, (char *)mem + offset, maxlen);
			objects[i].name[maxlen] = '\0';
		} else {
			objects[i].name[0] = '\0';
		}
	}

	return err_OK;
}

int object_load(const char *path)
{
	FILE  *fp;
	long   flen;
	UINT8 *mem;
	int    ec;

	num_objects = 0;

	fp = fopen(path, "rb");
	if (fp == NULL)
		return err_BadFileOpen;

	fseek(fp, 0, SEEK_END);
	flen = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	mem = (UINT8 *)malloc((size_t)flen);
	if (mem == NULL) {
		fclose(fp);
		return err_NotEnoughMemory;
	}

	fread(mem, 1, (size_t)flen, fp);
	fclose(fp);

	ec = decode_objects(mem, (UINT32)flen);
	free(mem);

	return ec;
}

void object_unload(void)
{
	num_objects = 0;
}

void object_set_location(unsigned int n, int location)
{
	if (n >= num_objects)
		return;
	objects[n].location = location;
}

int object_get_location(unsigned int n)
{
	if (n >= num_objects)
		return 0;
	return objects[n].location;
}

const char *object_name(unsigned int n)
{
	if (n >= num_objects)
		return "";
	return objects[n].name;
}

unsigned int object_count(void)
{
	return num_objects;
}
