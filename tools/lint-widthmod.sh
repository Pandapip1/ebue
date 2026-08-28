#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# lint-widthmod.sh -- the `z` and `t` printf/scanf length modifiers must
# be read and written as size_t and ptrdiff_t, not as long.
#
# THE DEFECT THIS EXISTS FOR.  src/stdio/printf.c handled both with
# `va_arg(ap, long)`:
#
#     case LM_z: case LM_t: sv = va_arg(ap, long); break;          (:514)
#     case LM_z: case LM_t: uv = va_arg(ap, unsigned long); break; (:531)
#     case LM_z: case LM_t: *(long *)ptr = (long)count; break;     (:628)
#
# This target is LLP64: `long` is 32 bits, `size_t` and `ptrdiff_t` are
# 64.  So "%zu" of a value above 4G prints its low half, and "%zn"
# writes FOUR bytes into an EIGHT-byte object -- leaving the caller's
# other four untouched, which is memory corruption that no sanitizer
# reports because the write is inside the object.  It was found by
# musl's libc-test, not by this tree's own clause audit of the same
# POSIX page, and it is fenced in test/posix-stdio.c.
#
# THE TREE CONTAINS ITS OWN CORRECT ANSWER.  src/stdio/scanf.c
# implements the same grammar and gets it right, in four places:
#
#     case LM_z: *(size_t *)va_arg(ap, void *) = (size_t)uv;       (:495)
#     case LM_t: *(ptrdiff_t *)va_arg(ap, void *) = (ptrdiff_t)uv; (:496)
#     case LM_z: *(size_t *)va_arg(ap, void *) = (size_t)sc.nread; (:617)
#     case LM_t: *(ptrdiff_t *)va_arg(ap, void *) = sc.nread;      (:618)
#
# So this script does not invent a rule.  It takes the pattern one of
# the two files already follows and makes it checkable in the other.
#
# THE RULE.  A line naming LM_z must also name size_t; a line naming
# LM_t must also name ptrdiff_t; a line naming both must name both.
# Three kinds of line are exempt, and nothing else is:
#
#   * the enum that declares the modifiers (recognised by LM_NONE);
#   * the parser assigning one (`lm = LM_z;`), which selects the
#     modifier and touches no value;
#   * a line carrying a `widthmod-ok:` comment and a reason, on the line
#     itself or the one above, like tools/lint-undefined.sh's
#     `undefined-ok:` escape.  It means "this is correct"; it is NOT for recording a
#     known bug, which is what the next paragraph is for.
#
# KNOWN, FENCED DEFECTS.  printf.c's three sites are real and are not
# fixed -- they are fenced in test/posix-stdio.c, and this project's
# convention is that a fenced defect is recorded, not green-lit, and
# does not red-line a shared gate.  So they are listed in
# tools/widthmod-known.txt, keyed by file and by the exact text of the
# offending line, and reported on every run as KNOWN rather than
# counted as findings.  A NEW site is a finding and fails.
#
# The list has its own floor, and it is the point of keeping the record
# in a file a machine reads: AN ENTRY THAT MATCHES NOTHING IS A
# FAILURE.  If printf.c is fixed and the entry stays, the check says so
# and names it, instead of the record quietly outliving the defect --
# which is exactly the failure that cost this project two agent cycles
# (an open-findings list of eight items, all fixed, still recorded as
# open).  Keyed on line TEXT rather than line number for the same
# reason: a number goes stale on any edit above it, and a stale key
# would silently stop matching, turning a recorded defect back into a
# finding or -- worse, if the entry is later "fixed" by renumbering --
# into nothing at all.
#
# WHY THIS SHAPE AND NOT THE OBVIOUS ONE.  The obvious rule is "flag
# va_arg(ap, long) in a tree where long is 32-bit".  Measured against
# the tree at f9f1937, that rule finds 4 lines, of which 2 are correct
# (`case LM_l:` genuinely wants a long) and it MISSES :628 entirely --
# the %zn write, which is the one that corrupts memory.  A third
# candidate, flagging stores through `*(long *)`, finds 4 lines with 1
# true positive.  The rule below finds 3 lines, all 3 of them the
# defect, no false positives.  The difference is that it keys on the
# thing that determines correctness -- the pairing of a length modifier
# with the C type it denotes -- rather than on a type name that is
# sometimes exactly right.
#
# THE HONEST LIMIT, and it is a real one: this is a rule about ONE
# IDIOM IN TWO FILES.  It does not generalise to "long is not size_t"
# across this tree; the two broader greps measured above are the
# evidence that the general form is not gateable at an acceptable
# false-positive rate.  What it buys is that this specific defect
# cannot come back unnoticed, and that it stays visible in the SOURCE
# while the test that would catch it is fenced -- a fenced test reports
# nothing, so without this the only live record of the finding is a
# comment.
#
# It is also line-based: a handler split across two lines, or one that
# reaches the value through a helper, is not seen.  Both files write
# these as one line per case today.
#
# Usage:
#   tools/lint-widthmod.sh [path ...]     default: src
#
# Environment:
#   LINT_STRICT=0      always exit 0 (report only).  Does not relax the
#                      floors below: a run that judged nothing is a
#                      broken run, not a report of zero findings.
#
# Exit status is 1 if any finding was reported (unless LINT_STRICT=0),
# or if any floor was not met (always).

set -u

# shellcheck disable=SC1007
srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$srcdir" || exit 1

: "${LINT_STRICT:=1}"

paths=${*:-src}
knownfile=tools/widthmod-known.txt

# $paths is a deliberately unquoted, space-separated list of CLI
# arguments (default: "src"); word-splitting it is the point.
# shellcheck disable=SC2086
files=$(find $paths -type f -name '*.c' 2>/dev/null)

work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT INT TERM
: > "$work/findings"
: > "$work/known"
: > "$work/matched"

nfiles=0
nlines=0
nexempt=0
njudged=0
findings=0
nknown=0

for f in $files; do
	nfiles=$((nfiles + 1))
	awk -v F="$f" -v KNOWNFILE="$knownfile" '
		function has(s) { return index(line, s) > 0 }
		BEGIN {
			# FILE<TAB>SOURCE-LINE-TEXT<TAB>REASON
			while ((getline kl < KNOWNFILE) > 0) {
				if (kl ~ /^#/ || kl ~ /^[ \t]*$/) continue
				if (split(kl, kf, "\t") < 3) continue
				known[kf[1], kf[2]] = kf[3]
			}
			close(KNOWNFILE)
		}
		{
			line = $0
			if (!has("LM_z") && !has("LM_t")) { prev = line; next }
			seen++
			# The enum that declares the modifiers.
			if (has("LM_NONE")) { exempt++; prev = line; next }
			# The parser selecting one: no value is read or written.
			if (line ~ /lm[ \t]*=[ \t]*LM_[zt][ \t]*;/) { exempt++; prev = line; next }
			# An explicitly justified exception ("this is correct").
			if (has("widthmod-ok:") || index(prev, "widthmod-ok:") > 0) {
				exempt++; prev = line; next
			}
			judged++
			bad = ""
			if (has("LM_z") && !has("size_t")) bad = "LM_z without size_t"
			if (has("LM_t") && !has("ptrdiff_t"))
				bad = (bad == "" ? "" : bad " and ") "LM_t without ptrdiff_t"
			if (bad != "") {
				key = line
				sub(/^[ \t]+/, "", key); sub(/[ \t]+$/, "", key)
				if ((F SUBSEP key) in known)
					printf "K\t%s\t%s\t%s:%d: %s [known: %s]\n", F, key, F, FNR, bad, known[F, key]
				else
					printf "F\t-\t-\t%s:%d: %s -- on LLP64 size_t and ptrdiff_t are 64 bits and long is 32; see src/stdio/scanf.c:495,496,617,618 for the form this tree already uses, or add a widthmod-ok: comment with a reason\n", F, FNR, bad
			}
			prev = line
		}
		END { printf "C\t%d\t%d\t%d\t-\n", seen + 0, exempt + 0, judged + 0 }
	' "$f" > "$work/one"

	while IFS="$(printf '\t')" read -r tag a b msg; do
		case $tag in
		F)
			printf '%s\n' "$msg" >> "$work/findings"
			findings=$((findings + 1)) ;;
		K)
			printf '%s\n' "$msg" >> "$work/known"
			printf '%s\t%s\n' "$a" "$b" >> "$work/matched"
			nknown=$((nknown + 1)) ;;
		C)
			nlines=$((nlines + a))
			nexempt=$((nexempt + b))
			# `read` with four names puts the rest of the line in
			# the fourth, so $msg here is "judged<TAB>-"; take the
			# first field of it.
			njudged=$((njudged + ${msg%%	*})) ;;
		esac
	done < "$work/one"
	rm -f "$work/one"
done

# ---- floors: did this run judge anything? ---------------------------------
#
# The exit status below is a function of $findings alone, so every way of
# ending up with nothing to judge is a silent pass -- the shape
# tools/asan-build.sh had (855fdb2) and that the rest of this tree has
# been taught to reject.  Four distinct ways, failing separately because
# they mean different things.
floor_failed=0
if [ "$nfiles" -eq 0 ]; then
	printf 'lint-widthmod: FAILED -- no .c file was scanned under: %s\n' "$paths" >&2
	printf 'lint-widthmod: nothing was examined, so this run checked nothing.\n' >&2
	floor_failed=1
fi
if [ "$nfiles" -gt 0 ] && [ "$nlines" -eq 0 ]; then
	printf 'lint-widthmod: FAILED -- %d file(s) scanned and not one line names LM_z or\n' "$nfiles" >&2
	printf 'lint-widthmod: LM_t.  Either the printf/scanf length-modifier dispatch has been\n' >&2
	printf 'lint-widthmod: rewritten under a different name -- in which case this script is\n' >&2
	printf 'lint-widthmod: now looking at nothing and must be retaught or retired in the\n' >&2
	printf 'lint-widthmod: same commit -- or the scan is broken.  A clean result over an\n' >&2
	printf 'lint-widthmod: empty population says nothing about the defect this exists for.\n' >&2
	floor_failed=1
fi
if [ "$nlines" -gt 0 ] && [ "$njudged" -eq 0 ]; then
	printf 'lint-widthmod: FAILED -- all %d LM_z/LM_t line(s) were exempted and none was\n' "$nlines" >&2
	printf 'lint-widthmod: judged.  Exemptions swallowing the whole population is how a\n' >&2
	printf 'lint-widthmod: check goes green without looking at anything: it reports the same\n' >&2
	printf 'lint-widthmod: "no findings" as a run that judged every site and approved it.\n' >&2
	floor_failed=1
fi

# The known-defect list must not outlive the defect.  An entry that
# matched nothing this run means either the site was fixed and the record
# was not updated -- which is precisely the failure that cost this
# project two agent cycles, an open-findings list of eight items all of
# which had been fixed -- or the line was edited and the key no longer
# matches, in which case the site is now being judged as if it were new
# and somebody should look.  Either way it is a failure that names the
# entry, not a quiet mismatch.
sort -u -o "$work/matched" "$work/matched" 2>/dev/null || :
grep -v -e '^#' -e '^[[:space:]]*$' "$knownfile" 2>/dev/null | cut -f1,2 | sort -u > "$work/declared"
nkdecl=$(grep -c . "$work/declared" || true)
stale=$(comm -23 "$work/declared" "$work/matched" 2>/dev/null || true)
if [ -n "$stale" ]; then
	printf 'lint-widthmod: FAILED -- %s names site(s) that no longer exist:\n' "$knownfile" >&2
	printf '%s\n' "$stale" | sed 's/^/    /' >&2
	printf 'lint-widthmod: either the defect was fixed and the entry must go in the same\n' >&2
	printf 'lint-widthmod: commit (delete it, and unfence the test that records it), or the\n' >&2
	printf 'lint-widthmod: line was edited and the entry must be re-keyed.  A known-defect\n' >&2
	printf 'lint-widthmod: record that outlives its defect is worse than no record.\n' >&2
	floor_failed=1
fi

[ "$floor_failed" -ne 0 ] && exit 1

if [ "$nknown" -gt 0 ]; then
	printf 'lint-widthmod: %d known, fenced site(s) matching all %d entry/entries in %s\n' \
		"$nknown" "$nkdecl" "$knownfile"
	printf 'lint-widthmod: (recorded, reported every run, not counted as findings):\n'
	sed 's/^/  /' "$work/known"
fi
if [ "$findings" -eq 0 ]; then
	printf 'lint-widthmod: no new findings (%d LM_z/LM_t line(s) in %d file(s): %d judged, %d exempt, %d known)\n' \
		"$nlines" "$nfiles" "$njudged" "$nexempt" "$nknown"
	exit 0
fi
cat "$work/findings"
printf 'lint-widthmod: %d NEW finding(s) (%d LM_z/LM_t line(s) in %d file(s): %d judged, %d exempt, %d known)\n' \
	"$findings" "$nlines" "$nfiles" "$njudged" "$nexempt" "$nknown"
[ "$LINT_STRICT" = 0 ] && exit 0
exit 1
