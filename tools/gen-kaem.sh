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
#   - the `cat A B > obj/include/bits/alltypes.h` step becomes a single
#     `catm obj/include/bits/alltypes.h A B` (mescc-tools-extra's
#     concatenator), since kaem has no `>` redirection.
#   - everything else (every compile command, the crt1.o copy, and the
#     final tcc -ar archiving step) is carried through close to verbatim,
#     with only whitespace normalized.
#   - a final pass then puts ${srcdir}/ in front of every path read from the
#     source tree, and replaces every command *name* with a variable
#     (${bin_mkdir}, ${bin_cp}, ${bin_catm}, ${CC}) so the caller supplies
#     absolute paths: kaem does no PATH lookup when it is itself a win32
#     PE32 binary, and does not append the .exe the tools are installed
#     with. See the comments on that pass, and the header it emits.
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

# Every arch/<a>/ except the shared arch/generic/ fallback headers, AND
# except an arch with no arch/<a>/src/ files of its own (arch/aarch64,
# a real target but only for PLATFORM=linux -- see configure's own
# --platform flag and Makefile's PLAT_GLOBS comment). kaem exclusively
# bootstraps the NT/tcc toolchain (CONTRIBUTING.md), and CC below is
# always forced to "${ARCH}-win32-tcc" -- an arch/<a> with nothing
# under src/ was never a real NT target to begin with, "aarch64-win32-
# tcc" names a compiler that has never existed. Skipping it here, not
# papering over it further down, is why: below this point every path
# assumes at least one ARCHCC-classified dry-run line exists (real for
# i386/x86_64, each of which has real arch/<a>/src/*.[cS] files) --
# without this guard, an empty arch/aarch64/src/ silently reached
# `field ARCHCC | ...`, whose `grep` found nothing and returned 1,
# which -- under this script's own `set -euo pipefail` -- aborted the
# whole run with NO error message at all. Caught by a real pre-commit
# hook failure with empty stderr, not anticipated.
kaem_arches() {
	for d in arch/*/; do
		a=${d%/}; a=${a#arch/}
		[ "$a" = generic ] && continue
		[ -n "$(find "arch/$a/src" -maxdepth 1 \( -name '*.c' -o -name '*.S' \) 2>/dev/null)" ] || continue
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
	/^cat / {
		# kaem has no shell redirection (`>` is just another argument
		# token, not a redirect), so this becomes a catm invocation
		# (from mescc-tools-extra) below. Expected shape:
		#   cat IN1 IN2 > OUT
		line = norm($0)
		n = split(line, a, " ")
		if (n != 5 || a[4] != ">") {
			print "OTHER\t" $0
			next
		}
		print "CATM\t" a[5] "\t" a[2] "\t" a[3]
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

CATM_ROW=$(grep '^CATM'$'\t' "${DRYRUN}.classified" || true)
CATM_OUT=$(printf '%s' "$CATM_ROW" | cut -f2)
CATM_IN1=$(printf '%s' "$CATM_ROW" | cut -f3)
CATM_IN2=$(printf '%s' "$CATM_ROW" | cut -f4)
CRT_CC=$(field CRTCC)
CRT_COPY=$(field CRTCOPY)
AR_LINE=$(field AR)

if [ -z "$CATM_OUT" ] || [ -z "$CRT_CC" ] || [ -z "$CRT_COPY" ] || [ -z "$AR_LINE" ]; then
	echo "gen-kaem.sh: dry run is missing one of the expected single-shot" >&2
	echo "steps (alltypes.h concatenation, crt1.o compile, crt1.o copy," >&2
	echo "libc.a ar)." >&2
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
# Assumed available tools, and nothing else -- each named by a variable this
# script's caller must set to the tool's path (see "must be set" below):
#   - \${CC}, a win32-cross tcc for ${ARCH} (${CC}), acting as both
#     compiler and archiver (tcc -ar rcs, no external ar/ranlib needed --
#     this mirrors what the real Makefile does via \$(AR) = \$(CC) -ar).
#     It must be the compiler alone, with no flags appended: tcc requires
#     \`-ar' to be its very first argument, and rejects it anywhere else
#     with "cannot parse -ar here".
#   - \${bin_mkdir}, \${bin_cp} and \${bin_catm}, all three from
#     mescc-tools-extra -- the companion package to mescc-tools (kaem's own
#     home), so it is already
#     on hand wherever kaem is. \`catm OUT IN...\` is its minimal
#     concatenator; it stands in for the shell's \`cat IN... > OUT\`, which
#     kaem cannot express because it has no redirection. Of the fifteen
#     programs mescc-tools-extra ships (catm, chmod, cp, match, mkdir,
#     replace, rm, sha256sum, sha3sum, unbz2, ungz, untar, unxz, wrap)
#     this script uses exactly three.
#   - kaem's own builtins (cd, set, echo, if/then/else/fi, etc. -- unused
#     here)
# Notably NOT assumed: make, a POSIX shell, sed or awk (mescc-tools-extra
# has neither, and nothing else in it can do a capture-group rewrite),
# coreutils rm, or a standalone binutils ar.
#
# Five variables must be set, and the working directory must be writable:
#   srcdir     the source tree.  Every path this reads from the tree is
#              \${srcdir}-relative; every path it writes is relative to the
#              working directory.
#   CC         the ${CC} above
#   bin_mkdir  \\
#   bin_cp      >  the three mescc-tools-extra programs above
#   bin_catm   /
# The four tool variables hold paths, not names to look up.  kaem does no
# PATH search of its own when it is itself a win32 PE32 binary -- the form it
# has at the bootstrap point this script exists for -- so a bare \`mkdir'
# there is \`Subprocess error -1', and it does not append the \`.exe' the
# tools are installed with either.  Give absolute paths and neither matters.
#
# From the repository root, in place, with the tools in one directory:
#   srcdir=. CC=/path/to/${CC}.exe bin_mkdir=/path/to/mkdir.exe bin_cp=/path/to/cp.exe bin_catm=/path/to/catm.exe kaem --strict --file boot/kaem/build-${ARCH}.kaem
# Or with the sources read-only somewhere else, which is the case a
# from-scratch bootstrap actually has -- an unpacked tarball or a store path
# it may not write to, and no recursive copy at this stage to stage it with:
#   cd /some/empty/writable/dir
#   srcdir=/path/to/ntlibc CC=... bin_mkdir=... bin_cp=... bin_catm=... kaem --strict --file .../build-${ARCH}.kaem
# (On a developer machine with a POSIX shell the tools are unsuffixed and
# \`command -v' fills these in, e.g. bin_cp=\$(command -v cp).)
#
# kaem substitutes an unset variable as nothing, so a forgotten srcdir fails
# on /src/... and a forgotten tool leaves a command whose name is its own
# first argument -- either way a loud failure, never a quiet wrong one.
#
# This script assumes a clean tree (no pre-existing obj/ or lib/): every
# directory below is created with a single bare \`\${bin_mkdir} DIR\`, never
# with -p, in strict parent-before-child order. Two reasons for that, both explained
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
# \`\${bin_mkdir} -p\` would probably also work here if that's the mkdir
# pointed at. This script still doesn't rely on it, since a minimal mkdir
# providing only bare POSIX mkdir(1) semantics is not ruled out at this
# stage, and the parent-first plain-mkdir list costs nothing extra to emit.)
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
	cat <<MID1

#
# Assemble obj/include/bits/alltypes.h out of two pre-expanded halves.
#
# The Makefile builds this header with one
#   cat $CATM_IN1 $CATM_IN2 > $CATM_OUT
# which kaem cannot run as-is: \`>\` is just another plain argument token to
# kaem, not a redirection operator, so there is nowhere for cat's stdout to
# go. mescc-tools-extra's \`catm\` exists for exactly this situation -- it
# takes the output file as its first argument and concatenates the rest
# into it (catm.c opens argv[1] with O_TRUNC|O_CREAT and copies argv[2..]
# through) -- so the whole step is one catm invocation and no redirection.
#
# Both inputs are *.h.gen files: the committed, already-expanded form of
# the compact TYPEDEF/STRUCT/UNION DSL in the matching *.h.in. The
# expansion is done at development time by tools/gen-alltypes.sh (\`make
# alltypes\`) rather than here, because it needs a capture-group rewrite
# (\`TYPEDEF unsigned _Addr size_t;\` -> a four-line __NEED_/__DEFINED_
# guarded block) that nothing available at this bootstrap point can do:
# there is no sed and no awk, and mescc-tools-extra's \`replace\` does
# literal substring substitution only. Compiling a helper on the spot is no
# way out either -- the tcc on PATH here is a *cross* compiler emitting
# win32 PE, so anything it builds will not run on the build host.
#
# Splitting the expansion from the concatenation is only sound because
# mkalltypes.sed's rules are all purely per-line (no hold space, no range
# addresses), so expanding each half separately and concatenating gives the
# same bytes as concatenating and then expanding -- verified byte-for-byte
# against the old single-sed output for both arches. The *.h.gen files are
# kept honest by the same regenerate-and-diff check that keeps this script
# honest (\`make generated\`, plus .githooks/pre-commit and CI).
#
MID1
	printf 'catm %s %s %s\n' "$CATM_OUT" "$CATM_IN1" "$CATM_IN2"
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
} >"$OUT.tmp"

# Source paths become ${srcdir}-relative; output paths stay relative to the
# working directory.  The two are already distinguishable in what make -n
# printed: everything the build *writes* is under obj/ or lib/, and
# everything it *reads* from the tree is ./arch, ./include, src/ or crt/.
#
# Without this the script can only run with the working directory set to the
# source tree, which is the one thing a from-scratch bootstrap cannot
# arrange: the sources arrive read-only (a nix store path, an unpacked
# tarball owned by the build driver), and nothing at this stage has a
# recursive copy to stage them with -- mescc-tools-extra's cp takes files,
# one at a time, and there is no shell to loop in.  With it, kaem runs in a
# writable empty directory and reads the tree wherever it is.
#
# srcdir must be set.  From the repository root that is `srcdir=.`; kaem
# substitutes an unset variable as nothing, so a forgotten one fails loudly
# on /src/... rather than quietly reading something else.
#
# The same pass turns every command name into a variable: ${bin_mkdir},
# ${bin_cp}, ${bin_catm} for the three mescc-tools-extra programs, and ${CC}
# for the cross tcc that is both compiler and archiver.  A bare name needs
# the running shell to search PATH, and kaem does not: built as a win32 PE32
# binary -- which is what it is at the point this script exists for -- it
# execs the command name as given, so `mkdir` is `Subprocess error -1' and
# only an absolute path runs.  The tools are also installed with a .exe
# suffix there, which kaem does not append either, so even a PATH-searching
# kaem would miss them.  Naming them by variable lets the build driver hand
# over the absolute, suffixed path it already knows.
#
# Anchoring each substitution at the start of the line is what keeps it from
# touching prose: every command in the emitted script begins its line, and
# every comment begins with `#'.  Only the command name is replaced, so ${CC}
# expands to the program alone and `-ar' stays tcc's *first* argument, which
# it insists on ("cannot parse -ar here" otherwise).
#
# As with srcdir, an unset one of these substitutes to nothing and the
# resulting command is malformed rather than plausible.
# The ${...} here are deliberately single-quoted: they are literal text
# emitted *into* the generated kaem file, for kaem to expand at bootstrap
# time, not values for this script to expand now. shellcheck 0.9 flags all
# six as SC2016 ("expressions don't expand in single quotes"); 0.11 does
# not, which is why this only showed up in CI.
# shellcheck disable=SC2016
sed -e 's,-I\./,-I${srcdir}/,g' \
    -e 's,\([ \t]\)\./,\1${srcdir}/,g' \
    -e 's,\([ \t]\)\(src/\|crt/\|arch/\),\1${srcdir}/\2,g' \
    -e 's,^mkdir ,${bin_mkdir} ,' \
    -e 's,^cp ,${bin_cp} ,' \
    -e 's,^catm ,${bin_catm} ,' \
    -e "s,^${CC} ,\${CC} ," \
    "$OUT.tmp" >"$OUT"
rm -f "$OUT.tmp"

rm -f "${DRYRUN}.classified"

echo "gen-kaem.sh: wrote $OUT ($(wc -l <"$OUT") lines)" >&2
