#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# lint-ushort.sh -- flag unguarded (USHORT) narrowing casts.
#
# UNICODE_STRING.Length (and the REPARSE_DATA_BUFFER/ANSI_STRING fields cut
# from the same cloth) is a USHORT counting *bytes*, saturating at 65535
# bytes = 32767 WCHARs.  An unchecked `(USHORT)` cast of a longer count does
# not truncate the string -- it wraps modulo 65536 and hands NT a *different*
# string than the caller passed.  This has been a recurring bug class here
# (see src/process/spawn.c, src/internal/path.c, src/unistd/chdir.c and
# src/unistd/link.c), and no sanitizer catches it: narrowing via an explicit
# cast is well-defined C, so -fsanitize=implicit-conversion/integer is silent
# on it, and DataFlowSanitizer taint-tracking cannot help either, because
# these lengths are loop-counter-derived rather than tainted from the input
# buffer itself.  A grep is the right-sized tool.
#
# What counts as guarded: the enclosing function contains one of the bound
# checks this codebase actually uses before a cast of this shape --
# __US_MAX_WCHARS, or a literal comparison against 0xffff/65535/USHRT_MAX.
# The window is "same function", not "within N lines": a crude line-distance
# window would flag the guard-then-use-later idiom every guarded site here
# follows, or miss a guard that happens to sit far from its cast in a long
# function.  Same-function is not perfect either -- it will happily pass a
# cast that sits before the guard check, or a guard that bounds a different
# variable entirely, as long as both appear somewhere in the same function
# body.  That false-negative rate is the honest price of a script that does
# not parse C; it is accepted deliberately rather than silently.
#
# A cast that is provably safe by construction (a fixed-size buffer, a
# length bounded by an unrelated compile-time constant) will never gain a
# real bounds check, and would otherwise be a permanent finding.  Mark it
# with a comment containing the literal text `USHORT-safe` -- on the cast's
# own line or the line immediately above it -- and this script skips just
# that site, regardless of the function-wide guard search:
#
#   x.Length = (USHORT)n;  /* USHORT-safe: n <= FD_MAX, far under 65535 */
#
# The floors.  "No findings" and "the scanner saw nothing to have findings
# about" print the same line and used to exit the same way, which is the
# `0 = 0` shape tools/asan-build.sh had (855fdb2) and that the rest of this
# tree has since been taught to reject.  Three things are therefore counted
# and checked before any result is reported:
#
#   * how many .c files were scanned -- zero means the path list matched
#     nothing (a directory rename, a bad argument), not a clean tree;
#   * how many lines textually contain `(USHORT)`, by grep, over exactly
#     the files that were scanned -- zero means there is nothing of this
#     shape left in the tree at all, at which point a "clean" result from
#     this script is true but empty and somebody should know;
#   * how many of those lines the awk state machine below actually
#     *classified* (guarded, marked, or reported).  This is the one that
#     matters.  The scanner only looks at casts inside a function body it
#     has recognised, and it recognises one by a signature line followed by
#     a lone `{`; a source file written in some other brace style, or a
#     function whose braces it fails to balance, leaves its casts unseen
#     and unclassified -- and the script's output for "I did not look at
#     this cast" is identical to its output for "this cast is fine".
#     Comparing the two counts turns that into a failure that names the
#     shortfall, exactly as tools/lint.sh's warn/analyze stages compare
#     their per-file log count against the source list.
#
# What is NOT a cast.  `sizeof(USHORT)` contains the token `(USHORT)`
# and is a type name inside sizeof, not a conversion of anything --
# crt/delayload2.c:239 skips a PE hint field with it.  Marking such a
# line USHORT-safe would document a narrowing that is not happening, and
# would then be indistinguishable from the 22 markers that document real
# ones; that is how a lint's findings stop meaning anything.  So the
# scanner is taught the rule instead: `sizeof (USHORT)` is stripped from
# a line before it is examined.  The textual floor below strips it the
# same way, from the same rule, so the scanner and the population it is
# measured against cannot disagree about what a cast is.
#
# Usage:
#   tools/lint-ushort.sh [path ...]     default: src crt sh
#
# Environment:
#   LINT_STRICT=0      always exit 0 (report only).  Does not relax the
#                      floors: a scanner that examined nothing is a broken
#                      scanner, not a report of zero findings.
#
# Exit status is 1 if any finding was reported (unless LINT_STRICT=0), or
# if any floor above was not met (always).

set -u

# shellcheck disable=SC1007
srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$srcdir" || exit 1

: "${LINT_STRICT:=1}"

# Every directory whose .c files this library builds: src/, the C
# runtime startup and delay-load helpers in crt/, and the sh(1p) binary
# in sh/.  crt/ and sh/ were outside this script's reach until now for no
# reason but that `src` was the first thing typed; crt/delayload2.c has a
# `(USHORT)` token in it, and a check that does not look at a directory
# reports the same "no findings" as one that looked and found nothing.
paths=${*:-src crt sh}

# $paths is a deliberately unquoted, space-separated list of CLI arguments
# (default: "src"); word-splitting it is the point.
# shellcheck disable=SC2086
files=$(find $paths -type f -name '*.c' 2>/dev/null)

findings=0
nfiles=0
nseen=0
ntext=0
for f in $files; do
	nfiles=$((nfiles + 1))
	# The textual population this file contributes, counted independently
	# of the scanner it is about to be compared against.  grep counts
	# *lines* containing the token and the awk below pushes once per such
	# line too, so the two are the same unit -- and both strip
	# `sizeof (USHORT)` first, by the same rule, so they cannot disagree
	# about what a cast is.
	ntf=$(sed 's/sizeof[ \t]*(USHORT)//g' "$f" | grep -c -F '(USHORT)' || true)
	ntext=$((ntext + ntf))
	out=$(awk '
		function flush(    i) {
			for (i = 0; i < ncasts; i++) {
				# Counted here rather than where the cast is pushed:
				# "classified" means the enclosing function was closed
				# and a verdict reached.  A cast pushed inside a
				# function whose braces never balance is dropped
				# silently, and that silent drop is what the
				# seen-vs-textual comparison in the caller catches.
				classified++
				if (guarded) continue
				if (castmarked[i]) continue
				printf "%s:%d: unguarded (USHORT) truncation -- no __US_MAX_WCHARS/0xffff/65535/USHRT_MAX check in this function and no USHORT-safe marker\n", FILENAME, castln[i]
			}
			infunc = 0; depth = 0; ncasts = 0; guarded = 0
		}
		{
			line = $0
			low = tolower(line)
		}
		{
			if (infunc) {
				if (index(low, "__us_max_wchars") > 0 ||
				    index(low, "0xffff") > 0 ||
				    index(low, "65535") > 0 ||
				    index(low, "ushrt_max") > 0) guarded = 1

				# See "What is NOT a cast" in the header: a
				# type name inside sizeof is not a conversion.
				casttext = line
				gsub(/sizeof[ \t]*\(USHORT\)/, "", casttext)
				if (index(casttext, "(USHORT)") > 0) {
					marked = (index(line, "USHORT-safe") > 0) || (index(prevline, "USHORT-safe") > 0)
					castln[ncasts] = FNR
					castmarked[ncasts] = marked
					ncasts++
				}

				o = gsub(/{/, "{", line)
				c = gsub(/}/, "}", line)
				depth += o - c
				if (depth <= 0) flush()

				prevline = $0
				next
			}

			if (line ~ /^\{[ \t]*$/ && prevsig) {
				infunc = 1; depth = 1; ncasts = 0; guarded = 0
			} else if (line ~ /[^ \t]/) {
				if (line ~ /\)[ \t]*$/ && line !~ /;[ \t]*$/) prevsig = 1
				else prevsig = 0
			}
			prevline = $0
		}
		END { printf "@classified\t%d\n", classified }
	' "$f")
	seen=$(printf '%s\n' "$out" | sed -n 's/^@classified\t//p')
	nseen=$((nseen + ${seen:-0}))
	out=$(printf '%s\n' "$out" | grep -v '^@classified	' || true)
	if [ -n "$out" ]; then
		printf '%s\n' "$out"
		n=$(printf '%s\n' "$out" | grep -c .)
		findings=$((findings + n))
	fi
done

# ---- floors: did this run examine anything? -------------------------------
floor_failed=0
if [ "$nfiles" -eq 0 ]; then
	printf 'lint-ushort: FAILED -- no .c file was scanned under: %s\n' "$paths" >&2
	printf 'lint-ushort: nothing was examined, so this run checked nothing.\n' >&2
	floor_failed=1
fi
if [ "$nfiles" -gt 0 ] && [ "$ntext" -eq 0 ]; then
	printf 'lint-ushort: FAILED -- %d file(s) scanned and not one line contains\n' "$nfiles" >&2
	printf 'lint-ushort: the token (USHORT).  A clean result over an empty population says\n' >&2
	printf 'lint-ushort: nothing about the bug class this script exists for.  If the\n' >&2
	printf 'lint-ushort: casts really are all gone, retire the script in the same\n' >&2
	printf 'lint-ushort: commit rather than letting it report success over nothing.\n' >&2
	floor_failed=1
fi
if [ "$nseen" -ne "$ntext" ]; then
	printf 'lint-ushort: FAILED -- %d line(s) contain the token (USHORT) but the scanner\n' "$ntext" >&2
	printf 'lint-ushort: classified only %d of them.  The remaining %d sit outside any\n' \
		"$nseen" "$((ntext - nseen))" >&2
	printf 'lint-ushort: function body this scanner recognised, so it never reached a\n' >&2
	printf 'lint-ushort: verdict on them.  Without this floor, "I did not look at this\n' >&2
	printf 'lint-ushort: cast" and "this cast is fine" are the same output.\n' >&2
	floor_failed=1
fi
[ "$floor_failed" -ne 0 ] && exit 1

if [ "$findings" -eq 0 ]; then
	printf 'lint-ushort: no findings (%d cast site(s) classified in %d file(s))\n' \
		"$nseen" "$nfiles"
	exit 0
fi
printf 'lint-ushort: %d finding(s) (%d cast site(s) classified in %d file(s))\n' \
	"$findings" "$nseen" "$nfiles"
[ "$LINT_STRICT" = 0 ] && exit 0
exit 1
