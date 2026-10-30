# ShadeBootloader — build with `make`, try it with `make run`.

ARCH      := x86_64
BUILD     := build
TARGET    := $(BUILD)/BOOTX64.EFI

CC        := clang
PYTHON    := python3
QEMU      := qemu-system-x86_64
OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS ?= /usr/share/OVMF/OVMF_VARS_4M.fd

CFLAGS := -target $(ARCH)-unknown-windows -std=c11 -O2 \
          -ffreestanding -nostdlibinc -fshort-wchar -mno-red-zone \
          -fno-stack-protector -fno-stack-check -mno-stack-arg-probe \
          -Wall -Wextra -Wno-unused-parameter \
          -Iinclude -Iinclude/libc -Isrc -Ithird_party/stb -MMD -MP
LDFLAGS := -target $(ARCH)-unknown-windows -nostdlib -fuse-ld=lld-link \
           -Wl,-entry:efi_main -Wl,-subsystem:efi_application

SRC := $(wildcard src/*.c src/*/*.c)
OBJ := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(SRC)) $(BUILD)/obj/assets_data.o

# Everything the binary carries so it works with no files on the ESP.
ASSETS := fonts/Inter-Regular.ttf=third_party/inter/Inter-Regular.ttf \
          fonts/Inter-SemiBold.ttf=third_party/inter/Inter-SemiBold.ttf \
          $(foreach f,$(wildcard themes/neutral/*.conf themes/neutral/icons/*.png),$(patsubst themes/%,%,$(f))=$(f))
ASSET_FILES := $(foreach a,$(ASSETS),$(word 2,$(subst =, ,$(a))))

.PHONY: all clean run run-headless debug esp test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD)/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Vendored code is not ours to lint.
$(BUILD)/obj/gfx/stb_impl.o: src/gfx/stb_impl.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@

$(BUILD)/gen/assets_data.c: tools/embed.py $(ASSET_FILES)
	@mkdir -p $(dir $@)
	$(PYTHON) tools/embed.py $@ $(ASSETS)

$(BUILD)/obj/assets_data.o: $(BUILD)/gen/assets_data.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ---- test environment ------------------------------------------------

TESTAPP := $(BUILD)/testapp.efi

$(TESTAPP): tests/testapp.c include/efi.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $(BUILD)/testapp.o
	$(CC) $(LDFLAGS) -o $@ $(BUILD)/testapp.o

esp: $(TARGET) $(TESTAPP)
	$(PYTHON) tools/mkdisks.py $(BUILD) $(TARGET) $(TESTAPP)

QEMU_COMMON = -machine q35 -m 512M -cpu max \
	-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
	-drive if=pflash,format=raw,file=$(BUILD)/OVMF_VARS.fd \
	-drive file=$(BUILD)/disk-main.img,format=raw,if=none,id=main -device nvme,drive=main,serial=shade0 \
	-drive file=$(BUILD)/disk-games.img,format=raw,if=none,id=games -device nvme,drive=games,serial=shade1 \
	-device qemu-xhci -drive file=$(BUILD)/disk-usb.img,format=raw,if=none,id=usb -device usb-storage,drive=usb \
	-device usb-tablet -device usb-kbd \
	-vga std -debugcon file:$(BUILD)/debug.log -serial none

$(BUILD)/OVMF_VARS.fd:
	@mkdir -p $(BUILD)
	cp $(OVMF_VARS) $@

run: esp $(BUILD)/OVMF_VARS.fd
	$(QEMU) $(QEMU_COMMON)

# Headless, with a monitor socket for screendump/sendkey (see tools/qemu-shot.py).
run-headless: esp $(BUILD)/OVMF_VARS.fd
	$(QEMU) $(QEMU_COMMON) -display none -monitor unix:$(BUILD)/monitor.sock,server,nowait

debug: esp $(BUILD)/OVMF_VARS.fd
	$(QEMU) $(QEMU_COMMON) -s -S

# ---- host unit tests -------------------------------------------------

HOST_CC := clang
test:
	@mkdir -p $(BUILD)/host
	$(HOST_CC) -std=c11 -g -Wall -Wextra -Isrc -o $(BUILD)/host/filter_test \
		tests/filter_test.c src/boot/filter.c src/core/str.c src/core/config.c
	$(BUILD)/host/filter_test

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)
