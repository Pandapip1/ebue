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
# The corpus persists between runs, in $NTLIBC_FUZZ_CORPUS (default
# obj/fuzzcorpus), one subtree per harness:
#
#   $NTLIBC_FUZZ_CORPUS/<h>/corpus    libFuzzer's minimized corpus
#   $NTLIBC_FUZZ_CORPUS/<h>/crashes   crash-<sha1>, leak-<sha1>, ...
#
# It did not, until fuzz/ntstubs.c grew NTLIBC_FUZZ_MIRROR.  The comment
# that used to be here said a corpus was impossible because libFuzzer's
# file I/O goes through ntlibc to NtCreateFile, "which a native build does
# not have (fuzz/ntstubs.c answers STATUS_NOT_IMPLEMENTED)".  That was not
# true: ntstubs.c implements a whole in-memory volume, and the corpus
# directory was merely never in it.  See the block comment above
# mirror_init() in fuzz/ntstubs.c for the two real reasons, both now
# fixed, and for what is deliberately not mirrored.
#
# Every run therefore starts from what the last one learned.  A crash
# artefact survives too, as a file -- feed it back with --repro.  A
# reproducer worth keeping still belongs in test/ as a case in the
# matching test/*.c; the corpus is search state, not a regression suite.
#
# Set NTLIBC_FUZZ_CORPUS= (empty) to run stateless, the way this script
# used to.
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

# handle_abort=1 is not cosmetic, and ASan does not default it on.
#
# The differential harnesses report a wrong *result* -- something no
# sanitizer can see -- and end the run with the host's abort()
# (fuzz/host_oracle.c's host_abort).  libFuzzer's own SIGABRT handler
# never fires: it installs one with sigaction(), which resolves to
# ntlibc's, which on a native build delivers nothing.  So a mismatch used
# to print three lines to stderr and die with status 134, and libFuzzer
# wrote no crash artefact and named no reproducing input.
#
# With handle_abort=1 the abort goes to ASan, which prints a stack trace
# and then calls the death callback libFuzzer registered -- and *that*
# path does dump the unit:
#
#   Test unit written to .../crashes/crash-adc83b19...
#
# Measured with a deliberately wrong oracle (host_strtod returning r+1):
# without it, no file; with it, the artefact above, which
# `tools/fuzz.sh --repro` replays.

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

# Why --repro used to hang after printing the first two lines of an ASan
# report, and `make -C fuzz run` did not when run from CI.
#
# ASan symbolizes a crash by forking llvm-symbolizer, writing the query
# to its stdin pipe and doing a blocking read() on its stdout pipe.  The
# harness therefore cannot make progress until the symbolizer answers.
#
# llvm-symbolizer (Ubuntu's llvm-symbolizer-18, and any LLVM built with
# LLVM_ENABLE_DEBUGINFOD) looks for a module's debug info in this order:
# /usr/lib/debug/.build-id/<id>.debug, then ~/.cache/llvm-debuginfod,
# then -- if $DEBUGINFOD_URLS is set -- an HTTPS GET to each server in
# it, and only then the DWARF already inside the binary.  Ubuntu exports
# DEBUGINFOD_URLS=https://debuginfod.ubuntu.com from
# /etc/profile.d/debuginfod.sh, so every interactive login shell has it.
# The harnesses are built with -g and carry their own DWARF, and no
# distro server has ever heard of their build-id, so that request can
# only ever fail -- but it is made first, and while it is outstanding
# llvm-symbolizer sits in poll() on the TLS socket and the harness sits
# in read() on the pipe.  Measured on this build: strace shows the
# symbolizer connect to 91.189.92.252:443 and then poll(fd=5, 1000ms) in
# a loop indefinitely; the harness's kernel wchan is anon_pipe_read.
# With DEBUGINFOD_URLS cleared the same query answers in under a second
# from the in-binary DWARF.
#
# That is also the whole of the CI-versus-terminal difference: GitHub
# Actions runs each step under a *non-login* shell, which never sources
# /etc/profile.d, so the workflow's Fuzz step never had the variable set
# and never made the request.
#
# Clearing it costs nothing this script wants: the /usr/lib/debug and
# ~/.cache lookups still happen (they are local and come first), and the
# harness's own frames -- the fuzz_path.c:78:11 an issue reporter needs
# -- come from DWARF inside the executable.  The only thing given up is
# network-fetched debug info for distro shared libraries, which was
# never arriving anyway.
export DEBUGINFOD_URLS=

if [ "${1:-}" = "--repro" ]; then
	shift
	[ $# -ge 1 ] || { echo "usage: tools/fuzz.sh --repro <artefact>" >&2; exit 2; }
	art_in=$1
	# Resolved to an absolute path here, once, and used for both the
	# mirror root and the argument the harness is handed.  A relative
	# artefact used to reach libFuzzer verbatim and die with
	#     ERROR: The required directory "crash-abc" does not exist
	# -- libFuzzer stats the name through ntlibc, which resolves it
	# against ntstubs.c's simulated volume, where the caller's host cwd
	# does not exist.  It is a clear failure rather than a silent wrong
	# answer, but there is no reason to make the reporter in GitHub
	# issue #1 type an absolute path when the script can compute one.
	artdir=$(cd "$(dirname "$art_in")" && pwd) || exit 1
	art=$artdir/$(basename "$art_in")
	name=${2:-$(basename "$artdir")}
	make -C "$srcdir/fuzz" "$srcdir/obj/fuzz/fuzz_$name" >/dev/null
	# The artefact has to be visible in ntstubs.c's simulated volume, or
	# the harness cannot read the file it was just handed.  Before
	# NTLIBC_FUZZ_MIRROR existed this branch could not work at all: it
	# answered every artefact with
	#     ERROR: The required directory "<file>" does not exist
	# which is libFuzzer saying it could not stat the path -- the
	# documented way to replay a finding, replaying nothing.
	exec env NTLIBC_FUZZ_MIRROR="$artdir" \
	     LD_PRELOAD="$ASAN_SO" \
	     ASAN_OPTIONS=detect_leaks="$LEAKS":handle_abort=1 \
	     UBSAN_OPTIONS=print_stacktrace=1 \
	     "$srcdir/obj/fuzz/fuzz_$name" "$art"
fi

time=${1:-60}
if [ $# -gt 0 ]; then shift; fi
harnesses=${*:-"strtod printf scanf utf path strptime strtol strftime fnmatch regex glob wordexp shparse inet string search getopt env"}

if [ ! -f "$srcdir/obj/include/bits/alltypes.h" ]; then
	echo "fuzz: run 'make' first (obj/include/bits/alltypes.h is missing)" >&2
	exit 1
fi

corpus=${NTLIBC_FUZZ_CORPUS-$srcdir/obj/fuzzcorpus}

make -C "$srcdir/fuzz" all
rc=0
for h in $harnesses; do
	# The before/after counts are printed, not asserted: the point of
	# persisting a corpus is that it grows, and a run that reports the same
	# number twice is telling you the mirror stopped working.
	if [ -n "$corpus" ]; then
		mkdir -p "$corpus/$h/corpus" "$corpus/$h/crashes"
		before=$(find "$corpus/$h/corpus" -type f | wc -l)
		echo "== fuzz_$h (${time}s, corpus $before)"
		set -- -artifact_prefix="$corpus/$h/crashes/" "$corpus/$h/corpus"
		mirror=$corpus/$h
	else
		echo "== fuzz_$h (${time}s, no corpus)"
		set --
		mirror=
	fi
	if ! NTLIBC_FUZZ_MIRROR="$mirror" LD_PRELOAD="$ASAN_SO" \
	     ASAN_OPTIONS=detect_leaks="$LEAKS":handle_abort=1 \
	     UBSAN_OPTIONS=print_stacktrace=1 \
	     "$srcdir/obj/fuzz/fuzz_$h" \
	     -max_total_time="$time" -max_len=256 -print_funcs=0 -print_final_stats=1 \
	     "$@"; then
		echo "   fuzz_$h FOUND SOMETHING (input shown above)"
		if [ -n "$corpus" ]; then
			echo "   artefacts: $corpus/$h/crashes"
			find "$corpus/$h/crashes" -type f 2>/dev/null | sed 's/^/     /'
			echo "   reproduce: tools/fuzz.sh --repro $corpus/$h/crashes/<file> $h"
		fi
		rc=1
	fi
	if [ -n "$corpus" ]; then
		echo "   corpus $before -> $(find "$corpus/$h/corpus" -type f | wc -l)"
	fi
done
exit $rc
