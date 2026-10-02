TARGET_NAME := j2me
FROGGY_KVM_SRC ?= $(CURDIR)/../../../froggykvm

ifeq ($(platform), sf2000)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	STATIC_LINKING = 1
endif

all:
	@echo "=== Building FroggyKVM static library from $(FROGGY_KVM_SRC) ==="
	$(MAKE) -C "$(FROGGY_KVM_SRC)" platform=sf2000
	cp -f "$(FROGGY_KVM_SRC)/$(TARGET)" ./$(TARGET)

clean:
	@if [ -d "$(FROGGY_KVM_SRC)" ]; then \
		$(MAKE) -C "$(FROGGY_KVM_SRC)" platform=sf2000 clean 2>/dev/null || true; \
	fi
	rm -f *.a

.PHONY: all clean
