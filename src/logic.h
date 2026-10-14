/*
 * logic.h - the AGI bytecode logic interpreter: resource loading
 * (decode_logic's message-block unpacking) plus the opcode VM
 * (run_logic/test_if_code). Platform-independent: every opcode that
 * touches the screen, sound, or keyboard calls through gfx_iigs.h /
 * sound_iigs.h / keyboard.h, never the toolbox directly.
 */
#ifndef INTERGELLIUM_LOGIC_H
#define INTERGELLIUM_LOGIC_H

#include "agitypes.h"

/*
 * Loads logic resource n from a flat file containing the raw,
 * already-decompressed Sierra LOGIC resource bytes (see README for
 * the on-disk layout convention this engine expects -- it does not
 * parse Sierra's VOL/DIR volume format itself).
 */
int  logic_load(int n, const char *path);
void logic_unload(int n);
void logic_unload_all(void);

/*
 * Runs logic resource n from its current/scanned IP until a return
 * (0x00) opcode or end of script. Returns 1 normally, 0 if a
 * new.room() happened partway through (same contract as Sarien's
 * run_logic, which new.room()'s callers use to re-dispatch logic 0).
 */
int  logic_run(int n);

/*
 * Call once per game frame: runs logic 0 (and whatever it calls),
 * re-dispatching it across new.room() the way Sarien's
 * interpret_cycle() did, then resets the per-cycle parser/controller
 * state ready for the next frame.
 */
void logic_interpret_cycle(void);

#endif /* INTERGELLIUM_LOGIC_H */
