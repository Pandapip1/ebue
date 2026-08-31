#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# lint-undefined.sh -- flag a public header declaring a function that is
# never defined anywhere in this library.
#
# This is the inverse of the -Wmissing-prototypes stage in tools/lint.sh
# (which catches a *definition* with no public prototype): here the bug
# is a *prototype* with no definition, which -Wmissing-prototypes cannot
# see -- it only warns about the translation units it is fed, and a
# header is never itself compiled as one.  Nothing short of trying to
# link every declared symbol catches it, which is how all three known
# instances of this bug were actually found:
#
#   - system()/posix_close(): declared in include/stdlib.h and
#     include/unistd.h, defined nowhere -- any program calling either
#     failed to link with "unresolved reference".
#   - __find_program(): declared in src/internal/libc.h (an *internal*
#     header, so out of this script's scope, but the same shape of bug),
#     defined nowhere -- broke every link touching execv/execvp.
#   - malloc_usable_size(): the mirror image (defined, but never
#     declared anywhere) -- caught by -Wmissing-prototypes, not this
#     script, and mentioned here only to explain why this script checks
#     "undefined", not "undeclared".
#
# What this checks: every function *declared* in include/**/*.h must be
# *defined* somewhere in src/, arch/, or crt/ -- as a C function
# definition, or (arch/*/src/*.S) as an assembly global symbol, or as a
# name tools/ntdll.def exports (an import from ntdll.dll, resolved by
# the linker rather than compiled here at all: NtClose and friends, plus
# LdrLoadDll/LdrGetDllHandle/LdrGetProcedureAddress).  No public header
# declares an Nt*/Ldr* name directly today, but the check is there in
# case one ever does.
#
# This is text-only, like tools/lint-sizearith.py: no build, no compiler,
# nothing to fall back to when a tool is missing.  A single character-at-
# a-time awk scanner (comments, string/char literals, and multi-line
# preprocessor directives all stripped or skipped first) tracks brace and
# paren nesting to find each top-level declarator.  It runs over headers
# *and* .c files with the same
# nesting tracker, and a one-line function definition (several of
# src/signal/signal.c's sig*set functions, raise(), killpg()...) is
# recognised, not just the "signature, then a lone '{' on its own line"
# common split-line definition shape.
#
# What counts as "declared": a top-level statement in a header that is
# not a typedef and contains an identifier immediately followed by '(':
# the ordinary `RETTYPE NAME(params);` shape, or
# `RETTYPE (*NAME(params))(params);` for a function returning a function
# pointer (signal() and sigset() are the only examples here).
# A statement with no such pattern (an extern variable, a struct/enum
# tag, a macro constant) is not a function and is skipped.  The
# `extern "C" { ... }` wrapper nearly every header has is not treated as
# a nesting level in header mode -- if it were, every real prototype
# inside it would look "nested", not top-level -- so brace tracking is
# only meaningful in .c-file mode, where it is what lets a definition's
# *body* be skipped over rather than mistaken for more top-level
# declarations.
#
# A declaration that is genuinely never meant to be implemented (a
# documented, permanent stub) is not this script's business to flag
# forever.  Mark it by adding a comment containing the literal text
# `undefined-ok:` and a reason, on the declaration's own line or the
# line above it:
#
#   int nice(int);  /* undefined-ok: no NT priority mapping decided yet */
#
# Usage:
#   tools/lint-undefined.sh [header ...]     default: include/**/*.h
#
# Environment:
#   LINT_STRICT=0      always exit 0 (report only)
#
# Exit status is 1 if any finding was reported (unless LINT_STRICT=0).
#
# Known limits (the same honesty tools/lint-sizearith.py keeps): this does
# not parse C.  Comments and string/char literals are stripped, and
# multi-line preprocessor directives are skipped in full, but a
# declarator too unusual for the two shapes above (there are none in
# this tree today) is silently skipped rather than flagged wrong, and a
# radically different top-level code shape introduced later -- something
# whose brace nesting this scanner's simple counter cannot follow --
# could defeat the .c-file side.  That is the accepted price of a script
# that does not build the tree.

set -u

# shellcheck disable=SC1007
srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$srcdir" || exit 1

: "${LINT_STRICT:=1}"

headers=${*:-}
if [ -z "$headers" ]; then
	headers=$(find include -type f -name '*.h' | sort)
fi
# How many headers this run is responsible for, for the floor near the
# report below.  $headers is a whitespace-separated list and is meant to
# word-split here, exactly as it does at every `for h in $headers` below.
# shellcheck disable=SC2086
nheaders=$(printf '%s\n' $headers | grep -c . || true)

workdir=$(mktemp -d) || exit 1
trap 'rm -rf "$workdir"' EXIT INT TERM

# ---------------------------------------------------------------------
# The scanner.  Reads one file at a time (awk's FNR resets per file).
# Emits "name<TAB>line" once per top-level declarator found.
#
#   mode = "decl": a header.  A declarator ends at a top-level ';'.
#          Braces (the extern "C" wrapper; div_t/ldiv_t's inline struct
#          bodies) are not tracked as nesting -- see the header comment
#          above for why -- so they are just ordinary characters as far
#          as this mode is concerned, which is safe because nothing
#          inside them ever matches the name pattern.
#   mode = "def": a .c file.  A declarator ends at a top-level '{',
#          which opens the function body; the body's own braces are
#          then balanced (ignoring everything inside, including any
#          further ';' or '(' ')') until nesting returns to 0, at which
#          point top-level scanning resumes.  A top-level ';' with no
#          '{' (an extern declaration, a file-scope variable, an
#          initializer with no braces) resets the buffer: not a
#          function.
# ---------------------------------------------------------------------
scan() {
	mode=$1; shift
	awk -v MODE="$mode" -f "$srcdir/tools/lint-decls.awk" "$@"
}

# ---- declared: name<TAB>file:line, one per header prototype ----------
declfile="$workdir/declared"
: > "$declfile"
for h in $headers; do
	scan decl "$h" | while IFS="$(printf '\t')" read -r nm ln; do
		printf '%s\t%s:%s\n' "$nm" "$h" "$ln"
	done >> "$declfile"
done

# ---- names covered by an `undefined-ok:` marker -----------------------
markednames="$workdir/markednames"
: > "$markednames"
for h in $headers; do
	awk '
		{ ismark[NR] = (index($0, "undefined-ok:") > 0); raw[NR] = $0 }
		END {
			for (i = 1; i <= NR; i++) {
				if (!ismark[i] && !(i > 1 && ismark[i-1])) continue
				line = raw[i]
				if (match(line, /\([ \t]*\*[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/)) {
					m = substr(line, RSTART, RLENGTH)
					sub(/^\([ \t]*\*[ \t]*/, "", m); sub(/[ \t]*\($/, "", m)
					print m
				} else if (match(line, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/)) {
					m = substr(line, RSTART, RLENGTH)
					sub(/[ \t]*\($/, "", m)
					print m
				}
			}
		}
	' "$h"
done | sort -u > "$markednames"

# ---- defined: names from src/, arch/, crt/ C sources -------------------
definedfile="$workdir/defined"
: > "$definedfile"
cfiles=$(find src arch crt -type f -name '*.c' 2>/dev/null)
if [ -n "$cfiles" ]; then
	# shellcheck disable=SC2086
	scan def $cfiles | cut -f1 | sort -u > "$definedfile"
fi

# ---- defined: assembly globals (*.S under src/, arch/, crt/) ------------
sfiles=$(find src arch crt -type f -name '*.S' 2>/dev/null)
for f in $sfiles; do
	grep -E '^\.globl?' "$f" 2>/dev/null
done | sed -e 's/^\.globl\?//' -e 's/_(\([A-Za-z_][A-Za-z0-9_]*\))/\1/g' \
	| tr ',' '\n' | tr -d ' \t' | grep -v '^$' >> "$definedfile"
sort -u -o "$definedfile" "$definedfile"

# ---- defined: names the linker resolves via ntdll.def --------------------
if [ -f tools/ntdll.def ]; then
	grep -v -E '^[[:space:]]*(;|LIBRARY|EXPORTS[[:space:]]*$)' tools/ntdll.def \
		| tr -d ' \t\r' | grep -v '^$' >> "$definedfile"
	sort -u -o "$definedfile" "$definedfile"
fi

# ---- floors: did this run have anything to compare? ----------------------
#
# The exit status below is a function of $findings alone, and $findings can
# only rise for a name that appears in $declfile.  So an empty $declfile --
# no headers found, or scan() stopping recognising prototypes -- prints
# "no findings" and exits 0, which is the same `0 = 0` defect
# tools/asan-build.sh had (855fdb2).  An empty $definedfile is the mirror
# image: every declared name would then look undefined, which is loud
# rather than silent, but it means the definition scan broke and the
# report is meaningless either way.
ndecl=$(grep -c . "$declfile" || true)
ndef=$(grep -c . "$definedfile" || true)
if [ "$ndecl" -eq 0 ]; then
	printf 'lint-undefined: FAILED -- no declarations were found in %s header(s).\n' \
		"$nheaders" >&2
	printf 'lint-undefined: nothing was compared, so this run verified nothing.\n' >&2
	exit 1
fi
if [ "$ndef" -eq 0 ]; then
	printf 'lint-undefined: FAILED -- no definitions were found in src/, arch/, crt/ or\n' >&2
	printf 'lint-undefined: tools/ntdll.def, so every one of the %s declared name(s) would\n' "$ndecl" >&2
	printf 'lint-undefined: be reported undefined.  The definition scan is broken, not the tree.\n' >&2
	exit 1
fi

# ---- report ---------------------------------------------------------------
sort -u -t "$(printf '\t')" -k1,1 "$declfile" | while IFS="$(printf '\t')" read -r nm loc; do
	[ -z "$nm" ] && continue
	grep -qxF "$nm" "$definedfile" && continue
	grep -qxF "$nm" "$markednames" && continue
	printf '%s: %s: declared but never defined (and not marked undefined-ok)\n' "$loc" "$nm"
done > "$workdir/report"

cat "$workdir/report"
# tools/lint.sh's stage_* functions all funnel their findings through its
# show_findings(), which is what calls tools/lint-gh-annotate.sh -- but
# this script runs standalone (see the dispatch at tools/lint.sh's tail:
# `undefined) tools/lint-undefined.sh ;;`, not a stage_undefined that
# could call back into lint.sh), so it calls the same shared helper
# directly.  A no-op unless GITHUB_ACTIONS=true; see that script's header.
tools/lint-gh-annotate.sh error "$workdir/report"
findings=$(grep -c . "$workdir/report")

if [ "$findings" -eq 0 ]; then
	printf 'lint-undefined: no findings (%s declared name(s) checked against %s definition(s))\n' \
		"$ndecl" "$ndef"
	exit 0
fi
printf 'lint-undefined: %d finding(s)\n' "$findings"
[ "$LINT_STRICT" = 0 ] && exit 0
exit 1
