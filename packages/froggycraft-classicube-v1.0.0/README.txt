FroggyCraft - ClassiCube for SF2000 / GB300
=============================================

First playable ClassiCube on the SF2000/GB300 handheld.

The console has no mouse and no keyboard, so this port drives the whole game
from the gamepad: looking around, moving, mining, building and a settings menu
whose values are stepped instead of typed.


INSTALL
-------
1. Insert the SD card into your computer and note the mount point
   (for example /media/$USER/GB300 or /Volumes/GB300 on macOS).
2. Run:

       ./install.sh <mount point> [gb300v2|sf2000]

   The variant defaults to gb300v2. Use sf2000 for the plain SF2000.
   The installer is idempotent; re-run it to update an existing install.
3. Safely eject the card, put it in the console, and start ClassiCube from
   ROMS/classicube in FrogUI.

Everything is written into two folders:

    cores/classicube/core_87000000      the libretro core
    ROMS/classicube/                    stub, options and the texture pack

No firmware modification is required.


CONTROLS
--------
  D-pad up / down      walk forward / back
  D-pad left / right   previous / next block in the hotbar
  X / B                look up / down
  A / Y                look right / left
  L                    break block
  R                    place block
  ZL / ZR              strafe left / right
  SELECT               jump (and fly up while flying)
  L + R                toggle fly mode
  MENU                 open / close the pause menu
  B (in a menu)        back

  Pause menu -> "Save level..."      saves the world to maps/<name>.cw
  Pause menu -> "Generate new level..."  Small / Normal / Huge / Flat

The saved world is loaded again automatically on the next launch.


GRAPHICS
--------
Resolution is configurable in Options -> Graphics -> Resolution and applies
immediately:

    320x240    full, not upscaled
    213x160    two thirds
    160x120    half          (default, best speed on this CPU)
    80x60      quarter

The 3D world is rendered at the chosen resolution and scaled up; the user
interface is always drawn at 320x240, so buttons and text stay crisp.

View distance can be set in Options -> Graphics -> View distance, and in
Options -> Classic for the preset steps.


PERFORMANCE NOTE
----------------
The software rasterizer runs on a CPU with no FPU, so it is fill-rate bound.
The single biggest cost is a 64-bit divide per pixel for perspective-correct
texturing. Lowering the resolution is therefore worth much more than lowering
the view distance.


FILES
-----
    install.sh            installer
    payload/gb300v2/      core and game data for GB300V2
    payload/sf2000/       core and game data for SF2000


THIRD PARTY CODE
----------------
ClassiCube is BSD-3 licensed, Copyright 2014-2025 ClassiCube contributors.
The Multicore frontend is ISC licensed, originally from
gitlab.com/kobily/sf2000_multicore. See the source/ folder in the repository
for the full licenses and the patch sets applied on top of both.
