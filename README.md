# FroggyCraft

ClassiCube running on the **SF2000 / GB300** handheld, with gamepad-only controls
and no firmware modifications.


## What this is

ClassiCube is normally a desktop multiplayer client. This is a port for the
SF2000/GB300: a 320x240 MIPS32 handheld with no FPU, no mouse and no keyboard.
Everything the game normally expects from a keyboard and a mouse is mapped onto
the gamepad, including the settings menu.

## Releases

Prebuilt, plug-and-play packages are attached to the
[releases](https://github.com/Synaps33/FroggyCraft/releases). Each release
contains a self-contained installer:

```sh
./install.sh /media/$USER/GB300 gb300v2     # GB300V2
./install.sh /media/$USER/GB300 sf2000      # plain SF2000
```

No firmware patching is required.

## Controls

| Input | Action |
|---|---|
| D-pad up / down | walk forward / back |
| D-pad left / right | previous / next block in the hotbar |
| X / B | look up / down |
| A / Y | look right / left |
| L / R | break / place block |
| ZL / ZR | strafe left / right |
| SELECT | jump, and fly up while flying |
| L + R | toggle fly mode |
| MENU | open / close the pause menu |

In menus: D-pad selects, **A** confirms, **B** goes back, left/right steps the
selected option's value (no keyboard needed to change settings).

## Notable implementation points

The port is mostly upstream ClassiCube. The parts that needed real work:

* **16bpp colour pipeline.** `BitmapCol` is a 16-bit `RGB555 + 1 alpha bit`.
  Upstream tests `alpha != 255`, which is *never* true in that format, so
  opaque texels were being blended 50/50 with the background and every opaque
  terrain face was discarded by the alpha test. Both are fixed, and the
  translucent pass no longer inherits a stale alpha-test state.
* **Two-resolution rendering.** The world is rendered into a smaller buffer,
  upscaled at the end of the 3D pass (`Gfx_End3D`), and the 2D UI is then drawn
  into a full 320x240 buffer, so HUD text and buttons stay crisp.
* **Input plumbing.** Console presses arrive as `CCPAD_*` with a device whose
  enter/escape fields are unset, so no menu item could be activated. A virtual
  keyboard mirrors the buttons onto key codes while a screen has input grab.
* **Consoles never shut down gracefully**, so `Options_SaveIfChanged()` and the
  save file are flushed by the core itself rather than from an exit handler.
* **Keyboard-free world saving** through a one-button pause menu entry, reusing
  ClassiCube's own `.cw` writer and loader.

## Repository layout

```
packages/    prebuilt plug-and-play packages (also attached to releases)
source/      full source of both components
  classicube_sf2000/   ClassiCube + the SF2000 platform layer
  sf2000_multicore/    Multicore frontend (libretro host for the console)
patches/     diffs against upstream ClassiCube and the Multicore frontend
```

`patches/classicube-sf2000.patch` applies on top of
[ClassiCube/ClassiCube](https://github.com/ClassiCube/ClassiCube) at the commit
recorded in the patch header, which is the fastest way to review the port.

## Building from source

```sh
cd source/classicube_sf2000
./scripts/build.sh all                       # native .so + MIPS core
./scripts/deploy_sdcard.sh /media/$USER/GB300
```

Cross toolchain: `mipsel-mti-elf` (the frog-toolchain build works). The Multicore
frontend in `source/sf2000_multicore` builds the loader and final core.

## Performance

The rasterizer is fill-rate bound because the CPU has no FPU; the dominant cost
is a 64-bit divide per pixel for perspective-correct texturing. On a GB300 at
160x120 this runs in the low tens of FPS, and dropping the resolution is worth
much more than dropping the view distance.

## Third party code

* ClassiCube - BSD-3, Copyright 2014-2025 ClassiCube contributors
* Multicore frontend - ISC, from <gitlab.com/kobily/sf2000_multicore>

See `source/*/LICENSE` and `patches/` for details.
