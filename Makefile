.DEFAULT_GOAL := kernel
.SUFFIXES:
.DELETE_ON_ERROR:

# Command-line overrides work with both GNU make 3.81 and newer versions.
CC := clang
LD := ld.lld
HOST_CC := cc
XORRISO := xorriso
QEMU := qemu-system-x86_64
ARCH := x86_64
ifneq ($(ARCH),x86_64)
$(error Unsupported ARCH=$(ARCH); only x86_64 is implemented)
endif

BUILD := build
LIMINE := vendor/limine
KERNEL := $(BUILD)/kernel.elf
ISO := $(BUILD)/on.iso
EXCEPTION_TEST ?= 0
ifeq ($(filter $(EXCEPTION_TEST),0 1 2 3 4 5 6 7 8 9),)
$(error EXCEPTION_TEST must be 0 through 9)
endif
PMM_TEST ?= 0
ifeq ($(filter $(PMM_TEST),0 1 2 3),)
$(error PMM_TEST must be 0 through 3)
endif
ifneq ($(PMM_TEST),0)
ifeq ($(filter build/%,$(BUILD)),)
$(error PMM_TEST requires an isolated BUILD directory beneath build/)
endif
ifneq ($(EXCEPTION_TEST),0)
$(error PMM_TEST and EXCEPTION_TEST must be run separately)
endif
endif
VMM_TEST ?= 0
ifeq ($(filter $(VMM_TEST),0 1),)
$(error VMM_TEST must be 0 or 1)
endif
ifneq ($(VMM_TEST),0)
ifeq ($(filter build/%,$(BUILD)),)
$(error VMM_TEST requires an isolated BUILD directory beneath build/)
endif
ifneq ($(EXCEPTION_TEST)-$(PMM_TEST),0-0)
$(error VMM_TEST must run separately from other injections)
endif
endif
MODE := $(EXCEPTION_TEST)-$(PMM_TEST)-$(VMM_TEST)
# Portable subsystems plus only the selected architecture.
COMMON_SOURCES := $(sort $(shell find kernel/core kernel/mm kernel/sched -name '*.c' -type f))
ARCH_SOURCES := $(sort $(shell find kernel/arch/$(ARCH) -name '*.c' -type f))
SOURCES := $(COMMON_SOURCES) $(ARCH_SOURCES)
ASM_SOURCES := $(sort $(shell find kernel/arch/$(ARCH) -name '*.S' -type f))
OBJECTS := $(patsubst %.c,$(BUILD)/obj/mode-$(MODE)/%.o,$(SOURCES)) $(patsubst %.S,$(BUILD)/obj/mode-$(MODE)/%.o,$(ASM_SOURCES))
# Fault injection is restricted to isolated output directories.
ifneq ($(EXCEPTION_TEST),0)
ifeq ($(filter build/%,$(BUILD)),)
$(error EXCEPTION_TEST requires an isolated BUILD directory beneath build/)
endif
endif
CPPFLAGS := -Iboot/protocol -Ikernel/include -Iinclude -DON_EXCEPTION_TEST=$(EXCEPTION_TEST) -DON_PMM_TEST=$(PMM_TEST) -DON_VMM_TEST=$(VMM_TEST)
CFLAGS := --target=x86_64-unknown-none-elf -std=c11 -O2 -g \
	-ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-mno-red-zone -mcmodel=kernel -mgeneral-regs-only \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-Wall -Wextra -Werror -MMD -MP
LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=4096 \
	--build-id=none -T kernel/arch/x86_64/linker.ld
QEMUFLAGS ?= -m 256M
DISPLAY ?= none
PYTHON := python3
# QEMU's bundled x86-64 OVMF code uses its shared IA32/X64 variables template.
OVMF_CODE ?= /opt/homebrew/share/qemu/edk2-x86_64-code.fd
OVMF_VARS ?= /opt/homebrew/share/qemu/edk2-i386-vars.fd

.PHONY: all kernel objects iso force-mode run run-uefi check-ovmf verify-uefi verify-boot verify-framebuffer test-framebuffer verify-exceptions verify-interrupts verify-memory test-memory fetch-limine check-tools check-deps help
all: kernel
kernel: $(KERNEL)
objects: $(OBJECTS)

$(BUILD)/obj/mode-$(MODE)/%.o: %.c Makefile
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(BUILD)/obj/mode-$(MODE)/%.o: %.S Makefile
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) --target=x86_64-unknown-none-elf -g -MMD -MP -c "$<" -o "$@"

.PHONY: force-mode
force-mode:

# GNU make 3.81 may compare only whole seconds; a mode switch must always link.
ifneq ($(shell cat "$(BUILD)/build-mode" 2>/dev/null),$(MODE))
$(KERNEL): force-mode
endif

# Re-link if a build directory switches injection mode, including back to normal.
$(BUILD)/build-mode: force-mode
	@mkdir -p "$(dir $@)"
	@if ! test -f "$@" || test "$$(cat "$@")" != "$(MODE)"; then \
		printf '%s\n' '$(MODE)' > "$@"; fi

$(KERNEL): $(BUILD)/build-mode $(OBJECTS) kernel/arch/x86_64/linker.ld Makefile
	$(LD) $(LDFLAGS) -Map="$(BUILD)/kernel.map" -o "$@" $(OBJECTS)

# This target alone downloads third-party files. It does not install software.
fetch-limine:
	sh scripts/fetch-limine.sh

# Compile the Limine image utility for the development host.
$(BUILD)/host/limine: $(LIMINE)/limine.c Makefile
	@mkdir -p "$(dir $@)"
	$(HOST_CC) -std=c99 -O2 "$<" -o "$@"

iso: $(ISO)
$(ISO): $(KERNEL) boot/config/limine.conf $(BUILD)/host/limine \
	$(LIMINE)/limine-bios.sys $(LIMINE)/limine-bios-cd.bin \
	$(LIMINE)/limine-uefi-cd.bin $(LIMINE)/BOOTX64.EFI
	mkdir -p "$(BUILD)/iso-root/boot/limine" "$(BUILD)/iso-root/EFI/BOOT"
	cp "$(KERNEL)" "$(BUILD)/iso-root/boot/kernel.elf"
	cp boot/config/limine.conf "$(BUILD)/iso-root/boot/limine/limine.conf"
	cp "$(LIMINE)/limine-bios.sys" "$(LIMINE)/limine-bios-cd.bin" \
		"$(LIMINE)/limine-uefi-cd.bin" "$(BUILD)/iso-root/boot/limine/"
	cp "$(LIMINE)/BOOTX64.EFI" "$(BUILD)/iso-root/EFI/BOOT/"
	$(XORRISO) -as mkisofs -R -r -J -V ON \
		-b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
		--protective-msdos-label "$(BUILD)/iso-root" -o "$@"
	"$(BUILD)/host/limine" bios-install "$@"

# TCG emulates x86-64 on Apple Silicon; HVF cannot accelerate this guest.
run: $(ISO)
	$(QEMU) -machine q35 -accel tcg -cpu qemu64 $(QEMUFLAGS) \
		-cdrom "$(ISO)" -boot d -display $(DISPLAY) -serial "file:$(BUILD)/serial.log" -monitor stdio \
		-debugcon "file:$(BUILD)/debug.log" -global isa-debugcon.iobase=0xe9 \
		-no-reboot -no-shutdown

check-ovmf:
	@test -f "$(OVMF_CODE)" || { echo 'Missing OVMF_CODE: $(OVMF_CODE)' >&2; exit 1; }
	@test -f "$(OVMF_VARS)" || { echo 'Missing OVMF_VARS: $(OVMF_VARS)' >&2; exit 1; }

# Preserve interactive UEFI NVRAM locally; firmware code is always read-only.
$(BUILD)/ovmf-vars.fd: $(OVMF_VARS)
	@mkdir -p "$(dir $@)"
	cp "$<" "$@"

run-uefi: check-ovmf $(ISO) $(BUILD)/ovmf-vars.fd
	$(QEMU) -machine q35 -accel tcg -cpu qemu64 $(QEMUFLAGS) \
		-drive if=pflash,format=raw,unit=0,readonly=on,file="$(OVMF_CODE)" \
		-drive if=pflash,format=raw,unit=1,file="$(BUILD)/ovmf-vars.fd" \
		-cdrom "$(ISO)" -boot d -display $(DISPLAY) -serial "file:$(BUILD)/uefi-serial.log" -monitor stdio \
		-debugcon "file:$(BUILD)/uefi-debug.log" -global isa-debugcon.iobase=0xe9 \
		-no-reboot -no-shutdown

verify-uefi: check-ovmf $(ISO)
	$(PYTHON) scripts/verify-framebuffer.py "$(QEMU)" "$(ISO)" "$(BUILD)" \
		--timeout "$(UEFI_TIMEOUT)" --ovmf-code "$(OVMF_CODE)" --ovmf-vars "$(OVMF_VARS)"

# Bounded smoke test: never accepts a previous run's debug log.
verify-boot: $(ISO)
	sh scripts/verify-boot.sh "$(QEMU)" "$(ISO)" "$(BUILD)" "$(BOOT_TIMEOUT)" $(QEMUFLAGS)

# Capture the real QEMU display and exercise the missing-framebuffer fallback.
verify-framebuffer: $(ISO)
	$(PYTHON) scripts/verify-framebuffer.py "$(QEMU)" "$(ISO)" "$(BUILD)" --timeout "$(BOOT_TIMEOUT)"
	$(PYTHON) scripts/verify-framebuffer.py "$(QEMU)" "$(ISO)" "$(BUILD)" --timeout "$(BOOT_TIMEOUT)" --no-framebuffer

verify-interrupts: verify-framebuffer verify-uefi

# Every fault image is isolated; test both firmware paths on each image.
verify-exceptions: check-ovmf
	@set -eu; for item in ud:1 de:2 gp:3 pf:4 df:5 stack:6 ro:7 nx:8 unmapped:9; do \
		name=$${item%:*}; mode=$${item#*:}; output="$(BUILD)/exceptions/$$name"; \
		mkdir -p "$$output"; \
		$(MAKE) iso BUILD="$$output" EXCEPTION_TEST="$$mode" > "$$output/build.log" 2>&1 || \
			{ cat "$$output/build.log"; exit 1; }; \
		$(PYTHON) scripts/verify-framebuffer.py "$(QEMU)" "$$output/on.iso" "$$output" \
			--timeout "$(UEFI_TIMEOUT)" --exception "$$name" --elf "$$output/kernel.elf" --nm "$(LLVM_NM)"; \
		$(PYTHON) scripts/verify-framebuffer.py "$(QEMU)" "$$output/on.iso" "$$output" \
			--timeout "$(UEFI_TIMEOUT)" --exception "$$name" --elf "$$output/kernel.elf" --nm "$(LLVM_NM)" \
			--ovmf-code "$(OVMF_CODE)" --ovmf-vars "$(OVMF_VARS)"; \
	done

# Exhaustive real-frame test and missing-map/HHDM fallback, both firmware paths.
verify-memory: check-ovmf test-memory
	@set -eu; for mode in 1 2 3; do \
		output="$(BUILD)/memory/mode-$$mode"; mkdir -p "$$output"; \
		$(MAKE) iso BUILD="$$output" EXCEPTION_TEST=0 PMM_TEST="$$mode" > "$$output/build.log" 2>&1 || \
			{ cat "$$output/build.log"; exit 1; }; \
		for ram in 128M 256M; do \
			$(PYTHON) scripts/verify-framebuffer.py "$(QEMU)" "$$output/on.iso" "$$output/$$ram" \
				--timeout "$(UEFI_TIMEOUT)" --memory-test "$$mode" --ram "$$ram"; \
			$(PYTHON) scripts/verify-framebuffer.py "$(QEMU)" "$$output/on.iso" "$$output/$$ram" \
				--timeout "$(UEFI_TIMEOUT)" --memory-test "$$mode" --ram "$$ram" \
				--ovmf-code "$(OVMF_CODE)" --ovmf-vars "$(OVMF_VARS)"; \
		done; \
	done

# Portable allocator tests substitute an interrupt-state model for privileged I/O.
test-memory:
	@mkdir -p "$(BUILD)/tests"
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
		-Ikernel/include kernel/mm/physical.c tests/unit/physical.c \
		-o "$(BUILD)/tests/physical"
	"$(BUILD)/tests/physical"

LLVM_NM ?= /opt/homebrew/opt/llvm/bin/llvm-nm

# Native host test; never link sanitizer/runtime code into the kernel.
test-framebuffer:
	@mkdir -p "$(BUILD)/tests"
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
		-Ikernel/include kernel/core/framebuffer.c tests/unit/framebuffer.c \
		-o "$(BUILD)/tests/framebuffer"
	"$(BUILD)/tests/framebuffer"

BOOT_TIMEOUT ?= 30
UEFI_TIMEOUT ?= 60

check-tools:
	@missing=0; for tool in "$(CC)" "$(LD)" "$(HOST_CC)" "$(XORRISO)" "$(QEMU)" curl shasum tar; do \
		if command -v "$$tool" >/dev/null 2>&1; then \
			printf 'OK       %s\n' "$$tool"; \
		else printf 'MISSING  %s\n' "$$tool"; missing=1; fi; \
	done; exit $$missing

check-deps:
	@missing=0; for asset in limine.c limine-bios.sys limine-bios-cd.bin limine-uefi-cd.bin BOOTX64.EFI; do \
		if test -f "$(LIMINE)/$$asset"; then printf 'OK       %s\n' "$(LIMINE)/$$asset"; \
		else printf 'MISSING  %s\n' "$(LIMINE)/$$asset"; missing=1; fi; \
	done; exit $$missing

help:
	@echo 'make objects       Compile freestanding x86-64 objects only'
	@echo 'make kernel        Compile and link build/kernel.elf (default)'
	@echo 'make fetch-limine  Explicitly download checksum-pinned Limine'
	@echo 'make iso           Build BIOS/UEFI build/on.iso'
	@echo 'make run           Boot under QEMU TCG; type quit to exit'
	@echo 'make run DISPLAY=cocoa  Show the welcome screen in a macOS window'
	@echo 'make run-uefi DISPLAY=cocoa  Boot with x86-64 OVMF firmware'
	@echo 'make verify-uefi   Verify UEFI entry, serial output, and welcome capture'
	@echo 'make verify-boot   Verify BIOS handoff and welcome rendering'
	@echo 'make verify-framebuffer  Capture welcome.png and test no-VGA fallback'
	@echo 'make verify-interrupts   Verify PIT ticks and welcome under BIOS/UEFI'
	@echo 'make verify-exceptions   Test nine fatal CPU fault cases under BIOS and UEFI'
	@echo 'make verify-virtual     Test paging, permissions, TLB reuse, and rollback under BIOS/UEFI'
	@echo 'make verify-memory      Test PMM exhaustion and fallback under BIOS/UEFI'
	@echo 'make test-memory        Run native allocator safety tests'
	@echo 'make test-framebuffer    Run native renderer safety tests'
	@echo 'make check-tools   Report all required commands without installing'
	@echo 'make check-deps    Check local Limine assets without downloading'

-include $(OBJECTS:.o=.d)

.PHONY: verify-virtual
verify-virtual: check-ovmf
	$(MAKE) iso BUILD=$(BUILD)/virtual VMM_TEST=1
	$(PYTHON) scripts/verify-framebuffer.py "$(QEMU)" "$(BUILD)/virtual/on.iso" "$(BUILD)/virtual" --timeout "$(UEFI_TIMEOUT)" --virtual-test
	$(PYTHON) scripts/verify-framebuffer.py "$(QEMU)" "$(BUILD)/virtual/on.iso" "$(BUILD)/virtual" --timeout "$(UEFI_TIMEOUT)" --virtual-test --ovmf-code "$(OVMF_CODE)" --ovmf-vars "$(OVMF_VARS)"
