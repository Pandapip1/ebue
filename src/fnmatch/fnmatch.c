/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fnmatch(): the XBD 9.13 "Pattern Matching Notation" grammar --
 * literal characters, '?' (exactly one character), '*' (zero or more),
 * and bracket expressions -- against two strings already in memory.
 * No OS dependency whatsoever, so this is pure recursive-descent
 * matching with backtracking on '*'.
 *
 * Bracket expressions are handled in full: a plain set ("[abc]"), a
 * '-'-separated range ("[a-z]"), negation with either the POSIX '!' or
 * the historical '^' spelling ("[!a-z]"/"[^a-z]"), and named character
 * classes ("[[:digit:]]"), any mix of which may appear together in one
 * expression.  A ']' as the very first member (right after '[' or the
 * negation marker) is a literal ']', per fnmatch.html's cross-reference
 * to the shell pattern matching notation.
 *
 * FNM_PATHNAME and FNM_PERIOD are both implemented by tracking, at
 * every position in `string`, whether that position is "leading" --
 * the true start of the string, or (only when FNM_PATHNAME is set)
 * immediately after a '/' -- since that is the only thing '*', '?',
 * and bracket expressions need to refuse under FNM_PERIOD; an explicit
 * literal in the pattern (e.g. the '.' in ".*") is never restricted.
 */
#include <fnmatch.h>
#include <ctype.h>
#include <string.h>

static int class_match(const char *name, size_t len, unsigned char c)
	__attribute__((pure));
static int class_match(const char *name, size_t len, unsigned char c) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
#define CLS(s) (len == sizeof(s) - 1 && !memcmp(name, s, len))
	if (CLS("alpha")) return isalpha(c) != 0;
	if (CLS("digit")) return isdigit(c) != 0;
	if (CLS("alnum")) return isalnum(c) != 0;
	if (CLS("upper")) return isupper(c) != 0;
	if (CLS("lower")) return islower(c) != 0;
	if (CLS("space")) return isspace(c) != 0;
	if (CLS("blank")) return isblank(c) != 0;
	if (CLS("cntrl")) return iscntrl(c) != 0;
	if (CLS("print")) return isprint(c) != 0;
	if (CLS("graph")) return isgraph(c) != 0;
	if (CLS("punct")) return ispunct(c) != 0;
	if (CLS("xdigit")) return isxdigit(c) != 0;
#undef CLS
	return 0;
}

struct bracket_result {
	const char *next;
	int match;
};

/* p points at the '[' that opens the bracket expression; next points past
 * the matching ']' on return.  match is 1 if c is a member (after applying
 * negation), 0 if not, and -1 if the expression is UNTERMINATED -- no ']'
 * before the end of the pattern -- in which case next remains at the '['
 * and the caller must treat it as an ordinary character.
 *
 * XCU 2.13.1: "Otherwise, the <left-square-bracket> shall match the
 * character itself."  This used to walk to the end looking for a ']'
 * and, not finding one, return the accumulated match state anyway -- so
 * an unterminated '[' was consumed as if it had been a bracket
 * expression instead of being demoted to a literal.  fnmatch("[abc",
 * "[abc", 0), fnmatch("a[b", "a[b", 0) and fnmatch("[", "[", 0) all
 * gave FNM_NOMATCH where POSIX requires 0; glibc, musl and the BSDs all
 * match, and glibc was re-measured to confirm it before this change.
 *
 * Note this differs deliberately from the regular-expression grammar,
 * where the same input is an error -- test_regex_bracket_edges()
 * asserts REG_EBRACK for regcomp("[abc").  The two pattern languages
 * are not the same language and this is one of the places they part. */
static struct bracket_result bracket_match(const char *p, unsigned char c)
	__attribute__((nonnull(1), pure));
static struct bracket_result bracket_match(const char *p, unsigned char c)
{
	struct bracket_result result = { p, 0 };
	int neg = 0, matched = 0, first = 1;

	p++;

	if (*p == '!' || *p == '^') {
		neg = 1;
		p++;
	}
	while (*p && (first || *p != ']')) {
		first = 0;
		if (p[0] == '[' && p[1] == ':') {
			const char *q = strstr(p + 2, ":]");
			if (q) {
				if (class_match(p + 2, (size_t)(q - (p + 2)), c))
					matched = 1;
				p = q + 2;
				continue;
			}
			/* A class introducer without its ":]" delimiter is a
			 * malformed bracket expression, not a set containing '[' and
			 * ':'.  Keep this distinct from an unterminated ordinary set,
			 * whose opening '[' is demoted to a literal by XCU 2.13.1. */
			result.match = -2;
			return result;
		}
		if (p[0] == '[' && (p[1] == '.' || p[1] == '=')) {
			char kind = p[1];
			const char *q = p + 2;
			while (*q && !(q[0] == kind && q[1] == ']')) q++;
			if (*q && q == p + 3) {
				if (c == (unsigned char)p[2]) matched = 1;
				p = q + 2;
				continue;
			}
		}
		{
			unsigned char lo = (unsigned char)*p;
			p++;
			if (*p == '-' && p[1] && p[1] != ']') {
				unsigned char hi;
				p++;
				hi = (unsigned char)*p;
				p++;
				if (c >= lo && c <= hi) matched = 1;
			} else {
				if (c == lo) matched = 1;
			}
		}
	}
	/* Loop exits on ']' or on end-of-pattern; in the latter case next
	 * deliberately stays at the opening '['. */
	if (*p != ']') {
		result.match = -1;
		return result;
	}
	result.next = p + 1;
	result.match = neg ? !matched : matched;
	return result;
}

/* s is required: `s[-1]` (reached whenever s != start and
 * FNM_PATHNAME is set) is the only real dereference in this function,
 * and there is no documented "s may be invalid" case. start is
 * deliberately NOT marked -- it is only ever compared by pointer
 * equality (`s == start`), never dereferenced anywhere in this
 * function's own body. */
static int leading(const char *start, const char *s, int flags)
	__attribute__((nonnull(2), pure));
static int leading(const char *start, const char *s, int flags)
{
	if (s == start) return 1;
	if ((flags & FNM_PATHNAME) && s[-1] == '/') return 1;
	return 0;
}

/* pat is dereferenced unconditionally by the main loop's own `while
 * (*pat)`; s is required too -- every path through this function
 * dereferences it somewhere, whether inside the loop (`*pat != *s`,
 * the first comparison an empty-pattern-free call reaches) or, for an
 * already-empty pattern, the final `return *s ? ... : 0;`. start is
 * left unmarked, the same "only ever compared, never dereferenced"
 * reasoning as leading() above (it is forwarded into leading() itself,
 * which only compares it too). */
static int fnm_match(const char *pat, const char *s, const char *start, int flags)
    __attribute__((nonnull(1, 2), pure));
static int fnm_match(const char *pat, const char *s, const char *start, int flags)
{
	while (*pat) {
		if (*pat == '*') {
			while (*pat == '*') pat++;
			if (!*pat) {
				if ((flags & FNM_PATHNAME) && strchr(s, '/')) return FNM_NOMATCH;
				if ((flags & FNM_PERIOD) && *s == '.' && leading(start, s, flags)) return FNM_NOMATCH;
				return 0;
			}
			for (;;) {
				if (fnm_match(pat, s, start, flags) == 0) return 0;
				if (!*s) return FNM_NOMATCH;
				if ((flags & FNM_PATHNAME) && *s == '/') return FNM_NOMATCH;
				if ((flags & FNM_PERIOD) && *s == '.' && leading(start, s, flags)) return FNM_NOMATCH;
				s++;
			}
		} else if (*pat == '?') {
			if (!*s) return FNM_NOMATCH;
			if ((flags & FNM_PATHNAME) && *s == '/') return FNM_NOMATCH;
			if ((flags & FNM_PERIOD) && *s == '.' && leading(start, s, flags)) return FNM_NOMATCH;
			pat++;
			s++;
		} else if (*pat == '[') {
			unsigned char c = (unsigned char)*s;
			struct bracket_result probe = { pat, -1 };
			int r;
			/* Probed before the FNM_PATHNAME/FNM_PERIOD guards below,
			 * because those describe what a BRACKET EXPRESSION may
			 * match; an unterminated '[' is not one, and must be judged
			 * as the ordinary character it is. */
			if (*s) probe = bracket_match(pat, c);
			r = probe.match;
			if (r == -2) return FNM_NOMATCH;
			if (r < 0) {
				/* not a bracket expression: a literal '[' */
				if (*s != '[') return FNM_NOMATCH;
				pat++;
				s++;
				continue;
			}
			if (!*s) return FNM_NOMATCH;
			if ((flags & FNM_PATHNAME) && c == '/') return FNM_NOMATCH;
			if ((flags & FNM_PERIOD) && c == '.' && leading(start, s, flags)) return FNM_NOMATCH;
			if (!r) return FNM_NOMATCH;
			pat = probe.next;
			s++;
		} else if (*pat == '\\' && !(flags & FNM_NOESCAPE)) {
			if (!pat[1]) return FNM_NOMATCH; /* trailing unescaped backslash: malformed */
			pat++;
			if (*pat != *s) return FNM_NOMATCH;
			pat++;
			s++;
		} else {
			if (*pat != *s) return FNM_NOMATCH;
			pat++;
			s++;
		}
	}
	return *s ? FNM_NOMATCH : 0;
}

int fnmatch(const char *pattern, const char *string, int flags)
{
	return fnm_match(pattern, string, string, flags);
}
