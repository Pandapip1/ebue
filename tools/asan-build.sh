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
# not.  Two kinds left: those that assert the target ABI, which a native
# compiler does not have (math's 80- vs 64-bit long double; strto used
# to be here too, see test/strto.c, now fixed instead of skipped); and
# posix-misc, blocked on an actual architecture mismatch, not a build-
# system one (see its entry below).  Process cloning (fork()) is not on
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
	spawn-runtimedata-stress)
		echo "needs RuntimeData-based descriptor inheritance for a fd above 2, which this stub's RtlCreateUserProcess (fuzz/ntstubs.c) does not model: it execve()s a real host binary, and the fresh child's __ntshim_init constructor wires up only StandardInput/Output/Error (FD2H(0..2)) before calling __fd_init -- there is no PEB-parameters blob carrying a RuntimeData table across that real execve the way real NT's process-parameters copy does. Covered by 'make check' under Wine (and real Windows CI) instead, where RtlCreateUserProcess is the real thing" ;;
	sh-main)
		echo "spawns obj/sh/sh.exe, a PE program built by \$(CC) from sh/main.c -- this build compiles src/*.c natively and produces no such binary at all, so the test would find nothing to exercise (it exits 77 rather than passing vacuously). What it covers is the shell *utility's* argument handling, exit status and diagnostics, which needs a real process; the engine those diagnostics come from is src/sh/*.c, which this build does compile and test/sh-engine.c does run here. Covered by 'make check' under Wine, and on the real-Windows CI leg, whose artefact carries obj/sh/sh.exe for exactly this reason -- see the upload step in .github/workflows/ci.yml" ;;
	*)  echo "" ;;
	esac
}


TINC="-I$srcdir/obj/include -I$srcdir/include -I$srcdir/arch/$ARCH -I$srcdir/arch/generic"
ran=0 passed=0 nolink=0 skipped=0 unverified=0
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
done

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
