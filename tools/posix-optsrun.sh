#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/posix-optsrun.sh -- EXECUTE the Open POSIX Test Suite tests that
# this library can actually be built against, and write the per-test
# verdicts to test/POSIX-OPTS-RUN.generated.md.
#
# WHAT THIS IS, AND HOW IT DIFFERS FROM tools/posix-gapmap.sh
#
# posix-gapmap.sh answers "how much of OPTS can we be COMPILED against".
# Its product is a distribution over three classes and its job comment
# says, correctly, "No Wine: nothing here is executed, only compiled and
# linked."  So the 591 tests in its class C -- the ones that compile and
# link clean -- had never been RUN.  Nobody knew whether they passed.
# That is the only gap this script closes.
#
# It does NOT relitigate the gap map's argument.  That argument is:
# OPTS is a GAP oracle here and not a correctness one (test/external-
# suites.md measured it catching zero of the five defects this project's
# own audits fenced), its output is a distribution rather than a verdict,
# and "a threshold on that distribution would be a number nobody could
# justify, so there is none".  All of that still holds and none of it is
# an argument against EXECUTING the tests.  Running 591 tests and
# recording which ones fail is a different act from thresholding a count:
#
#   * There is NO pass-count threshold anywhere in this file.  Search for
#     one before adding it; the absence is deliberate.
#   * The report is CHECKED IN, so the DIFF is the signal, exactly as for
#     the gap map.  A test that passed yesterday and fails today is a
#     line in a diff, not a number crossing a line.
#   * What the gate enforces is REGRESSION: a test moving PASS ->
#     not-PASS.  A test moving FAIL -> UNRESOLVED is movement in a
#     population that was already not passing; it shows up in the diff
#     and does not turn the job red.  A gate that fires on everything is
#     as useless as one that fires on nothing.
#
# THE RESULT CODES ARE THE SUITE'S OWN, NOT OURS
#
# The suite's Documentation/HOWTO_ResultCodes says its codes are "taken
# from IEEE Test Methods for Measuring Conformance to POSIX (IEEE
# 1003.3-1991)" and are "a subset of the LSB result codes".
# include/posixtest.h defines them:
#
#   PTS_PASS         0   executed fully, no problems, passed
#   PTS_FAIL         1   executed fully, failed
#   PTS_UNRESOLVED   2   blocked from completing; pass/fail undeterminable
#   PTS_UNSUPPORTED  4   conditional feature not implemented
#   PTS_UNTESTED     5   stub, or the test could not reach its assertion
#
# Nothing here invents a scheme on top of that.  Two buckets exist that
# the suite does not define, because they are things the suite cannot say
# about itself:
#
#   TIMEOUT     the runner killed it.  Its own bucket with a stated
#               reason, never a pass.  See TIMEOUT_SECS below.
#   ABNORMAL    the process exited with a status outside {0,1,2,4,5}.
#               26 tests do this: sigaction/12-1x print "Test FAILED" and
#               then exit(-1), which arrives as 255.  Kept SEPARATE from
#               FAIL on purpose -- an exit(-1) and a PTS_FAIL are
#               different claims about what the test knew it was doing,
#               and the point of measurement is the one place that
#               distinction cannot be recovered later.
#   FLAKY       the test did not agree with itself across ATTEMPTS runs.
#               See below; this one is not a formatting nicety.
#
# WHY FLAKY IS A BUCKET AND NOT A RETRY
#
# The obvious way to handle a test that sometimes passes is to retry it
# and take the best answer.  That was rejected.  The first sweeps found
# clock_nanosleep/1-1.c passing about half the time and printing
# "clock_nanosleep() did not sleep long enough" on the other half.
#
# That is INTERMITTENCE, and it is a fact about this library that only
# execution can produce -- test/external-suites.md's one-shot sweep saw
# the deterministic half of this family ("nanosleep/clock_nanosleep
# return before the interval is observable on the clock", 19 tests,
# judged the suite being stricter than POSIX, which permits a
# _POSIX_CLOCKRES_MIN of 20 ms) but a single run cannot see that two of
# them sit ON the boundary rather than past it.  Whether that is a
# defect or the suite's strictness is a question for the ledger; that it
# is not REPRODUCIBLE is this file's finding, and retrying until it
# passed would have deleted it at the point of measurement.  A
# best-of-N gate is exactly the "make it green" reflex the rest of this
# tree is built to refuse.
#
# So every test runs ATTEMPTS times, always, symmetrically:
#
#   all ATTEMPTS pass            -> PASS
#   no attempt passes            -> the outcome of the last attempt
#   some pass and some do not    -> FLAKY
#
# FLAKY is a NOT-PASS outcome.  A test moving PASS -> FLAKY is a
# regression and turns the gate red, because a test that used to pass
# every time and now passes half the time has got worse.
#
# The residual honesty problem, stated rather than hidden: three runs
# cannot tell "always passes" from "passes 90% of the time", so a
# near-unanimous flake can be recorded PASS by luck and then turn
# --check red on a tree nobody touched.  Two things fence that, and
# neither of them is a retry:
#
#   * --check treats a test the report records as FLAKY as a WILDCARD
#     for staleness -- it may come back with any outcome without making
#     the report stale.  Without this, the two tests below would make
#     --check a coin flip.
#   * FLAKY IS STICKY ACROSS REGENERATION.  A test the checked-in report
#     already records as FLAKY stays FLAKY, so a lucky 3-for-3 sweep
#     cannot quietly promote a known-intermittent test back to PASS and
#     re-arm the coin flip.  Regenerating repeatedly therefore only ever
#     ACCUMULATES known flakes, which is the direction that makes the
#     report more honest rather than less.  OPTSRUN_FRESH=1 clears the
#     set deliberately, which is the only way to clear it.
#
# Stickiness plus a wildcard is exactly the shape that could rot into a
# place to put failures, so it is capped: FLAKY_CEILING below is a
# pinned CEILING and raising it takes a commit that names the test,
# exactly as tools/libc-test.sh requires for its `unclassified` count.
#
# The set at the pin is small and is recorded in the report itself, so
# it is reviewable there rather than here.
#
# THE FAILURE MODE THIS SCRIPT IS BUILT AROUND
#
# It is not "a test fails".  It is that a harness which executes NOTHING
# reports "0 failures", which is indistinguishable from a clean sweep and
# strictly more dangerous, being good news.  This project spent a day
# removing stages that reported success having measured nothing
# (test/verification-measures.md): a windows-test leg that exited 0 on an
# empty artifact; `make asan` reporting 0 passed of 0; a `make kaem`
# whose empty diff came from a run that died before doing anything.  A
# 591-test sweep is the easiest place in the tree to reintroduce that
# defect, because "591 ran, 591 passed" from a harness that ran nothing
# looks exactly like success.
#
# So, three REFUSALS before a single test runs, and five INVARIANTS after:
#
#   R1  The suite is absent or incomplete -> exit 2, naming the one
#       command that fixes it.  Same contract as posix-gapmap.sh's
#       require_suite() and tools/libc-test.sh's.
#   R2  lib/libc.a is missing -> exit 2.  posix-gapmap.sh's reasoning
#       applies verbatim: without it nothing links, the report reads as a
#       total gap, and "that is a build error, not a measurement".
#   R3  Wine is absent or does not work -> exit 2.  This is the refusal
#       this script ADDS and the one that matters most here.  The natural
#       check -- "is `wine` on PATH" -- would be vacuous: a Wine that
#       starts and immediately dies yields zero executions, zero
#       failures, and a green job.  So R3 does not ask whether Wine
#       exists.  It BUILDS a known-good test (the PASS canary), requires
#       it to come back PASS, AND requires its output to contain the
#       string that test prints on success.  Exit status alone is not
#       enough and the reason is concrete: a `wine` that is a stub
#       exiting 0 produces status 0 for every test in the suite, which
#       this script would read as 591 passes.  A process that exits 0
#       having printed nothing did not run the test.  The instrument is
#       proven to work before its silence is trusted.
#
#   I1  LINK CENSUS, cross-checked against the gap map.  The number of
#       tests that link here must equal the class-C count in the
#       CHECKED-IN test/POSIX-GAP-MAP.generated.md.  This is how the
#       class list is consumed rather than reimplemented -- see "THE
#       CONTRACT WITH posix-gapmap.sh" below.  If the two tools disagree
#       about what links, BOTH reports are suspect, and that is the
#       correct severity.
#   I2  RESULT CENSUS.  The journal must hold exactly one outcome line
#       per linked test.  This is what makes a killed sweep FAIL instead
#       of passing: a run that dies at test 300 leaves 300 lines, 591 are
#       expected, and the job goes red.  Checked at the END against an
#       expected count, deliberately NOT read off a summary line -- a
#       process killed mid-sweep never reaches the line that would have
#       said how much it did.  (An empty log after a kill reading as
#       "nothing went wrong" is a defect this project has already hit.)
#   I3  FLOORS, IN BOTH DIRECTIONS.  PASS >= FLOOR_PASS catches a
#       collapse.  not-PASS >= FLOOR_NOTPASS catches the dangerous
#       direction: a runner that has started reporting 0 for everything
#       makes the whole suite read as passing.  Same shape as the gap
#       map's floors, for the same reason.
#   I4  CANARIES, one in each direction, and NAMED IN THE REPORT so a
#       failure is legible without reading this file.
#   I5  ANCESTRY.  The report records the ntlibc SHA and --check fails if
#       it is not an ancestor of HEAD.  Nothing in the file's contents
#       can reveal that it describes a tree from three months ago.
#
# THE CONTRACT WITH posix-gapmap.sh
#
# This script does NOT modify, call into, or duplicate the gap map's
# classifier.  It consumes exactly ONE documented number out of the
# checked-in report: the class-C count, read from the row of the class
# table whose first cell contains `**C**`, in
# test/POSIX-GAP-MAP.generated.md.  That is the whole coupling.
#
# It is a parse of a specific line, stated here so that a future
# refactor of posix-gapmap.sh which moves that row produces a LOUD parse
# error naming this contract, rather than a silently wrong census.  If
# you are that refactor: keep a class table row whose first cell holds
# `**C**` and whose second cell is the count, or update read_gapmap_C()
# below in the same commit.
#
# WHY SERIAL
#
# The whole sweep is ~80 s serial.  OPTS contains timing assertions
# (nanosleep, clock_*, sched_*) that are sensitive to load, and
# tools/libc-test.sh already measured Wine process startup under gate
# contention at 40x its idle cost.  This report is CHECKED IN and its
# diff is the entire signal, so a timing test that flips under parallel
# load costs more than the ~50 s parallelism would save.  Serial is not
# an oversight.
#
# Usage:
#   tools/posix-optsrun.sh            regenerate test/POSIX-OPTS-RUN.generated.md
#   tools/posix-optsrun.sh --check    run the sweep, enforce every invariant,
#                                     and fail on any REGRESSION against the
#                                     checked-in report; write nothing
#   tools/posix-optsrun.sh --selftest prove the invariants above still fire
#
# Env:
#   OPTSRUN_JOBS    parallel COMPILES (default 4).  The RUN is always serial.
#   OPTSRUN_ATTEMPTS  runs per test (default 3).  Raise it to bootstrap or
#                   widen the sticky FLAKY set; see below.
#   OPTSRUN_FRESH   set to 1 to regenerate WITHOUT inheriting the
#                   checked-in report's FLAKY set.  This is how the flake
#                   set is cleared, and it is deliberately not the
#                   default: see "WHY FLAKY IS A BUCKET" above.
#   OPTSRUN_GITDIR  repository to resolve the SHA-ancestry check against.
#                   Defaults to the source tree.  tools/gate.sh sets it,
#                   because its stages run in an rsync'd copy with no
#                   .git of its own.

set -u

# ------------------------------------------------------------ determinism
#
# Byte ordering, for the same reason posix-gapmap.sh sets it: this report
# is CHECKED IN, glibc's UTF-8 collation ignores punctuation at the first
# comparison level, and a developer's locale and CI's would otherwise
# disagree permanently about where `sched_getparam` sorts.
LC_ALL=C
export LC_ALL

# shellcheck disable=SC1007
srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$srcdir" || exit 1

SUITE="$srcdir/third_party/ltp/testcases/open_posix_testsuite"
IFACES="$SUITE/conformance/interfaces"
REPORT="$srcdir/test/POSIX-OPTS-RUN.generated.md"
GAPMAP="$srcdir/test/POSIX-GAP-MAP.generated.md"

# ------------------------------------------------------- pinned constants

# I1.  The census of test sources, identical to posix-gapmap.sh's and
# pinned for the same reason: a glob that stops finding the suite must
# announce itself before anything reports a number.
CENSUS_TESTS=1610

# I3.  Measured at ntlibc 5270f20 / LTP 4c0cfb8 / wine-9.0: PASS=358,
# not-PASS=233 of 591 executed.  Both floors sit ~12% below the
# measurement, the same headroom the gap map uses -- far enough not to
# trip on ordinary movement, close enough that a collapse in either
# direction is caught.
#
# FLOOR_NOTPASS is the one that is easy to forget and is the reason this
# script has two floors rather than one: a Wine that has started exiting
# 0 without running anything, or a runner that lost its exit status,
# makes the entire suite read as PASSING.  That is good news shaped like
# a broken instrument.
FLOOR_PASS=315
FLOOR_NOTPASS=200

# I4.  One canary in each direction, named individually so a failure says
# which way the measurement broke, and reproduced into the report so the
# person reading a red job does not have to come here to find out.
#
#   CANARY_PASS  strlen with two static strings.  If this stops passing,
#                the runner is broken, not strlen.  It is also what R3
#                uses to prove Wine works at all, which is why
#                CANARY_PASS_MARK exists beside it.
#   CANARY_NOTPASS  sigaltstack/1-1.c asks whether a signal handler runs
#                on the alternate stack.  This library has no
#                sigaltstack, so it fails, and it fails for a STRUCTURAL
#                reason: no fork (so it does not depend on Wine's missing
#                RtlCloneUserProcess) and no timing assertion (so it does
#                not depend on machine load).  Both were deliberate --
#                the obvious candidates, raise/1-2.c and kill/1-2.c, both
#                fork, and a canary whose verdict depends on the runner
#                rather than on the library cannot distinguish the two.
#                If it starts passing, either that gap closed -- move the
#                canary in the commit that closed it -- or the runner has
#                started reporting PASS unconditionally.
CANARY_PASS=strlen/1-1.c
CANARY_NOTPASS=sigaltstack/1-1.c

# What CANARY_PASS prints on success.  R3 requires this string in the
# canary's output, not merely a zero exit status, because a `wine` that
# is a stub exiting 0 gives a zero exit status for all 591 tests.  A
# process that exited 0 having printed nothing did not run anything.
# The suite's own convention (Documentation/HOWTO_CodingGuidelines and
# every conformance test in the tree) is to print exactly this on the
# PTS_PASS path, so it is a property of the suite rather than of one
# test -- but it is pinned here so a change to it is a deliberate edit.
CANARY_PASS_MARK='Test Passed'

# The per-test kill.  120 s, matching tools/libc-test.sh's bound and its
# reasoning: this exists to stop a wedged test wedging the stage, not to
# police runtime, so it must sit far above anything a working test needs.
#
# Sized from a measurement, not a guess.  nanosleep/10000-1.c sleeps
# 0+1+1+2+10+13 = 27 real seconds by construction; at the 10 s bound this
# started with it was killed and recorded TIMEOUT, which would have
# turned a legitimately slow test into a measurement artifact.  At 120 s
# it completes and returns a real verdict, and NO test in the suite hits
# the bound.
TIMEOUT_SECS=120

# How many times each test runs.  Three, not one, because a single run
# cannot tell PASS from "passed this time"; and three, not five, because
# the sweep cost is linear in this and the marginal discrimination is
# not.  See "WHY FLAKY IS A BUCKET AND NOT A RETRY" above.
# Overridable so the checked-in report can be BOOTSTRAPPED with a higher
# count (which is how the sticky FLAKY set is seeded) without CI paying
# for it on every push.  Not overridable downwards in CI: the ci.yml job
# does not set it.
DEFAULT_ATTEMPTS=3
ATTEMPTS=${OPTSRUN_ATTEMPTS:-$DEFAULT_ATTEMPTS}

# The CEILING on the FLAKY population, not a target.  A ceiling and not
# a pinned equality on purpose: FLAKY is the one outcome --check treats
# as a staleness wildcard, so it is the one bucket that could quietly
# become a place to put failures.  Raising it requires a commit that
# says which test became flaky and why, exactly as tools/libc-test.sh
# requires for its `unclassified` count.
#
# Sized from the population it can plausibly hold rather than from the
# measurement plus a margin.  Every flake found so far is in the
# nanosleep/clock_nanosleep family, and that is not a coincidence:
# test/external-suites.md already records 19 tests of that family
# failing because they "return before the interval is observable on the
# clock", against a suite that asks for 3 ns where POSIX permits a
# _POSIX_CLOCKRES_MIN of 20 ms.  Tests sitting exactly ON that boundary
# are the ones that flip.  The two directories hold 24 executable tests
# between them, so a ceiling of 12 is half that family and still far
# below "anything could go in here" -- and a flake appearing OUTSIDE
# those two directories should be looked at rather than absorbed, which
# a ceiling this size makes likely to be noticed.
FLAKY_CEILING=12

usage() { sed -n '2,175p' "$0" | sed 's/^# \{0,1\}//'; }

# ------------------------------------------------------------- R1: suite
#
# A clone made without --recurse-submodules leaves third_party/ltp an
# EMPTY DIRECTORY.  Every loop below would then find nothing, run
# nothing, and report no failures -- the exact good-news-shaped failure
# this script exists to prevent.  Loud error naming the fix, never a
# skip, never a pass.
require_suite() {
	if [ -d "$IFACES" ] && [ -f "$SUITE/lib/common.c" ] &&
	   [ -f "$SUITE/include/posixtest.h" ]; then
		return 0
	fi
	echo "posix-optsrun: third_party/ltp is empty or incomplete." >&2
	echo "posix-optsrun: it is a git submodule -- the Linux Test Project," >&2
	echo "posix-optsrun: whose testcases/open_posix_testsuite/ is the Open" >&2
	echo "posix-optsrun: POSIX Test Suite this report EXECUTES -- pinned at" >&2
	echo "posix-optsrun: the SHA recorded in third_party/README.md, and this" >&2
	echo "posix-optsrun: clone does not have it checked out.  Fix it with:" >&2
	echo "posix-optsrun:" >&2
	echo "posix-optsrun:     git submodule update --init --recursive" >&2
	echo "posix-optsrun:" >&2
	echo "posix-optsrun: This is an error, not a skip.  Without the suite" >&2
	echo "posix-optsrun: this script would run nothing and report no" >&2
	echo "posix-optsrun: failures -- and 'no failures because nothing ran'" >&2
	echo "posix-optsrun: is indistinguishable from 'no failures'." >&2
	exit 2
}

# ------------------------------------------------------ the five invariants
#
# Each takes its inputs as arguments and returns 0/1, for the same reason
# posix-gapmap.sh does it that way: --selftest can then call the REAL
# comparison with synthetic numbers and assert it rejects them.  An
# invariant expressed inline in the main flow can only ever be tested by
# breaking the tree.

# I1: the link census agrees with the gap map's class C.
check_link_census() {
	_linked=$1; _gapC=$2
	if [ -z "$_gapC" ]; then
		echo "posix-optsrun: LINK CENSUS FAILED -- could not read the class-C" >&2
		echo "posix-optsrun:   count out of $GAPMAP." >&2
		echo "posix-optsrun: This script consumes exactly one number from that" >&2
		echo "posix-optsrun:   report: the class table row whose first cell" >&2
		echo "posix-optsrun:   contains '**C**'.  See 'THE CONTRACT WITH" >&2
		echo "posix-optsrun:   posix-gapmap.sh' in this file's header.  If that" >&2
		echo "posix-optsrun:   report's format moved, update read_gapmap_C()" >&2
		echo "posix-optsrun:   in the same commit." >&2
		return 1
	fi
	if [ "$_linked" -eq "$_gapC" ]; then return 0; fi
	echo "posix-optsrun: LINK CENSUS FAILED -- $_linked test(s) link here," >&2
	echo "posix-optsrun:   but $GAPMAP records class C = $_gapC." >&2
	echo "posix-optsrun: The two tools disagree about which tests can be" >&2
	echo "posix-optsrun:   built at all, so BOTH reports are suspect: one of" >&2
	echo "posix-optsrun:   them is describing a tree that is not this one." >&2
	echo "posix-optsrun: Regenerate the gap map first (tools/posix-gapmap.sh)," >&2
	echo "posix-optsrun:   then this report." >&2
	return 1
}

# I2: one outcome per linked test, no more and no fewer.
#
# The load-bearing invariant for a killed sweep.  A scope kill, an OOM,
# or a Wine crash partway through leaves a SHORT journal, and a short
# journal must be a failure rather than a small clean result set.
check_result_census() {
	_got=$1; _want=$2
	if [ "$_got" -eq "$_want" ] && [ "$_want" -gt 0 ]; then return 0; fi
	if [ "$_want" -le 0 ]; then
		echo "posix-optsrun: RESULT CENSUS FAILED -- zero tests were expected" >&2
		echo "posix-optsrun:   to run.  A sweep with nothing in it cannot" >&2
		echo "posix-optsrun:   report 'no failures'." >&2
		return 1
	fi
	echo "posix-optsrun: RESULT CENSUS FAILED -- $_got outcome(s) recorded," >&2
	echo "posix-optsrun:   $_want expected." >&2
	if [ "$_got" -lt "$_want" ]; then
		echo "posix-optsrun: The sweep did not finish.  A run killed partway" >&2
		echo "posix-optsrun:   through -- OOM, a scope kill, a Wine crash --" >&2
		echo "posix-optsrun:   leaves exactly this: a short journal whose" >&2
		echo "posix-optsrun:   contents contain no failures because the tests" >&2
		echo "posix-optsrun:   that would have failed never ran.  That must" >&2
		echo "posix-optsrun:   never read as success, which is why this count" >&2
		echo "posix-optsrun:   is checked against an EXPECTED number and not" >&2
		echo "posix-optsrun:   against a summary line the dead process never" >&2
		echo "posix-optsrun:   got to write." >&2
	else
		echo "posix-optsrun: More outcomes than tests: a stale journal was" >&2
		echo "posix-optsrun:   reused, or a test was recorded twice." >&2
	fi
	return 1
}

# The FLAKY ceiling.  Not a floor and not an equality: FLAKY is the one
# outcome --check accepts as a staleness wildcard, which makes it the one
# bucket that could silently become a place to put failures.  Same
# discipline tools/libc-test.sh applies to `unclassified`.
check_flaky_ceiling() {
	_fl=$1
	if [ "$_fl" -le "$FLAKY_CEILING" ]; then return 0; fi
	echo "posix-optsrun: FLAKY CEILING FAILED -- $_fl test(s) disagreed with" >&2
	echo "posix-optsrun:   themselves across $ATTEMPTS runs, ceiling is $FLAKY_CEILING." >&2
	echo "posix-optsrun: FLAKY is the only outcome --check treats as a" >&2
	echo "posix-optsrun:   wildcard, so it is the only bucket that could" >&2
	echo "posix-optsrun:   quietly absorb real failures.  It is capped for" >&2
	echo "posix-optsrun:   that reason and it is not a place to put things." >&2
	echo "posix-optsrun: Raise FLAKY_CEILING only in a commit that names the" >&2
	echo "posix-optsrun:   test that became flaky and says why." >&2
	return 1
}

# I3: floors, in both directions.
check_floors() {
	_pass=$1; _notpass=$2; _rc=0
	if [ "$_pass" -lt "$FLOOR_PASS" ]; then
		echo "posix-optsrun: FLOOR FAILED -- only $_pass test(s) passed, floor" >&2
		echo "posix-optsrun:   is $FLOOR_PASS.  A collapse this large is usually the" >&2
		echo "posix-optsrun:   runner or the build, not the library losing 40" >&2
		echo "posix-optsrun:   interfaces overnight -- but check the per-test" >&2
		echo "posix-optsrun:   diff before assuming so." >&2
		_rc=1
	fi
	if [ "$_notpass" -lt "$FLOOR_NOTPASS" ]; then
		echo "posix-optsrun: FLOOR FAILED -- only $_notpass test(s) did not pass," >&2
		echo "posix-optsrun:   floor is $FLOOR_NOTPASS.  This is the dangerous" >&2
		echo "posix-optsrun:   direction: a runner that has started reporting 0" >&2
		echo "posix-optsrun:   for everything, or an exit status that stopped" >&2
		echo "posix-optsrun:   being propagated, makes the whole suite read as" >&2
		echo "posix-optsrun:   PASSING.  That is good news shaped like a broken" >&2
		echo "posix-optsrun:   instrument." >&2
		echo "posix-optsrun: If this library really did fix 33 tests, lower" >&2
		echo "posix-optsrun:   FLOOR_NOTPASS in the commit that shows the diff." >&2
		_rc=1
	fi
	return $_rc
}

# I4: the two canaries.
check_canaries() {
	_p=$1; _n=$2; _rc=0
	if [ "$_p" != PASS ]; then
		echo "posix-optsrun: CANARY FAILED -- $CANARY_PASS came back $_p, want PASS." >&2
		echo "posix-optsrun:   This canary is two static strings and strlen.  If" >&2
		echo "posix-optsrun:   it does not pass, the RUNNER is broken." >&2
		_rc=1
	fi
	if [ "$_n" = PASS ]; then
		echo "posix-optsrun: CANARY FAILED -- $CANARY_NOTPASS came back PASS," >&2
		echo "posix-optsrun:   want anything else." >&2
		echo "posix-optsrun:   Either that gap genuinely closed -- move the" >&2
		echo "posix-optsrun:   canary in the commit that closed it -- or the" >&2
		echo "posix-optsrun:   runner has started answering PASS constantly." >&2
		_rc=1
	fi
	if [ "$_n" = MISSING ] || [ "$_p" = MISSING ]; then
		echo "posix-optsrun: CANARY FAILED -- a canary produced no outcome at" >&2
		echo "posix-optsrun:   all, so it was not among the tests that ran." >&2
		_rc=1
	fi
	if [ $_rc -ne 0 ]; then
		echo "posix-optsrun: The canaries exist to tell 'the library changed'" >&2
		echo "posix-optsrun:   from 'the measurement broke'.  Real movement" >&2
		echo "posix-optsrun:   shifts the population and leaves these two alone;" >&2
		echo "posix-optsrun:   a runner that has started answering constantly" >&2
		echo "posix-optsrun:   moves them too.  Do not adjust a canary to make" >&2
		echo "posix-optsrun:   this pass." >&2
	fi
	return $_rc
}

# I5: the report's recorded ntlibc SHA must be an ancestor of HEAD.
#
# Same construction and same reasoning as posix-gapmap.sh's: if no
# repository is reachable this FAILS rather than skipping, because "I
# could not check" and "it checks out" are different claims.
check_ancestry() {
	_sha=$1
	_gd=${2:-$OPTSRUN_GITDIR}
	if ! git -C "$_gd" rev-parse --git-dir >/dev/null 2>&1; then
		echo "posix-optsrun: ANCESTRY FAILED -- $_gd is not a git repository," >&2
		echo "posix-optsrun:   so the report's recorded ntlibc SHA cannot be" >&2
		echo "posix-optsrun:   checked against HEAD.  Point OPTSRUN_GITDIR at" >&2
		echo "posix-optsrun:   the real tree.  This is a failure and not a skip." >&2
		return 1
	fi
	if [ -z "$_sha" ]; then
		echo "posix-optsrun: ANCESTRY FAILED -- the report records no ntlibc SHA." >&2
		return 1
	fi
	if ! git -C "$_gd" cat-file -e "$_sha^{commit}" 2>/dev/null; then
		echo "posix-optsrun: ANCESTRY FAILED -- the report records ntlibc SHA" >&2
		echo "posix-optsrun:   $_sha, which this repository does not have." >&2
		return 1
	fi
	if ! git -C "$_gd" merge-base --is-ancestor "$_sha" HEAD 2>/dev/null; then
		echo "posix-optsrun: ANCESTRY FAILED -- the report records ntlibc SHA" >&2
		echo "posix-optsrun:   $_sha, which is not an ancestor of HEAD." >&2
		echo "posix-optsrun: Regenerate it: tools/posix-optsrun.sh" >&2
		return 1
	fi
	return 0
}

# THE GATE.  Regression only: PASS -> anything else.
#
# Takes two "OUTCOME<TAB>path" streams (paths, files) and returns 1 if
# any test that the OLD stream records as PASS is not PASS in the NEW
# one.  Deliberately asymmetric:
#
#   PASS -> FAIL          red.  a regression.
#   FAIL -> ABNORMAL      not red.  movement inside a population that was
#                         already not passing.  It is in the diff, which
#                         is where it belongs.
#   FAIL -> PASS          not red.  it is an improvement, and forcing the
#                         report to be regenerated is what --check's
#                         staleness comparison is for, not this.
#   PASS -> (absent)      red.  a test that used to pass and produced no
#                         outcome at all is the worst case, not the best.
#
# A gate that fired on every kind of movement would be as useless as one
# that fired on none, and would train people to regenerate the report
# without reading it.
find_regressions() {
	_old=$1; _new=$2
	awk -F'\t' '
		NR == FNR { if ($1 == "PASS") was[$2] = 1; next }
		{ now[$2] = $1 }
		END {
			for (t in was)
				if (!(t in now))       printf "%s\tPASS\t(no outcome)\n", t
				else if (now[t] != "PASS") printf "%s\tPASS\t%s\n", t, now[t]
		}
	' "$_old" "$_new" | sort
}

# ------------------------------------------------ the gap-map class-C read
#
# The ENTIRE coupling to posix-gapmap.sh.  Documented in this file's
# header as a contract so that a refactor which moves the row fails
# loudly here instead of producing a silently wrong census.
read_gapmap_C() {
	[ -f "$GAPMAP" ] || return 0
	sed -n 's/^| \*\*C\*\*[^|]*| *\([0-9][0-9]*\) *|.*/\1/p' "$GAPMAP" | head -1
}

# ------------------------------------------------------------- the selftest
#
# Every invariant above is a comparison, and a comparison that is never
# exercised is a comment.  These call the REAL functions with synthetic
# inputs and assert they reject them, so the guards are proven to fire
# without anybody having to break the tree to find out.
selftest() {
	fails=0
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
	ck "link census rejects a disagreement"    1 check_link_census 590 591
	ck "link census rejects an unreadable map" 1 check_link_census 591 ""
	ck "link census accepts agreement"         0 check_link_census 591 591
	ck "result census rejects a killed sweep"  1 check_result_census 300 591
	ck "result census rejects a duplicate"     1 check_result_census 592 591
	ck "result census rejects an empty sweep"  1 check_result_census 0 0
	ck "result census accepts a full sweep"    0 check_result_census 591 591
	ck "pass floor rejects a collapse"         1 check_floors 0 591
	ck "notpass floor rejects an all-green"    1 check_floors 591 0
	ck "floors accept the measurement"         0 check_floors 358 233
	ck "flaky ceiling rejects a bulge"         1 check_flaky_ceiling $((FLAKY_CEILING + 1))
	ck "flaky ceiling accepts the ceiling"     0 check_flaky_ceiling "$FLAKY_CEILING"
	ck "flaky ceiling accepts none"            0 check_flaky_ceiling 0
	ck "canaries reject a stuck-PASS answer"   1 check_canaries PASS PASS
	ck "canaries reject a stuck-FAIL answer"   1 check_canaries FAIL FAIL
	ck "canaries reject a missing outcome"     1 check_canaries PASS MISSING
	ck "canaries accept PASS and FAIL"         0 check_canaries PASS FAIL
	ck "ancestry rejects a non-repository"     1 check_ancestry HEAD /nonexistent-not-a-repo
	ck "ancestry rejects an empty SHA"         1 check_ancestry ""
	ck "ancestry rejects an unknown SHA"       1 check_ancestry 0000000000000000000000000000000000000000

	# The regression gate, exercised in BOTH directions.  Proving that it
	# does NOT fire on movement within the not-PASS population matters as
	# much as proving that it fires on PASS -> FAIL: a gate that fires on
	# everything is as useless as one that fires on nothing.
	sd=$(mktemp -d "${TMPDIR:-/tmp}/optsrun-selftest.XXXXXX") || return 1
	printf 'PASS\ta/1-1.c\nFAIL\tb/1-1.c\nUNTESTED\tc/1-1.c\n' > "$sd/old"
	printf 'FAIL\ta/1-1.c\nFAIL\tb/1-1.c\nUNTESTED\tc/1-1.c\n' > "$sd/reg"
	printf 'PASS\ta/1-1.c\nABNORMAL\tb/1-1.c\nPASS\tc/1-1.c\n'  > "$sd/move"
	printf 'FAIL\tb/1-1.c\nUNTESTED\tc/1-1.c\n'                 > "$sd/gone"
	ckout() { # ckout DESC WANT-LINES OLD NEW
		co_desc=$1; co_want=$2
		co_got=$(find_regressions "$3" "$4" | wc -l | tr -d ' ')
		if [ "$co_got" -eq "$co_want" ]; then
			echo "selftest: OK   -- $co_desc"
		else
			echo "selftest: FAIL -- $co_desc ($co_got regressions, wanted $co_want)" >&2
			fails=$((fails + 1))
		fi
	}
	ckout "gate FIRES on PASS -> FAIL"                  1 "$sd/old" "$sd/reg"
	ckout "gate is SILENT on FAIL -> ABNORMAL"          0 "$sd/old" "$sd/move"
	ckout "gate is SILENT on UNTESTED -> PASS"          0 "$sd/old" "$sd/move"
	ckout "gate FIRES on PASS -> no outcome at all"     1 "$sd/old" "$sd/gone"
	ckout "gate is SILENT on an unchanged sweep"        0 "$sd/old" "$sd/old"
	printf 'FLAKY\ta/1-1.c\nFAIL\tb/1-1.c\nUNTESTED\tc/1-1.c\n' > "$sd/flaky"
	ckout "gate FIRES on PASS -> FLAKY"                 1 "$sd/old" "$sd/flaky"
	rm -rf "$sd"
	nck=26

	if git -C "$OPTSRUN_GITDIR" rev-parse HEAD >/dev/null 2>&1; then
		ck "ancestry accepts HEAD itself"      0 check_ancestry "$(git -C "$OPTSRUN_GITDIR" rev-parse HEAD)"
		nck=$((nck + 1))
	else
		echo "selftest: NOTE -- $OPTSRUN_GITDIR is not a git repository, so the"
		echo "selftest:         positive ancestry check was not run.  Set"
		echo "selftest:         OPTSRUN_GITDIR to exercise it."
	fi
	if [ "$fails" -ne 0 ]; then
		echo "selftest: $fails invariant check(s) did not behave as documented." >&2
		return 1
	fi
	echo "selftest: all $nck invariant checks fire as documented."
	return 0
}

# ------------------------------------------------------------------ modes

mode=${1:---generate}
case "$mode" in
--generate|--check|--selftest) ;;
-h|--help) usage; exit 0 ;;
*) usage >&2; exit 2 ;;
esac

: "${OPTSRUN_JOBS:=4}"
: "${OPTSRUN_FRESH:=0}"
: "${OPTSRUN_GITDIR:=$srcdir}"

[ "$mode" = --selftest ] && { selftest; exit $?; }

require_suite

# ------------------------------------------------------------------ config

[ -f "$srcdir/config.mak" ] || {
	echo "posix-optsrun: no config.mak; run ./configure first." >&2; exit 2; }
cfg() { sed -n "s/^$1 *= *//p" "$srcdir/config.mak" | tail -1; }
CC=$(cfg CC); ARCH=$(cfg ARCH)
CFLAGS_C99FSE=$(cfg CFLAGS_C99FSE); CFLAGS_AUTO=$(cfg CFLAGS_AUTO)
[ -n "$CC" ] || { echo "posix-optsrun: config.mak has no CC." >&2; exit 2; }

# R2.  posix-gapmap.sh's reasoning, applied to execution: without the
# library nothing links, nothing runs, and the report says "no failures"
# about a sweep of zero tests.
[ -f "$srcdir/lib/libc.a" ] || {
	echo "posix-optsrun: lib/libc.a is missing; run make first." >&2
	echo "posix-optsrun: without it every test would fail to LINK, nothing" >&2
	echo "posix-optsrun: would be executed, and this report would say 'no" >&2
	echo "posix-optsrun: failures' about a sweep of zero tests.  That is a" >&2
	echo "posix-optsrun: build error, not a measurement." >&2
	exit 2; }

# The per-test block out of a checked-in report.  Used by --check to
# compare against, and by --generate to inherit the sticky FLAKY set.
recorded_block() { # recorded_block FILE
	awk '/^## Per-test outcomes$/{s=1} s && /^```$/{f++; next} s && f==1' "$1"
}

W=$(mktemp -d "${TMPDIR:-/tmp}/ntlibc-optsrun.XXXXXX") || exit 1
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/exe" "$W/run"

# The journal.  Appended to one line at a time, as each outcome is
# produced, precisely because a scope kill gives no cleanup window: what
# survives a kill is what was already written, and I2 is what turns that
# short file into a failure instead of a small clean result.
JOURNAL="$W/results.tsv"
: > "$JOURNAL"

# --------------------------------------------------- the compile/link line
#
# Byte-identical to the one posix-gapmap.sh uses -- the Makefile's
# obj/test/%.exe include set plus the suite's two include roots, as
# test/external-suites.md's "Reproducing these numbers" records it.  It
# has to be identical or I1 would be comparing two different questions.
INC="-I$srcdir/arch/$ARCH -I$srcdir/arch/generic -I$srcdir/obj/include -I$srcdir/include -I$SUITE/include -I$SUITE"

cat > "$W/build-one.sh" <<'BUILD_EOF'
#!/bin/sh
f=$1
tag=$(echo "$f" | tr '/' '_')
# shellcheck disable=SC2086  # word lists from config.mak, as the Makefile uses them
if $CC $CFLAGS_C99FSE $CFLAGS_AUTO -D_GNU_SOURCE $INC -nostdlib \
    -o "$W/exe/$tag.exe" "$SRCDIR/lib/crt1.o" \
    "$IFACES/$f" "$SUITE/lib/common.c" \
    -L"$SRCDIR/lib" -lc -lntdll >/dev/null 2>&1; then
	printf '%s\n' "$f"
else
	rm -f "$W/exe/$tag.exe"
fi
BUILD_EOF
chmod +x "$W/build-one.sh"
export W CC CFLAGS_C99FSE CFLAGS_AUTO INC IFACES SUITE
SRCDIR=$srcdir; export SRCDIR

# ------------------------------------------------------------- the census

n_tests=$(find "$IFACES" -name '*.c' | wc -l | tr -d ' ')
if [ "$n_tests" -ne "$CENSUS_TESTS" ]; then
	echo "posix-optsrun: CENSUS FAILED -- found $n_tests test sources under" >&2
	echo "posix-optsrun:   conformance/interfaces/, pinned at $CENSUS_TESTS." >&2
	echo "posix-optsrun: Either the LTP pin moved (bump CENSUS_TESTS in the" >&2
	echo "posix-optsrun:   same commit, alongside posix-gapmap.sh's) or the" >&2
	echo "posix-optsrun:   discovery glob has stopped finding the suite." >&2
	exit 1
fi

# The LTP pin, read the same two ways posix-gapmap.sh reads it and with
# the same hard error when they disagree: the report would otherwise be
# measured against one suite and labelled with another.
ltp_head=$(git -C "$SUITE" rev-parse HEAD 2>/dev/null || true)
ltp_link=$(git -C "$OPTSRUN_GITDIR" rev-parse "HEAD:third_party/ltp" 2>/dev/null || true)
if [ -n "$ltp_head" ] && [ -n "$ltp_link" ] && [ "$ltp_head" != "$ltp_link" ]; then
	echo "posix-optsrun: PIN FAILED -- third_party/ltp is checked out at" >&2
	echo "posix-optsrun:   $ltp_head" >&2
	echo "posix-optsrun: but this repository pins" >&2
	echo "posix-optsrun:   $ltp_link" >&2
	echo "posix-optsrun: The verdicts would be measured against one suite and" >&2
	echo "posix-optsrun:   labelled with another." >&2
	exit 1
fi
LTP_SHA=${ltp_head:-$ltp_link}
[ -n "$LTP_SHA" ] || {
	echo "posix-optsrun: could not determine which LTP revision is checked" >&2
	echo "posix-optsrun:   out at $SUITE.  The report's whole claim is 'these" >&2
	echo "posix-optsrun:   verdicts came from THIS suite at THIS revision'." >&2
	exit 2; }
NTLIBC_SHA=$(git -C "$OPTSRUN_GITDIR" rev-parse HEAD 2>/dev/null || echo unknown)

# --------------------------------------------------------------- R3: wine
#
# Not "is wine on PATH".  Build the PASS canary and require it to come
# back PASS.  A Wine that starts and immediately dies would satisfy a
# PATH check and then produce zero executions, zero failures, and a green
# job -- which is the precise defect this whole file is built around.
WINE=${WINE:-wine}
WINE_VER=$(WINEDEBUG=-all "$WINE" --version 2>/dev/null </dev/null || true)

attempt_one() { # attempt_one TEST-PATH ATTEMPT-N -> prints one attempt's outcome
	_f=$1; _n=$2; _tag=$(echo "$_f" | tr '/' '_')
	# Each test gets a fresh working directory: OPTS tests create files
	# with fixed names and would otherwise collide with each other and
	# with reruns.
	_d=$(mktemp -d "$W/run/w.XXXXXX") || { echo UNRESOLVED; return; }
	( cd "$_d" && WINEDEBUG=-all WINEDLLOVERRIDES=winedbg.exe=d \
	    timeout -k 5 "$TIMEOUT_SECS" "$WINE" "$W/exe/$_tag.exe" \
	) >"$W/run/$_tag.$_n.log" 2>&1 </dev/null
	_rc=$?
	rm -rf "$_d"
	# The suite's own codes (include/posixtest.h, and see
	# Documentation/HOWTO_ResultCodes for their IEEE 1003.3-1991
	# provenance), plus the two the suite cannot say about itself.
	case $_rc in
	0)   echo PASS ;;
	1)   echo FAIL ;;
	2)   echo UNRESOLVED ;;
	4)   echo UNSUPPORTED ;;
	5)   echo UNTESTED ;;
	124|137) echo TIMEOUT ;;
	*)   echo ABNORMAL ;;
	esac
}

# The outcome of ATTEMPTS runs, collapsed by the rule documented in "WHY
# FLAKY IS A BUCKET AND NOT A RETRY": unanimous PASS is PASS, unanimous
# not-PASS is that outcome, anything else is FLAKY.  Symmetric on
# purpose -- there is no early exit on a first PASS, because "it passed
# once" and "it passes" are different claims and the whole bucket exists
# to keep them different.
run_one() { # run_one TEST-PATH -> prints the recorded outcome word
	_rf=$1; _npass=0; _last=""; _i=1
	while [ "$_i" -le "$ATTEMPTS" ]; do
		_o=$(attempt_one "$_rf" "$_i")
		[ "$_o" = PASS ] && _npass=$((_npass + 1)) || _last=$_o
		_i=$((_i + 1))
	done
	if [ "$_npass" -eq "$ATTEMPTS" ]; then echo PASS
	elif [ "$_npass" -eq 0 ];        then echo "$_last"
	else                                  echo FLAKY
	fi
}

echo "posix-optsrun: proving the runner works before trusting its silence ..." >&2
"$W/build-one.sh" "$CANARY_PASS" >/dev/null 2>&1
if [ ! -f "$W/exe/$(echo "$CANARY_PASS" | tr '/' '_').exe" ]; then
	echo "posix-optsrun: RUNNER REFUSED -- the PASS canary $CANARY_PASS does" >&2
	echo "posix-optsrun:   not even build.  Nothing below could have run, and" >&2
	echo "posix-optsrun:   a sweep that runs nothing reports no failures." >&2
	exit 2
fi
# One attempt, not ATTEMPTS: this is proving the INSTRUMENT works, not
# recording a verdict, and a Wine that cannot run one trivial test is
# not going to run it on the third try.
canary_probe=$(attempt_one "$CANARY_PASS" 0)
canary_log="$W/run/$(echo "$CANARY_PASS" | tr '/' '_').0.log"
if [ "$canary_probe" = PASS ] && ! grep -q "$CANARY_PASS_MARK" "$canary_log" 2>/dev/null; then
	echo "posix-optsrun: RUNNER REFUSED -- the PASS canary $CANARY_PASS exited" >&2
	echo "posix-optsrun:   0 under '$WINE' but never printed '$CANARY_PASS_MARK'." >&2
	echo "posix-optsrun:   version: ${WINE_VER:-(no answer from --version)}" >&2
	echo "posix-optsrun: A process that exits 0 having printed nothing did" >&2
	echo "posix-optsrun:   not run the test.  This is the exact shape of the" >&2
	echo "posix-optsrun:   dangerous failure: a runner that returns 0 for" >&2
	echo "posix-optsrun:   everything makes all $CENSUS_TESTS tests read as" >&2
	echo "posix-optsrun:   PASSING, which is good news shaped like a broken" >&2
	echo "posix-optsrun:   instrument.  Checking the exit status alone would" >&2
	echo "posix-optsrun:   not have caught it, which is why this check is" >&2
	echo "posix-optsrun:   here and not merely 'is wine on PATH'." >&2
	echo "posix-optsrun: What the canary actually wrote:" >&2
	sed 's/^/posix-optsrun:   | /' "$canary_log" >&2
	exit 2
fi
if [ "$canary_probe" != PASS ]; then
	echo "posix-optsrun: RUNNER REFUSED -- the PASS canary $CANARY_PASS came" >&2
	echo "posix-optsrun:   back $canary_probe under '$WINE'." >&2
	echo "posix-optsrun:   version: ${WINE_VER:-(no answer from --version)}" >&2
	echo "posix-optsrun: That test is two static strings and strlen; if it" >&2
	echo "posix-optsrun:   does not pass, the RUNNER is broken, not the" >&2
	echo "posix-optsrun:   library.  This is a refusal and not a skip: a Wine" >&2
	echo "posix-optsrun:   that starts and dies would satisfy a 'is wine on" >&2
	echo "posix-optsrun:   PATH' check and then yield zero executions, zero" >&2
	echo "posix-optsrun:   failures, and a green job.  The instrument is" >&2
	echo "posix-optsrun:   proven to work before its silence is trusted." >&2
	echo "posix-optsrun: Last output from the canary:" >&2
	sed 's/^/posix-optsrun:   | /' "$canary_log" >&2
	exit 2
fi
echo "posix-optsrun: runner proven: $CANARY_PASS PASSes under ${WINE_VER:-$WINE}." >&2

# ------------------------------------------------------------ build sweep

echo "posix-optsrun: building $n_tests conformance tests with $CC ..." >&2
( cd "$IFACES" && find . -name '*.c' | sed 's|^\./||' | sort ) \
	| xargs -P "$OPTSRUN_JOBS" -n1 "$W/build-one.sh" \
	| sort > "$W/linked.txt"
n_linked=$(wc -l < "$W/linked.txt" | tr -d ' ')

gap_C=$(read_gapmap_C)
check_link_census "$n_linked" "$gap_C" || exit 1

# -------------------------------------------------------------- run sweep
#
# Serial, and appending to the journal one line per test as the outcome
# is produced.  See "WHY SERIAL" in the header.
echo "posix-optsrun: running $n_linked test(s) under ${WINE_VER:-$WINE}, serially," >&2
echo "posix-optsrun:   ${TIMEOUT_SECS}s kill each ..." >&2
run_start=$(date +%s)
while read -r f; do
	printf '%s\t%s\n' "$(run_one "$f")" "$f" >> "$JOURNAL"
done < "$W/linked.txt"
run_end=$(date +%s)
run_secs=$((run_end - run_start))

# ------------------------------------------------------------- invariants

n_results=$(wc -l < "$JOURNAL" | tr -d ' ')
check_result_census "$n_results" "$n_linked" || exit 1

sort -t"$(printf '\t')" -k2,2 "$JOURNAL" > "$W/results.raw"

# Sticky FLAKY.  Applied to the RECORDED outcome only, never to the gate
# input, and only when regenerating -- --check must not be made lenient
# by anything except the documented wildcard.
if [ "$mode" = --generate ] && [ "$OPTSRUN_FRESH" != 1 ] && [ -f "$REPORT" ]; then
	recorded_block "$REPORT" > "$W/prev.tsv"
	awk -F'\t' '
		NR == FNR { if ($1 == "FLAKY") was[$2] = 1; next }
		{ if (($2 in was) && $1 != "FLAKY") { $1 = "FLAKY"; kept++ }
		  printf "%s\t%s\n", $1, $2 }
		END { if (kept) printf "posix-optsrun: kept %d test(s) FLAKY that this sweep saw agree with itself;\nposix-optsrun:   a lucky sweep does not clear a known flake (OPTSRUN_FRESH=1 does).\n", kept > "/dev/stderr" }
	' "$W/prev.tsv" "$W/results.raw" > "$W/results.sorted"
else
	cp "$W/results.raw" "$W/results.sorted"
fi
count_of() { awk -F'\t' -v k="$1" '$1==k' "$W/results.sorted" | wc -l | tr -d ' '; }
n_pass=$(count_of PASS)
n_fail=$(count_of FAIL)
n_unres=$(count_of UNRESOLVED)
n_unsup=$(count_of UNSUPPORTED)
n_untst=$(count_of UNTESTED)
n_tmout=$(count_of TIMEOUT)
n_abn=$(count_of ABNORMAL)
n_flaky=$(count_of FLAKY)
n_notpass=$((n_results - n_pass))

# The buckets must themselves partition the sweep.  An outcome word that
# is none of the seven would otherwise vanish from every count while
# still sitting in the journal.
n_sum=$((n_pass + n_fail + n_unres + n_unsup + n_untst + n_tmout + n_abn + n_flaky))
if [ "$n_sum" -ne "$n_results" ]; then
	echo "posix-optsrun: PARTITION FAILED -- the eight buckets hold $n_sum" >&2
	echo "posix-optsrun:   outcomes, the journal holds $n_results.  An outcome" >&2
	echo "posix-optsrun:   that falls out of classification is a bug here, and" >&2
	echo "posix-optsrun:   dropping it silently makes the sweep look cleaner." >&2
	exit 1
fi

check_flaky_ceiling "$n_flaky" || exit 1
check_floors "$n_pass" "$n_notpass" || exit 1

outcome_of() { awk -F'\t' -v f="$1" '$2==f{print $1; ok=1} END{if(!ok) print "MISSING"}' "$W/results.sorted"; }
check_canaries "$(outcome_of "$CANARY_PASS")" "$(outcome_of "$CANARY_NOTPASS")" || exit 1

# --------------------------------------------------------------- the report

# The machine-readable block inside the report, and the one --check
# compares.  Fenced rather than tabulated: 591 markdown table rows diff
# badly and this file exists for its diff.
outcome_block() { cat "$W/results.sorted"; }

# Per-directory ledger, sorted by "most not-PASS first, then by name" --
# the useful reading order, and stable under equal counts.
ledger() {
	awk -F'\t' '
		{ split($2, p, "/"); d = p[1]; tot[d]++; if ($1 == "PASS") ok[d]++ }
		END { for (d in tot) printf "%s\t%d\t%d\t%d\n", d, tot[d], ok[d]+0, tot[d]-(ok[d]+0) }
	' "$W/results.sorted" | sort -k4,4nr -k1,1
}

write_report() {
	cat <<EOF
<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Open POSIX Test Suite, executed

**Generated by \`tools/posix-optsrun.sh\` -- do not edit by hand.**

| | |
|---|---|
| ntlibc | \`$NTLIBC_SHA\` |
| LTP (\`third_party/ltp\`) | \`$LTP_SHA\` |
| suite | \`testcases/open_posix_testsuite/conformance/interfaces/\` |
| compiler | \`$CC\` |
| runner | \`${WINE_VER:-$WINE}\` |
| census | $n_linked of $n_tests tests link and were executed |
| per-test kill | ${TIMEOUT_SECS}s |
| runs per test | $ATTEMPTS for this report; $DEFAULT_ATTEMPTS in the gate |
| sweep wall clock | ${run_secs}s, serial |

\`test/POSIX-GAP-MAP.generated.md\` measures how much of this suite this
library can be **compiled** against, and executes nothing. This file is
the other half: it **runs** the $n_linked tests that gap map classes as
C -- compiles and links clean -- and records what each one did. Until
this existed, those tests had never been run and nobody knew whether
they passed.

It is **not** a pass/fail threshold, and adding one would reintroduce
exactly what the gap map's header argues against. There is no number
here that anything is compared to. What the gate enforces is
**regression**: a test moving \`PASS\` -> anything else. Movement inside
the not-\`PASS\` population -- \`FAIL\` -> \`ABNORMAL\`, \`UNTESTED\` ->
\`FAIL\` -- appears in the diff and does not turn the job red, because a
gate that fires on every kind of movement is as useless as one that
fires on none.

The \`runner\` row is load-bearing. These verdicts are what this library
does **under that Wine**, and a distribution that shifts because the
runner's Wine changed, with this tree untouched, would otherwise read as
a regression in us.

## Outcomes

The codes are the suite's own, not this project's:
\`Documentation/HOWTO_ResultCodes\` records them as "taken from IEEE Test
Methods for Measuring Conformance to POSIX (IEEE 1003.3-1991)", and
\`include/posixtest.h\` defines the values.

| outcome | tests | meaning |
|---|---|---|
| \`PASS\` | $n_pass | \`PTS_PASS\` (0). Executed fully, no problems, passed. |
| \`FAIL\` | $n_fail | \`PTS_FAIL\` (1). Executed fully, and failed. |
| \`UNRESOLVED\` | $n_unres | \`PTS_UNRESOLVED\` (2). Blocked from completing; pass/fail undeterminable. |
| \`UNSUPPORTED\` | $n_unsup | \`PTS_UNSUPPORTED\` (4). A conditional feature that is not implemented. |
| \`UNTESTED\` | $n_untst | \`PTS_UNTESTED\` (5). A stub, or the test could not reach its assertion. |
| \`TIMEOUT\` | $n_tmout | **Not a suite code.** The runner killed it at ${TIMEOUT_SECS}s. Never a pass. |
| \`FLAKY\` | $n_flaky | **Not a suite code.** Did not agree with itself across repeated runs. A not-\`PASS\` outcome: \`PASS\` -> \`FLAKY\` is a regression, because a test that used to pass every time and now passes half the time has got worse. |
| \`ABNORMAL\` | $n_abn | **Not a suite code.** Exited outside \`{0,1,2,4,5}\` -- e.g. \`sigaction/12-1x\` print \`Test FAILED\` and then \`exit(-1)\`, arriving as 255. Kept separate from \`FAIL\` because an \`exit(-1)\` and a \`PTS_FAIL\` are different claims about what the test knew. |

## Why every test runs more than once

The obvious way to handle a test that sometimes passes is to retry it
and keep the best answer. That is refused here. The first sweeps found
\`clock_nanosleep/1-1.c\` passing about half the time and printing
\`clock_nanosleep() did not sleep long enough\` on the other half.

That **intermittence** is a fact about this library that only execution
can produce. \`test/external-suites.md\`'s one-shot sweep saw the
deterministic half of this family -- 19 \`nanosleep\`/
\`clock_nanosleep\` tests returning before the interval is observable
on the clock, judged there to be the suite being stricter than POSIX,
which permits a \`_POSIX_CLOCKRES_MIN\` of 20 ms -- but one run cannot
see that a couple of them sit *on* the boundary rather than past it.
Whether that is a defect or the suite's strictness is a ledger
question. That it is not reproducible is this file's finding, and a
best-of-N gate would have deleted it at the point of measurement.

So every test runs several times, symmetrically: unanimous \`PASS\` is
\`PASS\`, unanimous not-\`PASS\` is that outcome, and anything else is
\`FLAKY\`. There is no early exit on a first \`PASS\` -- "it passed
once" and "it passes" are different claims.

The gate runs $DEFAULT_ATTEMPTS attempts, which is what the count in the
header table means when the two numbers differ: a report is sometimes
regenerated with more, to widen the sticky \`FLAKY\` set below, and the
extra attempts cost nothing on every push afterwards.

The residual hole, stated rather than hidden: a test whose pass
probability is near one half is not perfectly reproducible even as
\`FLAKY\`, so \`--check\` treats a test this report records as
\`FLAKY\` as a **staleness wildcard** -- it may come back with any
outcome without making the report stale. That population is therefore
capped at $FLAKY_CEILING by a pinned ceiling in the tool, so it cannot
grow without a commit that names the test and says why. It is not a
place to put things.

## The canaries

Named here so a red job is legible without reading the tool. They exist
to tell "the library changed" from "the measurement broke": real
movement shifts the population and leaves these two alone, whereas a
runner that has started answering constantly moves them too.

| canary | must be | why |
|---|---|---|
| \`$CANARY_PASS\` | \`PASS\` | Two static strings and \`strlen\`. If it stops passing, the **runner** is broken, not \`strlen\`. It is also what the pre-sweep refusal runs to prove Wine works at all -- and that refusal requires the canary to *print* \`$CANARY_PASS_MARK\`, not merely to exit 0, because a \`wine\` that is a stub exiting 0 exits 0 for every test in the suite. |
| \`$CANARY_NOTPASS\` | anything but \`PASS\` | Asks whether a handler runs on the alternate signal stack; this library has no \`sigaltstack\`. Chosen because it fails **structurally**: no \`fork\` (so it does not depend on Wine's missing \`RtlCloneUserProcess\`) and no timing assertion (so it does not depend on machine load). The obvious candidates -- \`raise/1-2.c\`, \`kill/1-2.c\` -- both fork, and a canary whose verdict depends on the runner cannot tell the runner from the library. |

## A note on fork

Wine does not implement \`RtlCloneUserProcess\`, so a test that forks
hangs rather than failing. That sounds like it should dominate this
sweep and it does not: $n_tmout of $n_results executed tests hit the
${TIMEOUT_SECS}s kill. The reason is structural -- nearly every
fork-dependent OPTS test also pulls \`pthread.h\`, \`mqueue.h\`,
\`sys/mman.h\` or \`semaphore.h\`, so it is already gap-map class A and
never reaches execution at all. **The fork problem is hidden behind the
header gap.** Relocating a "fork-dependent subset" to the real-Windows
\`windows-test\` leg, where \`fork\` works, would therefore be
infrastructure for a population of approximately zero, and is
deliberately not done.

Fork-dependent tests that DO reach execution are not silently excused:
they fail or come back \`ABNORMAL\` like anything else, and appear in
the ledger below. Nothing here is skipped for being fork-dependent,
because a baked-in exclusion would hide the day Wine grows
\`RtlCloneUserProcess\` -- the same reasoning tools/libc-test.sh gives
for detecting that condition at run time rather than from a static
list.

## Per-directory ledger

Interface directories with at least one test that does not pass, worst
first.

| interface | executed | \`PASS\` | not \`PASS\` |
|---|---|---|---|
EOF
	ledger | awk -F'\t' '$4 > 0 { printf "| `%s` | %d | %d | **%d** |\n", $1, $2, $3, $4 }'
	cat <<EOF

Interface directories in which every executed test passes:

EOF
	ledger | awk -F'\t' '$4 == 0 { printf "`%s` (%d) ", $1, $2 }' | fold -s -w 72
	cat <<EOF


## Per-test outcomes

Every executed test, one line each. This block is what
\`tools/posix-optsrun.sh --check\` compares, and it is the reason this
file is checked in: the diff is the signal.

\`\`\`
EOF
	outcome_block
	printf '```\n'
}

# ------------------------------------------------------------ generate/check

if [ "$mode" = --generate ]; then
	write_report > "$REPORT" || exit 1
	echo "posix-optsrun: wrote $REPORT" >&2
	echo "posix-optsrun: $n_pass PASS, $n_fail FAIL, $n_unres UNRESOLVED," >&2
	echo "posix-optsrun:   $n_unsup UNSUPPORTED, $n_untst UNTESTED," >&2
	echo "posix-optsrun:   $n_tmout TIMEOUT, $n_abn ABNORMAL, $n_flaky FLAKY" >&2
	echo "posix-optsrun:   of $n_results, each run $ATTEMPTS times." >&2
	exit 0
fi

# --check.
[ -f "$REPORT" ] || {
	echo "posix-optsrun: $REPORT does not exist." >&2
	echo "posix-optsrun: generate it: tools/posix-optsrun.sh" >&2
	exit 1; }

# The recorded SHA, and I5.
# shellcheck disable=SC2016  # the backticks are markdown, not a subshell
rec_sha=$(sed -n 's/^| ntlibc | `\([0-9a-f]*\)` |.*/\1/p' "$REPORT" | head -1)
rc=0
check_ancestry "$rec_sha" || rc=1

# Pull the checked-in per-test block out of the fenced section.  Extracted
# rather than diffing the whole file on purpose, and the exclusions are
# principled rather than convenient: the metadata block carries the
# ntlibc SHA (checked by I5 instead, exactly as posix-gapmap.sh does) and
# the Wine version and wall clock, neither of which is a claim about this
# tree.  Requiring those to match would make a developer's Wine and CI's
# disagree permanently -- the same defect the LC_ALL note at the top of
# this file describes for collation.
recorded_block "$REPORT" > "$W/recorded.tsv"
n_rec=$(wc -l < "$W/recorded.tsv" | tr -d ' ')
if [ "$n_rec" -eq 0 ]; then
	echo "posix-optsrun: the checked-in report has no per-test outcome block." >&2
	echo "posix-optsrun: Nothing could be compared, so nothing is claimed." >&2
	echo "posix-optsrun: Regenerate it: tools/posix-optsrun.sh" >&2
	exit 1
fi

regs=$(find_regressions "$W/recorded.tsv" "$W/results.sorted")
if [ -n "$regs" ]; then
	echo "posix-optsrun: REGRESSION -- $(printf '%s\n' "$regs" | wc -l | tr -d ' ') test(s) that the checked-in" >&2
	echo "posix-optsrun:   report records as PASS no longer pass:" >&2
	printf '%s\n' "$regs" | awk -F'\t' '{printf "posix-optsrun:   %s: %s -> %s\n", $1, $2, $3}' >&2
	echo "posix-optsrun: This is the gate.  It fires ONLY on PASS -> not-PASS;" >&2
	echo "posix-optsrun:   movement within the not-PASS population is a diff," >&2
	echo "posix-optsrun:   not a failure." >&2
	rc=1
fi

# The FLAKY wildcard, applied by masking BOTH sides for the tests the
# checked-in report records as FLAKY.  Written as a mask rather than as
# a filter so the masked tests remain in the compared streams -- a test
# that vanished entirely must still show up as a difference.
awk -F'\t' '
	NR == FNR { if ($1 == "FLAKY") wild[$2] = 1; next }
	{ if ($2 in wild) $1 = "FLAKY(wildcard)"; printf "%s\t%s\n", $1, $2 }
' "$W/recorded.tsv" "$W/recorded.tsv" > "$W/recorded.masked"
awk -F'\t' '
	NR == FNR { if ($1 == "FLAKY") wild[$2] = 1; next }
	{ if ($2 in wild) $1 = "FLAKY(wildcard)"; printf "%s\t%s\n", $1, $2 }
' "$W/recorded.tsv" "$W/results.sorted" > "$W/results.masked"

if ! diff -u "$W/recorded.masked" "$W/results.masked" > "$W/outcome.diff" 2>&1; then
	echo "posix-optsrun: $REPORT is STALE -- the sweep no longer matches it." >&2
	echo "posix-optsrun: (No regression above means the movement is within the" >&2
	echo "posix-optsrun:  not-PASS population, or tests started passing.  The" >&2
	echo "posix-optsrun:  report is still checked in and must still be current.)" >&2
	sed 's/^/posix-optsrun:   /' "$W/outcome.diff" >&2
	echo "posix-optsrun: Regenerate it: tools/posix-optsrun.sh" >&2
	rc=1
fi

if [ $rc -eq 0 ]; then
	echo "posix-optsrun: $n_results test(s) executed; $n_pass PASS, $n_notpass not." >&2
	echo "posix-optsrun: no regressions, report current, all invariants hold." >&2
fi
exit $rc
