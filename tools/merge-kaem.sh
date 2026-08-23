#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# merge-kaem.sh -- git merge driver for boot/kaem/build-*.kaem.
#
# boot/kaem/build-i386.kaem and build-x86_64.kaem are generated (by
# tools/gen-kaem.sh, driven by `make kaem`) from the Makefile's own source
# file list, and committed. That means any two branches that each add a
# source file conflict textually in these two files -- a three-way merge
# has no way to know that the "right" result is neither side verbatim but
# a fresh regeneration containing *both* additions. Left to git's default
# text merge, this either produces a conflict a human resolves by hand
# (always the same way: pick a side, `make kaem`, `git add`) or, worse,
# gets resolved in a hurry with conflict markers accidentally committed
# straight into the generated bootstrap script -- which then sits broken
# until a from-scratch kaem bootstrap actually runs it. See
# CONTRIBUTING.md and .githooks/pre-commit (which now refuses staged
# conflict markers for exactly this reason).
#
# This driver replaces the text merge for these two files with: regenerate
# from the merged tree, and let the regenerated content stand in as the
# resolution. It is registered as a git merge driver (see below for how to
# enable it) and applies during `git merge`, `git rebase` and
# `git cherry-pick` alike -- anywhere git performs a three-way content
# merge on a path matched by .gitattributes.
#
# git invokes a merge driver as:
#   driver %O %A %B %P
# with %O/%A/%B temp files holding the ancestor/current/other blobs (%A is
# also where the resolved result must end up) and %P the real path being
# merged (boot/kaem/build-i386.kaem or build-x86_64.kaem, worktree-
# relative). We don't need %O or %B at all: the whole point is that the
# correct output is a pure function of the *current worktree tree* (which
# by the time the driver runs already has every other file's conflicts
# resolved or merged, and %A/%B/%O's siblings checked out), not of any
# textual combination of the three kaem blobs. So the driver ignores its
# content entirely and asks tools/gen-kaem.sh to regenerate from scratch.
#
# gen-kaem.sh, by design, always writes straight to
# boot/kaem/build-$ARCH.kaem in the worktree (see its own header for why:
# the committed script must be a pure function of the source tree, not of
# whatever config.mak last had). It has no way to target an arbitrary
# output path... except that it does, via its optional second positional
# argument (`gen-kaem.sh --arch=ARCH [out]`). We use that to write directly
# to %A, so nothing here ever touches the real boot/kaem/build-*.kaem path
# except through git's own checkout of the merge result -- there is no
# window where a half-regenerated file sits at its real path.
#
# The arch comes from %P, not from any of %O/%A/%B's content (which this
# driver never reads) and not by re-deriving it from config.mak (which has
# only one ARCH, not one per file): boot/kaem/build-i386.kaem must become
# `--arch=i386`, build-x86_64.kaem must become `--arch=x86_64`, and mixing
# that mapping up -- writing i386's output into the x86_64 slot or vice
# versa -- would be silently wrong, not loudly wrong: both are valid kaem
# scripts, just for the other CPU. %P is matched with a glob anchored to
# the exact committed filename shape (boot/kaem/build-ARCH.kaem) precisely
# so a rename or a new unrelated file under boot/kaem/ fails loudly here
# instead of being regenerated as whatever arch happens to fall out of a
# looser pattern.
#
# `make kaem` (and hence gen-kaem.sh with no --arch) regenerates *every*
# arch in one run, so a conflict touching both build-i386.kaem and
# build-x86_64.kaem invokes this driver twice, once per path -- each
# invocation still only regenerates its own single arch (via --arch=ARCH),
# so the two runs can't race or half-write each other's output; the only
# cost is gen-kaem.sh's `make -n -B` dry run running twice instead of
# being shared.
#
# gen-kaem.sh requires config.mak (for CFLAGS_C99FSE/CFLAGS_AUTO/KERNEL32 --
# see its own header for why the compiler name itself is not read from
# there). A checkout that has never run ./configure has no config.mak, and
# in that state this driver has no way to regenerate anything: rather than
# guess or emit something plausible, it fails (non-zero exit) and leaves
# the path conflicted, exactly like git's default text merge would, so a
# human resolves it by hand (configure the tree, then `make kaem`).
#
# Exit status is how git knows what happened: 0 means %A now holds the
# resolved content and the path is clean; non-zero leaves the path
# conflicted for a human, same as if no driver were registered at all. A
# driver that silently wrote a wrong or stale bootstrap script on failure
# would be worse than no driver.
#
# This script alone does nothing -- git does not read merge driver
# *behavior* from the repository, only the .gitattributes *association*
# between a path pattern and a driver name. The actual
# `merge.<name>.driver` command line is per-clone config, same as
# core.hooksPath for .githooks: see ./configure, which sets both with one
# `git config` block.  To enable by hand in an already-configured clone:
#   git config merge.ntlibc-kaem.driver 'tools/merge-kaem.sh %O %A %B %P'

set -eu

ancestor=$1
current=$2
other=$3
path=$4

cd "$(git rev-parse --show-toplevel)"

case $path in
	boot/kaem/build-i386.kaem)   arch=i386 ;;
	boot/kaem/build-x86_64.kaem) arch=x86_64 ;;
	*)
		echo "merge-kaem.sh: don't know how to derive an arch from '$path'" >&2
		echo "merge-kaem.sh: (.gitattributes should only ever route" >&2
		echo "merge-kaem.sh: boot/kaem/build-i386.kaem and build-x86_64.kaem here)" >&2
		exit 1
		;;
esac

if [ ! -f config.mak ]; then
	echo "merge-kaem.sh: config.mak not found -- can't regenerate" >&2
	echo "merge-kaem.sh: boot/kaem/build-$arch.kaem without CC/ARCH." >&2
	echo "merge-kaem.sh: run ./configure first, e.g.:" >&2
	echo "merge-kaem.sh:   ./configure --host=$arch-win32 CC=$arch-win32-tcc" >&2
	echo "merge-kaem.sh: then re-run the merge/rebase/cherry-pick, or" >&2
	echo "merge-kaem.sh: resolve '$path' by hand and 'git add' it." >&2
	exit 1
fi

# %O and %B (ancestor and current) are deliberately unused -- see the
# header above.  Silence shellcheck's unused-variable warning for them.
: "$ancestor" "$other"

if ! ./tools/gen-kaem.sh --arch="$arch" "$current"; then
	echo "merge-kaem.sh: ./tools/gen-kaem.sh --arch=$arch failed; leaving" >&2
	echo "merge-kaem.sh: '$path' conflicted." >&2
	exit 1
fi

echo "merge-kaem.sh: regenerated $path from the merged tree" >&2
