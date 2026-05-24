# Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
# This software is released under the GNU General Public License v3.0. See LICENSE file for details.
# This header needs to maintain in any file it is present in, as per the GPL license terms.
CROSS_BIN := $(firstword $(dir $(shell command -v x86_64-elf-gcc 2>/dev/null)) $(if $(wildcard /home/linuxbrew/.linuxbrew/bin/x86_64-elf-gcc),/home/linuxbrew/.linuxbrew/bin/))
CC = $(CROSS_BIN)x86_64-elf-gcc
CXX = $(CROSS_BIN)x86_64-elf-g++
AS = nasm
LD = $(CROSS_BIN)x86_64-elf-ld
AR := $(if $(wildcard $(CROSS_BIN)x86_64-elf-ar),$(CROSS_BIN)x86_64-elf-ar,ar)

CFLAGS = -Wall -Wextra -std=gnu11 -ffreestanding -O2 -fno-stack-protector -fno-stack-check -fno-lto -fno-pie -m64 -march=x86-64 -mno-red-zone -Wa,--noexecstack -I. -Ilibc -I../sys -I../wm
CXXFLAGS = -Wall -Wextra -ffreestanding -O2 -fno-stack-protector -fno-stack-check -fno-lto -fno-pie -m64 -march=x86-64 -mno-red-zone -fno-exceptions -fno-rtti -Wa,--noexecstack -I. -Ilibc -I../sys -I../wm
LDFLAGS = -m elf_x86_64 -nostdlib -static -no-pie -Ttext=0x40000000 --no-dynamic-linker -z text -z max-page-size=0x1000 -e _start

.PHONY: all clean sdk

BIN_DIR = bin
REPO_ROOT = ../..
OPENTTD_UPSTREAM_DIR ?= $(REPO_ROOT)/src/userland/games/openttd/upstream-15.3
OPENTTD_UPSTREAM_BUILD ?= $(REPO_ROOT)/build/openttd-boredos-upstream-15.3
OPENTTD_UPSTREAM_BIN = $(OPENTTD_UPSTREAM_BUILD)/openttd
APP_METADATA_TOOL = ../../tools/gen_userland_note.sh
APP_ICON_SOURCE_DIR = ../images/icons/colloid
APP_METADATA_SOURCE_DOOM = games/doom/doomgeneric_boredos.c
APP_METADATA_SOURCE_LUA = cli/third_party/lua/boredos_onelua.c
APP_METADATA_SOURCE_OPENTTD = games/openttd/openttd_boredos_main.cpp
APP_SOURCE_DIRS = . cli gui sys games net cli/third_party

define app_source_for
$(if $(filter doom,$1),$(APP_METADATA_SOURCE_DOOM),$(if $(filter lua,$1),$(APP_METADATA_SOURCE_LUA),$(if $(filter openttd,$1),$(APP_METADATA_SOURCE_OPENTTD),$(if $(filter tcc,$1),cli/third_party/tcc/tcc.c,$(firstword $(foreach d,$(APP_SOURCE_DIRS),$(wildcard $(d)/$1.c)) $(wildcard games/$1.cpp))))))
endef

LIBC_SOURCES = $(wildcard libc/*.c)
LIBC_CXX_SOURCES = $(wildcard libc/*.cpp)
LIBC_OBJS = $(patsubst libc/%.c, $(BIN_DIR)/libc_%.o, $(LIBC_SOURCES)) $(patsubst libc/%.cpp, $(BIN_DIR)/libc_%.o, $(LIBC_CXX_SOURCES)) $(BIN_DIR)/crt0.o $(BIN_DIR)/libwidget.o $(BIN_DIR)/stb_image.o

VPATH = cli gui sys games libc net cli/third_party
vpath %.c cli gui sys games libc net cli/third_party
vpath %.cpp games

APP_C_SOURCES_FULL = $(wildcard cli/*.c gui/*.c sys/*.c games/*.c *.c net/*.c cli/third_party/*.c)
APP_C_SOURCES = $(filter-out stb_image.c, $(APP_C_SOURCES_FULL))
APP_C_NAMES = $(basename $(notdir $(APP_C_SOURCES)))
APP_CPP_SOURCES_FULL = $(wildcard games/*.cpp)
APP_CPP_SOURCES = $(filter-out $(foreach n,$(APP_C_NAMES),games/$(n).cpp),$(APP_CPP_SOURCES_FULL))
APP_CPP_NAMES = $(basename $(notdir $(APP_CPP_SOURCES)))
APP_ELFS = $(patsubst %, $(BIN_DIR)/%.elf, $(APP_C_NAMES) $(APP_CPP_NAMES))
APP_NAMES = $(sort $(APP_C_NAMES) $(APP_CPP_NAMES) doom lua)
APP_NOTE_CSOURCES = $(patsubst %, $(BIN_DIR)/%.note.c, $(APP_NAMES))
APP_NOTE_OBJS = $(patsubst %, $(BIN_DIR)/%.note.o, $(APP_NAMES))

DOOM_SOURCES = $(wildcard games/doom/*.c)
DOOM_OBJS = $(patsubst games/doom/%.c, $(BIN_DIR)/%.o, $(DOOM_SOURCES))

OPENTTD_UPSTREAM_PORT_SOURCES = \
	games/openttd/boredos_platform.cpp \
	games/openttd/boredos_cxx_runtime.cpp \
	games/openttd/boredos_video.cpp \
	games/openttd/boredos_input.cpp \
	games/openttd/boredos_timer.cpp \
	games/openttd/boredos_files.cpp \
	games/openttd/boredos_sound_null.cpp \
	games/openttd/boredos_upstream_bridge.cpp \
	games/openttd/cmake/boredos-toolchain.cmake \
	$(OPENTTD_UPSTREAM_DIR)/src/os/boredos/boredos_main.cpp \
	$(OPENTTD_UPSTREAM_DIR)/src/video/boredos_v.cpp \
	$(OPENTTD_UPSTREAM_DIR)/src/video/boredos_v.h \
	$(OPENTTD_UPSTREAM_DIR)/src/openttd.cpp \
	$(REPO_ROOT)/tools/build_openttd_boredos_upstream.sh

LUA_DIR = cli/third_party/lua
LUA_CFLAGS = -std=gnu11 -ffreestanding -O2 -fno-stack-protector -fno-stack-check -fno-lto -fno-pie -m64 -march=x86-64 -mno-red-zone -Wa,--noexecstack -isystem $(LUA_DIR)/sysinclude -I. -Ilibc -I$(LUA_DIR) -DLUA_USE_C89 -Wno-conversion -Wno-sign-conversion -Wno-double-promotion -Wno-unused-parameter -Wno-missing-declarations -Wno-shadow -Wno-undef -Wno-redundant-decls -Wno-old-style-definition -Wno-missing-prototypes -Wno-implicit-fallthrough -Wno-type-limits

all: $(BIN_DIR) $(APP_ELFS) $(BIN_DIR)/doom.elf $(BIN_DIR)/lua.elf $(BIN_DIR)/tcc.elf $(BIN_DIR)/empty.o

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/crt0.o: crt0.asm
	$(AS) -f elf64 $< -o $@

$(BIN_DIR)/libc_%.o: libc/%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/libc_%.o: libc/%.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR)/libwidget.o: ../wm/libwidget.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/stb_image.o: stb_image.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/empty.o: | $(BIN_DIR)
	echo "section .text" > $(BIN_DIR)/empty.asm
	$(AS) -f elf64 $(BIN_DIR)/empty.asm -o $@
	rm $(BIN_DIR)/empty.asm

$(BIN_DIR)/%.note.c: $(APP_METADATA_TOOL) | $(BIN_DIR)
	src="$(call app_source_for,$*)"; \
	if [ -z "$$src" ] || [ ! -f "$$src" ]; then \
		echo "error: metadata source not found for app '$*'" >&2; \
		exit 1; \
	fi; \
	sh $(APP_METADATA_TOOL) "$*" "$$src" "$(APP_ICON_SOURCE_DIR)" "$@"

$(BIN_DIR)/%.note.o: $(BIN_DIR)/%.note.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/%.o: %.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/%.o: %.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR)/viewer.elf: $(LIBC_OBJS) $(BIN_DIR)/viewer.o $(BIN_DIR)/viewer.note.o
	$(LD) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/about.elf: $(LIBC_OBJS) $(BIN_DIR)/about.o $(BIN_DIR)/about.note.o
	$(LD) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/settings.elf: $(LIBC_OBJS) $(BIN_DIR)/settings.o $(BIN_DIR)/settings.note.o
	$(LD) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/browser.elf: $(LIBC_OBJS) $(BIN_DIR)/browser.o $(BIN_DIR)/browser.note.o
	$(LD) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/screenshot.elf: $(LIBC_OBJS) $(BIN_DIR)/screenshot.o $(BIN_DIR)/screenshot.note.o
	$(LD) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/%.o: games/doom/%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -Wno-error -DBOREDOS -Igames/doom -c $< -o $@

$(BIN_DIR)/doom.elf: $(LIBC_OBJS) $(DOOM_OBJS) $(BIN_DIR)/doom.note.o
	$(LD) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/openttd.elf: $(OPENTTD_UPSTREAM_PORT_SOURCES) | $(BIN_DIR)
	cd $(REPO_ROOT) && sh tools/build_openttd_boredos_upstream.sh
	cp $(OPENTTD_UPSTREAM_BIN) $@

$(BIN_DIR)/%.elf: $(LIBC_OBJS) $(BIN_DIR)/%.o $(BIN_DIR)/%.note.o
	$(LD) $(LDFLAGS) $^ -o $@

# Lua 5.5.0 - compiled as a single translation unit
$(BIN_DIR)/lua_onelua.o: $(LUA_DIR)/boredos_onelua.c | $(BIN_DIR)
	$(CC) $(LUA_CFLAGS) -c $< -o $@

$(BIN_DIR)/lua.elf: $(LIBC_OBJS) $(BIN_DIR)/lua_onelua.o $(BIN_DIR)/lua.note.o
	$(LD) $(LDFLAGS) $^ -o $@

# TCC - Tiny C Compiler
TCC_DIR = cli/third_party/tcc
$(BIN_DIR)/tcc.elf: $(LIBC_OBJS) $(BIN_DIR)/tcc.note.o sdk $(TCC_DIR)/tcc.c $(TCC_DIR)/config.h $(TCC_DIR)/tcc.h
	cd $(TCC_DIR) && $(CC) -O2 -m64 -march=x86-64 -fno-stack-protector -fno-stack-check -fno-lto -fno-pie -ffreestanding -nostdlib -static -no-pie -Wa,--noexecstack -Wno-return-type -DONE_SOURCE=1 -DTARGETOS_BoredOS=1 -I. -I../../../sdk/include -Ttext=0x40000000 tcc.c -o tcc.elf ../../../sdk/lib/libboredos.a
	cp $(TCC_DIR)/tcc.elf $(BIN_DIR)/tcc.elf
	cd $(TCC_DIR) && CC="$(CC)" AR="$(AR)" sh build_libtcc1.sh

# SDK target
SDK_DIR = sdk
sdk: $(LIBC_OBJS) $(BIN_DIR)/libui.o
	mkdir -p $(SDK_DIR)/lib $(SDK_DIR)/include/sys
	cp libc/*.h $(SDK_DIR)/include/
	cp libc/sys/*.h $(SDK_DIR)/include/sys/
	$(AR) rcs $(SDK_DIR)/lib/libboredos.a $(LIBC_OBJS)
	cp $(SDK_DIR)/lib/libboredos.a $(SDK_DIR)/lib/libc.a
	cp $(SDK_DIR)/lib/libboredos.a $(SDK_DIR)/lib/libm.a
	$(AR) rcs $(SDK_DIR)/lib/libui.a $(BIN_DIR)/libui.o

clean:
	rm -rf $(BIN_DIR) $(SDK_DIR)
