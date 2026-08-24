#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# linkcheck.sh -- prove that a user can actually link a call to every
# function ntlibc declares in a public header, for the arch this tree is
# currently configured for.
#
# The bug class: a header can declare a function that no program can
# actually link against.  include/alloca.h used to do exactly this --
# `#define alloca __builtin_alloca` unconditionally, which tcc (having no
# such builtin) turned into `unresolved reference to '__builtin_alloca'`
# at link time for every caller, silently defeating arch/*/src/alloca.S,
# which exists precisely so tcc can call alloca as an ordinary function.
# It shipped because nothing in test/ ever called alloca.
#
# tools/lint-undefined.sh already catches the *textual* half of this (a
# prototype with no matching definition anywhere in src/arch/crt), but it
# cannot see the alloca shape at all: alloca(3) *is* defined, by
# arch/*/src/alloca.S, so lint-undefined.sh's declared-vs-defined name
# match is satisfied and it never notices that a macro at the call site
# silently retargeted every caller to a name that isn't.  Nothing short
# of actually compiling and linking a real call through the header, the
# same way a user's own TU would, catches that -- which is the entire
# reason this script exists.
#
# Method (see "why a real call, not just &func" below):
#
#   1. Mechanically extract every function declared in include/**/*.h
#      (plus obj/include's generated headers) using the same
#      comment/string-stripping, brace/paren-nesting scanner
#      tools/lint-undefined.sh uses for its own "decl" pass, extended
#      here to also capture each declaration's fixed argument count.
#      Never hand-listed: add a header, add a prototype, and the next
#      run sees it with no change to this script.
#
#   2. For every declared name not covered by an `undefined-ok:` marker
#      (see tools/lint-undefined.sh's header for that convention -- this
#      script reuses the exact same markers as its exception list rather
#      than keeping a second, parallel one that could drift from the
#      first: setrlimit and select carry theirs today), emit a tiny
#      translation unit:
#
#          #include <the header that declared it>
#          void __linkcheck_NAME(void) { (void)NAME(0, 0, ...); }
#
#      with as many zero arguments as the prototype's fixed parameter
#      count (variadic functions get just the fixed ones; tcc does not
#      require more).  Every such TU is compiled and linked, individually,
#      against this arch's freshly built lib/crt1.o + lib/libc.a +
#      lib/ntdll.def -- the exact recipe the Makefile's obj/test/%.exe
#      rule uses for a real test binary -- with $(CC), never a host
#      compiler, so this sees precisely what a user's own build would.
#
#   3. Every failure (compile or link -- both mean "cannot use this
#      symbol") is collected and reported together, grouped, instead of
#      the run dying at the first one.
#
# Why a real call and not just `(void)&NAME`: taking the address does
# catch alloca's exact bug (an *object-like* macro rename: `#define
# alloca __builtin_alloca` turns `&alloca` into `&__builtin_alloca`,
# which fails), but it does NOT catch a *function-like* macro overriding
# a declared name (`#define foo(x) __foo(x)`) -- the preprocessor only
# expands a function-like macro when the name is followed by `(`, so a
# bare `&foo` silently refers to the un-expanded literal `foo` and can
# pass even when every real call would not.  A real call `foo(0)`
# triggers expansion either way and reproduces the historical failure
# faithfully: reverting the alloca.h fix locally and running this script
# reports the identical `tcc: error: unresolved reference to
# '__builtin_alloca'` the original bug report described (see the commit
# that introduced this script for a transcript).  No function-like macro
# shadows a declared prototype's name anywhere in include/ today (checked
# by hand at the time this was written), so the fixed-arity-zero-argument
# call this script generates is exact for every symbol currently declared;
# if one is ever added with a *different* arity than its own prototype,
# preprocessing that call would fail loudly (a macro/prototype arity
# mismatch), which is itself a finding worth seeing, not a false pass.
#
# Categories this does not, and cannot mechanically, cover:
#   - object-like macros that are never declared as a prototype at all
#     (assert, isdigit-the-macro-that-doesn't-exist-here, WIFEXITED,
#     FD_SET, va_start, ...) never appear in the "declared" list in the
#     first place, because they have no `RET NAME(...);` line to scan --
#     nothing to except, nothing to check.
#   - `static inline`/`static __inline` functions defined with a body in
#     the header itself (__bswap16/32/64 in endian.h): the scanner's decl
#     mode treats the body's `{` as opening an opaque nested block and
#     never emits a top-level declarator for it, exactly like
#     lint-undefined.sh -- correctly: nothing to link, the definition is
#     right there in every including TU.
#   - a symbol legitimately declared-and-unimplemented is not a failure
#     of this script; it must carry an `undefined-ok:` marker (checked by
#     lint-undefined.sh too) or this script reports it same as any other
#     unlinkable symbol.
#
# Usage: tools/linkcheck.sh
#   Requires the tree to already be `./configure`d and `make`d for the
#   arch to check (reads config.mak via the Makefile's own `linkcheck`
#   target, which passes CC/ARCH/flags in).  Run once per arch -- see
#   `make linkcheck`'s own comment for why this cannot check both arches
#   in one invocation: arch/ is not shared, so lib/crt1.o and lib/libc.a
#   are only ever built for whichever arch config.mak currently names.
#
# Environment (all normally supplied by `make linkcheck`):
#   CC, ARCH, CFLAGS_C99FSE, CFLAGS_AUTO   as in config.mak
#   LINKCHECK_STRICT=0   always exit 0 (report only)

set -u

# shellcheck disable=SC1007
srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$srcdir" || exit 1

: "${CC:?linkcheck.sh needs CC set (run via 'make linkcheck', not directly)}"
: "${ARCH:?linkcheck.sh needs ARCH set (run via 'make linkcheck', not directly)}"
: "${CFLAGS_C99FSE:=-std=c99 -nostdinc -fno-builtin}"
: "${CFLAGS_AUTO:=}"
: "${LINKCHECK_STRICT:=1}"

for f in lib/crt1.o lib/libc.a lib/ntdll.def; do
	[ -f "$f" ] || { echo "linkcheck: $f missing -- run 'make' first" >&2; exit 1; }
done

builddir=obj/linkcheck
rm -rf "$builddir"
mkdir -p "$builddir" || exit 1

INC="-I$srcdir/arch/$ARCH -I$srcdir/arch/generic -Iobj/include -I$srcdir/include"
CFLAGS="$CFLAGS_C99FSE $CFLAGS_AUTO -D_XOPEN_SOURCE=700 -D_ALL_SOURCE $INC"

# ---------------------------------------------------------------------
# The scanner.  Same character-at-a-time comment/string-literal-stripping
# nesting tracker as tools/lint-undefined.sh's "decl" mode (see that
# file's header for the full rationale of each choice: why the
# `extern "C" { ... }` wrapper is not treated as a nesting level, why a
# one-line `{ ... }` body -- the endian.h inline bswaps -- is correctly
# swallowed as an opaque block and never emitted, etc).  Extended here to
# also record, per declarator, the header it came from, whether the line
# carries an `undefined-ok:` marker, and the declaration's fixed argument
# count.
#
# Output: one line per declared function, tab-separated:
#   name  header  fixed_argc_or_V  undefined_ok(0/1)
#
# fixed_argc is the count of comma-separated top-level parameter groups,
# with a lone `void` counted as 0 and a trailing `...` dropped from the
# count (its presence doesn't change fixed_argc, since ntlibc calls a
# variadic function with only its required arguments).
# ---------------------------------------------------------------------
scan() {
	awk '
		function name_and_args(text,    m) {
			if (match(text, /\([ \t]*\*[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/)) {
				m = substr(text, RSTART, RLENGTH)
				ARGSTART = RSTART + RLENGTH
				sub(/^\([ \t]*\*[ \t]*/, "", m)
				sub(/[ \t]*\($/, "", m)
				return m
			}
			if (match(text, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/)) {
				m = substr(text, RSTART, RLENGTH)
				ARGSTART = RSTART + RLENGTH
				sub(/[ \t]*\($/, "", m)
				return m
			}
			ARGSTART = 0
			return ""
		}
		# Extract the raw text between the arg-list "(" (already consumed;
		# start is the index of the first char after it) and its matching
		# ")", honouring nested parens (function-pointer parameters).
		function extract_args(text, start,    d, i, c, n, out) {
			d = 1; out = ""; n = length(text)
			for (i = start; i <= n; i++) {
				c = substr(text, i, 1)
				if (c == "(") d++
				else if (c == ")") { d--; if (d == 0) break }
				out = out c
			}
			return out
		}
		# Split argstext on top-level commas (not inside nested parens)
		# and print the fixed argument count.
		function argcount(argstext,    d, i, c, n, trimmed, n_fields, field, out) {
			trimmed = argstext
			gsub(/^[ \t]+|[ \t]+$/, "", trimmed)
			if (trimmed == "" || trimmed == "void") return 0
			d = 0; n_fields = 1; field = ""; n = length(trimmed)
			for (i = 1; i <= n; i++) {
				c = substr(trimmed, i, 1)
				if (c == "(") d++
				else if (c == ")") d--
				else if (c == "," && d == 0) { n_fields++; continue }
			}
			# A trailing "..." is its own top-level field; it does not
			# add to the fixed count.
			nlast = trimmed
			sub(/^.*,/, "", nlast)
			gsub(/^[ \t]+|[ \t]+$/, "", nlast)
			if (nlast == "...") n_fields--
			return n_fields
		}
		FNR == 1 {
			depth = 0; pdepth = 0; buf = ""; hasmark = 0
			incomment = 0; instr = 0; inchr = 0; indirective = 0
		}
		{
			raw = $0
			wasdirective = indirective || (raw ~ /^[ \t]*#/)
			indirective = wasdirective && (raw ~ /\\[ \t]*$/)
			if (index(raw, "undefined-ok:") > 0) hasmark = 1
			if (wasdirective) next

			n = length(raw)
			for (i = 1; i <= n; i++) {
				ch = substr(raw, i, 1)
				ch2 = substr(raw, i, 2)

				if (incomment) {
					if (ch2 == "*/") { incomment = 0; i++ }
					continue
				}
				if (instr) {
					if (ch == "\\") { i++ }
					else if (ch == "\"") { instr = 0 }
					continue
				}
				if (inchr) {
					if (ch == "\\") { i++ }
					else if (ch == "'"'"'") { inchr = 0 }
					continue
				}
				if (ch2 == "/*") { incomment = 1; i++; continue }
				if (ch == "\"") { instr = 1; continue }
				if (ch == "'"'"'") { inchr = 1; continue }

				if (depth == 0) {
					if (ch == "(") pdepth++
					else if (ch == ")") { if (pdepth > 0) pdepth-- }
					else if (ch == "{" && pdepth == 0) {
						trimmed = buf; sub(/^[ \t]+/, "", trimmed); sub(/[ \t]+$/, "", trimmed)
						if (trimmed == "extern") { buf = ""; hasmark = 0; continue }
						# Any other "{" at top level -- a struct/union/enum
						# body, or a static-inline function body defined
						# right here -- is opaque: skip it, emit nothing.
						depth = 1; buf = ""; hasmark = 0
						continue
					} else if (ch == ";" && pdepth == 0) {
						if (buf !~ /^[ \t]*typedef([ \t]|$)/) {
							nm = name_and_args(buf)
							if (nm != "") {
								args = extract_args(buf, ARGSTART)
								print nm "\t" FILENAME "\t" argcount(args) "\t" (hasmark ? 1 : 0)
							}
						}
						buf = ""; hasmark = 0
						continue
					}
					buf = buf ch
				} else {
					if (ch == "{") depth++
					else if (ch == "}") { depth--; if (depth < 0) depth = 0 }
				}
			}
			if (depth == 0) buf = buf " "
		}
	' "$@"
}

headers=$(find include obj/include -type f -name '*.h' 2>/dev/null | sort)

declfile="$builddir/declared"
# $headers is a list of file names and must word-split.
# shellcheck disable=SC2086
scan $headers > "$declfile"

# undefined-ok reason, for the report: the marker's own comment line(s),
# collapsed to one line.  Mirrors tools/lint-undefined.sh's own
# markednames pass, but this script only needs the free text, not a
# name list -- names come straight from column 4 of $declfile above.
reason_for() {
	nm=$1 hdr=$2
	awk -v NM="$nm" '
		index($0, NM) && index($0, "undefined-ok:") {
			line = $0
			sub(/^.*undefined-ok:[ \t]*/, "", line)
			gsub(/\*\//, "", line)
			gsub(/^[ \t]+|[ \t]+$/, "", line)
			# The reason often starts on the *next* line ("undefined-ok:"
			# with nothing else on its own line) -- pull it in too, so the
			# report is never just a bare "undefined-ok:" with no text.
			if (line == "" && (getline nextline) > 0) {
				sub(/^[ \t]*\*?[ \t]*/, "", nextline)
				gsub(/\*\//, "", nextline)
				gsub(/^[ \t]+|[ \t]+$/, "", nextline)
				line = nextline
			}
			print line; exit
		}
	' "$hdr"
}

# A second, small exception list, in the same shape as
# tools/asan-build.sh's not_native(): symbols that are genuinely defined
# in the library (so `undefined-ok:` above does not apply, and
# lint-undefined.sh correctly reports nothing) but that cannot link from
# a standalone TU like the ones this script generates, because their
# *contract* requires the calling program itself to supply another
# symbol.  Found by running this script once and reading the failures:
# every ntlibc_rpath_*/ntlibc_delayLoadHelper2 entry point resolves
# `__rpath` (include/ntlibc/rpath.h) as an extern array the *executable*
# is documented to define (see test/rpath.c for a real definition) --
# libc.a deliberately does not carry one, the same way it does not carry
# a definition of `main`. A generated TU that only calls the function,
# the way every other symbol here is checked, would always be missing
# that array and would misreport a real, working library entry point as
# broken.
#
# dlopen()/dlsym()/dlclose()/dlerror() (include/dlfcn.h, src/dlfcn/
# dlfcn.c) and ntlibc_rpath_unload()/ntlibc_rpath_error_seq()
# (include/ntlibc/rpath.h, src/internal/rpath.c) join the list for the
# same reason at one remove: dlopen()/dlsym()/dlclose() call straight
# into ntlibc_rpath_load()/_sym()/_unload(), and dlerror() into
# ntlibc_rpath_error_seq() (which shares rpath.c's one translation unit
# and so pulls in the same unresolved `__rpath`, even though it does
# not read that array itself) -- a generated single-call TU for any of
# these six hits the identical missing-`__rpath` link failure, for a
# reason that has nothing to do with whether the function itself works.
#
# hsearch() (include/search.h, src/search/hsearch.c) is a different
# shape of the same "the generator, not the symbol, is the problem"
# story: basedefs/search.h.html's ENTRY is passed *by value*
# (`ENTRY hsearch(ENTRY item, ACTION action)` -- ACTION is an enum, so
# its own `0` argument is fine), and this script's call-site generator
# (see "method" above) always fills every argument slot with the
# literal `0`. `0` does not implicitly convert to a struct type in C,
# so the generated `hsearch(0, 0)` fails to compile -- not because
# hsearch is unlinkable (test/posix-glob.c calls it with a real ENTRY
# literal and it works), but because a bare `0` can never stand in for
# a byval struct argument no matter what the callee does with it.
#
# inet_ntoa() (include/arpa/inet.h, src/socket/inet.c) is the identical
# by-value-struct shape: `char *inet_ntoa(struct in_addr)` takes its
# only argument by value, so the generated `inet_ntoa(0)` hits the same
# "0 cannot convert to a struct type" compile failure, not a real link
# problem (test/posix-socket.c calls it with a real struct in_addr and
# it works).
linkcheck_exception() {
	case $1 in
	ntlibc_rpath_load|ntlibc_rpath_sym|ntlibc_rpath_error|ntlibc_rpath_fail|ntlibc_delayLoadHelper2|ntlibc_rpath_unload|ntlibc_rpath_error_seq|dlopen|dlsym|dlclose|dlerror)
		echo "resolves __rpath (include/ntlibc/rpath.h), an extern array the *calling program* is documented to define (see test/rpath.c) -- not a libc symbol, so no standalone TU can supply it" ;;
	hsearch)
		echo "takes ENTRY by value (basedefs/search.h.html); this script's generated call site fills every argument with a literal 0, which cannot convert to a struct type -- a generator limitation, not an unlinkable symbol (see test/posix-glob.c's real ENTRY-literal call)" ;;
	inet_ntoa)
		echo "takes struct in_addr by value (include/arpa/inet.h); this script's generated call site fills every argument with a literal 0, which cannot convert to a struct type -- a generator limitation, not an unlinkable symbol (see test/posix-socket.c's real struct in_addr call)" ;;
	*) echo "" ;;
	esac
}

# ---------------------------------------------------------------------
# PE header sanity check.
#
# Wine's PE loader and the real Windows NT loader disagree about which
# header fields they actually enforce -- measured directly on real
# Windows 11 Pro 22621 by building header variants of the same program
# and launching each on the true NT loader (see the commit that added
# this check for the full transcript):
#
#   - SectionAlignment below the page size (0x1000), *even with
#     FileAlignment set to match it* -- a combination the PE/COFF spec
#     (section "Optional Header Windows-Specific Fields", the
#     SectionAlignment entry) explicitly permits -- makes the real NT
#     loader reject the image outright with ERROR_BAD_EXE_FORMAT (193),
#     "is not a valid Win32 application". Wine loads the identical image
#     without complaint.
#   - SectionAlignment == 0x1000 loads and runs on real Windows even
#     when SizeOfRawData is not a multiple of FileAlignment, which the
#     spec says it MUST be -- ReactOS's ntoskrnl/mm/section.c documents
#     deliberately disabling that exact check, with a comment noting
#     real-world images violate it and the loader copes. So that field
#     is a confirmed non-hazard on the "spec MUST but nobody enforces
#     it" side and is deliberately NOT asserted here.
#
# Every test this project runs, runs under Wine (tools/runtests.sh);
# none of them would ever notice a SectionAlignment regression. This
# check exists so a static header inspection catches it instead, on
# every .exe tools/linkcheck.sh builds.
#
# Read with `od`, not objdump/readelf or a PE-parsing library: this
# script has no dependency today beyond awk/sh/the target $CC, od is a
# POSIX utility (IEEE Std 1003.1-2017), and the three fields needed
# (Magic, SectionAlignment, FileAlignment) sit at fixed byte offsets
# from e_lfanew, so a handful of `od -j/-N` reads is simpler and more
# portable than adding a new tool dependency for three integers this
# script's own build-and-link recipe already produced. This stays out
# of the boot/kaem/ bootstrap path entirely -- that path never invokes
# `make linkcheck`, only tcc + mkdir/cp/catm (see gen-kaem.sh).
#
# Bytes are combined explicitly as little-endian (PE's byte order,
# always) rather than via `od`'s native-endian numeric types, so this
# is correct even run on a big-endian host.
#
# Standard fields before the Windows-specific fields are 28 bytes long
# in a PE32 optional header (through BaseOfData) and 24 bytes in PE32+
# (no BaseOfData), but ImageBase is 4 bytes in PE32 and 8 in PE32+, so
# the two differences exactly cancel: SectionAlignment lands at
# optional-header offset 32, and FileAlignment at offset 36, in both
# PE32 and PE32+ alike.
# ---------------------------------------------------------------------
pe_u8_list() {
	# pe_u8_list FILE OFFSET COUNT -- COUNT decimal byte values, one per line.
	od -An -tu1 -j "$2" -N "$3" "$1" | tr -s ' \t' '\n' | sed '/^$/d'
}

pe_le() {
	# pe_le FILE OFFSET COUNT -- little-endian unsigned integer built
	# from COUNT bytes (COUNT <= 4) read at OFFSET in FILE.
	pe_le_val=0 pe_le_mul=1
	for pe_le_byte in $(pe_u8_list "$1" "$2" "$3"); do
		pe_le_val=$((pe_le_val + pe_le_byte * pe_le_mul))
		pe_le_mul=$((pe_le_mul * 256))
	done
	echo "$pe_le_val"
}

pe_header_check() {
	# pe_header_check EXE -- prints nothing and returns 0 if EXE's PE
	# header passes every check above; otherwise prints one line per
	# violation (to stdout -- callers redirect) and returns nonzero.
	img=$1
	case $ARCH in
	x86_64) pe_want_magic=$((0x20b)); pe_want_name="PE32+" ;;
	i386)   pe_want_magic=$((0x10b)); pe_want_name="PE32" ;;
	*)
		echo "linkcheck: ARCH='$ARCH' has no known expected PE Magic -- add it to pe_header_check() in tools/linkcheck.sh"
		return 1 ;;
	esac

	set -- $(pe_u8_list "$img" 0 2)
	if [ "${1:-0}" -ne 77 ] || [ "${2:-0}" -ne 90 ]; then
		echo "$img: no MZ signature at offset 0 -- not a PE image"
		return 1
	fi

	pe_off=$(pe_le "$img" 60 4)
	set -- $(pe_u8_list "$img" "$pe_off" 4)
	if [ "${1:-0}" -ne 80 ] || [ "${2:-0}" -ne 69 ] || [ "${3:-0}" -ne 0 ] || [ "${4:-0}" -ne 0 ]; then
		echo "$img: no PE signature at e_lfanew offset $pe_off"
		return 1
	fi

	opt_off=$((pe_off + 4 + 20))
	pe_magic=$(pe_le "$img" "$opt_off" 2)
	pe_sec_align=$(pe_le "$img" $((opt_off + 32)) 4)
	pe_file_align=$(pe_le "$img" $((opt_off + 36)) 4)

	pe_ok=1
	if [ "$pe_magic" -ne "$pe_want_magic" ]; then
		printf '%s: Magic 0x%x, expected 0x%x (%s, for ARCH=%s)\n' \
			"$img" "$pe_magic" "$pe_want_magic" "$pe_want_name" "$ARCH"
		pe_ok=0
	fi
	if [ "$pe_sec_align" -lt 4096 ]; then
		printf '%s: SectionAlignment 0x%x is below the page size (0x1000). The real NT loader rejects this outright with ERROR_BAD_EXE_FORMAT (193), "is not a valid Win32 application" -- even when FileAlignment is set to match it, which the PE/COFF spec explicitly permits. Measured on real Windows 11 Pro 22621; Wine loads such an image fine, so this project'"'"'s Wine-based test suite can never catch it (see the comment above pe_header_check() in tools/linkcheck.sh).\n' \
			"$img" "$pe_sec_align"
		pe_ok=0
	fi
	if [ "$pe_sec_align" -lt "$pe_file_align" ]; then
		printf '%s: SectionAlignment 0x%x is less than FileAlignment 0x%x; the PE/COFF spec requires SectionAlignment >= FileAlignment unconditionally.\n' \
			"$img" "$pe_sec_align" "$pe_file_align"
		pe_ok=0
	fi
	[ "$pe_ok" -eq 1 ]
}

total=0 checked=0 excepted=0 failed=0
: > "$builddir/failures"
: > "$builddir/exceptions"
: > "$builddir/pe-seen"

while IFS="$(printf '\t')" read -r nm hdr argc marked; do
	[ -z "$nm" ] && continue
	total=$((total + 1))
	if [ "$marked" = 1 ]; then
		excepted=$((excepted + 1))
		printf '%s (%s): %s\n' "$nm" "$hdr" "$(reason_for "$nm" "$hdr")" >> "$builddir/exceptions"
		continue
	fi
	why=$(linkcheck_exception "$nm")
	if [ -n "$why" ]; then
		excepted=$((excepted + 1))
		printf '%s (%s): %s\n' "$nm" "$hdr" "$why" >> "$builddir/exceptions"
		continue
	fi
	checked=$((checked + 1))

	args=""
	i=0
	while [ "$i" -lt "$argc" ]; do
		args="$args${args:+, }0"
		i=$((i + 1))
	done

	src="$builddir/$nm.c"
	obj="$builddir/$nm.o"
	exe="$builddir/$nm.exe"
	{
		printf '#include <%s>\n' "${hdr#include/}"
		printf 'void __linkcheck_%s(void) { (void)%s(%s); }\n' "$nm" "$nm" "$args"
		printf 'int main(void) { return 0; }\n'
	} > "$src"

	# shellcheck disable=SC2086
	if ! $CC $CFLAGS -c -o "$obj" "$src" 2> "$obj.err"; then
		failed=$((failed + 1))
		{
			printf '%s (%s, %s args) -- COMPILE FAILED:\n' "$nm" "$hdr" "$argc"
			sed 's/^/    /' "$obj.err"
		} >> "$builddir/failures"
		continue
	fi
	# shellcheck disable=SC2086
	if ! $CC $CFLAGS -nostdlib -o "$exe" lib/crt1.o "$obj" -Llib -lc -lntdll 2> "$exe.err"; then
		failed=$((failed + 1))
		{
			printf '%s (%s, %s args) -- LINK FAILED:\n' "$nm" "$hdr" "$argc"
			sed 's/^/    /' "$exe.err"
		} >> "$builddir/failures"
		continue
	fi

	if ! pehdr=$(pe_header_check "$exe" 2>&1); then
		failed=$((failed + 1))
		# Every .exe built this run shares the same $CC/$CFLAGS/link
		# recipe, so a PE header regression is not per-symbol -- it is
		# the same header field, wrong the same way, in every failing
		# .exe. Print the full reason once per distinct message (keyed
		# on the text with this file's own path stripped out) and fold
		# repeats into a one-line pointer, so a real regression's
		# report stays readable instead of repeating the same
		# paragraph once per declared symbol (hundreds of times).
		pe_key=$(printf '%s\n' "$pehdr" | sed "s#$exe#<exe>#g")
		if grep -qxF "$pe_key" "$builddir/pe-seen" 2>/dev/null; then
			printf '%s (%s, %s args) -- PE HEADER CHECK FAILED: %s (same reason as the first instance above; see %s)\n' \
				"$nm" "$hdr" "$argc" "$(printf '%s\n' "$pehdr" | head -1)" "$exe" >> "$builddir/failures"
		else
			printf '%s\n' "$pe_key" >> "$builddir/pe-seen"
			{
				printf '%s (%s, %s args) -- PE HEADER CHECK FAILED:\n' "$nm" "$hdr" "$argc"
				printf '%s\n' "$pehdr" | sed 's/^/    /'
			} >> "$builddir/failures"
		fi
	fi
done < "$declfile"

echo "linkcheck [$ARCH]: $checked symbol(s) checked, $excepted excepted (undefined-ok), $failed unlinkable, out of $total declared"

if [ "$excepted" -gt 0 ]; then
	echo ""
	echo "excepted (declared, deliberately unimplemented, see the header for the full reason):"
	sort -u "$builddir/exceptions" | sed 's/^/  /'
fi

if [ "$failed" -eq 0 ]; then
	echo "linkcheck [$ARCH]: no findings"
	exit 0
fi

echo ""
echo "unlinkable symbols -- declared in a public header, but a program cannot link a call to them:"
cat "$builddir/failures"
echo "linkcheck [$ARCH]: $failed finding(s); full logs under $builddir/"
[ "$LINKCHECK_STRICT" = 0 ] && exit 0
exit 1
