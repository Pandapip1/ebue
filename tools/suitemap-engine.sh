# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# shellcheck shell=sh
#
# No shebang, because this file is sourced and never executed -- see
# "NOT EXECUTABLE" below.  The directive is how shellcheck is told which
# dialect to apply to a library; without it the whole file is checked
# against an unknown shell (SC2148) and the useful checks are skipped.
#
# tools/suitemap-engine.sh -- report, cache and compiler-guard helpers for
# tools/posix-gapmap.sh.  This is sourced shell code, not an executable.
# Keeping the generic mechanics here leaves the driver focused on OPTS
# discovery and classification.
#
# WHAT A BACKEND SUPPLIES, AND WHAT IT MAY NOT
#
# Before sourcing this file a backend sets:
#
#   srcdir         absolute path of the source tree
#   SM_TOOL        the name in every diagnostic, e.g. `posix-gapmap`
#   SM_ROW_TAGS    the data-block row tags it uses, as an sed/awk
#                  bracket expression, e.g. '[stdn]'
#
# That list used to include SM_REPORT, "absolute path of the generated
# report it owns".  Both backends set it and nothing has ever read it:
# each renders to its own $REPORT directly.  It is removed rather than
# documented, because a contract variable nobody reads is a contract
# nobody is keeping, and the next reader has to grep the whole engine to
# find that out.  shellcheck's SC2034 is what noticed.
#
# The three that remain are re-declared immediately below rather than
# merely described here, so that a backend which forgets one fails at
# the source line naming what is missing, instead of somewhere later in
# a diagnostic that prints an empty tool name.
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
# glob in this tool orders BYTES, not words.
#
# glibc's UTF-8 collations ignore punctuation at the first comparison
# level, so `sched_getparam` sorts BEFORE `sched_get_priority_max` under
# en_US.UTF-8 (compare `schedgetparam` with `schedgetprioritymax`) and
# AFTER it under C (`_` is 0x5F, `p` is 0x70).
#
# Stable byte ordering keeps reports comparable across developer and CI
# locales.
#
# Set once, here, and exported, rather than per-`sort`: pathname
# expansion, awk string comparison, `grep` and `tr` ranges are all
# locale-sensitive too, and one setting is one thing to reason about
# instead of an audit of every pipeline in 2000 lines.
LC_ALL=C
export LC_ALL

# The inbound contract, enforced.  `${x:?msg}` in an assignment to the
# same name is a no-op when the backend has done its job and an abort
# naming the omission when it has not.  It also makes the contract
# visible to shellcheck, which lints this file standalone and otherwise
# reports SM_TOOL as a possible misspelling of SM_K_TOOL (SC2153) and
# srcdir as never assigned (SC2154) -- both of which are the same
# observation it cannot see the backend, and neither of which it should
# have to guess at.
srcdir=${srcdir:?a suitemap backend must set srcdir before sourcing the engine}
SM_TOOL=${SM_TOOL:?a suitemap backend must set SM_TOOL before sourcing the engine}
SM_ROW_TAGS=${SM_ROW_TAGS:?a suitemap backend must set SM_ROW_TAGS before sourcing the engine}

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

	# ARCH is read by both backends' include paths, not by this file,
	# and the engine is linted standalone, so that use is invisible.
	# (A comment whose first word is the linter's name is parsed as a
	# directive, hence the wording -- SC1072/SC1073 if you get it wrong.)
	CC=$(sm_cfg CC)
	# On its own line because a directive attaches to the next COMMAND,
	# and `CC=...; ARCH=...` is two of them -- placed above the pair it
	# would suppress nothing and the finding would survive.
	# shellcheck disable=SC2034  # consumed by the backends, not here
	ARCH=$(sm_cfg ARCH)
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
# Keeping the rows and rendering together also makes an uploaded report
# self-contained.
#
# Emitted by each backend's report body; sm_read_data_block below reads
# only the END marker, so shellcheck sees no use of the BEGIN one.
# shellcheck disable=SC2034  # emitted by the backends, not here
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
# It changes on every commit; sm_check_provenance is what vets it, and
# the regeneration diff below is what vets the rows it stamps.  Diffing it would make the stage red for a reason that has nothing
# to do with the gap, and a stage that is red for no reason is a stage
# somebody turns off.
#
# The second pattern carries a literal tab: POSIX sed has no \t.
sm_strip_shas() { sed -e '/^| ntlibc | `/d' -e '/^s	ntlibc	/d' "$1"; }

# ---------------------------------------------------------- provenance
#
# WHAT THIS WAS, AND WHY IT IS NOT THAT ANY MORE
#
# This was an ANCESTRY check: the SHA the report records had to be an
# ancestor of HEAD.  The intent was right -- a report claims to describe
# a specific tree, and that claim should be checkable -- but it keyed the
# claim on a COMMIT'S IDENTITY, and a commit's identity is the one thing
# a rebase does not preserve.  Work is landed in this repository by
# rebasing it onto current origin/main and pushing, so the commit a
# report was measured at is rewritten before it is ever published.  The
# recorded SHA is then not merely a non-ancestor of HEAD: it is not in
# the repository at all, in any clone, ever.
#
# That is not hypothetical.  Both reports were red on main at 45d1616
# recording ebddd6b, a commit no clone of this repository has.  A full
# regeneration -- 1610 conformance compiles for the gap map, 146 for the
# coverage map -- changed exactly four lines across the two files, and
# all four were the SHA.  Every measured row was byte-identical.  The
# reports were correct and the gate was red, which is exactly the "red
# for a reason that has nothing to do with the gap" failure the rest of
# this file is written to avoid, and exactly what gets a stage turned
# off.
#
# WHAT ACTUALLY CHECKS THE CLAIM
#
# --check does not take the report's word for anything.  It regenerates
# from THIS tree and diffs every row.  That diff is the report's claim
# EVALUATED rather than asserted, and it is strictly stronger than any
# statement about a SHA:
#
#   - a report carried in from an unrelated tree describes that tree's
#     gap, so its rows differ from this tree's and the diff rejects it;
#   - and if its rows do NOT differ, then it is a true statement about
#     this tree, whatever tree it was measured on.
#
# An ancestry proof stacked on top of a full re-measurement therefore
# adds nothing about the numbers.  It only ever spoke about the stamp.
#
# So the stamp is now checked AS a stamp, and only for the properties a
# rebase cannot destroy:
#
#   1. the repository named must actually be a repository.  Not
#      cosmetic: --generate stamps the literal `unknown` when it cannot
#      run rev-parse (see NTLIBC_SHA in the backends), and sm_strip_shas
#      removes the SHA rows before the diff, so a --check run pointed at
#      a non-repository would compare `unknown` against `unknown` and
#      call it agreement.  "I could not check" and "it checks out" are
#      different claims.
#   2. the report must carry a stamp at all.
#   3. the stamp must be a well-formed object name -- 40 lower-case hex.
#
# Ancestry itself is reported and not enforced.  The reason is that its
# two failing outcomes are indistinguishable in the only case that
# arises: a SHA a rebase rewrote away and a SHA from somebody else's
# repository are both simply absent.  Enforcing it cannot separate the
# honest case from the dishonest one, and does reliably fail the honest
# one several times an hour.  What it CAN still do is say which of the
# three situations holds, so a reader of a --check log knows whether the
# stamp is quotable to `git show`; so it says so, and passes.
sm_check_provenance() {
	_sha=$1; _gitdir=${2:-$SM_GITDIR}
	git -C "$_gitdir" rev-parse --git-dir >/dev/null 2>&1 || {
		echo "$SM_TOOL: PROVENANCE FAILED -- $_gitdir is not a git" >&2
		echo "$SM_TOOL:   repository, so a regeneration here would stamp" >&2
		echo "$SM_TOOL:   'unknown' and --check would be comparing one" >&2
		echo "$SM_TOOL:   unstamped report against another." >&2
		[ -z "${SM_GITDIR_HINT:-}" ] ||
			echo "$SM_TOOL:   Point $SM_GITDIR_HINT at the real tree." >&2
		echo "$SM_TOOL:   This is a failure and not a skip: an unverifiable" >&2
		echo "$SM_TOOL:   pin and a verified one are different claims." >&2
		return 1; }
	[ -n "$_sha" ] || {
		echo "$SM_TOOL: PROVENANCE FAILED -- the report records no ntlibc SHA." >&2
		return 1; }
	[ "$_sha" != unknown ] || {
		echo "$SM_TOOL: PROVENANCE FAILED -- the report records ntlibc SHA" >&2
		echo "$SM_TOOL:   'unknown'; this report is not tied to a checkout." >&2
		echo "$SM_TOOL:   Regenerate for real: tools/$SM_TOOL.sh" >&2
		return 1; }
	# 40 lower-case hex.  A stamp is documentation, and documentation
	# that cannot be pasted into `git show` is not documentation.
	_bad=no
	case $_sha in *[!0-9a-f]*) _bad=yes ;; esac
	[ "${#_sha}" -eq 40 ] || _bad=yes
	[ "$_bad" = no ] || {
		echo "$SM_TOOL: PROVENANCE FAILED -- the report records ntlibc SHA" >&2
		echo "$SM_TOOL:   '$_sha', which is not a full object name (40" >&2
		echo "$SM_TOOL:   lower-case hex digits).  The stamp has been edited" >&2
		echo "$SM_TOOL:   by hand or truncated; regenerate: tools/$SM_TOOL.sh" >&2
		return 1; }
	# Reported, not enforced -- see the block above.  Goes to stdout,
	# because none of the three outcomes is an error.
	if ! git -C "$_gitdir" cat-file -e "$_sha^{commit}" 2>/dev/null; then
		echo "$SM_TOOL: provenance: measured at $_sha, which this clone does"
		echo "$SM_TOOL:   not have -- the ordinary reason is that landing that"
		echo "$SM_TOOL:   work rebased it.  Not an error: the regeneration diff"
		echo "$SM_TOOL:   is what establishes the report describes THIS tree."
	elif git -C "$_gitdir" merge-base --is-ancestor "$_sha" HEAD 2>/dev/null; then
		echo "$SM_TOOL: provenance: measured at $_sha, an ancestor of HEAD."
	else
		echo "$SM_TOOL: provenance: measured at $_sha, which this clone has but"
		echo "$SM_TOOL:   which is not an ancestor of HEAD."
	fi
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
# Single quotes are the point: this is shell TEXT, printed verbatim into
# the generated compile wrapper and evaluated there, in another process,
# against that process's SM_BUILD_VERIFIED.  Expanding it here would bake
# in this process's answer and the guard would assert nothing.
# shellcheck disable=SC2034,SC2016  # emitted by the backends; deliberately unexpanded
SM_WORKER_GUARD='[ "${SM_BUILD_VERIFIED:-no}" = yes ] || { echo "suitemap: INTERNAL ERROR -- a compile worker started without the build guard; refusing to measure." >&2; exit 2; }'

sm_pct() { awk -v a="$1" -v b="$2" 'BEGIN{printf "%.1f%%", (b?a*100/b:0)}'; }

# ==========================================================  the cache
#
# Measuring is not cheap, and the pre-commit hook that keeps these
# reports honest has to pay for it on every commit that could have moved
# a number.  So the analysis half is cached.
#
# WHAT THE COST ACTUALLY IS, MEASURED
#
# posix-gapmap, 1610 tests, GAPMAP_JOBS=2, total 8.5s:
#
#   compile every test        3.0s   35%
#   absent-header resolution  2.2s   26%
#   class B detail + ledger   2.7s   32%
#   emit                      0.5s    6%
#
# Note what that says: caching "the compiled artefacts" alone would leave
# two thirds of the cost on the table.  The unit worth caching is the
# whole ANALYSIS -- every intermediate the derivations read.
#
# THE KEY IS THE ENTIRE DESIGN
#
# A cache keyed on something that does not fully determine its value
# serves a stale answer silently, and a silently stale gap measurement is
# strictly worse than no cache: it is exactly the "good news shaped like
# a broken instrument" failure this instrument exists to prevent, with a
# performance optimisation as its cause.  So the key covers every input,
# and each component is recorded SEPARATELY so a miss can be explained
# rather than guessed at:
#
#   cc       $CC and its own version banner.  A compiler that has been
#            rebuilt in place classifies differently and is invisible to
#            a name-only key.
#   flags    CFLAGS_C99FSE, CFLAGS_AUTO and the -I set, verbatim.
#   headers  the content of every header this library offers --
#            include/, arch/, obj/include/.  This is what decides class A.
#   libc     lib/libc.a by content.  This is what decides B from C, and
#            it is why the cache correctly misses after almost any build:
#            a changed library can change a link outcome.
#   suite    the suite's SHARED material -- its include tree and harness
#            -- hashed separately from the per-test sources.
#   tool     this engine and the calling backend, by content.  The
#            classification rules live in them, so an edit to either must
#            invalidate everything -- including an edit made while
#            debugging the cache itself.
#
# Deliberately NOT keyed on: the ntlibc SHA (it changes every commit and
# determines none of these numbers; the report records it separately and
# --check enforces it), the wall clock, or a version counter somebody has
# to remember to bump.
#
# A cache that never misses is as broken as one that never hits, and
# neither is visible from the outside, so sm_cache_explain_miss names the
# components that moved and there is an invalidation matrix in
# CONTRIBUTING.md that changes each input in turn and checks the answer.
#
# WHAT THE CACHE MAY NOT DO
#
# It may not stand in for the build guard.  sm_require_built runs first
# and unconditionally: a tree with no lib/libc.a is refused before a key
# is ever computed, so no hit can let an unbuilt tree report a total gap.
# The census and the submodule pin check are likewise outside the cache.
#
# Disable with SUITEMAP_CACHE=0; relocate with SUITEMAP_CACHE_DIR.  The
# directory is gitignored: derived, per-machine, safe to delete.

SM_CACHE_DIR=${SUITEMAP_CACHE_DIR:-$srcdir/.suitemap-cache}
SM_CACHE_ENABLED=${SUITEMAP_CACHE:-1}

# A content hash over a set of trees.  Paths are hashed as well as
# contents, so MOVING a header invalidates just as editing one does.
# NUL-delimited throughout: a path containing a space must not hash as
# two paths.
sm_hash_paths() {
	for _r in "$@"; do
		[ -e "$_r" ] && find "$_r" -type f -print0
	done | sort -z | xargs -0 -r sha256sum 2>/dev/null |
		sha256sum | cut -d' ' -f1
}

sm_hash_str() { printf '%s' "$1" | sha256sum | cut -d' ' -f1; }

# SM_CACHE_SUITE_PATHS is set by the backend to its suite's shared
# material only -- the tests' own sources are not in it.
sm_cache_compute_key() {
	[ "$SM_CACHE_ENABLED" = 1 ] || return 1

	SM_K_CC=$(sm_hash_str "$CC$("$CC" -v 2>&1 | head -5)")
	SM_K_FLAGS=$(sm_hash_str "$CFLAGS_C99FSE|$CFLAGS_AUTO|${INC:-}")
	SM_K_HEADERS=$(sm_hash_paths "$srcdir/include" "$srcdir/arch" "$srcdir/obj/include")
	SM_K_LIBC=$(sha256sum "$srcdir/lib/libc.a" | cut -d' ' -f1)
	# shellcheck disable=SC2086  # a deliberate word list of roots
	SM_K_SUITE=$(sm_hash_paths ${SM_CACHE_SUITE_PATHS:-})
	SM_K_TOOL=$(sm_hash_paths "$srcdir/tools/suitemap-engine.sh" "$srcdir/tools/$SM_TOOL.sh")

	SM_CACHE_KEY=$(sm_hash_str \
		"$SM_K_CC|$SM_K_FLAGS|$SM_K_HEADERS|$SM_K_LIBC|$SM_K_SUITE|$SM_K_TOOL")
	return 0
}

# Emitted in a fixed order that is also sorted, because
# sm_cache_explain_miss joins two of these.
sm_cache_key_breakdown() {
	printf 'cc\t%s\nflags\t%s\nheaders\t%s\nlibc\t%s\nsuite\t%s\ntool\t%s\n' \
		"$SM_K_CC" "$SM_K_FLAGS" "$SM_K_HEADERS" "$SM_K_LIBC" \
		"$SM_K_SUITE" "$SM_K_TOOL"
}

sm_cache_explain_miss() {
	_last="$SM_CACHE_DIR/$SM_TOOL.last-key"
	[ -f "$_last" ] || { echo "$SM_TOOL: cache: cold (no previous run)" >&2; return; }
	mkdir -p "$SM_CACHE_DIR"
	sm_cache_key_breakdown > "$SM_CACHE_DIR/.now.$$"
	_ch=$(join -j1 "$_last" "$SM_CACHE_DIR/.now.$$" 2>/dev/null |
	      awk '$2 != $3 { printf "%s ", $1 }')
	rm -f "$SM_CACHE_DIR/.now.$$"
	if [ -n "$_ch" ]; then
		echo "$SM_TOOL: cache: miss -- changed: $_ch" >&2
	else
		echo "$SM_TOOL: cache: miss (no entry for this key yet)" >&2
	fi
}

# ------------------------------------------------- the analysis entry
#
# Every intermediate the derivations read.  A hit skips everything
# between enumeration and rendering.
sm_cache_analysis_load() {
	[ -n "${SM_CACHE_KEY:-}" ] || return 1
	_d="$SM_CACHE_DIR/analysis/$SM_CACHE_KEY"
	[ -f "$_d/.complete" ] || return 1
	cp "$_d"/* "$W/" 2>/dev/null || return 1
	rm -f "$W/.complete"
	# A HIT records the key too.  It used to be written only on a store,
	# which meant that after any hit the recorded key was whatever the
	# last MISS had computed, and the next miss then blamed components
	# that had not moved -- "changed: flags tool" for a run in which only
	# the flags changed.  A cache whose miss explanation is wrong is a
	# cache nobody can tell from a broken one.
	sm_cache_key_breakdown > "$SM_CACHE_DIR/$SM_TOOL.last-key"
	echo "$SM_TOOL: cache: hit (analysis reused; nothing recompiled)" >&2
	return 0
}

# Written to a temporary directory and renamed into place, so an
# interrupted run cannot leave a half-written entry that a later run
# would trust.  `.complete` is created last and is the only thing a load
# looks for -- a partial entry is not a smaller gap, it is no entry.
sm_cache_analysis_store() {
	[ -n "${SM_CACHE_KEY:-}" ] || return 0
	_d="$SM_CACHE_DIR/analysis/$SM_CACHE_KEY"
	_t="$SM_CACHE_DIR/analysis/.tmp.$$"
	rm -rf "$_t"; mkdir -p "$_t" || return 0
	for _f in "$@"; do [ -f "$W/$_f" ] && cp "$W/$_f" "$_t/"; done
	: > "$_t/.complete"
	rm -rf "$_d"
	mv "$_t" "$_d" 2>/dev/null || rm -rf "$_t"
	sm_cache_key_breakdown > "$SM_CACHE_DIR/$SM_TOOL.last-key"
	return 0
}
