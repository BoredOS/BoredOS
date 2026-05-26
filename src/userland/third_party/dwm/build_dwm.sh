#!/bin/bash
# build_dwm.sh — cross-compile dwm 6.5 for BoredOS
# Uses x86_64-elf-gcc + mlibc + libX11.a (core fonts only, no Xft/Xinerama).
set -e

# Resolve repo root from env, git, or script location.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOREDOS_ROOT="${BOREDOS_ROOT:-}"
if [ -z "$BOREDOS_ROOT" ]; then
  if command -v git >/dev/null 2>&1; then
    BOREDOS_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null || true)"
  fi
  if [ -z "$BOREDOS_ROOT" ]; then
    BOREDOS_ROOT="$(cd "$SCRIPT_DIR/../../../../" && pwd)"
  fi
fi
if [ ! -f "$BOREDOS_ROOT/Makefile" ]; then
  echo "[ERR] Could not resolve BoredOS root (missing Makefile)." >&2
  exit 1
fi
MLIBC_HEADERS="${BOREDOS_ROOT}/src/userland/mlibc/build/mlibc-headers/usr/include"
MLIBC_CRT1="${BOREDOS_ROOT}/src/userland/mlibc/build/sysdeps/boredos/crt1.o"
SDK_INCLUDE="${BOREDOS_ROOT}/src/userland/sdk/include"
SDK_LIB="${BOREDOS_ROOT}/src/userland/sdk/lib"
LIBX11_INC="${BOREDOS_ROOT}/src/userland/third_party/libX11/include"
XORGPROTO="${BOREDOS_ROOT}/src/userland/third_party/xorgproto/include"
BIN_DIR="${BOREDOS_ROOT}/src/userland/bin"

CC="x86_64-elf-gcc"
LD="x86_64-elf-ld"
STRIP="x86_64-elf-strip"

CFLAGS=(
  "-ffreestanding"
  "-O2"
  "-fno-stack-protector"
  "-fno-stack-check"
  "-fno-lto"
  "-fno-pie"
  "-m64"
  "-march=x86-64"
  "-mno-red-zone"
  "-U__INT_FAST8_TYPE__"  "-D__INT_FAST8_TYPE__=signed char"
  "-U__UINT_FAST8_TYPE__" "-D__UINT_FAST8_TYPE__=unsigned char"
  "-U__INT_FAST16_TYPE__" "-D__INT_FAST16_TYPE__=long int"
  "-U__UINT_FAST16_TYPE__" "-D__UINT_FAST16_TYPE__=long unsigned int"
  "-U__INT_FAST32_TYPE__" "-D__INT_FAST32_TYPE__=long int"
  "-U__UINT_FAST32_TYPE__" "-D__UINT_FAST32_TYPE__=long unsigned int"
  "-D_GNU_SOURCE"
  "-D_DEFAULT_SOURCE"
  "-DVERSION=\"6.5\""
  # No Xinerama
  "-Wno-unused-function"
  "-Wno-unused-parameter"
  "-Wno-sign-compare"
  "-Wno-implicit-fallthrough"
  "-Wno-missing-prototypes"
  "-Wno-strict-prototypes"
  "-Wno-missing-declarations"
  "-isystem" "${MLIBC_HEADERS}"
  "-I."
  "-I${LIBX11_INC}"
  "-I${XORGPROTO}"
  "-I${SDK_INCLUDE}"
)

LDFLAGS=(
  "-m" "elf_x86_64"
  "-nostdlib"
  "-static"
  "-no-pie"
  "-Ttext=0x40000000"
  "--no-dynamic-linker"
  "-z" "text"
  "-z" "max-page-size=0x1000"
  "-e" "_start"
  "-z" "muldefs"
)

echo "[DWM] Compiling dwm.c..."
$CC "${CFLAGS[@]}" -c -o dwm.o dwm.c

echo "[DWM] Compiling drw.c..."
$CC "${CFLAGS[@]}" -c -o drw.o drw.c

echo "[DWM] Compiling util.c..."
$CC "${CFLAGS[@]}" -c -o util.o util.c

echo "[DWM] Linking dwm.elf..."
$LD "${LDFLAGS[@]}" \
    "${MLIBC_CRT1}" \
    "${BIN_DIR}/crti.o" \
    dwm.o drw.o util.o \
    "${BIN_DIR}/crtn.o" \
    "${SDK_LIB}/libX11.a" \
    "${SDK_LIB}/libXau.a" \
    "${SDK_LIB}/libboredos.a" \
    "${BOREDOS_ROOT}/src/userland/mlibc/build/libc.a" \
    -o dwm.elf

echo "[DWM] Stripping..."
$STRIP dwm.elf

echo "[OK] dwm.elf built successfully."
