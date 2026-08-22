#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build ntlibc natively (Linux/ELF) under AddressSanitizer + UBSan and run
# whichever of test/*.c can be built that way.  See CONTRIBUTING.md.
#
# The library targets NT, so a native build cannot be complete: anything
# that ends up calling into ntdll needs a stub.  fuzz/ntstubs.c provides
# those.  Which src/*.c take part is decided *mechanically* -- every file
# is compiled, and the ones the compiler accepts are kept -- rather than
# from a hand-maintained list that would rot.  Likewise for the tests: a
# test is run if and only if it links.
#
# Usage: tools/asan-build.sh [--quiet | --objects-only]
#   --objects-only  build the instrumented library objects and stop; used
#                   by fuzz/Makefile so the fuzzers and this script share
#                   one mechanically derived file list.
# Env:   NTLIBC_CC (default clang), NTLIBC_ASAN_OBJ (default obj/asan),
#        NTLIBC_ASAN_EXTRA (extra CFLAGS, e.g. -fsanitize=fuzzer-no-link)

set -eu

srcdir=$(cd "$(dirname "$0")/.." && pwd)
CC=${NTLIBC_CC:-clang}
OBJ=${NTLIBC_ASAN_OBJ:-$srcdir/obj/asan}
ARCH=${NTLIBC_ARCH:-x86_64}
mode=${1:-}
EXTRA=${NTLIBC_ASAN_EXTRA:-}

# -shared-libasan is not cosmetic either: with the static runtime, ASan's
# own calls to sysconf()/malloc() bind at link time to ntlibc's versions,
# and ASan starts up on an NT libc that is not initialised yet.  In the
# shared runtime they go through libc.so like any other library's.
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -shared-libasan"
RTDIR=$($CC -print-file-name=libclang_rt.asan-x86_64.so)
RTDIR=$(dirname "$RTDIR")
LINKFLAGS="-Wl,-rpath,$RTDIR"
INC="-I$srcdir/src/internal -I$srcdir/obj/include -I$srcdir/include \
     -I$srcdir/arch/$ARCH -I$srcdir/arch/generic"
# -fvisibility=hidden matters: without it ntlibc's own malloc() lands in
# the executable's dynamic symbol table and preempts glibc's, so ld.so and
# ASan's own start-up allocate through RtlAllocateHeap before the shim's
# constructor has run.  Hidden keeps ntlibc's definitions for ntlibc (and
# the tests, which are in the same module) and out of everyone else's way.
CFLAGS="$SAN -g -O1 -std=c99 -nostdinc -fno-builtin -fvisibility=hidden \
        -D_XOPEN_SOURCE=700 -D_NTLIBC_INTERNAL $INC $EXTRA"

if [ ! -f "$srcdir/obj/include/bits/alltypes.h" ]; then
	echo "asan: obj/include/bits/alltypes.h missing -- run 'make' first" >&2
	exit 1
fi

rm -rf "$OBJ"
mkdir -p "$OBJ/obj" "$OBJ/test"

# ---- 1. which src/*.c compile natively? ------------------------------------
#
# Excluded by hand, with a reason each:
#   src/*/<other-arch>/*  -- wrong architecture, not an NT dependency
#   src/internal/$ARCH/teb.c -- reads gs:0x30; ntstubs.c supplies __teb()

: > "$OBJ/compiled.txt"
: > "$OBJ/skipped.txt"
: > "$OBJ/partial.txt"
for f in $(cd "$srcdir" && find src -name '*.c' | sort); do
	# src/<area>/<arch>/<file>.c overrides src/<area>/<file>.c; keep only ours
	sub=$(echo "$f" | awk -F/ 'NF==4{print $3}')
	if [ -n "$sub" ] && [ "$sub" != "$ARCH" ]; then
		echo "$f  (other architecture)" >> "$OBJ/skipped.txt"
		continue
	fi
	case $f in
	*/teb.c) echo "$f  (reads gs:0x30; __teb() comes from ntstubs.c)" >> "$OBJ/skipped.txt"
	         continue ;;
	esac
	# A few files use the musl aligned-word scan: align to sizeof(size_t),
	# then read whole words.  Such a read can go past the end of the string
	# object but never past the end of its page, so it is safe in fact --
	# ASan tracks objects, not pages, and reports every call.  Build those
	# with UBSan only, and say so, rather than drown the run in noise.
	xcflags=$CFLAGS
	case $f in
	src/string/strlen.c)
		xcflags="$(echo "$CFLAGS" | sed 's/-fsanitize=address,undefined/-fsanitize=undefined/')"
		echo "$f  (built UBSan-only: aligned word-at-a-time scan)" >> "$OBJ/partial.txt" ;;
	esac
	o="$OBJ/obj/$(echo "$f" | tr / _).o"
	if $CC -c $xcflags -w "$srcdir/$f" -o "$o" 2> "$o.err"; then
		echo "$f" >> "$OBJ/compiled.txt"
	else
		echo "$f  (see $o.err)" >> "$OBJ/skipped.txt"
		rm -f "$o"
	fi
done

$CC -c $CFLAGS -w "$srcdir/fuzz/ntstubs.c" -o "$OBJ/ntstubs.o"

# An archive would be wrong here.  libclang_rt.asan.so exports weak
# strcmp/strlen/strxfrm/memcpy/... interceptors and the driver puts it
# ahead of our inputs, so every one of those references would be satisfied
# from the DSO and the matching archive member never pulled -- i.e. the
# tests would be exercising glibc, not ntlibc.  Linking the objects
# unconditionally, hidden, makes ntlibc's definitions the ones that bind.
LIBOBJS=$(ls "$OBJ"/obj/*.o | tr '\n' ' ')

if [ "$mode" = "--objects-only" ]; then
	echo "asan: $(wc -l < "$OBJ/compiled.txt") src/*.c objects in $OBJ/obj"
	exit 0
fi

nsrc=$(wc -l < "$OBJ/compiled.txt")
nskip=$(wc -l < "$OBJ/skipped.txt")
echo "asan: $nsrc of $((nsrc + nskip)) src/*.c compiled natively ($nskip skipped, see $OBJ/skipped.txt)"

# ---- 2. run the tests that a native build can say anything about -----------
#
# A test is run unless it is on this list, and each entry says why it is
# not.  Two kinds: those that need NT services fuzz/ntstubs.c does not
# provide (real files, real processes), and those that assert the target
# ABI, which a native compiler does not have.  Nothing else is excused --
# in particular a genuine ASan or UBSan finding in ntlibc must fail here.
not_native()
{
	case $1 in
	dirent|stdlib|unistd|misc)
		echo "needs a real filesystem: NtCreateFile/NtQueryDirectoryFile are stubs" ;;
	exec|fork-win|fork-handles-win|process-win|waitpid-overflow)
		echo "needs real NT process creation: RtlCreateUserProcess is a stub" ;;
	math)
		echo "long double is 64-bit on the NT target and 80-bit here" ;;
	strto)
		echo "asserts sizeof(long)==4 (LLP64); a native long is 8" ;;
	*)  echo "" ;;
	esac
}


TINC="-I$srcdir/obj/include -I$srcdir/include -I$srcdir/arch/$ARCH -I$srcdir/arch/generic"
ran=0 passed=0 nolink=0 skipped=0
: > "$OBJ/unlinkable.txt"
for t in $(cd "$srcdir" && ls test/*.c | sort); do
	n=$(basename "$t" .c)
	exe="$OBJ/test/$n"
	why=$(not_native "$n")
	if [ -n "$why" ]; then
		skipped=$((skipped + 1))
		[ "$mode" = "--quiet" ] || echo "  SKIP $n  ($why)"
		continue
	fi
	if $CC $SAN -g -O1 -std=c99 -nostdinc -fno-builtin -D_XOPEN_SOURCE=700 -D_GNU_SOURCE -w \
	     $TINC $LINKFLAGS "$srcdir/$t" "$OBJ/ntstubs.o" $LIBOBJS -o "$exe" \
	     2> "$exe.link.err"; then
		ran=$((ran + 1))
		if ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
		   timeout 120 "$exe" > "$exe.out" 2>&1 < /dev/null; then
			passed=$((passed + 1))
			[ "$mode" = "--quiet" ] || echo "  PASS $n"
		else
			echo "  FAIL $n  (output in $exe.out)"
			[ "$mode" = "--quiet" ] || sed -n '1,25p' "$exe.out" | sed 's/^/        /'
		fi
	else
		nolink=$((nolink + 1))
		echo "$n: $(grep -o 'undefined reference to .*' "$exe.link.err" | sort -u | tr '\n' ' ')" \
			>> "$OBJ/unlinkable.txt"
	fi
done

echo "asan: $passed/$ran tests passed, $skipped not applicable natively, $nolink unlinkable"
[ "$passed" = "$ran" ]
