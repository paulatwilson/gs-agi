/*
 * agitypes.h - portable fixed-width types and limits.
 *
 * ORCA/C on the 65816: int/short = 16-bit, long = 32-bit, char = 8-bit.
 * These typedefs match that model directly (no 32-bit-host assumptions
 * like the original Sarien build carried).
 */
#ifndef GSAGI_AGITYPES_H
#define GSAGI_AGITYPES_H

typedef unsigned char   UINT8;
typedef signed char     SINT8;
typedef unsigned short  UINT16;
typedef signed short    SINT16;
typedef unsigned long   UINT32;
typedef signed long     SINT32;

typedef int BOOL_T;
#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*
 * Resource ceilings. These match Sarien's own (MAX_DIRS/MAX_VIEWTABLE
 * = 256; Sarien's comment on MAX_VIEWTABLE notes "KQ3 uses o255!").
 * They look large, but every one of these is a small slot descriptor,
 * not the resource bytes themselves -- a logic/view/sound's actual
 * data is malloc'd only once it's loaded, so the static cost of these
 * tables is a few KB total, not "256 resources' worth of memory".
 * Shrinking them is tempting but risks indexing past the array on a
 * real game script that references a high object/view-table number,
 * which is worse than the memory saved.
 */
#define MAX_DIRS        256  /* logic/pic/view/sound resource slots  */
#define MAX_OBJECTS     256  /* inventory objects                    */
#define MAX_VARS        256  /* AGI variables (always 256, 1 byte)   */
#define MAX_FLAGS       32   /* 256 1-bit flags, packed              */
#define MAX_VIEWTABLE   256  /* animated screen objects              */
#define MAX_WORDS       20   /* words per typed sentence             */
#define MAX_WORD_LEN    16   /* chars per dictionary word             */
#define MAX_STRINGS     24
#define MAX_STRINGLEN   40
#define MAX_OBJNAMELEN  32
#define MAX_INPUTLEN    40

#define EGO_OWNED       0xff

enum agi_error {
	err_OK = 0,
	err_BadFileOpen,
	err_NotEnoughMemory,
	err_BadResource,
	err_Unknown
};

enum agi_restype {
	rLOGIC = 1,
	rSOUND,
	rVIEW,
	rPICTURE
};

#endif /* GSAGI_AGITYPES_H */
