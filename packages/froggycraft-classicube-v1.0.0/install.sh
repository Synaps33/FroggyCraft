#!/usr/bin/env bash
# FroggyCraft - ClassiCube for SF2000 / GB300
# Copies the core and game data onto the console's SD card.
#
# Usage:  ./install.sh /path/to/sd/card [gb300v2|sf2000]
set -euo pipefail

TARGET="${1:-}"
VARIANT="${2:-gb300v2}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

die() { echo "error: $*" >&2; exit 1; }

case "$VARIANT" in
  gb300v2|sf2000) ;;
  *) die "unknown variant '$VARIANT' (use gb300v2 or sf2000)" ;;
esac

if [ -z "$TARGET" ]; then
  echo "No target given. Mount the SD card and pass its mount point, e.g.:"
  echo "    ./install.sh /media/\$USER/GB300 gb300v2"
  echo
  echo "Looking for a mounted card automatically..."
  for d in /media/*/* /media/* /Volumes/* /mnt/*; do
    [ -d "$d/ROMS" ] && [ -d "$d/cores" ] && TARGET="$d" && break
  done
fi

[ -n "$TARGET" ] || die "could not find the SD card. Pass the mount point explicitly."
[ -d "$TARGET" ] || die "not a directory: $TARGET"

echo "Installing ClassiCube ($VARIANT) into $TARGET"

mkdir -p "$TARGET/cores/classicube"
mkdir -p "$TARGET/ROMS/classicube/texpacks"

cp -f "$HERE/payload/$VARIANT/cores/classicube/core_87000000" \
      "$TARGET/cores/classicube/core_87000000"
# Stub files: the console's loader resolves the content path from these.
cp -f "$HERE/payload/$VARIANT/ROMS/classicube/classicube.gba" \
      "$TARGET/ROMS/classicube/classicube.gba"
cp -f "$HERE/payload/$VARIANT/ROMS/classicube/classicube" \
      "$TARGET/ROMS/classicube/classicube"
cp -f "$HERE/payload/$VARIANT/ROMS/classicube/texpacks/default.zip" \
      "$TARGET/ROMS/classicube/texpacks/default.zip"
cp -f "$HERE/payload/$VARIANT/ROMS/classicube/options.txt" \
      "$TARGET/ROMS/classicube/options.txt"

sync
echo "Done. Safely eject the card, insert it into the console and run"
echo "ClassiCube from ROMS/classicube in FrogUI."
