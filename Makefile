# ShadeBootloader — build with `make`, try it with `make run`.

ARCH      := x86_64
BUILD     := build
TARGET    := $(BUILD)/BOOTX64.EFI

CC        := clang
PYTHON    := python3
QEMU      := qemu-system-x86_64

# UEFI firmware for QEMU: first match across distro layouts (override with
# `make run OVMF_CODE=... OVMF_VARS=...`). Code and vars must be a pair.
OVMF_PAIRS := \
  /usr/share/OVMF/OVMF_CODE_4M.fd:/usr/share/OVMF/OVMF_VARS_4M.fd \
  /usr/share/OVMF/OVMF_CODE.fd:/usr/share/OVMF/OVMF_VARS.fd \
  /usr/share/edk2/x64/OVMF_CODE.4m.fd:/usr/share/edk2/x64/OVMF_VARS.4m.fd \
  /usr/share/edk2/x64/OVMF_CODE.fd:/usr/share/edk2/x64/OVMF_VARS.fd \
  /usr/share/edk2/ovmf/OVMF_CODE.fd:/usr/share/edk2/ovmf/OVMF_VARS.fd \
  /usr/share/edk2-ovmf/x64/OVMF_CODE.fd:/usr/share/edk2-ovmf/x64/OVMF_VARS.fd \
  /usr/share/qemu/ovmf-x86_64-4m-code.bin:/usr/share/qemu/ovmf-x86_64-4m-vars.bin \
  /usr/share/qemu/edk2-x86_64-code.fd:/usr/share/qemu/edk2-i386-vars.fd \
  /opt/homebrew/share/qemu/edk2-x86_64-code.fd:/opt/homebrew/share/qemu/edk2-i386-vars.fd \
  /usr/local/share/qemu/edk2-x86_64-code.fd:/usr/local/share/qemu/edk2-i386-vars.fd
OVMF_PAIR := $(firstword $(foreach p,$(OVMF_PAIRS),$(if $(wildcard $(firstword $(subst :, ,$(p)))),$(p))))
OVMF_CODE ?= $(word 1,$(subst :, ,$(OVMF_PAIR)))
OVMF_VARS ?= $(word 2,$(subst :, ,$(OVMF_PAIR)))

# Hardware acceleration when available (QEMU falls back to emulation).
QEMU_ACCEL := $(if $(wildcard /dev/kvm),-accel kvm -accel tcg,$(if $(filter Darwin,$(shell uname -s)),-accel hvf -accel tcg,))
# Native window backend this QEMU build has: gtk (Linux), cocoa (macOS) or sdl.
QEMU_DISPLAYS := $(shell $(QEMU) -display help 2>/dev/null)
QEMU_WINDOW := $(firstword $(filter gtk cocoa sdl,$(QEMU_DISPLAYS)))
QEMU_GUI := $(if $(QEMU_WINDOW),yes)
DISPLAY_gtk := gtk,zoom-to-fit=on
DISPLAY_cocoa := cocoa,zoom-to-fit=on
DISPLAY_sdl := sdl
NOVNC ?= $(firstword $(wildcard /usr/share/novnc /usr/share/webapps/novnc /usr/share/novnc-core))

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

.PHONY: all clean run run-window run-browser run-headless debug esp test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

# Fail early with a useful message instead of "clang: No such file".
MISSING_TOOLS := $(strip $(foreach t,$(CC) lld-link $(PYTHON),$(if $(shell command -v $(t) 2>/dev/null),,$(t))))
# (only when building: `make clean` / `make test` don't need the cross toolchain)
ifneq ($(MISSING_TOOLS),)
ifneq ($(if $(MAKECMDGOALS),$(filter-out clean test,$(MAKECMDGOALS)),all),)
$(OBJ): toolcheck
endif
endif
.PHONY: toolcheck
toolcheck:
	@echo "Missing build tools: $(MISSING_TOOLS)"
	@echo "Install them (full list per OS in the README, under Requirements):"
	@echo "  Arch:          sudo pacman -S clang lld python"
	@echo "  Debian/Ubuntu: sudo apt install clang lld python3"
	@echo "  Fedora:        sudo dnf install clang lld python3"
	@exit 1

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

QEMU_COMMON = -machine q35 -m 512M -cpu max $(QEMU_ACCEL) \
	-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
	-drive if=pflash,format=raw,file=$(BUILD)/OVMF_VARS.fd \
	-drive file=$(BUILD)/disk-main.img,format=raw,if=none,id=main -device nvme,drive=main,serial=shade0 \
	-drive file=$(BUILD)/disk-games.img,format=raw,if=none,id=games -device nvme,drive=games,serial=shade1 \
	-device qemu-xhci -drive file=$(BUILD)/disk-usb.img,format=raw,if=none,id=usb -device usb-storage,drive=usb \
	-device usb-tablet -device usb-kbd \
	-vga std -debugcon file:$(BUILD)/debug.log -serial none

$(BUILD)/OVMF_VARS.fd:
	@test -n "$(OVMF_VARS)" && test -f "$(OVMF_VARS)" || { \
	  echo "OVMF (UEFI firmware for QEMU) not found. Install it:"; \
	  echo "  Debian/Ubuntu: sudo apt install ovmf    Fedora: sudo dnf install edk2-ovmf"; \
	  echo "  Arch: sudo pacman -S edk2-ovmf          or pass OVMF_CODE=... OVMF_VARS=..."; exit 1; }
	@mkdir -p $(BUILD)
	cp $(OVMF_VARS) $@

# A native window when QEMU has one, otherwise the browser (VNC + noVNC).
ifeq ($(QEMU_GUI),yes)
run: run-window
else
run: run-browser
endif

run-window: esp $(BUILD)/OVMF_VARS.fd
	@test -n "$(QEMU_WINDOW)" || { echo "this QEMU has no window support (gtk/cocoa/sdl); try make run-browser"; exit 1; }
	$(QEMU) $(QEMU_COMMON) -name Shade -display $(DISPLAY_$(QEMU_WINDOW))

# QEMU's VNC server (websocket on 5700) + noVNC served on 6080.
run-browser: esp $(BUILD)/OVMF_VARS.fd
	@test -n "$(NOVNC)" || { echo "noVNC not found (apt/dnf install novnc), or use make run-window"; exit 1; }
	@echo ""
	@echo "  Shade is starting. Open this in your browser:"
	@echo "    http://localhost:6080/vnc.html?autoconnect=1&resize=scale&port=5700"
	@echo "  (Ctrl+C here to stop)"
	@echo ""
	@$(PYTHON) -m http.server 6080 --bind 127.0.0.1 --directory $(NOVNC) >/dev/null 2>&1 & \
	  srv=$$!; trap "kill $$srv 2>/dev/null" EXIT INT TERM; \
	  $(QEMU) $(QEMU_COMMON) -display none -vnc 127.0.0.1:0,websocket=5700

# Headless, with a monitor socket for screendump/sendkey (see tools/qemu-shot.py).
run-headless: esp $(BUILD)/OVMF_VARS.fd
	$(QEMU) $(QEMU_COMMON) -display none -monitor unix:$(BUILD)/monitor.sock,server,nowait

debug: esp $(BUILD)/OVMF_VARS.fd
	$(QEMU) $(QEMU_COMMON) -s -S

# ---- host unit tests -------------------------------------------------

# gcc: its address/UB sanitizer runtimes ship with the toolchain
HOST_CC := gcc
HOST_CFLAGS := -std=c11 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc
test:
	@mkdir -p $(BUILD)/host
	$(HOST_CC) $(HOST_CFLAGS) -o $(BUILD)/host/filter_test \
		tests/filter_test.c src/boot/filter.c src/core/str.c src/core/config.c
	$(BUILD)/host/filter_test
	$(HOST_CC) $(HOST_CFLAGS) -o $(BUILD)/host/hid_test \
		tests/hid_test.c src/input/hid.c
	$(BUILD)/host/hid_test
	sh tests/install_test.sh

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)
