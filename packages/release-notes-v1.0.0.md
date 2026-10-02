## ClassiCube on SF2000 / GB300 — first playable release

A working ClassiCube on the SF2000/GB300 handheld, controlled entirely from the
gamepad. **No firmware modifications required** — drop two folders on the SD
card and launch it from FrogUI.

### Install

```sh
tar xzf froggycraft-classicube-v1.0.0.tar.gz
cd froggycraft-classicube-v1.0.0
./install.sh /media/$USER/GB300 gb300v2     # or: sf2000
```

The installer writes:

```
cores/classicube/core_87000000      the libretro core
ROMS/classicube/                    stub, options.txt, texpacks/default.zip
```

### Controls

| Input | Action |
|---|---|
| D-pad up / down | walk forward / back |
| D-pad left / right | previous / next block in the hotbar |
| X / B | look up / down |
| A / Y | look right / left |
| L / R | break / place block |
| ZL / ZR | strafe left / right |
| SELECT | jump, fly up while flying |
| L + R | toggle fly mode |
| MENU | open / close the pause menu |

In menus: D-pad selects, **A** confirms, **B** goes back, **left/right steps the
selected option's value** — the console has no keyboard, so settings are
adjusted with a stepper rather than typed in.

### Saving

Pause menu → **Save level...** writes `maps/<world>.cw` using ClassiCube's own
map writer, and the world is loaded again automatically on the next launch. New
worlds get a random name and are created from **Small / Normal / Huge / Flat**.

### Graphics

Options → Graphics → **Resolution** applies immediately, no restart:

| | |
|---|---|
| 320x240 | full, not upscaled |
| 213x160 | two thirds |
| 160x120 | half — default, fastest |
| 80x60 | quarter |

The 3D world renders at the chosen resolution and is scaled up; the user
interface is always drawn at 320x240, so buttons and text stay crisp.

### What it took to get here

The bulk of the port is upstream ClassiCube. The parts that needed real work:

* **The colour pipeline was broken in 16bpp.** `BitmapCol` is `RGB555 + 1 alpha
  bit`, so upstream's `alpha != 255` test is *never* true. Opaque texels were
  being blended 50/50 with the background, and the alpha test was discarding
  every opaque terrain face — the world was invisible and only water survived.
  The translucent pass also inherited a stale alpha-test state, which made
  plants, water and glass flicker in and out.
* **Menus could not be activated at all.** Console presses arrive as `CCPAD_*`
  with a device whose enter/escape fields are unset. A virtual keyboard mirrors
  the buttons onto key codes while a screen has input grab.
* **Settings never persisted** because a console never shuts down gracefully, so
  `Options_SaveIfChanged()` from `Game_Free()` never ran. The core now flushes
  them itself.
* **The console's loader derives the content path from its own bookkeeping**, not
  from the file names on the card, so the core ships a fallback stub and stops
  hard-failing when the resolved path is missing.
* **Changing resolution used to freeze the game**, because the resize freed the
  framebuffer while it was being drawn from. Resizes are now deferred to the
  start of the next frame.
* **Split rendering** so the UI stays crisp: the world is rendered small,
  upscaled at `Gfx_End3D()`, and the UI drawn on top at full resolution.

### Performance note

The rasterizer is fill-rate bound on a CPU with no FPU; the dominant cost is a
64-bit divide per pixel for perspective-correct texturing. Dropping resolution
is worth much more than dropping the view distance — at 160x120 it runs in the
low tens of FPS on a GB300.

### Credits

ClassiCube is BSD-3, Copyright 2014-2025 ClassiCube contributors. The Multicore
frontend is ISC, from <gitlab.com/kobily/sf2000_multicore>. `patches/` in the
repository carries the diffs against both, which is the quickest way to review
this port.
