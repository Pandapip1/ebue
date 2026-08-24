#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# lint-unreferenced.sh -- flag a function that a public header declares,
# that this library implements, and that no test/*.c references.
#
# tools/lint-undefined.sh answers "is every declared function defined?".
# This answers the next question along: of the ones that are defined, how
# many does the test suite never so much as call?  That population was
# discovered by hand once already -- test/POSIX-GAP-ACCOUNTING.md's "the
# remaining 112 implemented-but-unasserted functions" is the same set,
# described in prose after an audit -- and writing the very first
# assertion for one of them (vdprintf) immediately exposed a leak.  A
# number that costs an audit to obtain gets recomputed roughly never;
# this makes it a few seconds of a gate instead.
#
# ---------------------------------------------------------------------
# What "referenced" means here, and which way it errs
# ---------------------------------------------------------------------
#
# A name is REFERENCED if a test object file carries an undefined-symbol
# entry for it: the compiler emitted a relocation against that name from
# code it actually compiled.  Every test/*.c is compiled natively to an
# object with the same flags tools/asan-build.sh uses for them, and
# `nm --undefined-only` over the results is the reference set.
#
# This is deliberately narrower than the textual grep the earlier audit
# used (48d4025, "grepped against the concatenation of all test/*.c"),
# and narrower in the direction that matters: a name in a comment is not
# a reference.  `alarm` appears in test/fork-handles-win.c only inside a
# sentence explaining that it is a stub, and the grep counted that.
#
# Three consequences worth stating rather than discovering:
#
#   1. Only DIRECT references count.  test/posix-unistd.c calls symlink(),
#      and src/unistd/link.c's symlink() calls symlinkat() -- so symlinkat
#      is exercised, and this script still reports it, correctly: no test
#      names it, so no test can assert anything specific about it.
#      Transitive reachability is a coverage question and needs a coverage
#      instrument, not this one.
#   2. Code the native compile does not see does not count.  A call inside
#      a preprocessor branch that is off here is not a reference.  That is
#      the intended reading -- an uncompiled call asserts nothing -- and
#      it is why the two files that genuinely cannot compile natively are
#      handled explicitly below rather than silently contributing nothing.
#   3. "Referenced" is not "asserted".  A test that calls a function and
#      ignores the result satisfies this check.  It is still the right
#      first cut: a function no test even mentions is a larger and far
#      more tractable problem than the assertion-quality one behind it.
#   4. A function a public header also #defines as a macro can never
#      produce an undefined symbol, however hard a test calls it, so it
#      would be a permanent false positive.  The analysis this implements
#      supposed there were none of these in the tree; there are exactly
#      two -- include/alloca.h:29-33 defines `alloca` to
#      __builtin_alloca where the compiler has one (test/alloca.c does
#      call it, and the object carries no `alloca` relocation at all),
#      and include/setjmp.h does the same for `setjmp`.  Both are found
#      mechanically rather than listed by name, and the count is printed,
#      so a third one appearing is visible rather than silently excused.
#
# WHICH WAY THIS ERRS: toward reporting FEWER names than are truly
# unreferenced, never more.  Two files (test/rpath.c, test/delayall.c)
# are PE-only and do not compile natively at all; rather than let their
# references vanish -- which would report their subjects as unreferenced
# when they are the best-tested things in the tree -- their identifiers
# are harvested textually, comments and all.  That is the generous
# reading, and it is confined to those two files.  The result is that
# every name this script reports is a name no test object references and
# no PE-only test even mentions, so a reader who investigates one never
# discovers it was already covered.  The cost is that a name mentioned in
# a comment in exactly those two files is let through; today that
# suppresses two names (gets, ntlibc_delayLoadHelper2), which is a price
# worth paying for a worklist that does not cry wolf.
#
# ---------------------------------------------------------------------
# The floors
# ---------------------------------------------------------------------
#
# This script's finding count can only rise for a name that is in all
# three sets, so every set going empty is a silent pass -- the exact
# shape tools/asan-build.sh had (855fdb2) and that the rest of this tree
# has since been taught to reject.  Worse, the reference set going *big*
# is also a silent pass, and that is the easy accident: a test that stops
# compiling loses its references, which makes findings go UP; a build
# flag that makes every compile fail makes the reference set empty and
# every implemented function a finding, which is loud.  The dangerous
# direction is the quiet one, so the compile step is checked file by file
# rather than in aggregate:
#
#   * every test/*.c must either compile or be named in NOT_NATIVE below,
#     with a reason.  A partial run is a failure, not a smaller number.
#   * the declared, implemented and referenced sets must each be
#     non-empty.
#
# ---------------------------------------------------------------------
# The baseline
# ---------------------------------------------------------------------
#
# tools/unreferenced-baseline.txt holds a single number: the count this
# tree is known to have.
#
# It is a one-way ratchet, and deliberately asymmetric:
#
#   count > baseline  -> FAIL, naming the whole list.
#   count < baseline  -> PASS, printing "lower the baseline to N".
#
# The asymmetry is the point, and it is a decision rather than an
# oversight.  This number moves *downward* as a side effect of work that
# has nothing to do with this check: the POSIX clause audits landing
# through today took it from the 165 the analysis measured at 082ed2c to
# 56, by writing assertions for the math.h f/l tail, getc, mkdirat,
# linkat, fchownat, mkfifo, mknod, fexecve, gets, getlogin and the whole
# *_unlocked read side.  Failing on a fall would mean every one of those
# commits was blocked by this script until its author edited a file they
# had never heard of, in a tree where several audits are in flight at
# once.  That is how a check acquires a LINT_STRICT=0 in someone's shell
# profile and stops meaning anything.  A rise is different: it is either
# a new public function nobody tested or a test that stopped referencing
# one, and both are exactly what this exists to surface.
#
# What happens when a legitimately new declaration lands unreferenced:
# this fails, and the author chooses -- write the test, or raise the
# baseline by one in the same commit with the reason in the message.
# That is a deliberate choice to make "shipping a public function with no
# test" cost one visible line, not an accident of the mechanism.  If it
# ever proves to be the wrong trade, the answer is to argue about the
# trade, not to widen the tolerance quietly.
#
# The honest limit of a count: it cannot see a swap.  Giving function A a
# test while adding function B with none is a net zero and passes.  A
# committed *name list* would catch that and was the alternative; it was
# not taken because the list is 56 names today and would churn on every
# commit that adds a public function, and because the analysis this
# implements (test/verification-measures.md, M2) explicitly ranks
# "report-only against a committed baseline count first" ahead of "a hard
# failure on new unreferenced functions", calling the latter a later and
# separate decision.  The full sorted list is written to
# obj/lint/unreferenced.txt on every run, so promoting the baseline from
# a count to that list is a one-line change when somebody wants it.
#
# Usage:
#   tools/lint-unreferenced.sh
#
# Environment:
#   CC_NATIVE=...      native compiler for the test objects (default:
#                      clang, then cc)
#   LINT_JOBS=N        parallel compiles (default: nproc)
#   LINT_STRICT=0      always exit 0 (report only).  Does not relax the
#                      floors: a run that compiled nothing is a broken
#                      run, not a report of zero findings.
#
# Exit status is 1 if the finding count exceeds the baseline, or if any
# floor was not met.

set -u

# shellcheck disable=SC1007
srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$srcdir" || exit 1

: "${LINT_STRICT:=1}"
: "${LINT_JOBS:=}"
if [ -z "$LINT_JOBS" ]; then
	LINT_JOBS=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
fi

# The native compile is an ELF/LP64 compile of test sources written
# against this tree's own headers -- the same thing tools/asan-build.sh
# does, minus the sanitizers and minus the link.  Only -c is needed, so
# no ntstubs.o and no library.
: "${CC_NATIVE:=}"
if [ -z "$CC_NATIVE" ]; then
	if command -v clang >/dev/null 2>&1; then CC_NATIVE=clang
	elif command -v cc >/dev/null 2>&1; then CC_NATIVE=cc
	fi
fi
if [ -z "$CC_NATIVE" ]; then
	printf 'lint-unreferenced: MISSING -- no native compiler (tried clang, cc).\n' >&2
	printf 'lint-unreferenced: this stage cannot run, and a stage that cannot run\n' >&2
	printf 'lint-unreferenced: must not report success.\n' >&2
	exit 1
fi

# ARCH for the header set.  This is a native build, so the only sensible
# answer is the one whose bits/ headers describe this machine's word
# size; x86_64 is the tree's LLP64 arch and the one asan-build uses.
ARCH=${NTLIBC_ARCH:-x86_64}

# Tests that genuinely cannot be compiled natively, with the reason, in
# the style of tools/asan-build.sh's not_native().  Anything here is
# scanned textually instead (see the header comment); anything NOT here
# must compile, or this run fails.
NOT_NATIVE="rpath delayall"
not_native_why() {
	case $1 in
	rpath)    echo "exercises the delay-load/\$ORIGIN machinery in src/internal/{rpath,delayload}.c, which is PE-only (LdrLoadDll/LdrGetProcedureAddress against a real NT image)" ;;
	delayall) echo "proof of the -Wl,--delay-all path (crt/delayload2.c, PE-only, same reason)" ;;
	*)        echo "" ;;
	esac
}

workdir=$(mktemp -d) || exit 1
trap 'rm -rf "$workdir"' EXIT INT TERM

# ---- declared: every function a public header prototypes ------------------
headers=$(find include -type f -name '*.h' | sort)
nheaders=$(printf '%s\n' "$headers" | grep -c . || true)
# $headers is a whitespace-separated list and is meant to word-split.
# shellcheck disable=SC2086
awk -v MODE=decl -f "$srcdir/tools/lint-decls.awk" $headers \
	| cut -f1 | sort -u > "$workdir/declared"
ndecl=$(grep -c . "$workdir/declared" || true)

# ---- macro-shadowed: declared as a function AND #defined as a macro -------
# See consequence 4 in the header.  A call to one of these never becomes a
# relocation, so it can never be "referenced" by this script's definition
# and would be a permanent finding.  Derived, not listed.
# shellcheck disable=SC2086
grep -rhE '^[[:space:]]*#[[:space:]]*define[[:space:]]+[A-Za-z_][A-Za-z0-9_]*' $headers \
	| sed 's/.*define[[:space:]]*\([A-Za-z_][A-Za-z0-9_]*\).*/\1/' \
	| sort -u > "$workdir/macros"
comm -12 "$workdir/declared" "$workdir/macros" > "$workdir/shadowed"
nshadow=$(grep -c . "$workdir/shadowed" || true)
comm -23 "$workdir/declared" "$workdir/shadowed" > "$workdir/declared.eff"
mv -f "$workdir/declared.eff" "$workdir/declared"
ndecl=$(grep -c . "$workdir/declared" || true)

# ---- implemented: every function src/, arch/, crt/, sh/ define ------------
#
# Deliberately NOT including tools/ntdll.def, unlike lint-undefined.sh: a
# name the linker imports from ntdll.dll is not something this library
# implements, so "no test references it" is not a gap in this tree's own
# coverage.  sh/ is left out for the mirror reason -- it is a program
# built on top of libc.a, not part of it, and nothing it defines is
# declared in include/ (checked: the two sets do not intersect today).
cfiles=$(find src arch crt -type f -name '*.c' 2>/dev/null)
# shellcheck disable=SC2086
awk -v MODE=def -f "$srcdir/tools/lint-decls.awk" $cfiles \
	| cut -f1 | sort -u > "$workdir/implemented"
sfiles=$(find src arch crt -type f -name '*.S' 2>/dev/null)
for f in $sfiles; do
	grep -E '^\.globl?' "$f" 2>/dev/null
done | sed -e 's/^\.globl\?//' -e 's/_(\([A-Za-z_][A-Za-z0-9_]*\))/\1/g' \
	| tr ',' '\n' | tr -d ' \t' | grep -v '^$' >> "$workdir/implemented"
sort -u -o "$workdir/implemented" "$workdir/implemented"
nimpl=$(grep -c . "$workdir/implemented" || true)

# ---- referenced: undefined symbols of natively compiled test objects ------
gendir=$workdir/gen/include/bits
mkdir -p "$gendir" "$workdir/obj" || exit 1
cat "arch/$ARCH/bits/alltypes.h.gen" include/alltypes.h.gen > "$gendir/alltypes.h" || exit 1

TINC="-I$workdir/gen/include -I$srcdir/include -I$srcdir/arch/$ARCH -I$srcdir/arch/generic"

# -O0, not asan-build's -O1: at -O1 a call whose result is unused can be
# eliminated before it ever becomes a relocation, which would report a
# function as unreferenced because the optimiser agreed the test did
# nothing with it.  -fno-builtin is kept for the same reason one level
# down -- it stops the compiler folding str*/mem* calls into inline code.
CFLAGS_NATIVE="-c -O0 -std=c99 -nostdinc -fno-builtin -D_XOPEN_SOURCE=700 -D_GNU_SOURCE -w"

ntests=0 ncompiled=0 nexcluded=0
: > "$workdir/tocompile"
for t in test/*.c; do
	n=$(basename "$t" .c)
	ntests=$((ntests + 1))
	case " $NOT_NATIVE " in
	*" $n "*)
		nexcluded=$((nexcluded + 1))
		printf '%s\t%s\n' "$t" "$(not_native_why "$n")" >> "$workdir/textscan"
		continue ;;
	esac
	printf '%s\n' "$t" >> "$workdir/tocompile"
done

if [ -s "$workdir/tocompile" ]; then
	# $CFLAGS_NATIVE and $TINC are flag lists and must word-split; the
	# $workdir splice into the single-quoted child script is the
	# close-quote/reopen-quote trick tools/lint.sh uses for $pardir, for
	# the same reason (this shell substitutes it, the child does not).
	# shellcheck disable=SC2086,SC2016
	xargs -P "$LINT_JOBS" -I{} sh -c '
		f=$1; cc=$2; shift 2
		n=$(basename "$f" .c)
		# shellcheck disable=SC2086
		"$cc" "$@" "$f" -o "'"$workdir"'/obj/$n.o" \
			2> "'"$workdir"'/obj/$n.err"
	' _ {} "$CC_NATIVE" $CFLAGS_NATIVE $TINC < "$workdir/tocompile"
fi

: > "$workdir/nocompile"
while read -r t; do
	n=$(basename "$t" .c)
	if [ -f "$workdir/obj/$n.o" ]; then
		ncompiled=$((ncompiled + 1))
	else
		printf '%s\n' "$t" >> "$workdir/nocompile"
	fi
done < "$workdir/tocompile"

: > "$workdir/symrefs"
if ls "$workdir"/obj/*.o >/dev/null 2>&1; then
	nm --undefined-only "$workdir"/obj/*.o 2>/dev/null \
		| awk '{ print $NF }' | sed 's/@@.*//' | grep -v '^$' >> "$workdir/symrefs"
fi
sort -u -o "$workdir/symrefs" "$workdir/symrefs"
nsym=$(grep -c . "$workdir/symrefs" || true)
cp "$workdir/symrefs" "$workdir/referenced"
# The generous textual pass, for the PE-only files only.
if [ -f "$workdir/textscan" ]; then
	while IFS="$(printf '\t')" read -r t why; do
		printf 'lint-unreferenced: %s scanned textually, not compiled: %s\n' \
			"$t" "$why" >&2
		grep -oE '[A-Za-z_][A-Za-z0-9_]*' "$t"
	done < "$workdir/textscan" >> "$workdir/referenced"
fi
sort -u -o "$workdir/referenced" "$workdir/referenced"
nref=$(grep -c . "$workdir/referenced" || true)

# ---- floors ---------------------------------------------------------------
floor_failed=0
if [ "$ndecl" -eq 0 ]; then
	printf 'lint-unreferenced: FAILED -- no function declarations found in %s header(s).\n' \
		"$nheaders" >&2
	printf 'lint-unreferenced: nothing was compared, so this run verified nothing.\n' >&2
	floor_failed=1
fi
if [ "$nimpl" -eq 0 ]; then
	printf 'lint-unreferenced: FAILED -- no definitions found in src/, arch/, crt/ or sh/.\n' >&2
	printf 'lint-unreferenced: the "is it implemented" filter would then pass nothing, so\n' >&2
	printf 'lint-unreferenced: this run reports zero findings by construction.\n' >&2
	floor_failed=1
fi
if [ "$ntests" -eq 0 ]; then
	printf 'lint-unreferenced: FAILED -- no test/*.c found at all.\n' >&2
	floor_failed=1
fi
if [ -s "$workdir/nocompile" ]; then
	printf 'lint-unreferenced: FAILED -- %d of %d test source(s) did not compile:\n' \
		"$(grep -c . "$workdir/nocompile")" "$((ntests - nexcluded))" >&2
	while read -r t; do
		n=$(basename "$t" .c)
		printf 'lint-unreferenced:   %s\n' "$t" >&2
		sed -n '1,3p' "$workdir/obj/$n.err" 2>/dev/null | sed 's/^/lint-unreferenced:     /' >&2
	done < "$workdir/nocompile"
	printf 'lint-unreferenced: a test that does not compile contributes no references, so\n' >&2
	printf 'lint-unreferenced: its subjects would be reported unreferenced.  Fix the compile,\n' >&2
	printf 'lint-unreferenced: or add the test to NOT_NATIVE in this script with a reason --\n' >&2
	printf 'lint-unreferenced: never leave it silently uncounted.\n' >&2
	floor_failed=1
fi
# Counted on the nm output alone, not on the merged set: the textual pass
# over the two PE-only files contributes ~370 identifiers no matter what
# the symbol scan does, so a floor on the merged set could not tell a
# working nm from a broken one.
if [ "$nsym" -eq 0 ]; then
	printf 'lint-unreferenced: FAILED -- the %d compiled test object(s) yielded no\n' \
		"$ncompiled" >&2
	printf 'lint-unreferenced: undefined symbols at all.  Every implemented function would\n' >&2
	printf 'lint-unreferenced: then look unreferenced; the reference scan is broken, not the\n' >&2
	printf 'lint-unreferenced: tests.\n' >&2
	floor_failed=1
fi
[ "$floor_failed" -ne 0 ] && exit 1

# ---- report ---------------------------------------------------------------
comm -12 "$workdir/declared" "$workdir/implemented" > "$workdir/declimpl"
ndeclimpl=$(grep -c . "$workdir/declimpl" || true)
comm -23 "$workdir/declimpl" "$workdir/referenced" > "$workdir/unreferenced"
findings=$(grep -c . "$workdir/unreferenced" || true)

mkdir -p obj/lint
cp "$workdir/unreferenced" obj/lint/unreferenced.txt

baselinefile=tools/unreferenced-baseline.txt
baseline=$(grep -v '^[[:space:]]*#' "$baselinefile" 2>/dev/null | grep -E '^[0-9]+$' | head -n 1)
if [ -z "$baseline" ]; then
	printf 'lint-unreferenced: FAILED -- %s holds no baseline count.\n' "$baselinefile" >&2
	printf 'lint-unreferenced: without one there is nothing to compare against, and this\n' >&2
	printf 'lint-unreferenced: check reduces to printing a number nobody reads.\n' >&2
	exit 1
fi

printf 'lint-unreferenced: %s declared, %s implemented, %s both.\n' \
	"$ndecl" "$nimpl" "$ndeclimpl"
printf 'lint-unreferenced: %s name(s) referenced: %s undefined symbol(s) over %d compiled\n' \
	"$nref" "$nsym" "$ncompiled"
printf 'lint-unreferenced:   test object(s), plus %d PE-only source(s) scanned textually.\n' \
	"$nexcluded"
printf 'lint-unreferenced: %s declaration(s) excluded as macro-shadowed: %s\n' \
	"$nshadow" "$(tr '\n' ' ' < "$workdir/shadowed")"
printf 'lint-unreferenced: %s declared-and-implemented function(s) no test references' "$findings"
printf ' (baseline %s)\n' "$baseline"
printf 'lint-unreferenced: full list -> obj/lint/unreferenced.txt\n'

if [ "$findings" -gt "$baseline" ]; then
	printf 'lint-unreferenced: FAILED -- %d exceeds the baseline of %d.\n' \
		"$findings" "$baseline" >&2
	printf 'lint-unreferenced: newly unreferenced (or newly implemented with no test):\n' >&2
	sed 's/^/lint-unreferenced:   /' obj/lint/unreferenced.txt >&2
	[ "$LINT_STRICT" = 0 ] && exit 0
	exit 1
fi
if [ "$findings" -lt "$baseline" ]; then
	printf 'lint-unreferenced: the count went DOWN (%d < %d) -- lower %s to %d\n' \
		"$findings" "$baseline" "$baselinefile" "$findings"
	printf 'lint-unreferenced: to tighten the ratchet.  Not a failure: see this script\n'
	printf 'lint-unreferenced: header for why a fall is a nudge and a rise is not.\n'
fi
exit 0
