#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# linux-build-crt.sh -- build and run fuzz/linux_pilot_test_crt.c as a
# real, freestanding, statically linked ELF binary through the REAL
# ./configure --platform=linux / Makefile build, not an ad hoc FILES
# list compiled straight from this script the way every earlier
# tools/linux-build-*.sh pilot works.
#
# The difference matters more than it sounds: every earlier Linux pilot
# links with a bare `$CC ... -o binary $objs`, no -nostdlib -- which
# means every one of them silently rides on the HOST's real glibc crt
# (_start, argv/envp/auxv parsing, and -- easy to miss -- real TLS
# setup via the dynamic linker) for everything this script's own
# crt/linux/crt1.c + crt/linux/aarch64/start.S now do from scratch.
# That is *why* errno (a __thread variable) always worked in those
# pilots despite no ntlibc code anywhere setting up TPIDR_EL0: glibc's
# own crt already had. This script is the first one that does not
# borrow that -- $(CC) links -nostdlib -static -no-pie against nothing
# but this build's own lib/crt1.o, lib/start.o and lib/libc.a-shaped
# object set, so a pass here is a real, first-time proof that this
# project's OWN program-startup code (not the host's) gets argv/
# environ/TLS/errno/fd 0-2/exit status all correct end to end.
#
# -no-pie matters beyond style: crt1.c's TLS setup reads AT_PHDR
# straight out of auxv and treats it as an absolute, already-relocated
# address (see linux_setup_tls()'s own comment) -- correct for a
# non-PIE static binary, where the kernel loads the image at its
# link-time addresses with no bias to account for. A PIE build would
# need that bias computed first; out of scope for what this script
# proves, so the link line simply asks for the format this crt is
# written against.
#
# Deliberately NOT `make all`: PLATFORM=linux's `make lib/libc.a`
# builds the WHOLE library, including large swaths (src/math/x87.h's
# x87-specific inline asm, setjmp/longjmp, signal altstack assembly --
# none arch-guarded) that assume x86 unconditionally and do not even
# compile on aarch64. Porting the rest of the library to a second CPU
# architecture is real, separate, disclosed future work -- the crt/
# build-target gap this script proves closed is specifically "can a
# real program start up and run under this platform's own crt", which
# needs only the curated file list below, the same shape every earlier
# linux-build-*.sh pilot already uses.
#
# Usage: tools/linux-build-crt.sh
# Env:   NTLIBC_CC (default clang)

set -eu

srcdir=$(cd "$(dirname "$0")/.." && pwd)
CC=${NTLIBC_CC:-clang}
BUILD=${NTLIBC_LINUX_CRT_BUILD:-$srcdir/obj/linux-crt-build}
TAG=linux-build-crt

cd "$srcdir"

mkdir -p "$BUILD"

echo "$TAG: configuring (platform=linux, native $CC)..."
(cd "$BUILD" && "$srcdir/configure" --srcdir="$srcdir" --platform=linux CC="$CC" >/dev/null)

cfg_arch=$(sed -n 's/^ARCH *= *//p' "$BUILD/config.mak" | head -1)
if [ "$cfg_arch" != "aarch64" ]; then
	echo "$TAG: this build host configured ARCH=$cfg_arch, but crt/linux/aarch64/start.S is the only arch implemented so far -- see crt/linux/crt1.c's own banner." >&2
	exit 1
fi

echo "$TAG: building lib/crt1.o + lib/start.o + lib/libc.a's needed objects..."
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
	src/exit/linux/plat_exit.c
	src/string/memcpy.c
	src/string/memset.c
	src/string/strlen.c
	src/string/strcmp.c
	src/string/strncmp.c
"

INC="-I$srcdir/src/internal -I$BUILD/obj/include -I$srcdir/include -I$srcdir/arch/aarch64 -I$srcdir/arch/generic"
# -fno-stack-protector: this build has no __stack_chk_guard/_fail (NT
# never needed one under tcc, which does not insert stack-protector
# calls by default) -- without this flag, this host's clang defaults
# to inserting them anyway and the link fails on two symbols nothing
# in this tree defines. Found empirically, not anticipated.
CFLAGS="-std=c99 -nostdinc -fno-builtin -fno-stack-protector -g -O0 -ffunction-sections -fdata-sections \
$INC -D_XOPEN_SOURCE=700 -D_ALL_SOURCE -D_NTLIBC_INTERNAL -Wall -Wno-unused-function"

echo "$TAG: compiling ($CC, native ELF)..."
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

echo "$TAG: running (real _start, no host crt involved)..."
if "$BUILD/linux_pilot_test_crt"; then
	echo "$TAG: PASS"
	exit 0
else
	echo "$TAG: FAIL (exit $?)" >&2
	exit 1
fi
