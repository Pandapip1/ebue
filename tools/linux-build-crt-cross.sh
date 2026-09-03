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
	src/signal/linux/plat_signal.c
	src/process/children.c
	src/process/linux/plat_process.c
	src/stdio/file.c
	src/stdio/buf.c
	src/thread/pthread.c
	src/thread/pthread_cancel.c
	src/thread/pthread_tsd.c
	src/thread/linux/plat_thread.c
	src/misc/resource.c
	src/unistd/getpid.c
	src/unistd/ids.c
	src/unistd/write.c
	src/unistd/lseek.c
	src/unistd/linux/plat_unistd.c
	src/socket/sendrecv.c
	src/string/memcpy.c
	src/string/memset.c
	src/string/strlen.c
	src/string/strcmp.c
	src/string/strncmp.c
"
# arch/$arch/src/sigreturn_trampoline.S: the real SA_RESTORER trampoline
# src/signal/linux/plat_signal.c's own __plat_sig_install_real_handler()
# unconditionally references (by name, k_restorer = __ntlibc_sigreturn_
# trampoline) for every arch it supports -- see that file's own comment
# on that function and each arch's own arch/$arch/src/sigreturn_
# trampoline.S banner. __signal_init() (crt1.c, unconditional) reaches
# __plat_sig_install_fault_handlers() -> __plat_sig_install_real_handler()
# on every arch, so this is a REAL, not merely potential, link
# requirement -- confirmed by first trying without it and getting a real
# "undefined symbol: __ntlibc_sigreturn_trampoline" from this exact
# script. Named directly here (basename-based object naming below still
# works: the loop's `basename "$f" .c` leaves a ".S" input's own
# extension alone, producing "sigreturn_trampoline.S.o" -- a valid,
# unique object filename, just not as tidy as tools/linux-build-thread.sh's
# own sed-based stem-stripping).
FILES="$FILES
	arch/$arch/src/sigreturn_trampoline.S
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
# src/signal/linux/plat_signal.c, src/thread/linux/plat_thread.c,
# src/process/linux/plat_process.c and src/unistd/linux/plat_unistd.c
# (added to FILES above, alongside arch/$arch/src/sigreturn_trampoline.S
# -- see that entry's own comment) USED TO be exactly this gap: every one
# of the four hardcoded aarch64's `svc #0` raw-syscall calling convention
# with NO `#if defined(__x86_64__)`/`#if defined(__i386__)` branch at
# all, unlike src/fcntl/linux/plat_fcntl.c, src/unistd/linux/plat_fd.c,
# src/internal/linux/plat_fd_init.c and src/exit/linux/plat_exit.c above,
# which already handled all three arches. That is now closed for real,
# for both x86_64 and i386: real per-arch syscall numbers (confirmed
# against a real x86_64-linux-gnu glibc's own asm/unistd_64.h/
# unistd_32.h, not guessed or offset from aarch64's), real per-arch
# raw_syscall() bodies (x86_64's `syscall` convention, i386's register-
# starved array trick -- both already-established patterns this tree's
# own crt1.c/plat_fcntl.c set), and two real per-arch struct-layout fixes
# discovered along the way, not just syscall-number swaps: plat_process.c's
# raw_stat_prefix (the kernel's real struct stat has a genuinely
# different FIELD ORDER on x86_64 -- st_nlink before st_mode, not after --
# and a genuinely different, narrower field set on i386), and plat_signal.c's
# struct kernel_sigaction (the kernel's rt_sigaction(2) sigset is always
# exactly 8 bytes on every arch, which a plain `unsigned long k_mask`
# only happens to be on the two LP64 arches -- i386 needs a real two-word
# array plus a fixed RT_SIGSETSIZE, not sizeof(unsigned long)).
#
# One more, for i386 only, of a different KIND than the files
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
# Real SysV __sig_call_on_altstack is still real, disclosed, pre-existing
# scope, not a curated-list omission: .github/workflows/ci.yml's own
# Linux-build-matrix comment still says "Linux is complete on aarch64;
# x86_64 and i386 currently have curated CRT build/run coverage", and
# commit 66367136's own title ("Install real cross-process signal
# delivery on Linux/aarch64 (Tier 2)") names the one arch it landed for.
# Confirmed by a real clean build, both arches: with the four raw-syscall
# backend files above now genuinely ported (see the FILES list itself
# and each new #elif branch's own comments), this script's link ends on
# EXACTLY ONE remaining undefined symbol for x86_64 -- __sig_call_on_
# altstack, precisely the gap this comment already named -- and, for
# i386, that same symbol plus three more that are ALSO real, disclosed,
# pre-existing gaps unrelated to this task's own four files: __divdi3/
# __moddi3 (i386 has no native 64-bit divide instruction; src/thread/
# linux/plat_thread.c's own __plat_wait_one() -- pre-existing arithmetic,
# not touched by this pass -- does a real `long long / long long`
# converting relative_ticks to a struct timespec, which a -nostdlib
# freestanding build has no libgcc/compiler-rt to supply the routine
# for; a real fix needs a genuine, carefully-derived 64-bit software
# division implementation, out of scope for a raw-syscall/struct-layout
# port and not attempted here), __plat_write_start_offset and
# __plat_sock_send (src/misc/linux/plat_misc.c and src/socket/linux/
# plat_socket.c -- the two files this script's OWN pre-existing comment,
# a few paragraphs up, already names as x86_64-only, unrelated to the
# four backend files this pass ported). Porting a real SysV __sig_call_
# on_altstack, writing real __divdi3/__moddi3, and porting plat_misc.c/
# plat_socket.c to i386 are each separate, disclosed follow-up work.

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
