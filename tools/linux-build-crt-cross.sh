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
	src/exit/exit.c
	src/malloc/crt_alloc.c
	src/malloc/linux/plat_malloc.c
	src/malloc/malloc.c
	src/math/fenv.c
	src/signal/signal.c
	src/signal/linux/sigdelivery.c
	src/process/children.c
	src/stdio/file.c
	src/stdio/buf.c
	src/thread/pthread.c
	src/thread/pthread_cancel.c
	src/thread/pthread_tsd.c
	src/misc/resource.c
	src/unistd/getpid.c
	src/unistd/ids.c
	src/unistd/write.c
	src/unistd/lseek.c
	src/socket/sendrecv.c
	src/string/memcpy.c
	src/string/memset.c
	src/string/strlen.c
	src/string/strcmp.c
	src/string/strncmp.c
"
# src/misc/linux/plat_misc.c is NOT in the common list above: unlike the
# other Linux backend files, it DOES try to be multi-arch (its own
# raw_syscall()-adjacent SYS_* block is `#if defined(__aarch64__) ...
# #elif defined(__x86_64__) ... #else #error "plat_misc.c: unsupported
# architecture" #endif`) but was only ever carried through for two of
# this project's three arches -- confirmed empirically, the same way as
# every other file in this comment block: it compiles clean for
# x86_64-linux-gnu and hits that #error, plus a cascade of "undeclared
# identifier SYS_kill"/etc from the arch-less syscall() calls below the
# guard, for i386-linux-gnu. src/misc/resource.c's own __fsize_clamp()/
# __fsize_room_at() (write()'s own RLIMIT_FSIZE check, itself
# unconditionally compiled into write()'s reachable body) call into it
# via fsize_start() -- so resource.c stays in the common list above
# (real, needed, and it compiles fine on both), while plat_misc.c joins
# the aarch64-only backend files below as an i386-specific extra gap this
# list cannot close either.
# src/socket/linux/plat_socket.c: the identical situation, one file
# down -- its own SYS_* block is also `#if defined(__aarch64__) ...
# #elif defined(__x86_64__) ... #else #error "plat_socket.c: unsupported
# architecture" #endif`, so it compiles clean for x86_64-linux-gnu and
# fails outright for i386-linux-gnu. write()'s own __FD_SOCKET branch
# (again, unconditionally compiled into write()'s reachable body) calls
# send() (src/socket/sendrecv.c, common list above -- portable itself)
# which calls into this file's __plat_sock_send() -- so, like
# plat_misc.c above, this is an i386-specific extra gap alongside the
# four aarch64-only backend files, not a reason to drop sendrecv.c from
# the common list.
if [ "$arch" = "x86_64" ]; then
	FILES="$FILES
	src/misc/linux/plat_misc.c
	src/socket/linux/plat_socket.c
"
fi
# exit.c/crt_alloc.c/fenv.c and everything else added above (matching
# tools/linux-build-crt.sh's own FILES -- see its own comments for what
# each one resolves): all genuinely multi-arch C, confirmed by actually
# compiling every one of them for both x86_64-linux-gnu and i386-linux-gnu
# with this exact CC/CFLAGS combination before adding it here, not
# assumed from the aarch64 list.
#
# What this list still CANNOT close, on this arch pair, is a clean link:
# __signal_init()'s Linux path (src/signal/signal.c, via __sig_delivery_
# init() unconditionally and __plat_sig_install_fault_handlers() on the
# aarch64 arm this build never takes) and exit()'s own default-terminate
# path (src/exit/exit.c's __plat_sig_default_terminate() call) both
# bottom out in src/signal/linux/plat_signal.c; children.c's own
# __child_resume_stopped() bottoms out in src/process/linux/
# plat_process.c; getpid()/getuid()/getpgrp() (signal.c's own kill()/
# make_siginfo() call these) bottom out in src/unistd/linux/
# plat_unistd.c; and sigdelivery.c's lock/semaphore machinery bottoms out
# in src/thread/linux/plat_thread.c. Every one of those four backend
# files hardcodes aarch64's `svc #0` raw-syscall calling convention with
# NO `#if defined(__x86_64__)`/`#if defined(__i386__)` branch at all --
# unlike src/fcntl/linux/plat_fcntl.c, src/unistd/linux/plat_fd.c,
# src/internal/linux/plat_fd_init.c and src/exit/linux/plat_exit.c above,
# which already handle all three arches -- confirmed empirically:
# `clang --target=x86_64-linux-gnu -c src/signal/linux/plat_signal.c`
# (and the same for the other three files, and for --target=i386-linux-
# gnu) fails outright with "unknown register name 'x8' in asm", so none
# of the four can even be ADDED to this FILES list, let alone close the
# link.
#
# One more, for i386 only, of a different KIND than the five files
# above: src/signal/signal.c's own sig_dispatch() calls
# __sig_call_on_altstack() for SA_ONSTACK delivery on `#if defined(_WIN32)
# || defined(__linux__)`, same as aarch64 -- but unlike aarch64's own
# src/signal/aarch64/altstack.S (AAPCS64 argument registers, already
# right for both NT and SysV Linux, per that file's own banner),
# src/signal/i386/altstack.S and src/signal/x86_64/altstack.S both
# implement the WINDOWS calling convention (x86_64's own header comment
# says so outright: "Windows x64 ABI: sp arrives in %rcx, fn in %rdx, arg
# in %r8"; i386's is cdecl, also NT's convention there). Both assemble
# clean for an i386-linux-gnu/x86_64-linux-gnu target -- they are bare
# instructions, nothing OS-specific -- so adding either here would not
# fail this script's own compile step at all; it would link clean and be
# a genuine, silent ABI-mismatch bug the very first time a real SysV
# Linux caller's arguments got read out of the wrong registers. Left out
# for that reason, not because it fails to build.
#
# This is real, disclosed, pre-existing scope, not a curated-list
# omission: .github/workflows/ci.yml's own Linux-build-matrix comment
# already says "Linux is complete on aarch64; x86_64 and i386 currently
# have curated CRT build/run coverage", and commit 66367136's own title
# ("Install real cross-process signal delivery on Linux/aarch64 (Tier
# 2)") names the one arch it landed for. Porting these backends' raw
# syscalls (and, for i386/x86_64, a real SysV __sig_call_on_altstack) to
# the two arches that need them (right syscall numbers, right kernel-ABI
# struct layouts, right calling conventions, per arch) is real,
# substantial, correctness-sensitive work -- out of scope for this
# script's FILES list, and not attempted here. Every other gap crt1.c's
# own growing call graph has introduced IS closed above, so the
# undefined symbols this script still ends on (__plat_getpid,
# __plat_detect_uid, __plat_wait_one, __plat_fast_lock,
# __plat_process_resume, __plat_sig_default_terminate, __plat_event_set,
# __plat_sigevent_set, __plat_stop_event_create, __sig_call_on_altstack,
# and their like -- run this script to see the current, complete list
# for a given arch) are exactly, only, and entirely a symptom of those
# still-unported backends, not of anything still missing from this list.

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
