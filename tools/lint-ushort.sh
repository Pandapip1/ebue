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
# Usage:
#   tools/lint-ushort.sh [path ...]     default: src
#
# Environment:
#   LINT_STRICT=0      always exit 0 (report only)
#
# Exit status is 1 if any finding was reported (unless LINT_STRICT=0).

set -u

# shellcheck disable=SC1007
srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$srcdir" || exit 1

: "${LINT_STRICT:=1}"

paths=${*:-src}

# $paths is a deliberately unquoted, space-separated list of CLI arguments
# (default: "src"); word-splitting it is the point.
# shellcheck disable=SC2086
files=$(find $paths -type f -name '*.c' 2>/dev/null)

findings=0
for f in $files; do
	out=$(awk '
		function flush(    i) {
			for (i = 0; i < ncasts; i++) {
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

				if (index(line, "(USHORT)") > 0) {
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
	' "$f")
	if [ -n "$out" ]; then
		printf '%s\n' "$out"
		n=$(printf '%s\n' "$out" | grep -c .)
		findings=$((findings + n))
	fi
done

if [ "$findings" -eq 0 ]; then
	printf 'lint-ushort: no findings\n'
	exit 0
fi
printf 'lint-ushort: %d finding(s)\n' "$findings"
[ "$LINT_STRICT" = 0 ] && exit 0
exit 1
