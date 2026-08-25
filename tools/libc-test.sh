#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/libc-test.sh -- build and run musl's libc-test corpus
# (third_party/libc-test, a git submodule pinned at the SHA recorded in
# third_party/README.md) against this library, and
# adjudicate every result against test/libc-test-expected.txt.
#
# WHY THIS SCRIPT IS SHAPED THE WAY IT IS
#
# test/external-suites.md measured this corpus against the tree and found
# 22 behavioural defects on the first run. A ledger must make those known
# states reviewable without turning into an append-only exemption list.
#
#   * Every test in the corpus must have exactly one line in
#     test/libc-test-expected.txt.  A test in the corpus with no line is
#     a hard error, not a default-to-ignored.
#   * Pedantic requires BUG to compile and fail and UNIMPL not to compile.
#     A fixed defect therefore forces the ledger to be updated.
#   * The ledger carries pinned counts in its header, and this script
#     checks them. A disposition population cannot change silently.
#   * The ledger uses the same PASS/BUG/UNIMPL/NA/FLAKY dispositions as
#     the in-tree suite. Raw compiler and process outcomes remain separate.
#
# OBSERVATIONS AND DISPOSITIONS
#
# Same contract as tools/run-tests.py (see its header): 0 pass, 77 "ran
# but declined to check something", anything else fail.  Mapped here:
#
#   does not compile/link  -> UNBUILDABLE observation. UNIMPL expects it.
#                             52 of the 146
#                             tests die at #include; that is the gap
#                             accounting restated as a build error, and
#                             it is evidence of nothing about behaviour.
#   output contains the    -> NA for this runtime. The shim
#   shim's stub marker        stubs four helpers it cannot implement here
#                             (no mmap, no enforced rlimits, no
#                             langinfo); a test whose premise was stubbed
#                             out did not check its premise.
#   output contains Wine's -> NA for this runtime. Detected at run time from
#   RtlCloneUserProcess       the output, never from a static list: these
#   string                    same tests run for real on the Windows CI
#                             leg and on a Wine build that implements it,
#                             and a baked-in exclusion would hide that.
#   otherwise              -> the exit status is the verdict.
#
# The summary prints UNIMPL even when it is the largest number,
# because "39% of this suite cannot be compiled against this library" is
# the most useful single fact this stage produces.  And a run in which
# nothing was actually adjudicated exits non-zero (the VERIFIED_FLOOR
# below) -- measure M1 of test/verification-measures.md applied here.
#
# THE MATH CORPUS
#
# Present in the submodule but never built by default, and not wired into
# tools/gate.sh; `tools/libc-test.sh math` says so and exits non-zero
# unless explicitly pointed at a checkout.  See the `math` case below for
# the two reasons (licence, and 82 untriaged failures).
#
# Usage:
#   tools/libc-test.sh              build + run + adjudicate (the gate)
#   tools/libc-test.sh math         the on-demand math corpus
#   tools/libc-test.sh --selftest   prove the guards below still fire
#
# Env:
#   WINE            wine binary (default: config.mak's WINE)
#   LIBC_TEST_JOBS  parallel workers (default: nproc)
#   LIBC_TEST_MATH  path to a full libc-test checkout, for `math`
#   NTLIBC_TEST_PROFILE  whitespace-separated additional KEY=VALUE selectors

set -u

# shellcheck disable=SC1007
srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$srcdir" || exit 1

SUITE="$srcdir/third_party/libc-test"
LEDGER="$srcdir/test/libc-test-expected.txt"
SHIM="$srcdir/test/libc-test-shim-src/libc-test-shim.c"

# The literal the shim prints from every stubbed helper.  Load-bearing:
# --selftest asserts it still occurs in the shim, so renaming it in one
# place without the other fails loudly instead of silently promoting the
# stubbed tests to passes.
SHIM_MARK='SHIM-STUBBED: '
# The literal Wine prints when RtlCloneUserProcess is missing.
WINE_MARK='RtlCloneUserProcess'

# The floor on this stage: how many tests must produce a real verdict
# (built, ran, was neither shim-stubbed nor Wine-blocked) for the run to
# count as having verified anything.  Deliberately well below the 86 seen
# on a Wine that implements RtlCloneUserProcess -- it is a floor, not the
# expected value, and it exists so that a run where the toolchain, the
# library or Wine has fallen over cannot report success.  It only moves
# deliberately.
VERIFIED_FLOOR=70

# Normal skips known BUG and UNIMPL cases. Pedantic probes both to catch
# stale classifications. Strict performs the same probes, then disallows
# either disposition and requires every FLAKY case to pass.
: "${NTLIBC_TEST_MODE:=normal}"
: "${NTLIBC_TEST_RUNTIME:=wine}"
: "${NTLIBC_TEST_PROFILE:=}"
case "$NTLIBC_TEST_MODE" in
	normal|pedantic|strict) ;;
	*) echo "libc-test: NTLIBC_TEST_MODE must be normal, pedantic or strict." >&2; exit 2 ;;
esac

usage() { sed -n '2,80p' "$0" | sed 's/^# \{0,1\}//'; }

# ------------------------------------------------------- the submodule

# third_party/libc-test is a git submodule (see .gitmodules and
# third_party/README.md).  A clone made without
# --recurse-submodules leaves it as an EMPTY DIRECTORY -- not a stale
# one, not a partial one -- and every loop below that walks
# "$SUITE"/src/... would then find nothing and adjudicate nothing.
#
# That must be a loud failure and never a skip, for exactly the reason
# the `math` case below exits 2 rather than shrugging: a verification
# stage that reports success having run zero tests is worse than one that
# is missing, because it looks like evidence.  This project closed nine
# such stages in a day (test/verification-measures.md); an absent
# submodule must not open a tenth.
#
# Note what this check is NOT: it is not a network fallback.  There is no
# cached copy to degrade to and no fetch attempted here.  It names the
# one command that fixes it and gets out of the way.
require_suite() {
	if [ -f "$SUITE/COPYRIGHT" ] && [ -d "$SUITE/src/functional" ] &&
	   [ -d "$SUITE/src/common" ] && [ -d "$SUITE/src/regression" ]; then
		return 0
	fi
	echo "libc-test: third_party/libc-test is empty or incomplete." >&2
	echo "libc-test: it is a git submodule -- musl's libc-test, pinned at the" >&2
	echo "libc-test: SHA recorded in third_party/README.md -- and this clone" >&2
	echo "libc-test: does not have it checked out.  Fix it with:" >&2
	echo "libc-test:" >&2
	echo "libc-test:     git submodule update --init --recursive" >&2
	echo "libc-test:" >&2
	echo "libc-test: (or clone with --recurse-submodules next time)." >&2
	echo "libc-test: This is an error, not a skip.  Without the corpus this" >&2
	echo "libc-test: stage would build nothing, run nothing and adjudicate" >&2
	echo "libc-test: nothing -- and a stage that reports success having" >&2
	echo "libc-test: checked nothing is the exact failure this project's" >&2
	echo "libc-test: verification measures exist to prevent." >&2
	exit 2
}

# ---------------------------------------------------------------- ledger

# Parsed once into three shell-safe lookup files rather than re-grepped
# per test.
read_ledger() {
	[ -f "$LEDGER" ] || { echo "libc-test: missing $LEDGER" >&2; return 1; }
	awk -v out="$1" '
	/^#[ \t]*COUNTS[ \t]/ { print "COUNTS " $0 > out "/counts"; next }
	/^[ \t]*#/ || /^[ \t]*$/ { next }
	{
		if (NF < 3) { print "MALFORMED " $0 > out "/errors"; next }
		name=$1; selector=$2; status=$3
		reason=""
		for (i=4;i<=NF;i++) reason = reason (i>4?" ":"") $i
		if (status != "PASS" && status != "BUG" && status != "UNIMPL" &&
		    status != "NA" && status != "FLAKY") {
			print "BADSTATUS " name " " status > out "/errors"; next
		}
		if (status != "PASS" && reason == "") {
			print "NOREASON " name > out "/errors"; next
		}
		key=name SUBSEP selector
		if (key in seen) { print "DUPLICATE " name " " selector > out "/errors"; next }
		seen[key]=1
		print name "\t" selector "\t" status "\t" reason > out "/base"
	}' "$LEDGER"
	[ -f "$1/errors" ] && return 0
	profile_args=""
	for term in $NTLIBC_TEST_PROFILE; do
		profile_args="$profile_args --profile $term"
	done
	# shellcheck disable=SC2086 -- profile selectors are whitespace-separated
	"$srcdir/tools/test-policy.py" resolve --suite libc-test \
		--defaults "$LEDGER" --profile "runtime=$NTLIBC_TEST_RUNTIME" \
		$profile_args > "$1/rows" || return 1
	return 0
}

ledger_status() { awk -F'\t' -v n="$1" '$1==n{print $2; f=1} END{if(!f) print "ABSENT"}' "$W/rows"; }
ledger_reason() { awk -F'\t' -v n="$1" '$1==n{print $3}' "$W/rows"; }

# ------------------------------------------------------------------ math

# --selftest checks only in-tree artefacts (the shim and the ledger) and
# is deliberately still usable without the corpus; everything else needs
# it, so demand it here, before any work.
[ "${1:-}" = "--selftest" ] || require_suite

if [ "${1:-}" = "math" ]; then
	# The submodule brings src/math with it -- it is on disk, roughly
	# 9.6 MB of it -- but it is deliberately NOT built, for two
	# independent reasons, either sufficient:
	#
	#  1. Licence.  src/math/ucb/ and src/math/crlibm/ are BSD- and
	#     GPL-licensed third-party vectors (libc-test's own COPYRIGHT
	#     says so); the rest of the corpus is uniformly MIT.  This tree
	#     distributes a 20-byte gitlink, not those vectors, and building
	#     them into anything we ship would change that.
	#  2. Readiness.  82 of its 174 linkable tests fail here and there
	#     is no ledger for any of them, so this mode reports; it does
	#     not adjudicate.  external-suites.md says it stays on demand
	#     until those are triaged.
	#
	# What it must NOT do is quietly succeed.  No opt-in, no run, no
	# zero exit -- the same rule the submodule guard above enforces.
	if [ -z "${LIBC_TEST_MATH:-}" ]; then
		echo "libc-test: the math corpus is present in the submodule but is" >&2
		echo "libc-test: not adjudicated by this tree: 82 of its 174 linkable" >&2
		echo "libc-test: tests fail here and none has been triaged, and its" >&2
		echo "libc-test: vectors under src/math/ucb and src/math/crlibm are" >&2
		echo "libc-test: BSD/GPL rather than MIT (test/external-suites.md and" >&2
		echo "libc-test: third_party/README.md).  To run it anyway, opt in" >&2
		echo "libc-test: explicitly:" >&2
		echo "libc-test:" >&2
		echo "libc-test:     LIBC_TEST_MATH=third_party/libc-test $0 math" >&2
		echo "libc-test:" >&2
		echo "libc-test: This is an error, not a skip: a stage that reports" >&2
		echo "libc-test: success having run nothing is the thing this" >&2
		echo "libc-test: project's verification measures exist to prevent." >&2
		exit 2
	fi
	if [ ! -d "$LIBC_TEST_MATH/src/math" ]; then
		echo "libc-test: LIBC_TEST_MATH=$LIBC_TEST_MATH has no src/math." >&2
		exit 2
	fi
	echo "libc-test: the math corpus has no ledger yet; every result below is"
	echo "libc-test: UNCLASSIFIED by construction.  This mode reports; it does"
	echo "libc-test: not adjudicate, and it is not wired into tools/gate.sh."
	MATH_SRC="$LIBC_TEST_MATH"
else
	MATH_SRC=""
fi

# -------------------------------------------------------------- selftest

if [ "${1:-}" = "--selftest" ]; then
	rc=0
	if ! grep -q "$SHIM_MARK" "$SHIM"; then
		echo "selftest: FAIL -- $SHIM no longer prints '$SHIM_MARK'." >&2
		echo "selftest: the stubbed-helper detection in this script is keyed" >&2
		echo "selftest: to that literal; without it two structurally" >&2
		echo "selftest: runtime-NA tests would be adjudicated as ordinary" >&2
		echo "selftest: failures or passes." >&2
		rc=1
	else
		echo "selftest: OK -- shim marker present in test/libc-test-shim-src/libc-test-shim.c"
	fi
	W=$(mktemp -d) || exit 1
	trap 'rm -rf "$W"' EXIT
	if read_ledger "$W" && [ ! -f "$W/errors" ]; then
		echo "selftest: OK -- ledger parses with no malformed rows"
	else
		echo "selftest: FAIL -- ledger has malformed rows:" >&2
		cat "$W/errors" >&2 2>/dev/null
		rc=1
	fi
	# Every non-PASS disposition must explain why it applies.
	bad="$W/bad-ledger"
	printf '# COUNTS PASS=0 BUG=0 UNIMPL=0 NA=0 FLAKY=0\nsome-test runtime=wine NA\n' > "$bad"
	mkdir -p "$W/b"
	# read_ledger reads the global LEDGER; save and restore it rather
	# than prefixing the call, because a variable assignment prefixed to
	# a FUNCTION invocation persists after it in some POSIX shells and
	# would leave the rest of this script pointed at a fake ledger.
	real_ledger="$LEDGER"
	LEDGER="$bad"
	read_ledger "$W/b" 2>/dev/null
	LEDGER="$real_ledger"
	if grep -q '^NOREASON some-test$' "$W/b/errors" 2>/dev/null; then
		echo "selftest: OK -- an unexplained non-PASS row is rejected"
	else
		echo "selftest: FAIL -- an unexplained NA row was accepted." >&2
		rc=1
	fi
	exit $rc
fi

if [ "${1:-}" != "" ] && [ "${1:-}" != "math" ]; then
	usage; exit 2
fi

# ---------------------------------------------------------------- config

[ -f "$srcdir/config.mak" ] || {
	echo "libc-test: no config.mak; run ./configure first." >&2; exit 2; }
cfg() { sed -n "s/^$1 *= *//p" "$srcdir/config.mak" | tail -1; }
CC=$(cfg CC); ARCH=$(cfg ARCH)
CFLAGS_C99FSE=$(cfg CFLAGS_C99FSE); CFLAGS_AUTO=$(cfg CFLAGS_AUTO)
: "${WINE:=$(cfg WINE)}"
: "${LIBC_TEST_JOBS:=$(nproc 2>/dev/null || echo 1)}"

[ -n "$CC" ] || { echo "libc-test: config.mak has no CC." >&2; exit 2; }
[ -f "$srcdir/lib/libc.a" ] || {
	echo "libc-test: lib/libc.a is missing; run make first." >&2; exit 2; }
if [ -z "$WINE" ]; then
	# Same reasoning as tools/run-tests.py: "no wine at all" is the whole
	# stage going empty, not a per-test exception, so it fails.
	echo "libc-test: config.mak has no WINE and none was given." >&2
	echo "libc-test: nothing would run; that is a failure, not a skip." >&2
	exit 1
fi

# The shim-marker pin, enforced on every run and not only under
# --selftest: the two 77s that come from a stubbed helper are detected by
# grepping the test's output for this literal, so if the shim stops
# printing it those tests silently become ordinary failures or -- worse
# -- ordinary passes.  Cheap enough to do unconditionally.
if ! grep -q "$SHIM_MARK" "$SHIM"; then
	echo "libc-test: FAILED -- test/libc-test-shim-src/libc-test-shim.c no longer prints" >&2
	echo "libc-test: '$SHIM_MARK', which is what this script keys the" >&2
	echo "libc-test: stubbed-helper 77s to.  See --selftest." >&2
	exit 1
fi

INC="-I$srcdir/arch/$ARCH -I$srcdir/arch/generic -Iobj/include -I$srcdir/include -I$SUITE/src/common"

W=$(mktemp -d "${TMPDIR:-/tmp}/ntlibc-libctest.XXXXXX") || exit 1
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/out" obj/libc-test

read_ledger "$W" || exit 2
if [ -f "$W/errors" ]; then
	echo "libc-test: test/libc-test-expected.txt is malformed:" >&2
	sed 's/^/    /' "$W/errors" >&2
	exit 2
fi

# ----------------------------------------------------------------- build

# The four helpers we keep from upstream's src/common.  vmfill.c,
# setrlim.c, fdfill.c and utf8.c are deliberately absent: test/
# libc-test-shim.c replaces them (see its header for each one's reason).
HELPERS="print rand path memfill"
hobjs=""
for h in $HELPERS; do
	# shellcheck disable=SC2086  # $CC/$CFLAGS_*/$INC are word lists from
	# config.mak, exactly as the Makefile's own recipes use them.
	if ! $CC -c $CFLAGS_C99FSE $CFLAGS_AUTO -D_GNU_SOURCE $INC \
	     -o "obj/libc-test/$h.o" "$SUITE/src/common/$h.c" 2>"$W/$h.err"; then
		echo "libc-test: the shared harness helper $h.c does not compile." >&2
		echo "libc-test: this is not a per-test gap -- nothing can run." >&2
		sed 's/^/    /' "$W/$h.err" >&2
		exit 1
	fi
	hobjs="$hobjs obj/libc-test/$h.o"
done
# shellcheck disable=SC2086
if ! $CC -c $CFLAGS_C99FSE $CFLAGS_AUTO -D_GNU_SOURCE $INC \
     -o obj/libc-test/shim.o "$SHIM" 2>"$W/shim.err"; then
	echo "libc-test: test/libc-test-shim-src/libc-test-shim.c does not compile." >&2
	sed 's/^/    /' "$W/shim.err" >&2
	exit 1
fi
hobjs="$hobjs obj/libc-test/shim.o"

corpus=""
for f in "$SUITE"/src/functional/*.c "$SUITE"/src/regression/*.c; do
	[ -f "$f" ] || continue
	corpus="$corpus $f"
done
[ -n "$corpus" ] && [ -n "$MATH_SRC" ] && corpus="$corpus $(echo "$MATH_SRC"/src/math/*.c)"
if [ -z "$corpus" ]; then
	echo "libc-test: the corpus in $SUITE has no test sources." >&2
	echo "libc-test: see 'git submodule update --init --recursive'." >&2
	exit 1
fi

build_one() {
	f=$1; n=$(basename "$f" .c)
	want=$(ledger_status "$n")
	if [ "$want" = NA ] ||
	   { [ "$NTLIBC_TEST_MODE" = normal ] &&
	     { [ "$want" = BUG ] || [ "$want" = UNIMPL ]; }; }; then
		echo NA > "$W/out/$n.state"
		return
	fi
	# shellcheck disable=SC2086
	if $CC $CFLAGS_C99FSE $CFLAGS_AUTO -D_GNU_SOURCE $INC -nostdlib \
	    -o "obj/libc-test/$n.exe" "$srcdir/lib/crt1.o" "$f" $hobjs \
	    -L"$srcdir/lib" -lc -lntdll > "$W/out/$n.build" 2>&1; then
		echo built > "$W/out/$n.state"
	else
		echo UNBUILDABLE > "$W/out/$n.state"
		rm -f "obj/libc-test/$n.exe"
	fi
}

echo "libc-test: building $(echo "$corpus" | wc -w) tests with $CC ..."
build_start=$(date +%s)
# shellcheck disable=SC2086
for f in $corpus; do build_one "$f"; done
build_end=$(date +%s)

# ------------------------------------------------------------------- run

runner="$W/runone.sh"
cat > "$runner" <<'EOF'
#!/bin/sh
n=$(basename "$1" .exe)
d=$(mktemp -d "$W/work.XXXXXX") || { echo 1 > "$W/out/$n.rc"; exit 0; }
# 120s, not the 25s this started with: the gate runs every stage
# at once and Wine process startup under that contention was measured
# at 40x its idle cost, which timed 14 trivial tests out and reported
# them as behavioural failures.  The genuine hang risk here is
# `winedbg --auto`, and WINEDLLOVERRIDES above is what removes it --
# a fork that cannot clone aborts in ~0.3s.  So this bound exists to
# stop a wedged test wedging the stage, not to police runtime, and it
# should be far above anything a working test needs.
( cd "$d" && WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d \
    timeout -k 5 120 "$WINE" "$1" ) > "$W/out/$n.log" 2>&1 </dev/null
echo $? > "$W/out/$n.rc"
rm -rf "$d"
EOF
chmod +x "$runner"
export W WINE

exes=""
for f in $corpus; do
	n=$(basename "$f" .c)
	[ -x "obj/libc-test/$n.exe" ] && exes="$exes $srcdir/obj/libc-test/$n.exe"
done
run_start=$(date +%s)
if [ -n "$exes" ]; then
	# shellcheck disable=SC2086
	printf '%s\n' $exes | xargs -P "$LIBC_TEST_JOBS" -I{} sh "$runner" {}
fi
run_end=$(date +%s)

# ----------------------------------------------------------- adjudicate

passed=0; bugs=0; flaky=0; na=0; unimpl=0
regressions=""; unexpected=""; stale=""; absent=""; timeouts=""
verified=0

for f in $corpus; do
	n=$(basename "$f" .c)
	want=$(ledger_status "$n")
	state=$(cat "$W/out/$n.state" 2>/dev/null || echo UNBUILDABLE)

	if [ "$want" = ABSENT ]; then
		absent="$absent $n"
		continue
	fi

	if [ "$state" = NA ]; then
		case "$want" in
		BUG) bugs=$((bugs + 1)); echo "BUG $n (not probed in normal mode) -- $(ledger_reason "$n")" ;;
		UNIMPL) unimpl=$((unimpl + 1)); echo "UNIMPL $n (not probed in normal mode) -- $(ledger_reason "$n")" ;;
		NA) na=$((na + 1)); echo "NA $n -- $(ledger_reason "$n")" ;;
		*) stale="$stale $n(ledger:$want,actual:NA)" ;;
		esac
		continue
	fi

	if [ "$state" = UNBUILDABLE ]; then
		why=$(grep -m1 -i 'error\|include' "$W/out/$n.build" 2>/dev/null | sed 's/^ *//')
		if [ "$want" = UNIMPL ]; then
			unimpl=$((unimpl + 1))
			echo "UNIMPL $n -- ${why:-no diagnostic}"
		else
			echo "UNBUILDABLE $n -- ${why:-no diagnostic}"
			stale="$stale $n(ledger:$want,actual:UNBUILDABLE)"
		fi
		continue
	fi
	if [ "$want" = UNIMPL ]; then
		stale="$stale $n(ledger:UNIMPL,actual:built)"
	fi

	rc=$(cat "$W/out/$n.rc" 2>/dev/null || echo 1)
	log="$W/out/$n.log"

	# Environment-derived 77s, both read out of the test's OWN output at
	# run time.  Never a static list: on the real-Windows CI leg and on a
	# Wine that implements RtlCloneUserProcess these same tests produce a
	# real verdict, and that must be visible without editing anything.
	if grep -qF "$WINE_MARK" "$log" 2>/dev/null; then
		na=$((na + 1))
		echo "NA $n (rc=77) -- fork unavailable under this wine ($WINE_MARK)"
		continue
	fi
	if grep -qF "$SHIM_MARK" "$log" 2>/dev/null; then
		na=$((na + 1))
		echo "NA $n (rc=77) -- $(grep -m1 -F "$SHIM_MARK" "$log" | sed "s/.*$SHIM_MARK//")"
		continue
	fi

	# A test killed by the timeout above did not produce a verdict: it
	# did not finish.  Counting rc=124 as "it failed" would let a
	# wedged or merely slow test satisfy a BUG row -- observed
	# exactly once, when a 25s bound under gate contention turned 14
	# trivial tests into "regressions" and quietly let `fcntl`'s
	# unresolved row be satisfied by a timeout rather than by the
	# behaviour it describes.  Its own bucket, and it fails the stage.
	if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
		timeouts="$timeouts $n"
		echo "TIMEOUT $n (rc=$rc) -- killed after 120s; this is not a verdict"
		continue
	fi

	verified=$((verified + 1))
	case "$want" in
	PASS)
		if [ "$rc" = 0 ]; then
			passed=$((passed + 1))
		else
			regressions="$regressions $n"
			echo "FAIL $n (rc=$rc) -- ledger says this passes"
			sed 's/^/    /' "$log" | head -20
		fi ;;
	BUG)
		if [ "$rc" = 0 ]; then
			unexpected="$unexpected $n"
		else
			bugs=$((bugs + 1))
			echo "BUG $n (failed as declared) -- $(ledger_reason "$n")"
		fi ;;
	FLAKY)
		flaky=$((flaky + 1))
		if [ "$NTLIBC_TEST_MODE" = strict ] && [ "$rc" != 0 ]; then
			regressions="$regressions $n(FLAKY-strict)"
		fi
		echo "FLAKY $n (rc=$rc) -- $(ledger_reason "$n")" ;;
	esac
done

echo
echo "=== libc-test summary (build $((build_end - build_start))s, run $((run_end - run_start))s) ==="
echo "$passed PASS, $bugs BUG, $unimpl UNIMPL, $na NA, $flaky FLAKY"
echo
if [ "$NTLIBC_TEST_MODE" = normal ]; then
	echo "$unimpl of $(echo "$corpus" | wc -w) tests are classified UNIMPL and were not"
	echo "compiled in normal mode. Pedantic verifies that each still fails to build."
else
	echo "UNIMPL is printed above even though it is one of the largest"
	echo "numbers here: $unimpl of $(echo "$corpus" | wc -w) tests cannot be compiled against this"
	echo "library at all, almost all of them at #include.  That is the gap"
	echo "accounting restated as a build error, and it is the single most"
	echo "useful fact this stage produces.  It is not a pass."
fi

rc=0

if [ -n "$absent" ]; then
	echo
	echo "libc-test: FAILED -- test(s) in the corpus with no line in"
	echo "libc-test: test/libc-test-expected.txt:$absent"
	echo "libc-test: every test must be accounted for by name.  A test with no"
	echo "libc-test: ledger line is one nobody decided about, and defaulting it"
	echo "libc-test: to ignored is how a suite stops meaning anything."
	rc=1
fi
if [ -n "$timeouts" ]; then
	echo
	echo "libc-test: FAILED -- test(s) killed by the timeout:$timeouts"
	echo "libc-test: a test that did not finish has not been adjudicated."
	echo "libc-test: neither a pass nor a known failure -- find out why it"
	echo "libc-test: hung, or why this machine is that slow, before touching"
	echo "libc-test: test/libc-test-expected.txt."
	rc=1
fi
if [ -n "$regressions" ]; then
	echo
	echo "libc-test: FAILED -- regression(s):$regressions"
	rc=1
fi
if [ -n "$unexpected" ]; then
	echo
	echo "libc-test: FAILED -- BUG test(s) now PASS:$unexpected"
	echo "libc-test: that is a fixed defect.  Update test/libc-test-expected.txt"
	echo "libc-test: (disposition PASS, and the COUNTS header) in the same commit as"
	echo "libc-test: the fix, so the ledger cannot outlive what it describes."
	rc=1
fi
if [ -n "$stale" ]; then
	echo
	echo "libc-test: FAILED -- ledger disagrees with what actually built:$stale"
	rc=1
fi
# ---- the counts pin ------------------------------------------------------
#
# The whole anti-drift mechanism, in six lines.  The ledger's header
# states, as numbers, how many tests are in each bucket; this compares
# them against the default rows. Profile overrides live separately in
# test/test-profiles.tsv, so the base counts remain stable across runners.
#
counts=$(sed -n 's/^#[ \t]*COUNTS[ \t]*//p' "$LEDGER" | tail -1)
if [ -z "$counts" ]; then
	echo
	echo "libc-test: FAILED -- test/libc-test-expected.txt has no '# COUNTS' header." >&2
	rc=1
else
	for kv in $counts; do
		k=${kv%%=*}; v=${kv#*=}
		a=$(awk -F'\t' -v k="$k" '$3==k {c++} END{print c+0}' "$W/base")
		if [ "$a" -ne "$v" ]; then
			echo
			echo "libc-test: FAILED -- ledger header says $k=$v, resolved profile has $a."
			rc=1
		fi
	done
fi

if [ "$NTLIBC_TEST_MODE" = strict ]; then
	strict_bug=$(awk -F'\t' '$2=="BUG"{c++}END{print c+0}' "$W/rows")
	strict_unimpl=$(awk -F'\t' '$2=="UNIMPL"{c++}END{print c+0}' "$W/rows")
	if [ "$strict_bug" -gt 0 ] || [ "$strict_unimpl" -gt 0 ]; then
		echo "libc-test: STRICT disallows $strict_bug BUG and $strict_unimpl UNIMPL disposition(s)."
		rc=1
	fi
fi

# ---- the floor -----------------------------------------------------------
if [ "$NTLIBC_TEST_MODE" != normal ] && [ "$verified" -lt "$VERIFIED_FLOOR" ]; then
	echo
	echo "libc-test: FAILED -- only $verified test(s) produced a real verdict;"
	echo "libc-test: this stage's floor is $VERIFIED_FLOOR.  Everything else was"
	echo "libc-test: UNIMPL or NA, so this run checked almost"
	echo "libc-test: nothing about the library's behaviour and must not be"
	echo "libc-test: reported as success.  (tools/libc-test.sh: VERIFIED_FLOOR)"
	rc=1
fi
if [ "$verified" -eq 0 ]; then
	echo "libc-test: NOT ONE TEST WAS ADJUDICATED.  This run verified nothing."
	rc=1
fi

if [ "$rc" -ne 0 ]; then
	echo
	echo "libc-test FAILED."
fi
exit $rc
