# GS/AGI

GS/AGI is a native Apple IIgs GS/OS AGI-style adventure game engine,
built for ORCA/C. It is a standalone engine: games supply their own resources and are built against it. The
text parser, bytecode logic interpreter and inventory
system are a clean-room-adjacent port of the platform-independent
core of [Sarien](https://github.com/cmatsuoka/sarien) (an AGI
interpreter); the graphics, sound and application layers are written
fresh against the Apple IIGS toolbox.

**This targets real, native 320x200 Super Hi-Res -- not Sierra's
original double-wide-pixel AGI graphics stretched to fit.** Sierra's
AGI PICTURE/VIEW resources are authored in a 160x168 logic space and
displayed as doubled EGA pixels; running those resources here would
just be pixel-doubling in a different costume. This engine doesn't
read or play original Sierra AGI game resources -- it borrows AGI's
bytecode *mechanics* (the parser/logic-VM/object-location model) for
original games, with its own native-resolution art pipeline in
`tools/`. Every coordinate in `logic.c`/
`gamestate.h` is a real screen pixel, 1:1, no scale factor anywhere.

## Layout

```
src/
  agitypes.h      fixed-width types + resource ceilings (65816-sized)
  gamestate.h     struct agi_game, vt_entry, word/object/logic structs
  resfmt.h        PIC and VIEW formats (shared with tools/)
  opcodes.h       AGI opcode table (shared with tools/agic.c)
  util.c/.h       byte helpers, var/flag accessors, AGI text decrypt

  vocab.c/.h      dictionary (WORDS.TOK) lookup + sentence tokeniser
  object.c/.h     OBJECT resource loader + inventory location tracking
  logic.c/.h      LOGIC resource loader + the bytecode VM
  pic.c/.h        PIC decoder: visual, priority and control layers
  view.c/.h       VIEW loader, cel lookup, perspective scaling
  motion.c/.h     per-cycle stepper: walking, cel cycling, collisions
  gfx.c/.h        renderer: picture + objects, text, boxes (portable)
  sound.c, audio.h  SOUND.n loader and note sequencer (portable)
  keyboard.c/.h   line editor and set.key() matching (portable half)
  video.h         backend seam: present a 320x200 frame + palette
  sound_hw.h      backend seam: set a generator's note
  syshooks.h      seam from core back to main.c (paths, rooms, idle, ticks)

  main.c          startup -> real-time game loop -> shutdown

  sdl/            host backend: video_sdl.c, keyboard_sdl.c, sound_sdl.c
  iigs/           Apple IIgs backend: video_iigs.c, keyboard_iigs.c,
                  sound_iigs.c (kbd_init/poll/direction live here)

tools/            picc, viewc, sndc, agic, picview (host-side authoring)
game-src/demo/    the demo game's sources
```

Everything outside `src/sdl/` and `src/iigs/` is portable C and must not
include SDL or toolbox headers.

## What's a faithful port vs. what's new vs. what's a stub

**Ported from Sarien**: the WORDS.TOK dictionary search and sentence
tokeniser (`vocab.c`), OBJECT loading and location tracking
(`object.c`), and the logic VM (`logic.c`: 183 action opcodes, 19 test
opcodes). SDL/mouse/menu/debug-console code was stripped and
heap-per-word/per-object allocation replaced by fixed static arrays.

**Written fresh:** the native 320x200 PIC and VIEW formats and their
decoders, the renderer, the motion stepper, the sound sequencer, both
platform backends and `main.c`.

**Not implemented:**

- Save/restore game: a "not implemented" message box stands in.
- `wander` and `follow.ego` set bookkeeping but do not move objects.
- Menu and mouse opcodes are no-ops.
- IIgs `shake.screen` is a no-op, and IIgs keys are tap-to-walk.
- Sierra's VOL/DIR volume format, and Sierra resources in general.

## On-disk resource convention

The engine ships no game data. Each resource is a flat file in a game
directory: `game/` on the host (or the directory given as the first
argument), the `GAME:` prefix on the IIgs. All paths are built by
`sys_game_path()` in `main.c`.

| File             | Contents |
|------------------|----------|
| `WORDS.TOK`      | Dictionary, Sierra's byte format (plain word-to-id data) |
| `OBJECT`         | Object names and start locations, same reasoning |
| `LOGIC.n`        | Compiled logic #n, built by `tools/agic` with 320x200 coordinates throughout, not a Sierra game's LOGIC |
| `PIC.n`          | Native picture (`tools/picc`), see `resfmt.h` |
| `VIEW.n`         | Native view (`tools/viewc`), see `resfmt.h` |
| `SOUND.n`        | Note list (`tools/sndc`), see `audio.h` |

## Building

### Host (SDL2), macOS first

```
cmake -S . -B build && cmake --build build   # engine, tools and the demo (into game/)
./build/gsagi game/                          # play the demo
```

Arrow keys walk, typing fills the input line at the bottom, Enter
parses it. The game runs in real time: every `V_time_delay * 2` ticks
of 1/60 s it runs the logic cycle and then steps the objects.

Headless runs use the `GSAGI_SCRIPT` test aid, which drives the
keyboard and can save frames:

```
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
GSAGI_SCRIPT="500:+right;1500:-right;1600:text=look tree;1700:enter;2500:dump=frame.ppm;3000:quit" \
./build/gsagi game/
```

`build/picview out.ppm game/PIC.1` renders a picture alone.

### Apple IIgs, in ORCA Shell

This has never been built against a real ORCA/C install, so treat it as
a first draft. Toolbox call names and shapes in `src/iigs/` and the
`__ORCAC__` blocks of `main.c` are unverified against your
`<event.h>`/`<sound.h>`/`<locator.h>`/`<memory.h>`. The portable core
must stay strict C89 and 16-bit-int safe
(`cc -std=c89 -pedantic -fsyntax-only` on the core files stays clean).

```
cc src/util.c -o obj/util.o
cc src/vocab.c -o obj/vocab.o
cc src/object.c -o obj/object.o
cc src/logic.c -o obj/logic.o
cc src/pic.c -o obj/pic.o
cc src/view.c -o obj/view.o
cc src/motion.c -o obj/motion.o
cc src/gfx.c -o obj/gfx.o
cc src/sound.c -o obj/sound.o
cc src/keyboard.c -o obj/keyboard.o
cc src/iigs/video_iigs.c -o obj/video_iigs.o
cc src/iigs/keyboard_iigs.c -o obj/keyboard_iigs.o
cc src/iigs/sound_iigs.c -o obj/sound_iigs.o
cc src/main.c -o obj/main.o
link obj/util.o obj/vocab.o obj/object.o obj/logic.o obj/pic.o obj/view.o \
     obj/motion.o obj/gfx.o obj/sound.o obj/keyboard.o obj/video_iigs.o \
     obj/keyboard_iigs.o obj/sound_iigs.o obj/main.o -o GSAGI -t S16
```

(Flag names and the `-t S16` syntax follow your ORCA/C version's `link`
reference.)

## Licence

MIT. See [LICENCE](LICENSE).
