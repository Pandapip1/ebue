#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Ledger consistency: test/POSIX-COVERAGE.md describes the CURRENT state
# of this tree, and a row that says a clause is "fenced" is a claim that
# can go stale silently -- the fence gets removed when the defect is
# fixed and the row is not.  Three such rows were found by hand in one
# session (a scanf row, a destructive-BUG row, and five [ELOOP] sites
# blaming a privilege a later commit had corrected), which is what this
# exists to stop.
#
# The check is deliberately grep-level and knows nothing about the prose:
#
#   A. Every table row claiming "fenced" must name a test function that
#      is actually inside a `#if 0` block.
#   B. Every fenced test function must be named by some row claiming it
#      is fenced.
#
# Both directions, because each catches a different rot: A is a fix that
# forgot its row, B is a fence nobody recorded.
#
# WHY A BASELINE, and why it can only shrink.  Wiring this in with 100+
# pre-existing findings would make the gate permanently red, which is
# worse than no gate: a red light nobody can turn green stops being
# read.  tools/ledger-baseline.txt records what was already inconsistent
# when this check was written.  A finding NOT in the baseline is an
# error, and -- this is the half that keeps the baseline honest -- a
# baseline entry that is no longer a finding is ALSO an error, so the
# file cannot silently outlive the drift it excuses.  Same design as
# test/libc-test-expected.txt, where an xfail that starts passing is a
# hard error rather than a quiet win.
#
# NOT covered here, and it does not need to be: test/libc-test-expected.txt
# has a stronger check of its own in tools/libc-test.sh, which runs the
# corpus and treats an expected-to-fail test that passes as a hard error.
# That is verified by execution; this can only be verified by grep,
# because POSIX-COVERAGE.md describes clauses rather than runnable things.
set -eu

srcdir=$(dirname "$0")/..
cov="$srcdir/test/POSIX-COVERAGE.md"
baseline="$srcdir/tools/ledger-baseline.txt"
tmp=${TMPDIR:-/tmp}/ledger.$$
trap 'rm -f "$tmp".*' EXIT INT TERM

# Functions defined inside a `#if 0` block.  Nested conditionals are
# counted so an `#ifndef __i386__` inside a fence does not end it early.
for f in "$srcdir"/test/*.c; do
	awk '
		/^#if 0/                      { d++; next }
		d>0 && /^[ \t]*#[ \t]*if/     { d++; next }
		d>0 && /^[ \t]*#[ \t]*endif/  { d--; next }
		d>0 && /^static .*[ *]test_[A-Za-z0-9_]*\(/ {
			if (match($0, /test_[A-Za-z0-9_]*/))
				print substr($0, RSTART, RLENGTH)
		}
	' "$f"
done | sort -u > "$tmp".fenced

# Test names on a TABLE ROW that CLAIMS THE FENCE IS STILL THERE.
#
# The marker is the literal "(fenced" of a status cell -- "**BUG
# (fenced)**", "**BUG (fenced, latent)**", "UNIMPL (fenced)".  Matching
# the bare word "fenced" does not work and the reason is worth recording,
# because it made this check's first run report 52 findings of which most
# were noise: a row that has ALREADY been corrected says things like
# "asserted unfenced" or "was a fenced BUG ... FIXED", and both contain
# "fenced" as a substring.  A consistency check that cannot tell a
# current claim from a description of a corrected one is itself the kind
# of stale-reporting artefact it exists to catch.
#
# Rows only: the narrative sections discuss fences in prose and are not
# claims about the current state of a particular function.
grep '^|' "$cov" | grep 'fenced' | grep -v 'unfenced' | grep -F '(fenced' |
	grep -o 'test_[A-Za-z0-9_]*' | sort -u > "$tmp".claimed

comm -23 "$tmp".claimed "$tmp".fenced | sed 's/^/A /' >  "$tmp".found
comm -13 "$tmp".claimed "$tmp".fenced | sed 's/^/B /' >> "$tmp".found
sort -o "$tmp".found "$tmp".found

if [ -f "$baseline" ]; then
	grep -v '^#' "$baseline" | grep -v '^[[:space:]]*$' | sort > "$tmp".base
else
	: > "$tmp".base
fi

rc=0
new=$(comm -23 "$tmp".found "$tmp".base)
gone=$(comm -13 "$tmp".found "$tmp".base)

if [ -n "$new" ]; then
	rc=1
	echo "lint-ledger: NEW ledger inconsistency (test/POSIX-COVERAGE.md):" >&2
	echo "$new" | while read -r kind name; do
		case $kind in
		A) echo "  $name: a table row says this is fenced, but it is not inside a #if 0" >&2 ;;
		B) echo "  $name: this is fenced, but no table row records it as fenced" >&2 ;;
		esac
	done
	echo "lint-ledger: fix the row, or add the entry to tools/ledger-baseline.txt" >&2
fi

if [ -n "$gone" ]; then
	rc=1
	echo "lint-ledger: STALE baseline entries in tools/ledger-baseline.txt" >&2
	echo "  (these are no longer inconsistent -- delete them, the baseline may only shrink):" >&2
	echo "$gone" | sed 's/^/    /' >&2
fi

if [ "$rc" -eq 0 ]; then
	echo "lint-ledger: consistent ($(wc -l < "$tmp".fenced) fenced function(s), $(grep -c . "$tmp".base 2>/dev/null || echo 0) baselined)"
fi
exit $rc
