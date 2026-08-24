#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# merge-kaem.sh -- git merge driver for boot/kaem/build-*.kaem.
#
# boot/kaem/build-i386.kaem and build-x86_64.kaem are generated (by
# tools/gen-kaem.sh, driven by `make kaem`) from the Makefile's own source
# file list, and committed. Any two branches that each add a source file
# therefore conflict textually in these two files: the compile-command
# list gets a new line inserted at the same point from both sides (an
# add/add hunk plain diff3 cannot order), and the single `${CC} -ar rcs
# lib/libc.a ...` line -- every object in one line -- is rewritten
# differently by each side, so it conflicts every time either side
# changes at all. Left to git's default text merge, a human resolves this
# by hand, always the same mechanical way (pick a side, `make kaem`,
# `git add`) -- and twice that resolution has gone wrong badly enough to
# leave conflict markers committed straight into a generated bootstrap
# script, which then sits broken until a from-scratch kaem bootstrap
# actually runs it. See CONTRIBUTING.md and .githooks/pre-commit (which
# now refuses staged conflict markers for exactly this reason).
#
# WHY THIS DOES NOT JUST CALL tools/gen-kaem.sh
# ----------------------------------------------
# The obvious design -- regenerate from the live worktree via gen-kaem.sh,
# then copy the right arch's output to %A -- was the first thing tried
# here, and it does not work. gen-kaem.sh reads the source tree straight
# off disk (a real `make -n -B` dry run over `$(wildcard ...)` globs), not
# out of git's index. Testing it against a real cherry-pick conflict --
# two branches each adding a different src/ file, cherry-picked into each
# other -- showed that git's default merge backend (`ort`, used by
# `merge`, `rebase` and `cherry-pick` alike since it became the default)
# computes the *entire* merge in memory and does not write anything to
# the real index or working tree -- not even the sibling paths that
# merged with no conflict at all -- until every path's merge, including
# every custom driver invocation, has finished. `git checkout-index -a -f`
# right before regenerating does not help either: the sibling path is not
# even in the index yet at that point, only inside ort's in-memory result.
# So a live regeneration silently drops whatever the *other* side of the
# same merge added -- it exits 0, leaves no conflict markers, and is
# simply wrong. That is exactly the "plausible but wrong" failure this
# driver exists to avoid, and it is not a rare timing fluke: since ort
# defers every write until the whole operation completes, it reproduces
# on every conflict that also touches a sibling path, every time.
#
# Worse, there is no safety net downstream for a cherry-pick that
# resolves without any manual conflict: git does not run the pre-commit
# hook for the commit an auto-resolved `cherry-pick`/`merge` makes on its
# own (hooks only fire when `git commit` is actually invoked, which
# happens only if a human has to finish resolving something by hand) --
# confirmed here by pointing core.hooksPath at a wrapper that logs before
# calling the real hook, and watching it never fire across a clean
# driver-resolved cherry-pick. So .githooks/pre-commit's own `make kaem`
# drift check, despite doing the same live regeneration, could not have
# caught this either: the two live-tree approaches share the exact same
# blind spot, and it goes uncaught all the way into the commit.
#
# THE ACTUAL APPROACH: a textual 3-way merge, using only %O/%A/%B
# -----------------------------------------------------------------
# git guarantees the three blob contents it hands this driver (the
# ancestor, current and other versions of *this one file*) regardless of
# what stage the rest of the merge is at -- unlike any sibling path, they
# are never in-flight. So instead of asking the live tree what the answer
# should be, this driver reruns `git merge-file` (plain diff3) on those
# three blobs to get git's own conflict hunks, and resolves each one
# using what is known about gen-kaem.sh's output shape.
#
# The compile-command/mkdir lines need nothing special: a hunk that is
# exactly one line added on each side is a plain add/add ambiguity from
# two independent single-line insertions landing at the same point in an
# already-sorted list, and emitting both lines -- ordered against each
# other the same way the surrounding list already is -- is exactly what a
# fresh gen-kaem.sh run would have produced.
#
# The one `${CC} -ar rcs lib/libc.a ...` line needs more care, because it
# is not sorted by a single plain rule: the Makefile builds it as
# `$(filter obj/src/%,$(ALL_OBJS)) $(filter obj/arch/%,$(ALL_OBJS))` --
# every obj/src/ object (itself globally sorted, and this is where a
# per-file arch override under src/$dir/$(ARCH)/ ends up too) followed by
# every obj/arch/ object (from arch/$(ARCH)/src/, also sorted) -- not one
# flat sort of the whole line. Reimplementing that grouping rule by hand
# here would just be a second, independent copy of Makefile logic to keep
# in sync by hand -- exactly the kind of drift this whole mechanism exists
# to prevent. So instead of text-merging that single monster line (or
# hand-sorting its tokens), this driver explodes it into one object per
# line -- in all three of %O/%A/%B, before the merge, not after -- so the
# *same* add/add single-line-insertion handling above resolves a
# coincident object insertion too, landing each new object exactly where
# gen-kaem.sh would have put it: diff3 only ever produces a conflicting
# hunk where both sides' edits land at the *same* point relative to
# unchanged context, so a new object token surfaces in a hunk immediately
# next to the (unchanged, already-correctly-grouped-and-sorted) tokens
# around it -- never merged across the obj/src/ vs obj/arch/ boundary,
# since that boundary is unchanged context no add/add hunk crosses. The
# line is reassembled after the merge.
#
# Anything else -- multi-line sides, a hunk this driver cannot confidently
# classify as one of the two shapes above -- is left as git's own
# conflict-marker output and the driver exits non-zero. Guessing at an
# unfamiliar hunk shape is exactly the "plausible but wrong" mistake this
# driver exists to rule out; a shape it does not recognize is a real
# conflict for a human, the same as if this driver were not registered at
# all.
#
# This needs no source tree, no compiler, and no config.mak: it is a pure
# function of the three blobs git already handed it, so it cannot be
# fooled by the live tree's state, and it produces the identical output a
# correct fresh regeneration would, without ever needing one.
#
# %P is only used to sanity-check that this driver is only ever being
# asked about the two files it knows how to reason about (.gitattributes
# should never route anything else here); the merge itself does not
# depend on the arch at all, since it operates purely on the three files'
# own text.
#
# This script alone does nothing -- git does not read merge driver
# *behavior* from the repository, only the .gitattributes *association*
# between a path pattern and a driver name. The actual
# `merge.<name>.driver` command line is per-clone config, same as
# core.hooksPath for .githooks: see ./configure, which sets both with one
# `git config` block. To enable by hand in an already-configured clone:
#   git config merge.ntlibc-kaem.driver 'tools/merge-kaem.sh %O %A %B %P'

set -eu

ancestor=$1
current=$2
other=$3
path=$4

case $path in
	boot/kaem/build-i386.kaem | boot/kaem/build-x86_64.kaem) ;;
	*)
		echo "merge-kaem.sh: registered for an unexpected path '$path'" >&2
		echo "merge-kaem.sh: (.gitattributes should only ever route" >&2
		echo "merge-kaem.sh: boot/kaem/build-i386.kaem and build-x86_64.kaem here)" >&2
		exit 1
		;;
esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Explode the single archive-command line into one object per line, framed
# by sentinels, in all three inputs -- see the header above for why. The
# sentinels are identical, constant text in every input, so they are never
# themselves part of a conflict hunk; only the object lines between them
# can be.
explode() {
	awk '
		BEGIN { prefix = "${CC} -ar rcs lib/libc.a " }
		index($0, prefix) == 1 {
			print "##ARLINE-BEGIN##"
			n = split(substr($0, length(prefix) + 1), tok, " ")
			for (i = 1; i <= n; i++) if (tok[i] != "") print tok[i]
			print "##ARLINE-END##"
			next
		}
		{ print }
	' "$1" >"$2"
}
explode "$ancestor" "$work/O"
explode "$current" "$work/A"
explode "$other" "$work/B"

# Plain (non-diff3) 3-way merge: on conflict this leaves
# "<<<<<<< ... ======= ... >>>>>>>" hunks in the output; on a clean merge
# (a change that touches only one side, or edits landing on disjoint
# lines) there is nothing left for the awk pass below to do. Exit status
# is the conflict count, not pass/fail, so it is captured rather than
# trusted directly.
mfec=0
git merge-file -p -L "current ($path)" -L "ancestor" -L "other" \
	"$work/A" "$work/O" "$work/B" >"$work/merged" 2>"$work/stderr" || mfec=$?

if [ "$mfec" -lt 0 ]; then
	echo "merge-kaem.sh: git merge-file itself failed on '$path':" >&2
	cat "$work/stderr" >&2
	exit 1
fi

# Resolve every remaining hunk (there is nothing to do here at all if
# mfec was already 0) and recollapse the exploded archive line back into
# a single line, in one pass. A hunk this driver does not recognize --
# anything other than exactly one line added on each side -- is passed
# through as git's own conflict-marker text and clears `ok`, so the
# driver reports failure and a human sees a normal conflict; it never
# guesses at an unfamiliar shape.
awk '
	function flush_hunk(   a, b, i) {
		if (ours_n == 1 && theirs_n == 1) {
			a = ours[1]; b = theirs[1]
			if (a == b) { print a; return }
			# Two independent single-line insertions landing at the same
			# point in an already-sorted list (a new compile command, a
			# new mkdir line, or -- inside an exploded archive line -- a
			# new object): keep both, ordered against each other the same
			# way the surrounding list already is.
			if (a < b) { print a; print b } else { print b; print a }
			return
		}
		ok = 0
		print "<<<<<<< " ours_label
		for (i = 1; i <= ours_n; i++) print ours[i]
		print "======="
		for (i = 1; i <= theirs_n; i++) print theirs[i]
		print ">>>>>>> " theirs_label
	}
	BEGIN { state = "normal"; ok = 1; inar = 0; arline = "" }
	/^<<<<<<< / { state = "ours"; ours_n = 0; theirs_n = 0; ours_label = substr($0, 9); next }
	state == "ours" && /^=======$/ { state = "theirs"; next }
	state == "theirs" && /^>>>>>>> / { theirs_label = substr($0, 9); flush_hunk(); state = "normal"; next }
	state == "ours" { ours[++ours_n] = $0; next }
	state == "theirs" { theirs[++theirs_n] = $0; next }
	# Recollapse: only reachable in state=="normal", so never interferes
	# with hunk collection above.
	/^##ARLINE-BEGIN##$/ { inar = 1; arline = "${CC} -ar rcs lib/libc.a "; next }
	/^##ARLINE-END##$/ { print arline; inar = 0; next }
	inar { arline = arline (arline == "${CC} -ar rcs lib/libc.a " ? "" : " ") $0; next }
	{ print }
	END { exit ok ? 0 : 1 }
' "$work/merged" >"$work/resolved" 2>"$work/awk-stderr" && resolve_status=0 || resolve_status=$?

# $work/resolved is written unconditionally above (the exit status only
# says whether every hunk was a recognized shape), and it already has any
# unrecognized hunk's original marker text passed straight through and
# the archive line recollapsed around it -- there is nothing more to
# reconstruct on failure, just report it and copy the same file.
cp "$work/resolved" "$current"

if [ "$resolve_status" -ne 0 ] || [ -s "$work/awk-stderr" ]; then
	cat "$work/awk-stderr" >&2 2>/dev/null || true
	echo "merge-kaem.sh: '$path' has a conflict hunk this driver does not" >&2
	echo "merge-kaem.sh: recognize (something other than two independent" >&2
	echo "merge-kaem.sh: single-line insertions) -- leaving it conflicted" >&2
	echo "merge-kaem.sh: for a human." >&2
	exit 1
fi

echo "merge-kaem.sh: resolved $path (add/add hunks reconciled from \$O/\$A/\$B)" >&2
