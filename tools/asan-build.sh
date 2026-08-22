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
#        NTLIBC_ASAN_EXTRA (extra CFLAGS, e.g. -fsanitize=fuzzer-no-link),
#        NTLIBC_LEAKS (default 1; set 0 to switch LeakSanitizer off)
#        NTLIBC_ASAN_CONVERSION=1 (see CONVSAN below)

set -eu

srcdir=$(cd "$(dirname "$0")/.." && pwd)
CC=${NTLIBC_CC:-clang}
OBJ=${NTLIBC_ASAN_OBJ:-$srcdir/obj/asan}
ARCH=${NTLIBC_ARCH:-x86_64}
mode=${1:-}
EXTRA=${NTLIBC_ASAN_EXTRA:-}

# LeakSanitizer is on, and that is the point.  ntlibc's malloc is
# RtlAllocateHeap, which fuzz/ntstubs.c answers with ASan's own allocator,
# so LSan sees every ntlibc allocation with a full ntlibc stack -- there is
# nothing here it cannot account for and no suppression file is needed.  It
# used to be off for no better reason than that it was off by default in
# the fuzzers this was modelled on, and that cost real bugs: sscanf leaked
# a BUFSIZ block per call from the first commit until 64ea74e, through a
# green `make check` the whole time, and LSan reports it in one run.  Set
# NTLIBC_LEAKS=0 only to isolate some other failure.
LEAKS=${NTLIBC_LEAKS:-1}

# -shared-libasan is not cosmetic either: with the static runtime, ASan's
# own calls to sysconf()/malloc() bind at link time to ntlibc's versions,
# and ASan starts up on an NT libc that is not initialised yet.  In the
# shared runtime they go through libc.so like any other library's.
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -shared-libasan"
RTDIR=$($CC -print-file-name=libclang_rt.asan-x86_64.so)
RTDIR=$(dirname "$RTDIR")
LINKFLAGS="-Wl,-rpath,$RTDIR"

# -fsanitize=implicit-conversion is NOT part of the -fsanitize=undefined
# group; it is a separate group of three checks, and they have very
# different signal in this codebase.  Measured over the native test run
# plus a 4x90s libFuzzer run of fuzz/ (~6.5M execs per harness):
#
#   implicit-{un,}signed-integer-truncation
#       0 findings.  A libc narrows constantly -- `unsigned char` in the
#       ctype and string code, int->char in the digit paths -- but UBSan
#       reports only when the value actually *changes*, and none of those
#       ever do.  So it costs nothing today and it is the class that would
#       catch a real narrowing bug: on by default, and made fatal below so
#       one fails the run rather than scrolling past.
#
#   implicit-integer-sign-change
#       6 sites, every one a deliberate idiom and none a bug: memmove.c's
#       `-2*n` overlap test, the `unsigned u = i` in ffs/ffsl/ffsll,
#       time_impl.h's `mp + (mp < 10 ? 3 : -9)` month wrap, and open.c's
#       `~FILE_WRITE_DATA` mask.  Off by default and report-only when on,
#       the way tools/lint.sh treats LINT_CONVERSION: worth a periodic
#       read, not worth a gate.  NTLIBC_ASAN_CONVERSION=1 enables it.
#
# Neither catches an *explicit* cast -- `(USHORT)v` is silent under all
# three -- so this is not a substitute for reading narrowing casts.
#
# CONVSAN applies to the library only, never to test/*.c or ntstubs.c:
# a narrowing in test code is not a finding about ntlibc.
CONVSAN="-fsanitize=implicit-signed-integer-truncation,implicit-unsigned-integer-truncation \
 -fno-sanitize-recover=implicit-signed-integer-truncation,implicit-unsigned-integer-truncation"
if [ "${NTLIBC_ASAN_CONVERSION:-0}" = 1 ]; then
	CONVSAN="$CONVSAN -fsanitize=implicit-integer-sign-change"
fi

INC="-I$srcdir/src/internal -I$srcdir/obj/include -I$srcdir/include \
     -I$srcdir/arch/$ARCH -I$srcdir/arch/generic"
# -fvisibility=hidden matters: without it ntlibc's own malloc() lands in
# the executable's dynamic symbol table and preempts glibc's, so ld.so and
# ASan's own start-up allocate through RtlAllocateHeap before the shim's
# constructor has run.  Hidden keeps ntlibc's definitions for ntlibc (and
# the tests, which are in the same module) and out of everyone else's way.
CFLAGS="$SAN $CONVSAN -g -O1 -std=c99 -nostdinc -fno-builtin -fvisibility=hidden \
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
	# $xcflags is a flag list and must word-split.
	# shellcheck disable=SC2086
	if $CC -c $xcflags -w "$srcdir/$f" -o "$o" 2> "$o.err"; then
		echo "$f" >> "$OBJ/compiled.txt"
	else
		echo "$f  (see $o.err)" >> "$OBJ/skipped.txt"
		rm -f "$o"
	fi
done

# shellcheck disable=SC2086
$CC -c $CFLAGS -w "$srcdir/fuzz/ntstubs.c" -o "$OBJ/ntstubs.o"

# An archive would be wrong here.  libclang_rt.asan.so exports weak
# strcmp/strlen/strxfrm/memcpy/... interceptors and the driver puts it
# ahead of our inputs, so every one of those references would be satisfied
# from the DSO and the matching archive member never pulled -- i.e. the
# tests would be exercising glibc, not ntlibc.  Linking the objects
# unconditionally, hidden, makes ntlibc's definitions the ones that bind.
# Object names are generated above from source paths with `tr / _`, so the
# glob can never produce a name needing quoting.  LIBOBJS is expanded
# unquoted below, as a list of link inputs.
LIBOBJS=$(echo "$OBJ"/obj/*.o)

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
for t in $(cd "$srcdir" && echo test/*.c); do
	n=$(basename "$t" .c)
	exe="$OBJ/test/$n"
	why=$(not_native "$n")
	if [ -n "$why" ]; then
		skipped=$((skipped + 1))
		[ "$mode" = "--quiet" ] || echo "  SKIP $n  ($why)"
		continue
	fi
	# $SAN/$TINC/$LINKFLAGS/$LIBOBJS are flag and object lists: word-split.
	# shellcheck disable=SC2086
	if $CC $SAN -g -O1 -std=c99 -nostdinc -fno-builtin -D_XOPEN_SOURCE=700 -D_GNU_SOURCE -w \
	     $TINC $LINKFLAGS "$srcdir/$t" "$OBJ/ntstubs.o" $LIBOBJS -o "$exe" \
	     2> "$exe.link.err"; then
		ran=$((ran + 1))
		# test/malloc asserts that malloc() returns NULL with ENOMEM for a
		# request that cannot be satisfied -- what C99 7.20.3.3p3 requires.
		# ASan's default allocator_may_return_null=0 aborts inside its own
		# allocator on such a request, so that path is never reached; the
		# option makes ASan behave like a conforming allocator instead, so
		# it permits the behaviour under test rather than relaxing a check.
		# test/malloc.c defines __asan_default_options() to the same effect,
		# but the dynamic runtime this script needs (-shared-libasan) never
		# lets a program's definition preempt its own, so it is set here too
		# -- for that one test, so every other test keeps the strict default.
		aopts=detect_leaks=$LEAKS
		[ "$n" = malloc ] && aopts=$aopts,allocator_may_return_null=1
		if ASAN_OPTIONS=$aopts UBSAN_OPTIONS=print_stacktrace=1 \
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

# implicit-integer-sign-change is recoverable, so a test that reports one
# still passes and the report scrolls by unread.  Collect the distinct
# sites and say how many there were.  (The truncation checks are fatal, so
# they turn up as a FAIL above and need no summary.)
if [ "${NTLIBC_ASAN_CONVERSION:-0}" = 1 ]; then
	nconv=$(grep -h 'runtime error: implicit conversion' "$OBJ"/test/*.out 2>/dev/null \
		| sed 's/: runtime error.*//' | sort -u | tee "$OBJ/conversion.txt" | wc -l)
	echo "asan: $nconv implicit-conversion site(s) -> $OBJ/conversion.txt (report-only)"
fi
[ "$passed" = "$ran" ]
