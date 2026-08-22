#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build and run the libFuzzer harnesses in fuzz/.  See CONTRIBUTING.md.
#
#   tools/fuzz.sh                 every harness, 60s each
#   tools/fuzz.sh 300             every harness, 300s each
#   tools/fuzz.sh 300 strtod utf  just those, 300s each
#   tools/fuzz.sh --repro <file> <harness>   re-run one saved input
#
# The fuzzers run without an on-disk corpus, and that is not laziness.
# libFuzzer reads its corpus and writes its crash artefacts through the C
# library it is linked against, which here is ntlibc -- and ntlibc's
# open/stat/readdir go to NtCreateFile and friends, which a native build
# does not have (fuzz/ntstubs.c answers STATUS_NOT_IMPLEMENTED).  So a
# corpus directory would come back as "The required directory does not
# exist".  Instead each harness prints the input that failed, and
# libFuzzer prints the crashing unit as Base64; --repro feeds either back
# in.  A reproducer worth keeping belongs in test/ as a case in the
# matching test/*.c, not in a corpus directory.
#
# -print_funcs=0 matters for speed: symbolizing each newly reached
# function shells out to llvm-symbolizer against a 200-object binary and
# costs about thirty seconds a time.  Crash reports are still symbolized.
#
# Exits non-zero if any harness finds something.

set -eu
srcdir=$(cd "$(dirname "$0")/.." && pwd)

# Leak detection on, for the reason given in tools/asan-build.sh: ntlibc's
# heap is ASan's heap here, so LSan can account for every block.  A fuzzer
# is where a leak per call shows up fastest -- libFuzzer checks after each
# input, so one leaking format string is reported in seconds rather than
# waiting for an absurd RSS to be noticed.  It found the sprintf/snprintf
# one-byte-per-call leak the moment it was switched on.
LEAKS=${NTLIBC_LEAKS:-1}

# The harnesses use the shared ASan runtime (see tools/asan-build.sh for
# why), and libFuzzer drags in libstdc++, which the linker puts ahead of
# it.  ASan refuses to start unless its runtime is first in the library
# list, so preload it -- which is what its own error message asks for.
#
# Deliberately not exported: the preload must apply to the harnesses when
# they *run*, not to the build.  Exporting it puts clang, and in
# particular ld, under ASan too, and the linker's ordinary allocations
# then trip the leak checker and fail the link:
#   SUMMARY: AddressSanitizer: 1596455 byte(s) leaked in 3224 allocation(s)
#   clang: error: linker command failed with exit code 1
# It is set per-invocation below instead.
ASAN_SO=$(${CC:-clang} -print-file-name=libclang_rt.asan-x86_64.so)

if [ "${1:-}" = "--repro" ]; then
	shift
	[ $# -ge 1 ] || { echo "usage: tools/fuzz.sh --repro <artefact>" >&2; exit 2; }
	art=$1
	name=${2:-$(basename "$(dirname "$art")")}
	make -C "$srcdir/fuzz" "$srcdir/obj/fuzz/fuzz_$name" >/dev/null
	exec env LD_PRELOAD="$ASAN_SO" \
	     ASAN_OPTIONS=detect_leaks=$LEAKS UBSAN_OPTIONS=print_stacktrace=1 \
	     "$srcdir/obj/fuzz/fuzz_$name" "$art"
fi

time=${1:-60}
[ $# -gt 0 ] && shift || true
harnesses=${*:-"strtod printf scanf utf"}

if [ ! -f "$srcdir/obj/include/bits/alltypes.h" ]; then
	echo "fuzz: run 'make' first (obj/include/bits/alltypes.h is missing)" >&2
	exit 1
fi

make -C "$srcdir/fuzz" all
rc=0
for h in $harnesses; do
	echo "== fuzz_$h (${time}s)"
	if ! LD_PRELOAD="$ASAN_SO" \
	     ASAN_OPTIONS=detect_leaks=$LEAKS UBSAN_OPTIONS=print_stacktrace=1 \
	     "$srcdir/obj/fuzz/fuzz_$h" \
	     -max_total_time="$time" -max_len=256 -print_funcs=0 -print_final_stats=1; then
		echo "   fuzz_$h FOUND SOMETHING (input shown above)"
		rc=1
	fi
done
exit $rc
