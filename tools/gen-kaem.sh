#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/gen-kaem.sh -- regenerate boot/kaem/build-$(ARCH).kaem from the real
# Makefile's own build recipe, so the kaem bootstrap script can never
# silently drift out of sync with the Makefile as source files are added,
# removed, or renamed.
#
# This script itself requires a normal dev environment (bash, GNU make,
# sed, awk, sort, mktemp) -- it is a *generator*, run by a developer (or by
# `make kaem`) on a machine that already has the regular toolchain. It is
# not itself meant to run under kaem; only its *output* is kaem-compatible.
#
# How it works: it asks the real Makefile what it would do, via
#   make -j1 -n -B lib/libc.a lib/crt1.o
# (-B forces every recipe to print regardless of what obj/ and lib/
# currently contain; -n means "print, don't run"; asking for exactly
# lib/libc.a and lib/crt1.o -- rather than the default `all` target --
# excludes the empty stub libs (libm.a, libpthread.a, ...), lib/ntdll.def
# and the ntlibc-tcc wrapper script, none of which the kaem bootstrap stage
# needs), and then mechanically rewrites that dry-run output into
# kaem-legal syntax:
#   - `mkdir -p DIR` lines are expanded into the full parent-before-child
#     chain of bare `mkdir` commands (kaem's assumed toolset may not have a
#     -p-capable mkdir -- see CONTRIBUTING.md and the comments emitted into
#     the generated script itself for the full rationale).
#   - the `rm -f lib/libc.a` step is dropped: it is only needed for
#     re-builds over a dirty tree, coreutils rm is not assumed to be on
#     PATH at this bootstrap stage, and the generated script assumes (and
#     documents) a clean tree.
#   - everything else (the sed alltypes.h generation, every compile
#     command, the crt1.o copy, and the final tcc -ar archiving step) is
#     carried through close to verbatim, with only whitespace normalized.
#
# Usage:
#   ./configure --host=x86_64-win32 CC=x86_64-win32-tcc   # if not already done
#   ./tools/gen-kaem.sh                    # regenerate every arch (the default)
#   ./tools/gen-kaem.sh --arch=i386 [out]  # just one, for debugging
#
# With no --arch, this regenerates boot/kaem/build-$a.kaem for *every* arch
# under arch/ (bar arch/generic, which is not a target of its own). Doing
# them all every time is deliberate: a new source file otherwise lands in
# whichever bootstrap script the developer happened to have configured and
# silently misses the others, and CI's drift check only regenerates one leg,
# so the stale one can sit broken indefinitely.
#
# The compiler name is *not* taken from config.mak. It is derived per arch
# as $a-win32-tcc (with AR = $CC -ar, mirroring the Makefile), and handed to
# make as a command-line override, so the committed scripts are a pure
# function of the source tree rather than of whatever path ./configure was
# last pointed at -- an absolute CC in config.mak used to get baked into the
# generated output verbatim. config.mak is still required and still supplies
# the arch-independent bits (CFLAGS_C99FSE, CFLAGS_AUTO, KERNEL32, ...).

set -euo pipefail

cd "$(dirname "$0")/.."

if [ ! -f config.mak ]; then
	echo "gen-kaem.sh: config.mak not found -- run ./configure first, e.g.:" >&2
	echo "  ./configure --host=x86_64-win32 CC=x86_64-win32-tcc" >&2
	exit 1
fi

# Every arch/<a>/ except the shared arch/generic/ fallback headers.
kaem_arches() {
	for d in arch/*/; do
		a=${d%/}; a=${a#arch/}
		[ "$a" = generic ] && continue
		echo "$a"
	done
}

ARCH=""
OUT=""
for arg in "$@"; do
	case $arg in
		--arch=*) ARCH=${arg#--arch=} ;;
		-*)
			echo "gen-kaem.sh: unknown option '$arg'" >&2
			exit 1
			;;
		*) OUT=$arg ;;
	esac
done

# No --arch: do the whole set, one child invocation each.
if [ -z "$ARCH" ]; then
	if [ -n "$OUT" ]; then
		echo "gen-kaem.sh: an output file only makes sense with --arch=ARCH" >&2
		exit 1
	fi
	for a in $(kaem_arches); do
		"$0" --arch="$a"
	done
	exit 0
fi

if [ ! -d "arch/$ARCH" ]; then
	echo "gen-kaem.sh: no such arch '$ARCH' (have: $(kaem_arches | tr '\n' ' '))" >&2
	exit 1
fi

CC="${ARCH}-win32-tcc"
AR="$CC -ar"

OUT=${OUT:-boot/kaem/build-${ARCH}.kaem}
mkdir -p "$(dirname "$OUT")"

DRYRUN=$(mktemp)
trap 'rm -f "$DRYRUN"' EXIT

# ARCH/CC/AR are forced on the command line (which beats config.mak's own
# assignments) so this works for any arch regardless of what ./configure was
# last run with -- see the note at the top of this file.
if ! make --no-print-directory -j1 -n -B \
		ARCH="$ARCH" CC="$CC" AR="$AR" \
		lib/libc.a lib/crt1.o >"$DRYRUN" 2>&1; then
	echo "gen-kaem.sh: 'make -n -B lib/libc.a lib/crt1.o' failed for $ARCH:" >&2
	cat "$DRYRUN" >&2
	exit 1
fi

MKDIRS=$(mktemp)
trap 'rm -f "$DRYRUN" "$MKDIRS"' EXIT

# Pass 1: pull every `mkdir -p DIR` line's DIR out, and expand each into its
# full chain of ancestors (obj/src/foo -> obj, obj/src, obj/src/foo), one
# per line. `sort -u` in the C locale then gives a parent-before-child,
# duplicate-free ordering for free: '/' sorts before any letter or digit,
# so a directory's own line is always a strict, lexicographically-smaller
# prefix of any of its descendants' lines.
awk '/^mkdir -p /{print $3}' "$DRYRUN" | while IFS= read -r d; do
	acc=""
	IFS='/' read -ra parts <<<"$d"
	for part in "${parts[@]}"; do
		if [ -z "$acc" ]; then acc="$part"; else acc="$acc/$part"; fi
		echo "$acc"
	done
done | LC_ALL=C sort -u >"$MKDIRS"

# Pass 2: classify every non-mkdir line from the dry run and normalize
# whitespace (the Makefile's CFLAGS_ALL leaves double spaces where optional
# variables like CPPFLAGS/CFLAGS are empty; harmless to kaem's tokenizer,
# but ugly to read).
awk -v cc="$CC" '
	function norm(s) { gsub(/  +/, " ", s); sub(/^ +/, "", s); sub(/ +$/, "", s); return s }
	/^mkdir -p /      { next }
	/^rm -f lib\/libc\.a$/ { next }
	/^sed -f / {
		# kaem has no shell redirection (`>` is just another argument
		# token, not a redirect), so this can not be emitted as-is --
		# see the SED handling below main(). Expected shape:
		#   sed -f SCRIPT IN1 IN2 > OUT
		line = norm($0)
		n = split(line, a, " ")
		if (n != 7 || a[6] != ">") {
			print "OTHER\t" $0
			next
		}
		print "SED\t" a[3] "\t" a[4] "\t" a[5] "\t" a[7]
		next
	}
	/^cp obj\/crt\//  { print "CRTCOPY\t" norm($0); next }
	$0 ~ ("^" cc " -ar rcs lib/libc\\.a ") { print "AR\t" norm($0); next }
	$0 ~ ("^" cc " .* -c -o ") {
		line = norm($0)
		# find the object file named after "-o "
		n = split(line, a, " -o ")
		obj = a[2]
		sub(/ .*/, "", obj)
		if (obj ~ /^obj\/crt\//)      print "CRTCC\t" line
		else if (obj ~ /^obj\/arch\//) print "ARCHCC\t" line
		else                           print "MODCC\t" line
		next
	}
	{ print "OTHER\t" $0 }
' "$DRYRUN" >"${DRYRUN}.classified"

if grep -q '^OTHER' "${DRYRUN}.classified"; then
	echo "gen-kaem.sh: dry run produced lines this generator doesn't know" >&2
	echo "how to classify -- Makefile must have changed shape; update" >&2
	echo "tools/gen-kaem.sh. Offending lines:" >&2
	grep '^OTHER' "${DRYRUN}.classified" >&2
	exit 1
fi

field() { grep "^$1"$'\t' "${DRYRUN}.classified" | cut -f2-; }

SED_ROW=$(grep '^SED'$'\t' "${DRYRUN}.classified" || true)
SED_SCRIPT=$(printf '%s' "$SED_ROW" | cut -f2)
SED_IN1=$(printf '%s' "$SED_ROW" | cut -f3)
SED_IN2=$(printf '%s' "$SED_ROW" | cut -f4)
SED_OUT=$(printf '%s' "$SED_ROW" | cut -f5)
CRT_CC=$(field CRTCC)
CRT_COPY=$(field CRTCOPY)
AR_LINE=$(field AR)

if [ -z "$SED_SCRIPT" ] || [ -z "$CRT_CC" ] || [ -z "$CRT_COPY" ] || [ -z "$AR_LINE" ]; then
	echo "gen-kaem.sh: dry run is missing one of the expected single-shot" >&2
	echo "steps (alltypes.h sed, crt1.o compile, crt1.o copy, libc.a ar)." >&2
	exit 1
fi

{
	cat <<HEADER
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# boot/kaem/build-${ARCH}.kaem -- kaem-only bootstrap build of ntlibc for
# ${ARCH}-win32, producing lib/libc.a and lib/crt1.o without make, without a
# real shell, and without a general-purpose ar.
#
# GENERATED FILE -- do not hand-edit. Regenerate with:
#   ./configure --host=${ARCH}-win32 CC=${CC}
#   ./tools/gen-kaem.sh
# (or \`make kaem\`, which does the same via this Makefile's own recipe for
# lib/libc.a and lib/crt1.o, so this script can never silently drift out of
# sync with the Makefile as source files are added/removed/changed).
#
# This is NOT the normal way to build ntlibc. It exists solely for the
# "right after mes compiles tcc" point in a from-scratch bootstrap chain
# (live-bootstrap style), where the only command driver available is kaem
# (from mescc-tools) and make/bash do not exist yet. See CONTRIBUTING.md
# for the full rationale and the analogy to Guix's gzip-mesboot0.
#
# Assumed available tools, and nothing else:
#   - the win32-cross tcc that this script's PATH points at (${CC})
#     acting as both compiler and archiver (tcc -ar rcs, no external ar/
#     ranlib needed -- this mirrors what the real Makefile does via
#     \$(AR) = \$(CC) -ar)
#   - mkdir, cp, sed
#   - kaem's own builtins (cd, set, echo, if/then/else/fi, etc. -- unused
#     here)
# Notably NOT assumed: make, a POSIX shell, coreutils rm, or a standalone
# binutils ar.
#
# Run from the repository root, e.g.:
#   PATH=/path/to/win32-cross-tcc/bin:\$PATH kaem --strict --file boot/kaem/build-${ARCH}.kaem
#
# This script assumes a clean tree (no pre-existing obj/ or lib/): every
# directory below is created with a single bare \`mkdir\`, not \`mkdir -p\`,
# in strict parent-before-child order. Two reasons for that, both explained
# in CONTRIBUTING.md:
#   1. \`mkdir -p\` is not guaranteed to exist at this bootstrap point -- the
#      handwritten early-stage tools favor minimality, and depending on -p
#      would be an unverified assumption at exactly the point where an
#      unverified assumption is most expensive to be wrong about.
#   2. kaem has no branching/conditionals worth using for "did this already
#      exist" logic, and no loop construct to factor this out -- so the
#      dependency-ordered flat list below is both the safe option and the
#      only option that fits kaem's model.
# (For what it's worth: mescc-tools-extra's own \`mkdir\` *does* implement
# --parents/-p, in a way that tolerates an already-existing directory
# without erroring -- see mescc-tools-extra/mkdir.c's create_dir(). So
# \`mkdir -p\` would probably also work here if that's the mkdir on PATH.
# This script still doesn't rely on it, since a minimal mkdir providing
# only bare POSIX mkdir(1) semantics is not ruled out at this stage, and
# the parent-first plain-mkdir list costs nothing extra to emit.)
#
# For the same reason (clean-tree assumption, no coreutils rm assumed to be
# on PATH), this script does not \`rm -f lib/libc.a\` before archiving,
# unlike the Makefile's \`lib/libc.a:\` rule -- there is nothing to remove
# yet. If you re-run this script's *output* over a non-clean tree, remove
# obj/ and lib/ first with whatever tool you have.

#
# Create output directories, parent before child.
#
HEADER
	sed 's/^/mkdir /' "$MKDIRS"
	SED_IN1_LINES=$(wc -l <"$SED_IN1")
	if [ -z "$SED_IN1_LINES" ] || [ "$SED_IN1_LINES" -le 0 ]; then
		echo "gen-kaem.sh: could not get a usable line count for $SED_IN1" >&2
		exit 1
	fi
	if [ "$(tail -c1 "$SED_IN1" | wc -l)" -ne 1 ]; then
		echo "gen-kaem.sh: $SED_IN1 does not end with a newline -- the" >&2
		echo "'insert after last line' trick below needs an exact line" >&2
		echo "count from wc -l, which undercounts a missing final newline." >&2
		exit 1
	fi
	cat <<MID1

#
# Generate obj/include/bits/alltypes.h. The Makefile does this with one
# \`sed -f $SED_SCRIPT $SED_IN1 $SED_IN2 > $SED_OUT\`
# invocation, but kaem can't run it as-is for two independent reasons:
#   - kaem has no shell redirection (\`>\` is just another plain argument,
#     not an operator), so there is nowhere for sed's stdout to go; and
#   - kaem's variable expander treats every literal '\$' in any token
#     (quoted or not) as the start of a \${...}/\$@ substitution and aborts
#     otherwise, so sed's own '\$r file' ("after the last line, read
#     file") address can't be spelled at all here.
# Both are worked around at once, using only cp and sed -i (both in the
# assumed toolset) and a fixed line number computed when this script was
# *generated*, not a live '\$':
#   1. seed the output file with a raw copy of the first input;
#   2. use sed's 'Nr file' (read-file after line N) command, via -i, with
#      N hardcoded to $SED_IN1's line count at generation time ($SED_IN1_LINES), to
#      append the raw second input after it -- equivalent to '\$r' here
#      only because N is exactly the first input's last line;
#   3. run the real mkalltypes.sed over the now-combined file in place.
# This is only correct because mkalltypes.sed's substitutions are all
# purely per-line (no multi-line hold-space state, no line-range address),
# so "transform the concatenation of the two inputs" and "concatenate the
# already-independently-transformed halves" give the same bytes -- verified
# byte-for-byte identical to the Makefile's own output for this exact input
# pair as part of generating this script. If $SED_IN1 ever gains or loses
# lines, or stops ending in a newline, rerun \`make kaem\` to pick up the
# new line count -- do not hand-edit the number below.
#
MID1
	printf 'cp %s %s\n' "$SED_IN1" "$SED_OUT"
	printf 'sed -i "%sr %s" %s\n' "$SED_IN1_LINES" "$SED_IN2" "$SED_OUT"
	printf 'sed -i -f %s %s\n' "$SED_SCRIPT" "$SED_OUT"
	cat <<'MID2'

#
# crt1.o: compiled like any other object, then copied into lib/ under its
# install name (mirrors the Makefile's `lib/%.o: obj/crt/%.o` / `cp` rule).
#
MID2
	printf '%s\n' "$CRT_CC"
	printf '%s\n' "$CRT_COPY"
	cat <<'MID3'

#
# Compile every libc source file into obj/, one literal command per file
# (mirrors the Makefile's `obj/%.o: $(srcdir)/%.c` / `.S` rules, applied to
# BASE_SRCS for this arch after arch-override filtering).
#
MID3
	field MODCC
	cat <<'MID4'

#
# Per-arch overrides/extra objects from arch/$(ARCH)/src/.
#
MID4
	field ARCHCC
	cat <<'MID5'

#
# Archive every libc/arch object into lib/libc.a using tcc's built-in
# self-archiving mode (tcc -ar), exactly as the Makefile's AR = $(CC) -ar
# does -- no standalone binutils ar required.
#
MID5
	printf '%s\n' "$AR_LINE"
} >"$OUT"

rm -f "${DRYRUN}.classified"

echo "gen-kaem.sh: wrote $OUT ($(wc -l <"$OUT") lines)" >&2
