TARGET_NAME := brickgame
BRICKGAME_SRC ?= /home/Sajnaps/gb300/brickgame_sf2000

ifeq ($(platform), sf2000)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	STATIC_LINKING = 1
endif

all:
	$(MAKE) -C "$(BRICKGAME_SRC)" SF2000_PLATFORM=sf2000
	cp -f "$(BRICKGAME_SRC)/$(TARGET)" ././$(TARGET)

clean:
	rm -f *.a

.PHONY: all clean
