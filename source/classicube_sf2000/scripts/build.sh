#!/bin/bash
# Build ClassiCube for Data Frog SF2000 / GB300 (Multicore/FrogUI).
#
#   ./scripts/build.sh native   -> classicube_libretro.so  (x86_64, for test_runner)
#   ./scripts/build.sh sf2000   -> classicube_libretro_sf2000.a (MIPS32r2 static lib)
#   ./scripts/build.sh link     -> core_87000000 + .sf2k for SF2000 and GB300V2
#   ./scripts/build.sh all      -> everything
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MULTICORE="${MULTICORE:-/home/Sajnaps/gb300/sf2000_multicore}"
MIPS_PREFIX="${MIPS_PREFIX:-/opt/mips32-mti-elf/2019.09-03-2/bin/mips-mti-elf-}"

CORE_NAME="classicube"

step() { printf '\n\033[1;33m==> %s\033[0m\n' "$1"; }

build_native() {
	step "Building native x86_64 core (for test_runner)"
	make -C "$PROJECT_DIR" SF2000_PLATFORM=native
	cc -O2 -Wall -I"$PROJECT_DIR/libretro/include" \
		-o "$PROJECT_DIR/test_runner" "$PROJECT_DIR/runner/test_runner.c" -ldl
	ln -sf "$PROJECT_DIR/texpacks" "$PROJECT_DIR/frames/texpacks" 2>/dev/null || true
	echo "Built: classicube_libretro.so, test_runner"
}

build_sf2000() {
	step "Building MIPS32r2 static library for SF2000/GB300"
	make -C "$PROJECT_DIR" SF2000_PLATFORM=sf2000
	/opt/mips32-mti-elf/2019.09-03-2/bin/mips-mti-elf-size \
		"$PROJECT_DIR/classicube_libretro_sf2000.a" | tail -1
	echo "Built: classicube_libretro_sf2000.a"
}

link_cores() {
	if [ ! -d "$MULTICORE" ]; then
		echo "ERROR: Multicore repo not found at $MULTICORE" >&2
		exit 1
	fi

	# Multicore invokes our Makefile via this stub
	mkdir -p "$MULTICORE/cores/$CORE_NAME"
	cat > "$MULTICORE/cores/$CORE_NAME/Makefile" <<EOF
TARGET_NAME := $CORE_NAME
CLASSICUBE_SRC ?= $PROJECT_DIR

ifeq (\$(platform), sf2000)
	TARGET := \$(TARGET_NAME)_libretro_\$(platform).a
	STATIC_LINKING = 1
endif

all:
	\$(MAKE) -C "\$(CLASSICUBE_SRC)" SF2000_PLATFORM=sf2000
	cp -f "\$(CLASSICUBE_SRC)/\$(TARGET)" ./\$(TARGET)

clean:
	rm -f *.a

.PHONY: all clean
EOF

	for FROGGY in SF2000 GB300V2; do
		step "Linking core for $FROGGY"
		rm -rf "$MULTICORE/build"
		make -C "$MULTICORE" FROGGY_TYPE="$FROGGY" \
			CORE="cores/$CORE_NAME" CONSOLE="$CORE_NAME" \
			MIPS="$MIPS_PREFIX" >/dev/null
		cp "$MULTICORE/build/core_87000000" "$PROJECT_DIR/core_87000000_$FROGGY"
		echo "Built: core_87000000_$FROGGY ($(stat -c %s "$PROJECT_DIR/core_87000000_$FROGGY") bytes)"
	done
}

case "${1:-all}" in
	native) build_native ;;
	sf2000) build_sf2000 ;;
	link)   link_cores ;;
	all)    build_native; build_sf2000; link_cores ;;
	*)      echo "usage: $0 {native|sf2000|link|all}" >&2; exit 1 ;;
esac

echo
echo "Done."