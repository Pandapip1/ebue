#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# linux-build-malloc.sh -- build and run the malloc front-door pilot
# natively. See tools/linux-build-open.sh for the pattern this mirrors
# (host crt/stdio for program startup and test output -- unlike
# tools/linux-build-crt.sh, this pilot's job is proving the allocator
# itself correct, not re-proving freestanding startup, which the crt
# pilot already does).
#
# Proves src/malloc/linux/plat_malloc.c -- the real segregated free-
# list, mmap-backed allocator this platform needs from scratch, since
# unlike NT (RtlAllocateHeap) there is no existing serious allocator
# to delegate to here -- against the REAL src/malloc/malloc.c front
# door (malloc/calloc/realloc/free/malloc_usable_size/posix_memalign),
# statically linked into one native, runnable ELF binary.
#
# Usage: tools/linux-build-malloc.sh
# Env:   NTLIBC_CC (default clang)

set -eu

srcdir=$(cd "$(dirname "$0")/.." && pwd)
CC=${NTLIBC_CC:-clang}
OBJ=${NTLIBC_LINUX_OBJ:-$srcdir/obj/linux-pilot-malloc}
TAG=linux-build-malloc

cd "$srcdir"

mkdir -p "$OBJ"
if [ ! -f obj/include/bits/alltypes.h ]; then
	echo "$TAG: obj/include/bits/alltypes.h is missing -- run './configure --platform=linux CC=$CC' and 'make obj/include/bits/alltypes.h' first (the generated header this build needs, same one 'make'/'make asan' use)." >&2
	exit 1
fi

cfg_arch=$(sed -n 's/^ARCH *= *//p' config.mak 2>/dev/null | head -1)
arch=${cfg_arch:-aarch64}

FILES="
	fuzz/linux_pilot_test_malloc.c
	src/malloc/malloc.c
	src/malloc/linux/plat_malloc.c
"

INC="-Isrc/internal -Iobj/include -Iinclude -Iarch/$arch -Iarch/generic"
# -fno-stack-protector: see tools/linux-build-crt.sh's own comment --
# this tree has no __stack_chk_guard/_fail, and a native host cc
# defaults to inserting stack-protector calls anyway.
CFLAGS="-std=c99 -nostdinc -fno-builtin -fno-stack-protector -g -O0 -ffunction-sections -fdata-sections \
$INC -D_XOPEN_SOURCE=700 -D_ALL_SOURCE -D_NTLIBC_INTERNAL -Wall -Wno-unused-function"

echo "$TAG: compiling ($CC, native ELF)..."
objs=""
for f in $FILES; do
	o="$OBJ/$(basename "$f" .c).o"
	# shellcheck disable=SC2086
	if ! $CC $CFLAGS -c -o "$o" "$f"; then
		echo "$TAG: FAILED compiling $f" >&2
		exit 1
	fi
	objs="$objs $o"
done

# __plat_fast_lock()/__plat_fast_unlock() (src/internal/plat_thread.h):
# plat_malloc.c's free lists are guarded by the real process-wide fast
# lock, but linking the whole src/thread/linux/plat_thread.c object
# for it would pull in __plat_thread_spawn() and its own dependency
# chain -- the same whole-object-linking hazard every other pilot in
# this tree works around the same way, with a small local stand-in.
# No-op is a correct stand-in here specifically because this pilot is
# single-threaded (real concurrency needs pthread_create(), which has
# no Linux backend yet -- a separate, disclosed gap), not a general
# substitute for the real lock.
cat > "$OBJ/harness.c" <<'EOF'
void __plat_fast_lock(void) {}
void __plat_fast_unlock(void) {}
EOF
# shellcheck disable=SC2086
if ! $CC $CFLAGS -c -o "$OBJ/harness.o" "$OBJ/harness.c"; then
	echo "$TAG: FAILED compiling the lock-stub harness" >&2
	exit 1
fi
objs="$objs $OBJ/harness.o"

echo "$TAG: linking..."
# shellcheck disable=SC2086
if ! $CC -g -O0 -Wl,--gc-sections -o "$OBJ/linux_pilot_test_malloc" $objs; then
	echo "$TAG: FAILED linking" >&2
	exit 1
fi

echo "$TAG: running..."
if "$OBJ/linux_pilot_test_malloc"; then
	echo "$TAG: PASS"
	exit 0
else
	echo "$TAG: FAIL" >&2
	exit 1
fi
