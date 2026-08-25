#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/posix-gapmap.sh -- measure how much of the Open POSIX Test Suite
# this library can even be compiled against, and write the answer to
# an ignored report at test/POSIX-GAP-MAP.generated.md.
#
# WHAT THIS IS AND IS NOT
#
# test/external-suites.md evaluated OPTS against this tree and reached a
# verdict that is easy to misread: as a CORRECTNESS oracle it is close to
# worthless here -- it caught zero of the five defects our own audits
# fenced -- and as a GAP oracle it is the best instrument available,
# because its subject matter is precisely the half of POSIX this library
# does not have.  Those are different axes and OPTS is strong on the
# second BECAUSE it is weak on the first.
#
# So the 1019 tests that do not compile are this script's PRODUCT, not
# its failure.  Nothing below treats them as an error.
#
# WHY THIS IS A REPORT AND NOT A PASS/FAIL SUITE
#
# A gate stage answers yes/no.  This instrument's output is a
# distribution, so making the distribution itself a gate would force a
# threshold nobody can justify -- and a threshold nobody can justify is
# the "number nobody reads" failure mode by another route.  The report is
# therefore written as a CI/developer artefact rather than reduced to a
# pass count. The hard gate is that the measurement still discriminates.
#
# THE FAILURE MODE THIS SCRIPT IS BUILT AROUND
#
# It is not "the number is wrong".  It is that **if the measurement
# silently stops working, the gap appears to close**.  A driver that
# mis-resolves the suite path, or an -I that stops pointing at it,
# produces "0 blocked tests" -- indistinguishable from success and
# strictly more dangerous, because it is good news.  This project spent a
# day removing nine stages that reported success having checked nothing
# (test/verification-measures.md); this must not open a tenth.
#
# Four invariants, all cheap, all hard errors rather than warnings:
#
#   1. CENSUS.  The number of .c under conformance/interfaces/ must equal
#      CENSUS_TESTS and the number of directories must equal CENSUS_DIRS.
#      If the submodule moves or the glob breaks, this fires before
#      anything else reports a number.  Bumping either constant is a
#      deliberate commit, reviewed alongside the LTP pin -- the same
#      discipline as verification-measures.md's M6, "pin the check LIST,
#      not just the tool version".
#
#   2. PARTITION.  A + B + C must equal the census EXACTLY.  A test that
#      fell out of classification is a bug in the classifier, not an
#      absence in the library, and silently dropping it SHRINKS the gap.
#
#   3. FLOORS, IN BOTH DIRECTIONS.  C (links) >= FLOOR_LINKS and A+B
#      (blocked) >= FLOOR_BLOCKED.  The first catches "the compiler
#      stopped finding lib/libc.a" -- everything blocked, which looks
#      like a catastrophe and so announces itself.  The second catches
#      the dangerous one: "the compiler stopped finding the SUITE", or an
#      -I pointing at a host libc, which makes everything link and reads
#      as a closed gap.  That is M1 (a floor on every stage that reports
#      a result) applied in the direction that is easy to forget.  The
#      LOWER floor moves down only in a commit that also shows the header
#      or symbol that closed the gap.
#
#   4. CANARIES, one in each direction.  CANARY_A must still classify A
#      and CANARY_C must still classify C.  This is the check that tells
#      "the gap closed" from "the measurement broke": closing a gap moves
#      the population and leaves the canaries alone, whereas a broken
#      measurement moves the canaries too.  Two canaries rather than one
#      because a classifier that has started answering constantly is
#      still right about half the population.
#
# The report records the ntlibc SHA it was generated from so an uploaded
# artefact remains attributable to a checkout.
#
# Usage:
#   tools/posix-gapmap.sh              regenerate test/POSIX-GAP-MAP.generated.md
#   tools/posix-gapmap.sh --check      compare an existing local report
#   tools/posix-gapmap.sh --render     re-render the report from the data
#                                      block it already carries: no suite,
#                                      no build, no config.mak, no compile
#   tools/posix-gapmap.sh --render F   the same, in place, on F instead of
#                                      on the checked-in report
#   tools/posix-gapmap.sh --selftest   prove the invariants above still fire
#
# Env:
#   GAPMAP_JOBS     parallel compiles (default: nproc)
#   GAPMAP_GITDIR   repository to resolve the provenance stamp against.
#                   Defaults to the source tree.  tools/gate.sh sets it,
#                   because its stages run in an rsync'd copy with no
#                   .git of its own.

set -u

# shellcheck disable=SC1007
srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$srcdir" || exit 1

SUITE="$srcdir/third_party/ltp/testcases/open_posix_testsuite"
IFACES="$SUITE/conformance/interfaces"
REPORT="$srcdir/test/POSIX-GAP-MAP.generated.md"

# ------------------------------------------------------------ the engine
#
# This file is a BACKEND.  Everything that decides what a number means --
# LC_ALL, the refuse-to-measure guard, the compiler wrapper that enforces
# it, the data block, the provenance check, the greedy closure -- lives in
# tools/suitemap-engine.sh.
# What is left here is the Open POSIX Test Suite itself: how to find its
# tests, how to classify one, and what its report says.
#
# The split keeps suite-specific classification separate from generic
# report, cache and compiler-guard mechanics.
SM_TOOL=posix-gapmap
SM_ROW_TAGS='[stdn]'
# shellcheck source=tools/suitemap-engine.sh
. "$srcdir/tools/suitemap-engine.sh"

# ------------------------------------------------------- pinned constants
#
# Every one of these is a MEASURED value with deliberate headroom, not a
# guess, and every one of them only moves in a commit that says why.

# Invariant 1.  Measured at LTP 4c0cfb8: 1610 .c files in 190
# directories under conformance/interfaces/ (189 interface directories
# plus testfrmw, which is suite infrastructure and carries 2 .c of its
# own -- so the per-directory ledger below has 189 rows over 1608 tests).
CENSUS_TESTS=1610
CENSUS_DIRS=190
CENSUS_IFACE_DIRS=189

# Invariant 3.  Measured at ntlibc eb5a607: C=591, A+B=1019.  Both floors
# sit ~12% below the measurement -- far enough not to trip on ordinary
# movement, close enough that a collapse in either direction is caught.
FLOOR_LINKS=520
FLOOR_BLOCKED=900

# Invariant 4.  One test in each direction, named individually so a
# failure says which way the measurement broke.
#   CANARY_A  needs <pthread.h>, which this library does not have and is
#             not about to grow; it must stay class A.
#   CANARY_C  is strlen, about as safely present as anything here; it
#             must stay class C.
CANARY_A=pthread_create/1-1.c
CANARY_C=strlen/1-1.c

usage() { sed -n '2,110p' "$0" | sed 's/^# \{0,1\}//'; }

# ------------------------------------------------------------ the submodule
#
# third_party/ltp is a git submodule (see .gitmodules and
# third_party/README.md).  A clone made without --recurse-submodules
# leaves it an EMPTY DIRECTORY, and every loop below would then find
# nothing, classify nothing, and report a gap of zero -- which is the
# exact shape of the good-news-shaped failure this script exists to
# prevent.  So: a loud error naming the one command that fixes it, never
# a skip, never a pass, and deliberately no cached copy or fallback
# tarball to degrade to.  Same contract as tools/libc-test.sh's
# require_suite().
require_suite() {
	if [ -d "$IFACES" ] && [ -f "$SUITE/lib/common.c" ] &&
	   [ -f "$SUITE/include/posixtest.h" ]; then
		return 0
	fi
	echo "posix-gapmap: third_party/ltp is empty or incomplete." >&2
	echo "posix-gapmap: it is a git submodule -- the Linux Test Project," >&2
	echo "posix-gapmap: whose testcases/open_posix_testsuite/ is the Open" >&2
	echo "posix-gapmap: POSIX Test Suite this report measures -- pinned at" >&2
	echo "posix-gapmap: the SHA recorded in third_party/README.md, and this" >&2
	echo "posix-gapmap: clone does not have it checked out.  Fix it with:" >&2
	echo "posix-gapmap:" >&2
	echo "posix-gapmap:     git submodule update --init --recursive" >&2
	echo "posix-gapmap:" >&2
	echo "posix-gapmap: (or clone with --recurse-submodules next time)." >&2
	echo "posix-gapmap: This is an error, not a skip.  Without the suite" >&2
	echo "posix-gapmap: this script would compile nothing, classify nothing," >&2
	echo "posix-gapmap: and report a gap of zero -- and a zero that means" >&2
	echo "posix-gapmap: 'the measurement broke' is indistinguishable from a" >&2
	echo "posix-gapmap: zero that means 'the gap closed'." >&2
	exit 2
}

mode=${1:---generate}
case "$mode" in
--generate|--check|--selftest) ;;
-h|--help) usage; exit 0 ;;
*) usage >&2; exit 2 ;;
esac

# --selftest exercises the invariant checks themselves against synthetic
# inputs; it needs no suite and no build.  --render re-derives the report
# from the data block already embedded in it (see "the data block" below):
# it compiles nothing, so it needs neither the suite nor a build either.
if [ "$mode" != --selftest ]; then
	require_suite
fi

# ------------------------------------------------------------------ config
#
# --render works entirely from the report's own data block, so it needs
# no compiler, no ARCH and no config.mak.  That is the point of the
# split: the cheap half has to run in a clone that has never been
# configured and whose submodules were never checked out, because that is
# what a pre-commit hook has to cope with.
#
# The guard, the compiler identity and the CFLAGS all come from
# sm_require_built, which is also what unlocks sm_cc.  Nothing here may
# invoke $CC directly -- see the engine's "the guard".
if [ "$mode" != --selftest ]; then
	sm_require_built
	sm_require_nm
fi
CC=${CC:-$(sm_cfg CC)}; ARCH=${ARCH:-$(sm_cfg ARCH)}
: "${GAPMAP_JOBS:=$(nproc 2>/dev/null || echo 1)}"
: "${GAPMAP_GITDIR:=$srcdir}"

sm_workdir


# ---------------------------------------------------- the include resolver
#
# Written out rather than inlined because the closure section needs it
# once per blocked test.  Given a start file and a search path, it prints
# the headers that resolve NOWHERE -- which for a blocked test is the set
# of things that would have to exist for it to compile at all.
#
# Suite-owned headers that DO resolve are followed one level deeper, so a
# test blocked only by what posixtest.h drags in is attributed to the
# header rather than to the harness.  First-error-only counting (what the
# compiler gives you for free) is biased by include ORDER and would put
# whichever header happens to be listed first at the top of the lever
# table; this is the whole reason the table is computed here rather than
# read off the build logs.
cat > "$W/absent.awk" <<'AWK_EOF'
function canread(p,   junk, r) { r = (getline junk < p); close(p); return (r >= 0) }
function resolve(h, base,   i, n, d, p) {
	p = base "/" h; if (canread(p)) return p
	n = split(SEARCH, d, ":")
	for (i = 1; i <= n; i++) { p = d[i] "/" h; if (canread(p)) return p }
	return ""
}
BEGIN {
	nq = 0; wf[++nq] = FILE; wd[nq] = DEPTH; head = 0
	while (++head <= nq) {
		f = wf[head]; depth = wd[head]
		if (f in scanned) continue
		scanned[f] = 1
		base = f; sub(/\/[^\/]*$/, "", base)
		while ((getline line < f) > 0) {
			if (line !~ /^[ \t]*#[ \t]*include[ \t]*[<"]/) continue
			h = line
			sub(/^[ \t]*#[ \t]*include[ \t]*[<"]/, "", h)
			sub(/[>"].*$/, "", h)
			if (h in seen) continue
			seen[h] = 1
			r = resolve(h, base)
			if (r == "") { absent[h] = 1; continue }
			if (depth > 0 && index(r, SUITE) == 1) { wf[++nq] = r; wd[nq] = depth - 1 }
		}
		close(f)
	}
	for (h in absent) print h
}
AWK_EOF


# ------------------------------------------------------ the four invariants
#
# Each is a function taking its inputs as arguments and returning 0/1,
# for one reason: --selftest can then call the REAL comparison with
# synthetic numbers and assert it rejects them.  An invariant expressed
# inline in the main flow can only ever be tested by breaking the tree.

# Invariant 1: census.
check_census() {
	_t=$1; _d=$2; _rc=0
	if [ "$_t" -ne "$CENSUS_TESTS" ]; then
		echo "posix-gapmap: CENSUS FAILED -- found $_t test sources under" >&2
		echo "posix-gapmap:   conformance/interfaces/, pinned at $CENSUS_TESTS." >&2
		echo "posix-gapmap: Either the LTP pin moved (in which case bump" >&2
		echo "posix-gapmap:   CENSUS_TESTS in the same commit that moves it, so a" >&2
		echo "posix-gapmap:   reviewer sees both) or the discovery glob has" >&2
		echo "posix-gapmap:   stopped finding the suite -- which would report a" >&2
		echo "posix-gapmap:   smaller gap than exists." >&2
		_rc=1
	fi
	if [ "$_d" -ne "$CENSUS_DIRS" ]; then
		echo "posix-gapmap: CENSUS FAILED -- found $_d directories under" >&2
		echo "posix-gapmap:   conformance/interfaces/, pinned at $CENSUS_DIRS" >&2
		echo "posix-gapmap:   ($CENSUS_IFACE_DIRS interfaces + testfrmw)." >&2
		_rc=1
	fi
	return $_rc
}

# Invariant 2: partition.
check_partition() {
	_a=$1; _b=$2; _c=$3; _n=$4
	if [ $((_a + _b + _c)) -eq "$_n" ]; then return 0; fi
	echo "posix-gapmap: PARTITION FAILED -- A=$_a + B=$_b + C=$_c is" >&2
	echo "posix-gapmap:   $((_a + _b + _c)), not the census $_n." >&2
	echo "posix-gapmap: A test that fell out of classification is a bug in" >&2
	echo "posix-gapmap:   this script, not an absence in the library -- and" >&2
	echo "posix-gapmap:   dropping it silently makes the gap look smaller." >&2
	return 1
}

# Invariant 3: floors, in both directions.
check_floors() {
	_c=$1; _blocked=$2; _rc=0
	if [ "$_c" -lt "$FLOOR_LINKS" ]; then
		echo "posix-gapmap: FLOOR FAILED -- only $_c test(s) link, floor is" >&2
		echo "posix-gapmap:   $FLOOR_LINKS.  Everything blocked usually means the" >&2
		echo "posix-gapmap:   compiler stopped finding lib/libc.a, not that the" >&2
		echo "posix-gapmap:   library lost 70 interfaces overnight." >&2
		_rc=1
	fi
	if [ "$_blocked" -lt "$FLOOR_BLOCKED" ]; then
		echo "posix-gapmap: FLOOR FAILED -- only $_blocked test(s) are blocked," >&2
		echo "posix-gapmap:   floor is $FLOOR_BLOCKED.  This is the dangerous" >&2
		echo "posix-gapmap:   direction: an -I that has started pointing at a host" >&2
		echo "posix-gapmap:   libc, or a suite path that resolves somewhere with" >&2
		echo "posix-gapmap:   every header present, makes everything link and the" >&2
		echo "posix-gapmap:   gap read as CLOSED.  That is good news shaped like" >&2
		echo "posix-gapmap:   a broken instrument." >&2
		echo "posix-gapmap: If the gap really did close, lower FLOOR_BLOCKED in a" >&2
		echo "posix-gapmap:   commit that also shows the header or symbol that" >&2
		echo "posix-gapmap:   closed it." >&2
		_rc=1
	fi
	return $_rc
}

# Invariant 4: the two canaries.
check_canaries() {
	_ga=$1; _gc=$2; _rc=0
	if [ "$_ga" != A ]; then
		echo "posix-gapmap: CANARY FAILED -- $CANARY_A classified $_ga, want A." >&2
		_rc=1
	fi
	if [ "$_gc" != C ]; then
		echo "posix-gapmap: CANARY FAILED -- $CANARY_C classified $_gc, want C." >&2
		_rc=1
	fi
	if [ $_rc -ne 0 ]; then
		echo "posix-gapmap: The canaries exist to tell 'the gap closed' from" >&2
		echo "posix-gapmap:   'the measurement broke'.  Closing a gap moves the" >&2
		echo "posix-gapmap:   population and leaves these two alone; a classifier" >&2
		echo "posix-gapmap:   that has started answering constantly moves them" >&2
		echo "posix-gapmap:   too.  Do not adjust a canary to make this pass --" >&2
		echo "posix-gapmap:   find out why the classifier changed its mind." >&2
	fi
	return $_rc
}

# The process invariant: the checked-in report must carry a well-formed
# provenance stamp, and must not carry the `unknown` that a merge
# resolution leaves behind.  What the report DESCRIBES is settled by
# --check's regeneration diff, not by the stamp -- see "provenance" in
# tools/suitemap-engine.sh for why that swap happened and what it cost.
#
# The gate runs its stages in an rsync'd copy with no .git, which is why
# GAPMAP_GITDIR exists.  Note what this does NOT do: if no repository is
# reachable it FAILS rather than skipping.  "I could not check" and "it
# checks out" are different claims.
# The engine owns the provenance check itself; this names the override
# variable this backend has always used, for the diagnostic.
SM_GITDIR=$GAPMAP_GITDIR
SM_GITDIR_HINT=GAPMAP_GITDIR
check_provenance() { sm_check_provenance "$@"; }

# ------------------------------------------------------------- the selftest
#
# Every invariant above is a comparison, and a comparison that is never
# exercised is a comment.  These call the REAL functions with synthetic
# numbers and assert they reject them -- so the guards are proven to fire
# without anybody having to break the tree to find out.
if [ "$mode" = --selftest ]; then
	fails=0
	# Deliberately distinct variable names: the functions under test use
	# _d/_t/_rc, and sh has no locals, so reusing those here made every
	# census line print the callee's leftovers instead of its own label.
	ck() { # ck DESCRIPTION WANT-RC CMD...
		ck_desc=$1; ck_want=$2; shift 2
		"$@" >/dev/null 2>&1; ck_got=$?
		if [ "$ck_got" -eq "$ck_want" ]; then
			echo "selftest: OK   -- $ck_desc"
		else
			echo "selftest: FAIL -- $ck_desc (rc=$ck_got, wanted $ck_want)" >&2
			fails=$((fails + 1))
		fi
	}
	ck "census rejects a short test count"    1 check_census $((CENSUS_TESTS - 1)) "$CENSUS_DIRS"
	ck "census rejects a long test count"     1 check_census $((CENSUS_TESTS + 1)) "$CENSUS_DIRS"
	ck "census rejects a wrong dir count"     1 check_census "$CENSUS_TESTS" $((CENSUS_DIRS - 1))
	ck "census accepts the pinned values"     0 check_census "$CENSUS_TESTS" "$CENSUS_DIRS"
	ck "partition rejects a dropped test"     1 check_partition 873 146 590 "$CENSUS_TESTS"
	ck "partition rejects a double count"     1 check_partition 873 146 592 "$CENSUS_TESTS"
	ck "partition accepts an exact split"     0 check_partition 873 146 591 "$CENSUS_TESTS"
	ck "links floor rejects a collapse"       1 check_floors 0 "$CENSUS_TESTS"
	ck "blocked floor rejects a closed gap"   1 check_floors "$CENSUS_TESTS" 0
	ck "floors accept the measurement"        0 check_floors 591 1019
	ck "canaries reject a stuck-A answer"     1 check_canaries A A
	ck "canaries reject a stuck-C answer"     1 check_canaries C C
	ck "canaries reject a swap"               1 check_canaries C A
	ck "canaries accept A for A and C for C"  0 check_canaries A C
	ck "provenance rejects a non-repository"  1 check_provenance HEAD /nonexistent-not-a-repo
	ck "provenance rejects an empty SHA"      1 check_provenance ""
		ck "provenance rejects an unknown stamp" 1 check_provenance unknown
	ck "provenance rejects a short SHA"       1 check_provenance 45d1616
	ck "provenance rejects a non-hex SHA"     1 check_provenance zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
	# A rebased-away SHA -- well-formed, and absent from this repository
	# because landing the work that measured it rewrote the commit.  This
	# is the case the old ancestry rule failed and this one must not: it
	# is what made both reports red on main at 45d1616 while every
	# measured row in them was byte-correct.  What establishes those rows
	# is the regeneration diff, which is unchanged and still runs.
	ck "provenance accepts a rebased-away SHA" 0 check_provenance 0000000000000000000000000000000000000000
	# The positive direction needs a real repository, which a gate tree
	# copy does not have.  Reported as such rather than silently skipped:
	# an unrun check must never read as a passed one.
	if git -C "$GAPMAP_GITDIR" rev-parse HEAD >/dev/null 2>&1; then
		ck "provenance accepts HEAD itself"   0 check_provenance "$(git -C "$GAPMAP_GITDIR" rev-parse HEAD)"
		nck=21
	else
		echo "selftest: NOTE -- $GAPMAP_GITDIR is not a git repository, so the"
		echo "selftest:         positive provenance check was not run.  Set"
		echo "selftest:         GAPMAP_GITDIR to exercise it."
		nck=20
	fi
	if [ "$fails" -ne 0 ]; then
		echo "selftest: $fails invariant check(s) did not behave as documented." >&2
		exit 1
	fi
	echo "selftest: all $nck invariant checks fire as documented."
	exit 0
fi

# ------------------------------------------------ the analysis, or not
#
# Everything between here and "the derivations" is the EXPENSIVE half: it
# compiles all 1610 conformance tests, parses their build logs, and scans
# this tree's headers and lib/libc.a.  It is also the only half that
# needs the LTP submodule, a config.mak and a build.
#
# --render skips all of it and reads the same facts straight out of the
# data block embedded in the checked-in report.  Everything below "the
# derivations" is a pure function of those facts and runs identically in
# both modes -- which is what makes the cheap half trustworthy: there is
# no second copy of the derivation logic to drift, and no invariant that
# only one path enforces.
if [ "$mode" = --render ]; then
	[ -f "$REPORT" ] || {
		echo "posix-gapmap: $REPORT does not exist, so there is nothing to" >&2
		echo "posix-gapmap:   re-render.  Generate it with: make posix-gapmap" >&2
		exit 1; }
	sm_read_data_block "$REPORT" > "$W/data"
	[ -s "$W/data" ] || {
		echo "posix-gapmap: $REPORT carries no ntlibc-generated-data block," >&2
		echo "posix-gapmap:   so there is nothing to re-render it from.  It" >&2
		echo "posix-gapmap:   predates the block, or was edited by hand." >&2
		echo "posix-gapmap:   Regenerate with: make posix-gapmap" >&2
		exit 1; }

	sc() { awk -F'\t' -v k="$2" '$1=="s" && $2==k { print $3; exit }' "$1"; }
	NTLIBC_SHA=$(sc "$W/data" ntlibc)
	LTP_SHA=$(sc "$W/data" ltp)
	CC=$(sc "$W/data" cc)
	n_tests=$(sc "$W/data" tests)
	n_dirs=$(sc "$W/data" dirs)
	# Named as `VARIABLE=key`, and the message quotes the KEY: a reader
	# told a scalar is missing has to go and find it in the block, and
	# `NTLIBC_SHA` is not a string that appears there.
	for _vk in NTLIBC_SHA=ntlibc LTP_SHA=ltp CC=cc n_tests=tests n_dirs=dirs; do
		_v=${_vk%%=*}; _k=${_vk#*=}
		eval "_x=\$$_v"
		[ -n "$_x" ] || {
			echo "posix-gapmap: the data block in $REPORT has no '$_k' scalar." >&2
			echo "posix-gapmap:   It is truncated or corrupt -- a half-written" >&2
			echo "posix-gapmap:   block must never render as a smaller gap." >&2
			echo "posix-gapmap:   Regenerate with: make posix-gapmap" >&2
			exit 1; }
	done

	# Back into exactly the intermediate files the derivations read, so
	# those derivations cannot tell which mode produced them.
	awk -F'\t' '$1=="t" { printf "%s\t%s\n", $3, $2 }' "$W/data" |
		sort -t"$(printf '\t')" -k2,2 > "$W/class.tsv"
	awk -F'\t' '$1=="t" && ($3=="A" || $3=="B") { printf "%s\t%s\n", $2, $4 }' "$W/data" |
		sort > "$W/sets.tsv"
	awk -F'\t' '$1=="t" && $5 != "" { printf "%s\t%s\t%s\n", $5, $6, $2 }' "$W/data" > "$W/bdetail.tsv"
	awk -F'\t' '$1=="d" { print $2 }' "$W/data" | sort > "$W/dirs.txt"
	{ awk -F'\t' '$1=="d" && $3=="yes" { print $2 }' "$W/data"
	  awk -F'\t' '$1=="n" && $3=="yes" { print $2 }' "$W/data"; } | sort -u > "$W/declared.txt"
	awk -F'\t' '$1=="d" && $4=="yes" { print $2 }' "$W/data" | sort -u > "$W/defined.txt"
	{ awk -F'\t' '$1=="d" && $5=="yes" { print $2 }' "$W/data"
	  awk -F'\t' '$1=="n" && $4=="yes" { print $2 }' "$W/data"; } | sort -u > "$W/undefok.txt"

	# The one thing a rendered file cannot tell you about itself: whether
	# the block it was rendered from is all there.  A truncated block
	# would render a smaller population, and a smaller population is a
	# SMALLER GAP -- good news, which is the exact shape of failure this
	# script is built around.  The census invariant below would catch it
	# too; this catches it first and says why.
	_rows=$(awk -F'\t' '$1=="t"' "$W/data" | wc -l | tr -d ' ')
	if [ "$_rows" -ne "$n_tests" ]; then
		echo "posix-gapmap: the data block records $n_tests tests but carries" >&2
		echo "posix-gapmap:   $_rows rows -- it is truncated.  Rendering it" >&2
		echo "posix-gapmap:   would report a smaller gap than was measured." >&2
		echo "posix-gapmap:   Regenerate with: make posix-gapmap" >&2
		exit 1
	fi
else
# ------------------------------------------------------------- the census
#
# Counted before anything is compiled, so that a suite the glob cannot
# find is reported as a broken measurement rather than as a library with
# no gaps.

n_tests=$(find "$IFACES" -name '*.c' | wc -l | tr -d ' ')
n_dirs=$(find "$IFACES" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')
check_census "$n_tests" "$n_dirs" || exit 1

# The LTP pin.  Two independent sources, because neither alone is always
# available and they answer subtly different questions:
#
#   the submodule's own HEAD   what is actually on disk and being measured
#   the gitlink at HEAD        what this repository says should be there
#
# tools/gate.sh's stage copies are rsync'd with --exclude=.git, which
# strips third_party/ltp's .git FILE along with the top-level directory,
# so inside a stage copy the first source is simply not there -- the
# report came out saying `unknown`, which is how this fallback exists.
# The second is read out of GAPMAP_GITDIR, the same repository the
# provenance invariant uses.
#
# When both are available and DISAGREE, that is a hard error rather than
# a preference for one of them: the checkout has been moved off the pin,
# so the report would describe a version of the suite nobody else has,
# under a SHA that says otherwise.  That is the one failure here that
# would silently produce plausible, wrong, permanent numbers.
# The interface-directory list, written out rather than re-globbed at the
# ledger below, because --render has no suite to glob and must supply the
# identical list from the data block.  testfrmw is suite infrastructure,
# not an interface, and is excluded here once instead of at every use.
find "$IFACES" -mindepth 1 -maxdepth 1 -type d |
	sed 's|.*/||' | grep -vx testfrmw | sort > "$W/dirs.txt"

ltp_head=$(git -C "$SUITE" rev-parse HEAD 2>/dev/null || true)
ltp_link=$(git -C "$GAPMAP_GITDIR" rev-parse "HEAD:third_party/ltp" 2>/dev/null || true)
if [ -n "$ltp_head" ] && [ -n "$ltp_link" ] && [ "$ltp_head" != "$ltp_link" ]; then
	echo "posix-gapmap: PIN FAILED -- third_party/ltp is checked out at" >&2
	echo "posix-gapmap:   $ltp_head" >&2
	echo "posix-gapmap: but this repository pins" >&2
	echo "posix-gapmap:   $ltp_link" >&2
	echo "posix-gapmap: The report would be measured against one suite and" >&2
	echo "posix-gapmap:   labelled with another.  Either restore the pin" >&2
	echo "posix-gapmap:   (git submodule update --init) or commit the move." >&2
	exit 1
fi
LTP_SHA=${ltp_head:-$ltp_link}
if [ -z "$LTP_SHA" ]; then
	echo "posix-gapmap: could not determine which LTP revision is checked" >&2
	echo "posix-gapmap:   out at $SUITE, from either the submodule itself or" >&2
	echo "posix-gapmap:   the gitlink in $GAPMAP_GITDIR." >&2
	echo "posix-gapmap: The report's whole claim is 'these numbers came from" >&2
	echo "posix-gapmap:   THIS suite at THIS revision'; without the revision" >&2
	echo "posix-gapmap:   it is a table of numbers from nowhere." >&2
	exit 2
fi
NTLIBC_SHA=$(git -C "$GAPMAP_GITDIR" rev-parse HEAD 2>/dev/null || echo unknown)

# ---------------------------------------------------------- the cache
#
# Everything from here to "the derivations" is the expensive half: 3.0s
# of compiling, 2.2s resolving absent headers and 2.7s of class B detail
# and header scanning, on a 24-core box at GAPMAP_JOBS=2.  All of it is a
# pure function of the inputs the cache key covers, so on a hit it is
# skipped entirely.
#
# The census, the LTP pin check and the SHA lookups above stay OUTSIDE
# the cache deliberately.  They are cheap, and two of them are guards:
# a cache that could answer for a suite whose pin had moved would be
# defeating the check that exists to stop the report describing one
# suite while labelled with another.
#
# SM_CACHE_SUITE_PATHS is the suite's SHARED material only -- the include
# tree and the harness every test compiles against.  The per-test sources
# are keyed per test, which is what makes a submodule bump recompile the
# tests that changed rather than all 1610.
INC="-I$srcdir/arch/$ARCH -I$srcdir/arch/generic -I$srcdir/obj/include -I$srcdir/include -I$SUITE/include -I$SUITE"
SM_CACHE_SUITE_PATHS="$SUITE/include $SUITE/lib"
if sm_cache_compute_key && sm_cache_analysis_load; then
	: # every intermediate below is already in $W
else
	[ "$SM_CACHE_ENABLED" = 1 ] && sm_cache_explain_miss

# ---------------------------------------------------------- classification
#
# INC is the Makefile's obj/test/%.exe include set plus the suite's own
# two include roots, exactly as test/external-suites.md's "Reproducing
# these numbers" section records the compile line.  (Set above, before the
# cache key, because the key covers it.)
SEARCH="$SUITE/include:$SUITE:$srcdir/arch/$ARCH:$srcdir/arch/generic:$srcdir/obj/include:$srcdir/include"

mkdir -p "$W/log" "$W/exe"

# The three classes, and why the boundary between A and B is where it is:
#
#   A  the #include itself fails.  The honest failure -- we do not claim
#      the interface and a portable program finds out at COMPILE time.
#   B  the #include SUCCEEDS and the test dies later: at link on an
#      implicit declaration, or on an undeclared macro, or on an
#      incomplete type.  This is the worst of the three for a downstream
#      consumer and the one our interface ledger is least able to see,
#      because at interface granularity these look PRESENT.
#   C  compiles and links.
#
# So A is exactly "tcc could not find a header", and B is everything else
# that failed.  Nothing is discarded: see check_partition.
{
printf '%s\n' '#!/bin/sh'
printf '%s\n' "$SM_WORKER_GUARD"
cat <<'ONE_EOF'
f=$1
tag=$(echo "$f" | tr '/' '_')
log="$W/log/$tag.log"
# shellcheck disable=SC2086  # word lists from config.mak, as the Makefile uses them
$CC $CFLAGS_C99FSE $CFLAGS_AUTO -D_GNU_SOURCE $INC -nostdlib \
    -o "$W/exe/$tag.exe" "$SRCDIR/lib/crt1.o" \
    "$IFACES/$f" "$SUITE/lib/common.c" \
    -L"$SRCDIR/lib" -lc -lntdll > "$log" 2>&1
if [ $? -eq 0 ]; then
	cls=C
elif grep -q "include file .* not found" "$log"; then
	cls=A
else
	cls=B
fi
printf '%s\t%s\n' "$cls" "$f"
ONE_EOF
} > "$W/one.sh"
chmod +x "$W/one.sh"
export W CC CFLAGS_C99FSE CFLAGS_AUTO INC IFACES SUITE
SRCDIR=$srcdir; export SRCDIR

echo "posix-gapmap: compiling $n_tests conformance tests with $CC ..." >&2
# Sorted by test path AFTER the parallel compile, not merely fed in
# sorted: xargs -P returns results in completion order, and several
# columns below ("the first blocking header for this directory") are
# first-match lookups over this file.  Unsorted, those columns change
# between two runs of an unchanged tree -- which would make --check red
# at random and, far worse, make the checked-in diff untrustworthy as the
# changelog this report exists to be.
( cd "$IFACES" && find . -name '*.c' | sed 's|^\./||' | sort ) \
	| xargs -P "$GAPMAP_JOBS" -n1 "$W/one.sh" \
	| sort -t"$(printf '\t')" -k2,2 > "$W/class.tsv"

# -------------------------------------------------- section 1: the levers
#
# For every blocked test, the set of headers that resolve nowhere.  Then
# the decision-useful question, which is NOT "which header is named most
# often": it is "if we added header X, how many tests stop being blocked
# ENTIRELY".  A test naming both <pthread.h> and <semaphore.h> is
# unblocked by neither alone, and a lever table that counted it twice
# would overstate both.
awk -F'\t' '$1=="A"||$1=="B"{print $2}' "$W/class.tsv" > "$W/blocked.txt"
: > "$W/sets.tsv"
while read -r f; do
	a=$(awk -v FILE="$IFACES/$f" -v SEARCH="$SEARCH" -v SUITE="$SUITE" -v DEPTH=1 \
		-f "$W/absent.awk" | sort | tr '\n' ' ')
	printf '%s\t%s\n' "$f" "$a" >> "$W/sets.tsv"
done < "$W/blocked.txt"

# ---------------------------------------------- section 2: class B detail
#
# Sub-classified by scanning the WHOLE compile log with a fixed priority,
# not by its first line: a test whose first diagnostic is an implicit
# declaration (a warning) goes on to die at link, and the link symbol is
# the useful fact.
: > "$W/bdetail.tsv"
awk -F'\t' '$1=="B"{print $2}' "$W/class.tsv" | while read -r f; do
	tag=$(echo "$f" | tr '/' '_'); L="$W/log/$tag.log"
	if grep -q "unresolved reference" "$L"; then
		sub='link'
		what=$(grep -o "unresolved reference to '[^']*'" "$L" | head -1 |
			sed "s/.*'\\(.*\\)'/\\1/")
	elif grep -q "undeclared" "$L"; then
		sub='macro'
		what=$(grep -o "'[A-Za-z_][A-Za-z0-9_]*' undeclared" "$L" | head -1 |
			sed "s/'\\([^']*\\)'.*/\\1/")
	elif grep -q "incomplete type" "$L"; then
		# tcc names neither the type nor the object, so the name has to
		# come from the source line the diagnostic points at.  Worth the
		# extra hop: "27 tests blocked on an incomplete type" is not
		# actionable and "27 tests blocked on struct sched_param" is.
		sub='type'
		loc=$(grep -m1 'incomplete type' "$L")
		lf=${loc%%:*}
		lr=${loc#*:}; lr=${lr%%:*}
		what=""
		case $lr in
		*[!0-9]*|"") ;;
		*) [ -f "$lf" ] && what=$(sed -n "${lr}p" "$lf" |
			grep -o 'struct [A-Za-z_][A-Za-z0-9_]*' | head -1) ;;
		esac
		[ -n "$what" ] || what='(unnamed incomplete type)'
	elif grep -q '#error' "$L"; then
		sub='optprobe'
		what=$(grep -o '#error.*' "$L" | head -1 | sed 's/#error *//; s/"//g')
	else
		sub='other'
		what=$(head -1 "$L" | sed 's/^[^:]*:[0-9]*: *//')
	fi
	printf '%s\t%s\t%s\n' "$sub" "$what" "$f" >> "$W/bdetail.tsv"
done

# `undefined-ok:` is this tree's marker for "declared on purpose, and
# deliberately not implemented" (tools/lint-undefined.sh's exception
# mechanism).  A class B symbol that carries one is a documented
# decision; one that does not is either a hole nobody noticed or a stale
# exception list.  That distinction is the highest-signal line this
# report can print, so it gets its own column rather than a footnote.
find "$srcdir/include" "$srcdir/obj/include" -name '*.h' -print0 |
	xargs -0 grep -h 'undefined-ok:' |
	awk '{
		# The marker always sits on the declaration line, so the name
		# is the last identifier before the first "(".
		i = index($0, "(")
		if (i == 0) next
		s = substr($0, 1, i - 1)
		gsub(/[^A-Za-z0-9_]/, " ", s)
		n = split(s, a, " ")
		if (n > 0) print a[n]
	}' | sort -u > "$W/undefok.txt"

# Declared-anywhere, for the "declared?" column and for the
# reconciliation below.  Comments are stripped first so that a name that
# only ever appears in prose is not mistaken for a prototype.
find "$srcdir/include" "$srcdir/obj/include" -name '*.h' -print0 |
	xargs -0 cat |
	sed -e 's://.*::' |
	tr '\n' ' ' |
	sed -e 's:/\*[^*]*\*\+\([^/*][^*]*\*\+\)*/: :g' |
	grep -o '[A-Za-z_][A-Za-z0-9_]*[ ]*(' |
	sed 's/[ ]*(//' |
	sort -u > "$W/declared.txt"

nm -g --defined-only "$srcdir/lib/libc.a" 2>/dev/null |
	awk '$2 ~ /^[TDBRW]$/ { print $3 }' |
	sed 's/^_//' | sort -u > "$W/defined.txt"

	sm_cache_analysis_store class.tsv sets.tsv bdetail.tsv \
		declared.txt defined.txt undefok.txt
fi

fi

# ------------------------------------------------------ the derivations
#
# From here down nothing reads the suite, the tree or a build log: every
# number is computed from class.tsv, sets.tsv, bdetail.tsv and the three
# membership lists, which is what lets --render reproduce this file byte
# for byte from the data block alone.
#
# The four invariants live HERE rather than in the analysis half on
# purpose.  They have to fire on the cheap path too -- otherwise
# --render, and the pre-commit hook that calls it, would be a way to
# launder a broken or truncated measurement past every one of them.
check_census "$n_tests" "$n_dirs" || exit 1

n_hdr_blocked=$(awk -F'\t' '$2 ~ /[^ ]/' "$W/sets.tsv" | wc -l | tr -d ' ')

n_A=$(awk -F'\t' '$1=="A"' "$W/class.tsv" | wc -l | tr -d ' ')
n_B=$(awk -F'\t' '$1=="B"' "$W/class.tsv" | wc -l | tr -d ' ')
n_C=$(awk -F'\t' '$1=="C"' "$W/class.tsv" | wc -l | tr -d ' ')
n_blocked=$((n_A + n_B))

check_partition "$n_A" "$n_B" "$n_C" "$n_tests" || exit 1
check_floors "$n_C" "$n_blocked" || exit 1

cls_of() { awk -F'\t' -v f="$1" '$2==f{print $1; found=1} END{if(!found) print MISSING}' MISSING=MISSING "$W/class.tsv"; }
check_canaries "$(cls_of "$CANARY_A")" "$(cls_of "$CANARY_C")" || exit 1
# The greedy closure -- which greedy, and why it is that one rather than
# "the most-named header", is stated once in the engine.
awk -F'\t' '$2 ~ /[^ ]/{print $2}' "$W/sets.tsv" > "$W/sets.txt"
sm_closure "$W/sets.txt" > "$W/closure.tsv"
# ----------------------------------------- section 3/4: the per-directory
#
# One row per interface directory (testfrmw excluded -- it is suite
# infrastructure, not an interface), classified from BUILD ARTIFACTS and
# never from either ledger's prose.  That independence is the entire
# value of the reconciliation: a check that read POSIX-GAP-ACCOUNTING.md
# to decide what POSIX-GAP-ACCOUNTING.md should say would be a mirror.
: > "$W/ledger.tsv"
while read -r d; do
	tests=$(awk -F'\t' -v d="$d/" 'index($2, d)==1' "$W/class.tsv" | wc -l | tr -d ' ')
	links=$(awk -F'\t' -v d="$d/" '$1=="C" && index($2, d)==1' "$W/class.tsv" | wc -l | tr -d ' ')
	nA=$(awk -F'\t' -v d="$d/" '$1=="A" && index($2, d)==1' "$W/class.tsv" | wc -l | tr -d ' ')
	nB=$(awk -F'\t' -v d="$d/" '$1=="B" && index($2, d)==1' "$W/class.tsv" | wc -l | tr -d ' ')
	grep -qx "$d" "$W/declared.txt" && dec=yes || dec=no
	grep -qx "$d" "$W/defined.txt"  && def=yes || def=no
	# The `blocking` column: ONE representative for a directory whose
	# tests are typically blocked by several different things.  The rule
	# has to be total and written down, because a reviewer looking at a
	# row has no way to reconstruct why that header won -- and because
	# the implicit rule was not stable.  It used to be "the first absent
	# header of the first blocked test in the directory", which is an
	# arbitrary element of an unordered set twice over: the "first test"
	# depends on the collation class.tsv was sorted under, and the "first
	# header" on the collation each test's absent set was sorted under.
	# That is how `sched_setparam` could keep identical counts
	# (22|4|14|4) while its blocker flipped between `pthread.h` and
	# `sys/pstat.h`, and `timer_create` (11|0|10|1) between `_SC_CPUTIME`
	# and `timer_create`, between two machines measuring the same tree.
	#
	# The rule now: the header absent from the MOST of this directory's
	# blocked tests, ties broken by C-collation name.  Most-frequent is
	# also the decision-useful answer -- it is the one header that
	# unblocks the most of this directory, where the old rule could
	# report a 1-of-10 outlier (`_SC_CPUTIME` above; `timer_create`
	# itself accounts for 7 of that directory's 10) -- and byte
	# collation makes the tie-break total, so the column is a function
	# of the measurement and of nothing else.  Where every header
	# resolves, the same rule is applied to the class B
	# symbols/macros/types instead.
	why=$(awk -F'\t' -v d="$d/" 'index($1, d)==1 {
			n = split($2, h, " ")
			for (i = 1; i <= n; i++) if (h[i] != "") print h[i]
		}' "$W/sets.tsv" |
		sort | uniq -c | sort -k1,1nr -k2,2 | awk 'NR==1 { print $2 }')
	if [ -z "$why" ]; then
		why=$(awk -F'\t' -v d="$d/" 'index($3, d)==1 { print $2 }' "$W/bdetail.tsv" |
			sort | uniq -c | sort -k1,1nr -k2,2 |
			awk 'NR==1 { $1 = ""; sub(/^ +/, ""); print }')
	fi
	[ -n "$why" ] || why='-'
	printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
		"$d" "$tests" "$nA" "$nB" "$links" "$dec" "$def" "$why" >> "$W/ledger.tsv"
done < "$W/dirs.txt"

n_iface_dirs=$(wc -l < "$W/ledger.tsv" | tr -d ' ')
if [ "$n_iface_dirs" -ne "$CENSUS_IFACE_DIRS" ]; then
	echo "posix-gapmap: CENSUS FAILED -- the per-directory ledger has" >&2
	echo "posix-gapmap:   $n_iface_dirs rows, pinned at $CENSUS_IFACE_DIRS." >&2
	exit 1
fi

# The four-cell reconciliation.  Empty is the EXPECTED state and is
# printed as an explicit count, never as an absent section: a section
# that disappears when it has nothing to say is indistinguishable from
# one that was never generated.
rec() { awk -F'\t' -v a="$1" -v b="$2" '$6==a && $7==b { d++; t += $2 } END { printf "%d %d\n", d+0, t+0 }' "$W/ledger.tsv"; }
# The word split is the point: rec() prints "dirs tests" on one line and
# `set --` is how a POSIX shell unpacks a pair.
# shellcheck disable=SC2046
set -- $(rec yes yes);  dd_dirs=$1; dd_tests=$2
# shellcheck disable=SC2046
set -- $(rec yes no);   dn_dirs=$1; dn_tests=$2
# shellcheck disable=SC2046
set -- $(rec no yes);   nd_dirs=$1; nd_tests=$2
# shellcheck disable=SC2046
set -- $(rec no no);    nn_dirs=$1; nn_tests=$2

# "links" is not a synonym for "the interface exists": an option-group
# test compiles its #else branch and reports PTS_UNSUPPORTED, producing
# a linking executable for an interface we do not have.
# "blocked" is not a synonym for "the interface is missing" either: a
# test can be blocked by a header it includes for reasons unrelated to
# its subject.  Both directions are listed separately, because a report
# that conflated either would lie in both directions at once.
awk -F'\t' '$6=="no" && $7=="no" && $5>0 { printf "%s\t%s\n", $1, $5 }' "$W/ledger.tsv" > "$W/exc-links.tsv"
awk -F'\t' '$6=="yes" && $7=="yes" && $5==0 { printf "%s\t%s\n", $1, $2 }' "$W/ledger.tsv" > "$W/exc-blocked.tsv"
n_disagree=$nd_dirs

# A declared-but-undefined interface is only a disagreement if it is NOT
# carrying an `undefined-ok:` marker.  With one, it is exactly the
# ledger's "declared but deliberately unimplemented" bucket and the two
# instruments agree.  Without one, the tree is claiming something it does
# not provide and nothing has noticed.
: > "$W/dn.tsv"
awk -F'\t' '$6=="yes" && $7=="no" { print $1 }' "$W/ledger.tsv" | while read -r d; do
	if grep -qx "$d" "$W/undefok.txt"; then
		printf '%s\tundefined-ok\n' "$d" >> "$W/dn.tsv"
	else
		printf '%s\tnot-marked\n' "$d" >> "$W/dn.tsv"
	fi
done
n_unmarked=$(awk -F'\t' '$2=="not-marked"' "$W/dn.tsv" | wc -l | tr -d ' ')
n_disagree=$((nd_dirs + n_unmarked))

# ------------------------------------------------------------ the report

emit() {
	cat <<EOF
<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# POSIX gap map, measured against the Open POSIX Test Suite

**Generated by \`tools/posix-gapmap.sh\` -- do not edit by hand.**

| | |
|---|---|
| ntlibc | \`$NTLIBC_SHA\` |
| LTP (\`third_party/ltp\`) | \`$LTP_SHA\` |
| suite | \`testcases/open_posix_testsuite/conformance/interfaces/\` |
| census | $n_tests tests in $n_dirs directories ($CENSUS_IFACE_DIRS interfaces + \`testfrmw\`) |
| compiler | \`$CC\` |

This report is an ignored build artefact uploaded by CI. Compare artefacts
when a change intentionally moves compile/link coverage; the source tree
does not carry generated measurements through merges.

It is **not a pass/fail gate**. Its output is a distribution, and a
threshold on a distribution is a number nobody can justify. The census,
partition, floors and canaries instead ensure the measurement discriminates.

Read the taxonomy the right way round. As a *correctness* oracle OPTS is
close to worthless against this tree -- it caught zero of the five
defects our own audits fenced. As a *gap* oracle it is the best
instrument available, because its subject matter is precisely the half of
POSIX this library does not have. **The $n_blocked tests that do not
compile are this file's product, not its failure.**

| class | tests | meaning |
|---|---|---|
| **A** header absent entirely | $n_A | the \`#include\` fails. The honest failure: a portable program finds out at compile time. |
| **B** header present, interface missing | $n_B | the \`#include\` *succeeds* and it dies at link, or on an undeclared macro, or on an incomplete type. **The worst of the three for a downstream consumer**, and the one an interface-granularity ledger cannot see. |
| **C** compiles and links | $n_C | reachable. Not the same as correct, and not the same as present -- see section 3. |

## 1. Header levers

The decision-useful question is not which header is named most often; it
is **how many tests stop being blocked entirely if header X exists**. A
test naming both \`pthread.h\` and \`semaphore.h\` is unblocked by neither
alone, and a table that counted it under both would overstate both.

$n_hdr_blocked of the $n_blocked blocked tests are blocked by at least one
absent header. Greedy closure, recomputed at every step (a header's value
rises as its co-blockers land):

| # | header | tests naming it | unblocked by it alone | unblocked at this step | still blocked after |
|---|---|---|---|---|---|
EOF
	awk -F'\t' '$1>0 { printf "| %s | `%s` | %s | %s | **%s** | %s |\n", $1, $2, $3, $4, $5, $6 }' "$W/closure.tsv"
	cat <<'EOF'

Headers that never take a turn — every test naming them is also blocked
by something else:

| header | tests naming it | unblocked by it alone |
|---|---|---|
EOF
	awk -F'\t' '$1==0 { printf "| `%s` | %s | %s |\n", $2, $3, $4 }' "$W/closure.tsv" |
		sort -t'|' -k3,3nr
	cat <<'EOF'

**Caveat, stated because the number would otherwise be over-read.** A
header appearing does not mean those tests would then *pass*, or even
link — they would still need the functions behind it. The closure
measures how much of the suite becomes *reachable*, which is the right
question for prioritising a header and the wrong one for predicting a
pass rate.

Note also what this ranking does that an interface count cannot:
`aio.h` is 8 interfaces and `sys/mman.h` is 14, but `aio.h` carries more
tests per interface by an order of magnitude. **Counting assertions gives
a materially different priority order from counting interfaces**, and
only the first reflects how much the standard actually says.

## 2. Class B — the header is present and the interface is not

These are the rows an interface-granularity ledger reports as present.
The `#include` succeeds; the failure surfaces at link, or on a macro the
header does not define, or on a type it leaves incomplete — which is
exactly the shape a configure probe gets wrong.

`undefined-ok` marks a symbol this tree *declares on purpose and
deliberately does not implement* (`tools/lint-undefined.sh`'s exception
mechanism). A `not-marked` row is either a hole nobody noticed or a stale
exception list, and it is the highest-signal line in this file.

EOF
	for s in link macro type optprobe other; do
		# shellcheck disable=SC2016  # Markdown backticks, not command substitution
		case $s in
		link)     t='### Unresolved at link (implicit declaration reached the linker)'; c='symbol' ;;
		macro)    t='### Undeclared identifier — a missing **macro**, not a missing function'; c='identifier' ;;
		type)     t='### Incomplete type'; c='type' ;;
		optprobe) t='### The suite'"'"'s own option-group probe firing'; c='`#error`' ;;
		other)    t='### Suite-side'; c='what' ;;
		esac
		n=$(awk -F'\t' -v s="$s" '$1==s' "$W/bdetail.tsv" | wc -l | tr -d ' ')
		printf '%s (%s test(s))\n\n' "$t" "$n"
		if [ "$n" -eq 0 ]; then
			printf 'None.\n\n'
			continue
		fi
		# The declared?/marker columns only mean something for a row
		# that names a C identifier.  A `#error` string is not a symbol
		# and cannot carry an `undefined-ok:` marker, so printing
		# "not-marked" against one would be a made-up finding in the
		# column this report says is its highest-signal one.
		case $s in
		link|macro|type) named=1 ;;
		*) named=0 ;;
		esac
		if [ "$named" -eq 1 ]; then
			printf '| %s | tests | declared? | marker |\n|---|---|---|---|\n' "$c"
		else
			printf '| %s | tests |\n|---|---|\n' "$c"
		fi
		awk -F'\t' -v s="$s" '$1==s { c[$2]++ } END { for (k in c) printf "%s\t%s\n", c[k], k }' "$W/bdetail.tsv" |
			sort -k1,1nr -k2,2 |
			while IFS="$(printf '\t')" read -r cnt what; do
				if [ "$named" -eq 0 ]; then
					# shellcheck disable=SC2016  # Markdown backticks
					printf '| `%s` | %s |\n' "$what" "$cnt"
					continue
				fi
				if grep -qx "$what" "$W/declared.txt"; then dec=yes; else dec=no; fi
				# shellcheck disable=SC2016  # Markdown backticks
				if grep -qx "$what" "$W/undefok.txt"; then mk='`undefined-ok`'; else mk='**not-marked**'; fi
				# shellcheck disable=SC2016  # Markdown backticks
				printf '| `%s` | %s | %s | %s |\n' "$what" "$cnt" "$dec" "$mk"
			done
		printf '\n'
	done
	cat <<EOF
The **macro** rows above are the shape this project's ledger has no row
for: \`POSIX-GAP-ACCOUNTING.md\` is a *function*-interface ledger by
construction, and a missing \`sysconf\` name or a missing \`signal.h\`
constant has nowhere to be recorded in it. They are invisible to it and
visible here, which is the clearest single argument for keeping this
report.

## 3. Reconciliation against \`POSIX-GAP-ACCOUNTING.md\`

The check the ledger cannot perform on itself. Each of the
$CENSUS_IFACE_DIRS interface directories was classified from **build
artifacts** — *declared* = a prototype in \`include/\` or
\`obj/include/\` with comments stripped; *defined* = a \`T\`/\`D\`/\`B\`/\`R\`/\`W\`
symbol in \`nm -g --defined-only lib/libc.a\`. Nothing here reads either
ledger's prose; a check that did would be a mirror.

| | dirs | tests |
|---|---|---|
| declared **and** defined | $dd_dirs | $dd_tests |
| declared, **not** defined | $dn_dirs | $dn_tests |
| defined but **not** declared | $nd_dirs | $nd_tests |
| neither | $nn_dirs | $nn_tests |

### **$n_disagree disagreements.**

EOF
	if [ "$n_disagree" -eq 0 ]; then
		cat <<'EOF'
Nothing OPTS can reach is present that the ledger calls absent, and
nothing is absent that it calls present. That is a null result and it is
worth having: it is an externally-authored check on a classification
built by reading the specification, and it says that classification
survives contact with an independent index. Recording the null result is
the point of running the check — had it come out any other way it would
be the most important line in this file.

EOF
	else
		echo 'Each row below is a place where this tree and an independently'
		echo 'authored index of POSIX disagree about what exists. Triage each one.'
		echo
	fi
	printf 'Declared but not defined:\n\n| interface | marker |\n|---|---|\n'
	if [ -s "$W/dn.tsv" ]; then
		sort "$W/dn.tsv" | while IFS="$(printf '\t')" read -r d m; do
			# shellcheck disable=SC2016  # Markdown backticks
			case $m in undefined-ok) mm='`undefined-ok`' ;; *) mm='**not-marked**' ;; esac
			# shellcheck disable=SC2016  # Markdown backticks
			printf '| `%s` | %s |\n' "$d" "$mm"
		done
	else
		printf '| *(none)* | |\n'
	fi
	cat <<'EOF'

### "links" is not a synonym for "the interface exists"

Directories this tree does not provide at all, which nonetheless produce
a linking executable — the suite compiles its `#else` branch and reports
`PTS_UNSUPPORTED` because the option group is undefined. Correct
behaviour of the suite, and a report that conflated the two would
overstate coverage.

| interface | linking tests |
|---|---|
EOF
	if [ -s "$W/exc-links.tsv" ]; then
		sort "$W/exc-links.tsv" | awk -F'\t' '{ printf "| `%s` | %s |\n", $1, $2 }'
	else
		printf '| *(none)* | |\n'
	fi
	cat <<'EOF'

### "blocked" is not a synonym for "the interface is missing"

Directories this tree *does* provide, with zero linking tests — blocked
by headers the tests include for reasons unrelated to their subject, or
carrying no test files at all.

| interface | tests, all blocked |
|---|---|
EOF
	if [ -s "$W/exc-blocked.tsv" ]; then
		sort "$W/exc-blocked.tsv" | awk -F'\t' '{ printf "| `%s` | %s |\n", $1, $2 }'
	else
		printf '| *(none)* | |\n'
	fi
	cat <<EOF

Both directions are listed separately on purpose. A single "coverage"
number folding them together would be wrong in both directions at once.

## 4. Per-directory ledger

$CENSUS_IFACE_DIRS rows, one per interface directory. This is the raw
material; it goes last because nobody reads it directly, and it is what
makes the diff of a regeneration reviewable.

\`blocking\` is the first absent header for the directory, or, where the
headers all resolve, the first symbol/macro/type that does not.

| interface | tests | A | B | links | declared | defined | blocking |
|---|---|---|---|---|---|---|---|
EOF
	sort "$W/ledger.tsv" |
		awk -F'\t' '{ printf "| `%s` | %s | %s | %s | %s | %s | %s | `%s` |\n", $1, $2, $3, $4, $5, $6, $7, $8 }'

	# ------------------------------------------------- the data block
	#
	# Emitted last, and identically in --generate and --render: every
	# table above is a pure function of these rows, so a re-render has
	# to reproduce the whole file byte for byte.  Sorted, so that the
	# diff of a regeneration stays readable and so that two branches'
	# rows merge as lines rather than as a reshuffle.
	printf '\n%s\n' "$SM_DATA_BEGIN"
	printf '%s\n' 'Do not edit by hand -- see "the data block" in tools/posix-gapmap.sh.'
	printf '\n'
	printf 's\tntlibc\t%s\n' "$NTLIBC_SHA"
	printf 's\tltp\t%s\n'    "$LTP_SHA"
	printf 's\tcc\t%s\n'     "$CC"
	printf 's\ttests\t%s\n'  "$n_tests"
	printf 's\tdirs\t%s\n'   "$n_dirs"
	# One row per test: class, the absent-header set, and the class B
	# subclass/detail where there is one.  Keyed on the test path, which
	# is what makes two branches' rows mergeable.
	awk -F'\t' '
		FILENAME == ARGV[1] { cls[$2] = $1; next }
		FILENAME == ARGV[2] { abs[$1] = $2; next }
		{ bsub[$3] = $1; bwhat[$3] = $2 }
		END {
			for (t in cls)
				printf "t\t%s\t%s\t%s\t%s\t%s\n", \
				       t, cls[t], abs[t], bsub[t], bwhat[t]
		}
	' "$W/class.tsv" "$W/sets.tsv" "$W/bdetail.tsv" | sort
	# One row per interface directory: the three membership answers the
	# reconciliation needs, which come from nm and from scanning this
	# tree's headers and so cannot be recomputed without it.
	while read -r _d; do
		grep -qx "$_d" "$W/declared.txt" && _dec=yes || _dec=no
		grep -qx "$_d" "$W/defined.txt"  && _def=yes || _def=no
		grep -qx "$_d" "$W/undefok.txt"  && _uok=yes || _uok=no
		printf 'd\t%s\t%s\t%s\t%s\n' "$_d" "$_dec" "$_def" "$_uok"
	done < "$W/dirs.txt"
	# And one per class B name, for the declared?/marker columns.  Only
	# the subclasses that name a C identifier: a `#error` string is not a
	# symbol and cannot carry an `undefined-ok:` marker.
	awk -F'\t' '$1=="link" || $1=="macro" || $1=="type" { print $2 }' "$W/bdetail.tsv" |
		sort -u |
		while IFS= read -r _n; do
			[ -n "$_n" ] || continue
			grep -qx "$_n" "$W/declared.txt" && _dec=yes || _dec=no
			grep -qx "$_n" "$W/undefok.txt"  && _uok=yes || _uok=no
			printf 'n\t%s\t%s\t%s\n' "$_n" "$_dec" "$_uok"
		done
	printf '%s\n' "$SM_DATA_END"
}

if [ "$mode" = --generate ]; then
	emit > "$REPORT.tmp" || { rm -f "$REPORT.tmp"; exit 1; }
	mv "$REPORT.tmp" "$REPORT"
	echo "posix-gapmap: wrote $REPORT"
	echo "posix-gapmap: A=$n_A (header absent)  B=$n_B (present, interface missing)  C=$n_C (links)"
	echo "posix-gapmap: $n_blocked of $n_tests blocked; $n_disagree ledger disagreement(s)"
	exit 0
fi

# ------------------------------------------------------------- --check
#
# Regenerates into a temporary file and diffs.  What it deliberately does
# NOT compare is the two SHA lines: the recorded ntlibc SHA is the point
# of the provenance check below, and comparing it here would make the
# stage red on every commit for a reason that has nothing to do with the
# gap.  The diff, not the stamp, is what says this report describes this
# tree -- and it is the whole reason the stamp no longer has to.
[ -f "$REPORT" ] || {
	echo "posix-gapmap: $REPORT does not exist." >&2
	echo "posix-gapmap: generate it with tools/posix-gapmap.sh" >&2
	exit 1; }

# shellcheck disable=SC2016  # a sed script, and Markdown backticks in it
recorded=$(sed -n 's/^| ntlibc | `\(.*\)` |$/\1/p' "$REPORT" | head -1)
check_provenance "$recorded" || exit 1

emit > "$W/new.md" || exit 1
# The recorded ntlibc SHA appears twice now -- in the header table and as
# an `s` row -- and neither is compared, for the same reason: it changes
# on every commit, check_provenance above is what vets it, and
# diffing it here would make this stage red for a reason that has nothing
# to do with the gap.

sm_strip_shas "$REPORT" > "$W/a.md"
sm_strip_shas "$W/new.md" > "$W/b.md"
if diff -u "$W/a.md" "$W/b.md" > "$W/report.diff"; then
	echo "posix-gapmap: OK -- $n_tests tests, A=$n_A B=$n_B C=$n_C, $n_disagree disagreement(s)"
	echo "posix-gapmap: OK -- test/POSIX-GAP-MAP.generated.md is current (ntlibc $recorded)"
	exit 0
fi
echo "posix-gapmap: STALE -- test/POSIX-GAP-MAP.generated.md does not match the tree." >&2
echo "posix-gapmap: Regenerate it and commit the result:" >&2
echo "posix-gapmap:" >&2
echo "posix-gapmap:     make posix-gapmap" >&2
echo "posix-gapmap:" >&2
echo "posix-gapmap: The diff is the point -- it is the changelog entry for" >&2
echo "posix-gapmap: whatever moved.  Below, '-' is the checked-in file and" >&2
echo "posix-gapmap: '+' is this tree." >&2
sed 's/^/    /' "$W/report.diff" >&2
exit 1
