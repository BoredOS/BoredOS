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

BLUE  = \033[1;34m
GREEN = \033[1;32m
YELLOW= \033[1;33m
RESET = \033[0m

define PRINT_STEP
	@printf ""
	@printf "\n$(BLUE)============================================================$(RESET)\n"
	@printf "$(BLUE)== %s$(RESET)\n" "$(1)"
	@printf "$(BLUE)============================================================$(RESET)\n"
endef

DOCK_COLLOID_ICONS = $(shell sed -n 's/^[[:space:]]*{"\([^"]*\.png\)",[[:space:]]*DOCK_ICON_UNTRIED.*/\1/p' $(SRC_DIR)/wm/wm.c)
USERLAND_COLLOID_ICONS = $(shell { \
	find $(SRC_DIR)/userland -type f -name '*.c' ! -path '*/third_party/*' -exec grep -hoE '"[^"]+\.png"' {} + 2>/dev/null; \
	find $(SRC_DIR)/userland -type f -name '*.h' ! -path '*/third_party/*' ! -name 'stb_image.h' -exec grep -hoE '"[^"]+\.png"' {} + 2>/dev/null; \
} | sed 's/"//g' | sed 's@.*/@@' | sort -u)
USERLAND_METADATA_ICONS = $(shell { \
	find $(SRC_DIR)/userland -type f -name '*.c' -exec sed -n 's@^[[:space:]]*//[[:space:]]*BOREDOS_APP_ICONS:[[:space:]]*@@p' {} + 2>/dev/null; \
} | tr ';' '\n' | sed 's@.*/@@' | sed '/^[[:space:]]*$$/d' | sort -u)
COLLOID_ICONS = $(sort $(DOCK_COLLOID_ICONS) $(USERLAND_COLLOID_ICONS) $(USERLAND_METADATA_ICONS) xterm.png)

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

.PHONY: all clean run limine-setup run-windows run-mac run-linux

all:
	$(call PRINT_STEP,STARTING BOREDOS BUILD)
	$(MAKE) $(ISO_IMAGE)
	$(call PRINT_STEP,BUILD COMPLETE)

$(BUILD_DIR):
	$(call PRINT_STEP,CREATING BUILD DIRECTORY)
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)

limine-setup:
	$(call PRINT_STEP,SETTING UP LIMINE)
	@if [ ! -f limine/limine-bios.sys ]; then \
		printf "$(YELLOW)[LIMINE] Limine binaries missing or invalid. Cloning v$(LIMINE_VERSION)-binary...$(RESET)"; \
		rm -rf limine; \
		git clone https://github.com/limine-bootloader/limine.git --branch=v$(LIMINE_VERSION)-binary --depth=1 limine; \
	else \
		printf "$(YELLOW)[LIMINE] Existing Limine binaries found.$(RESET)"; \
	fi
	@if [ ! -f $(SRC_DIR)/core/limine.h ]; then \
		printf "$(YELLOW)[LIMINE] Copying limine.h...$(RESET)"; \
		cp limine/limine.h $(SRC_DIR)/core/limine.h; \
	else \
		printf "$(YELLOW)[LIMINE] limine.h already present.$(RESET)"; \
	fi
	@printf "$(YELLOW)[LIMINE] Building Limine host utility...$(RESET)"
	$(MAKE) -C limine
	@printf "$(GREEN)[OK] Limine setup complete.$(RESET)"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR) limine-setup
	@printf "$(YELLOW)[CC]$(RESET) $< -> $@"
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/core/%.c | $(BUILD_DIR) limine-setup
	@printf "$(YELLOW)[CC]$(RESET)[core] $< -> $@"
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/sys/%.c | $(BUILD_DIR) limine-setup
	@printf "$(YELLOW)[CC]$(RESET)[sys] $< -> $@"
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/mem/%.c | $(BUILD_DIR) limine-setup
	@printf "$(YELLOW)[CC]$(RESET)[mem] $< -> $@"
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/dev/%.c | $(BUILD_DIR) limine-setup
	@printf "$(YELLOW)[CC]$(RESET)[dev] $< -> $@"
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	
$(BUILD_DIR)/%.o: $(SRC_DIR)/input/%.c | $(BUILD_DIR) limine-setup
	@printf "$(YELLOW)[CC]$(RESET)[input] $< -> $@"
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/net/%.c | $(BUILD_DIR) limine-setup
	@printf "$(YELLOW)[CC]$(RESET)[net] $< -> $@"
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/net/nic/%.c | $(BUILD_DIR) limine-setup
	@printf "$(YELLOW)[CC]$(RESET)[net/nic] $< -> $@"
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/fs/%.c | $(BUILD_DIR) limine-setup
	@printf "$(YELLOW)[CC]$(RESET)[fs] $< -> $@"
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/wm/%.c | $(BUILD_DIR) limine-setup
	@printf "$(YELLOW)[CC]$(RESET)[wm] $< -> $@"
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lwip/%.o: $(SRC_DIR)/net/third_party/lwip/%.c | $(BUILD_DIR) limine-setup
	@printf "$(YELLOW)[CC]$(RESET)[lwIP] $< -> $@"
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/arch/%.asm | $(BUILD_DIR)
	@printf "$(YELLOW)[ASM]$(RESET) $< -> $@"
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/test_syscall.o: $(SRC_DIR)/arch/test_syscall.asm | $(BUILD_DIR)
	@printf "$(YELLOW)[ASM][test_syscall]$(RESET) $< -> $@"
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/user_test.o: $(SRC_DIR)/arch/user_test.asm | $(BUILD_DIR)
	@printf "$(YELLOW)[ASM][user_test]$(RESET) $< -> $@"
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/process_asm.o: $(SRC_DIR)/arch/process_asm.asm | $(BUILD_DIR)
	@printf "$(YELLOW)[ASM][process]$(RESET) $< -> $@"
	$(NASM) $(NASMFLAGS) $< -o $@

$(KERNEL_ELF): $(OBJ_FILES)
	$(call PRINT_STEP,LINKING KERNEL)
	@printf "$(YELLOW)[LD]$(RESET) Linking kernel ELF: $@"
	$(LD) $(LDFLAGS) -o $@ $(OBJ_FILES)
	@printf "$(GREEN)[OK]$(RESET) Kernel ELF built: $@"
	$(call PRINT_STEP,BUILDING USERLAND)
	$(MAKE) -C $(SRC_DIR)/userland
	@printf "$(GREEN)[OK]$(RESET) Userland build complete."

$(BUILD_DIR)/initrd.tar: $(KERNEL_ELF)
	$(call PRINT_STEP,BUILDING INITRD)
	@printf "$(YELLOW)[INITRD]$(RESET) Cleaning previous initrd directory..."
	rm -rf $(BUILD_DIR)/initrd

	@printf "$(YELLOW)[INITRD]$(RESET) Creating directory structure..."
	mkdir -p $(BUILD_DIR)/initrd/bin
	mkdir -p $(BUILD_DIR)/initrd/Library/images/Wallpapers
	mkdir -p $(BUILD_DIR)/initrd/Library/images/gif
	mkdir -p $(BUILD_DIR)/initrd/Library/images/icons/colloid
	mkdir -p $(BUILD_DIR)/initrd/Library/Fonts/Emoji
	mkdir -p $(BUILD_DIR)/initrd/Library/DOOM
	mkdir -p $(BUILD_DIR)/initrd/Library/conf
	mkdir -p $(BUILD_DIR)/initrd/Library/bsh
	mkdir -p $(BUILD_DIR)/initrd/Library/BWM/Wallpaper
	mkdir -p $(BUILD_DIR)/initrd/docs
	mkdir -p $(BUILD_DIR)/initrd/boot
	mkdir -p $(BUILD_DIR)/initrd/mnt
	mkdir -p $(BUILD_DIR)/initrd/dev
	mkdir -p $(BUILD_DIR)/initrd/root/Desktop
	mkdir -p $(BUILD_DIR)/initrd/root/Pictures
	mkdir -p $(BUILD_DIR)/initrd/root/Documents
	mkdir -p $(BUILD_DIR)/initrd/root/Downloads
	mkdir -p $(BUILD_DIR)/initrd/etc
	mkdir -p $(BUILD_DIR)/initrd/usr/lib/tcc/include
	mkdir -p $(BUILD_DIR)/initrd/usr/local/include
	mkdir -p $(BUILD_DIR)/initrd/usr/include/sys
	mkdir -p $(BUILD_DIR)/initrd/usr/include/libc
	mkdir -p $(BUILD_DIR)/initrd/usr/lib

	@printf "$(YELLOW)[COPY]$(RESET) Limine binaries + kernel for installer..."
	@if [ -f limine/BOOTX64.EFI ];     then cp limine/BOOTX64.EFI    $(BUILD_DIR)/initrd/boot/; fi
	@if [ -f limine/BOOTIA32.EFI ];    then cp limine/BOOTIA32.EFI   $(BUILD_DIR)/initrd/boot/; fi
	@if [ -f limine/limine-bios.sys ]; then cp limine/limine-bios.sys $(BUILD_DIR)/initrd/boot/; fi
	@cp $(KERNEL_ELF) $(BUILD_DIR)/initrd/boot/boredos.elf

	@printf "$(YELLOW)[COPY]$(RESET) Userland binaries..."
	@for f in $(SRC_DIR)/userland/bin/*.elf; do \
		if [ -f "$$f" ]; then \
			printf "  -> $$f"; \
			cp "$$f" $(BUILD_DIR)/initrd/bin/; \
		fi \
	done

	@printf "$(YELLOW)[COPY]$(RESET) TCC support files..."
	@cp $(SRC_DIR)/userland/cli/third_party/tcc/libtcc1.a $(BUILD_DIR)/initrd/usr/lib/tcc/
	@cp $(SRC_DIR)/userland/cli/third_party/tcc/libtcc1.a $(BUILD_DIR)/initrd/usr/lib/
	@cp $(SRC_DIR)/userland/cli/third_party/tcc/include/*.h $(BUILD_DIR)/initrd/usr/lib/tcc/include/
	@cp $(SRC_DIR)/userland/sdk/lib/libboredos.a $(BUILD_DIR)/initrd/usr/lib/
	@cp $(SRC_DIR)/userland/sdk/lib/libc.a $(BUILD_DIR)/initrd/usr/lib/
	@cp $(SRC_DIR)/userland/sdk/lib/libm.a $(BUILD_DIR)/initrd/usr/lib/
	@cp $(SRC_DIR)/userland/bin/crt0.o $(BUILD_DIR)/initrd/usr/lib/crt0.o
	@cp $(SRC_DIR)/userland/bin/crt0.o $(BUILD_DIR)/initrd/usr/lib/crt1.o
	@cp $(SRC_DIR)/userland/bin/empty.o $(BUILD_DIR)/initrd/usr/lib/crti.o
	@cp $(SRC_DIR)/userland/bin/empty.o $(BUILD_DIR)/initrd/usr/lib/crtn.o
	@cp $(SRC_DIR)/userland/libc/*.h $(BUILD_DIR)/initrd/usr/include/
	@cp $(SRC_DIR)/userland/libc/sys/*.h $(BUILD_DIR)/initrd/usr/include/sys/
	@cp $(SRC_DIR)/userland/libc/*.h $(BUILD_DIR)/initrd/usr/include/libc/
	@cp $(SRC_DIR)/userland/libc/*.h $(BUILD_DIR)/initrd/usr/local/include/

	@printf "$(YELLOW)[COPY]$(RESET) Wallpapers..."
	@for f in $(SRC_DIR)/images/wallpapers/*; do \
		if [ -f "$$f" ]; then \
			printf "  -> $$f"; \
			cp "$$f" $(BUILD_DIR)/initrd/Library/images/Wallpapers/; \
		fi \
	done

	@printf "$(YELLOW)[COPY]$(RESET) GIF assets..."
	@for f in $(SRC_DIR)/images/gif/*.gif; do \
		if [ -f "$$f" ]; then \
			printf "  -> $$f"; \
			cp "$$f" $(BUILD_DIR)/initrd/Library/images/gif/; \
		fi \
	done

	@printf "$(YELLOW)[COPY]$(RESET) Colloid icons..."
	@for f in $(COLLOID_ICONS); do \
		src="$(SRC_DIR)/images/icons/colloid/$$f"; \
		if [ -f "$$src" ]; then \
			printf "  -> $$src"; \
			cp "$$src" $(BUILD_DIR)/initrd/Library/images/icons/colloid/; \
		fi \
	done

	@printf "$(YELLOW)[COPY]$(RESET) Fonts..."
	@for f in $(SRC_DIR)/fonts/*.ttf; do \
		if [ -f "$$f" ]; then \
			printf "  -> $$f"; \
			cp "$$f" $(BUILD_DIR)/initrd/Library/Fonts/; \
		fi \
	done

	@printf "$(YELLOW)[COPY]$(RESET) Emoji fonts..."
	@for f in $(SRC_DIR)/fonts/Emoji/*.ttf; do \
		if [ -f "$$f" ]; then \
			printf "  -> $$f"; \
			cp "$$f" $(BUILD_DIR)/initrd/Library/Fonts/Emoji/; \
		fi \
	done

	@printf "$(YELLOW)[COPY]$(RESET) bsh configuration..."
	@if [ -f $(SRC_DIR)/library/bsh/bshrc ]; then printf "  -> bshrc"; cp $(SRC_DIR)/library/bsh/bshrc $(BUILD_DIR)/initrd/Library/bsh/; fi
	@if [ -f $(SRC_DIR)/library/bsh/startup.bsh ]; then printf "  -> startup.bsh"; cp $(SRC_DIR)/library/bsh/startup.bsh $(BUILD_DIR)/initrd/Library/bsh/; fi
	@if [ -f $(SRC_DIR)/library/bsh/boot.bsh ]; then printf "  -> boot.bsh"; cp $(SRC_DIR)/library/bsh/boot.bsh $(BUILD_DIR)/initrd/Library/bsh/; fi

	@printf "$(YELLOW)[COPY]$(RESET) DOOM assets..."
	@if [ -f $(SRC_DIR)/userland/games/doom/doom1.wad ]; then printf "  -> doom1.wad"; cp $(SRC_DIR)/userland/games/doom/doom1.wad $(BUILD_DIR)/initrd/Library/DOOM/; fi

	@printf "$(YELLOW)[COPY]$(RESET) Documentation..."
	@for f in $$(find docs -name '*.md' 2>/dev/null); do \
		if [ -f "$$f" ]; then \
			printf "  -> $$f"; \
			dir=$$(dirname "$$f"); \
			mkdir -p $(BUILD_DIR)/initrd/"$$dir"; \
			cp "$$f" $(BUILD_DIR)/initrd/"$$dir"/; \
		fi \
	done

	@printf "$(YELLOW)[COPY]$(RESET) Root files..."
	@if [ -f README.md ]; then printf "  -> README.md"; cp README.md $(BUILD_DIR)/initrd/; fi
	@if [ -f LICENSE ]; then printf "  -> LICENSE"; cp LICENSE $(BUILD_DIR)/initrd/; fi
	@if [ -f limine.conf ]; then printf "  -> limine.conf"; cp limine.conf $(BUILD_DIR)/initrd/; fi
	@if [ -f $(SRC_DIR)/userland/gui/about.c ]; then printf "  -> about.c"; cp $(SRC_DIR)/userland/gui/about.c $(BUILD_DIR)/initrd/; fi
	
	@printf "$(YELLOW)[TAR]$(RESET) Creating initrd.tar..."
	cd $(BUILD_DIR)/initrd && COPYFILE_DISABLE=1 tar --exclude="._*" -cf ../initrd.tar *
	@printf "$(GREEN)[OK]$(RESET) Initrd created: $(BUILD_DIR)/initrd.tar"

$(ISO_IMAGE): $(KERNEL_ELF) $(BUILD_DIR)/initrd.tar limine.conf limine-setup
	$(call PRINT_STEP,CREATING ISO IMAGE)
	@printf "$(YELLOW)[ISO]$(RESET) Cleaning previous ISO root..."
	rm -rf $(ISO_DIR)

	@printf "$(YELLOW)[ISO]$(RESET) Creating ISO directory structure..."
	mkdir -p $(ISO_DIR)
	mkdir -p $(ISO_DIR)/EFI/BOOT
	
	@printf "$(YELLOW)[COPY]$(RESET) Kernel ELF..."
	cp $(KERNEL_ELF) $(ISO_DIR)/

	@printf "$(YELLOW)[COPY]$(RESET) Limine config..."
	cp limine.conf $(ISO_DIR)/
	
	@printf "$(YELLOW)[COPY]$(RESET) Initrd..."
	cp $(BUILD_DIR)/initrd.tar $(ISO_DIR)/

	@printf "$(YELLOW)[CONFIG]$(RESET) Adding initrd module path..."
	printf "    module_path: boot():/initrd.tar" >> $(ISO_DIR)/limine.conf
	
	@printf "$(YELLOW)[COPY]$(RESET) Optional splash image..."
	@if [ -f splash.jpg ]; then printf "  -> splash.jpg"; cp splash.jpg $(ISO_DIR)/; else printf "  -> no splash.jpg found"; fi
	
	@printf "$(YELLOW)[COPY]$(RESET) Limine boot files..."
	cp limine/limine-bios.sys $(ISO_DIR)/
	cp limine/limine-bios-cd.bin $(ISO_DIR)/
	cp limine/limine-uefi-cd.bin $(ISO_DIR)/
	
	@printf "$(YELLOW)[COPY]$(RESET) EFI bootloaders..."
	cp limine/BOOTX64.EFI $(ISO_DIR)/EFI/BOOT/
	cp limine/BOOTIA32.EFI $(ISO_DIR)/EFI/BOOT/

	$(call PRINT_STEP,GENERATING BOOTABLE ISO)
	$(XORRISO) -as mkisofs -R -J -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $(ISO_IMAGE)
	
	@printf "$(YELLOW)[LIMINE]$(RESET) Installing BIOS bootloader..."
	./limine/limine bios-install $(ISO_IMAGE)
	@printf "$(GREEN)[OK]$(RESET) ISO image ready: $(ISO_IMAGE)"

clean:
	$(call PRINT_STEP,CLEANING BUILD OUTPUT)
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO_IMAGE)
	$(MAKE) -C $(SRC_DIR)/userland clean
	@printf "$(GREEN)[OK]$(RESET) Clean complete."

disk.qcow2:
	$(call PRINT_STEP,CREATING 10GB EXPANDABLE DISK IMAGE)
	qemu-img create -f qcow2 disk.qcow2 10G

run-windows: $(ISO_IMAGE) disk.qcow2
	$(call PRINT_STEP,RUNNING BOREDOS IN QEMU ON WINDOWS)
	qemu-system-x86_64 -m 4G -serial stdio -cdrom $< -boot d \
	    -smp 4 \
		-audiodev dsound,id=audio0 -machine pcspk-audiodev=audio0 \
		-vga std -global VGA.xres=1920 -global VGA.yres=1080 \
		-drive file=disk.qcow2,format=qcow2,file.locking=off 

run-mac: $(ISO_IMAGE) disk.qcow2
	$(call PRINT_STEP,RUNNING BOREDOS IN QEMU ON MACOS)
	qemu-system-x86_64 -m 4G -serial stdio -cdrom $< -boot d \
	    -smp 4 \
		-audiodev coreaudio,id=audio0 -machine pcspk-audiodev=audio0 \
		-vga std -global VGA.xres=1920 -global VGA.yres=1080 \
		-display cocoa,show-cursor=off \
		-device ahci,id=ahci -drive file=disk.qcow2,format=qcow2,if=none,id=disk0 -device ide-hd,bus=ahci.0,drive=disk0 \
		-cpu max

OVMF_CODE := /opt/homebrew/share/qemu/edk2-x86_64-code.fd
OVMF_VARS_TMPL := /opt/homebrew/share/qemu/edk2-i386-vars.fd
OVMF_VARS := edk2-vars.fd

ifeq ($(shell test -f $(OVMF_CODE) && echo 1),)
    OVMF_CODE := /usr/local/share/qemu/edk2-x86_64-code.fd
    OVMF_VARS_TMPL := /usr/local/share/qemu/edk2-i386-vars.fd
endif

$(OVMF_VARS):
	@if [ -f $(OVMF_VARS_TMPL) ]; then \
		printf "$(YELLOW)[UEFI]$(RESET) Creating local NVRAM vars..."; \
		cp $(OVMF_VARS_TMPL) $(OVMF_VARS); \
	fi

run-hd: disk.qcow2 $(OVMF_VARS)
	$(call PRINT_STEP,BOOTING BOREDOS FROM HARD DRIVE)
	qemu-system-x86_64 -m 4G -serial stdio -boot c \
	    -smp 4 \
		-audiodev coreaudio,id=audio0 -machine pcspk-audiodev=audio0 \
		-vga std -global VGA.xres=1920 -global VGA.yres=1080 \
		-display cocoa,show-cursor=off \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=$(OVMF_VARS) \
		-device ahci,id=ahci \
		-drive file=disk.qcow2,format=qcow2,if=none,id=disk0 -device ide-hd,bus=ahci.0,drive=disk0 \
		-drive file=disk.img,format=raw,if=none,id=disk1 -device ide-hd,bus=ahci.1,drive=disk1 \
		-cpu max

run-linux: $(ISO_IMAGE) disk.qcow2
	$(call PRINT_STEP,RUNNING BOREDOS IN QEMU ON LINUX)
	qemu-system-x86_64 -m 4G -serial stdio -cdrom $< -boot d \
	    -smp 4 \
		-audiodev pa,id=audio0 -machine pcspk-audiodev=audio0 \
		-vga std -global VGA.xres=1920 -global VGA.yres=1080 \
		-display gtk,show-cursor=off \
		-device ahci,id=ahci -drive file=disk.qcow2,format=qcow2,if=none,id=disk0 -device ide-hd,bus=ahci.0,drive=disk0 \
		-cpu max
