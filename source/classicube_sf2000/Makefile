SF2000_CORE      := classicube
SF2000_PLATFORM  ?= sf2000

TARGET_NAME := $(SF2000_CORE)

# Source directories: ClassiCube core + SF2000 platform/window + libretro wrapper
SOURCE_DIRS := upstream/src upstream/src/sf2000 libretro third_party/bearssl

ifeq ($(SF2000_PLATFORM),sf2000)
	# Cross build for the Data Frog SF2000 / GB300 (HiChip MIPS32r2, soft float)
	# NOTE: /opt/frog-toolchain only ships gcc/g++/ld/objcopy, so binutils
	# (ar etc.) come from the mips32-mti-elf toolchain the loader is built with.
	SF2000_MIPS ?= /opt/frog-toolchain/mipsel-mti-elf_stable/bin/mipsel-mti-elf-
	SF2000_MIPS_BINUTILS ?= /opt/mips32-mti-elf/2019.09-03-2/bin/mips-mti-elf-
	CC   := $(SF2000_MIPS)gcc
	AR   := $(SF2000_MIPS_BINUTILS)ar
	TARGET := $(TARGET_NAME)_libretro_sf2000.a
	BUILD_DIR := build/sf2000
	STATIC_LINKING := 1

	CFLAGS := -EL -march=mips32 -mtune=mips32r2 -msoft-float
	CFLAGS += -Os -G0 -mno-abicalls -fno-pic -ffreestanding
	CFLAGS += -ffunction-sections -fdata-sections
	CFLAGS += -fno-strict-aliasing
	# The console's newlib has no <dirent.h>; the Multicore loader provides its own
	CFLAGS += -Isf2000_include
	LIBS := -lm
else
	# Native build (x86_64) used for the standalone test runner.
	# PLAT_SF2000 is set so the test build has the exact same feature profile.
	CC   ?= gcc
	AR   ?= ar
	TARGET := $(TARGET_NAME)_libretro.so
	BUILD_DIR := build/native
	fpic := -fPIC
	SHARED := -shared
	LIBS := -lm
endif

CFLAGS += -O2 -Wall -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable $(fpic)
CFLAGS += -Iupstream/src -Ilibretro -Ilibretro/include
CFLAGS += -DPLAT_SF2000
CFLAGS += -D_DEFAULT_SOURCE

C_SOURCES := $(foreach dir,$(SOURCE_DIRS),$(wildcard $(dir)/*.c))

ifneq ($(BUILD_ARCH),)
	BUILD_ROOT := $(BUILD_DIR)/$(BUILD_ARCH)
else
	BUILD_ROOT := $(BUILD_DIR)
endif

OBJECTS := $(patsubst %.c,$(BUILD_ROOT)/%.o,$(C_SOURCES))

.PHONY: all clean

all: $(TARGET)

ifeq ($(STATIC_LINKING),1)
$(TARGET): $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)
else
$(TARGET): $(OBJECTS)
	$(CC) $(SHARED) $(CFLAGS) -o $@ $(OBJECTS) $(LIBS)
endif

$(BUILD_ROOT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)