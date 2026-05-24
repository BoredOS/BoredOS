#!/bin/sh
set -eu

fail() {
    echo "build-openttd-boredos-upstream: $*" >&2
    exit 1
}

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
UPSTREAM=${OPENTTD_UPSTREAM:-"$ROOT/src/userland/games/openttd/upstream-15.3"}
TOOLCHAIN="$ROOT/src/userland/games/openttd/cmake/boredos-toolchain.cmake"
BUILD=${OPENTTD_BUILD:-"$ROOT/build/openttd-boredos-upstream-15.3"}
HOST_BUILD=${OPENTTD_HOST_BUILD:-"$ROOT/build/openttd-host-tools-15.3"}
CXX=${CXX:-x86_64-elf-g++}
LIBCXX_ROOT=${LIBCXX_ROOT:-/home/linuxbrew/.linuxbrew/Cellar/llvm/22.1.5}

[ -f "$UPSTREAM/CMakeLists.txt" ] || fail "missing upstream OpenTTD source snapshot at $UPSTREAM"
[ -f "$TOOLCHAIN" ] || fail "missing BoredOS OpenTTD toolchain file at $TOOLCHAIN"
command -v cmake >/dev/null 2>&1 || fail "cmake is required for the upstream OpenTTD build"
[ -d "$LIBCXX_ROOT/include/c++/v1" ] || fail "libc++ headers are required at $LIBCXX_ROOT/include/c++/v1"
command -v "$CXX" >/dev/null 2>&1 || fail "$CXX is required for the BoredOS upstream OpenTTD build"

TMP=${TMPDIR:-/tmp}/openttd-boredos-cxx-stdlib.$$
trap 'rm -f "$TMP"' EXIT
printf '#include <algorithm>\n#include <string>\n#include <vector>\nint main(){std::vector<int> v{2,1}; std::sort(v.begin(), v.end()); return v[0];}\n' > "$TMP.cpp"
"$CXX" -std=gnu++23 -fsyntax-only -nostdinc++ \
    -isystem "$ROOT/src/userland/games/openttd/libcxx_boredos" \
    -isystem "$LIBCXX_ROOT/include/c++/v1" \
    -isystem "$ROOT/src/userland/libc" \
    -isystem "$ROOT/src/userland" \
    "$TMP.cpp" >/dev/null 2>&1 ||
    fail "$CXX cannot compile C++ standard-library headers with the BoredOS libc++ overlay."

cmake -S "$UPSTREAM" -B "$HOST_BUILD" \
    -DOPTION_TOOLS_ONLY=ON \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "$HOST_BUILD" --target tools

cmake -S "$UPSTREAM" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DBOREDOS=ON \
    -DOPTION_DEDICATED=OFF \
    -DOPTION_TOOLS_ONLY=OFF \
    -DOPTION_USE_ASSERTS=OFF \
    -DOPTION_USE_THREADS=OFF \
    -DOPTION_PACKAGE_DEPENDENCIES=OFF \
    -DGLOBAL_DIR=/Library/OpenTTD \
    -DHOST_BINARY_DIR="$HOST_BUILD" \
    -DCMAKE_CXX_STANDARD=23 \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD" --target openttd
