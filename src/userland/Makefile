# Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
# This software is released under the GNU General Public License v3.0. See LICENSE file for details.
# This header needs to maintain in any file it is present in, as per the GPL license terms.
CC  = x86_64-elf-gcc
AS  = nasm
LD  = x86_64-elf-ld
STRIP = x86_64-elf-strip

CFLAGS  = -Wall -Wextra -std=gnu11 -ffreestanding -O2 -fno-stack-protector \
          -fno-stack-check -fno-lto -fno-pie -m64 -march=x86-64 -mno-red-zone \
          -U__INT_FAST8_TYPE__ -D__INT_FAST8_TYPE__="signed char" \
          -U__UINT_FAST8_TYPE__ -D__UINT_FAST8_TYPE__="unsigned char" \
          -U__INT_FAST16_TYPE__ -D__INT_FAST16_TYPE__="long int" \
          -U__UINT_FAST16_TYPE__ -D__UINT_FAST16_TYPE__="long unsigned int" \
          -U__INT_FAST32_TYPE__ -D__INT_FAST32_TYPE__="long int" \
          -U__UINT_FAST32_TYPE__ -D__UINT_FAST32_TYPE__="long unsigned int" \
          -D_GNU_SOURCE -D_DEFAULT_SOURCE \
          -isystem mlibc/build/mlibc-headers/usr/include -I. -Ilibboredos -I../sys -I../wm
LDFLAGS = -m elf_x86_64 -nostdlib -static -no-pie -Ttext=0x40000000 \
          --no-dynamic-linker -z text -z max-page-size=0x1000 -e _start

BIN_DIR            = bin
SDK_DIR            = sdk
APP_METADATA_TOOL  = ../../tools/gen_userland_note.sh
APP_ICON_SOURCE_DIR= ../images/icons/colloid
APP_METADATA_SOURCE_DOOM = games/doom/doomgeneric_boredos.c
APP_METADATA_SOURCE_LUA  = cli/third_party/lua/boredos_onelua.c
APP_SOURCE_DIRS    = . cli sys net cli/third_party

# ---------------------------------------------------------------------------
# mlibc paths
# ---------------------------------------------------------------------------
MLIBC_DIR        = mlibc
MLIBC_SYSDEP     = $(MLIBC_DIR)/sysdeps/boredos
MLIBC_CROSS_FILE = $(MLIBC_SYSDEP)/boredos-x86_64.cross
MLIBC_BUILD_DIR  = $(MLIBC_DIR)/build
MLIBC_LIB        = $(MLIBC_BUILD_DIR)/libc.a
MLIBC_CRT1       = $(MLIBC_BUILD_DIR)/sysdeps/boredos/crt1.o

# ---------------------------------------------------------------------------
# libboredos: BoredOS-specific API (syscall wrappers, network, disk helpers)
# Kept separate from the standard C library.
# ---------------------------------------------------------------------------
BOREDOS_SRCS = libboredos/syscall.c libboredos/utf-8.c
BOREDOS_OBJS = $(patsubst libboredos/%.c, $(BIN_DIR)/boredos_%.o, $(BOREDOS_SRCS))

# ---------------------------------------------------------------------------
# App source discovery
# ---------------------------------------------------------------------------
define app_source_for
$(if $(filter doom,$1),$(APP_METADATA_SOURCE_DOOM),$(if $(filter lua,$1),$(APP_METADATA_SOURCE_LUA),$(if $(filter tcc,$1),cli/third_party/tcc/tcc.c,$(firstword $(foreach d,$(APP_SOURCE_DIRS),$(wildcard $(d)/$1.c))))))
endef

VPATH = cli sys libboredos net cli/third_party
vpath %.c cli sys libboredos net cli/third_party

APP_SOURCES_FULL = $(wildcard cli/*.c sys/*.c *.c net/*.c cli/third_party/*.c)
APP_SOURCES      = $(filter-out stb_image.c, $(APP_SOURCES_FULL))
APP_ELFS         = $(patsubst %.c, $(BIN_DIR)/%.elf, $(notdir $(APP_SOURCES)))
APP_NAMES        = $(sort $(basename $(notdir $(APP_SOURCES))) lua)

APP_NOTE_CSOURCES= $(patsubst %, $(BIN_DIR)/%.note.c, $(APP_NAMES))
APP_NOTE_OBJS    = $(patsubst %, $(BIN_DIR)/%.note.o, $(APP_NAMES))

DOOM_SOURCES     = $(wildcard games/doom/*.c)
DOOM_OBJS        = $(patsubst games/doom/%.c, $(BIN_DIR)/%.o, $(DOOM_SOURCES))

LUA_DIR    = cli/third_party/lua
LUA_CFLAGS = -std=gnu11 -ffreestanding -O2 -fno-stack-protector -fno-stack-check \
             -fno-lto -fno-pie -m64 -march=x86-64 -mno-red-zone \
             -U__INT_FAST8_TYPE__ -D__INT_FAST8_TYPE__="signed char" \
             -U__UINT_FAST8_TYPE__ -D__UINT_FAST8_TYPE__="unsigned char" \
             -U__INT_FAST16_TYPE__ -D__INT_FAST16_TYPE__="long int" \
             -U__UINT_FAST16_TYPE__ -D__UINT_FAST16_TYPE__="long unsigned int" \
             -U__INT_FAST32_TYPE__ -D__INT_FAST32_TYPE__="long int" \
             -U__UINT_FAST32_TYPE__ -D__UINT_FAST32_TYPE__="long unsigned int" \
             -D_GNU_SOURCE -D_DEFAULT_SOURCE \
             -isystem mlibc/build/mlibc-headers/usr/include -I. -Ilibboredos -I$(LUA_DIR) -DLUA_USE_C89 \
             -Wno-conversion -Wno-sign-conversion -Wno-double-promotion \
             -Wno-unused-parameter -Wno-missing-declarations -Wno-shadow -Wno-undef \
             -Wno-redundant-decls -Wno-old-style-definition -Wno-missing-prototypes \
             -Wno-implicit-fallthrough -Wno-type-limits

# Link deps every app needs: mlibc's crt1.o, crti/crtn stubs, libboredos, libc
APP_LINK_DEPS = $(MLIBC_CRT1) $(BIN_DIR)/crti.o $(BIN_DIR)/crtn.o \
                $(SDK_DIR)/lib/libboredos.a $(MLIBC_LIB)

# ---------------------------------------------------------------------------
# PDCurses/Ncurses Build Configuration
# ---------------------------------------------------------------------------
PDCURSES_DIR  = cli/third_party/pdcurses
PDC_CFLAGS    = $(CFLAGS) -I$(PDCURSES_DIR) -DPDC_WIDE -DPDC_FORCE_UTF8 \
                -Wno-unused-parameter -Wno-missing-prototypes -Wno-strict-prototypes \
                -Wno-shadow -Wno-conversion -Wno-sign-conversion -Wno-double-promotion \
                -Wno-missing-declarations -Wno-implicit-fallthrough -Wno-redundant-decls

PDC_CORE_SRCS = $(wildcard $(PDCURSES_DIR)/pdcurses/*.c)
PDC_VT_SRCS   = $(wildcard $(PDCURSES_DIR)/vt/*.c)
PDC_OBJS      = $(patsubst $(PDCURSES_DIR)/pdcurses/%.c, $(BIN_DIR)/pdcurses_core_%.o, $(PDC_CORE_SRCS)) \
                $(patsubst $(PDCURSES_DIR)/vt/%.c, $(BIN_DIR)/pdcurses_vt_%.o, $(PDC_VT_SRCS))

all: $(BIN_DIR) $(MLIBC_LIB) $(SDK_DIR)/lib/libboredos.a $(SDK_DIR)/lib/libncurses.a $(APP_ELFS) \
     $(BIN_DIR)/lua.elf $(BIN_DIR)/tcc.elf $(BIN_DIR)/empty.o


# ---------------------------------------------------------------------------
# mlibc build — meson configure (once) + ninja -j10
# ---------------------------------------------------------------------------
$(MLIBC_LIB): $(MLIBC_CROSS_FILE)
	@echo "[MLIBC] Checking meson configuration..."
	@libgcc_path="$$( $(CC) -print-libgcc-file-name )"; \
	if [ ! -f "$$libgcc_path" ]; then \
		echo "[MLIBC] error: libgcc.a missing (got '$$libgcc_path')."; \
		echo "[MLIBC] hint: rebuild/install your cross toolchain with target libgcc."; \
		echo "[MLIBC] e.g. 'make all-gcc all-target-libgcc && make install-gcc install-target-libgcc'"; \
		exit 1; \
	fi
	@if [ ! -f $(MLIBC_BUILD_DIR)/build.ninja ]; then \
		echo "[MLIBC] Running meson setup..."; \
		meson setup \
			--cross-file=$(MLIBC_CROSS_FILE) \
			--prefix=/usr \
			-Ddefault_library=static \
			-Dno_headers=false \
			-Dposix_option=enabled \
			-Dglibc_option=disabled \
			-Dlinux_option=disabled \
			-Dbsd_option=disabled \
			$(MLIBC_BUILD_DIR) $(MLIBC_DIR); \
	fi
	@echo "[MLIBC] Building with ninja -j10..."
	ninja -C $(MLIBC_BUILD_DIR) -j10
	@echo "[MLIBC] Installing headers and libs to mlibc-headers..."
	DESTDIR=$(CURDIR)/mlibc/build/mlibc-headers ninja -C $(MLIBC_BUILD_DIR) install
	@echo "[MLIBC] Done: $(MLIBC_LIB)"

$(MLIBC_CRT1): $(MLIBC_LIB)
	@:

# ---------------------------------------------------------------------------
# Directories and utility objects
# ---------------------------------------------------------------------------
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# crti/crtn: reuse the existing empty NASM stubs (just section headers)
$(BIN_DIR)/crti.o: crti.asm | $(BIN_DIR)
	$(AS) -f elf64 $< -o $@

$(BIN_DIR)/crtn.o: crtn.asm | $(BIN_DIR)
	$(AS) -f elf64 $< -o $@

$(BIN_DIR)/empty.o: | $(BIN_DIR)
	echo "section .text" > $(BIN_DIR)/empty.asm
	$(AS) -f elf64 $(BIN_DIR)/empty.asm -o $@
	rm $(BIN_DIR)/empty.asm

$(BIN_DIR)/stb_image.o: stb_image.c | $(BIN_DIR) $(MLIBC_LIB) $(SDK_DIR)/lib/libboredos.a
	$(CC) $(CFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# libboredos.a — BoredOS platform API (syscall.c, network, disk, etc.)
# ---------------------------------------------------------------------------
$(BIN_DIR)/boredos_%.o: libboredos/%.c | $(BIN_DIR) $(MLIBC_LIB)
	$(CC) $(CFLAGS) -c $< -o $@

$(SDK_DIR)/lib/libboredos.a: $(BOREDOS_OBJS)
	mkdir -p $(SDK_DIR)/lib $(SDK_DIR)/include/sys $(SDK_DIR)/include/linux
	cp libboredos/*.h $(SDK_DIR)/include/
	cp libboredos/sys/*.h $(SDK_DIR)/include/sys/
	cp libboredos/linux/*.h $(SDK_DIR)/include/linux/
	x86_64-elf-ar rcs $@ $(BOREDOS_OBJS)

# ---------------------------------------------------------------------------
# libncurses.a & libcurses.a — PDCurses/Ncurses library
# ---------------------------------------------------------------------------
$(BIN_DIR)/pdcurses_core_%.o: $(PDCURSES_DIR)/pdcurses/%.c | $(BIN_DIR) $(MLIBC_LIB)
	$(CC) $(PDC_CFLAGS) -c $< -o $@

$(BIN_DIR)/pdcurses_vt_%.o: $(PDCURSES_DIR)/vt/%.c | $(BIN_DIR) $(MLIBC_LIB)
	$(CC) $(PDC_CFLAGS) -c $< -o $@

$(SDK_DIR)/lib/libncurses.a: $(PDC_OBJS)
	mkdir -p $(SDK_DIR)/lib $(SDK_DIR)/include
	cp $(PDCURSES_DIR)/curses.h $(SDK_DIR)/include/curses.h
	cp $(PDCURSES_DIR)/curses.h $(SDK_DIR)/include/ncurses.h
	cp $(PDCURSES_DIR)/panel.h $(SDK_DIR)/include/panel.h
	cp $(PDCURSES_DIR)/term.h $(SDK_DIR)/include/term.h
	x86_64-elf-ar rcs $@ $(PDC_OBJS)
	cp $@ $(SDK_DIR)/lib/libcurses.a

sdk: $(SDK_DIR)/lib/libboredos.a $(SDK_DIR)/lib/libncurses.a

# ---------------------------------------------------------------------------
# App metadata notes
# ---------------------------------------------------------------------------
$(BIN_DIR)/%.note.c: $(APP_METADATA_TOOL) | $(BIN_DIR)
	src="$(call app_source_for,$*)"; \
	if [ -z "$$src" ] || [ ! -f "$$src" ]; then \
		echo "error: metadata source not found for app '$*'" >&2; \
		exit 1; \
	fi; \
	sh $(APP_METADATA_TOOL) "$*" "$$src" "$(APP_ICON_SOURCE_DIR)" "$@"

$(BIN_DIR)/%.note.o: $(BIN_DIR)/%.note.c | $(MLIBC_LIB) sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/%.o: %.c | $(BIN_DIR) $(MLIBC_LIB) sdk
	$(CC) $(CFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# App linking
# Order: crt1.o(mlibc) crti.o  app.o note.o  -lboredos -lc  crtn.o
# ---------------------------------------------------------------------------
APP_LD = $(LD) $(LDFLAGS) $(MLIBC_CRT1) $(BIN_DIR)/crti.o

$(BIN_DIR)/viewer.elf: $(BIN_DIR)/viewer.o $(BIN_DIR)/viewer.note.o $(APP_LINK_DEPS)
	$(APP_LD) $< $(BIN_DIR)/viewer.note.o -L$(SDK_DIR)/lib -lboredos -L$(MLIBC_BUILD_DIR) -lc $(BIN_DIR)/crtn.o -o $@
	$(STRIP) $@

$(BIN_DIR)/about.elf: $(BIN_DIR)/about.o $(BIN_DIR)/about.note.o $(APP_LINK_DEPS)
	$(APP_LD) $< $(BIN_DIR)/about.note.o -L$(SDK_DIR)/lib -lboredos -L$(MLIBC_BUILD_DIR) -lc $(BIN_DIR)/crtn.o -o $@
	$(STRIP) $@

$(BIN_DIR)/settings.elf: $(BIN_DIR)/settings.o $(BIN_DIR)/settings.note.o $(APP_LINK_DEPS)
	$(APP_LD) $< $(BIN_DIR)/settings.note.o -L$(SDK_DIR)/lib -lboredos -L$(MLIBC_BUILD_DIR) -lc $(BIN_DIR)/crtn.o -o $@
	$(STRIP) $@

$(BIN_DIR)/browser.elf: $(BIN_DIR)/browser.o $(BIN_DIR)/browser.note.o $(APP_LINK_DEPS)
	$(APP_LD) $< $(BIN_DIR)/browser.note.o -L$(SDK_DIR)/lib -lboredos -L$(MLIBC_BUILD_DIR) -lc $(BIN_DIR)/crtn.o -o $@
	$(STRIP) $@

$(BIN_DIR)/screenshot.elf: $(BIN_DIR)/screenshot.o $(BIN_DIR)/screenshot.note.o $(APP_LINK_DEPS)
	$(APP_LD) $< $(BIN_DIR)/screenshot.note.o -L$(SDK_DIR)/lib -lboredos -L$(MLIBC_BUILD_DIR) -lc $(BIN_DIR)/crtn.o -o $@
	$(STRIP) $@

$(BIN_DIR)/%.o: games/doom/%.c | $(BIN_DIR) $(MLIBC_LIB) sdk
	$(CC) $(CFLAGS) -Wno-error -DBOREDOS -Igames/doom -c $< -o $@

$(BIN_DIR)/doom.elf: $(DOOM_OBJS) $(BIN_DIR)/doom.note.o $(APP_LINK_DEPS)
	$(APP_LD) $(DOOM_OBJS) $(BIN_DIR)/doom.note.o -L$(SDK_DIR)/lib -lboredos -L$(MLIBC_BUILD_DIR) -lc $(BIN_DIR)/crtn.o -o $@
	$(STRIP) $@

$(BIN_DIR)/cursestest.o: cli/cursestest.c | $(BIN_DIR) $(MLIBC_LIB) sdk
	$(CC) $(CFLAGS) -Isdk/include -DPDC_WIDE -DPDC_FORCE_UTF8 -c $< -o $@

$(BIN_DIR)/cursestest.elf: $(BIN_DIR)/cursestest.o $(BIN_DIR)/cursestest.note.o $(APP_LINK_DEPS) $(SDK_DIR)/lib/libncurses.a
	$(APP_LD) $(BIN_DIR)/cursestest.o $(BIN_DIR)/cursestest.note.o -L$(SDK_DIR)/lib -lncurses -lboredos -L$(MLIBC_BUILD_DIR) -lc $(BIN_DIR)/crtn.o -o $@
	$(STRIP) $@

$(BIN_DIR)/%.elf: $(BIN_DIR)/%.o $(BIN_DIR)/%.note.o $(APP_LINK_DEPS)
	$(APP_LD) $< $(BIN_DIR)/$*.note.o -L$(SDK_DIR)/lib -lboredos -L$(MLIBC_BUILD_DIR) -lc $(BIN_DIR)/crtn.o -o $@
	$(STRIP) $@

# ---------------------------------------------------------------------------
# Lua
# ---------------------------------------------------------------------------
$(BIN_DIR)/lua_onelua.o: $(LUA_DIR)/boredos_onelua.c | $(BIN_DIR) $(MLIBC_LIB) sdk
	$(CC) $(LUA_CFLAGS) -c $< -o $@

$(BIN_DIR)/lua.elf: $(BIN_DIR)/lua_onelua.o $(BIN_DIR)/lua.note.o $(APP_LINK_DEPS)
	$(APP_LD) $< $(BIN_DIR)/lua.note.o -L$(SDK_DIR)/lib -lboredos -L$(MLIBC_BUILD_DIR) -lc $(BIN_DIR)/crtn.o -o $@
	$(STRIP) $@

# ---------------------------------------------------------------------------
# TCC — still links against libboredos only (its own internal libc)
# ---------------------------------------------------------------------------
TCC_DIR = cli/third_party/tcc
$(BIN_DIR)/tcc.o: $(TCC_DIR)/tcc.c $(TCC_DIR)/config.h $(TCC_DIR)/tcc.h | $(BIN_DIR) $(MLIBC_LIB) $(SDK_DIR)/lib/libboredos.a
	$(CC) $(CFLAGS) -DONE_SOURCE=1 -DTARGETOS_BoredOS=1 -I$(TCC_DIR) -c $< -o $@

$(BIN_DIR)/tcc.elf: $(BIN_DIR)/tcc.o $(BIN_DIR)/tcc.note.o $(SDK_DIR)/lib/libboredos.a $(APP_LINK_DEPS)
	$(APP_LD) $(BIN_DIR)/tcc.o $(BIN_DIR)/tcc.note.o -L$(SDK_DIR)/lib -lboredos -L$(MLIBC_BUILD_DIR) -lc $(BIN_DIR)/crtn.o -o $@
	$(STRIP) $@
	cp $(BIN_DIR)/tcc.elf $(TCC_DIR)/tcc.elf
	cd $(TCC_DIR) && sh build_libtcc1.sh

# ---------------------------------------------------------------------------
clean:
	rm -rf $(BIN_DIR) $(SDK_DIR)
	@if [ -d $(MLIBC_BUILD_DIR) ]; then \
		echo "[CLEAN] Removing mlibc build dir..."; \
		rm -rf $(MLIBC_BUILD_DIR); \
	fi

