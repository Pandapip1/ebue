# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/suitemap-engine.sh -- the common half of the two gap maps.
#
# NOT EXECUTABLE, AND NOT A TOOL.  It is sourced by a backend, which is
# the executable the world calls:
#
#   tools/posix-gapmap.sh   -- LTP's Open POSIX Test Suite  (1610 tests)
#   tools/libc-test-map.sh  -- musl's libc-test             (146 tests)
#
# WHY THIS FILE EXISTS
#
# The two backends were written in parallel from one design, and for a
# while they were two near-identical ~1000-line scripts.  That is not a
# duplication complaint, it is a correctness one: every property either
# tool establishes has to be established AGAIN in the other, and the
# second time is where it silently is not.  Three found instances, all
# of them real:
#
#   - LC_ALL.  posix-gapmap's sorts ordered words rather than bytes; CI
#     was red on it and c2ac057 fixed it there.  libc-test-map had the
#     identical defect and nobody looked: the report as committed had
#     been generated under a UTF-8 locale, so anyone running --check
#     under C got a red stage for a reason unrelated to the gap.
#
#   - The refuse-to-measure guard.  posix-gapmap refuses to run without
#     lib/libc.a and says why -- without it every test fails to link and
#     the report reads as a total gap, which is a build error and not a
#     measurement.  libc-test-map had the same test buried inside
#     classify() as a bare one-line `die` with no reasoning, where a
#     future edit could reorder past it without noticing what it was for.
#
#   - The greedy closure.  They were two DIFFERENT algorithms wearing the
#     same section heading.  posix-gapmap picks the header that fully
#     unblocks the most tests, recomputed at every step.  libc-test-map
#     picked the most-NAMED header, which is not the same thing: on
#     `t1={a,b} t2={a,c} t3={d} t4={d}` it recommends `a.h` first, and
#     a.h unblocks nothing at all.  It also drained its residue to zero,
#     implying a gap that can always be closed one header at a time.
#     They happen to agree on both corpora today; they agree by accident
#     of the data, not by construction.
#
# So the rule this file exists to enforce: anything that decides what a
# number MEANS lives here, exactly once.  A backend describes its suite
# and nothing else.
#
# WHAT A BACKEND SUPPLIES, AND WHAT IT MAY NOT
#
# Before sourcing this file a backend sets:
#
#   SM_TOOL        the name in every diagnostic, e.g. `posix-gapmap`
#   SM_REPORT      absolute path of the generated report it owns
#   SM_ROW_TAGS    the data-block row tags it uses, as an sed/awk
#                  bracket expression, e.g. '[stdn]'
#
# and defines its own enumeration, its own per-test classification, and
# its own report body.  Those three are genuinely per-suite: LTP's
# open_posix_testsuite and musl's libc-test have different layouts,
# different build recipes and different notions of a blocked test.
#
# What a backend may NOT do is compile anything itself.  Every compile
# goes through sm_cc, and sm_cc refuses to run until sm_require_built has
# passed.  That is the point: the guard is not merely present in both
# backends, it is unreachable around.  See sm_require_built.

# ---------------------------------------------------------- determinism
#
# Every `sort`, every `grep -x`, every awk string comparison and every
# glob in this tool and in both backends orders BYTES, not words.
#
# glibc's UTF-8 collations ignore punctuation at the first comparison
# level, so `sched_getparam` sorts BEFORE `sched_get_priority_max` under
# en_US.UTF-8 (compare `schedgetparam` with `schedgetprioritymax`) and
# AFTER it under C (`_` is 0x5F, `p` is 0x70).
#
# Both reports are CHECKED IN and both are verified by diffing a fresh
# render against the committed file, so this is not cosmetic: a
# developer's locale and CI's disagree permanently, --check goes red on a
# regeneration that changed nothing, and the diff -- the entire reason
# the files are checked in -- stops being readable.  It has happened to
# both tools.
#
# Set once, here, and exported, rather than per-`sort`: pathname
# expansion, awk string comparison, `grep` and `tr` ranges are all
# locale-sensitive too, and one setting is one thing to reason about
# instead of an audit of every pipeline in 2000 lines.
LC_ALL=C
export LC_ALL

sm_die() { echo "$SM_TOOL: $*" >&2; exit 2; }

# ------------------------------------------------------------- the guard
#
# THE most important thing in either backend, and the reason sm_cc
# exists.
#
# A tree that has not been built has no lib/libc.a.  Without it every
# test in the suite fails to link, every test classifies as blocked, and
# the report reads as a total gap -- 100% of the suite unavailable.  That
# is a build error wearing the costume of a measurement, and it is
# indistinguishable in the output from the library having genuinely lost
# every interface overnight.
#
# The same shape, in the other direction, is what FLOOR_BLOCKED guards:
# an -I that starts resolving against a host libc makes everything link
# and the gap read as CLOSED.  Both are "good news shaped like a broken
# instrument", and neither report is worth anything if either is
# reachable.
#
# Structurally unskippable, not merely present: this sets
# SM_BUILD_VERIFIED, and sm_cc -- the only way any backend is permitted
# to invoke a compiler -- refuses to run without it.  A backend cannot
# classify a single test without having come through here, and a future
# edit that reorders the guard away breaks every compile loudly instead
# of quietly reporting a total gap.
SM_BUILD_VERIFIED=no

sm_require_built() {
	[ -f "$srcdir/config.mak" ] || {
		echo "$SM_TOOL: no config.mak; run ./configure first." >&2
		exit 2; }

	CC=$(sm_cfg CC); ARCH=$(sm_cfg ARCH)
	CFLAGS_C99FSE=$(sm_cfg CFLAGS_C99FSE); CFLAGS_AUTO=$(sm_cfg CFLAGS_AUTO)

	[ -n "$CC" ] || { echo "$SM_TOOL: config.mak has no CC." >&2; exit 2; }

	[ -f "$srcdir/lib/libc.a" ] || {
		echo "$SM_TOOL: lib/libc.a is missing; run make first." >&2
		echo "$SM_TOOL: without it every test would fail to link and the" >&2
		echo "$SM_TOOL: report would read as a total gap.  That is a" >&2
		echo "$SM_TOOL: build error, not a measurement." >&2
		exit 2; }

	SM_BUILD_VERIFIED=yes
	export SM_BUILD_VERIFIED
}

sm_cfg() {
	[ -f "$srcdir/config.mak" ] || return 0
	sed -n "s/^$1 *= *//p" "$srcdir/config.mak" | tail -1
}

# nm is how "defined" is decided wherever a backend reconciles declared
# interfaces against the library.  If it is missing, that reconciliation
# would call every interface absent -- or, worse, agree with itself while
# both sides are wrong the same way.  Separate from sm_require_built
# because only one backend reconciles; it is still here, and not in that
# backend, so the reasoning is stated once.
sm_require_nm() {
	nm -g --defined-only "$srcdir/lib/libc.a" >/dev/null 2>&1 || {
		echo "$SM_TOOL: 'nm -g --defined-only lib/libc.a' does not work." >&2
		echo "$SM_TOOL: binutils' nm is how this script decides which" >&2
		echo "$SM_TOOL: interfaces are DEFINED; without it the whole" >&2
		echo "$SM_TOOL: reconciliation section would be fabricated." >&2
		exit 2; }
}

# sm_cc ARGS... -- the only permitted way to run the compiler.
#
# The assertion is not defensive programming, it is the enforcement
# mechanism for sm_require_built above.  If it ever fires it means a
# backend reached a compile without passing the guard, which is a bug in
# this tool and not a fact about the tree -- so it says so, rather than
# letting the run continue and produce a report.
sm_cc() {
	[ "$SM_BUILD_VERIFIED" = yes ] || {
		echo "$SM_TOOL: INTERNAL ERROR -- sm_cc called before" >&2
		echo "$SM_TOOL: sm_require_built.  A backend has reached a compile" >&2
		echo "$SM_TOOL: without confirming this tree is built, which would" >&2
		echo "$SM_TOOL: classify every test as blocked and report a total" >&2
		echo "$SM_TOOL: gap.  Refusing to measure.  This is a bug in" >&2
		echo "$SM_TOOL: tools/suitemap-engine.sh or its backend, not a" >&2
		echo "$SM_TOOL: problem with your checkout." >&2
		exit 2; }
	# shellcheck disable=SC2086  # $CFLAGS_* and $INC are deliberate word lists
	"$CC" "$@"
}

# ---------------------------------------------------------- the workdir
# Sets W in the CALLER and installs the cleanup trap there.  Deliberately
# not `W=$(sm_workdir)`: a command substitution runs in a subshell, the
# EXIT trap fires when that subshell returns, and the caller is handed
# the path of a directory that has just been deleted.  (Asked for it,
# got it, and the ledger census caught it -- it refused to write a report
# with zero interface directories rather than publishing an empty map.)
sm_workdir() {
	W=$(mktemp -d "${TMPDIR:-/tmp}/ntlibc-suitemap.XXXXXX") || exit 1
	trap 'rm -rf "$W"' EXIT
}

# ------------------------------------------------------- the data block
#
# Each report carries, in an HTML comment at its end, the rows it was
# rendered from.  Everything printed above that block is a pure function
# of those rows, which is what lets `--render` reproduce a report without
# the suite, a config.mak, a build or a compiler.
#
# WHY IT IS IN THE REPORT AND NOT IN A SECOND FILE
#
# Two checked-in files that must agree are a staleness bug waiting to
# happen, and a stale checked-in report is the exact thing these tools
# exist to prevent.  One file cannot drift from itself.
#
# It is also what makes a merge driver possible.  A three-way TEXT merge
# of a rendered table is meaningless -- two branches that each land a
# header change move every count, and the right answer is neither side's
# text -- but the rows are keyed, structured and independent, so merging
# THOSE and re-rendering is a real resolution.  Doing that needs the data
# of all three versions of one path, which is exactly what git hands a
# merge driver and nothing else: not the tree, not the suite, not a
# compiler, none of which git guarantees are on disk mid-merge.  See
# tools/merge-gendata.sh, and tools/merge-kaem.sh's header for the
# empirical story behind that constraint.
SM_DATA_BEGIN='<!-- BEGIN ntlibc-generated-data v1 -- the rows this report was rendered from.'
SM_DATA_END='END ntlibc-generated-data -->'

sm_read_data_block() {
	awk -v e="$SM_DATA_END" -v tags="^$SM_ROW_TAGS\t" '
		/^<!-- BEGIN ntlibc-generated-data v1/ { inb = 1; next }
		$0 == e { inb = 0; next }
		inb && $0 ~ tags { print }
	' "$1"
}

# The recorded ntlibc SHA appears twice in every report -- once in the
# header table, once as an `s` row -- and NEITHER is compared by --check.
# It changes on every commit; sm_check_ancestry is what actually enforces
# it.  Diffing it would make the stage red for a reason that has nothing
# to do with the gap, and a stage that is red for no reason is a stage
# somebody turns off.
#
# The second pattern carries a literal tab: POSIX sed has no \t.
sm_strip_shas() { sed -e '/^| ntlibc | `/d' -e '/^s	ntlibc	/d' "$1"; }

# ------------------------------------------------------------- ancestry
#
# The report claims to describe a specific tree.  This is what makes that
# claim checkable: the recorded SHA must be an ancestor of HEAD.  A
# failure is not a skip -- an unverifiable pin and a verified one are
# different claims, and a report that cannot say which tree it describes
# is not evidence about any tree.
#
# tools/merge-gendata.sh stamps the SHA `unknown` after resolving a
# merge, precisely so this fails loudly until someone measures for real.
# The repository is a parameter with a default rather than read straight
# from the environment, so a selftest can point one call at a
# non-repository without a subshell to contain the override.  The default
# is SM_GITDIR, which each backend sets to its own override variable --
# the gate runs its stages in an rsync'd copy with no .git of its own,
# and the two backends spell that override differently for historical
# reasons.  SM_GITDIR_HINT, if set, names it in the diagnostic.
sm_check_ancestry() {
	_sha=$1; _gitdir=${2:-$SM_GITDIR}
	git -C "$_gitdir" rev-parse --git-dir >/dev/null 2>&1 || {
		echo "$SM_TOOL: ANCESTRY FAILED -- $_gitdir is not a git" >&2
		echo "$SM_TOOL:   repository, so the report's recorded ntlibc SHA" >&2
		echo "$SM_TOOL:   cannot be checked against HEAD." >&2
		[ -z "${SM_GITDIR_HINT:-}" ] ||
			echo "$SM_TOOL:   Point $SM_GITDIR_HINT at the real tree." >&2
		echo "$SM_TOOL:   This is a failure and not a skip: an unverifiable" >&2
		echo "$SM_TOOL:   pin and a verified one are different claims." >&2
		return 1; }
	[ -n "$_sha" ] || {
		echo "$SM_TOOL: ANCESTRY FAILED -- the report records no ntlibc SHA." >&2
		return 1; }
	git -C "$_gitdir" cat-file -e "$_sha^{commit}" 2>/dev/null || {
		echo "$SM_TOOL: ANCESTRY FAILED -- the report records ntlibc SHA" >&2
		echo "$SM_TOOL:   $_sha, which this repository does not have." >&2
		return 1; }
	git -C "$_gitdir" merge-base --is-ancestor "$_sha" HEAD 2>/dev/null || {
		echo "$SM_TOOL: ANCESTRY FAILED -- the report records ntlibc SHA" >&2
		echo "$SM_TOOL:   $_sha, which is not an ancestor of HEAD." >&2
		echo "$SM_TOOL: The report therefore describes a tree that is not" >&2
		echo "$SM_TOOL:   this one.  Regenerate it: tools/$SM_TOOL.sh" >&2
		return 1; }
	return 0
}

# -------------------------------------------------------- the closure
#
# sm_closure SETSFILE -- SETSFILE is one line per blocked test holding
# that test's absent-header set, space separated.  Emits one row per
# step:  STEP \t HEADER \t naming \t alone \t unblocked \t remaining
# and then, with STEP 0, every header that never takes a turn.
#
# WHICH GREEDY, AND WHY IT MATTERS
#
# "Best next header" is the header that fully UNBLOCKS the most
# still-blocked tests, recomputed from scratch at every step -- not the
# header NAMED by the most tests, and not a single sort by "unblocked
# alone".  The three differ, and the difference is not academic: on
# `t1={a,b} t2={a,c} t3={d} t4={d}` the most-named rule picks `a.h`
# first, and `a.h` alone unblocks nothing whatsoever.  A section headed
# "what to build next" must not open with a header that buys nothing.
#
# Recomputing each step also matters on its own: a header's value RISES
# as its co-blockers are added, so an order fixed in advance is wrong as
# soon as any set has more than one member.
#
# Stopping when nothing fully unblocks anything is deliberate.  The
# residue is real -- tests blocked by a combination no single header
# resolves -- and draining it by pretending otherwise would overstate how
# far header-at-a-time work can get.  Those headers are listed after, at
# step 0, so they are visible without being ranked.
#
# The tie-break is a plain byte comparison on the header name, which is
# total, stated, and (given LC_ALL=C above) the same everywhere.
sm_closure() {
	awk '
	{
		nrows++
		len[nrows] = split($0, a, " ")
		for (i = 1; i <= len[nrows]; i++) { mem[nrows, i] = a[i]; hdrs[a[i]] = 1 }
	}
	END {
		for (r = 1; r <= nrows; r++) {
			for (i = 1; i <= len[r]; i++) naming[mem[r, i]]++
			if (len[r] == 1) alone[mem[r, 1]]++
		}
		remaining = nrows
		step = 0
		while (1) {
			best = ""; bestc = 0
			for (h in hdrs) {
				if (h in got) continue
				c = 0
				for (r = 1; r <= nrows; r++) {
					if (dead[r]) continue
					ok = 1
					for (i = 1; i <= len[r]; i++)
						if (!((mem[r, i]) in got) && mem[r, i] != h) { ok = 0; break }
					if (ok) c++
				}
				if (c > bestc || (c == bestc && c > 0 && h < best)) { bestc = c; best = h }
			}
			if (bestc == 0) break
			got[best] = 1
			for (r = 1; r <= nrows; r++) {
				if (dead[r]) continue
				ok = 1
				for (i = 1; i <= len[r]; i++) if (!((mem[r, i]) in got)) { ok = 0; break }
				if (ok) { dead[r] = 1; remaining-- }
			}
			step++
			printf "%d\t%s\t%d\t%d\t%d\t%d\n", step, best, naming[best], alone[best], bestc, remaining
		}
		for (h in hdrs) if (!(h in got)) printf "%d\t%s\t%d\t%d\t%d\t%d\n", 0, h, naming[h], alone[h], 0, remaining
	}' "$1" | sort -k1,1n -k2,2
}

# SM_WORKER_GUARD -- the same assertion as sm_cc, as a line of text to be
# written into a generated worker script.
#
# A backend that compiles in parallel runs its compile in a separate
# process (xargs -P), where a shell function in this file is out of
# scope.  Rather than leave that path unguarded -- which would be the one
# compile in the tool that could run on an unbuilt tree -- the worker
# carries the assertion inline, and SM_BUILD_VERIFIED is exported so it
# can see it.  Same contract as sm_cc: reaching a compile without the
# guard is a bug in this tool, not a fact about the tree.
SM_WORKER_GUARD='[ "${SM_BUILD_VERIFIED:-no}" = yes ] || { echo "suitemap: INTERNAL ERROR -- a compile worker started without the build guard; refusing to measure." >&2; exit 2; }'

sm_pct() { awk -v a="$1" -v b="$2" 'BEGIN{printf "%.1f%%", (b?a*100/b:0)}'; }
