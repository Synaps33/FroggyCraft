TARGET_NAME := fheroes2

STATIC_LINKING := 0
AR             := ar

ifeq ($(platform),)
platform = unix
endif

CORE_DIR := .

ifeq ($(platform), sf2000)
   TARGET := $(TARGET_NAME)_libretro_$(platform).a
   MIPS ?= /opt/mips32-mti-elf/2019.09-03-2/bin/mips-mti-elf-
   CC = $(MIPS)gcc
   CXX = $(MIPS)g++
   AR = $(MIPS)ar
   CFLAGS = -EL -march=mips32 -mtune=mips32 -msoft-float -G0 -mno-abicalls -fno-pic
   CFLAGS += -ffast-math -fomit-frame-pointer -ffunction-sections -fdata-sections
   CFLAGS += -O2 -DSF2000 -DNDEBUG
   ifeq ($(FROGGY_TYPE), GB300V2)
      CFLAGS += -DGB300V2
   endif
   CXXFLAGS := $(CFLAGS) -std=gnu++11
   STATIC_LINKING = 1
   LIBCO_SRC = libco_sf2000.c
   # Only include the multicore frontend headers when this tree lives inside a
   # multicore checkout. Nothing in the core includes them (no deimos.h, file.h,
   # input.h, logging.h or core.h), so building standalone is fine and
   # -I../../include is just dead weight there.
   MULTICORE_INCLUDE := $(wildcard ../../include/deimos.h)
   ifneq ($(MULTICORE_INCLUDE),)
      EXTRA_INCDIRS = -I../../include
   endif
else
   TARGET := $(TARGET_NAME)_libretro.so
   CC := /usr/bin/gcc -m64
   CXX := /usr/bin/g++ -m64
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=link.T -Wl,--no-undefined
   CFLAGS += -O2 -g $(fpic)
   CXXFLAGS += -O2 -g $(fpic) -std=gnu++11
   LIBCO_SRC = libco_ucontext.c
endif

DEFS := -D__LIBRETRO__ -DWITHOUT_MOUSE -DWITHOUT_MIXER -DWITHOUT_IMAGE -DWITHOUT_UNICODE \
        -DWITHOUT_NETWORK -DWITHOUT_EDITOR -DWITHOUT_ZLIB -DSVN_REVISION=\"1898\"

INCDIRS := $(EXTRA_INCDIRS) \
           -I$(CORE_DIR) \
           -I$(CORE_DIR)/sdl/include \
           -I$(CORE_DIR)/sdl/include/SDL \
           -I$(CORE_DIR)/sdl/src \
           -I$(CORE_DIR)/sdl/src/video \
           -I$(CORE_DIR)/sdl/src/events \
           -I$(CORE_DIR)/src/engine \
           -I$(CORE_DIR)/src/xmlccwrap \
           -I$(CORE_DIR)/src/fheroes2/agg \
           -I$(CORE_DIR)/src/fheroes2/algorithm \
           -I$(CORE_DIR)/src/fheroes2/army \
           -I$(CORE_DIR)/src/fheroes2/battle2 \
           -I$(CORE_DIR)/src/fheroes2/castle \
           -I$(CORE_DIR)/src/fheroes2/dialog \
           -I$(CORE_DIR)/src/fheroes2/editor \
           -I$(CORE_DIR)/src/fheroes2/game \
           -I$(CORE_DIR)/src/fheroes2/gui \
           -I$(CORE_DIR)/src/fheroes2/heroes \
           -I$(CORE_DIR)/src/fheroes2/image \
           -I$(CORE_DIR)/src/fheroes2/kingdom \
           -I$(CORE_DIR)/src/fheroes2/maps \
           -I$(CORE_DIR)/src/fheroes2/monster \
           -I$(CORE_DIR)/src/fheroes2/network \
           -I$(CORE_DIR)/src/fheroes2/objects \
           -I$(CORE_DIR)/src/fheroes2/pocketpc \
           -I$(CORE_DIR)/src/fheroes2/resource \
           -I$(CORE_DIR)/src/fheroes2/spell \
           -I$(CORE_DIR)/src/fheroes2/system \
           -I$(CORE_DIR)/src/fheroes2/test

CFLAGS   += $(DEFS) $(INCDIRS)
CXXFLAGS += $(DEFS) $(INCDIRS)

SOURCES_C := \
	sdl/src/SDL.c \
	sdl/src/SDL_error.c \
	sdl/src/SDL_fatal.c \
	sdl/src/video/SDL_video.c \
	sdl/src/video/SDL_surface.c \
	sdl/src/video/SDL_pixels.c \
	sdl/src/video/SDL_blit.c \
	sdl/src/video/SDL_blit_0.c \
	sdl/src/video/SDL_blit_1.c \
	sdl/src/video/SDL_blit_A.c \
	sdl/src/video/SDL_blit_N.c \
	sdl/src/video/SDL_RLEaccel.c \
	sdl/src/video/SDL_cursor.c \
	sdl/src/video/SDL_bmp.c \
	sdl/src/video/SDL_libretro_video.c \
	sdl/src/events/SDL_events.c \
	sdl/src/events/SDL_keyboard.c \
	sdl/src/events/SDL_mouse.c \
	sdl/src/events/SDL_active.c \
	sdl/src/events/SDL_quit.c \
	sdl/src/events/SDL_resize.c \
	sdl/src/file/SDL_rwops.c \
	sdl/src/timer/SDL_timer.c \
	sdl/src/timer/SDL_systimer.c \
	sdl/src/thread/SDL_thread.c \
	sdl/src/thread/SDL_systhread.c \
	sdl/src/thread/SDL_sysmutex.c \
	sdl/src/thread/SDL_syssem.c \
	sdl/src/thread/SDL_syscond.c \
	sdl/src/audio/SDL_audio.c \
	sdl/src/audio/SDL_audiocvt.c \
	sdl/src/audio/SDL_mixer.c \
	sdl/src/audio/SDL_dummyaudio.c \
	sdl/src/cpuinfo/SDL_cpuinfo.c \
	sdl/src/stdlib/SDL_string.c \
	sdl/src/stdlib/SDL_getenv.c \
	$(LIBCO_SRC)

# Gather all C++ sources
RAW_CXX := $(wildcard libretro.cpp) \
           $(wildcard src/xmlccwrap/*.cpp) \
           $(wildcard src/engine/*.cpp) \
           $(wildcard src/fheroes2/*/*.cpp)

# Filter out unneeded engine files
SOURCES_CXX := $(filter-out \
	src/engine/IMG_savepng.cpp \
	src/engine/audio_cdrom.cpp \
	src/engine/font.cpp \
	src/engine/zzlib.cpp \
	src/xmlccwrap/gzstream.cpp, $(RAW_CXX))

OBJS_C   := $(SOURCES_C:.c=.o)
OBJS_CXX := $(SOURCES_CXX:.cpp=.o)
OBJECTS  := $(OBJS_C) $(OBJS_CXX)

all: $(TARGET)

ifeq ($(STATIC_LINKING), 1)
$(TARGET): $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)
else
$(TARGET): $(OBJECTS)
	$(CXX) $(SHARED) $(CXXFLAGS) -o $@ $(OBJECTS) -lm
endif

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: clean all
