STATIC_LINKING := 0
AR             := ar

ifneq ($(V),1)
   Q := @
endif

ifeq ($(platform),)
platform = unix
endif

# system platform
system_platform = unix

CORE_DIR    := .
TARGET_NAME := menu
LIBM        = -lm

ifeq ($(STATIC_LINKING), 1)
EXT := a
endif

# SF2000 / GB300 platform
ifeq ($(platform), sf2000)
   TARGET := _libretro_$(platform).a
   MIPS ?= /opt/mips32-mti-elf/2019.09-03-2/bin/mips-mti-elf-
   CC = $(MIPS)gcc
   CXX = $(MIPS)g++
   AR = $(MIPS)ar
   CFLAGS = -EL -march=mips32 -mtune=mips32 -msoft-float -G0 -mno-abicalls -fno-pic
   CFLAGS += -ffast-math -fomit-frame-pointer -ffunction-sections -fdata-sections
   CFLAGS += -O3 -DSF2000 -DNDEBUG
   ifeq ($(FROGGY_TYPE), GB300V2)
      CFLAGS += -DGB300V2
   endif
   CXXFLAGS := $(CFLAGS)
   STATIC_LINKING = 1
else
   # Default unix build for testing
   EXT ?= so
   TARGET := $(TARGET_NAME)_libretro.$(EXT)
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=link.T -Wl,--no-undefined
   ifeq ($(DEBUG), 1)
      CFLAGS += -O0 -g -DDEBUG
      CXXFLAGS += -O0 -g -DDEBUG
   else
      CFLAGS += -Os
      CXXFLAGS += -Os
   endif
endif

LDFLAGS += $(LIBM)

# Source files - main menu (v79: filemanager, calculator added)
SOURCES_C := frogos.c font.c render.c recent_games.c settings.c theme.c favorites.c gfx_theme.c lodepng.c avi_bg.c display_opts.c osk.c text_editor.c stb_image_jpeg.c gifdec.c simplewebp_impl.c video_browser.c video_player.c music_player.c image_viewer.c filemanager.c calculator.c

# libmad sources (MP3 decoder for video player audio)
LIBMAD_SOURCES := \
	libmad/bit.c \
	libmad/fixed.c \
	libmad/frame.c \
	libmad/huffman.c \
	libmad/layer12.c \
	libmad/layer3.c \
	libmad/libmad.c \
	libmad/stream.c \
	libmad/synth.c \
	libmad/timer.c

SOURCES_C += $(LIBMAD_SOURCES)

# XVID library sources (decoder only - for animated backgrounds)
XVID_SOURCES := \
	xvid/xvid.c \
	xvid/decoder.c \
	xvid/bitstream/bitstream.c \
	xvid/bitstream/cbp.c \
	xvid/bitstream/mbcoding.c \
	xvid/dct/idct.c \
	xvid/dct/simple_idct.c \
	xvid/image/colorspace.c \
	xvid/image/image.c \
	xvid/image/interpolate8x8.c \
	xvid/image/postprocessing.c \
	xvid/image/qpel.c \
	xvid/image/reduced.c \
	xvid/motion/gmc.c \
	xvid/motion/motion_comp.c \
	xvid/motion/sad.c \
	xvid/prediction/mbprediction.c \
	xvid/quant/quant_h263.c \
	xvid/quant/quant_matrix.c \
	xvid/quant/quant_mpeg.c \
	xvid/utils/emms.c \
	xvid/utils/mem_align.c \
	xvid/utils/mem_transfer.c

SOURCES_C += $(XVID_SOURCES)

OBJECTS := $(SOURCES_C:.c=.o)

# Include directories
INCFLAGS := -I../../include -I../.. -I.

CFLAGS   += $(fpic) -Wall -D__LIBRETRO__ $(INCFLAGS)
CXXFLAGS += $(fpic) -Wall -D__LIBRETRO__ $(INCFLAGS)

all: $(TARGET)

$(TARGET): $(OBJECTS)
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	@$(if $(Q), $(shell echo echo LD $@),)
	$(Q)$(CC) $(fpic) $(SHARED) -o $@ $(OBJECTS) $(LDFLAGS)
endif

%.o: %.c
	@$(if $(Q), $(shell echo echo CC $<),)
	$(Q)$(CC) $(CFLAGS) $(fpic) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)
	rm -f xvid/*.o xvid/bitstream/*.o xvid/dct/*.o xvid/image/*.o
	rm -f xvid/motion/*.o xvid/prediction/*.o xvid/quant/*.o xvid/utils/*.o
	rm -f libmad/*.o

.PHONY: clean all
