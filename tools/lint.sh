#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# lint.sh -- opt-in static checking for ntlibc.
#
# This is deliberately NOT part of `make all` or `make check`.  The library
# itself is built with tcc, which accepts a great deal that gcc and clang
# diagnose; this script runs the checks tcc cannot, using whichever of
# gcc/clang/clang-tidy/cppcheck/shellcheck happen to be installed, and skips
# (loudly) the ones that are not.  Nothing here may ever become a build
# dependency, and nothing compiler-specific may be added to src/ to satisfy
# it -- findings get reported and judged, not blanket-silenced.
#
# Stages, in order:
#   warn      -fsyntax-only builds of the whole library with gcc and clang,
#             for every arch, under a curated warning set (see WARN_FLAGS).
#   analyze   clang's static analyzer (via clang-tidy if present, else
#             `clang --analyze`), which is the stage that finds real
#             uninitialised-value and leak paths rather than style nits.
#   cppcheck  cppcheck --enable=warning,portability, if installed.
#   shell     shellcheck over configure, the git hooks and tools/*.sh.
#
# Usage:
#   tools/lint.sh                 run every stage
#   tools/lint.sh warn analyze    run only the named stages
#
# Environment:
#   LINT_ARCHS=...        arches to check (default: every dir under arch/
#                         except `generic`)
#   LINT_CONVERSION=1     additionally enable -Wconversion -Wsign-conversion.
#                         Off by default: ~50 findings, the large majority of
#                         which are the deliberate `~0777`-style mask idioms
#                         and NT-type narrowings this code is written around.
#                         Worth a periodic read, not worth a gate.
#   LINT_STRICT=0         always exit 0 (report only)
#   LINT_ALLOW_MISSING=1  skip a stage whose tool is absent instead of
#                         failing.  Off by default: a stage that cannot
#                         run is a failure, because a silent skip makes a
#                         local 'no findings' mean less than CI's -- which
#                         is how the shellcheck, cppcheck and clang-tidy
#                         backlogs each went unseen for a while.
#
# Exit status is 1 if any stage produced findings, so this can be used as a
# gate once the current backlog is dealt with.  It does not pass today; see
# the report in the commit that added this file.
#

set -u

# `CDPATH=` is an assignment prefixing the `cd`, not a botched assignment.
# shellcheck disable=SC1007
srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$srcdir" || exit 1

builddir=obj/lint
: "${LINT_CONVERSION:=0}"
: "${LINT_STRICT:=1}"
: "${LINT_ALLOW_MISSING:=0}"
missing=0

# Every arch/ subdirectory except the generic fallback header tree.
if [ -z "${LINT_ARCHS:-}" ]; then
	LINT_ARCHS=
	for d in arch/*/; do
		a=${d%/}; a=${a#arch/}
		[ "$a" = generic ] && continue
		LINT_ARCHS="$LINT_ARCHS $a"
	done
fi

findings=0
note() { printf '%s\n' "$*"; }
hdr() { printf '\n=== %s ===\n' "$*"; }

# A stage whose tool is absent is a *failure*, not a pass.  Silently
# degrading is how three stages went untriaged for weeks: shellcheck and
# cppcheck simply never ran here, and the analyzer quietly fell back to
# `clang --analyze`, which runs none of the bugprone-*/cert-* checks --
# so a local "no findings" meant "the checks you care about did not
# run".  CI has all of them, so a green local run has to mean the same
# thing CI means.  LINT_ALLOW_MISSING=1 restores the old behaviour for
# anyone who genuinely cannot install one.
require_tool() {
	command -v "$1" >/dev/null 2>&1 && return 0
	if [ "$LINT_ALLOW_MISSING" = 1 ]; then
		note "SKIP: $1 not installed (LINT_ALLOW_MISSING=1)"
		return 1
	fi
	note "MISSING: $1 is not installed, so this stage cannot run."
	note "  install it, or set LINT_ALLOW_MISSING=1 to skip it and accept"
	note "  that this run checks less than CI does."
	missing=1
	return 1
}

#
# The warning set.  -Wall -Wextra plus the checks that actually mean
# something for a freestanding libc.  Two families are deliberately absent:
#
#   -Wcast-qual        ~50 hits, essentially all of them the const-stripping
#                      return that C requires of strchr/memchr/strstr and
#                      friends.  Unfixable by construction.
#   -Wconversion       see LINT_CONVERSION above.
#
# The project's own CFLAGS_AUTO carries -Wno-unused-function; keep it, so
# lint does not disagree with the build about static inline helpers.
#
WARN_FLAGS="-Wall -Wextra -Wno-unused-function \
-Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition \
-Wvla -Wpointer-arith -Wwrite-strings -Wundef"
[ "$LINT_CONVERSION" = 1 ] && WARN_FLAGS="$WARN_FLAGS -Wconversion -Wsign-conversion"

# bits/alltypes.h for an arch, assembled exactly as the Makefile does it, so
# lint can check an arch the tree is not currently configured for.
gen_alltypes() {
	mkdir -p "$builddir/$1/include/bits" || return 1
	cat "arch/$1/bits/alltypes.h.gen" include/alltypes.h.gen \
		> "$builddir/$1/include/bits/alltypes.h"
}

cppflags_for() {
	echo "-std=c99 -nostdinc -fno-builtin -D_XOPEN_SOURCE=700 -D_ALL_SOURCE -D_NTLIBC_INTERNAL" \
	     "-Iarch/$1 -Iarch/generic -I$builddir/$1/include -Iinclude -Isrc/internal"
}

# The same source set the Makefile builds: base sources, minus any that an
# arch/ subdirectory of the same module overrides, plus the arch sources.
sources_for() {
	arch=$1
	for f in src/*/*.c crt/*.c arch/"$arch"/src/*.c src/*/"$arch"/*.c; do
		[ -e "$f" ] || continue
		case $f in
		src/*/*.c)
			# skip a base source that arch/ replaces
			d=${f%/*}; b=${f##*/}
			[ -e "$d/$arch/$b" ] && [ "${d##*/}" != "$arch" ] && continue
			;;
		esac
		echo "$f"
	done
}

# Getting the target ABI right matters here: both win32 targets are
# ILP32/LLP64 (`long` is 32 bits on each), whereas native Linux x86_64 is
# LP64.  A native pass with the wrong `long` still catches most of what we
# care about, but it is not the same compile, so prefer, in order:
#
#   1. clang --target=<triple>, which needs no cross toolchain installed at
#      all, because -nostdinc means we never touch the target's headers;
#   2. an installed mingw-w64 gcc;
#   3. the native compiler with -m32/-m64, with a printed caveat.
#
triple_for() {
	case $1 in
	i386)   echo i686-w64-mingw32 ;;
	x86_64) echo x86_64-w64-mingw32 ;;
	esac
}

# --target=<triple> for clang; empty if the arch has no known triple.
pick_target() {
	t=$(triple_for "$1")
	[ -n "$t" ] && echo "--target=$t"
}

pick_cc() {
	base=$1 arch=$2
	triple=$(triple_for "$arch")
	case $arch in i386) bits=-m32 ;; *) bits=-m64 ;; esac
	if [ "$base" = clang ] && [ -n "$triple" ] && command -v clang >/dev/null 2>&1; then
		echo "clang --target=$triple"
	elif [ -n "$triple" ] && command -v "$triple-$base" >/dev/null 2>&1; then
		echo "$triple-$base"
	elif command -v "$base" >/dev/null 2>&1; then
		echo "$base $bits"
	fi
}

stage_warn() {
	hdr "warning build"
	any=0
	for arch in $LINT_ARCHS; do
		gen_alltypes "$arch" || { note "cannot generate alltypes for $arch"; continue; }
		flags=$(cppflags_for "$arch")
		srcs=$(sources_for "$arch")
		for base in gcc clang; do
			cc=$(pick_cc "$base" "$arch")
			[ -n "$cc" ] || { note "SKIP $base ($arch): not installed"; continue; }
			case $cc in
			*-w64-mingw32*) ;;
			*) note "note: $cc targets the host, not $arch-win32;" \
				"install a mingw-w64 gcc for an ABI-faithful pass" ;;
			esac
			out=$builddir/$arch.$base.log
			: > "$out"
			for f in $srcs; do
				# shellcheck disable=SC2086
				$cc -fsyntax-only $flags $WARN_FLAGS "$f" 2>> "$out"
			done
			# Header diagnostics repeat once per translation unit; collapse
			# them, and drop the source-quote/caret lines gcc interleaves.
			n=$(grep -E '(warning|error):' "$out" | sed 's/^ *//' | sort -u \
				| tee "$out.uniq" | wc -l)
			note "$cc [$arch]: $n unique diagnostic(s) -> $out.uniq"
			[ "$n" -gt 0 ] && any=1
		done
	done
	return $any
}

stage_analyze() {
	hdr "static analyzer"
	require_tool clang || return $missing
	any=0
	# CLANG_TIDY lets a caller (CI) pin an exact binary/version -- clang-tidy's
	# findings vary release to release (newer LLVM adds checks under the
	# families this project enables), so an unpinned `command -v clang-tidy`
	# is a gate that can flip red on a toolchain image bump alone.
	: "${CLANG_TIDY:=clang-tidy}"
	require_tool "$CLANG_TIDY" || [ "$LINT_ALLOW_MISSING" = 1 ] || return 1
	tidy=$(command -v "$CLANG_TIDY" 2>/dev/null || true)
	for arch in $LINT_ARCHS; do
		gen_alltypes "$arch" || continue
		flags=$(cppflags_for "$arch")
		out=$builddir/$arch.analyze.log
		: > "$out"
		for f in $(sources_for "$arch"); do
			if [ -n "$tidy" ]; then
				# .clang-tidy at the tree root supplies the check list.
				# pick_target prints either nothing or one flag.
				# shellcheck disable=SC2046,SC2086
				"$tidy" --quiet "$f" -- $(pick_target "$arch") $flags \
					>> "$out" 2>/dev/null
			else
				# shellcheck disable=SC2086
				# shellcheck disable=SC2046
				clang $(pick_target "$arch") --analyze \
					-Xanalyzer -analyzer-output=text \
					$flags -o /dev/null "$f" >> "$out" 2>&1
			fi
		done
		[ -n "$tidy" ] || note "note: clang-tidy not installed; using \`clang --analyze\`," \
			"which runs clang-analyzer-* but none of the bugprone-*/cert-* checks"
		n=$(grep -E '(warning|error):' "$out" | sed 's/^ *//' | sort -u \
			| tee "$out.uniq" | wc -l)
		note "analyzer [$arch]: $n unique finding(s) -> $out.uniq"
		[ "$n" -gt 0 ] && any=1
	done
	return $any
}

stage_cppcheck() {
	hdr "cppcheck"
	require_tool cppcheck || return $missing
	any=0
	for arch in $LINT_ARCHS; do
		gen_alltypes "$arch" || continue
		out=$builddir/$arch.cppcheck.log
		# shellcheck disable=SC2046,SC2086
		cppcheck --quiet --enable=warning,portability --std=c99 --force \
			--inline-suppr --suppressions-list=tools/cppcheck-suppressions.txt \
			--error-exitcode=0 \
			-DNTLIBC_LINT=1 -D_XOPEN_SOURCE=700 -D_ALL_SOURCE -D_NTLIBC_INTERNAL \
			-Iarch/"$arch" -Iarch/generic -I"$builddir/$arch/include" \
			-Iinclude -Isrc/internal \
			$(sources_for "$arch") > "$out" 2>&1
		n=$(grep -c . "$out")
		note "cppcheck [$arch]: $n line(s) -> $out"
		[ "$n" -gt 0 ] && any=1
	done
	return $any
}

stage_shell() {
	hdr "shellcheck"
	require_tool shellcheck || return $missing
	out=$builddir/shellcheck.log
	mkdir -p "$builddir"
	# No -s: these scripts are a deliberate mix of #!/bin/sh (configure,
	# the hook, install.sh, runtests.sh, this file) and #!/usr/bin/env bash
	# (the two generators), and forcing one dialect would report the other
	# half's perfectly valid syntax as errors.  shellcheck reads the
	# shebangs itself.
	shellcheck configure .githooks/pre-commit tools/*.sh > "$out" 2>&1
	rc=$?
	n=$(grep -cE '^In .* line [0-9]+:' "$out")
	note "shellcheck: $n finding(s) -> $out"
	[ "$rc" -eq 0 ] && return 0
	return 1
}

stages=${*:-warn analyze cppcheck shell}
mkdir -p "$builddir" || exit 1
for s in $stages; do
	case $s in
	warn)     stage_warn     || findings=1 ;;
	analyze)  stage_analyze  || findings=1 ;;
	cppcheck) stage_cppcheck || findings=1 ;;
	shell)    stage_shell    || findings=1 ;;
	ushort)   tools/lint-ushort.sh || findings=1 ;;
	*) note "unknown stage: $s"; exit 2 ;;
	esac
done

hdr "summary"
if [ "$missing" -ne 0 ]; then
	note "one or more stages could not run because a tool is missing."
	note "this run checked less than CI does, so it cannot report success."
	exit 2
fi
if [ "$findings" -eq 0 ]; then
	note "no findings"
	exit 0
fi
note "findings above; logs under $builddir/"
[ "$LINT_STRICT" = 0 ] && exit 0
exit 1
