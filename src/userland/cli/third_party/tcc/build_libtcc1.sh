#!/bin/bash
CC=x86_64-elf-gcc
AR=x86_64-elf-ar
CFLAGS="-O2 -m64 -march=x86-64 -fno-stack-protector -ffreestanding -nostdlib -I. -I./include -I../../../sdk/include"

echo "Building libtcc1.a..."
$CC $CFLAGS -c lib/libtcc1.c -o libtcc1.o
$AR rcs libtcc1.a libtcc1.o
rm libtcc1.o
echo "Done."
