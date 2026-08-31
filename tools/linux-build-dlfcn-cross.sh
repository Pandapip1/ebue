#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# linux-build-dlfcn-cross.sh -- tools/linux-build-dlfcn.sh's own sibling
# for x86_64 (this dev host's own CPU is aarch64 -- see tools/linux-
# build-crt-cross.sh's own banner for the cross-build/qemu-user
# discipline this script follows identically, just proving src/dlfcn/
# linux/plat_dlfcn.c's x86_64 relocation support instead of the CRT).
#
# Unlike tools/linux-build-dlfcn.sh (which links the target test program
# against the REAL `make lib/libc.a`), this script uses a CURATED file
# list -- the same one tools/linux-build-crt-cross.sh already proved,
# plus exactly what dlopen()/dlsym()/dlclose()/dlerror() themselves need
# (malloc, the dlfcn front door, plat_dlfcn.c). `make lib/libc.a` for
# x86_64/i386 is not yet possible AT ALL: it would compile all ~20
# src/*/linux/plat_*.c backends, most of which still contain hand-
# written AARCH64 inline assembly with no x86_64/i386 port (this pass's
# own scope is the CRT + dlopen loader specifically, not the entire
# Linux syscall backend -- see this project's own task brief). A
# curated list is not a lesser proof of THIS file's own relocation
# support -- exactly the same reasoning tools/linux-build-crt.sh's own
# banner already gives for why ITS curated list was a real, first-time
# proof of the CRT layer specifically.
#
# Usage: tools/linux-build-dlfcn-cross.sh <x86_64>
# Env:   NTLIBC_CLANG (default clang), NTLIBC_QEMU_X86_64 (default qemu-x86_64)

set -eu

arch=${1:?"usage: $0 <x86_64>"}
srcdir=$(cd "$(dirname "$0")/.." && pwd)
CLANG=${NTLIBC_CLANG:-clang}
TAG="linux-build-dlfcn-cross($arch)"

case "$arch" in
x86_64) target=x86_64-linux-gnu; qemu=${NTLIBC_QEMU_X86_64:-qemu-x86_64} ;;
*) echo "$TAG: unsupported arch \"$arch\" (only x86_64 is implemented -- see src/dlfcn/linux/plat_dlfcn.c's own banner for why i386 is not)" >&2; exit 1 ;;
esac

BUILD=${NTLIBC_LINUX_DLFCN_CROSS_BUILD:-$srcdir/obj/linux-dlfcn-cross-build-$arch}
CC="$CLANG --target=$target -fuse-ld=lld"

cd "$srcdir"
mkdir -p "$BUILD"

echo "$TAG: configuring (platform=linux, cross $CC)..."
(cd "$BUILD" && "$srcdir/configure" --srcdir="$srcdir" --platform=linux --target="$target" CC="$CC" \
	CFLAGS="-fno-stack-protector" >/dev/null)

cfg_arch=$(sed -n 's/^ARCH *= *//p' "$BUILD/config.mak" | head -1)
if [ "$cfg_arch" != "$arch" ]; then
	echo "$TAG: configure picked ARCH=$cfg_arch, expected $arch" >&2
	exit 1
fi

echo "$TAG: building lib/crt1.o + lib/start.o..."
"${MAKE:-make}" -C "$BUILD" -f "$srcdir/Makefile" srcdir="$srcdir" \
	lib/crt1.o lib/start.o >/dev/null

echo "$TAG: building the target .so's (host $CLANG, real $arch shared objects)..."
if ! $CLANG --target="$target" -shared -fPIC -nostdlib -fuse-ld=lld -Wl,--hash-style=sysv \
	-Wl,-soname,linux_pilot_test_dlopen_lib.so \
	-o "$BUILD/linux_pilot_test_dlopen_lib.so" \
	fuzz/linux_pilot_test_dlopen_lib.c; then
	echo "$TAG: FAILED building the target .so" >&2
	exit 1
fi
if ! $CLANG --target="$target" -shared -fPIC -nostdlib -fuse-ld=lld -Wl,--hash-style=sysv \
	-Wl,-soname,linux_pilot_test_dlopen_tlslib.so \
	-o "$BUILD/linux_pilot_test_dlopen_tlslib.so" \
	fuzz/linux_pilot_test_dlopen_tlslib.c; then
	echo "$TAG: FAILED building the TLS-bearing target .so" >&2
	exit 1
fi

FILES="
	fuzz/linux_pilot_test_dlopen_cross.c
	fuzz/linux_pilot_dlfcn_cross_yield.c
	src/fcntl/open.c
	src/fcntl/linux/plat_fcntl.c
	src/unistd/read.c
	src/unistd/close.c
	src/unistd/linux/plat_fd.c
	src/internal/fd.c
	src/internal/linux/plat_fd_init.c
	src/internal/linux/fdpos.c
	src/internal/errno.c
	src/internal/ldbl_layout_check.c
	src/exit/linux/plat_exit.c
	src/socket/sendrecv.c
	src/socket/linux/plat_socket.c
	src/string/memcpy.c
	src/string/memset.c
	src/string/memmove.c
	src/string/memchr.c
	src/string/memcmp.c
	src/string/strlen.c
	src/string/strcmp.c
	src/string/strncmp.c
	src/string/strerror.c
	src/stdlib/mbrtowc.c
	src/math/fabs.c
	src/math/fpclassify.c
	src/malloc/malloc.c
	src/malloc/linux/plat_malloc.c
	src/dlfcn/dlfcn.c
	src/dlfcn/linux/plat_dlfcn.c
	src/stdio/printf.c
	src/stdio/rw.c
	src/stdio/buf.c
	src/stdio/mem.c
	src/stdio/seek.c
	src/unistd/write.c
	src/unistd/lseek.c
	src/misc/resource.c
	src/misc/linux/plat_misc.c
"

INC="-I$srcdir/src/internal -I$BUILD/obj/include -I$srcdir/include -I$srcdir/arch/$arch -I$srcdir/arch/generic"
CFLAGS="-std=c99 -nostdinc -fno-builtin -fno-stack-protector -g -O0 -ffunction-sections -fdata-sections \
$INC -D_XOPEN_SOURCE=700 -D_ALL_SOURCE -D_NTLIBC_INTERNAL -Wall -Wno-unused-function"

echo "$TAG: compiling the test program + curated support files (cross $CC)..."
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
# -Wl,-u,host_provided_value: forces the linker to treat this symbol as
# a GC root despite --gc-sections -- see fuzz/linux_pilot_test_dlopen_
# cross.c's own comment on host_provided_value() for why plain
# __attribute__((used)) alone (a compiler-level "don't dead-code-
# eliminate this", not a linker-level GC root) was not enough: nothing
# in the STATIC link graph itself calls it, only the dlopen()'d .so's
# own runtime relocation does, invisible to --gc-sections' own
# reachability analysis.
if ! $CC -g -O0 -nostdlib -static -no-pie -Wl,--gc-sections -Wl,-u,host_provided_value -o "$BUILD/linux_pilot_test_dlopen" $objs; then
	echo "$TAG: FAILED linking" >&2
	exit 1
fi

echo "$TAG: running under $qemu (real _start, real dlopen(), no host crt involved)..."
if (cd "$BUILD" && "$qemu" ./linux_pilot_test_dlopen); then
	echo "$TAG: PASS"
	exit 0
else
	echo "$TAG: FAIL (exit $?)" >&2
	exit 1
fi
