# SPDX-FileCopyrightText: (C) 2026 Gavin John
# SPDX-License-Identifier: GPL-3.0-or-later
#
# lint-decls.awk -- the shared top-level-declarator scanner.
#
# Extracted verbatim from tools/lint-undefined.sh, which had it inline,
# so tools/lint-unreferenced.sh can ask the same question of the same
# headers and get the same answer by construction rather than by two
# scanners that agree until one of them is edited.  See
# tools/lint-undefined.sh's header comment for what it recognises and
# what it deliberately does not; the two modes are:
#
#   -v MODE=decl   a header.  A declarator ends at a top-level ';'.
#   -v MODE=def    a .c file.  A declarator ends at a top-level '{', and
#                  the body's braces are then balanced and skipped.
#
# Emits "name<TAB>line" once per top-level declarator found.

function name_from(text,    m) {
	# Function returning a function pointer: "(" "*" IDENT "(".
	if (match(text, /\([ \t]*\*[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/)) {
		m = substr(text, RSTART, RLENGTH)
		sub(/^\([ \t]*\*[ \t]*/, "", m)
		sub(/[ \t]*\($/, "", m)
		return m
	}
	# Ordinary: first IDENT immediately followed by "(".
	if (match(text, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/)) {
		m = substr(text, RSTART, RLENGTH)
		sub(/[ \t]*\($/, "", m)
		return m
	}
	return ""
}
FNR == 1 {
	depth = 0; pdepth = 0; buf = ""; bufline = 0
	incomment = 0; instr = 0; inchr = 0; indirective = 0
}
{
	raw = $0
	# A preprocessor directive, or the continuation of one via a
	# trailing backslash, contributes nothing: skip the whole
	# line rather than risk its tokens (parens in a #if
	# condition, say) being mistaken for code.
	wasdirective = indirective || (raw ~ /^[ \t]*#/)
	indirective = wasdirective && (raw ~ /\\[ \t]*$/)
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
			else if (ch == "'") { inchr = 0 }
			continue
		}
		if (ch2 == "/*") { incomment = 1; i++; continue }
		if (ch == "\"") { instr = 1; continue }
		if (ch == "'") { inchr = 1; continue }

		if (depth == 0) {
			if (ch == "(") pdepth++
			else if (ch == ")") { if (pdepth > 0) pdepth-- }
			else if (ch == "{" && pdepth == 0 && MODE == "def") {
				nm = name_from(buf)
				if (nm != "" && buf !~ /^[ \t]*typedef([ \t]|$)/)
					print nm "\t" (bufline == 0 ? FNR : bufline)
				depth = 1; buf = ""; bufline = 0
				continue
			} else if (ch == "{" && pdepth == 0 && MODE == "decl") {
				# Two shapes of "{" occur in a header, and only one
				# of them nests: the extern "C" { ... } wrapper
				# almost every header has must NOT be treated as a
				# nesting level, or every real prototype inside it
				# would look nested rather than top-level and never
				# get scanned.  (Its string literal is invisible
				# here -- the instr/inchr handling above never
				# appends a quoted character to buf -- so by the
				# time "{" is reached, buf holds exactly "extern".)
				# Anything else -- a struct/union/enum body -- is
				# opaque exactly like a function body is in "def"
				# mode: nothing inside it is a top-level
				# declarator (div_t and struct sigaction'\''s
				# function-pointer *members* are data, not
				# functions), so it is skipped rather than parsed.
				trimmed = buf; sub(/^[ \t]+/, "", trimmed); sub(/[ \t]+$/, "", trimmed)
				if (trimmed == "extern") { buf = ""; bufline = 0; continue }
				depth = 1; buf = ""; bufline = 0
				continue
			} else if (ch == ";" && pdepth == 0) {
				if (MODE == "decl") {
					nm = name_from(buf)
					if (nm != "" && buf !~ /^[ \t]*typedef([ \t]|$)/)
						print nm "\t" (bufline == 0 ? FNR : bufline)
				}
				buf = ""; bufline = 0
				continue
			}
			if (buf == "" && ch !~ /[ \t]/) bufline = FNR
			buf = buf ch
		} else {
			if (ch == "{") depth++
			else if (ch == "}") { depth--; if (depth < 0) depth = 0 }
		}
	}
	if (depth == 0) buf = buf " "
}
