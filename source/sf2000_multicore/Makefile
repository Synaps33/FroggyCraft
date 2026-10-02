NPROC=$(shell nproc)
SHELL:=/bin/bash

# clear the log file every boot
CLEAR_LOG_ON_BOOT = 0
# debug logging with xlog
DEBUG_XLOG = 1
# select which device this is for
FROGGY_TYPE ?= SF2000
SCREEN_SWAP ?= FALSE

SRC_DIR ?= $(CURDIR)/src
INC_DIR ?= $(CURDIR)/include
BUILD_DIR ?= $(CURDIR)/build

vpath %.c $(SRC_DIR) $(SRC_DIR)/libretro_frontend
vpath %.s $(SRC_DIR) $(SRC_DIR)/libretro_frontend
vpath %.cpp $(SRC_DIR) $(SRC_DIR)/libretro_frontend

LCDFONT_OFFSET=0x2260
LOADER_OFFSET=0x1500
LOADER_ADDR=0x80001500
LOADER_ADDR_MAX=0x80002180

MIPS=/opt/frog-toolchain/mipsel-mti-elf_stable/bin/mipsel-mti-elf-

CC = $(MIPS)gcc
CXX = $(MIPS)g++
LD = $(MIPS)ld
OBJCOPY = $(MIPS)objcopy

CFLAGS := -EL -march=mips32 -mtune=mips32r2 -msoft-float
CFLAGS += -I $(INC_DIR) -I libs/libretro-common/include -I libs/Argent-Cores/include
CFLAGS += -Os -G0 -mno-abicalls -fno-pic
CFLAGS += -ffunction-sections -fdata-sections
# CFLAGS += -Wall
ifeq ($(CLEAR_LOG_ON_BOOT), 1)
CFLAGS += -DCLEAR_LOG_ON_BOOT=1
endif
ifeq ($(DEBUG_XLOG), 1)
CFLAGS += -DDEBUG_XLOG=1
endif
ifeq ($(FROGGY_TYPE), SF2000)
	BISRVLDSCRIPT := bisrv_08_03.ld
	CORELDSCRIPT := bisrv_08_03-core.ld
	CFLAGS += -DSF2000 -DFROGGY_MADCTL=0x60
	ifeq ($(SCREEN_SWAP), FALSE)
		BISRVFILE := bisrv_08_03.asd
	else
#		GB300 screen
		BISRVFILE := bisrv_08_03_Screen_Swap.asd
	endif
else ifeq ($(FROGGY_TYPE),GB300V2)
	BISRVLDSCRIPT := bisrv_GB300_V2.ld
	CORELDSCRIPT := bisrv_GB300_V2-core.ld
	CFLAGS += -DGB300V2 -DFROGGY_MADCTL=0x28
	ifeq ($(SCREEN_SWAP), FALSE)
#		GB300 screen
		BISRVFILE := bisrv_GB300_V2.asd
	else
#		SF2000 screen
		BISRVFILE := bisrv_GB300_V2_Screen_Swap.asd
	endif
else
	$(error Unsupported FROGGY_TYPE: $(FROGGY_TYPE))
endif
#ifneq ($(filter DBLCherryGB TGB Dual,$(CONSOLE)),)
#    CFLAGS += -DDUAL_SAVE
#    CORE_OBJS = core_api_dual.o
#else
#    CORE_OBJS = core_api.o
#endif

LDFLAGS := -EL -nostdlib -z max-page-size=32
LDFLAGS += --gc-sections
# needed for .text LMA = VMA
LDFLAGS += --build-id

CXX_LDFLAGS := -EL -march=mips32 -mtune=mips32r2 -msoft-float
CXX_LDFLAGS += -Wl,--gc-sections --static -flto
CXX_LDFLAGS += -z max-page-size=32
# Route free() calls through __wrap_free in libretro_frontend/lib.c, which can
# report invalid pointers together with the caller address (heap debugging).
CXX_LDFLAGS += -Wl,--wrap=free

CORE_OBJS = $(patsubst $(SRC_DIR)/libretro_frontend/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/libretro_frontend/*.c))
CORE_OBJS += $(BUILD_DIR)/debug.o
LOADER_OBJS = $(BUILD_DIR)/init.o $(BUILD_DIR)/main.o $(BUILD_DIR)/debug.o

# Default target
ifneq ($(CORE),)
install: $(BUILD_DIR)/core_87000000 $(BUILD_DIR)/bisrv.asd
all: install
else
install: $(BUILD_DIR)/bisrv.asd
all: install
endif

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR) 
	$(CC) $(CFLAGS) -o $@ -c $<

$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ -c $<

$(BUILD_DIR)/libretro_core: | $(BUILD_DIR)
	@$(call echo_i,"compiling $(CORE)")
	$(MAKE) -j$(NPROC) -C "$(CORE)" $(MAKEFILE) platform=sf2000

$(BUILD_DIR)/libretro_core.a: $(BUILD_DIR)/libretro_core | $(BUILD_DIR)
	cp "$(CORE)"/*.a $(BUILD_DIR)/libretro_core.a

libretro-common:
	@$(call echo_i,"compiling $@")
	$(MAKE) -j$(NPROC) -C libs/libretro-common

$(BUILD_DIR)/libretro-common.a: libretro-common | $(BUILD_DIR)
	cp -u "$(CURDIR)/libs/libretro-common/libretro-common.a" $@

$(BUILD_DIR)/core.elf: $(BUILD_DIR)/libretro_core.a $(BUILD_DIR)/libretro-common.a $(CORE_OBJS) | $(BUILD_DIR)
	@$(call echo_i,"compiling $@")
	$(CXX) -Wl,-Map=$@.map $(CXX_LDFLAGS) -e __core_entry__ -Tlinker_scripts/core.ld linker_scripts/$(CORELDSCRIPT) -o $@ \
		-Wl,--start-group $(CORE_OBJS) $(BUILD_DIR)/libretro_core.a $(BUILD_DIR)/libretro-common.a -lc -Wl,--end-group

$(BUILD_DIR)/core_87000000: $(BUILD_DIR)/core.elf | $(BUILD_DIR)
	$(OBJCOPY) -O binary -R .MIPS.abiflags -R .note.gnu.build-id -R ".rel*" $(BUILD_DIR)/core.elf $(BUILD_DIR)/core_87000000

$(BUILD_DIR)/loader.elf: $(LOADER_OBJS) | $(BUILD_DIR)
	@$(call echo_i,"compiling $@")
	$(LD) -Map "$@".map $(LDFLAGS) -e __start -Ttext=$(LOADER_ADDR) linker_scripts/$(BISRVLDSCRIPT) $(LOADER_OBJS) -o $(BUILD_DIR)/loader.elf

$(BUILD_DIR)/loader.bin: $(BUILD_DIR)/loader.elf | $(BUILD_DIR)
	$(Q)$(OBJCOPY) -O binary -j .text -j .rodata -j .data $(BUILD_DIR)/loader.elf $(BUILD_DIR)/loader.bin

$(BUILD_DIR)/bisrv.asd: $(BUILD_DIR)/loader.bin $(BUILD_DIR)/lcd_font.bin $(BUILD_DIR)/crc | $(BUILD_DIR)
	@$(call echo_i,"patching $@")

# check that loader's .bss does not exceeds LOADER_ADDR_MAX
	@BSSEND=$(shell grep -w "_end =" $(BUILD_DIR)/loader.elf.map | awk '{print $$1}'); \
	if [ $$(($${BSSEND})) -gt $$(($(LOADER_ADDR_MAX))) ]; then \
		$(call echo_e,"error: loader is too big. \
		bss ending $${BSSEND} exceeds $(LOADER_ADDR_MAX) by \
		$$(( $${BSSEND} - $(LOADER_ADDR_MAX) )) bytes") ; \
		exit 1; \
	else \
		echo "bss ending $${BSSEND}. still $$(( $(LOADER_ADDR_MAX) - $${BSSEND} )) bytes left" ; \
	fi

# check that lcd_font.bin has the anticipated size
	@LCDFONT_SIZE=$(shell stat -c %s $(BUILD_DIR)/lcd_font.bin) ; \
	if [ $${LCDFONT_SIZE} -ne 672 ]; then \
		$(call echo_e,"error: $(BUILD_DIR)/lcd_font.bin size $${LCDFONT_SIZE}. should be 672") ; \
		exit 1; \
	fi

	$(Q)cp assets/os/$(BISRVFILE) $(BUILD_DIR)/bisrv.asd

	$(Q)dd if=$(BUILD_DIR)/loader.bin of=$(BUILD_DIR)/bisrv.asd bs=$$(($(LOADER_OFFSET))) seek=1 conv=notrunc 2>/dev/null

	dd if=$(BUILD_DIR)/lcd_font.bin of=$(BUILD_DIR)/bisrv.asd bs=$$(($(LCDFONT_OFFSET))) seek=1 conv=notrunc

	# note: this patch must match $(LOADER_ADDR)
	# jal run_gba -> jal 0x80001500
	if [ "$(FROGGY_TYPE)" = "SF2000" ]; then \
		printf "\x40\x05\x00\x0C" | dd of=$(BUILD_DIR)/bisrv.asd bs=1 seek=$$((0x35a900)) conv=notrunc; \
	elif [ "$(FROGGY_TYPE)" = "GB300V2" ]; then \
		printf "\x40\x05\x00\x0C" | dd of=$(BUILD_DIR)/bisrv.asd bs=1 seek=$$((0x35ee10)) conv=notrunc; \
	fi

	# endless loop in sys_watchdog_reboot -> j 0x80001508
	printf "\x42\x05\x00\x08" | dd of=$(BUILD_DIR)/bisrv.asd bs=1 seek=$$((0x30d4)) conv=notrunc
	# endless loop in INT_General_Exception_Hdlr -> j 0x80001510
	printf "\x44\x05\x00\x08" | dd of=$(BUILD_DIR)/bisrv.asd bs=1 seek=$$((0x495a0)) conv=notrunc

	# patch the buffer size for handling the save state snapshot image
	# \x0c (768k) would be enough up to cores displaying at 640x480x2
	if [ "$(FROGGY_TYPE)" = "SF2000" ]; then \
		printf "\x0c" | dd of=$(BUILD_DIR)/bisrv.asd bs=1 seek=$$((0x34f8b8)) conv=notrunc; \
	elif [ "$(FROGGY_TYPE)" = "GB300V2" ]; then \
		printf "\x0c" | dd of=$(BUILD_DIR)/bisrv.asd bs=1 seek=$$((0x354214)) conv=notrunc; \
	fi

	$(Q)$(BUILD_DIR)/crc $(BUILD_DIR)/bisrv.asd

$(BUILD_DIR)/lcd_font.bin: $(BUILD_DIR)/lcd_font.o | $(BUILD_DIR)
	$(OBJCOPY) -O binary -j ".rodata.lcd_font" $< $@

$(BUILD_DIR)/crc: $(SRC_DIR)/crc.c | $(BUILD_DIR)
	gcc -o $(BUILD_DIR)/crc $(SRC_DIR)/crc.c

install:
	@$(call echo_i,"install to sdcard")
	-$(call copy_if_updated,"$(BUILD_DIR)/bisrv.asd","sdcard/$(FROGGY_TYPE)$(if $(filter TRUE,$(SCREEN_SWAP)),-screen_swap)/bios/bisrv.asd")
	@if [ "$(SCREEN_SWAP)" = "TRUE" ]; then \
		if [ "$(FROGGY_TYPE)" = "SF2000" ]; then \
			printf '\x28' > "sdcard/$(FROGGY_TYPE)-screen_swap/bios/madctl.bin"; \
		elif [ "$(FROGGY_TYPE)" = "GB300V2" ]; then \
			printf '\x60' > "sdcard/$(FROGGY_TYPE)-screen_swap/bios/madctl.bin"; \
		fi; \
	fi
	-$(call copy_if_updated,"$(BUILD_DIR)/core_87000000","sdcard/$(FROGGY_TYPE)/system/Deimos/cores/$(CONSOLE).sf2k")
	@$(call echo_d,"bisrv.asd")
	@$(call echo_d,"$(CORE)")

ifneq ($(ALPHARELEASE),)
updatelogo:
	@$(call echo_i,"update boot logo")
	-$(call update_bisrv_logo,"sdcard/bios/bisrv.asd")
	@$(call echo_d,"$(1) logo updated")
else
updatelogo:
	@$(call echo_e,"ALPHARELEASE not defined!")
endif

# Clean intermediate files and the final executable
clean:
	-rm -rf $(BUILD_DIR)
	@if [ -n "$(CORE)" ]; then \
		$(MAKE) -j$(NPROC) -C "$(CORE)" $(MAKEFILE) clean platform=sf2000; \
	fi

.PHONY: all clean

define echo_i
    echo -e "\033[1;33m$(1)\033[0m"
endef

define echo_e
    echo -e "\033[1;31m$(1)\033[0m"
endef

define echo_d
    echo -e "\033[1;37m$(1)\033[0m"
endef

define copy_if_updated
    diff -q $(1) $(2) || { rm -rf $(2) && mkdir -p "$(shell echo $(2) | sed 's:\([^/]*\)[/]*$$::')" && cp $(1) $(2) && echo "$(1) updated"; }
endef

define update_bisrv_logo
	printf "\xB8\x0B" | dd of=$(1) bs=1 seek=$$((0x3463d0)) conv=notrunc && \
	convert /home/adcockm/downloads/multicore_alpha_release/Multicore_Boot_Logo_-_Xmasprint_-_RGB565.png -gravity North -font helvetica -fill white -pointsize 14 -annotate +0+36 'version $(ALPHARELEASE)' /home/adcockm/downloads/multicore_alpha_release/versionlogo.png && \
	python3 /home/adcockm/downloads/multicore_alpha_release/bootlogo.py
endef
