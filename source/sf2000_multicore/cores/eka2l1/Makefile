# SF2000 libretro core: EKA2L1 (Symbian OS / S60 emulator)
#
# The emulator itself is a CMake project that lives outside this repository, so
# this Makefile drives that build and then folds every static library it
# produces into the single archive sf2000_multicore expects to find here.
#
# Nothing is downloaded: EKA2L1_SRC must point at an existing checkout.

TARGET_NAME := eka2l1

EKA2L1_SRC ?= ../../../eka2l1
EKA2L1_BUILD ?= /tmp/build_eka2l1_sf2000
EKA2L1_TOOLCHAIN ?= /home/Sajnaps/gb300/eka2l1/cmake/sf2000.toolchain.cmake

# The SF2000 multicore toolchain ships gcc/g++/ld/objcopy only, so reach for
# ar/ranlib from the same Code::Blocks release under its original prefix.
TOOLCHAIN := /opt/mips32-mti-elf/2019.09-03-2/bin/mips-mti-elf-
HOST_CC := gcc
CMAKE := cmake

# Core entry points. An archive is only scanned for symbols that are already
# undefined when the linker reaches it, and the partial link below starts with
# none, so seed the set with everything the SF2000 loader calls into.
ENTRYPOINTS := \
	retro_init \
	retro_deinit \
	retro_api_version \
	retro_get_system_info \
	retro_get_system_av_info \
	retro_set_environment \
	retro_set_video_refresh \
	retro_set_audio_sample \
	retro_set_audio_sample_batch \
	retro_set_input_poll \
	retro_set_input_state \
	retro_set_controller_port_device \
	retro_reset \
	retro_run \
	retro_serialize_size \
	retro_serialize \
	retro_unserialize \
	retro_cheat_reset \
	retro_cheat_set \
	retro_load_game \
	retro_load_game_special \
	retro_unload_game \
	retro_get_region \
	retro_get_memory_data \
	retro_get_memory_size

LD_UNDEF := $(foreach sym,$(ENTRYPOINTS),--undefined=$(sym))

.PHONY: all clean check FORCE

ifeq ($(platform), sf2000)
TARGET := $(TARGET_NAME)_libretro_$(platform).a
STATIC_LINKING = 1
else
$(error This core only supports platform=sf2000)
endif

all: $(TARGET)

# 1. Build EKA2L1 for baremetal MIPS32.
$(EKA2L1_BUILD)/CMakeCache.txt:
	$(CMAKE) -B $(EKA2L1_BUILD) -S $(EKA2L1_SRC) -DCMAKE_TOOLCHAIN_FILE=$(EKA2L1_TOOLCHAIN)

$(EKA2L1_BUILD)/src/emu/libretro/eka2l1_libretro.a: FORCE | $(EKA2L1_BUILD)/CMakeCache.txt
	$(CMAKE) --build $(EKA2L1_BUILD) -j$(shell nproc) --target eka2l1_libretro

# 2. Merge every archive of the emulator into one, so that the multicore
#    Makefile's "cp $(CORE)/*.a" step has a single file to pick up.
#    Do it with a partial link rather than "ar x" + "ar rcs": CMake reuses
#    basenames like server.cpp.obj across archives, so extracting into one
#    directory silently drops the duplicates.
$(TARGET): $(EKA2L1_BUILD)/src/emu/libretro/eka2l1_libretro.a
	@echo "  MERGE   $@"
	@rm -f $@ $(EKA2L1_BUILD)/merged/eka2l1_all.o
	@mkdir -p $(EKA2L1_BUILD)/merged
	@cd $(EKA2L1_BUILD)/merged && \
		$(TOOLCHAIN)ld -EL -r $(LD_UNDEF) --start-group \
			`find $(EKA2L1_BUILD) -name '*.a' | sort -r` \
			--end-group -o eka2l1_all.o && \
		$(TOOLCHAIN)ar rcs $(CURDIR)/$@ eka2l1_all.o && \
		$(TOOLCHAIN)ranlib $(CURDIR)/$@
	@echo "  ARCHECK $@"
	@$(TOOLCHAIN)nm --defined-only $@ | grep -q ' T retro_init' \
		|| (echo "error: retro_init missing from $@" && false)

# Same list of symbols the multicore core.ld expects to find, so a broken
# merge shows up here rather than half way through the firmware build.
check: $(TARGET)
	@for sym in retro_init retro_run retro_load_game retro_unload_game \
	            retro_serialize retro_unserialize retro_set_environment; do \
		$(TOOLCHAIN)nm --defined-only $(TARGET) | grep -q " T $$sym" \
			|| (echo "error: $$sym missing" && false); \
	done
	@echo "  CHECK    all libretro entry points present"

clean:
	@echo "clean"
	@rm -f $(TARGET) $(EKA2L1_BUILD)/merged/eka2l1_all.o
