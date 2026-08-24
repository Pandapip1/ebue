#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/libc-test-map.sh -- the coverage map for musl's libc-test.
#
# WHAT THIS IS, AND WHY IT IS NOT tools/libc-test.sh
#
# tools/libc-test.sh is a GATE: it adjudicates every test in the corpus
# against test/libc-test-expected.txt and answers yes/no.  Its summary
# line ends with a bare count -- "57 unbuildable" -- and that count is
# the single largest bucket it reports while saying nothing at all about
# what it costs us or why.  57 of 146 is 39% of the corpus, and the
# script's own header calls it "the gap accounting restated as a build
# error", which is exactly right and exactly unactionable in that form.
#
# This script is the other half: it takes those unbuildable tests apart
# and says WHICH gap, WHICH header, WHICH symbol, and -- the decision-
# useful question -- WHICH SINGLE ADDITION WOULD UNBLOCK THE MOST.
#
# It is deliberately NOT a gate stage in the pass/fail sense.  Its output
# is a distribution, not a verdict; making it a gate would force a
# threshold nobody can justify, and a threshold nobody can justify is the
# "number nobody reads" failure mode by another route.  What IS wired
# into tools/gate.sh is `--check`: a staleness-and-honesty check over the
# CHECKED-IN report, which is cheap and does have a yes/no answer.
#
# This mirrors, deliberately and section for section, the OPTS gap-map
# design in test/external-suites.md ("The gap map, weighted by
# conformance surface" / "Integration as a report, not a gate" / "How the
# report stays honest").  Two near-identical instruments with gratuitously
# different output shapes would be worse than either alone.
#
# THE TAXONOMY IS THE SUBSTANCE
#
# Three classes, and the middle one is the whole point:
#
#   A. header absent entirely.  The #include fails.  This is the HONEST
#      failure: we do not claim the interface, and a portable program
#      finds out at compile time, which is the earliest and cheapest
#      moment it could.
#   B. header present, interface missing.  The #include SUCCEEDS.  The
#      call falls through to an implicit declaration, or a macro is
#      undeclared, and the whole thing dies at LINK.  This is the worst
#      of the three for a downstream consumer -- it is the shape a
#      configure probe gets wrong -- and it is the class our function-
#      interface ledger is least able to see, because at interface
#      granularity those headers look present.
#   C. builds.
#
# Class B is itemised down to the symbol, and within it the report keeps
# apart two facts that the raw build log renders identical:
#
#     env  -- unresolved reference to 'clearenv'
#     wcsstr-false-negative -- unresolved reference to 'wcsstr'
#
# `wcsstr` was never implemented.  `clearenv` was DELETED, on purpose, in
# a commit this script names and verifies.  Those are different claims
# about the tree and a report that blurs them is worse than no report.
#
# THE DENOMINATOR IS PART OF THE FINDING
#
# tools/gate.sh runs functional + regression only: 146 files.  Upstream
# ships 425 test .c files.  A report that says "we run 62 of 146" while
# silently dropping src/math's 199 would be exactly the kind of
# true-but-misleading number this project keeps finding, so section 1 of
# the generated report states BOTH denominators and leads with the
# smaller fraction.
#
# Usage:
#   tools/libc-test-map.sh              regenerate test/LIBC-TEST-MAP.generated.md
#   tools/libc-test-map.sh --check      verify the checked-in report (the gate stage)
#   tools/libc-test-map.sh --print      write the report to stdout, touch nothing
#
# Env:
#   LIBC_TEST_MAP_GITREPO  the git repository to ask for SHAs and ancestry
#                        (default: the source tree).  tools/gate.sh runs
#                        this inside an rsync'd copy with no .git and
#                        points this back at the real tree.
#   LIBC_TEST_MAP_SUITE  alternate libc-test checkout to classify against
#                        (default: third_party/libc-test).  Exists so the
#                        census invariant can be demonstrated firing
#                        without touching the submodule, which must stay
#                        byte-identical to upstream.

set -u

# ------------------------------------------------------------ determinism
#
# Every `sort` below must order BYTES, not words.  glibc's UTF-8
# collations ignore punctuation at the first comparison level, so
# `sched_getparam` sorts BEFORE `sched_get_priority_max` under
# en_US.UTF-8 (compare `schedgetparam` with `schedgetprioritymax`) and
# AFTER it under C (`_` is 0x5F, `p` is 0x70).  In this corpus the same
# rule reorders `pthread-robust-detach` against
# `pthread_condattr_setclock`, `regexec-nosub` against `regex-*`, and
# `iconv-roundtrips` against `iconv_*`.
#
# This report is CHECKED IN, so that difference is not cosmetic: a
# developer's locale and CI's disagree permanently, --check goes red on a
# regeneration that changed nothing, and the diff -- the entire reason
# the file is checked in -- stops being readable.  tools/posix-gapmap.sh
# had exactly this bug and CI was red on it, and this file was written in
# parallel with it from the same design, so it shipped the same defect.
#
# Not a latent one, either: the report as committed was itself generated
# under a UTF-8 locale, so regenerating under C reordered eight rows of
# section 5's appendix.  Anyone running --check under C was already
# getting a red stage for a reason that had nothing to do with the gap.
#
# Set once and exported rather than sprinkled per `sort` invocation:
# pathname expansion (the `src/functional/*.c` glob that feeds the row
# order), awk string comparison (the greedy closure's tie-break), and
# `grep`/`tr` ranges are all locale-sensitive too, and one setting is one
# thing to reason about instead of an audit of every pipeline.
LC_ALL=C
export LC_ALL

# shellcheck disable=SC1007
srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$srcdir" || exit 1

SUITE="${LIBC_TEST_MAP_SUITE:-$srcdir/third_party/libc-test}"
# The git repository to ask about SHAs and ancestry.  Normally $srcdir --
# but tools/gate.sh runs this stage inside an rsync'd tree copy that has
# no .git at all, and points this back at the real tree.  It is NOT
# optional: see git_repo() below for why "no .git, so skip the check" is
# the one answer this script must never give.
GITREPO="${LIBC_TEST_MAP_GITREPO:-$srcdir}"
REPORT="$srcdir/test/LIBC-TEST-MAP.generated.md"
SHIM="$srcdir/test/libc-test-shim-src/libc-test-shim.c"

# ===================================================================
# THE PINS
# ===================================================================
#
# Every number below is a deliberate commit.  They exist because a gap
# report has one catastrophic failure mode that an ordinary test does
# not: IF THE MEASUREMENT SILENTLY STOPS WORKING, THE GAP APPEARS TO
# CLOSE.  A driver that mis-resolves the vendored path, or an -I that
# stops pointing at the suite, produces "0 blocked tests" -- which is
# indistinguishable from success and strictly more dangerous than a
# crash, because it is GOOD NEWS.
#
# Nine gate stages were deleted from this project in a single day for
# reporting success having checked nothing (test/verification-measures.md).
# These four invariants are what stop this file becoming the tenth.

# --- INVARIANT 1: CENSUS ------------------------------------------
# If the vendored tree moves, or a glob breaks, this fires BEFORE
# anything else reports a number.  Same discipline as
# verification-measures.md's M6: pin the check LIST, not just the tool.
CENSUS_FUNCTIONAL=77
CENSUS_REGRESSION=69
CENSUS=146

# Upstream's full test corpus, for the honest denominator in section 1.
# src/math is on disk and deliberately unadjudicated; src/api is
# compile-only declaration checking; src/musl is one file (pleval).
UPSTREAM_MATH=199
UPSTREAM_API=79
UPSTREAM_MUSL=1
UPSTREAM_TESTS=425

# --- INVARIANT 3: FLOORS, BOTH DIRECTIONS -------------------------
# FLOOR_BUILDS catches "the compiler stopped finding libc.a": everything
# blocked.  That direction is self-announcing -- it looks like a
# catastrophe -- and is the easy one.
#
# FLOOR_BLOCKED IS THE ONE THAT MATTERS.  It catches "the compiler
# stopped finding the suite", or an -I accidentally pointing at a host
# libc: everything links, the gap reads as ZERO, and the report
# congratulates us.  This is verification-measures.md's M1 applied in the
# direction that is easy to forget.
#
# FLOOR_BLOCKED moves DOWN only in a commit that also shows the header or
# symbol that closed the gap.  It is not a target and it is not the
# expected value: measured is 58, floored at 40, because a genuine
# pthread.h would legitimately move 23 at once and the floor must not
# make landing pthread.h fail the gate.
FLOOR_BUILDS=80
FLOOR_BLOCKED=40

# --- INVARIANT 4: TWO CANARIES ------------------------------------
# The specific check that separates "the gap closed" from "the
# measurement broke".  A closed gap moves the POPULATION; a broken
# measurement moves the CANARIES TOO.  One test known-blocked for a
# reason nobody is about to fix, one known-building; if both still agree
# with their pin, the classifier is discriminating rather than answering
# constantly.
CANARY_BLOCKED=pthread_cond-smasher       # class A on pthread.h
CANARY_BLOCKED_CLASS=A
CANARY_BUILDS=string                      # the core string test; class C
CANARY_BUILDS_CLASS=C

# --- DELIBERATE DELETIONS -----------------------------------------
# symbol|commit|what the commit did
#
# This table is what lets the report say "deleted on purpose" instead of
# "missing", and --check VERIFIES it rather than trusting it: the named
# commit must be an ancestor of HEAD and must actually contain a removal
# of that symbol.  A prose claim nobody checks is how a report starts
# lying; this one falsifies itself if the history is not what it says.
DELETED_SYMS='clearenv|49b8099|env: drop secure_getenv() and clearenv()'

# --- INTERFACE COUNTS, FOR THE DIVERGENCE CHECK -------------------
# header|absent-interfaces
#
# Lifted from test/POSIX-GAP-ACCOUNTING.md's "XSI vs base, restated"
# table (base-POSIX absences by header).  Pinned rather than scraped,
# and --check greps that file to confirm each pair is still what it
# says: a number quoted from another document is exactly the kind of
# claim that goes stale silently.
#
# This exists so section 3's central claim -- that ranking headers by
# CORPUS WEIGHT gives a materially different order than ranking them by
# INTERFACE COUNT -- is COMPUTED from two independent sources rather
# than asserted in prose.  That was the strongest argument in the OPTS
# work and it deserved to be checked here rather than assumed to repeat.
#
# The count is base-POSIX absences only, so it understates headers with
# large optional groups (pthread.h has 2 more XSI and a dozen CX/TSH
# rows beyond its 70).  Understating the big header makes the divergence
# claim HARDER to make, not easier, which is the right direction for a
# caveat to point.
INTERFACE_COUNTS='pthread.h|70
semaphore.h|10
langinfo.h|2
iconv.h|3
sys/mman.h|3
sys/msg.h|4
sys/shm.h|4
sys/sem.h|3'

# ===================================================================

die() { echo "libc-test-map: $*" >&2; exit 2; }

count_c() { find "$1" -maxdepth 1 -name '*.c' 2>/dev/null | wc -l | tr -d ' '; }

# A literal backtick, for building markdown inside sed scripts without
# either escaping games or a GNU-only \x60.
BQ='`'

g() { git -C "$GITREPO" "$@"; }

# Every SHA and every ancestry question goes through here, and a failure
# is fatal rather than a fallback.
#
# The tempting shape is `git rev-parse HEAD 2>/dev/null || echo unknown`,
# and it is wrong for the same reason everything else in this file is
# shaped the way it is.  This report's entire claim to being evidence
# about THIS tree rests on the SHA it records and on --check confirming
# that SHA is an ancestor of HEAD.  A run that cannot reach git cannot
# make that claim, and a report stamped "unknown" that still prints a
# gap distribution is worse than no report: it looks authoritative and
# is unfalsifiable.  So: hard error, naming the fix.
require_git() {
	g rev-parse --git-dir >/dev/null 2>&1 && return 0
	echo "libc-test-map: $GITREPO is not a git repository." >&2
	echo "libc-test-map: this report records the ntlibc SHA it was generated" >&2
	echo "libc-test-map: at, and --check verifies that SHA is an ancestor of" >&2
	echo "libc-test-map: HEAD.  Without git neither is possible, and a" >&2
	echo "libc-test-map: coverage map that cannot say which tree it describes" >&2
	echo "libc-test-map: is not evidence about any tree.  This is an error," >&2
	echo "libc-test-map: not a skip." >&2
	echo "libc-test-map:" >&2
	echo "libc-test-map: If you are running inside a copied tree (tools/gate.sh" >&2
	echo "libc-test-map: does this), point LIBC_TEST_MAP_GITREPO at the real one." >&2
	exit 2
}

# The submodule's own .git is a gitlink FILE, and rsync -a --exclude=.git
# strips it, so `git -C "$SUITE" rev-parse HEAD` fails inside a gate tree
# copy.  Ask the superproject for the gitlink instead -- which is the
# more authoritative answer anyway: it is the SHA this repository PINS,
# not merely the one that happens to be checked out.
suite_sha() {
	g rev-parse "HEAD:third_party/libc-test" 2>/dev/null && return 0
	git -C "$SUITE" rev-parse HEAD 2>/dev/null && return 0
	die "cannot determine the libc-test SHA from $GITREPO or $SUITE."
}

require_suite() {
	[ -f "$SUITE/COPYRIGHT" ] && [ -d "$SUITE/src/functional" ] &&
	[ -d "$SUITE/src/regression" ] && [ -d "$SUITE/src/common" ] && return 0
	echo "libc-test-map: $SUITE is empty or incomplete." >&2
	echo "libc-test-map: it is a git submodule.  Fix it with:" >&2
	echo "libc-test-map:     git submodule update --init --recursive" >&2
	echo "libc-test-map: This is an error, not a skip: a coverage map" >&2
	echo "libc-test-map: generated from an empty corpus would report a" >&2
	echo "libc-test-map: closed gap, which is the exact failure this" >&2
	echo "libc-test-map: instrument exists to make impossible." >&2
	exit 2
}

# ------------------------------------------------------------------
# classify -- build every test, then bucket it
# ------------------------------------------------------------------
#
# The build is byte-for-byte the one tools/libc-test.sh performs (same
# CFLAGS, same -I set, same four upstream helpers, same shim), because a
# map generated from a DIFFERENT build than the gate runs would describe
# a tree that does not exist.  If those two ever diverge the census and
# floors here will not notice -- but the `unbuildable` count in the gate
# summary and the A+B count here will disagree, which a human reading
# both will.
classify() {
	W=$1
	cfg() { sed -n "s/^$1 *= *//p" "$srcdir/config.mak" | tail -1; }
	[ -f "$srcdir/config.mak" ] || die "no config.mak; run ./configure first."
	[ -f "$srcdir/lib/libc.a" ] || die "lib/libc.a is missing; run make first."
	CC=$(cfg CC); ARCH=$(cfg ARCH)
	CFLAGS_C99FSE=$(cfg CFLAGS_C99FSE); CFLAGS_AUTO=$(cfg CFLAGS_AUTO)
	[ -n "$CC" ] || die "config.mak has no CC."

	INC="-I$srcdir/arch/$ARCH -I$srcdir/arch/generic -Iobj/include -I$srcdir/include -I$SUITE/src/common"
	mkdir -p "$W/build" "$W/obj"

	hobjs=""
	for h in print rand path memfill; do
		# shellcheck disable=SC2086
		$CC -c $CFLAGS_C99FSE $CFLAGS_AUTO -D_GNU_SOURCE $INC \
		    -o "$W/obj/$h.o" "$SUITE/src/common/$h.c" 2>"$W/$h.err" ||
			die "the shared harness helper $h.c does not compile; nothing can be classified."
		hobjs="$hobjs $W/obj/$h.o"
	done
	# shellcheck disable=SC2086
	$CC -c $CFLAGS_C99FSE $CFLAGS_AUTO -D_GNU_SOURCE $INC \
	    -o "$W/obj/shim.o" "$SHIM" 2>"$W/shim.err" ||
		die "test/libc-test-shim-src/libc-test-shim.c does not compile."
	hobjs="$hobjs $W/obj/shim.o"

	: > "$W/rows"
	for f in "$SUITE"/src/functional/*.c "$SUITE"/src/regression/*.c; do
		[ -f "$f" ] || continue
		n=$(basename "$f" .c)
		case "$f" in *"/src/functional/"*) corp=functional ;; *) corp=regression ;; esac
		# shellcheck disable=SC2086
		if $CC $CFLAGS_C99FSE $CFLAGS_AUTO -D_GNU_SOURCE $INC -nostdlib \
		    -o "$W/obj/$n.exe" "$srcdir/lib/crt1.o" "$f" $hobjs \
		    -L"$srcdir/lib" -lc -lntdll > "$W/build/$n.build" 2>&1; then
			printf '%s\t%s\tC\tbuilds\t\t\n' "$n" "$corp" >> "$W/rows"
			continue
		fi
		bucket_one "$n" "$corp" "$f" "$W" >> "$W/rows"
	done

	# Sorted by test name before anything else reads it.  The loop above
	# walks two globs -- functional, then regression -- and pathname
	# expansion is collation-ordered, so this file's line order was a
	# property of the caller's locale rather than of the data.  The
	# LC_ALL=C above already fixes that; sorting as well makes the order
	# a stated property of the file instead of an emergent one, so a
	# later reader does not have to know that a glob was involved.
	sort -o "$W/rows" "$W/rows"
}

# bucket_one NAME CORPUS SRC WORKDIR -> one tab-separated row:
#   name  corpus  class  subclass  key  detail
#
# Rule order matters and is the taxonomy itself.  A missing #include is
# checked FIRST because it short-circuits everything downstream: a test
# that dies at `#include <pthread.h>` never reaches the link, so whatever
# symbols it would also have wanted are unknowable and must not be
# guessed at.  That is why class A is keyed on a header and class B on a
# symbol -- they are not two views of one fact, they are two different
# facts observed at two different stages of the build.
bucket_one() {
	n=$1; corp=$2; src=$3; w=$4
	b="$w/build/$n.build"

	hdr=$(sed -n "s/.*include file '\([^']*\)' not found.*/\1/p" "$b" | head -1)
	if [ -n "$hdr" ]; then
		printf '%s\t%s\tA\theader-absent\t%s\t%s\n' "$n" "$corp" "$hdr" \
		       "#include <$hdr> does not resolve"
		return
	fi

	syms=$(sed -n "s/.*unresolved reference to '\([^']*\)'.*/\1/p" "$b" | sort -u | tr '\n' ' ')
	syms=${syms% }

	# --- corpus infrastructure, NOT a library gap ---------------
	#
	# This bucket is the reason the bare "57 unbuildable" overstates the
	# gap, and it is the finding that most changes how the number should
	# be read.  Upstream's own Makefile builds `*_dso.c` with -shared
	# (Makefile:135-136) -- they are shared objects with no main(), never
	# standalone programs.  tools/libc-test.sh builds every .c in the
	# corpus as a program, so their absent main() is reported as an
	# unbuildable test.  Nothing about this library is missing.  Same for
	# the companion-consuming half (tls_align wants `t` out of
	# tls_align_dso.so).
	case " $syms " in
	*" main "*)
		printf '%s\t%s\tB\tharness-dso\tmain\t%s\n' "$n" "$corp" \
		       "a shared object, not a program: upstream builds this with -shared (Makefile:135)"
		return ;;
	esac
	case " $syms " in
	*" __rpath "*)
		# Not a missing symbol either, and not upstream's fault: this
		# tree's dlopen() is delay-load based and, by documented design,
		# THE PROGRAM declares its own search path
		# (include/ntlibc/rpath.h: "__rpath ... is the one thing a
		# program using either mechanism still declares itself").  These
		# tests are upstream C that has never heard of that contract.  A
		# real divergence, but an ABI-contract one, not an absent
		# interface -- and filing it under "missing symbol" would put a
		# deliberate design decision in the same bucket as wcsstr.
		printf '%s\t%s\tB\tharness-rpath\t__rpath\t%s\n' "$n" "$corp" \
		       "this tree's dlopen requires the program to define __rpath (include/ntlibc/rpath.h)"
		return ;;
	esac
	if [ -n "$syms" ]; then
		for s in $syms; do
			if [ -f "$SUITE/src/functional/${n}_dso.c" ] &&
			   grep -q "\\b$s\\b" "$SUITE/src/functional/${n}_dso.c" 2>/dev/null; then
				printf '%s\t%s\tB\tharness-companion\t%s\t%s\n' "$n" "$corp" "$s" \
				       "provided by ${n}_dso.so, which this driver does not build"
				return
			fi
		done
	fi

	# --- a genuinely missing interface --------------------------
	if [ -n "$syms" ]; then
		first=${syms%% *}
		sub=$(symbol_subclass "$first")
		printf '%s\t%s\tB\t%s\t%s\t%s\n' "$n" "$corp" "$sub" "$syms" \
		       "$(symbol_detail "$first")"
		return
	fi

	# --- header present, MACRO absent ---------------------------
	# Invisible to a function-interface ledger by construction: it has no
	# row shape for a missing constant.
	mac=$(sed -n "s/.*error: '\([^']*\)' undeclared.*/\1/p" "$b" | head -1)
	if [ -n "$mac" ]; then
		printf '%s\t%s\tB\tmacro-absent\t%s\t%s\n' "$n" "$corp" "$mac" \
		       "header resolves; the constant does not"
		return
	fi

	# --- cannot interpose a libc symbol -------------------------
	dup=$(sed -n "s/.*link symbol '\([^']*\)' defined twice.*/\1/p" "$b" | head -1)
	if [ -n "$dup" ]; then
		printf '%s\t%s\tB\tno-interposition\t%s\t%s\n' "$n" "$corp" "$dup" \
		       "the test defines its own $dup; this library's archive does not allow overriding it"
		return
	fi

	err=$(grep -m1 'error' "$b" | sed 's/^ *//;s/\t/ /g')
	printf '%s\t%s\tB\tother\t-\t%s\n' "$n" "$corp" "${err:-no diagnostic}"
	unused=$src
	: "$unused"
}

# Is SYMBOL declared in a public header?  Comments are NOT stripped here
# on purpose -- the undefined-ok marker we are looking for lives IN a
# comment, and the two questions are asked together.
symbol_subclass() {
	s=$1
	case "$(deleted_line "$s")" in ?*) echo deleted-on-purpose; return ;; esac
	if grep -rqs "^[a-zA-Z_].*\\b${s}[ 	]*(" "$srcdir/include" "$srcdir/obj/include" 2>/dev/null; then
		if grep -rhs "\\b${s}[ 	]*(" "$srcdir/include" "$srcdir/obj/include" 2>/dev/null |
		   grep -q 'undefined-ok'; then
			echo declared-undefined-ok
		else
			# The single highest-signal line this report can print: a
			# prototype with no definition and no marker is a bug in
			# either the tree or tools/lint-undefined.sh's exceptions.
			echo declared-NOT-MARKED
		fi
	else
		echo never-implemented
	fi
}

symbol_detail() {
	s=$1
	d=$(deleted_line "$s")
	if [ -n "$d" ]; then
		echo "removed on purpose in $(echo "$d" | cut -d'|' -f2) -- $(echo "$d" | cut -d'|' -f3)"
		return
	fi
	case "$(symbol_subclass "$s")" in
	declared-undefined-ok)  echo "declared in a public header and marked undefined-ok: deliberately unimplemented" ;;
	declared-NOT-MARKED)    echo "declared in a public header, defined nowhere, NOT marked undefined-ok" ;;
	*)                      echo "never declared and never defined here" ;;
	esac
}

deleted_line() {
	printf '%s\n' "$DELETED_SYMS" | while IFS= read -r l; do
		[ -n "$l" ] || continue
		case "$l" in "$1|"*) echo "$l" ;; esac
	done
}

# ------------------------------------------------------------------
# absent-header set per blocked test, for the greedy closure
# ------------------------------------------------------------------
#
# First-error-only counts are biased by include order -- a test blocked
# on pthread.h might need semaphore.h too and the compiler never says so
# -- so the closure must parse each blocked test's FULL #include set,
# resolving the suite's own headers one level, and ask the decision-
# useful question instead: IF WE ADDED HEADER X, HOW MANY TESTS STOP
# BEING BLOCKED?
have_header() {
	for d in "$srcdir/include" "$srcdir/obj/include" \
	         "$srcdir/arch/$(sed -n 's/^ARCH *= *//p' "$srcdir/config.mak" | tail -1)" \
	         "$srcdir/arch/generic"; do
		[ -f "$d/$1" ] && return 0
	done
	return 1
}

absent_includes() {
	f=$1
	all=$(sed -n 's/^[ 	]*#[ 	]*include[ 	]*[<"]\([^>"]*\)[>"].*/\1/p' "$f")
	for h in $all; do
		[ -f "$SUITE/src/common/$h" ] &&
			all="$all $(sed -n 's/^[ 	]*#[ 	]*include[ 	]*[<"]\([^>"]*\)[>"].*/\1/p' "$SUITE/src/common/$h")"
	done
	# shellcheck disable=SC2086  # $all is a whitespace-separated list of
	# header names gathered from #include lines; splitting it is the point.
	for h in $(printf '%s\n' $all | sort -u); do
		[ -f "$SUITE/src/common/$h" ] && continue
		have_header "$h" || echo "$h"
	done
}

# ------------------------------------------------------------------
# emit the report
# ------------------------------------------------------------------
emit() {
	W=$1
	nt_sha=$(g rev-parse HEAD) || die "cannot determine the ntlibc SHA."
	lc_sha=$(suite_sha)

	nA=$(awk -F'\t' '$3=="A"' "$W/rows" | wc -l | tr -d ' ')
	nB=$(awk -F'\t' '$3=="B"' "$W/rows" | wc -l | tr -d ' ')
	nC=$(awk -F'\t' '$3=="C"' "$W/rows" | wc -l | tr -d ' ')
	nTot=$(wc -l < "$W/rows" | tr -d ' ')
	nBlocked=$((nA + nB))

	# per-blocked-test absent-header sets, for the closure
	: > "$W/needs"
	awk -F'\t' '$3=="A"{print $1}' "$W/rows" | while IFS= read -r n; do
		f="$SUITE/src/functional/$n.c"; [ -f "$f" ] || f="$SUITE/src/regression/$n.c"
		printf '%s\t%s\n' "$n" "$(absent_includes "$f" | sort -u | tr '\n' ' ')" >> "$W/needs"
	done

	cat <<EOF
<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later

GENERATED FILE -- do not edit by hand.
Regenerate with:  tools/libc-test-map.sh
Verified in the gate by:  tools/libc-test-map.sh --check
-->

# \`libc-test\` coverage map

| | |
|---|---|
| ntlibc | \`$nt_sha\` |
| libc-test | \`$lc_sha\` |
| generated by | \`tools/libc-test-map.sh\` |

**This file is checked in on purpose.** The value of a gap report is in
its **diff**: \`git diff test/LIBC-TEST-MAP.generated.md\` after landing
\`pthread.h\` would show 16 tests move out of *blocked* and 7 more
re-classify onto the next header they need, and that diff is
a better changelog entry than any prose. It also means the report cannot
rot silently — a stale checked-in file is visible in review; a CI
artifact nobody downloaded is not.

\`tools/libc-test.sh\` ends its summary with a bare count of unbuildable
tests. This file is that count taken apart.

## 1. Scope, and the honest denominator

\`tools/gate.sh\` runs **functional + regression only**. Two fractions
follow from that, and the smaller one is the true one:

| denominator | files | adjudicated | share |
|---|---|---|---|
| what the gate opted into (\`src/functional\` + \`src/regression\`) | $CENSUS | $CENSUS | 100% |
| **upstream's whole test corpus** | $UPSTREAM_TESTS | $CENSUS | **$(pct "$CENSUS" "$UPSTREAM_TESTS")** |

The gap between those two rows is almost entirely \`src/math\`:
**$UPSTREAM_MATH files**, on disk in the submodule, never built by
default. Also excluded: \`src/api\` ($UPSTREAM_API compile-only
declaration checks) and \`src/musl\` ($UPSTREAM_MUSL file, \`pleval\`).
By other measures \`src/math\` is a larger share still — 679 of the 933
files under \`src/\`, and 9.6 MB of 10.7 MB.

So the headline is not "we run 62 of 146". It is:

> **Of the $UPSTREAM_TESTS test files musl's \`libc-test\` ships, this
> tree adjudicates $CENSUS ($(pct "$CENSUS" "$UPSTREAM_TESTS")), builds $nC
> ($(pct "$nC" "$UPSTREAM_TESTS") of upstream), and gets a real behavioural
> verdict from fewer still.**

\`tools/libc-test.sh math\` exits 2 without \`LIBC_TEST_MATH=\`, which is
the right behaviour — a mode that quietly succeeded having run nothing
would be worse than one that is missing. But "correctly refuses to run"
is not "accounted for", and \`test/external-suites.md\` already records
three untriaged findings in there that nothing in this tree tracks:

- **\`fma()\` is not fused.** \`src/math/sanity/fma.h\` cases fail at
  0.5–0.8 ULP. A correct \`fma\` is exact — 0 ULP by definition — so any
  nonzero ULP is a categorical failure, not a tolerance question. This
  is the most concrete of the three.
- **Spurious \`FE_DIVBYZERO\`** from \`logb(±inf)\`, \`exp(inf)\`, and
  from \`pow\` on ordinary finite arguments. POSIX specifies no exception
  for any of them.
- **The \`isless\` family raises \`FE_INVALID\` on NaN**, specified not to.

None of those is visible anywhere in the gate. They are stated here so
that this file's denominator cannot be read as an all-clear for the 82
of 174 linkable math tests that fail.

## 2. The A/B/C split

| class | tests | share of $CENSUS | what it means |
|---|---|---|---|
| **A — header absent** | $nA | $(pct "$nA" "$CENSUS") | the \`#include\` fails. The honest failure: we do not claim the interface and a portable program finds out at compile time. |
| **B — header present, interface missing** | $nB | $(pct "$nB" "$CENSUS") | the \`#include\` **succeeds**, and it dies at link. The worst of the three for a consumer, and the one the function-interface ledger cannot see. |
| **C — builds** | $nC | $(pct "$nC" "$CENSUS") | reaches \`tools/libc-test.sh\`'s adjudication at all |

\`A + B + C = $nA + $nB + $nC = $nTot\`, and the census is $CENSUS.
(Invariant 2, below.)

## 3. Header levers — what to build next

Tests naming each absent header (a test may name several), and how many
would stop being blocked if that header **alone** arrived:

$(lever_table "$W")

Greedy closure — add headers best-first, and watch the residue:

\`\`\`
$(closure "$W")
\`\`\`

**Does the ordering diverge from an interface count?** This is the
question the OPTS work found most decision-useful, and it was worth
checking here rather than assuming it repeats. Over the eight absent
headers for which \`test/POSIX-GAP-ACCOUNTING.md\` publishes an absent-
interface count ("XSI vs base, restated"), the three rankings are:

$(divergence "$W")

**It diverges, and more sharply than the prose version of this argument
would have guessed.** \`pthread.h\` leads by corpus weight and by
interface count alike — that much any measure agrees on, and it is the
one place the two orders coincide. Everywhere else they come apart, and
the extreme case is \`pthread.h\` itself: **first by both raw measures,
sixth of eight by tests-per-interface.** 70 absent interfaces standing
in front of 23 tests is 0.33 tests per interface, against
\`langinfo.h\`'s 1.50 — \`nl_langinfo\` and \`nl_langinfo_l\`, two
declarations, blocking three tests. Ranked by density \`langinfo.h\`
beats \`pthread.h\` by a factor of 4.6, and \`semaphore.h\` beats it by
2.7.

By interface count \`semaphore.h\` (10) outranks \`langinfo.h\` (2) five
to one; by density \`langinfo.h\` wins. \`sys/msg.h\` and \`sys/shm.h\`
sit third and fourth on interface count and last on density. The two
rankings are not the same ranking, and the difference is not noise:
counting *declarations* weights a header by how much of it there is,
counting *tests* weights it by how much of the corpus stands behind it,
and only the second reflects what the suite actually exercises.

**And yet the honest conclusion is still "build \`pthread.h\`", which is
worth saying out loud.** Density is the right tiebreak between headers
of comparable reach; it is not a work list on its own. \`langinfo.h\` at
1.50 tests per interface unblocks **3 tests**. \`pthread.h\` at 0.33
unblocks **16**, and 23 stop naming an absent header. A report that
handed over the density column as a priority order would be
recommending two days of work for 3 tests over the one header that moves
39% of the blocked population.

**The divergence is also weaker here than in OPTS, and that is itself a
finding.** OPTS ranked \`aio.h\` above \`sys/mman.h\` by an order of
magnitude across 1610 tests. This corpus has 146, of which 41 are
header-blocked, and its long tail is **eleven headers blocking exactly
one test each** — at that granularity the difference between rank 7 and
rank 12 is one test, which is to say it is nothing. **\`pthread.h\` is
the only lever this corpus can rank with confidence**, and that reading
is not available from the closure or the interface count alone; it takes
both, disagreeing, to see that the rest of the table is under-powered.

**Caveat, stated because the number would otherwise be over-read.** A
header appearing does not mean those tests would then *pass*, or even
*link* — they would still need the functions behind it, and this suite
would then start reporting on their behaviour. The closure measures **how
much of the corpus becomes reachable**, which is the right question for
prioritising a header and the wrong one for predicting a pass rate.

## 4. Class B, itemised

The class the ledger cannot see. Every row here is a test whose
\`#include\` **succeeded**.

$(class_b_table "$W")

### 4a. Not a library gap at all

$(subclass_note "$W")

## 5. How this report stays honest

Four invariants, all checked by \`tools/libc-test-map.sh --check\`, any of
which failing is a hard error rather than a warning. They exist because
this instrument has a failure mode an ordinary test does not: **if the
measurement silently stops working, the gap appears to close**, and that
is good news, which is the most dangerous shape a bug can take.

| # | invariant | pinned | measured |
|---|---|---|---|
| 1 | **Census** — files discovered under \`src/functional\` + \`src/regression\` | $CENSUS_FUNCTIONAL + $CENSUS_REGRESSION = $CENSUS | $(count_c "$SUITE/src/functional") + $(count_c "$SUITE/src/regression") = $nTot |
| 2 | **Partition** — \`A + B + C\` must equal the census exactly | $CENSUS | $nA + $nB + $nC = $nTot |
| 3a | **Upper floor** — \`C\` (builds) ≥ pin. Catches "the compiler stopped finding \`libc.a\`" | ≥ $FLOOR_BUILDS | $nC |
| 3b | **Lower floor** — \`A + B\` (blocked) ≥ pin. **The one that matters**: catches an \`-I\` pointing at a host libc, which makes everything link and the gap read as *zero* | ≥ $FLOOR_BLOCKED | $nBlocked |
| 4a | **Canary, blocked** — \`$CANARY_BLOCKED\` must still classify $CANARY_BLOCKED_CLASS | $CANARY_BLOCKED_CLASS | $(awk -F'\t' -v n="$CANARY_BLOCKED" '$1==n{print $3}' "$W/rows") |
| 4b | **Canary, building** — \`$CANARY_BUILDS\` must still classify $CANARY_BUILDS_CLASS | $CANARY_BUILDS_CLASS | $(awk -F'\t' -v n="$CANARY_BUILDS" '$1==n{print $3}' "$W/rows") |

Invariant 3b's pin moves **down** only in a commit that also shows the
header or symbol that closed the gap.

**Why the lower floor is on \`A + B\` and not on \`A\` alone.** Measured,
not assumed: adding \`-I/usr/include\` to the classifier — the exact
accident invariant 3b exists to catch — moves the split from
\`A=41 B=17\` to \`A=36 B=22\`. The host's \`pthread.h\`, \`semaphore.h\`
and friends resolve, five tests stop failing at \`#include\` and start
failing at *link*, and **\`A + B\` does not move at all: 58 either way.**
A floor on \`A\` alone would have fired on a harmless reclassification
while still missing the case where everything genuinely links. The
partition is what makes the floor mean something.

**Each of these has been demonstrated firing**, by breaking the
condition and watching the check go red — not merely reasoned about,
because a guard nobody has seen fail is a guard nobody knows works:

| # | how it was broken | what it said |
|---|---|---|
| 1 | one \`.c\` removed from a copy of the corpus | \`pinned 77 + 69, found 76 + 69\` |
| 2 | one classifier bucket made to return without emitting its row | \`A=41 B=16 C=88 sum=145, census=146\` |
| 3a | the build step forced to fail for every test | \`only 0 tests build, floor is 80\` |
| 3b | the driver made to stop registering build failures — the gap reads as **closed** | \`only 0 tests blocked, floor is 40\` |
| 4 | a canary re-pinned to the wrong class, population untouched | \`pthread_cond-smasher is 'A' (want C)\` |
| — | deletion pin pointed at a commit that does not remove \`clearenv\` | \`eb5a607 does not remove any line mentioning 'clearenv'\` |
| — | recorded SHA replaced with a commit off this history | \`not an ancestor of HEAD\` |

The 3b run is the one worth reading twice: **invariant 4 fired at the
same time**, because a driver that has stopped seeing build failures
classifies \`pthread_cond-smasher\` as \`C\`. That is the design working.
Note also that the *building* canary still agreed in that run — \`string\`
was \`C\`, as pinned — which is why there are two canaries pointing in
opposite directions and not one.

Invariant 4 is the specific check that distinguishes *"the gap closed"*
from *"the measurement broke"*: **a closed gap moves the population; a
broken measurement moves the canaries too.** Two canaries rather than
one, in opposite directions, because a classifier that has started
answering constantly — always \`A\`, or always \`C\` — satisfies either one
alone.

And one process invariant tooling cannot enforce on its own: the ntlibc
SHA recorded above must be an **ancestor of \`HEAD\`**, which \`--check\`
verifies. That is what stops this file being quietly months old while
looking authoritative.

The \`deleted-on-purpose\` annotations in section 4 are verified the same
way, and for the same reason: \`--check\` confirms each named commit is an
ancestor of \`HEAD\` **and** that it actually removed that symbol. A prose
claim nobody checks is how a report starts lying.

## 6. Per-test ledger

All $CENSUS tests, so that a regeneration diffs meaningfully. This
section goes last because nobody reads it directly; it is the raw
material that makes the rest reviewable.

| test | corpus | class | sub-class | key | detail |
|---|---|---|---|---|---|
$(sort -t"$(printf '\t')" -k3,3 -k4,4 -k1,1 "$W/rows" |
  awk -F'\t' '{printf "| `%s` | %s | %s | %s | `%s` | %s |\n", $1,$2,$3,$4,($5==""?"-":$5),$6}')
EOF
}

pct() { awk -v a="$1" -v b="$2" 'BEGIN{printf "%.1f%%", (b?a*100/b:0)}'; }

# tests naming each absent header, and tests it alone would unblock
lever_table() {
	w=$1
	echo "| header | tests naming it | unblocked by it **alone** | absent interfaces | tests per interface |"
	echo "|---|---|---|---|---|"
	awk -F'\t' '{for(i=2;i<=NF;i++) if($i!="") print $i}' "$w/needs" |
		tr ' ' '\n' | grep -v '^$' | sort -u | while IFS= read -r h; do
		naming=$(awk -F'\t' -v h=" $h " '{if(index(" "$2" ",h)) c++} END{print c+0}' "$w/needs")
		alone=$(awk -F'\t' -v h="$h" '{
			n=split($2,a," "); only=0
			for(i=1;i<=n;i++) if(a[i]!="") { if(a[i]==h) only++; else {only=-99} }
			if(only==1) c++
		} END{print c+0}' "$w/needs")
		printf '%s\t%s\t%s\n' "$naming" "$alone" "$h"
	done | sort -rn | while IFS="$(printf '\t')" read -r naming alone h; do
		ifc=$(iface_count "$h")
		if [ -n "$ifc" ]; then
			tpi=$(awk -v a="$naming" -v b="$ifc" 'BEGIN{printf "%.2f", a/b}')
			# shellcheck disable=SC2016  # markdown backticks, not a subshell
			printf '| `%s` | %s | %s | %s | **%s** |\n' "$h" "$naming" "$alone" "$ifc" "$tpi"
		else
			# shellcheck disable=SC2016  # markdown backticks, not a subshell
			printf '| `%s` | %s | %s | — | — |\n' "$h" "$naming" "$alone"
		fi
	done
}

iface_count() {
	printf '%s\n' "$INTERFACE_COUNTS" | while IFS='|' read -r h n; do
		[ "$h" = "$1" ] && echo "$n"
	done
}

# The divergence, computed: the corpus-weight order against the
# interface-count order, over the headers where both are known.
divergence() {
	w=$1
	printf '%s\n' "$INTERFACE_COUNTS" | while IFS='|' read -r h n; do
		[ -n "$h" ] || continue
		naming=$(awk -F'\t' -v hh=" $h " '{if(index(" "$2" ",hh)) c++} END{print c+0}' "$w/needs")
		[ "$naming" -gt 0 ] || continue
		awk -v h="$h" -v n="$n" -v t="$naming" 'BEGIN{printf "%s\t%s\t%s\t%.3f\n", h, t, n, t/n}'
	done > "$w/div"
	echo "| rank | by corpus weight (tests naming it) | by interface count (absences) | by tests per interface |"
	echo "|---|---|---|---|"
	sort -t"$(printf '\t')" -k2,2nr "$w/div" | awk -F'\t' '{print $1" ("$2")"}' > "$w/o1"
	sort -t"$(printf '\t')" -k3,3nr "$w/div" | awk -F'\t' '{print $1" ("$3")"}' > "$w/o2"
	sort -t"$(printf '\t')" -k4,4nr "$w/div" | awk -F'\t' '{print $1" ("$4")"}' > "$w/o3"
	i=1
	while IFS= read -r a; do
		b=$(sed -n "${i}p" "$w/o2"); c=$(sed -n "${i}p" "$w/o3")
		# shellcheck disable=SC2016  # markdown backticks, not a subshell
		printf '| %s | `%s` | `%s` | `%s` |\n' "$i" "$a" "$b" "$c"
		i=$((i + 1))
	done < "$w/o1"
}

closure() {
	w=$1
	cp "$w/needs" "$w/cl"
	total=$(wc -l < "$w/cl" | tr -d ' ')
	printf 'start                        blocked on an absent header: %3d\n' "$total"
	while :; do
		best=$(awk -F'\t' '{n=split($2,a," "); for(i=1;i<=n;i++) if(a[i]!="") cnt[a[i]]++}
			END{m=0; for(h in cnt) if(cnt[h]>m||(cnt[h]==m&&h<b)){m=cnt[h];b=h} if(m)print b}' "$w/cl")
		[ -n "$best" ] || break
		gone=$(awk -F'\t' -v h="$best" '{
			n=split($2,a," "); rest=0
			for(i=1;i<=n;i++) if(a[i]!=""&&a[i]!=h) rest++
			if(rest==0) c++
		} END{print c+0}' "$w/cl")
		awk -F'\t' -v h="$best" 'BEGIN{OFS="\t"}{
			n=split($2,a," "); s=""
			for(i=1;i<=n;i++) if(a[i]!=""&&a[i]!=h) s=s a[i] " "
			if(s!="") print $1,s
		}' "$w/cl" > "$w/cl2"; mv "$w/cl2" "$w/cl"
		total=$(wc -l < "$w/cl" | tr -d ' ')
		printf '+%-26s unblocks %3d    still blocked: %3d\n' "$best" "$gone" "$total"
	done
}

class_b_table() {
	w=$1
	echo "| sub-class | tests | symbol / key | what it is |"
	echo "|---|---|---|---|"
	awk -F'\t' '$3=="B"{print $4}' "$w/rows" | sort -u | while IFS= read -r sc; do
		cnt=$(awk -F'\t' -v s="$sc" '$3=="B"&&$4==s' "$w/rows" | wc -l | tr -d ' ')
		keys=$(awk -F'\t' -v s="$sc" '$3=="B"&&$4==s{print $5}' "$w/rows" | tr ' ' '\n' |
		       grep -v '^$' | sort -u | sed "s/^/$BQ/;s/\$/$BQ/" | tr '\n' ' ')
		# ONE representative detail for a sub-class whose rows may each
		# carry their own.  The rule has to be total and stated, because
		# a reviewer looking at the row cannot reconstruct why that
		# wording won.  It used to be "whichever row came first in
		# $W/rows", which is an arbitrary element of a set: that file's
		# order was the order two `*.c` globs expanded in, i.e. a
		# property of the caller's collation, so `harness-companion`
		# ("provided by tls_align_dso.so") and `other` (the raw first
		# diagnostic) could each change wording between two runs of an
		# unchanged tree.
		#
		# The rule now: the detail belonging to the sub-class's
		# C-first test NAME.  Sorting on the name rather than on the
		# detail keeps the column tied to an identifiable row.
		det=$(awk -F'\t' -v s="$sc" '$3=="B"&&$4==s{printf "%s\t%s\n", $1, $6}' "$w/rows" |
		      sort | head -1 | cut -f2-)
		printf '| **%s** | %s | %s | %s |\n' "$sc" "$cnt" "$keys" "$det"
	done
	echo
	echo "Per test:"
	echo
	echo "| test | sub-class | symbol(s) |"
	echo "|---|---|---|"
	awk -F'\t' '$3=="B"{printf "| `%s` | %s | `%s` |\n", $1,$4,($5==""?"-":$5)}' "$w/rows" | sort
}

subclass_note() {
	w=$1
	inf=$(awk -F'\t' '$3=="B"&&($4=="harness-dso"||$4=="harness-rpath"||$4=="harness-companion")' "$w/rows" | wc -l | tr -d ' ')
	nB=$(awk -F'\t' '$3=="B"' "$w/rows" | wc -l | tr -d ' ')
	cat <<EOT
**$inf of the $nB class-B tests are not evidence of a missing
interface**, and this is the single most important correction the map
makes to the gate's bare count:

- **\`harness-dso\` / \`harness-companion\`** — \`*_dso.c\` files are
  *shared objects*. Upstream's own \`Makefile:135-136\` builds them with
  \`-shared\`; they have no \`main()\` and were never standalone programs.
  \`tools/libc-test.sh\` compiles every \`.c\` in the corpus as a program,
  so their absent \`main()\` surfaces as an unbuildable *test*. Nothing
  about this library is missing. \`tls_align\` is the consuming half:
  it wants \`t\` out of \`tls_align_dso.so\`.
- **\`harness-rpath\`** — a real divergence, but an **ABI-contract** one
  rather than an absent interface. This tree's \`dlopen()\` is delay-load
  based and, by documented design, the *program* declares its own search
  path: \`include/ntlibc/rpath.h\` says \`__rpath\` "is the one thing a
  program using either mechanism still declares itself". Upstream's C has
  never heard of that contract. Filing it under "missing symbol" would
  put a deliberate design decision in the same bucket as \`wcsstr\`.

Netting those out, the genuine interface gap behind the gate's
unbuildable count is **$((nB - inf)) class-B tests**, not $nB — and
that correction is invisible in the gate's summary line.
EOT
}

# ------------------------------------------------------------------
# invariants
# ------------------------------------------------------------------
check_invariants() {
	W=$1; rc=0

	f=$(count_c "$SUITE/src/functional")
	r=$(count_c "$SUITE/src/regression")
	if [ "$f" -ne "$CENSUS_FUNCTIONAL" ] || [ "$r" -ne "$CENSUS_REGRESSION" ]; then
		echo "INVARIANT 1 (census) FAILED: pinned $CENSUS_FUNCTIONAL functional + $CENSUS_REGRESSION regression, found $f + $r." >&2
		echo "  the corpus moved under the map.  Every number in this report is" >&2
		echo "  computed against a denominator that is no longer true.  Bumping" >&2
		echo "  the pin is a deliberate commit, reviewed alongside the submodule SHA." >&2
		rc=1
	else
		echo "INVARIANT 1 (census) ok: $f + $r = $CENSUS"
	fi

	nA=$(awk -F'\t' '$3=="A"' "$W/rows" | wc -l | tr -d ' ')
	nB=$(awk -F'\t' '$3=="B"' "$W/rows" | wc -l | tr -d ' ')
	nC=$(awk -F'\t' '$3=="C"' "$W/rows" | wc -l | tr -d ' ')
	nT=$(wc -l < "$W/rows" | tr -d ' ')
	if [ $((nA + nB + nC)) -ne "$CENSUS" ] || [ "$nT" -ne "$CENSUS" ]; then
		echo "INVARIANT 2 (partition) FAILED: A=$nA B=$nB C=$nC sum=$((nA+nB+nC)), rows=$nT, census=$CENSUS." >&2
		echo "  a test fell out of classification.  That is a bug in the" >&2
		echo "  classifier, not an absence in the library -- and silently" >&2
		echo "  dropping it SHRINKS THE REPORTED GAP." >&2
		rc=1
	else
		echo "INVARIANT 2 (partition) ok: $nA + $nB + $nC = $CENSUS"
	fi

	if [ "$nC" -lt "$FLOOR_BUILDS" ]; then
		echo "INVARIANT 3a (upper floor) FAILED: only $nC tests build, floor is $FLOOR_BUILDS." >&2
		echo "  the toolchain or lib/libc.a has fallen over." >&2
		rc=1
	else
		echo "INVARIANT 3a (upper floor) ok: $nC builds >= $FLOOR_BUILDS"
	fi
	if [ $((nA + nB)) -lt "$FLOOR_BLOCKED" ]; then
		echo "INVARIANT 3b (LOWER floor) FAILED: only $((nA+nB)) tests blocked, floor is $FLOOR_BLOCKED." >&2
		echo "  THIS IS THE DANGEROUS DIRECTION.  A gap that suddenly reads as" >&2
		echo "  nearly closed is far more likely to be an -I pointing at a host" >&2
		echo "  libc -- everything resolves, everything links, and the report" >&2
		echo "  congratulates us -- than 40 interfaces landing at once.  If the" >&2
		echo "  gap really did close, lower this pin in the commit that shows" >&2
		echo "  the header or symbol that closed it." >&2
		rc=1
	else
		echo "INVARIANT 3b (lower floor) ok: $((nA+nB)) blocked >= $FLOOR_BLOCKED"
	fi

	cb=$(awk -F'\t' -v n="$CANARY_BLOCKED" '$1==n{print $3}' "$W/rows")
	cc=$(awk -F'\t' -v n="$CANARY_BUILDS" '$1==n{print $3}' "$W/rows")
	if [ "$cb" != "$CANARY_BLOCKED_CLASS" ] || [ "$cc" != "$CANARY_BUILDS_CLASS" ]; then
		echo "INVARIANT 4 (canaries) FAILED: $CANARY_BLOCKED is '${cb:-ABSENT}' (want $CANARY_BLOCKED_CLASS); $CANARY_BUILDS is '${cc:-ABSENT}' (want $CANARY_BUILDS_CLASS)." >&2
		echo "  a closed gap moves the POPULATION; a broken measurement moves" >&2
		echo "  the CANARIES TOO.  Do not adjust these pins to make this pass" >&2
		echo "  without first establishing which of the two happened." >&2
		rc=1
	else
		echo "INVARIANT 4 (canaries) ok: $CANARY_BLOCKED=$cb, $CANARY_BUILDS=$cc"
	fi
	return $rc
}

check_deletions() {
	rc=0
	printf '%s\n' "$DELETED_SYMS" | while IFS='|' read -r s c _; do
		[ -n "$s" ] || continue
		if ! g merge-base --is-ancestor "$c" HEAD 2>/dev/null; then
			echo "DELETION PIN FAILED: $c (claimed to have removed '$s') is not an ancestor of HEAD." >&2
			exit 1
		fi
		if ! g show "$c" 2>/dev/null | grep -q "^-.*\\b$s\\b"; then
			echo "DELETION PIN FAILED: $c does not remove any line mentioning '$s'." >&2
			echo "  the report claims this symbol was deleted on purpose.  If the" >&2
			echo "  history does not say so, the claim is wrong and the row" >&2
			echo "  belongs in 'never-implemented' instead." >&2
			exit 1
		fi
		echo "deletion pin ok: '$s' removed in $c, an ancestor of HEAD"
	done || rc=1
	return $rc
}

# The interface counts in section 3 are quoted from another document in
# this tree, which is exactly the kind of claim that goes stale in
# silence: POSIX-GAP-ACCOUNTING.md gets edited, this file keeps citing
# the old number, and the divergence argument is then built on a figure
# nobody re-read.  Verify each pair is still there.
check_iface_counts() {
	acct="$srcdir/test/POSIX-GAP-ACCOUNTING.md"
	[ -f "$acct" ] || { echo "INTERFACE-COUNT PIN FAILED: $acct is missing." >&2; return 1; }
	bad=0
	printf '%s\n' "$INTERFACE_COUNTS" | while IFS='|' read -r h n; do
		[ -n "$h" ] || continue
		# Two shapes, because the accounting document states base-POSIX
		# absences in a table (`h` | n |) and XSI ones in prose (`h` (n)).
		if ! grep -qF "\`$h\` | $n " "$acct" && ! grep -qF "\`$h\` ($n)" "$acct"; then
			echo "INTERFACE-COUNT PIN FAILED: POSIX-GAP-ACCOUNTING.md no longer says '$h' has $n absent interfaces." >&2
			echo "  section 3's divergence argument is computed from that number." >&2
			echo "  Re-read the table there and update the pin, or the argument is" >&2
			echo "  being made from a figure that document has since revised." >&2
			exit 1
		fi
	done || bad=1
	[ "$bad" -eq 0 ] && echo "interface-count pins ok: $(printf '%s\n' "$INTERFACE_COUNTS" | grep -c .) headers still match POSIX-GAP-ACCOUNTING.md"
	return $bad
}

check_sha() {
	[ -f "$REPORT" ] || { echo "--check: $REPORT is missing.  Run tools/libc-test-map.sh." >&2; return 1; }
	# shellcheck disable=SC2016  # markdown backticks in a sed pattern
	sha=$(sed -n 's/^| ntlibc | `\([0-9a-f]*\)` |$/\1/p' "$REPORT" | head -1)
	[ -n "$sha" ] || { echo "--check: $REPORT records no ntlibc SHA." >&2; return 1; }
	if ! g merge-base --is-ancestor "$sha" HEAD 2>/dev/null; then
		echo "--check FAILED: the report was generated at $sha, which is not an" >&2
		echo "  ancestor of HEAD.  It describes a tree this one did not come from," >&2
		echo "  and is therefore not evidence about this one.  Regenerate it." >&2
		return 1
	fi
	echo "sha ok: report generated at $sha, an ancestor of HEAD"
	return 0
}

# ------------------------------------------------------------------
main() {
	mode=${1:---generate}
	case "$mode" in
	--generate|--print|--check) ;;
	*) sed -n '2,90p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
	esac

	require_suite
	require_git
	W=$(mktemp -d "${TMPDIR:-/tmp}/ntlibc-lctmap.XXXXXX") || exit 1
	trap 'rm -rf "$W"' EXIT

	classify "$W"

	rc=0
	check_invariants "$W" || rc=1
	check_deletions || rc=1
	check_iface_counts || rc=1

	case "$mode" in
	--print)
		[ "$rc" -eq 0 ] || exit 1
		emit "$W" ;;
	--generate)
		[ "$rc" -eq 0 ] || { echo "libc-test-map: refusing to write a report whose own invariants fail." >&2; exit 1; }
		emit "$W" > "$REPORT.tmp" && mv "$REPORT.tmp" "$REPORT"
		echo "libc-test-map: wrote $REPORT" ;;
	--check)
		check_sha || rc=1
		if [ "$rc" -eq 0 ]; then
			emit "$W" > "$W/fresh.md"
			# The recorded SHA legitimately trails HEAD between
			# regenerations, so compare everything EXCEPT that row --
			# otherwise every commit would make this stage red and the
			# stage would be turned off, which is the failure mode this
			# whole file is about.
			grep -v '^| ntlibc | `' "$REPORT" > "$W/have.md"
			grep -v '^| ntlibc | `' "$W/fresh.md" > "$W/want.md"
			if ! diff -u "$W/have.md" "$W/want.md" > "$W/diff"; then
				echo "--check FAILED: $REPORT is stale." >&2
				echo "  the coverage map no longer describes this tree.  Regenerate:" >&2
				echo "      tools/libc-test-map.sh" >&2
				echo >&2
				sed 's/^/    /' "$W/diff" >&2
				rc=1
			else
				echo "--check ok: $REPORT is current."
			fi
		fi ;;
	esac
	exit $rc
}

main "$@"
