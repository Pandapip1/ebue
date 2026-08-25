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
#   undefined tools/lint-undefined.sh: a public header declaring a
#             function nothing defines.  No tool needed.
#   unreferenced
#             tools/lint-unreferenced.sh: a function a public header
#             declares, this library implements, and no test/*.c
#             references.  Needs a native C compiler and nm.
#   widthmod  tools/lint-widthmod.sh: printf/scanf's `z` and `t` length
#             modifiers read or written as `long`, which is 32 bits on
#             this LLP64 target while size_t and ptrdiff_t are 64.  No
#             tool needed.
#   ushort    tools/lint-ushort.sh: unguarded (USHORT) narrowing casts,
#             the class of the chdir()/UNICODE_STRING.Length bug that
#             script was written for (4f02ef3).  No tool needed.  It was
#             dispatchable here from the day it was added and named in
#             none of the default stage list, tools/gate.sh's ALL_STAGES
#             or ci.yml's lint matrix, so in ~a year it never once ran in
#             a gate -- a bug hunt's own detector, left unwired.
#
# Usage:
#   tools/lint.sh                 run every stage
#   tools/lint.sh warn analyze    run only the named stages
#
# Tool versions matter, and have caught this project out three times: a
# green local run is not a green CI run if the versions differ.  CI (see
# .github/workflows/ci.yml) installs clang-tidy-18 and shellcheck 0.9 from
# ubuntu-24.04's apt; a newer local pair may report neither cppcheck's
# suppression-file syntax complaints, nor clang-analyzer findings that 18
# reports and 21 does not, nor SC2016 in tools/gen-kaem.sh's sed.  To
# reproduce CI's exact toolchain locally:
#
#   nix-shell -p llvmPackages_18.clang-tools \
#     --run 'CLANG_TIDY=clang-tidy tools/lint.sh analyze'
#   nix-shell -I nixpkgs=channel:nixos-23.11 -p shellcheck \
#     --run 'tools/lint.sh shell'
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
#   LINT_TIDY_EXACT=1     require the running clang-tidy's enabled check
#                         set to equal tools/clang-tidy-checks.txt
#                         exactly, rather than merely to be a superset of
#                         its required list.  Set by the two stages whose
#                         purpose is to reproduce one pinned toolchain
#                         (tools/gate.sh's lint-analyze-pinned and
#                         ci.yml's analyze leg); see that file's header.
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

# How many files/stages to run at once. warn/analyze run one process per
# source file (the actual work, e.g. clang-tidy on one TU, dwarfs process
# startup), and the top-level stages are themselves independent, so
# both parallelise for free. LINT_JOBS=1 restores the old fully serial
# behaviour, which is also the safe fallback if nproc/getconf are both
# missing.
: "${LINT_JOBS:=}"
if [ -z "$LINT_JOBS" ]; then
	LINT_JOBS=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
fi

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

# show_findings FILE -- print the findings a stage has just counted.
#
# A count is not something anyone can act on.  These logs are written
# under obj/lint/, which is a build directory: in CI it lives inside the
# runner and is never uploaded, so "-> obj/lint/x86_64.analyze.log.uniq"
# names a file nobody reading the red board can open.  Every stage below
# printed a number and a path to a file that, in the one place the number
# matters, does not exist.  Two separate people asked independently where
# the analyzer's count came from, and neither could answer it from the
# log; a stage that reports a count it cannot explain is a stage nobody
# can act on, and acting on it is the entire purpose.
#
# Bounded, because a stage that has genuinely fallen over emits thousands
# of lines and burying the summary underneath them is its own failure
# mode: the first $LINT_SHOW, then how many were elided and the path that
# still holds all of them.
: "${LINT_SHOW:=40}"
show_findings() {
	[ -s "$1" ] || return 0
	sed "${LINT_SHOW}q" "$1" | sed 's/^/    /'
	_tot=$(grep -c . "$1" || true)
	[ "$_tot" -le "$LINT_SHOW" ] ||
		note "    ... and $((_tot - LINT_SHOW)) more, all of them in $1"
}
hdr() { printf '\n=== %s ===\n' "$*"; }

# A stage whose tool is absent is a *failure*, not a pass.  Silently
# degrading is how three stages went untriaged for weeks: shellcheck and
# cppcheck simply never ran here, and the analyzer quietly fell back to
# `clang --analyze`, which runs none of the bugprone-*/cert-* checks --
# so a local "no findings" meant "the checks you care about did not
# run".  CI has all of them, so a green local run has to mean the same
# thing CI means.  LINT_ALLOW_MISSING=1 restores the old behaviour for
# anyone who genuinely cannot install one.
# report_missing DESCRIPTION -- the "a tool this stage needs is not here"
# path, factored out of require_tool so a stage whose tool is not a
# single `command -v`-able name can take it too.  stage_warn's compiler
# is picked by pick_cc(), which may answer with a mingw-w64 cross gcc, a
# `clang --target=` invocation, or the native compiler with -m32/-m64 --
# no one name to test -- and before this it simply printed "SKIP" and
# carried on, which meant a machine with no C compiler at all passed the
# warning stage *at the default LINT_ALLOW_MISSING=0*.  Always returns 1,
# so callers can `report_missing ... || continue`.
report_missing() {
	if [ "$LINT_ALLOW_MISSING" = 1 ]; then
		note "SKIP: $1 (LINT_ALLOW_MISSING=1)"
		return 1
	fi
	note "MISSING: $1"
	note "  install it, or set LINT_ALLOW_MISSING=1 to skip it and accept"
	note "  that this run checks less than CI does."
	missing=1
	# When the top-level stages run in parallel (see the dispatch loop at
	# the bottom of this file), each one runs in its own subshell, so a
	# plain `missing=1` above only ever changes that subshell's copy --
	# the parent shell's $missing never sees it. LINT_MISSING_MARKER, if
	# set, names a file whose mere existence (one per offending subshell,
	# via $$) the parent checks after `wait` instead.
	[ -n "${LINT_MISSING_MARKER:-}" ] && : > "$LINT_MISSING_MARKER.$$"
	return 1
}

require_tool() {
	command -v "$1" >/dev/null 2>&1 && return 0
	report_missing "$1 is not installed, so this stage cannot run."
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
#
# Idempotent and safe under concurrent callers: with the top-level
# stages now able to run at once (see the dispatch loop below), more than
# one of them can ask to generate the same arch's header at nearly the
# same moment. Skipping when the destination already exists avoids
# redoing the (cheap but non-zero) work every time; writing to a temp
# file and `mv`-ing it into place, rather than redirecting straight into
# the destination, means a compiler process that opens the header while a
# second generator is mid-write never sees a truncated file -- `mv` on
# the same filesystem is a single rename, not a byte-by-byte copy.
#
# The skip is conditional on the destination MATCHING what this tree
# would generate, not merely on it existing.  A bare -f cache never
# expires, and obj/ survives a checkout: a local run after any commit
# that touches either alltypes.h.gen went on linting against the
# previous tree's types.  That is not a stale-cache annoyance, it is a gate reporting
# about a tree that is not there -- it produced 83 phantom "incomplete
# definition of type 'struct tm'" errors here, against the 1 real
# finding CI reports, and made the stage look locally unreproducible.
# CI never sees it because CI always starts from an empty checkout,
# which is the worst possible split: the wrong answer appears only where
# someone is trying to reproduce a real one.
#
# Compared, not stat'd.  Mtimes are the wrong instrument twice over: `-nt`
# is not POSIX sh (SC3013, and this file is #!/bin/sh), and a checkout can
# leave a stale file whose mtime is newer than the source it is stale
# against.  The two inputs total 8KB, so generating unconditionally and
# comparing costs nothing measurable, and it keeps the temp-plus-`mv`
# shape the concurrency note above depends on.
gen_alltypes() {
	dest=$builddir/$1/include/bits/alltypes.h
	mkdir -p "$builddir/$1/include/bits" || return 1
	tmp="$dest.$$.tmp"
	cat "arch/$1/bits/alltypes.h.gen" include/alltypes.h.gen > "$tmp" || {
		rm -f "$tmp"; return 1
	}
	if [ -f "$dest" ] && cmp -s "$tmp" "$dest"; then
		rm -f "$tmp"
		return 0
	fi
	mv -f "$tmp" "$dest"
}

cppflags_for() {
	echo "-std=c99 -nostdinc -fno-builtin -D_XOPEN_SOURCE=700 -D_ALL_SOURCE -D_NTLIBC_INTERNAL" \
	     "-Iarch/$1 -Iarch/generic -I$builddir/$1/include -Iinclude -Isrc/internal"
}

# The same source set the Makefile builds: base sources, minus any that an
# arch/ subdirectory of the same module overrides, plus the arch sources --
# plus sh/*.c, the sh(1p) binary. sh/ is not part of libc.a (it is a
# program, see the Makefile's SH_SRCS comment), but it is first-party C in
# this repo built by `make all`, and nothing else in these gates compiles
# it with a warning set: leaving it out would mean the one deliverable a
# user actually runs is the one file gcc/clang/cppcheck never look at.
sources_for() {
	arch=$1
	for f in src/*/*.c crt/*.c sh/*.c arch/"$arch"/src/*.c src/*/"$arch"/*.c; do
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
	# How many (arch, compiler) passes actually happened.  Checked at the
	# bottom: a warning stage that ran none of them has diagnosed nothing,
	# and must not report success just because nothing complained.
	passes=0
	for arch in $LINT_ARCHS; do
		gen_alltypes "$arch" || { note "cannot generate alltypes for $arch"; any=1; continue; }
		flags=$(cppflags_for "$arch")
		srcs=$(sources_for "$arch")
		nsrc=$(printf '%s\n' "$srcs" | grep -c . || true)
		for base in gcc clang; do
			cc=$(pick_cc "$base" "$arch")
			# Absent tool, not "nothing to do": this used to print SKIP
			# and continue with $any still 0, so a machine with neither
			# gcc nor clang passed this stage outright.  Route it through
			# the same LINT_ALLOW_MISSING decision every other stage uses.
			[ -n "$cc" ] || { report_missing "no usable $base for $arch (tried a mingw-w64 $base, clang --target=, and plain $base)"; continue; }
			case $cc in
			*-w64-mingw32*) ;;
			*) note "note: $cc targets the host, not $arch-win32;" \
				"install a mingw-w64 gcc for an ABI-faithful pass" ;;
			esac
			out=$builddir/$arch.$base.log
			: > "$out"
			# One `-fsyntax-only` process per file, up to LINT_JOBS at
			# once: each is independent (no shared state but the source
			# tree, which this only reads), so this is embarrassingly
			# parallel. Every worker writes its own file under $pardir
			# (named for its source path, slashes flattened) instead of
			# appending to $out directly -- concurrent appends from
			# separate processes to one fd are not guaranteed atomic
			# once a diagnostic exceeds a pipe-buffer-sized write, which
			# would otherwise interleave two files' output mid-line.
			# shellcheck disable=SC2086
			set -- $cc
			cc_prog=$1; cc_extra=${2:-}
			pardir=$(mktemp -d "$builddir/warn.XXXXXX") || return 1
			# $srcs and $flags/$WARN_FLAGS are meant to word-split here --
			# one xargs input line per source file, and each flag as its
			# own argument to the per-file sh -c below. $pardir inside the
			# single-quoted script is the closing-quote/reopening-quote
			# trick (not a mistake shellcheck should expand): it is
			# spliced in by *this* shell so the child script -- which
			# genuinely must not expand $id itself until it runs -- gets
			# a literal path.
			# shellcheck disable=SC2086,SC2016
			printf '%s\n' $srcs | xargs -P "$LINT_JOBS" -I{} sh -c '
				f=$1; prog=$2; extra=$3; shift 3
				id=$(printf %s "$f" | tr / _)
				# shellcheck disable=SC2086
				"$prog" $extra -fsyntax-only "$@" "$f" \
					> "'"$pardir"'/$id.log" 2>&1
			' _ {} "$cc_prog" "$cc_extra" $flags $WARN_FLAGS
			# One log per source file, whether or not that file had
			# anything to say -- so counting them is how this stage knows
			# a compile was really attempted for every source.  Without
			# it, "no diagnostics" and "no compiles" are the same output:
			# an xargs whose input went empty (sources_for matching
			# nothing after a directory rename, say) leaves $n at 0 and
			# the old code called that a pass.  The count is taken before
			# $pardir is removed, and compared against the source list
			# rather than merely against zero, so a partial run -- some
			# files reached, some not -- fails too.
			nlog=$(find "$pardir" -name '*.log' 2>/dev/null | grep -c . || true)
			ls "$pardir"/*.log >/dev/null 2>&1 && cat "$pardir"/*.log > "$out"
			rm -rf "$pardir"
			if [ "$nsrc" -eq 0 ] || [ "$nlog" -ne "$nsrc" ]; then
				note "$cc [$arch]: FAILED -- $nlog of $nsrc source file(s) were compiled."
				note "  the warning build did not cover the source set, so a clean result"
				note "  here would mean nothing.  This is not a findings count of zero."
				any=1
				continue
			fi
			passes=$((passes + 1))
			# Header diagnostics repeat once per translation unit; collapse
			# them, and drop the source-quote/caret lines gcc interleaves.
			n=$(grep -E '(warning|error):' "$out" | sed 's/^ *//' | sort -u \
				| tee "$out.uniq" | wc -l)
			note "$cc [$arch]: $nsrc file(s), $n unique diagnostic(s) -> $out.uniq"
			show_findings "$out.uniq"
			[ "$n" -gt 0 ] && any=1
		done
	done
	if [ "$passes" -eq 0 ] && [ "$LINT_ALLOW_MISSING" != 1 ]; then
		note "warn: FAILED -- no (arch, compiler) pass ran at all; this stage"
		note "  compiled nothing and therefore diagnosed nothing."
		any=1
	fi
	return $any
}

# check_list_ok TIDY -- assert that the clang-tidy binary $1 offers the
# checks tools/clang-tidy-checks.txt says it must.
#
# .clang-tidy at the tree root selects by wildcard (bugprone-*, cert-*,
# clang-analyzer-*, misc-*), and a wildcard resolves to whatever the tool
# in $PATH implements.  So "analyze found 0 findings" is a statement
# about the machine as much as about the code, and the two ways it goes
# wrong are both invisible in that sentence: a tool that offers fewer
# checks than the pin, and the `clang --analyze` fallback below, which
# offers none of them at all.  Measured on the canonical probe
# `long long widen(int a, int b) { return a * b; }`: clang-tidy 18 with
# this tree's .clang-tidy reports bugprone-implicit-widening-of-
# multiplication-result; `clang --analyze` on the same file and target
# reports nothing.  That is defect 5's class.
#
# Run once per stage, not per arch: --list-checks reads .clang-tidy from
# the working directory, which is $srcdir either way.
check_list_ok() {
	tidy=$1
	listfile=tools/clang-tidy-checks.txt
	if [ ! -f "$listfile" ]; then
		note "analyze: FAILED -- $listfile is missing, so there is nothing to hold"
		note "  the running clang-tidy to.  A wildcard check list with no committed"
		note "  expectation is not a check; it is whatever the machine has."
		return 1
	fi
	req=$(mktemp "$builddir/checks.req.XXXXXX") || return 1
	exempt=$(mktemp "$builddir/checks.exempt.XXXXXX") || return 1
	got=$(mktemp "$builddir/checks.got.XXXXXX") || return 1
	sed -e 's/[[:space:]]*$//' "$listfile" \
		| grep -v -e '^#' -e '^[[:space:]]*$' > "$req.all"
	grep -v '^-' "$req.all" | sort -u > "$req"
	sed -n 's/^-//p' "$req.all" | sort -u > "$exempt"
	rm -f "$req.all"
	"$tidy" --list-checks 2>/dev/null | sed -n 's/^    //p' | sort -u > "$got"

	nreq=$(grep -c . "$req" || true)
	ngot=$(grep -c . "$got" || true)
	if [ "$nreq" -eq 0 ]; then
		note "analyze: FAILED -- $listfile names no required checks."
		note "  An empty expectation is satisfied by any tool, including one that"
		note "  offers nothing at all."
		rm -f "$req" "$exempt" "$got"
		return 1
	fi
	rc=0
	nmiss=$(comm -23 "$req" "$got" | grep -c . || true)
	if [ "$nmiss" -ne 0 ]; then
		note "analyze: FAILED -- $tidy offers $ngot check(s) and is missing $nmiss that"
		note "  $listfile requires:"
		comm -23 "$req" "$got" | sed 's/^/    /'
		note "  A tool that offers fewer checks reports the same '0 findings' as one"
		note "  that offers all of them.  Install the pinned clang-tidy, or move the"
		note "  check to the exempt list in $listfile with the reason, in this commit."
		rc=1
	fi
	nextra=$(comm -13 "$req" "$got" | grep -vxF -f "$exempt" 2>/dev/null | grep -c . || true)
	if [ "${LINT_TIDY_EXACT:-0}" = 1 ]; then
		nmissex=$(comm -23 "$exempt" "$got" | grep -c . || true)
		if [ "$nextra" -ne 0 ] || [ "$nmissex" -ne 0 ]; then
			note "analyze: FAILED -- LINT_TIDY_EXACT=1, but $tidy's enabled set is not"
			note "  exactly what $listfile records.  This stage exists to reproduce one"
			note "  pinned toolchain; a difference here means the pin is not pinning."
			comm -13 "$req" "$got" | grep -vxF -f "$exempt" 2>/dev/null \
				| sed 's/^/    only in the tool: /'
			comm -23 "$exempt" "$got" | sed 's/^/    only in the file: /'
			rc=1
		fi
	fi
	[ "$rc" -eq 0 ] && note "analyze: $tidy offers $ngot check(s); all $nreq required present ($nextra newer than the pin)."
	rm -f "$req" "$exempt" "$got"
	return $rc
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
	if [ -n "$tidy" ]; then
		check_list_ok "$tidy" || any=1
	else
		# The fallback is no longer allowed to be silent about what it
		# is not running.  It cannot reach here at all unless
		# LINT_ALLOW_MISSING=1 (see require_tool above), so this is a
		# run whose caller has accepted a weaker check -- but "weaker"
		# has to be a number, not an adjective.
		nreq=$(grep -v -e '^#' -e '^-' -e '^[[:space:]]*$' \
			tools/clang-tidy-checks.txt 2>/dev/null | grep -c . || true)
		note "analyze: clang --analyze offers 0 of the $nreq check(s)"
		note "  tools/clang-tidy-checks.txt requires: it runs the clang-analyzer"
		note "  engine only, and none of bugprone-*, cert-* or misc-*.  This run"
		note "  cannot catch anything those families catch, whatever it reports."
	fi
	# The three exemptions in tools/clang-tidy-checks.txt are excused on
	# one condition -- that this stage compiles no C++ -- and a condition
	# that is only written down is a condition that stops being true
	# quietly.  Assert it against the source set actually being fed in.
	for arch in $LINT_ARCHS; do
		cxx=$(sources_for "$arch" | grep -E '\.(cc|cpp|cxx|C)$' || true)
		if [ -n "$cxx" ]; then
			note "analyze: FAILED -- this stage is being handed C++ sources:"
			printf '%s\n' "$cxx" | sed 's/^/    /'
			note "  The three exemptions in tools/clang-tidy-checks.txt are excused"
			note "  solely because no C++ reaches clang-tidy here.  That is no longer"
			note "  true, so they must be re-justified or removed before this passes."
			any=1
		fi
		break
	done
	analyzed=0
	for arch in $LINT_ARCHS; do
		gen_alltypes "$arch" || { note "cannot generate alltypes for $arch"; any=1; continue; }
		flags=$(cppflags_for "$arch")
		nsrc=$(sources_for "$arch" | grep -c . || true)
		out=$builddir/$arch.analyze.log
		: > "$out"
		target=$(pick_target "$arch")
		# One process per source file, up to LINT_JOBS at once -- this is
		# the single most expensive stage (clang-tidy's checks dwarf
		# process startup), so it is the one parallelising this way
		# matters most for. Same per-worker-file-then-cat approach as
		# stage_warn, for the same reason (no interleaved writes).
		pardir=$(mktemp -d "$builddir/analyze.XXXXXX") || return 1
		# See stage_warn's comment above on $flags word-splitting and the
		# $pardir close/reopen-quote splice -- same reasoning applies here.
		if [ -n "$tidy" ]; then
			# .clang-tidy at the tree root supplies the check list.
			# shellcheck disable=SC2086,SC2016
			sources_for "$arch" | xargs -P "$LINT_JOBS" -I{} sh -c '
				f=$1; tidy=$2; target=$3; shift 3
				id=$(printf %s "$f" | tr / _)
				# shellcheck disable=SC2086
				"$tidy" --quiet "$f" -- $target "$@" \
					> "'"$pardir"'/$id.log" 2>/dev/null
			' _ {} "$tidy" "$target" $flags
		else
			# shellcheck disable=SC2086,SC2016
			sources_for "$arch" | xargs -P "$LINT_JOBS" -I{} sh -c '
				f=$1; target=$2; shift 2
				id=$(printf %s "$f" | tr / _)
				# shellcheck disable=SC2086
				clang $target --analyze -Xanalyzer -analyzer-output=text \
					"$@" -o /dev/null "$f" \
					> "'"$pardir"'/$id.log" 2>&1
			' _ {} "$target" $flags
			note "note: clang-tidy not installed; using \`clang --analyze\`," \
				"which runs clang-analyzer-* but none of the bugprone-*/cert-* checks"
		fi
		# Same floor as stage_warn's, for the same reason and the same
		# line of code: one log per source file is written regardless of
		# whether that file had a finding, so "no logs" and "no findings"
		# produced identical output and the old `ls ... && cat` treated
		# both as a pass.  Count the logs against the source list before
		# $pardir goes away.
		nlog=$(find "$pardir" -name '*.log' 2>/dev/null | grep -c . || true)
		ls "$pardir"/*.log >/dev/null 2>&1 && cat "$pardir"/*.log > "$out"
		rm -rf "$pardir"
		if [ "$nsrc" -eq 0 ] || [ "$nlog" -ne "$nsrc" ]; then
			note "analyzer [$arch]: FAILED -- $nlog of $nsrc source file(s) were analyzed."
			note "  the analyzer did not cover the source set, so a clean result here"
			note "  would mean nothing.  This is not a findings count of zero."
			any=1
			continue
		fi
		analyzed=$((analyzed + 1))
		n=$(grep -E '(warning|error):' "$out" | sed 's/^ *//' | sort -u \
			| tee "$out.uniq" | wc -l)
		note "analyzer [$arch]: $nsrc file(s), $n unique finding(s) -> $out.uniq"
		show_findings "$out.uniq"
		[ "$n" -gt 0 ] && any=1
	done
	if [ "$analyzed" -eq 0 ] && [ "$LINT_ALLOW_MISSING" != 1 ]; then
		note "analyze: FAILED -- no arch was analyzed at all; this stage examined"
		note "  nothing and therefore found nothing."
		any=1
	fi
	return $any
}

stage_cppcheck() {
	hdr "cppcheck"
	require_tool cppcheck || return $missing
	any=0
	# cppcheck 2.13 (what Ubuntu 24.04 ships, and so what CI runs) rejects
	# the comment and blank lines that document why each suppression exists
	# -- "Failed to add suppression. No id." -- while 2.21 accepts them.
	# Keep the documentation in the file and hand cppcheck a stripped copy,
	# so the same tree passes on both.
	suppr=$builddir/cppcheck-suppressions.stripped
	mkdir -p "$builddir"
	sed -e 's/[[:space:]]*#.*//' -e '/^[[:space:]]*$/d' \
		tools/cppcheck-suppressions.txt > "$suppr"
	for arch in $LINT_ARCHS; do
		gen_alltypes "$arch" || continue
		out=$builddir/$arch.cppcheck.log
		# shellcheck disable=SC2046,SC2086
		cppcheck --quiet --enable=warning,portability --std=c99 --max-configs=12 \
			--inline-suppr --suppressions-list="$suppr" \
			--error-exitcode=0 -j "$LINT_JOBS" \
			-DNTLIBC_LINT=1 -D_XOPEN_SOURCE=700 -D_ALL_SOURCE -D_NTLIBC_INTERNAL \
			-Iarch/"$arch" -Iarch/generic -I"$builddir/$arch/include" \
			-Iinclude -Isrc/internal \
			$(sources_for "$arch") > "$out" 2>&1
		n=$(grep -c . "$out")
		note "cppcheck [$arch]: $n line(s) -> $out"
		show_findings "$out"
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
# the hook, install.sh, run-tests.py, this file) and #!/usr/bin/env bash
	# (the two generators), and forcing one dialect would report the other
	# half's perfectly valid syntax as errors.  shellcheck reads the
	# shebangs itself.
	shellcheck configure .githooks/pre-commit tools/*.sh > "$out" 2>&1
	rc=$?
	n=$(grep -cE '^In .* line [0-9]+:' "$out")
	note "shellcheck: $n finding(s) -> $out"
	show_findings "$out"
	[ "$rc" -eq 0 ] && return 0
	return 1
}

stages=${*:-warn analyze cppcheck shell undefined ushort unreferenced widthmod}
mkdir -p "$builddir" || exit 1

# Generate every arch's alltypes.h once, up front, before any stage that
# needs it can possibly run -- see gen_alltypes's own comment for why this
# matters once stages run concurrently.
for arch in $LINT_ARCHS; do gen_alltypes "$arch" || note "cannot generate alltypes for $arch"; done

# The stages read only from the source tree and each other's-own
# obj/lint/* output files (never one another's), so they are independent
# and run concurrently, each buffered to its own log and printed as one
# unit afterwards -- exactly the same reasoning as the per-file
# parallelism inside stage_warn/stage_analyze above, one level up. A
# single `tools/lint.sh` invocation with all of the default stages was the
# dominant cost of a full local verification pass (it does not itself
# fork off separate toolchains the way the two pinned CI-reproduction
# nix-shell invocations do); this is what cuts that down.
rundir=$(mktemp -d "$builddir/run.XXXXXX") || exit 1
export LINT_MISSING_MARKER="$rundir/missing"
for s in $stages; do
	(
		case $s in
		warn)      stage_warn ;;
		analyze)   stage_analyze ;;
		cppcheck)  stage_cppcheck ;;
		shell)     stage_shell ;;
		ushort)    tools/lint-ushort.sh ;;
		widthmod)  tools/lint-widthmod.sh ;;
		unreferenced) tools/lint-unreferenced.sh ;;
		undefined) tools/lint-undefined.sh ;;
		*) note "unknown stage: $s"; exit 2 ;;
		esac
		rc=$?
		echo "$rc" > "$rundir/$s.rc"
		exit "$rc"
	) > "$rundir/$s.out" 2>&1 &
done
wait

# Same floor tools/gate.sh:266-322 keeps one level up, for the same
# reason: a stage whose subshell was killed before it could write its
# .rc used to default to rc=0 here and count as a pass, so a run in
# which nothing ran at all reported "no findings".  A stage that did not
# report a result is a failure, and it has to be named -- otherwise the
# only evidence of what went missing is that the output is short.
reported=0
absent=""
for s in $stages; do
	cat "$rundir/$s.out"
	if [ ! -f "$rundir/$s.rc" ]; then
		note "MISSING: stage '$s' never reported a result (no $s.rc was written)"
		absent="$absent $s"
		findings=1
		continue
	fi
	reported=$((reported + 1))
	rc=$(cat "$rundir/$s.rc")
	[ "$rc" != 0 ] && findings=1
done
# $stages is a whitespace-separated list and is meant to word-split here,
# exactly as it does at every `for s in $stages` above.
# shellcheck disable=SC2086
nstages=$(printf '%s\n' $stages | grep -c . || true)
if [ "$reported" -ne "$nstages" ]; then
	note "lint: $reported of $nstages stage(s) reported a result; never reported:$absent"
	findings=1
fi
ls "$LINT_MISSING_MARKER".* >/dev/null 2>&1 && missing=1
rm -rf "$rundir"

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
