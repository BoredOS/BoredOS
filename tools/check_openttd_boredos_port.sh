#!/bin/sh
set -eu

fail() {
    echo "openttd-port-check: $*" >&2
    exit 1
}

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
UPSTREAM=${OPENTTD_UPSTREAM:-"$ROOT/src/userland/games/openttd/upstream-15.3"}
USERLAND="$ROOT/src/userland"

[ -e "$UPSTREAM/.git" ] || fail "missing full upstream OpenTTD git clone at $UPSTREAM"
[ -f "$UPSTREAM/src/openttd.cpp" ] || fail "upstream source tree is incomplete"
[ -f "$UPSTREAM/src/video/boredos_v.cpp" ] || fail "missing upstream BoredOS video driver implementation"
[ -f "$UPSTREAM/src/video/boredos_v.h" ] || fail "missing upstream BoredOS video driver header"
[ -f "$UPSTREAM/src/os/boredos/boredos_main.cpp" ] || fail "missing upstream BoredOS OS entry point"
[ -f "$USERLAND/games/openttd/boredos_upstream_bridge.cpp" ] || fail "missing BoredOS/OpenTTD bridge layer"
[ -f "$USERLAND/games/openttd/cmake/boredos-toolchain.cmake" ] || fail "missing upstream BoredOS CMake toolchain file"
[ -f "$USERLAND/games/openttd/data/baseset/opengfx-8.0.tar" ] || fail "missing packaged OpenGFX base-set archive"
[ -f "$USERLAND/games/openttd/data/baseset/opensfx-1.0.3.tar" ] || fail "missing packaged OpenSFX base-set archive"
[ -f "$USERLAND/games/openttd/data/baseset/openmsx-0.4.2.tar" ] || fail "missing packaged OpenMSX base-set archive"
[ -f "$USERLAND/bin/openttd.elf" ] || fail "missing built /bin/openttd.elf"
[ -f "$USERLAND/bin/scribble.elf" ] || fail "missing built /bin/scribble.elf"

git -C "$UPSTREAM" rev-parse --is-inside-work-tree >/dev/null 2>&1 || fail "upstream clone is not a git worktree"
git -C "$UPSTREAM" rev-parse --is-shallow-repository | grep -qx false || fail "upstream clone is shallow; fetch full history first"

grep -q 'boredos_v.cpp' "$UPSTREAM/src/video/CMakeLists.txt" || fail "upstream CMake video list does not include boredos_v.cpp"
grep -q 'add_subdirectory(boredos)' "$UPSTREAM/src/os/CMakeLists.txt" || fail "upstream CMake OS list does not include BoredOS"
grep -q 'FVideoDriver_BoredOS' "$UPSTREAM/src/video/boredos_v.h" || fail "BoredOS video driver factory is missing"
grep -q 'ottd_boredos_window_present' "$UPSTREAM/src/video/boredos_v.cpp" || fail "upstream driver is not wired to BoredOS present bridge"
grep -q 'ottd_boredos_window_poll' "$UPSTREAM/src/video/boredos_v.cpp" || fail "upstream driver is not wired to BoredOS input polling bridge"
grep -q 'HandleBoredOSKeyEvent' "$UPSTREAM/src/video/boredos_v.cpp" || fail "upstream driver is not mapping BoredOS keys into OpenTTD input"
grep -q 'HandleBoredOSMouseEvent' "$UPSTREAM/src/video/boredos_v.cpp" || fail "upstream driver is not mapping BoredOS mouse events into OpenTTD input"
grep -q 'OpenTTD Port' "$USERLAND/games/openttd/openttd_boredos_main.cpp" || fail "OpenTTD launcher metadata is missing"
grep -q 'scribble' "$USERLAND/cli/help.c" || fail "scribble is not visible in help"

echo "openttd-port-check: upstream clone, BoredOS CMake hook, input bridge, base media, dock/package wiring, and built ELFs are present"
