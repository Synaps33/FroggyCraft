
MIPS = /opt/mips32-mti-elf/2019.09-03-2/bin/mips-mti-elf-
CC = $(MIPS)gcc
AR = $(MIPS)ar

CFLAGS = -EL -march=mips32 -mtune=mips32 -msoft-float
CFLAGS += -ffast-math -fomit-frame-pointer
CFLAGS += -G0 -mno-abicalls -fno-pic
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -DSF2000

CFLAGS += -O2 -DNDEBUG
CFLAGS += -Iinclude -I./include/compat/zlib
CFLAGS += -I../.. -I../../include
CFLAGS += -D_7ZIP_ST

TARGET=libretro-common.a

SOURCES_C += \
	audio/conversion/float_to_s16.c \
	audio/conversion/s16_to_float.c \
	audio/resampler/audio_resampler.c \
	audio/resampler/drivers/sinc_resampler.c \
	compat/fopen_utf8.c \
	compat/compat_posix_string.c \
	compat/compat_strl.c \
	compat/compat_snprintf.c \
	compat/compat_strcasestr.c \
	encodings/encoding_crc32.c \
	encodings/encoding_utf.c \
	features/features_cpu.c  \
	file/config_file.c \
	file/config_file_userdata.c \
	file/file_path.c \
	file/file_path_io.c \
	file/retro_dirent.c \
	formats/png/rpng.o \
	libco/libco.c \
	lists/dir_list.c \
	lists/string_list.c \
	memmap/memalign.c \
	memmap/memmap.c \
	streams/chd_stream.c \
	streams/file_stream.c \
	streams/file_stream_transforms.c \
	streams/interface_stream.c \
	streams/memory_stream.c \
	streams/trans_stream.o \
	streams/trans_stream_pipe.o \
	streams/trans_stream_zlib.o \
	string/stdstring.c \
	time/rtime.c \
	utils/md5.c \
	vfs/vfs_implementation.c

DEPS_DIR = deps
CFLAGS += -I$(DEPS_DIR) -I$(DEPS_DIR)/libchdr/include
SOURCES_C += \
        $(DEPS_DIR)/libz/adler32.c \
        $(DEPS_DIR)/libz/crc32.c \
        $(DEPS_DIR)/libz/deflate.c \
        $(DEPS_DIR)/libz/gzclose.c \
        $(DEPS_DIR)/libz/gzlib.c \
        $(DEPS_DIR)/libz/gzread.c \
		$(DEPS_DIR)/libz/gzwrite.c \
        $(DEPS_DIR)/libz/inffast.c \
        $(DEPS_DIR)/libz/inflate.c \
        $(DEPS_DIR)/libz/inftrees.c \
        $(DEPS_DIR)/libz/trees.c \
        $(DEPS_DIR)/libz/uncompr.c \
        $(DEPS_DIR)/libz/zutil.c

SOURCES_C += \
        $(DEPS_DIR)/7zip/7zArcIn.c \
        $(DEPS_DIR)/7zip/7zBuf.c \
        $(DEPS_DIR)/7zip/7zCrc.c \
        $(DEPS_DIR)/7zip/7zCrcOpt.c \
        $(DEPS_DIR)/7zip/7zDec.c \
        $(DEPS_DIR)/7zip/7zFile.c \
        $(DEPS_DIR)/7zip/7zStream.c \
        $(DEPS_DIR)/7zip/Bcj2.c \
        $(DEPS_DIR)/7zip/Bra.c \
        $(DEPS_DIR)/7zip/Bra86.c \
        $(DEPS_DIR)/7zip/BraIA64.c \
        $(DEPS_DIR)/7zip/CpuArch.c \
        $(DEPS_DIR)/7zip/Delta.c \
        $(DEPS_DIR)/7zip/Lzma2Dec.c \
        $(DEPS_DIR)/7zip/LzmaDec.c \
        $(DEPS_DIR)/7zip/LzmaEnc.c \
        $(DEPS_DIR)/7zip/LzFind.c

OBJECTS := $(SOURCES_C:.c=.o)

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: clean
