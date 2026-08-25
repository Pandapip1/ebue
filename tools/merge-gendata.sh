#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/merge-gendata.sh -- git merge driver for the two generated
# reports, test/POSIX-GAP-MAP.generated.md and
# test/LIBC-TEST-MAP.generated.md.
#
# WHY THESE FILES NEED A DRIVER AT ALL
#
# Both are generated (tools/posix-gapmap.sh, tools/libc-test-map.sh) and
# checked in, because the value of a gap measurement is its diff.  Both
# are also almost entirely DERIVED: a greedy header closure, a
# per-directory ledger, a four-cell reconciliation, a dozen counts in
# prose.  Land one header on one branch and a different one on another,
# and every number in both files moves.
#
# A three-way TEXT merge of that is not merely conflict-prone, it is
# meaningless: the correct result is neither side's text and is not any
# combination of their lines either.  It is what a fresh measurement of
# the MERGED tree would say.  Left to git's default merge, this produces
# a conflict whose only honest manual resolution is "take either side,
# regenerate, git add" -- and .githooks/pre-commit exists partly because
# that resolution has gone wrong here before, committing conflict markers
# into a generated file.
#
# WHY IT DOES NOT REGENERATE, AND WHY THAT IS NOT NEGOTIABLE
#
# The obvious driver -- run the generator against the worktree and let
# its output stand in as the resolution -- is exactly what
# tools/merge-kaem.sh tried first, and it is wrong.  git's default merge
# backend (`ort`, used by merge, rebase and cherry-pick alike) computes
# the whole merge in memory and writes NOTHING to the real index or
# working tree, not even a sibling path that merged with no conflict at
# all, until every path's merge -- including every custom driver
# invocation -- has finished.  So a live regeneration silently measures a
# tree that is missing whatever the other side of the same operation
# added: exit 0, no markers, wrong numbers.  See merge-kaem.sh's header
# for how that was confirmed, including the finding that git does not run
# the pre-commit hook for a commit an auto-resolved cherry-pick makes on
# its own, so there is no safety net downstream either.
#
# Here it would be worse still: these generators need their submodule
# (third_party/ltp, third_party/libc-test) and a built lib/libc.a, and
# neither is guaranteed to exist in a clone that is mid-rebase.
#
# WHY THIS ONE CAN DO BETTER THAN merge-kaem.sh ANYWAY
#
# merge-kaem.sh had no choice but to reconcile the generated TEXT, hunk
# shape by hunk shape, because boot/kaem/*.kaem has no separable data
# layer -- the text is all there is.  These two reports do have one.
# Each carries, in an HTML comment at its end, the rows it was rendered
# from: one per conformance test, one per interface directory, one per
# class B name, plus a handful of scalars.  Those rows are keyed,
# independent of each other, and small; the rendered tables above them
# are a pure function of them.
#
# So this driver does something merge-kaem.sh could not: it merges the
# DATA -- a genuine key-wise three-way merge, not a text merge -- and
# then re-renders the report from the merged rows, via the generator's
# own `--render FILE` mode.  That mode compiles nothing and reads neither
# the suite nor the tree, so it is safe to run mid-merge: like this
# driver, it is a pure function of the three blobs git handed us.
#
# The result is a real resolution rather than a re-derivation, and it is
# byte-identical to what the renderer would have produced anyway, because
# it IS the renderer.
#
# WHAT IT DELIBERATELY DOES NOT CLAIM
#
# A key-wise merge of the rows is still not a measurement of the merged
# tree.  Two branches whose header additions INTERACT -- a test blocked
# by both pthread.h and mqueue.h is still class A on each branch alone,
# and becomes class C only once both have landed -- will merge to rows
# that are individually right and jointly stale.  Nothing available to a
# merge driver can see that; only a fresh run can.
#
# So the driver refuses to certify what it cannot check: it stamps the
# recorded ntlibc SHA as `unknown`.  That is not cosmetic.  Both
# generators' --check verifies the recorded SHA is an ancestor of HEAD --
# the process invariant that stops a report being quietly months old --
# and `unknown` fails it loudly, by an existing, self-tested code path.
# The merge itself completes, with no markers anywhere; what does not
# happen is the merged report silently reading as a current measurement.
# .githooks/pre-commit sees the same `unknown` and re-measures for real
# when it can, so in the ordinary case the beacon is cleared by the very
# next commit without anyone having to know it was lit.
#
# Deliberately NOT the alternative of stamping one side's SHA: that is
# what git's default text merge effectively does today, and an unverified
# claim that reads as verified is the failure mode this whole area of the
# tree is built around.
#
# ENABLING IT
#
# git reads the .gitattributes ASSOCIATION from the repository but never
# the driver COMMAND, which is per-clone config, same as core.hooksPath.
# ./configure sets both.  By hand:
#   git config merge.ntlibc-gendata.driver 'tools/merge-gendata.sh %O %A %B %P'

set -eu

ancestor=$1
current=$2
other=$3
path=$4

cd "$(git rev-parse --show-toplevel)"

# %P decides which renderer to use.  Matched against the exact committed
# filenames, not a glob, so that a rename or a new generated report added
# without thinking about this driver fails loudly here instead of being
# rendered by whichever tool a looser pattern happened to pick.
case $path in
test/POSIX-GAP-MAP.generated.md)  renderer=tools/posix-gapmap.sh ;;
test/LIBC-TEST-MAP.generated.md)  renderer=tools/libc-test-map.sh ;;
*)
	echo "merge-gendata.sh: registered for an unexpected path '$path'" >&2
	echo "merge-gendata.sh: (.gitattributes should only ever route the two" >&2
	echo "merge-gendata.sh: generated reports here).  Leaving it conflicted." >&2
	exit 1
	;;
esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The data block, as the generators write it.  Kept in one place here
# rather than sourced from either tool, because this driver must keep
# working on the three blobs even when the worktree's copy of the tool is
# itself mid-conflict.
block() {
	awk '
		/^<!-- BEGIN ntlibc-generated-data v1/ { inb = 1; next }
		$0 == "END ntlibc-generated-data -->" { inb = 0; next }
		inb && /^[a-z]\t/ { print }
	' "$1"
}

block "$ancestor" > "$work/o"
block "$current"  > "$work/a"
block "$other"    > "$work/b"

# A version with no data block is one this driver cannot reason about: a
# report generated before the block existed, or one edited by hand into
# something else.  Fall back to exactly what git would have done without
# a driver -- a text merge, markers and all -- rather than inventing a
# resolution from a file we do not understand.
if [ ! -s "$work/a" ] || [ ! -s "$work/b" ]; then
	echo "merge-gendata.sh: '$path' has no ntlibc-generated-data block on" >&2
	echo "merge-gendata.sh: at least one side, so there is nothing" >&2
	echo "merge-gendata.sh: structured to merge.  Falling back to git's own" >&2
	echo "merge-gendata.sh: text merge; resolve it by hand and regenerate." >&2
	git merge-file "$current" "$ancestor" "$other" || exit 1
	exit 0
fi

# ------------------------------------------------------ the row merge
#
# Every row is `<section>\t<key>\t<value...>`, and the (section, key)
# pair is unique within a version -- one row per test, per directory, per
# name, per scalar.  So the merge is the textbook three-way rule applied
# per key, with an absent row meaning "not present in that version":
#
#   a == b            -> that value            (both sides agree, or
#                                               both changed it the same way)
#   a == o            -> b                     (only the other side moved it,
#                                               including removing it)
#   b == o            -> a                     (only this side moved it)
#   otherwise         -> a real divergence
#
# Divergence is NOT guessed at.  It takes %A's value so the output stays
# a valid, marker-free report, and sets the exit status so git records
# the path as conflicted and a human is told to regenerate -- the same
# contract merge-kaem.sh uses for a hunk shape it does not recognize.
awk -F'\t' '
	function key(line,   f) { split(line, f, "\t"); return f[1] SUBSEP f[2] }
	FILENAME == ARGV[1] { o[key($0)] = $0; next }
	FILENAME == ARGV[2] { a[key($0)] = $0; ka[key($0)] = 1; next }
	{ b[key($0)] = $0; kb[key($0)] = 1 }
	END {
		for (k in ka) all[k] = 1
		for (k in kb) all[k] = 1
		for (k in all) {
			av = (k in a) ? a[k] : ""
			bv = (k in b) ? b[k] : ""
			ov = (k in o) ? o[k] : ""
			if (av == bv)      r = av
			else if (av == ov) r = bv
			else if (bv == ov) r = av
			else { r = av; diverged++ }
			if (r != "") print r
		}
		exit (diverged ? 1 : 0)
	}
' "$work/o" "$work/a" "$work/b" > "$work/merged" 2>"$work/awkerr" && rc=0 || rc=$?

if [ "$rc" -gt 1 ] || [ -s "$work/awkerr" ]; then
	cat "$work/awkerr" >&2 2>/dev/null || true
	echo "merge-gendata.sh: the row merge itself failed on '$path' --" >&2
	echo "merge-gendata.sh: leaving it conflicted rather than writing" >&2
	echo "merge-gendata.sh: a report nothing produced." >&2
	exit 1
fi

# The recorded ntlibc SHA: see "WHAT IT DELIBERATELY DOES NOT CLAIM".
# Rewritten, never merged, because no measurement of the merged tree has
# happened and the report must not say one has.
LC_ALL=C sort "$work/merged" |
	sed -e 's/^\(s	ntlibc	\).*$/\1unknown/' > "$work/rows"

# Re-render.  The renderer reads the data block out of the file it is
# given and writes the whole report back to that same path, so %A is
# built from a stub holding nothing but the merged rows.  Nothing here
# touches the real report path: git owns it until the merge finishes.
{
	printf '%s\n' '<!-- BEGIN ntlibc-generated-data v1 -- merged by tools/merge-gendata.sh.'
	printf '\n'
	cat "$work/rows"
	printf '%s\n' 'END ntlibc-generated-data -->'
} > "$work/stub"

if ! "$renderer" --render "$work/stub" >"$work/render.log" 2>&1; then
	sed 's/^/merge-gendata.sh:   /' "$work/render.log" >&2
	echo "merge-gendata.sh: $renderer --render rejected the merged rows for" >&2
	echo "merge-gendata.sh: '$path' -- an invariant the merge cannot satisfy" >&2
	echo "merge-gendata.sh: (a census, a floor, a canary).  Leaving it" >&2
	echo "merge-gendata.sh: conflicted; resolve by hand and regenerate." >&2
	exit 1
fi
cp "$work/stub" "$current"

if [ "$rc" -ne 0 ]; then
	echo "merge-gendata.sh: '$path': some rows were changed differently on" >&2
	echo "merge-gendata.sh: both sides.  The merged report takes this side's" >&2
	echo "merge-gendata.sh: value for those and is marker-free, but nothing" >&2
	echo "merge-gendata.sh: can decide them without measuring: regenerate" >&2
	echo "merge-gendata.sh: with 'make posix-gapmap' / 'make libc-test-map'" >&2
	echo "merge-gendata.sh: and 'git add $path'." >&2
	exit 1
fi

echo "merge-gendata.sh: resolved $path (rows merged from \$O/\$A/\$B, report" >&2
echo "merge-gendata.sh: re-rendered; its ntlibc SHA is now 'unknown' until a" >&2
echo "merge-gendata.sh: real regeneration -- --check will say so)." >&2
