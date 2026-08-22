#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/gen-alltypes.sh -- expand the compact `TYPEDEF/STRUCT/UNION` DSL in
# every bits/*.h.in through tools/mkalltypes.sed once, at development time,
# and commit the result as a *.h.gen file next to its *.h.in source.
#
# Why pre-expand at all, when the build could just run sed?
#
# Because the kaem bootstrap path (boot/kaem/, see CONTRIBUTING.md) has to
# produce obj/include/bits/alltypes.h at the "right after mes compiles tcc"
# point in a live-bootstrap-style chain, where the only tools that exist
# are kaem itself and mescc-tools-extra's handful of small C programs:
# catm, chmod, cp, match, mkdir, replace, rm, sha256sum, sha3sum, unbz2,
# ungz, untar, unxz, wrap. There is no sed there, and no awk. Nor can the
# expansion be faked with what *is* there: mescc-tools-extra's `replace`
# does literal substring substitution only (replace.c's replace_string()),
# with no capture groups, so it cannot turn
#     TYPEDEF unsigned _Addr size_t;
# into the four-line `__NEED_`/`__DEFINED_` guarded block that
# mkalltypes.sed produces. And a helper compiled on the spot is no way out
# either: the tcc on PATH at that stage is a *cross* compiler emitting
# win32 PE, so anything it builds is not runnable on the build host.
#
# So the expansion is done here, by a developer with a real sed, and the
# bootstrap does the one thing it *can* do -- concatenate whole files with
# catm. Splitting it this way works only because mkalltypes.sed's rules are
# purely per-line (no hold space, no range addresses), so expanding the two
# halves separately and concatenating is byte-identical to concatenating
# and then expanding. The normal Makefile build consumes the very same
# *.h.gen files, so there is exactly one expansion and one code path rather
# than sed-for-make and catm-for-kaem.
#
# The *.h.in files stay the hand-edited source of truth; *.h.gen is a
# generated artifact that happens to be committed, exactly like
# boot/kaem/build-*.kaem. `make alltypes` (or `make generated`) regenerates
# it, and both .githooks/pre-commit and CI regenerate + `git diff
# --exit-code` to make a stale expansion impossible to commit.
#
# Usage:
#   ./tools/gen-alltypes.sh
#
# Unlike tools/gen-kaem.sh this needs no config.mak: it is a pure function
# of the source tree and does every arch, always.

set -euo pipefail

cd "$(dirname "$0")/.."

SED_SCRIPT=tools/mkalltypes.sed

expand() {
	# $1 = foo.h.in -> foo.h.gen
	out=${1%.in}.gen
	sed -f "$SED_SCRIPT" "$1" >"$out"
	echo "gen-alltypes.sh: wrote $out" >&2
}

count=0

# The generic half, shared by every arch.
for f in include/*.h.in; do
	[ -e "$f" ] || continue
	base=${f#include/}
	# Only the ones that are actually a bits/ header's generic half; a
	# stray include/*.h.in with no per-arch counterpart would not be
	# part of this scheme.
	found=""
	for d in arch/*/; do
		[ -e "${d}bits/$base" ] && found=yes
	done
	[ -n "$found" ] || continue
	expand "$f"
	count=$((count + 1))
done

# The per-arch halves. arch/generic/ holds ready-made headers, not DSL
# sources, so it simply has no *.h.in to pick up here.
for f in arch/*/bits/*.h.in; do
	[ -e "$f" ] || continue
	expand "$f"
	count=$((count + 1))
done

if [ "$count" -eq 0 ]; then
	echo "gen-alltypes.sh: found no *.h.in to expand -- has the tree moved?" >&2
	exit 1
fi
