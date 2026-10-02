# Standalone core build

This directory holds the Free Heroes II core on its own, so it can be compiled
without a multicore checkout. It produces the static library that the multicore
frontend later links into `core_87000000`.

## Layout

```
libretro.cpp            the core itself: input mapping, audio pump, libco wiring
link.T                 linker version script (libretro_* symbols only)
libco_sf2000.c          coroutine switching for the console (setjmp/longjmp)
libco_ucontext.c        coroutine switching for host builds
Makefile                both platform targets
test_fheroes2.c         host test harness, see below
sf2000_dirent.h         vendored firmware dirent declarations, see "deviations"
sdl/                    SDL 1.2.15 with the video backend replaced
src/                    the Free Heroes II engine, version 0.5 r1898
```

## Building

Host build, produces a loadable `.so` for testing on a PC:

```sh
make platform=unix
```

Console build, produces the static library for MIPS:

```sh
make platform=sf2000
```

Switching between the two requires `make clean` first. Both write `.o` files
into the same paths, and a MIPS object will not link into a host binary.

For the GB300 variant add `FROGGY_TYPE=GB300V2`:

```sh
make platform=sf2000 FROGGY_TYPE=GB300V2
```

The MIPS toolchain path is hardcoded in the `sf2000` branch. Point `MIPS=` at
your own if it differs:

```sh
make platform=sf2000 MIPS=/path/to/mipsel-mti-elf-
```

## Producing a runnable core binary

This tree alone gives you a static library. The final `core_87000000` needs the
multicore frontend, which supplies the firmware runtime — the OSD region
writer, the input hotkeys, the pause menu, the filesystem and the `__core_entry__`
entry point. From a multicore checkout:

```sh
make FROGGY_TYPE=GB300V2 CONSOLE=fheroes2 CORE=cores/fheroes2
make FROGGY_TYPE=SF2000  CONSOLE=fheroes2 CORE=cores/fheroes2
```

Get the frontend from [Trademarked69/sf2000_multicore][mc], which carries
firmware support for both devices.

[mc]: https://github.com/Trademarked69/sf2000_multicore

## Running the host test

```sh
make platform=unix
gcc -m64 -O2 -o test_fheroes2 test_fheroes2.c -ldl -I.
./test_fheroes2 ./fheroes2_libretro.so /path/to/DATA/HEROES2.AGG
```

It `dlopen`s the library, runs the game, drives a few button presses and writes
two PPM screenshots next to the working directory. Expected output ends with a
non-black pixel ratio around 91%.

`test_fheroes2.c` loads the game but needs `fheroes2.cfg` beside the data
directory; without it the engine takes its non-QVGA code path.

## Deviations from the upstream multicore tree

Two files differ from what the multicore build used:

**`sf2000_dirent.h`** replaces `#include "dirent.h"`. Upstream, that header came
from the frontend at `../../include`, which declares the firmware's
`opendir`/`readdir`/`closedir` triplet. Vendoring it removes the dependency on a
multicore checkout. The filename is deliberately *not* `dirent.h`:
`src/fheroes2/system` sits on the include path, so a plain `dirent.h` there
would shadow the system header on host builds and break them quietly.

**`Makefile`** only adds `EXTRA_INCDIRS = -I../../include` when
`../../include/deimos.h` actually exists, so the multicore include path is
picked up when present and skipped when this tree is standalone.

Neither changes the produced code: the 233 objects in `fheroes2_libretro_sf2000.a`
are byte-identical to those the multicore build produces. Verified by extracting
and hashing every member from both archives.

## What the frontend still has to provide

Linking `core_87000000` needs these from the frontend, all declared in the
firmware headers:

```
opendir   readdir   closedir     filesystem
state_save   state_load           save states
retro_video_refresh_cb            framebuffer
retro_input_state_cb              buttons
__core_entry__                    entry point
```

## Game data

Not part of this tree, and not redistributable here. `HEROES2.AGG` must be
supplied by the user; the freely redistributable H2DEMO demo works too. See the
project README.