#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# linux-build-crt-cross.sh -- tools/linux-build-crt.sh's own sibling for
# the two arches this dev host cannot compile OR run natively: x86_64
# and i386 (this host's own CPU is aarch64). Same proof, same curated
# FILES list, same -nostdlib -static -no-pie discipline -- the only
# real difference is HOW the binary gets built and run:
#
#   - built: with a cross-targeting clang (--target=$1-linux-gnu
#     -fuse-ld=lld) instead of a native compiler. This project's own
#     nix-wrapped clang warns that "supplying the --target ... argument
#     to a nix-wrapped compiler may not work correctly" -- empirically
#     confirmed harmless for this build specifically: nothing here links
#     against the wrapper's own target-specific sysroot/libc paths at
#     all (-nostdinc, -nostdlib, ntlibc's own headers only), so the one
#     thing the wrapper mismatch could actually break (finding the
#     RIGHT host libc/crt for the requested target) is exactly the
#     thing this build never asks it to do.
#   - run: under qemu-user (qemu-x86_64 / qemu-i386), Linux's own
#     userspace CPU emulator -- not this project's own arch/ code, a
#     completely external, off-the-shelf tool. A real kernel on real
#     x86_64/i386 hardware runs the identical binary unmodified; qemu-
#     user is standing in for hardware this dev host does not have, not
#     for anything this project itself implements.
#
# Usage: tools/linux-build-crt-cross.sh <x86_64|i386>
# Env:   NTLIBC_CLANG (default clang), NTLIBC_QEMU_X86_64 (default
#        qemu-x86_64), NTLIBC_QEMU_I386 (default qemu-i386)

set -eu

arch=${1:?"usage: $0 <x86_64|i386>"}
srcdir=$(cd "$(dirname "$0")/.." && pwd)
CLANG=${NTLIBC_CLANG:-clang}
TAG="linux-build-crt-cross($arch)"

case "$arch" in
x86_64) target=x86_64-linux-gnu; qemu=${NTLIBC_QEMU_X86_64:-qemu-x86_64} ;;
i386)   target=i386-linux-gnu;   qemu=${NTLIBC_QEMU_I386:-qemu-i386} ;;
*) echo "$TAG: unsupported arch \"$arch\" (expected x86_64 or i386)" >&2; exit 1 ;;
esac

BUILD=${NTLIBC_LINUX_CRT_CROSS_BUILD:-$srcdir/obj/linux-crt-cross-build-$arch}
CC="$CLANG --target=$target -fuse-ld=lld"

cd "$srcdir"
mkdir -p "$BUILD"

echo "$TAG: configuring (platform=linux, cross $CC)..."
(cd "$BUILD" && "$srcdir/configure" --srcdir="$srcdir" --platform=linux --target="$target" CC="$CC" >/dev/null)

cfg_arch=$(sed -n 's/^ARCH *= *//p' "$BUILD/config.mak" | head -1)
if [ "$cfg_arch" != "$arch" ]; then
	echo "$TAG: configure picked ARCH=$cfg_arch, expected $arch -- see configure's own --target handling" >&2
	exit 1
fi

echo "$TAG: building lib/crt1.o + lib/start.o..."
"${MAKE:-make}" -C "$BUILD" -f "$srcdir/Makefile" srcdir="$srcdir" \
	lib/crt1.o lib/start.o >/dev/null

FILES="
	fuzz/linux_pilot_test_crt.c
	src/fcntl/open.c
	src/fcntl/linux/plat_fcntl.c
	src/unistd/linux/plat_fd.c
	src/internal/fd.c
	src/internal/linux/plat_fd_init.c
	src/internal/errno.c
	src/internal/ldbl_layout_check.c
	src/exit/linux/plat_exit.c
	src/string/memcpy.c
	src/string/memset.c
	src/string/strlen.c
	src/string/strcmp.c
	src/string/strncmp.c
"

INC="-I$srcdir/src/internal -I$BUILD/obj/include -I$srcdir/include -I$srcdir/arch/$arch -I$srcdir/arch/generic"
CFLAGS="-std=c99 -nostdinc -fno-builtin -fno-stack-protector -g -O0 -ffunction-sections -fdata-sections \
$INC -D_XOPEN_SOURCE=700 -D_ALL_SOURCE -D_NTLIBC_INTERNAL -Wall -Wno-unused-function"

echo "$TAG: compiling (cross $CC)..."
objs="$BUILD/lib/crt1.o $BUILD/lib/start.o"
for f in $FILES; do
	o="$BUILD/$(basename "$f" .c).o"
	# shellcheck disable=SC2086
	if ! $CC $CFLAGS -c -o "$o" "$srcdir/$f"; then
		echo "$TAG: FAILED compiling $f" >&2
		exit 1
	fi
	objs="$objs $o"
done

echo "$TAG: linking (-nostdlib -static -no-pie -- no host crt, no host libc)..."
# shellcheck disable=SC2086
if ! $CC -g -O0 -nostdlib -static -no-pie -Wl,--gc-sections -o "$BUILD/linux_pilot_test_crt" $objs; then
	echo "$TAG: FAILED linking" >&2
	exit 1
fi

echo "$TAG: running under $qemu (real _start, no host crt involved)..."
if "$qemu" "$BUILD/linux_pilot_test_crt"; then
	echo "$TAG: PASS"
	exit 0
else
	echo "$TAG: FAIL (exit $?)" >&2
	exit 1
fi
