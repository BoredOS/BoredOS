# BoredOS Makefile
# Target Architecture: x86_64
# Host: macOS
# Copyright (c) 2023-2026 Chris (boreddevnl)
# This software is released under the GNU General Public License v3.0. See LICENSE file for details.
# This header needs to maintain in any file it is present in, as per the GPL license terms.

CC = x86_64-elf-gcc
LD = x86_64-elf-ld
NASM = nasm
XORRISO = xorriso

SRC_DIR = src
BUILD_DIR = build
ISO_DIR = iso_root

KERNEL_ELF = $(BUILD_DIR)/boredos.elf
ISO_IMAGE = boredos.iso

C_SOURCES = $(wildcard $(SRC_DIR)/core/*.c) \
            $(wildcard $(SRC_DIR)/sys/*.c) \
            $(wildcard $(SRC_DIR)/mem/*.c) \
            $(wildcard $(SRC_DIR)/dev/*.c) \
            $(wildcard $(SRC_DIR)/input/*.c) \
            $(wildcard $(SRC_DIR)/net/*.c) \
            $(wildcard $(SRC_DIR)/net/nic/*.c) \
            $(wildcard $(SRC_DIR)/fs/*.c) \
            $(wildcard $(SRC_DIR)/wm/*.c) \
			$(wildcard $(SRC_DIR)/net/third_party/lwip/core/*.c) \
			$(wildcard $(SRC_DIR)/net/third_party/lwip/core/ipv4/*.c) \
			$(SRC_DIR)/net/third_party/lwip/netif/ethernet.c \
			$(SRC_DIR)/net/third_party/lwip/netif/bridgeif.c

ASM_SOURCES = $(wildcard $(SRC_DIR)/arch/*.asm)
OBJ_FILES = $(patsubst $(SRC_DIR)/core/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/core/*.c)) \
            $(patsubst $(SRC_DIR)/sys/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/sys/*.c)) \
            $(patsubst $(SRC_DIR)/mem/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/mem/*.c)) \
            $(patsubst $(SRC_DIR)/dev/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/dev/*.c)) \
            $(patsubst $(SRC_DIR)/input/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/input/*.c)) \
            $(patsubst $(SRC_DIR)/net/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/net/*.c)) \
            $(patsubst $(SRC_DIR)/net/nic/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/net/nic/*.c)) \
            $(patsubst $(SRC_DIR)/fs/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/fs/*.c)) \
            $(patsubst $(SRC_DIR)/wm/%.c, $(BUILD_DIR)/%.o, $(wildcard $(SRC_DIR)/wm/*.c)) \
			$(patsubst $(SRC_DIR)/net/third_party/lwip/%.c, $(BUILD_DIR)/lwip/%.o, $(filter $(SRC_DIR)/net/third_party/lwip/%.c, $(C_SOURCES))) \
            $(patsubst $(SRC_DIR)/arch/%.asm, $(BUILD_DIR)/%.o, $(ASM_SOURCES))

CFLAGS = -g -O2 -pipe -Wall -Wextra -std=gnu11 -ffreestanding \
         -fno-stack-protector -fno-stack-check -fno-lto -fPIE \
         -m64 -march=x86-64 -msse -msse2 -mstackrealign -mno-red-zone \
		 -I$(SRC_DIR) -I$(SRC_DIR)/net/third_party/lwip -I$(SRC_DIR)/core -I$(SRC_DIR)/sys -I$(SRC_DIR)/mem -I$(SRC_DIR)/dev -I$(SRC_DIR)/net -I$(SRC_DIR)/net/nic -I$(SRC_DIR)/fs -I$(SRC_DIR)/wm  -I$(SRC_DIR)/input

LDFLAGS = -m elf_x86_64 -nostdlib -static -pie --no-dynamic-linker \
          -z text -z max-page-size=0x1000 -T linker.ld

NASMFLAGS = -f elf64

LIMINE_VERSION = 10.8.2
LIMINE_URL_BASE = https://github.com/limine-bootloader/limine/raw/v$(LIMINE_VERSION)

.PHONY: all clean run limine-setup

all: $(ISO_IMAGE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)

limine-setup:
	@if [ ! -f limine/limine-bios.sys ]; then \
		echo "Limine binaries missing or invalid. Cloning v$(LIMINE_VERSION)-binary..."; \
		rm -rf limine; \
		git clone https://github.com/limine-bootloader/limine.git --branch=v$(LIMINE_VERSION)-binary --depth=1 limine; \
	fi
	@if [ ! -f $(SRC_DIR)/core/limine.h ]; then \
		echo "Copying limine.h..."; \
		cp limine/limine.h $(SRC_DIR)/core/limine.h; \
	fi
	@echo "Building Limine host utility..."; \
	$(MAKE) -C limine

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/core/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/sys/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/mem/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/dev/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	
$(BUILD_DIR)/%.o: $(SRC_DIR)/input/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/net/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/net/nic/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/fs/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/wm/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lwip/%.o: $(SRC_DIR)/net/third_party/lwip/%.c | $(BUILD_DIR) limine-setup
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/arch/%.asm | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/test_syscall.o: $(SRC_DIR)/arch/test_syscall.asm | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/user_test.o: $(SRC_DIR)/arch/user_test.asm | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/process_asm.o: $(SRC_DIR)/arch/process_asm.asm | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(KERNEL_ELF): $(OBJ_FILES)
	$(LD) $(LDFLAGS) -o $@ $(OBJ_FILES)
	$(MAKE) -C $(SRC_DIR)/userland

$(BUILD_DIR)/initrd.tar: $(KERNEL_ELF)
	rm -rf $(BUILD_DIR)/initrd
	mkdir -p $(BUILD_DIR)/initrd/bin
	mkdir -p $(BUILD_DIR)/initrd/Library/images/Wallpapers
	mkdir -p $(BUILD_DIR)/initrd/Library/images/gif
	mkdir -p $(BUILD_DIR)/initrd/Library/Fonts/Emoji
	mkdir -p $(BUILD_DIR)/initrd/Library/DOOM
	mkdir -p $(BUILD_DIR)/initrd/Library/bsh
	mkdir -p $(BUILD_DIR)/initrd/docs

	@for f in $(SRC_DIR)/userland/bin/*.elf; do \
		if [ -f "$$f" ]; then cp "$$f" $(BUILD_DIR)/initrd/bin/; fi \
	done
	@for f in $(SRC_DIR)/images/wallpapers/*; do \
		if [ -f "$$f" ]; then cp "$$f" $(BUILD_DIR)/initrd/Library/images/Wallpapers/; fi \
	done
	@for f in $(SRC_DIR)/images/gif/*.gif; do \
		if [ -f "$$f" ]; then cp "$$f" $(BUILD_DIR)/initrd/Library/images/gif/; fi \
	done
	@for f in $(SRC_DIR)/fonts/*.ttf; do \
		if [ -f "$$f" ]; then cp "$$f" $(BUILD_DIR)/initrd/Library/Fonts/; fi \
	done
	@for f in $(SRC_DIR)/fonts/Emoji/*.ttf; do \
		if [ -f "$$f" ]; then cp "$$f" $(BUILD_DIR)/initrd/Library/Fonts/Emoji/; fi \
	done
	@if [ -f $(SRC_DIR)/library/bsh/bshrc ]; then cp $(SRC_DIR)/library/bsh/bshrc $(BUILD_DIR)/initrd/Library/bsh/; fi
	@if [ -f $(SRC_DIR)/library/bsh/startup.bsh ]; then cp $(SRC_DIR)/library/bsh/startup.bsh $(BUILD_DIR)/initrd/Library/bsh/; fi
	@if [ -f $(SRC_DIR)/library/bsh/boot.bsh ]; then cp $(SRC_DIR)/library/bsh/boot.bsh $(BUILD_DIR)/initrd/Library/bsh/; fi
	@if [ -f $(SRC_DIR)/userland/games/doom/doom1.wad ]; then cp $(SRC_DIR)/userland/games/doom/doom1.wad $(BUILD_DIR)/initrd/Library/DOOM/; fi
	@for f in $$(find docs -name '*.md' 2>/dev/null); do \
		if [ -f "$$f" ]; then \
			dir=$$(dirname "$$f"); \
			mkdir -p $(BUILD_DIR)/initrd/"$$dir"; \
			cp "$$f" $(BUILD_DIR)/initrd/"$$dir"/; \
		fi \
	done
	@if [ -f README.md ]; then cp README.md $(BUILD_DIR)/initrd/; fi
	@if [ -f LICENSE ]; then cp LICENSE $(BUILD_DIR)/initrd/; fi
	@if [ -f limine.conf ]; then cp limine.conf $(BUILD_DIR)/initrd/; fi
	
	cd $(BUILD_DIR)/initrd && COPYFILE_DISABLE=1 tar --exclude="._*" -cf ../initrd.tar *

$(ISO_IMAGE): $(KERNEL_ELF) $(BUILD_DIR)/initrd.tar limine.conf limine-setup
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)
	mkdir -p $(ISO_DIR)/EFI/BOOT
	
	cp $(KERNEL_ELF) $(ISO_DIR)/
	cp limine.conf $(ISO_DIR)/
	
	cp $(BUILD_DIR)/initrd.tar $(ISO_DIR)/
	echo "    module_path: boot():/initrd.tar" >> $(ISO_DIR)/limine.conf
	
	@if [ -f splash.jpg ]; then cp splash.jpg $(ISO_DIR)/; fi
	
	cp limine/limine-bios.sys $(ISO_DIR)/
	cp limine/limine-bios-cd.bin $(ISO_DIR)/
	cp limine/limine-uefi-cd.bin $(ISO_DIR)/
	
	cp limine/BOOTX64.EFI $(ISO_DIR)/EFI/BOOT/
	cp limine/BOOTIA32.EFI $(ISO_DIR)/EFI/BOOT/

	$(XORRISO) -as mkisofs -R -J -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $(ISO_IMAGE)
	
	./limine/limine bios-install $(ISO_IMAGE)

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO_IMAGE)
	$(MAKE) -C $(SRC_DIR)/userland clean

run-windows: $(ISO_IMAGE)
	qemu-system-x86_64 -m 4G -serial stdio -cdrom $< -boot d \
	    -smp 4 \
		-audiodev dsound,id=audio0 -machine pcspk-audiodev=audio0 \
		-vga std -global VGA.xres=1920 -global VGA.yres=1080 \
		-drive file=disk.img,format=raw,file.locking=off 
run-mac: $(ISO_IMAGE)
	qemu-system-x86_64 -m 4G -serial stdio -cdrom $< -boot d \
	    -smp 4 \
		-audiodev coreaudio,id=audio0 -machine pcspk-audiodev=audio0 \
		-vga std -global VGA.xres=1920 -global VGA.yres=1080 \
		-display cocoa,show-cursor=off \
		-drive file=disk.img,format=raw,file.locking=off \
		-cpu max
run-linux: $(ISO_IMAGE)
	qemu-system-x86_64 -m 4G -serial stdio -cdrom $< -boot d \
	    -smp 4 \
		-audiodev pa,id=audio0 -machine pcspk-audiodev=audio0 \
		-vga std -global VGA.xres=1920 -global VGA.yres=1080 \
		-display gtk,show-cursor=off \
		-drive file=disk.img,format=raw,file.locking=off \
		-cpu max

