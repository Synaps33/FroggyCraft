TARGET_NAME := bounce

ifeq ($(platform), sf2000)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	STATIC_LINKING = 1
endif

all:
	@echo "Prebuilt $(TARGET)"

clean:
	@echo "clean"

.PHONY: all clean
