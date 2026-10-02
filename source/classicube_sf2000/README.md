# ClassiCube for Data Frog SF2000 / GB300

A port of [ClassiCube](https://github.com/ClassiCube/ClassiCube) (Minecraft Classic
0.0.30a) to the SF2000 and GB300 handheld consoles, running as a **libretro core**
inside the [Multicore](https://gitlab.com/eden-exe/fwos) / FrogUI frontend.

Upstream ClassiCube is BSD-3 licensed. This port keeps that license; see
`upstream/license.txt`.

## What this port does

- Renders at **320x240 RGB565**, the native panel resolution of both consoles.
- Uses ClassiCube's **fixed-point software rasterizer** (`Graphics_SoftFP`),
  which does its math in 16.16 integer arithmetic rather than floats. The
  SF2000's MIPS32r2 CPU has no FPU, so float math would be extremely slow.
- Starts straight into a **singleplayer** world. The stock launcher is a
  mouse-driven web UI and the console has neither a mouse nor networking, so it
  is disabled (`CC_DISABLE_LAUNCHER`).
- Persists `options.txt`, world saves and texture packs to the SD card.

## Requirements

| Tool | Path |
|------|------|
| MIPS compiler | `/opt/frog-toolchain/mipsel-mti-elf_stable/bin/mipsel-mti-elf-gcc` |
| MIPS binutils | `/opt/mips32-mti-elf/2019.09-03-2/bin/mips-mti-elf-*` |
| Multicore repo | `/home/Sajnaps/gb300/sf2000_multicore` |

## Building

```bash
./scripts/build.sh native   # x86_64 .so + test_runner (for development)
./scripts/build.sh sf2000   # MIPS32r2 static library
./scripts/build.sh link     # core_87000000 + .sf2k for both consoles
./scripts/build.sh all      # all of the above
```

`link` invokes the Multicore build system with
`CORE=cores/classicube FROGGY_TYPE=SF2000|GB300V2` and produces
`core_87000000_SF2000` and `core_87000000_GB300V2`.

## Installing

```bash
./scripts/deploy_sdcard.sh /media/Sajnaps/GB300
```

This writes the FrogUI layout the console expects:

```
/cores/classicube/core_87000000                    libretro core
/ROMS/classicube/classicube;classicube;classicube.gba   launch stub
/ROMS/classicube/texpacks/default.zip              textures
/ROMS/classicube/options.txt                       tuned defaults
```

Launch it from the FrogUI `ROMs/classicube` folder.

The core binary must be named exactly `core_87000000` inside
`cores/classicube/`. The FrogUI firmware (`bios/bisrv.asd`) resolves it with
the string `/mnt/sda1/cores/%s/core_87000000`, where `%s` is the first
`;`-separated field of the ROM stub's filename. A missing or misnamed binary
fails without a message and leaves the console on the loading screen.

`deploy_sdcard.sh` picks the build from the mount point name and can be
overridden with `CONSOLE=SF2000` or `CONSOLE=GB300V2`.

Note: `sf2000_multicore/src/main.c` is a *different* loader that expects
`system/Deimos/cores/<name>.sf2k`. That path applies to the standalone
Multicore launcher, not to FrogUI. Do not install the core there.

## Testing without a console

`runner/test_runner.c` loads the native `.so` via `dlopen`, drives it through
the libretro API with a scripted joypad sequence, and dumps frames to
`frames/*.ppm`.

```bash
./scripts/build.sh native
./test_runner ./classicube_libretro.so 240
```

The frame times it prints are x86_64 numbers and are **not** representative of
the console; treat them as a regression signal only.

## Controls

The console has no mouse, so movement and camera control are mapped onto the
D-Pad and face buttons. These are ClassiCube's own gamepad defaults
(`PadBind_Defaults`), which were written for controllers with two sticks:

| Button | Action |
|--------|--------|
| D-Pad | Walk |
| A | Jump / confirm |
| B | Place block |
| X | Break block |
| Y | Pick block |
| L | Inventory |
| R | Drop item |
| START | Pause menu |
| SELECT | Screenshot / hotkey |

To look around, hold a modifier and press the D-Pad — for example
**L + D-Pad** rotates the camera. If that feels awkward, the bindings are
editable in `options.txt` under the `pad-*` keys (see `bindNames[]` in
`upstream/src/Input.c` for the full list).

## Layout

```
Makefile                  two-target build (native / sf2000)
libretro/libretro_core.c  libretro API: init, load_game, run, deinit
libretro/include/         libretro.h (from Multicore's libretro-common)
runner/test_runner.c      native test harness
scripts/build.sh          build + link helpers
scripts/deploy_sdcard.sh  SD card installer
sf2000_include/dirent.h   firmware dirent.h (console libc lacks one)
texpacks/default.zip      texture pack
upstream/                 unmodified ClassiCube checkout
upstream/src/sf2000/      Platform_SF2000.c, Window_SF2000.c (new)
```

## Changes to upstream

The upstream tree is kept as a real checkout under `upstream/` so upstream
changes can be pulled. Modifications are minimal and local:

**New files**
- `upstream/src/sf2000/Platform_SF2000.c` — filesystem, time, logging, sleep.
- `upstream/src/sf2000/Window_SF2000.c` — framebuffer, RGB565 conversion, input.
- `libretro/libretro_core.c` — libretro entry points.

**Modified**
- `upstream/src/Core.h` — adds the `PLAT_SF2000` profile. It is checked *before*
  the generic `__linux__` branch so native test builds get the same feature set
  as the console.
- `upstream/src/Bitmap.h` — ClassiCube's internal 16bpp layout is 5-5-5 plus an
  alpha bit, which is **not** libretro's RGB565. `Window_DrawFramebuffer`
  converts on submit.
- `upstream/src/Game.h` — `DEFAULT_VIEWDIST` 20, matching the other low-end ports.
- `upstream/src/Graphics_SoftFP.c` — three upstream bugs that this port hits:
  1. `ClearColorBuffer` used `Mem_Set`, which fills byte-by-byte and is wrong
     whenever `BitmapCol` is not 1 byte.
  2. The alpha-blend path computed `finG`/`finB` as `src + (src * (src - dst))`
     instead of `dst + (alpha * (src - dst))`.
  3. The fixed-point vertex structs used `BitmapCol` for the colour field; at
     16bpp that changes their size and breaks the in-place float→fixed
     conversion in `Gfx_UnlockVb`. Now `cc_uint32`, matching the float layout.
     Upstream never builds SoftFP in 16bpp mode, so these were latent.
- `upstream/src/Launcher.c`, `upstream/src/Widgets.c` — honour
  `CC_DISABLE_LAUNCHER` (the in-game option widgets referenced launcher theme
  functions that are compiled out).

## Known limitations

- Singleplayer only; no multiplayer (the console frontend has no socket layer).
- No audio (hardware audio is not exposed to cores).
- Performance on the console is unmeasured — no emulator or hardware was
  available. `viewdist=8` and `normal=fast` in the shipped `options.txt` are
  conservative starting points, not validated values.