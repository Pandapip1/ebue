#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# lint-gh-annotate.sh -- turn one tools/lint.sh finding file into GitHub
# Actions workflow commands, so a finding shows up as an inline marker on
# the PR's "Files changed" view instead of something only visible by
# opening the CI log.
#
# Every stage in tools/lint.sh, and every tools/lint-*.py driver it runs,
# reports a finding as one line in the shape
#
#   path/to/file.c:LINE: message text
#   path/to/file.c:LINE:COL: severity: message text
#
# (the first from every tools/lint-*.py driver, tools/lint-undefined.sh
# and tools/lint-widthmod.sh; the second from gcc/clang/clang-tidy,
# cppcheck, and shellcheck -f gcc).  This is the one place that shape is
# parsed and turned into
#
#   ::error file=PATH,line=LINE,col=COL::MESSAGE
#   ::warning file=PATH,line=LINE,col=COL::MESSAGE
#
# which is the documented GitHub Actions workflow-command syntax for a
# file/line annotation (see "Workflow commands for GitHub Actions",
# ::error / ::warning).  A line's own "warning:"/"error:"/"note:"/etc.
# token, if it has one, decides its severity; a line with none (every
# Python driver's shape) falls back to the SEVERITY this script was
# called with.
#
# GITHUB_ACTIONS is the variable GitHub Actions itself sets to "true" in
# every job -- nothing else in this tree should ever set it.  Gating on
# it here, in the one shared place every stage funnels through, is what
# keeps a local `tools/lint.sh` run's output exactly what it always was:
# see tools/lint.sh's own header on why a local "no findings" has to
# mean what CI's does, and the same holds in reverse for *how* CI's
# findings are shown -- a terminal is not a PR diff, and workflow-command
# lines in a local run would just be log noise no one asked for.
#
# Usage:
#   tools/lint-gh-annotate.sh SEVERITY FILE
#
# SEVERITY is "error" or "warning", used for any line in FILE that does
# not name its own.  Does nothing (including: does not read FILE) unless
# GITHUB_ACTIONS=true.  Always exits 0 -- this is a reporting side
# channel, never a gate; the stage's own exit status is unaffected.
#
# Escaping follows the rules GitHub's own actions/toolkit implements
# (packages/core/src/command.ts, escapeData/escapeProperty): '%' first
# (so the escape sequences the other substitutions introduce are never
# themselves re-escaped), then CR, then LF, and -- for property values
# only, since ':' and ',' are the property-list separators -- ':' and
# ','.  The message uses only the first three; file= is a property value
# and gets all five.

set -u

[ "${GITHUB_ACTIONS:-}" = true ] || exit 0

if [ $# -ne 2 ]; then
	printf 'usage: %s SEVERITY FILE\n' "$0" >&2
	exit 0
fi
severity=$1
file=$2
[ -s "$file" ] || exit 0

awk -v DEFAULT="$severity" '
# escape_data: the %/CR/LF triple every workflow-command value gets.
function escape_data(s,    r) {
	r = s
	gsub(/%/, "%25", r)
	gsub(/\r/, "%0D", r)
	gsub(/\n/, "%0A", r)
	return r
}
# escape_prop: escape_data, plus the two characters that would otherwise
# be read as the next "key=value" pair or the next property.
function escape_prop(s,    r) {
	r = escape_data(s)
	gsub(/:/, "%3A", r)
	gsub(/,/, "%2C", r)
	return r
}
{
	line = $0
	if (line == "") next

	# path: up to the first ":".  Every finding source this script reads
	# is fed relative POSIX paths (no drive letters, no colons), so the
	# first ":" is unambiguously the path/line separator.
	i = index(line, ":")
	if (i < 2) next
	path = substr(line, 1, i - 1)
	rest = substr(line, i + 1)

	# line number: up to the next ":", and it must be all digits, or
	# this line is not a "path:line: ..." finding at all (a continuation
	# line, a summary line, shellcheck tty-format prose, ...) and is
	# silently skipped -- same as show_findings() printing it with no
	# special meaning attached.
	i = index(rest, ":")
	if (i < 2) next
	lineno = substr(rest, 1, i - 1)
	if (lineno !~ /^[0-9]+$/) next
	rest = substr(rest, i + 1)

	# optional column: "COL: " right after the line number (compiler/
	# cppcheck/shellcheck -f gcc shape). A Python driver'"'"'s "path:line: msg"
	# has no such field, and msg practically never starts with digits
	# followed by a bare ":", so this is unambiguous in practice.
	col = ""
	probe = rest
	sub(/^ /, "", probe)
	i = index(probe, ":")
	if (i >= 2) {
		cand = substr(probe, 1, i - 1)
		if (cand ~ /^[0-9]+$/) {
			col = cand
			rest = substr(probe, i + 1)
		}
	}

	msg = rest
	sub(/^ +/, "", msg)
	if (msg == "") next

	# cppcheck (run with its default, non---template output, which is
	# what makes it worth reading at a terminal) follows a real finding
	# with one or more "path:line:col: note: ..." lines explaining the
	# path that led to it, plus bare source/caret context lines with no
	# leading "path:line:" at all -- the latter already fail the parse
	# above and are skipped as non-finding lines, but a "note:" line
	# parses just fine and is not a second, independent finding: it is
	# the primary warning'"'"'s own explanation, at a nearby but different
	# line/column.  Annotating it too would put two or three markers on
	# the diff for one real finding.  gcc/clang-tidy'"'"'s own "note:" lines
	# never reach here at all -- stage_warn/stage_analyze/stage_lockset
	# pre-filter their logs to `warning:`/`error:` lines before this
	# script ever sees them -- so this skip is cppcheck-specific in
	# practice, but written as a general rule in case any other tool
	# output ever gets piped through unfiltered.
	if (msg ~ /^note[ \t]*:/) next

	sev = DEFAULT
	if (msg ~ /^(fatal error|fatal|error)[ \t]*:/) sev = "error"
	else if (msg ~ /^(warning)[ \t]*:/) sev = "warning"
	else if (msg ~ /^(info|information|style|performance|portability)[ \t]*:/) sev = "warning"

	out = "::" sev " file=" escape_prop(path) ",line=" lineno
	if (col != "") out = out ",col=" col
	out = out "::" escape_data(msg)
	print out
}
' "$file"
