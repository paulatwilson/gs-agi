# Intergellium

A native Apple IIGS/GS/OS AGI-style adventure game engine, built for
ORCA/C. The text parser, bytecode logic interpreter and inventory
system are a clean-room-adjacent port of the platform-independent
core of [Sarien](https://github.com/cmatsuoka/sarien) (an AGI
interpreter); the graphics, sound and application layers are written
fresh against the Apple IIGS toolbox.

## Layout

```
src/
  agitypes.h      fixed-width types + resource ceilings (65816-sized)
  gamestate.h     struct agi_game, vt_entry, word/object/logic structs
  util.c/.h       byte helpers, var/flag accessors, AGI text decrypt

  vocab.c/.h      dictionary (WORDS.TOK) lookup + sentence tokenizer
  object.c/.h     OBJECT resource loader + inventory location tracking
  logic.c/.h      LOGIC resource loader + the bytecode VM (~183 opcodes)

  gfx_iigs.c/.h   QuickDraw II: SHR screen, parser input box, sprites
  sound_iigs.c/.h Sound Toolset: Ensoniq DOC-driven SFX/music
  keyboard.c/.h   Event Manager polling + line editor
  syshooks.h      the two calls logic.c reaches into main.c through

  main.c          toolbox startup -> main loop -> clean GS/OS shutdown
```

## What's a faithful port vs. what's new vs. what's a stub

**Ported from Sarien** (`src/words.c`, `src/objects.c`, `src/logic.c` +
`src/op_cmd.c` + `src/op_test.c`), with SDL/mouse/menu/debug-console
code stripped and heap-per-word/per-object allocation replaced by
fixed static arrays:

- `vocab_find_word()` -- the WORDS.TOK dictionary search, byte-for-byte
  the same algorithm (it's resource-format logic, not host logic).
- `vocab_parse_input()` -- sentence tokenizing into `game.ego_words[]`.
- `object_load()`/`object_get_location()`/`object_set_location()` --
  OBJECT resource decode and location tracking.
- `logic_load()` (message-block unpacking) and `logic_run()` /
  `test_if_code()` -- the full opcode dispatch table (183 action
  opcodes, 19 test opcodes), argument-count tables regenerated
  programmatically from Sarien's `op_dbg.c` rather than transcribed
  by hand (the hand-transcribed first draft had a silent off-by-one
  past opcode 0x44 -- see the extraction script output/history if you
  want to re-derive it against a different Sarien revision).

**Written fresh:** `gfx_iigs.c`, `sound_iigs.c`, `keyboard.c`,
`main.c` -- there is no SDL equivalent to port here, this is the
actual IIGS toolbox integration.

**Deliberately out of scope / stubbed** (none of these were in the
"extract vocab/logic/object, add graphics/sound/app-shell" brief, and
each is a substantial project in its own right):

- **AGI PICTURE/VIEW resource decoding.** Sierra's vector-picture fill
  language and RLE cel format aren't decoded; `gfx_load_picture()`/
  `gfx_draw_object()` draw placeholders. This is the seam where a real
  decoder plugs in.
- **The motion stepper** (`move.obj`, `follow.ego`, `wander`'s actual
  per-frame movement). The opcodes update `vt_entry` bookkeeping
  correctly; nothing currently advances position frame-to-frame.
- **Menu bar opcodes** (`set.menu`, `enable.item`, ...) and **mouse
  opcodes** are no-ops.
- **Save/restore game** -- stubbed with a "not implemented" message
  box.
- **Sierra's VOL/DIR volume format.** This engine expects each
  resource pre-extracted to its own flat GS/OS file (see below), not
  packed into Sierra's original multi-resource volume files.

## On-disk resource convention

Resources load from a `GAME:` prefixed GS/OS path (see `main.c`):

| File           | Contents                                              |
|----------------|--------------------------------------------------------|
| `GAME:WORDS.TOK` | Sierra dictionary resource, unmodified                |
| `GAME:OBJECT`    | Sierra OBJECT resource, unmodified                    |
| `GAME:LOGIC.n`   | Raw decompressed LOGIC resource #n, unmodified        |
| `GAME:SOUND.n`   | **Not** Sierra's format -- see `sound_iigs.c`'s header for the simple DOC note-list layout this engine expects instead |

A `GAME` prefix needs to exist as a GS/OS pathname (a subdirectory, or
a defined prefix) pointing at wherever you've extracted the original
game's resources with an existing AGI resource-extraction tool.

## Building

This was written without an ORCA/C install or the real Apple IIGS
Toolbox headers in hand, so treat it as a strong first draft, not a
verified build:

- `gfx_iigs.c`/`sound_iigs.c`/`keyboard.c`/`main.c` use toolbox call
  names and argument shapes as documented in the *Apple IIGS Toolbox
  Reference* (QuickDraw II, Sound Tool Set, Event Manager, Tool
  Locator, Memory Manager chapters). Check them against your actual
  `<quickdraw.h>`/`<sound.h>`/`<event.h>`/`<locator.h>`/`<memory.h>` --
  argument order and a few call names are the likeliest mismatches.
- `vocab.c`/`object.c`/`logic.c`/`util.c` are plain ANSI C with no
  toolbox dependency at all and should compile against any C89
  compiler as a sanity check before moving to ORCA/C.

In ORCA Shell:

```
cc src/util.c -o obj/util.o
cc src/vocab.c -o obj/vocab.o
cc src/object.c -o obj/object.o
cc src/logic.c -o obj/logic.o
cc src/gfx_iigs.c -o obj/gfx_iigs.o
cc src/sound_iigs.c -o obj/sound_iigs.o
cc src/keyboard.c -o obj/keyboard.o
cc src/main.c -o obj/main.o
link obj/util.o obj/vocab.o obj/object.o obj/logic.o obj/gfx_iigs.o \
     obj/sound_iigs.o obj/keyboard.o obj/main.o -o Intergellium -t S16
```

(flag names/`-t S16` output-type syntax per your ORCA/C version's
`link` reference -- adjust as needed.)

## Next steps, roughly in the order they unblock a playable game

1. A PICTURE/VIEW resource decoder feeding `gfx_iigs.c`'s seams.
2. A motion stepper (`motion.c`-equivalent) driven once per
   `sys_idle_pump()`/main-loop tick.
3. A one-time Sierra `.SND` -> DOC note-list converter for
   `sound_iigs.c`.
4. Save/restore game, backed by a GS/OS file instead of the message
   box currently standing in for it.
