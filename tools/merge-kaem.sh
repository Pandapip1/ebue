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
# are never in-flight. So instead of asking the live tree what the
# answer should be, this driver reruns `git merge-file` (plain diff3) on
# those three blobs to get git's own conflict hunks, and then resolves
# each hunk using what is known about gen-kaem.sh's output shape:
#
#   - a hunk that is exactly one line added on each side, neither of
#     which is the `${CC} -ar rcs lib/libc.a ...` line, is a plain add/add
#     ambiguity from two independent single-line insertions (a new
#     compile command, or a new `mkdir` line) at the same point in an
#     already-sorted list. Emitting both lines, ordered against each
#     other the same way the file as a whole is already sorted, is
#     exactly what a fresh gen-kaem.sh run would have produced.
#   - a hunk that is exactly one line on each side and *both* lines match
#     the archive command is the one genuinely special case: rather than
#     text-merge that single monster line, split each side's object list
#     into tokens, do the token-level three-way union (present unless
#     removed by a side that had it and the other side didn't add it
#     back), and rebuild the one archive line with a byte-sorted
#     (LC_ALL=C, matching GNU Make's `$(sort ...)`, which is what
#     produced the token order in the first place) object list.
#   - anything else -- multi-line sides, a hunk this driver cannot
#     confidently classify, anything at all outside those two known
#     shapes -- is left as git's own conflict-marker output and the
#     driver exits non-zero. Guessing at an unfamiliar hunk shape is
#     exactly the "plausible but wrong" mistake this driver exists to
#     rule out; a shape it does not recognize is a real conflict for a
#     human, the same as if this driver were not registered at all.
#
# This needs no source tree, no compiler, and no config.mak: it is a
# pure function of the three blobs git already handed it, so it cannot
# be fooled by the live tree's state, and it produces the identical
# output a correct fresh regeneration would, without ever needing one.
#
# %P is only used to sanity-check that this driver is only ever being
# asked about the two files it knows how to reason about (.gitattributes
# should never route anything else here); the regenerated content itself
# does not depend on the arch at all, since the merge operates purely on
# the three files' own text.
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

# Plain (non-diff3) 3-way merge: %A becomes "<<<<<<< ... ======= ... >>>>>>>"
# hunks on conflict, or the clean merge outright if there is one (a hunk
# that touches only one of the two files, or edits that land on disjoint
# lines, can merge with no conflict at all -- nothing below needs to run
# in that case). Exit status is the conflict count, not pass/fail, so it
# is captured rather than trusted directly.
mfec=0
git merge-file -p "$current" "$ancestor" "$other" >"$work/merged" 2>"$work/stderr" || mfec=$?

if [ "$mfec" -eq 0 ]; then
	cp "$work/merged" "$current"
	echo "merge-kaem.sh: $path merged cleanly (no add/add ambiguity)" >&2
	exit 0
fi

if [ "$mfec" -lt 0 ]; then
	echo "merge-kaem.sh: git merge-file itself failed on '$path':" >&2
	cat "$work/stderr" >&2
	exit 1
fi

# Resolve each conflict hunk against the two known shapes gen-kaem.sh's
# output can produce a conflict in (see the header above). Anything else
# is left alone -- ok is cleared and the original marked-up hunk is
# copied straight through, so a shape this driver does not recognize
# still ends up in %A exactly as git's own merge would have left it: a
# normal conflict, for a human, not a guess.
awk -v tokfile="$work/artoks" -v sortfile="$work/artoks.sorted" '
	function is_ar(line) { return line ~ /^\$\{CC\} -ar rcs lib\/libc\.a /}
	function flush_hunk(   a, b, i, seen, tok, merged, prefix, line, first) {
		if (ours_n == 1 && theirs_n == 1) {
			a = ours[1]; b = theirs[1]
			if (a == b) {
				print a
				return
			}
			if (is_ar(a) && is_ar(b)) {
				prefix = "${CC} -ar rcs lib/libc.a "
				split(substr(a, length(prefix) + 1), tok, " ")
				for (i in tok) if (tok[i] != "") seen[tok[i]] = 1
				delete tok
				split(substr(b, length(prefix) + 1), tok, " ")
				for (i in tok) if (tok[i] != "") seen[tok[i]] = 1
				# One token per line into a scratch file, sorted externally
				# with LC_ALL=C (the same byte-sort GNU Make'"'"'s
				# $(sort ...) used to order the tokens in the first place),
				# then read back and rebuilt into one line. Deliberately a
				# real file rather than `print | cmd` / `cmd | getline` on
				# the same command string: those are two unrelated pipes
				# in POSIX awk, not one bidirectional stream, so a sort
				# fed that way never reaches the getline side at all.
				for (i in seen) print i > tokfile
				close(tokfile)
				system("LC_ALL=C sort -u " tokfile " > " sortfile)
				merged = prefix
				first = 1
				while ((getline line < sortfile) > 0) {
					merged = merged (first ? "" : " ") line
					first = 0
				}
				close(sortfile)
				print merged
				return
			}
			# Two independent single-line insertions at the same point
			# (a new compile command or mkdir line): both belong in the
			# output, ordered the same way the surrounding list already
			# is.
			if (a < b) { print a; print b } else { print b; print a }
			return
		}
		# Not a shape this driver recognizes -- pass the original
		# conflict hunk through untouched and flag it.
		ok = 0
		print "<<<<<<< " ours_label
		for (i = 1; i <= ours_n; i++) print ours[i]
		print "======="
		for (i = 1; i <= theirs_n; i++) print theirs[i]
		print ">>>>>>> " theirs_label
	}
	BEGIN { state = "normal"; ok = 1 }
	/^<<<<<<< / { state = "ours"; ours_n = 0; theirs_n = 0; ours_label = substr($0, 9); next }
	state == "ours" && /^=======$/ { state = "theirs"; next }
	state == "theirs" && /^>>>>>>> / { theirs_label = substr($0, 9); flush_hunk(); state = "normal"; next }
	state == "ours" { ours[++ours_n] = $0; next }
	state == "theirs" { theirs[++theirs_n] = $0; next }
	{ print }
	END { exit ok ? 0 : 1 }
' "$work/merged" >"$work/resolved" 2>"$work/awk-stderr" && resolve_status=0 || resolve_status=$?

if [ "$resolve_status" -ne 0 ] || [ -s "$work/awk-stderr" ]; then
	cat "$work/awk-stderr" >&2 2>/dev/null || true
	echo "merge-kaem.sh: '$path' has a conflict hunk this driver does not" >&2
	echo "merge-kaem.sh: recognize (something other than two independent" >&2
	echo "merge-kaem.sh: single-line insertions, or the archive command" >&2
	echo "merge-kaem.sh: line) -- leaving it conflicted for a human." >&2
	cp "$work/merged" "$current"
	exit 1
fi

cp "$work/resolved" "$current"
echo "merge-kaem.sh: resolved $path (add/add hunks reconciled from \$O/\$A/\$B)" >&2
