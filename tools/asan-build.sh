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
#        ASAN_JOBS (default: nproc) -- how many src/*.c compiles and how
#          many test links to run at once.  The test *runs* are always
#          serial; see the comment above the link phase for why that is
#          a correctness constraint and not a tuning decision.  Output is
#          byte-identical at any value.

set -eu

srcdir=$(cd "$(dirname "$0")/.." && pwd)
CC=${NTLIBC_CC:-clang}
OBJ=${NTLIBC_ASAN_OBJ:-$srcdir/obj/asan}
ARCH=${NTLIBC_ARCH:-x86_64}

# This build compiles src/*.c *natively* (64-bit ELF) but includes
# obj/include/bits/alltypes.h, which `make` generates from
# arch/$(ARCH)/bits/alltypes.h.in and which therefore follows whatever
# arch configure was last run for.  Configure for i386, run `make asan`,
# and a 64-bit build silently picks up 32-bit size_t/ssize_t/intptr_t:
# snprintf("%zd", (ssize_t)-5) prints 4294967291, SIZE_MAX != (size_t)-1,
# and stdio/fcntl fault outright.  Those look exactly like library bugs
# and were reported as such, repeatedly, before anyone noticed the build
# was simply mismatched.
#
# Refuse instead of producing that.  config.mak is the record of what the
# tree is configured for; if it disagrees with the arch this script is
# building for, stop and say how to fix it.
if [ -f "$srcdir/config.mak" ]; then
	cfg_arch=$(sed -n 's/^ARCH *= *//p' "$srcdir/config.mak" | head -1)
	if [ -n "$cfg_arch" ] && [ "$cfg_arch" != "$ARCH" ]; then
		echo "asan: tree is configured for ARCH=$cfg_arch but this build is $ARCH." >&2
		echo "asan: obj/include/bits/alltypes.h would give a $cfg_arch-width" >&2
		echo "asan: size_t/ssize_t to a native $ARCH build -- wrong, and it" >&2
		echo "asan: fails in ways that look like library bugs." >&2
		echo "asan: reconfigure first (./configure --target=$ARCH-win32 CC=$ARCH-win32-tcc)," >&2
		echo "asan: or set NTLIBC_ARCH=$cfg_arch if you really meant that." >&2
		exit 2
	fi
fi
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

# See the long comment in tools/fuzz.sh: with $DEBUGINFOD_URLS set (Ubuntu
# exports it from /etc/profile.d/debuginfod.sh, so every login shell has
# it), llvm-symbolizer makes a doomed HTTPS request for each module's
# build-id before it reads the DWARF already inside the binary, and ASan's
# blocking read() on the symbolizer pipe stalls with it.  Cleared here so a
# failing test still prints a symbolized trace instead of being killed by
# the timeout below with nothing to show.
export DEBUGINFOD_URLS=

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
# plus a 4x90s libFuzzer run of fuzz/ (~6.5M execs per harness).  That
# measurement was taken when fuzz/ held four harnesses; it now holds
# eight (path, printf, scanf, strftime, strptime, strtod, strtol, utf).
# The four added since have NOT been measured for implicit-conversion
# findings, so the counts below describe the four that were -- they are
# not a claim about the current set:
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
# -fsanitize=unsigned-integer-overflow,unsigned-shift-base (the "integer"
# group's checks beyond -fsanitize=undefined) are not undefined behaviour
# -- unsigned wraparound is modular arithmetic, C99 6.2.5p9 -- so the
# point of enabling them is not finding UB but forcing every deliberate
# wraparound in the library to say so via __wraps (include/features.h),
# leaving an unmarked one visible as a real finding.  Fatal, like the
# truncation checks above, and library-only: never test/*.c or ntstubs.c.
INTSAN="-fsanitize=unsigned-integer-overflow,unsigned-shift-base \
 -fno-sanitize-recover=unsigned-integer-overflow,unsigned-shift-base"

# NTLIBC_USE_KERNEL32 is deliberately never defined here, unlike the real
# tcc/config.mak build (see the Makefile's CFLAGS_ALL). This build has no
# real kernel32 -- ntstubs.c stands in for ntdll, not for kernel32 on top
# of it -- and crt1.c calls __signal_init() unconditionally, so turning
# the define on would require this file to answer LdrLoadDll() (loading
# "kernel32.dll") and LdrGetProcedureAddress() (resolving
# "SetConsoleCtrlHandler" specifically) for *every* test and fuzz binary,
# not just ones that care about it -- crt1.c is linked into all of them.
# That part is a bounded, ~30-line addition (a fake module handle from
# LdrLoadDll, a name comparison and a stub SetConsoleCtrlHandler from
# LdrGetProcedureAddress) and would be worth doing the day something
# here actually needs to drive src/signal/signal.c's ctrl_handler().
# Nothing does yet: none of fuzz/fuzz_*.c touch signal handling, and even
# with the stubs in place there would be no way to *invoke*
# ctrl_handler() from here -- a native Linux process cannot receive a
# real console control event, and libFuzzer's byte-stream inputs have no
# natural mapping onto one either. So the stubs would only buy coverage
# of install_ctrl_handler()'s two Ldr* calls succeeding, not of the
# handler logic they install, which is the actual point of the guarded
# code. That handler logic already gets run for real -- against genuine
# kernel32.dll, under Wine and on real Windows -- by `make check` and CI's
# windows-test job on an --enable-kernel32 build (see
# .github/workflows/ci.yml); that is the right place for it, not a
# simulation here.
CFLAGS="$SAN $CONVSAN $INTSAN -g -O1 -std=c99 -nostdinc -fno-builtin -fvisibility=hidden \
        -D_XOPEN_SOURCE=700 -D_NTLIBC_INTERNAL $INC $EXTRA"

if [ ! -f "$srcdir/obj/include/bits/alltypes.h" ]; then
	echo "asan: obj/include/bits/alltypes.h missing -- run 'make' first" >&2
	exit 1
fi

# Two concurrent runs share $OBJ and clobber each other: the `rm -rf`
# below deletes objects the other run is still compiling into and linking
# against.  The build then fails in ways that look nothing like the real
# cause and everything like a bug elsewhere in the library -- a SEGV
# inside memcpy, or a test that appears to have been broken all along.
# That misdiagnosis is expensive and has happened more than once, so this
# is enforced rather than documented.  mkdir is atomic, so it is both the
# check and the lock; a run killed with SIGKILL leaves the directory
# behind, which is what the second message below is for.
if ! mkdir "$OBJ.lock" 2>/dev/null; then
	echo "asan: another build is using $OBJ ($OBJ.lock exists)." >&2
	echo "asan: wait for it, or remove the lock if no build is running." >&2
	exit 1
fi
trap 'rmdir "$OBJ.lock" 2>/dev/null || :' EXIT INT TERM

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

# How many compiles and links to run at once.  These are 282 independent
# clang invocations followed by 54 independent links; each reads the
# source tree and writes only files named after its own input, so this
# parallelises with no change to what is built.  ASAN_JOBS=1 restores
# the fully serial behaviour, and is the safe fallback when neither
# nproc nor getconf exists.
: "${ASAN_JOBS:=}"
if [ -z "$ASAN_JOBS" ]; then
	ASAN_JOBS=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
fi

# Phase 1a, serial and process-free: decide what happens to each source.
# The worklist line is "index<TAB>file<TAB>mode<TAB>reason", where mode
# is `skip`, `full` or `ubsan`.
#
# The index is what keeps this reproducible.  Nothing below appends to
# compiled.txt/skipped.txt/partial.txt from a worker, and NOTHING appends
# to them from this pass either: every line of all three manifests is
# written by the serial merge in phase 1c, in source order, exactly as
# the single serial loop this replaces wrote them.  Concurrent appends to
# a shared file are not guaranteed atomic once a write exceeds a pipe
# buffer, and even when they are, the *order* would depend on scheduling
# -- so the manifests this script's own report points a reader at would
# differ run to run on an unchanged tree.  Deciding early and emitting
# late costs one extra field in the worklist and buys byte-identical
# output.
cwork="$OBJ/compile-worklist"
: > "$cwork"
cidx=0
for f in $(cd "$srcdir" && find src -name '*.c' | sort); do
	cidx=$((cidx + 1))
	# src/<area>/<arch>/<file>.c overrides src/<area>/<file>.c; keep only ours
	sub=$(echo "$f" | awk -F/ 'NF==4{print $3}')
	if [ -n "$sub" ] && [ "$sub" != "$ARCH" ]; then
		printf '%06d\t%s\tskip\t(other architecture)\n' "$cidx" "$f" >> "$cwork"
		continue
	fi
	case $f in
	*/teb.c)
		printf '%06d\t%s\tskip\t(reads gs:0x30; __teb() comes from ntstubs.c)\n' \
			"$cidx" "$f" >> "$cwork"
		continue ;;
	esac
	# A few files use the musl aligned-word scan: align to sizeof(size_t),
	# then read whole words.  Such a read can go past the end of the string
	# object but never past the end of its page, so it is safe in fact --
	# ASan tracks objects, not pages, and reports every call.  Build those
	# with UBSan only, and say so, rather than drown the run in noise.
	case $f in
	src/string/strlen.c)
		printf '%06d\t%s\tubsan\t(built UBSan-only: aligned word-at-a-time scan)\n' \
			"$cidx" "$f" >> "$cwork" ;;
	*)
		printf '%06d\t%s\tfull\t-\n' "$cidx" "$f" >> "$cwork" ;;
	esac
done

# Phase 1b, parallel.  compile_one INDEX FILE MODE always writes
# $cpar/INDEX.rc -- `ok` or `fail` -- including on success, because the
# merge below counts those files.  A worker that dies without reporting
# is then a hard failure rather than a source file that quietly stops
# being compiled: dropping objects here would shrink the library the
# tests link against, which is precisely how `make asan` once reported
# success having verified nothing.
UBSAN_ONLY_CFLAGS=$(echo "$CFLAGS" | sed 's/-fsanitize=address,undefined/-fsanitize=undefined/')
cpar="$OBJ/cpar"
mkdir -p "$cpar" || exit 1
compile_one() {
	c_idx=$1 c_f=$2 c_mode=$3
	case $c_mode in
	ubsan) c_flags=$UBSAN_ONLY_CFLAGS ;;
	*)     c_flags=$CFLAGS ;;
	esac
	c_o="$OBJ/obj/$(echo "$c_f" | tr / _).o"
	# $c_flags is a flag list and must word-split.
	# shellcheck disable=SC2086
	if $CC -c $c_flags -w "$srcdir/$c_f" -o "$c_o" 2> "$c_o.err"; then
		echo ok > "$cpar/$c_idx.rc"
	else
		rm -f "$c_o"
		echo fail > "$cpar/$c_idx.rc"
	fi
}

cshard=0
while [ "$cshard" -lt "$ASAN_JOBS" ]; do
	(
		awk -v n="$ASAN_JOBS" -v k="$cshard" 'NR % n == k' "$cwork" \
		| while IFS="$(printf '\t')" read -r idx f cmode creason; do
			[ -z "$idx" ] && continue
			[ "$cmode" = skip ] && continue
			compile_one "$idx" "$f" "$cmode"
		done
		# Explicit, because the alternative is worse than it looks.
		# A shard whose last loop iteration ends on a `[ ... ] &&
		# continue` that evaluates false exits 1 -- for no reason but
		# which files happened to land in which shard.  Measured, in
		# this shell: `wait` with no operands then returns 0 ANYWAY,
		# even for `( exit 3 ) &`, and even under `set -e`.  So the
		# failure would not have aborted the run; it would have been
		# swallowed.  That cuts both ways and the second way is the one
		# that matters: A REWRITE THAT JUDGED ITS WORKERS BY `wait`
		# WOULD REPORT SUCCESS NO MATTER WHAT THEY DID.  This design
		# never asks.  A worker succeeded if and only if it wrote its
		# result file, and the merge below counts those.  `exit 0` says
		# that the shard's own status is deliberately meaningless rather
		# than accidentally so.
		exit 0
	) &
	cshard=$((cshard + 1))
done
wait

# Phase 1c, serial merge in source order.  This is where all three
# manifests are written, so their contents and their order are what the
# serial loop produced, whatever ASAN_JOBS was.
cmissing=0
while IFS="$(printf '\t')" read -r idx f cmode creason; do
	[ -z "$idx" ] && continue
	if [ "$cmode" = skip ]; then
		echo "$f  $creason" >> "$OBJ/skipped.txt"
		continue
	fi
	[ "$cmode" = ubsan ] && echo "$f  $creason" >> "$OBJ/partial.txt"
	o="$OBJ/obj/$(echo "$f" | tr / _).o"
	if [ ! -f "$cpar/$idx.rc" ]; then
		cmissing=$((cmissing + 1))
		echo "$f  (NO RESULT: the compile worker for this file never reported)" >> "$OBJ/skipped.txt"
		rm -f "$o"
		continue
	fi
	if [ "$(cat "$cpar/$idx.rc")" = ok ]; then
		echo "$f" >> "$OBJ/compiled.txt"
	else
		echo "$f  (see $o.err)" >> "$OBJ/skipped.txt"
	fi
done < "$cwork"
if [ "$cmissing" -ne 0 ]; then
	echo "asan: FAILED -- $cmissing compile worker(s) never reported a result, so those" >&2
	echo "asan: source files were never built.  A parallel phase that loses work must fail," >&2
	echo "asan: not link a smaller library and call the run a pass." >&2
	exit 1
fi

# ntstubs.c is test-support code, not part of the library, so it does not
# get the intentional-wraparound scrutiny below: build it without INTSAN.
stubcflags="$(echo "$CFLAGS" | sed "s!$INTSAN!!")"
# shellcheck disable=SC2086
$CC -c $stubcflags -w "$srcdir/fuzz/ntstubs.c" -o "$OBJ/ntstubs.o"

# An archive would be wrong here.  libclang_rt.asan.so exports weak
# strcmp/strlen/strxfrm/memcpy/... interceptors and the driver puts it
# ahead of our inputs, so every one of those references would be satisfied
# from the DSO and the matching archive member never pulled -- i.e. the
# tests would be exercising glibc, not ntlibc.  Linking the objects
# unconditionally, hidden, makes ntlibc's definitions the ones that bind.
#
# One consequence of that is worth stating, because it cost a long
# debugging session: the precompiled runtime linked in here (libFuzzer,
# compiler-rt, libstdc++) does not only get preempted, it also *calls*
# some of these libc-named functions -- and it was compiled against the
# host's headers, so it stack-allocates the host's struct sizes.  ntlibc's
# definition wins the call, so an ntlibc struct that is *larger* than the
# host's, for one of the functions that runtime calls, makes the callee
# write past the caller's frame.  struct rusage did exactly that: a stray
# `long __reserved[16]` made it 272 bytes against the host's 144, and
# getrusage()'s memset(ru, 0, sizeof *ru) smashed the return address of
# libFuzzer's GetPeakRSSMb() -- every harness died at execution #2 with a
# jump to address 0, for as long as the harnesses had existed.
#
# This is a constraint on *this build*, not on ntlibc's ABI.  ntlibc's
# headers are the ones its users compile against, and matching glibc's
# layouts is explicitly not a goal.  It binds only the handful of
# functions the precompiled runtime itself calls, and only in the
# direction of "must not be bigger than the host's": a struct that is
# smaller (struct stat, for one) is harmless here, since the host-ABI
# caller simply over-allocates.
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
# not.  The kinds left: those that assert the target ABI, which a native
# compiler does not have (math's 80- vs 64-bit long double; strto used
# to be here too, see test/strto.c, now fixed instead of skipped); and
# posix-misc, blocked on an actual architecture mismatch, not a build-
# system one (see its entry below); and those that reach an NT-only
# primitive a native process has no counterpart for -- the Ldr* loader
# entry points (spawn-stdhandle-attr, posix-rename-symlink), which do not
# even link here.  Process cloning (fork()) is not on
# this list any more: fuzz/ntstubs.c's RtlCloneUserProcess is a real host
# fork(2), not a stub.  Neither is the wait-status pair (waitpid-overflow,
# posix-signal): fuzz/ntstubs.c now carries a dying process's full exit
# code to its reaper out of band, alongside the host's own 8-bit one (see
# xstatus_record()/xstatus_init() there).  Nothing else is excused -- in
# particular a genuine ASan or UBSan finding in ntlibc must fail here.
not_native()
{
	case $1 in
	posix-dl)
		echo "calls ntlibc_rpath_load/_sym/_error to demonstrate how much of dlfcn.h already exists; those live in src/internal/rpath.c, which is NT-only (LdrLoadDll/LdrGetProcedureAddress against a real NT image) and so is excluded from this build -- see obj/asan/skipped.txt. Its dlfcn/mman/termios/spawn clauses are fenced UNIMPL/N-A anyway; the live parts run under 'make check'" ;;
	rpath)
		echo "exercises the delay-load/\$ORIGIN machinery in src/internal/{rpath,delayload}.c, which is PE-only (LdrLoadDll/LdrGetProcedureAddress against a real NT image) and is therefore not compiled into this build at all -- see obj/asan/skipped.txt. Covered by 'make check' under Wine instead" ;;
	delayall)
		echo "proof of the -Wl,--delay-all path (crt/delayload2.c, PE-only, same reason as rpath/delayload.c above) against a plugin DLL built with a real PE tcc -- neither the delay-load runtime nor a matching delayall_check() exists for this native build to link against. Covered by 'make check' under Wine instead, on both arches" ;;
	posix-misc)
		echo "uses sigsetjmp, whose src/setjmp/x86_64/setjmp.S is genuinely Win64-ABI machine code (first arg in %rcx, xmm6-15 treated as callee-saved) -- not merely unbuilt, but wrong if assembled for a SysV caller: %rcx is not this ABI's first-argument register and its xmm6-15 are caller-saved scratch, so jmp_buf would be silently corrupted rather than just fail to link" ;;
	spawn-stdhandle-attr)
		echo "resolves NtCreateUserProcess itself, at run time, with LdrGetDllHandle()/LdrGetProcedureAddress() against a loaded ntdll.dll (see its resolve_ncup()) -- module-handle and export-table primitives that only the NT loader has, and that fuzz/ntstubs.c cannot stand in for: there is no ntdll image in a native ELF process to hand back a handle to, and the syscall it goes on to look up is the very thing under test, so a stub answering it would be testing the stub. Its subject is what real NT's PsAttributeStdHandleInfo does to the child's process parameters, which needs a real NT process anyway. Covered by 'make check' under Wine (and real Windows CI)" ;;
	posix-rename-symlink)
		echo "builds a directory-flavoured reparse point with Win32 CreateSymbolicLinkW(SYMBOLIC_LINK_FLAG_DIRECTORY), resolved at run time through LdrLoadDll()/LdrGetProcedureAddress() because ntlibc declares no kernel32 imports (see test_rename_dir_over_forced_directory_symlink()). Those two Ldr* entry points are NT-loader primitives with no stub in fuzz/ntstubs.c, so the whole file fails to link natively -- 'undefined reference to LdrGetProcedureAddress / LdrLoadDll' -- not just that one group. A stub cannot supply them either: there is no kernel32.dll PE image in an ELF process to load or to walk an export table of, and the object the export produces is the very thing under test -- an entry carrying FILE_ATTRIBUTE_DIRECTORY and FILE_ATTRIBUTE_REPARSE_POINT at once, which is NT's file-attribute model and not something a host symlink(2) has. Standing in with a POSIX symlink would delete the subject and leave the measurement asserting against the stand-in, the same objection recorded for spawn-stdhandle-attr above. Covered by 'make check' under Wine (and real Windows CI)" ;;
	spawn-runtimedata-stress)
		echo "needs RuntimeData-based descriptor inheritance for a fd above 2, which this stub's RtlCreateUserProcess (fuzz/ntstubs.c) does not model: it execve()s a real host binary, and the fresh child's __ntshim_init constructor wires up only StandardInput/Output/Error (FD2H(0..2)) before calling __fd_init -- there is no PEB-parameters blob carrying a RuntimeData table across that real execve the way real NT's process-parameters copy does. Covered by 'make check' under Wine (and real Windows CI) instead, where RtlCreateUserProcess is the real thing" ;;
	posix-kill-perm-win)
		echo "asserts that NT denies PROCESS_TERMINATE on the protected System process (pid 4), which is NT access-control policy and not something this build has. fuzz/ntstubs.c's NtOpenProcess is NOTIMPL, so it answers STATUS_NOT_IMPLEMENTED, which src/signal/signal.c's kill() correctly maps to ESRCH rather than EPERM -- the test then fails on an assertion about real NT while measuring a stub. Teaching the stub to answer STATUS_ACCESS_DENIED for pid 4 would be modelling Windows' process table inside the stub and then asserting against the model, the same objection recorded for spawn-stdhandle-attr above. Its subject needs a real NT process table; it runs on the real-Windows CI leg, which is what *-win.c is for. NOTE: this is a per-test exclusion on purpose -- do NOT generalise it to a *-win pattern. The -win suffix means 'Wine cannot run this', which is a different axis from 'the native stub build cannot run this': fork-win, fork-handles-win, fork-cloexec-exec-win and process-win all PASS here, because this build has a real fork() where Wine lacks RtlCloneUserProcess" ;;
	sh-main)
		echo "spawns obj/sh/sh.exe, a PE program built by \$(CC) from sh/main.c -- this build compiles src/*.c natively and produces no such binary at all, so the test would find nothing to exercise (it exits 77 rather than passing vacuously). What it covers is the shell *utility's* argument handling, exit status and diagnostics, which needs a real process; the engine those diagnostics come from is src/sh/*.c, which this build does compile and test/sh-engine.c does run here. Covered by 'make check' under Wine, and on the real-Windows CI leg, whose artefact carries obj/sh/sh.exe for exactly this reason -- see the upload step in .github/workflows/ci.yml" ;;
	*)  echo "" ;;
	esac
}


TINC="-I$srcdir/obj/include -I$srcdir/include -I$srcdir/arch/$ARCH -I$srcdir/arch/generic"
ran=0 passed=0 nolink=0 skipped=0 unverified=0
: > "$OBJ/unlinkable.txt"

# ---- 2a. link every test, in parallel --------------------------------------
#
# The link and the run used to be one loop body.  They are separated
# because only ONE of the two halves is safe to run concurrently, and
# saying which is the whole point of the split:
#
#   The links are.  54 independent $CC invocations, each reading the same
#   read-only $LIBOBJS and writing one output named after its own test.
#   They are the same shape as the 282 compiles above and are the second
#   half of this script's build cost.
#
#   The RUNS ARE NOT, and they are deliberately left serial.  Every test
#   executes with this script's own working directory -- the source tree
#   root -- and several write fixed-name files into it: .hidden-glob-test,
#   cap1.txt..cap4.txt, script1.sh, script2.sh, sh-main-out.txt,
#   gfi1.gfitxt (that is what .gitignore's eleven entries are, ae93540).
#   test/posix-glob.c additionally *globs* that directory, so it does not
#   merely need its own names, it needs nothing else creating files
#   beside it while it looks.  Running these concurrently in one shared
#   directory would produce failures that depend on timing, in a
#   sanitizer build whose entire purpose is to make failures
#   deterministic.  Giving each test a private mktemp -d cwd -- what
#   tools/runtests.sh already does -- would fix that and is worth doing,
#   but it is a behavioural change to how every test sees the world and
#   belongs in its own commit with its own evidence, not smuggled in
#   behind a speedup.
#
# link_one INDEX NAME TESTSRC always writes $lpar/INDEX.rc, for the same
# reason compile_one does.
lwork="$OBJ/link-worklist"
: > "$lwork"
lidx=0
for t in $(cd "$srcdir" && echo test/*.c); do
	lidx=$((lidx + 1))
	n=$(basename "$t" .c)
	printf '%06d\t%s\t%s\n' "$lidx" "$n" "$t" >> "$lwork"
done

lpar="$OBJ/lpar"
mkdir -p "$lpar" || exit 1
link_one() {
	l_idx=$1 l_n=$2 l_t=$3
	l_exe="$OBJ/test/$l_n"
	# $SAN/$TINC/$LINKFLAGS/$LIBOBJS are flag and object lists: word-split.
	# shellcheck disable=SC2086
	if $CC $SAN -g -O1 -std=c99 -nostdinc -fno-builtin -D_XOPEN_SOURCE=700 -D_GNU_SOURCE -w \
	     $TINC $LINKFLAGS "$srcdir/$l_t" "$OBJ/ntstubs.o" $LIBOBJS -o "$l_exe" \
	     2> "$l_exe.link.err"; then
		echo ok > "$lpar/$l_idx.rc"
	else
		echo fail > "$lpar/$l_idx.rc"
	fi
}

lshard=0
while [ "$lshard" -lt "$ASAN_JOBS" ]; do
	(
		awk -v n="$ASAN_JOBS" -v k="$lshard" 'NR % n == k' "$lwork" \
		| while IFS="$(printf '\t')" read -r idx n t; do
			[ -z "$idx" ] && continue
			[ -n "$(not_native "$n")" ] && continue
			link_one "$idx" "$n" "$t"
		done
		# See the compile shard's `exit 0` above for why this is here.
		exit 0
	) &
	lshard=$((lshard + 1))
done
wait

# ---- 2b. run them, serially, in the original order -------------------------
while IFS="$(printf '\t')" read -r idx n t; do
	[ -z "$idx" ] && continue
	exe="$OBJ/test/$n"
	why=$(not_native "$n")
	if [ -n "$why" ]; then
		skipped=$((skipped + 1))
		[ "$mode" = "--quiet" ] || echo "  SKIP $n  ($why)"
		continue
	fi
	if [ ! -f "$lpar/$idx.rc" ]; then
		# See 2a: a link worker that never reported.  Counted as
		# unlinkable rather than dropped, so the `nolink` floor below
		# catches it -- a test that silently stops being linked is
		# exactly the shape of defect 1.
		nolink=$((nolink + 1))
		echo "$n: NO RESULT -- the link worker for this test never reported" \
			>> "$OBJ/unlinkable.txt"
		continue
	fi
	if [ "$(cat "$lpar/$idx.rc")" = ok ]; then
		ran=$((ran + 1))
		# test/malloc and test/posix-alloc assert that malloc()/calloc()/
		# realloc() return NULL with ENOMEM for a request that cannot be
		# satisfied -- what C99 7.20.3.3p3 requires. ASan's default
		# allocator_may_return_null=0 aborts inside its own allocator on
		# such a request, so that path is never reached; the option makes
		# ASan behave like a conforming allocator instead, so it permits
		# the behaviour under test rather than relaxing a check. Both
		# files define __asan_default_options() to the same effect, but
		# the dynamic runtime this script needs (-shared-libasan) never
		# lets a program's definition preempt its own, so it is set here
		# too -- for just these tests, so every other test keeps the
		# strict default.
		aopts=detect_leaks=$LEAKS
		case $n in
		malloc|posix-alloc) aopts=$aopts,allocator_may_return_null=1 ;;
		esac
		if ASAN_OPTIONS=$aopts UBSAN_OPTIONS=print_stacktrace=1 \
		   timeout 120 "$exe" > "$exe.out" 2>&1 < /dev/null; then
			passed=$((passed + 1))
			[ "$mode" = "--quiet" ] || echo "  PASS $n"
		else
			rc=$?
			if [ "$rc" = 77 ]; then
				# Same "ran, but declined to verify something it detected
				# at run time" outcome tools/runtests.sh's own rc=77
				# bucket reports (test/posix-socket.c's network probe,
				# specifically: this build's fuzz/ntstubs.c stub volume
				# has no \Device\Afd node, so socket() itself fails
				# here). Not a pass -- nothing was verified -- and not a
				# FAIL either, since nothing that ran gave a wrong
				# answer.
				unverified=$((unverified + 1))
				[ "$mode" = "--quiet" ] || echo "  UNVERIFIED $n  (output in $exe.out)"
			else
				echo "  FAIL $n  (output in $exe.out)"
				[ "$mode" = "--quiet" ] || sed -n '1,25p' "$exe.out" | sed 's/^/        /'
			fi
		fi
	else
		nolink=$((nolink + 1))
		echo "$n: $(grep -o 'undefined reference to .*' "$exe.link.err" | sort -u | tr '\n' ' ')" \
			>> "$OBJ/unlinkable.txt"
	fi
done < "$lwork"

echo "asan: $passed/$ran tests passed, $unverified unverified, $skipped not applicable natively, $nolink unlinkable"

# implicit-integer-sign-change is recoverable, so a test that reports one
# still passes and the report scrolls by unread.  Collect the distinct
# sites and say how many there were.  (The truncation checks are fatal, so
# they turn up as a FAIL above and need no summary.)
if [ "${NTLIBC_ASAN_CONVERSION:-0}" = 1 ]; then
	nconv=$(grep -h 'runtime error: implicit conversion' "$OBJ"/test/*.out 2>/dev/null \
		| sed 's/: runtime error.*//' | sort -u | tee "$OBJ/conversion.txt" | wc -l)
	echo "asan: $nconv implicit-conversion site(s) -> $OBJ/conversion.txt (report-only)"
fi
# ---- 3. did this stage actually verify anything? --------------------------
#
# The pass condition used to be `passed + unverified == ran` and nothing
# else, which is vacuously true when nothing ran.  That is not a
# hypothetical: commit ad5305b added sched_yield() over NtYieldExecution()
# without a matching fuzz/ntstubs.c stub, every test/*.c links the whole
# instrumented library, so all 48 test binaries stopped linking at once --
# and this stage compiled 282 files under ASan+UBSan, ran zero tests, and
# exited 0.  A green stage that verified nothing is worse than a red one.
#
# So three conditions, not one.
#
# (a) Nothing may fail to link.  This is deliberately `> 0` and not a
#     floor or an allowlist: unlike tools/linkcheck.sh -- whose
#     linkcheck_exception() has to excuse symbols its *call-site
#     generator* cannot express (hsearch/inet_ntoa take a struct by
#     value; the __rpath group resolves a symbol the calling program
#     defines) -- this loop has no generator limitation to excuse.  A
#     test that a native build genuinely cannot link belongs in
#     not_native() above, with a written reason, where it is counted as
#     `skipped` and never reaches this counter.  So every remaining
#     unlinkable test is a missing stub or a real regression, and the
#     right number of those is zero.  $OBJ/unlinkable.txt names them.
#
# (b) Something must have run.  Belt and braces against the next variant
#     of the same failure: if some future change empties this loop by a
#     route that leaves nolink at 0 -- an over-broad not_native(), a glob
#     that matches nothing, a test/ directory that moved -- the stage
#     must not report success for it either.
#
# (c) Everything that ran must have passed or declined to verify, which
#     is the original condition, kept.
rc=0
if [ "$nolink" -gt 0 ]; then
	echo "asan: FAILED -- $nolink test(s) did not link; see $OBJ/unlinkable.txt" >&2
	echo "asan: a test a native build cannot link belongs in not_native() with a reason," >&2
	echo "asan: not silently dropped from the run." >&2
	rc=1
fi
if [ "$ran" -eq 0 ]; then
	echo "asan: FAILED -- no tests ran at all; this stage verified nothing." >&2
	rc=1
fi
[ "$((passed + unverified))" = "$ran" ] || rc=1
exit $rc
