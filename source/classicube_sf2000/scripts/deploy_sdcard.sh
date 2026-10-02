#!/bin/bash
# Install ClassiCube onto an SD card (FrogUI layout).
#
#   ./scripts/deploy_sdcard.sh [/mnt/sda1]
#
# Layout expected by the FrogUI firmware (strings inside bios/bisrv.asd):
#     /mnt/sda1/cores/<core>/core_87000000   <- core binary
#     /mnt/sda1/ROMS/<core>/<file>.gba       <- launch stub shown in the browser
#
# The stub's content is ignored; the firmware splits the filename on ';' and
# uses the first field as the core name (see init_direct_loader() in
# sf2000_multicore/cores/menu/frogos.c). So a stub named
# "classicube;classicube;classicube.gba" selects cores/classicube/core_87000000.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SDCARD="${1:-/media/Sajnaps/GB300}"
CORE_NAME="classicube"

# Which console build to install. Override with CONSOLE=SF2000|GB300V2
if [ -z "$CONSOLE" ]; then
	case "$SDCARD" in
		*GB300*|*gb300*) CONSOLE="GB300V2" ;;
		*)             CONSOLE="SF2000" ;;
	esac
fi

if [ ! -d "$SDCARD" ]; then
	echo "ERROR: SD card mount point '$SDCARD' not found." >&2
	echo "Pass the mount point as the first argument." >&2
	exit 1
fi

CORE_BIN="$PROJECT_DIR/core_87000000_$CONSOLE"
if [ ! -f "$CORE_BIN" ]; then
	echo "ERROR: $CORE_BIN missing. Run scripts/build.sh link first." >&2
	exit 1
fi

echo "==> Installing ClassiCube ($CONSOLE build) into $SDCARD"

# --- core binary ------------------------------------------------------------
mkdir -p "$SDCARD/cores/$CORE_NAME"
cp "$CORE_BIN" "$SDCARD/cores/$CORE_NAME/core_87000000"

# --- launch stub + data -----------------------------------------------------
# IMPORTANT: the stub keeps a PLAIN filename. FrogUI's init_direct_loader()
# (cores/menu/frogos.c:834) does:
#     sprintf(ptr_gs_run_game_file, "%s;%s;%s.gba", core, directory, filename);
# i.e. it appends ";<dir>;<file>.gba" itself. A stub already named
# "core;dir;file.gba" would turn into five ';' separated fields and the
# firmware would fail to resolve the core - the console would just go black.
# The firmware's own comment calls this out: "stub doesn't have to exist
# (loader fixes later)".
mkdir -p "$SDCARD/ROMS/$CORE_NAME/texpacks"
rm -f "$SDCARD/ROMS/$CORE_NAME"/*.gba
printf '%s' "$CORE_NAME" > "$SDCARD/ROMS/$CORE_NAME/$CORE_NAME.gba"
cp "$PROJECT_DIR/texpacks/default.zip" "$SDCARD/ROMS/$CORE_NAME/texpacks/default.zip"

# The console's loader derives the content path from its own bookkeeping
# (ptr_gs_run_game_file), not from the file names on the card, and different
# firmware builds resolve it differently: some keep the extension, some strip
# it. Ship both spellings so the resolved path always exists. Our frontend also
# falls back to the .gba stub when the loader hands us a path that is missing.
printf '%s' "$CORE_NAME" > "$SDCARD/ROMS/$CORE_NAME/$CORE_NAME"

# --- default options --------------------------------------------------------
# Option keys match upstream/src/Options.h:
#   viewdist (OPT_VIEW_DISTANCE), fpslimit (OPT_FPS_LIMIT),
#   normal   (OPT_RENDER_TYPE), username (LOPT_USERNAME)
cat > "$SDCARD/ROMS/$CORE_NAME/options.txt" <<'EOF'
# ClassiCube options - tuned for SF2000 / GB300
# 320x240 panel, software fixed-point rasterizer, no FPU, no networking.

# Short view distance: fewer chunks => much less software rasterization.
# NOTE: ClassiCube rewrites this file on exit, so whatever you last set in the
# in-game options menu wins from then on.
viewdist=16
fpslimit=Limit30FPS

# Small view distance: the software rasterizer has no FPU on this CPU, so
# fewer loaded chunks is the single biggest win for input responsiveness.

# "fast" = no sky, no clouds (they are large full-screen fills)
normal=fast

username=Player
EOF

echo
echo "Installed:"
echo "  $SDCARD/cores/$CORE_NAME/core_87000000"
echo "  $SDCARD/ROMS/$CORE_NAME/$CORE_NAME.gba"
echo "  $SDCARD/ROMS/$CORE_NAME/$CORE_NAME"
echo "  $SDCARD/ROMS/$CORE_NAME/texpacks/default.zip"
echo "  $SDCARD/ROMS/$CORE_NAME/options.txt"
echo
echo "Launch from the FrogUI ROMs/classicube folder."