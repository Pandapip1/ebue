/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * man(1p): find a manual page by name (and, as a near-universal
 * historical extension POSIX itself does not specify, by section) and
 * format it for the terminal.
 *
 * ============================================================
 * WHAT THIS IS, AND WHY IT IS SHAPED THIS WAY
 * ============================================================
 *
 * A real `man` has two genuinely separate jobs: FIND the right page,
 * and FORMAT it. Formatting a real man page means running a real
 * troff/groff interpreter over `man`- or `mdoc`-macro-package source.
 * Building a general troff engine is its own enormous project (real
 * groff is enormous) and is explicitly out of scope here. What IS in
 * scope, per this project's own explicit direction, is a real parser
 * and formatter for the specific macro subset the overwhelming
 * majority of real-world man pages actually use: .TH, .SH/.SS,
 * .TP/.IP, .PP/.LP, .B/.I and the six standard alternating-font pairs
 * (.BI/.BR/.IR plus, for free from the same one shared helper, the
 * other three of the six: .IB/.RB/.RI), .RS/.RE, .nf/.fi, .br (a
 * one-line addition beyond the originally named set -- see man_
 * process_line()'s own ".br" case for why real pages, including this
 * project's own SYNOPSIS sections and the real GNU grep.1 fixture
 * below, make it a practical necessity rather than scope creep), and
 * a documented common subset of escape sequences. This file IS that
 * engine, not a wrapper around a real one.
 *
 * ---- WHAT IS DELIBERATELY NOT IMPLEMENTED, AND WHY --------------------
 *
 *  - .TS/.TE (tbl tables) and .EQ/.EN (eqn equations): each is its own
 *    real sub-language with its own grammar, comparable in size to
 *    this whole file. A page using either degrades gracefully (see
 *    "UNKNOWN-MACRO DEGRADATION" below) -- the table/equation's own
 *    markup lines are skipped rather than dumped as raw garbage, which
 *    means the reader loses that one table/equation's content but
 *    every other section of the same page still renders correctly.
 *  - .de/.ig (user macro definitions) -- a real `man` page in the
 *    wild (this project's own test fixture, a real, unmodified copy of
 *    GNU grep's grep.1, proves this is not a theoretical concern) can
 *    open a `.de NAME` ... `..` block whose BODY LINES are template
 *    text for a macro that is never invoked here (since custom macros
 *    aren't executed at all) -- so those lines must never be treated
 *    as real page content, or output would be corrupted with literal
 *    macro-definition source (e.g. a raw `\$1` placeholder). This
 *    file tracks .de/.ig spans specifically (skip-until-terminating
 *    ".." line) to prevent exactly that corruption; see in_macro_def
 *    below. This is the one piece of "custom macro" handling here, and
 *    it exists purely to avoid corruption, not to execute anything.
 *  - .if/.ie/.el (conditional text) -- real condition evaluation
 *    (device tests, number-register comparisons, string comparisons)
 *    is out of scope for the same reason .TS/.EQ are: it is a real
 *    expression language. Unlike .de, NOT tracking a conditional's
 *    `\{ ... \}` block span does not corrupt output the way not
 *    tracking .de would -- at worst, text meant to be conditional is
 *    always shown (every line inside such a block still individually
 *    parses as either a known macro, an unknown-and-skipped macro, or
 *    ordinary text), which is a reasonable, honestly-documented
 *    over-inclusion rather than the corruption .de would cause. That
 *    asymmetry is why one gets real span-tracking and the other does
 *    not.
 *  - .ds/.nr/.rn/.am and friends (string/number register definitions,
 *    macro renaming): unsupported: no register table is kept here.
 *    Any escape that INTERPOLATES a register (\*(xx, \n(xx, \k, \s...)
 *    is recognised and consumed (so its raw syntax never leaks into
 *    output) but always resolves to nothing/roman -- see decode_text()
 *    below.
 *  - gzip-compressed pages (the overwhelmingly common real-world
 *    on-disk form -- every page under /usr/share/man on a real Linux
 *    system is normally *.gz): NOT decompressed. That needs a real
 *    DEFLATE implementation, which is its own project, not a troff
 *    detail. If the only candidate found for a name is a .gz file,
 *    this says so explicitly (see find_page() below) rather than
 *    silently reporting "no manual entry".
 *  - Hyphenation and full justification (spreading inter-word spaces
 *    to make both margins flush, which real troff's fill+adjust mode
 *    does by default): not implemented. Output here is ordinary
 *    ragged-right greedy word-wrap. Every real `man` this project
 *    could be compared against still reads fine ragged-right; flush-
 *    both-margins text is a cosmetic nicety, not a comprehension one.
 *
 * ---- UNKNOWN-MACRO DEGRADATION ------------------------------------------
 *
 * Any `.xx` request line this file does not implement (including every
 * one this banner just listed as out of scope) is silently skipped --
 * one line consumed, nothing emitted, parsing continues with the next
 * line. This matches real troff's own behaviour for a macro with no
 * defined body: the request is a no-op, not an error, and it does NOT
 * consume any following text lines as part of itself (troff requests
 * only ever "eat" following lines when the request specifically says
 * so, which is exactly why .de/.ig above are the one deliberate
 * exception). Passing the raw troff request line through as text was
 * rejected on purpose: a stray ".ie \n(.g \{\" line printed verbatim
 * into the middle of a paragraph is *more* confusing than a silently
 * absent no-op, matching this project's own dd.c/sed.c/awk.c precedent
 * of "diagnose or degrade cleanly, never corrupt."
 *
 * ============================================================
 * ESCAPE SEQUENCES IMPLEMENTED
 * ============================================================
 *
 * See decode_text() for the exhaustive switch. Summary: \- \_ \& \e \c
 * \% \(space) \0 \| \^ \' \` \. \\ (literal-character/spacing
 * escapes), \" (comment to end of line, recognised both here and as a
 * whole-line `.\"` request), \fX \f(XX \f[...] (font change: B/I are
 * real, everything else -- R, P, numbered fonts, named fonts like CW
 * -- maps to "roman/reset", a documented simplification: this file
 * keeps no font *stack*, so \fP ("previous font") cannot distinguish
 * "the font two changes ago" from plain roman; every real page this
 * was tested against only ever nests one level deep in practice, where
 * that distinction never arises), \(xx (a built-in table of the
 * commonest named glyphs -- copyright, bullet, em/en dash, the curly-
 * quote family; unrecognised two-character names are dropped, not
 * guessed at), and \*(xx/\*x/\*[...] \n... \s... \k... \h... \v...
 * \w... \x... \X... \H... \V... (register interpolation and
 * motion/size requests: recognised and consumed so their syntax can
 * never leak into rendered text, but never resolve to real output --
 * see the .ds/.nr bullet above for why). Any other \X falls back to
 * printing X literally, troff's own "protect this character" meaning
 * for an escape this file does not otherwise know.
 *
 * ============================================================
 * RENDERING: WHERE BOLD/ITALIC COME FROM, AND HOW WIDTH IS CHOSEN
 * ============================================================
 *
 * Font state is carried through word-wrapping as three zero-width
 * marker bytes (MAN_M_BOLD/MAN_M_ITAL/MAN_M_ROMAN -- C0 control codes
 * no real troff source uses, and defensively stripped from raw input
 * on the way in so a hostile page could never forge them, see
 * decode_text()'s very first check) embedded directly in the styled
 * text stream. Wrapping measures width by codepoint (skipping UTF-8
 * continuation bytes and the marker bytes themselves), so the small
 * built-in \(xx table's multi-byte UTF-8 output still counts as one
 * column each, not one column per byte.
 *
 * The actual bytes those markers become depend on where the formatted
 * text is going, chosen once in man_display() below:
 *
 *   - Direct to a real terminal, or through this file's own built-in
 *     "--More--" pager (both cases: this file is fully in control of
 *     what interprets the bytes) -- real ANSI SGR (`\033[1m` bold,
 *     `\033[4m` italic/underline, `\033[0m` reset). This project's own
 *     src/util/tabs.c already writes a raw ANSI CSI sequence directly
 *     to a terminal (`\033[3g`) with no capability-database lookup, so
 *     "assume ANSI SGR is the honest common case" already has
 *     precedent here, not invented for this file.
 *   - Handed to an external $PAGER, or written to a non-terminal
 *     stdout (redirected to a file, piped): classic nroff overstrike
 *     (`X\bX` for bold, `_\bX` for italic/underline). This is the
 *     traditional -Tascii man-page convention, and deliberately the
 *     safer choice here specifically *because* it is external: this
 *     file cannot know whether an arbitrary $PAGER honours ANSI SGR
 *     (`less` needs an explicit -R the user may not have set), but
 *     both `less` and `more` have understood backspace-overstrike as
 *     bold/underline by default since long before either supported
 *     color, and a plain non-terminal destination (or a `| cat`) is
 *     exactly the case a real nroff pipeline expects a reader to run
 *     through `col -b` if they want the overstrike sequences gone.
 *
 * Terminal width: ioctl(TIOCGWINSZ) first (real on this project's NT
 * backend via kernel32's GetConsoleScreenBufferInfo -- src/ioctl/
 * ioctl.c's own banner; on this project's native-Linux backend
 * TIOCGWINSZ is not yet wired to a real syscall, so this always falls
 * through there, which is a real, already-documented gap in
 * src/ioctl/ioctl.c, not something invented by this file), then
 * $COLUMNS if it parses as a positive integer, then a fixed 80 --
 * exactly src/util/ls.c's own term_width() fallback chain, with the
 * ioctl attempt added first because man's whole job is laying text out
 * for the terminal, unlike ls's optional multi-column mode. Height
 * (for the built-in pager's page size) follows the same chain against
 * ws_row/$LINES/24.
 *
 * ============================================================
 * PAGING
 * ============================================================
 *
 * If stdout is not a terminal: no paging at all, the formatted text
 * goes straight to stdout -- matching how `less`/`more` themselves
 * degrade when piped.
 * If stdout IS a terminal: $PAGER (split on whitespace only -- no
 * shell-quoting support, a deliberate, small, documented limit) is
 * spawned via this project's own __find_program()/__spawn() with the
 * formatted output written to a real mkstemp() file passed as its
 * last argument (a pipe was deliberately rejected: it would need
 * either a second process/thread to drain it concurrently with the
 * pager reading, or risk deadlock if the pager doesn't read until it
 * has seen EOF -- a temp file sidesteps that entirely, the same
 * tradeoff a number of historical `man` implementations made). If
 * $PAGER is unset, a minimal built-in "--More--" pager runs instead --
 * necessarily line-buffered (an Enter-terminated prompt, not a real
 * single-keystroke read) rather than putting the terminal in raw mode
 * the way `more`/`less` do, because this project's own termios(3)
 * (src/termios/termios.c's own banner) only has real ICANON/raw-mode
 * control on its NT/kernel32 backend -- there is no native-Linux path
 * to it yet. This is a real, already-documented platform constraint
 * being honoured, not a shortcut invented for this file.
 *
 * ============================================================
 * FINDING A PAGE
 * ============================================================
 *
 * `$MANPATH`, colon-separated, each entry a directory containing
 * man1/, man2/, ... subdirectories -- the same layout every real Unix
 * `man` uses. If MANPATH is unset or empty, the fallback list is
 * "/usr/share/man:/usr/local/share/man": the conventional real-Linux
 * locations (harmless on this project's NT target, where they simply
 * never exist, giving the same clean "no manual entry" any unset-
 * MANPATH `man` produces on a fresh machine). `$MANSECT` (also colon-
 * separated) overrides the default section search order
 * "1:2:3:4:5:6:7:8:9" when no explicit section operand is given.
 * POSIX's own man(1p) SYNOPSIS is only `man [-k] name...` -- no
 * section operand at all -- but every real `man` in practice also
 * accepts a leading bare section (`man 3 printf`), implemented here as:
 * if two or more operands are given and the first looks like a section (matches
 * ^[0-9][A-Za-z0-9]*$), it restricts every name operand that follows,
 * exactly like every real `man`.
 *
 * -k (POSIX: "[i]nterprets name operands as keywords for searching a
 * utilities summary database... match[ing] the pattern of a grep
 * search"): implemented as a real, honest degrade -- there is no
 * prebuilt whatis database here (building/maintaining one is its own
 * subsystem, `makewhatis`, out of scope), so -k does the equivalent
 * scan directly: walk MANPATH, read every page's `.SH NAME` line, and
 * substring-match (case-insensitively) each keyword against it. This
 * is exactly what real `man -k` itself falls back to ("using slow
 * method") when no cached database exists, so it is not a fabricated
 * shortcut, just the one code path this file bothers to implement.
 *
 * ============================================================
 * "CLOSING THE LOOP": THIS REPOSITORY'S OWN man/man1/ PAGES
 * ============================================================
 *
 * This project ships a handful of real, hand-written man pages for its
 * own utilities under man/man1/ (true.1, false.1, cat.1, echo.1,
 * mkdir.1, man.1) -- not installed anywhere by the build (there is no
 * mandir/install-man wiring in this pass; see this file's own header
 * for why that's out of scope this round: this project's Makefile has
 * no existing $(prefix)-relative default-search-path precedent to
 * extend, unlike $(bindir)/$(libdir)/$(includedir), and inventing one
 * without a real installed-tree convention to match would be guessing,
 * not implementing). They exist to prove the macro subset above
 * against real, useful content (point $MANPATH at man/ in a checkout
 * and `man true` works today) and, together with test/util-man.c's
 * embedded copy of real-world GNU grep.1 content, to prove this
 * formatter against troff nobody wrote by hand for this project.
 *
 * Never calls exit()/_exit(): __util_man_main() can run in-process as
 * a shell built-in (src/sh/builtin.c) exactly like every other utility
 * in src/util/ -- see src/util/dd.c's own header comment for why that
 * rule matters and what it would break.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include "util.h"
#include "libc.h" /* __find_program()/__spawn() -- src/process/, the same primitives sh's own execute.c uses */

/* A whole page this large would be pathological (real man pages are a
 * few KB to a few hundred KB) -- this is a safety net against reading
 * an unbounded amount of an arbitrary file into memory, the same
 * "bound it, document why, move on" discipline src/util/m4.c's own
 * M4_MAX_EXPANSIONS comment describes, not a real limit any honest
 * page would hit. */
#define MAN_MAX_PAGE_SIZE (16 * 1024 * 1024)

/* Nesting nowhere near this deep occurs in real pages; bounding
 * .RS/.RE stack depth turns a pathological/malformed page's unbounded
 * `.RS` run into a loud "too deeply nested" skip instead of unbounded
 * growth. */
#define MAN_MAX_RS_DEPTH 64

#define MAN_BASE_INDENT 7  /* classic troff `an.tmac` .nr IN default */
#define MAN_SS_COL      3

/* ==== zero-width font-state markers embedded in styled text ============ */
#define MAN_M_BOLD  '\x01'
#define MAN_M_ITAL  '\x02'
#define MAN_M_ROMAN '\x03'

/* ==== growable byte buffer (src/util/m4.c's strbuf_append() idiom) ===== */

struct man_buf { char *data; size_t len, cap; };

static int mbuf_append(struct man_buf *restrict b,
	const char *restrict data, size_t n)
{
	if (!n) {
		if (!b->data) {
			b->data = malloc(1);
			if (!b->data) return 0;
			b->data[0] = 0;
			b->cap = 1;
		}
		return 1;
	}
	if (b->len + n + 1 > b->cap) {
		size_t newcap;
		char *g;
		if (!__util_array_capacity(b->cap, b->len, n + 1, 256, 1, &newcap)) return 0;
		g = __util_reallocarray(b->data, newcap, 1);
		if (!g) return 0;
		b->data = g; b->cap = newcap;
	}
	for (size_t i = 0; i < n; i++) b->data[b->len + i] = data[i];
	b->len += n;
	b->data[b->len] = 0;
	return 1;
}

static int mbuf_appendc(struct man_buf *b, char c) { return mbuf_append(b, &c, 1); }
static int mbuf_appendstr(struct man_buf *b, const char *s) { return mbuf_append(b, s, strlen(s)); }
static int mbuf_appendn(struct man_buf *b, int n, char c)
{
	while (n-- > 0) if (!mbuf_appendc(b, c)) return 0;
	return 1;
}
static void mbuf_free(struct man_buf *b) { free(b->data); b->data = 0; b->len = b->cap = 0; }
static void mbuf_reset(struct man_buf *b) { b->len = 0; if (b->data) b->data[0] = 0; }

/* ==== \(xx named-glyph table: the commonest subset, ASCII source only == */

static const struct { char name[2]; const char *rep; } man_specials[] = {
	{ { 'c', 'o' }, "\xC2\xA9" },         /* copyright sign */
	{ { 'r', 'g' }, "\xC2\xAE" },         /* registered sign */
	{ { 't', 'm' }, "\xE2\x84\xA2" },     /* trade mark sign */
	{ { 'b', 'u' }, "\xE2\x80\xA2" },     /* bullet */
	{ { 'e', 'm' }, "\xE2\x80\x94" },     /* em dash */
	{ { 'e', 'n' }, "\xE2\x80\x93" },     /* en dash */
	{ { 'd', 'g' }, "\xE2\x80\xA0" },     /* dagger */
	{ { 'l', 'q' }, "\xE2\x80\x9C" },     /* left curly double quote */
	{ { 'r', 'q' }, "\xE2\x80\x9D" },     /* right curly double quote */
	{ { 'o', 'q' }, "\xE2\x80\x98" },     /* left curly single quote */
	{ { 'c', 'q' }, "\xE2\x80\x99" },     /* right curly single quote */
	{ { 'a', 'q' }, "'" },                /* apostrophe */
	{ { 'd', 'q' }, "\"" },               /* double quote */
	{ { 's', 'q' }, "'" },                /* apostrophe (alt name) */
	{ { 'g', 'a' }, "`" },                /* grave accent */
	{ { 'a', 'a' }, "'" },                /* acute accent */
	{ { 'h', 'a' }, "^" },                /* circumflex */
	{ { 't', 'i' }, "~" },                /* tilde */
};

static const char *man_lookup_special(char c1, char c2)
{
	size_t i;
	for (i = 0; i < sizeof man_specials / sizeof *man_specials; i++)
		if (man_specials[i].name[0] == c1 && man_specials[i].name[1] == c2)
			return man_specials[i].rep;
	return 0;
}

/* Consume a `'...'`- or `[...]`-delimited argument (the shape \h, \v,
 * \w, \x, \X, \H, \V all take) and produce no output -- see this
 * file's own header comment ("motion/size requests") for why these
 * are recognised-and-dropped rather than implemented. Returns the new
 * index. */
static size_t man_skip_delim_arg(const char *s, size_t n, size_t i)
{
	if (i < n && s[i] == '\'') {
		i++;
		while (i < n && s[i] != '\'') i++;
		if (i < n) i++;
	} else if (i < n && s[i] == '[') {
		i++;
		while (i < n && s[i] != ']') i++;
		if (i < n) i++;
	}
	return i;
}

/* Escape/glyph decoder: appends the rendering of one chunk of raw
 * troff text (a whole text line, or one macro argument) to `out`,
 * expanding the escapes this file's own header comment documents.
 * See that comment for the exact, exhaustive list. */
static int decode_text(struct man_buf *out, const char *s, size_t n)
{
	size_t i = 0;

	while (i < n) {
		unsigned char c = (unsigned char)s[i];

		/* Defensive: strip any of the three internal marker bytes a
		 * malformed/hostile page might contain literally, so the
		 * marker channel word-wrap and rendering rely on can never
		 * be forged by input -- see this file's own header comment. */
		if (c == (unsigned char)MAN_M_BOLD || c == (unsigned char)MAN_M_ITAL ||
		    c == (unsigned char)MAN_M_ROMAN) { i++; continue; }

		if (c != '\\') { if (!mbuf_appendc(out, (char)c)) return 0; i++; continue; }
		i++;
		if (i >= n) { if (!mbuf_appendc(out, '\\')) return 0; break; }
		c = (unsigned char)s[i];

		switch (c) {
		case '-': case '_': case '\'': case '`': case '.': case '\\':
			if (!mbuf_appendc(out, (char)c)) return 0;
			i++;
			break;
		case ' ': case '0':
			if (!mbuf_appendc(out, ' ')) return 0;
			i++;
			break;
		case 'e':
			if (!mbuf_appendc(out, '\\')) return 0;
			i++;
			break;
		case '&': case 'c': case '%': case '|': case '^':
			i++; /* zero-width / spacing / no-break hints: dropped */
			break;
		case '"':
			i = n; /* comment to end of line */
			break;
		case '(': {
			char c1 = 0, c2 = 0;
			const char *rep;
			i++;
			if (i < n) { c1 = s[i]; i++; }
			if (i < n) { c2 = s[i]; i++; }
			rep = man_lookup_special(c1, c2);
			if (rep && !mbuf_appendstr(out, rep)) return 0;
			break;
		}
		case '*': /* string register interpolation: unsupported */
			i++;
			if (i < n && s[i] == '(') { i++; if (i < n) i++; if (i < n) i++; }
			else if (i < n && s[i] == '[') { i++; while (i < n && s[i] != ']') i++; if (i < n) i++; }
			else if (i < n) { i++; }
			break;
		case 'n': /* number register interpolation: unsupported */
			i++;
			if (i < n && (s[i] == '+' || s[i] == '-')) i++;
			if (i < n && s[i] == '(') { i++; if (i < n) i++; if (i < n) i++; }
			else if (i < n && s[i] == '[') { i++; while (i < n && s[i] != ']') i++; if (i < n) i++; }
			else if (i < n) { i++; }
			break;
		case 'k': /* mark register: unsupported */
			i++;
			if (i < n && s[i] == '(') { i++; if (i < n) i++; if (i < n) i++; }
			else if (i < n) { i++; }
			break;
		case 's': /* point-size change: unsupported */
			i++;
			if (i < n && (s[i] == '+' || s[i] == '-')) i++;
			if (i < n && (s[i] == '\'' || s[i] == '[')) {
				i = man_skip_delim_arg(s, n, i);
			} else {
				while (i < n && s[i] >= '0' && s[i] <= '9') i++;
			}
			break;
		case 'h': case 'v': case 'w': case 'x': case 'X': case 'H': case 'V':
			i++;
			i = man_skip_delim_arg(s, n, i);
			break;
		case 'f': {
			i++;
			if (i < n && s[i] == '(') {
				i++;
				if (i < n) i++;
				if (i < n) i++;
				if (!mbuf_appendc(out, MAN_M_ROMAN)) return 0;
			} else if (i < n && s[i] == '[') {
				i++;
				while (i < n && s[i] != ']') i++;
				if (i < n) i++;
				if (!mbuf_appendc(out, MAN_M_ROMAN)) return 0;
			} else if (i < n) {
				char fc = s[i];
				i++;
				if (fc == 'B') { if (!mbuf_appendc(out, MAN_M_BOLD)) return 0; }
				else if (fc == 'I') { if (!mbuf_appendc(out, MAN_M_ITAL)) return 0; }
				else { if (!mbuf_appendc(out, MAN_M_ROMAN)) return 0; }
			}
			break;
		}
		default:
			/* Unknown escape: troff's own fallback is "the escaped
			 * character, literally" (this is how `\.` protects a
			 * dot from control-character interpretation, etc). */
			if (!mbuf_appendc(out, (char)c)) return 0;
			i++;
			break;
		}
	}
	return 1;
}

/* ==== macro-argument tokenizer: whitespace-separated, "quoted strings" == */

struct man_argv { char **v; size_t n, cap; };

static void man_argv_free(struct man_argv *a)
{
	size_t i;
	for (i = 0; i < a->n; i++) free(a->v[i]);
	free(a->v);
	a->v = 0; a->n = a->cap = 0;
}

static int man_argv_push(struct man_argv *a, const char *tok, size_t len)
{
	char *dup;
	if (a->n + 1 > a->cap) {
		size_t newcap;
		char **g;
		if (!__util_array_capacity(a->cap, a->n, 1, 8, sizeof *a->v, &newcap)) return 0;
		g = __util_reallocarray(a->v, newcap, sizeof *a->v);
		if (!g) return 0;
		a->v = g; a->cap = newcap;
	}
	dup = malloc(len + 1);
	if (!dup) return 0;
	for (size_t i = 0; i < len; i++) dup[i] = tok[i];
	dup[len] = 0;
	a->v[a->n++] = dup;
	return 1;
}

/* Splits a request line's argument text into raw (not escape-decoded --
 * callers decode each token themselves once they know what font, if
 * any, applies) tokens. A token beginning with `"` runs to the next
 * `"` or end of line -- doubled `""`-inside-a-quote escaping is not
 * implemented, a small, honest simplification. */
static int man_tokenize(const char *s, struct man_argv *out)
{
	size_t i = 0, n = strlen(s);
	memset(out, 0, sizeof *out);
	while (i < n) {
		size_t start;
		while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
		if (i >= n) break;
		if (s[i] == '"') {
			i++;
			start = i;
			while (i < n && s[i] != '"') i++;
			if (!man_argv_push(out, s + start, i - start)) { man_argv_free(out); return 0; }
			if (i < n) i++;
		} else {
			start = i;
			/* A `\` always protects whatever byte follows it from
			 * ending the token here -- most importantly `\ `
			 * (escaped space), real troff's own way to embed a
			 * literal space inside an unquoted macro argument
			 * without splitting it in two (this project's own
			 * man/man1/mkdir.1 fixture uses exactly this, in
			 * `.BI \-m\ mode`). Escape decoding itself still happens
			 * later in decode_text(); this only keeps tokenizing from
			 * cutting an escape sequence in half. */
			while (i < n && s[i] != ' ' && s[i] != '\t') {
				if (s[i] == '\\' && i + 1 < n) i += 2;
				else i++;
			}
			if (!man_argv_push(out, s + start, i - start)) { man_argv_free(out); return 0; }
		}
	}
	return 1;
}

/* ==== visible-column counting (UTF-8-codepoint-aware, marker-aware) ==== */

static size_t man_vislen(const char *s, size_t n)
{
	size_t i, cols = 0;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c == (unsigned char)MAN_M_BOLD || c == (unsigned char)MAN_M_ITAL ||
		    c == (unsigned char)MAN_M_ROMAN) continue;
		if ((c & 0xC0) == 0x80) continue; /* UTF-8 continuation byte */
		cols++;
	}
	return cols;
}

/* ==== rendering context ================================================ */

struct man_ctx {
	struct man_buf doc;     /* the final formatted document, styled */
	struct man_buf acc;     /* paragraph/tag accumulator, styled */
	int width;
	int fill;               /* 1 = fill (wrap) mode, 0 = .nf no-fill mode */
	int nf_started;         /* has this .nf block emitted its first line yet */
	int rs_indent;          /* current body indent, from base + RS/RE stack */
	int rs_stack[MAN_MAX_RS_DEPTH];
	int rs_depth;
	int extra_indent;       /* active .TP/.IP tag-body indent bump */
	int pending_tag;        /* next content chunk becomes a .TP tag */
	int tag_width;
	char *pending_prefix;   /* deferred short-tag first-line prefix, styled */
	int just_emitted_tag;   /* suppress the next flush's leading blank line
	                          * -- set after a .TP/.IP tag (its body is a
	                          * continuation, not a new block), after .br
	                          * (same reasoning), and after a .SH/.SS
	                          * heading (its own first paragraph sits
	                          * flush against it, no blank -- see each
	                          * case's own comment) */
	int had_output;         /* has anything at all been written to doc yet */
	int in_macro_def;       /* inside a .de/.ig ... .. span: skip everything */
};

static int man_ctx_init(struct man_ctx *c, int width)
{
	memset(c, 0, sizeof *c);
	c->width = width;
	c->fill = 1;
	c->rs_indent = MAN_BASE_INDENT;
	return 1;
}

static void man_ctx_free(struct man_ctx *c)
{
	mbuf_free(&c->doc);
	mbuf_free(&c->acc);
	free(c->pending_prefix);
}

/* Blank-line-before-a-new-block bookkeeping: exactly one blank line
 * between consecutive blocks (headings, paragraphs, .TP items), none
 * before the very first block, and none between a .TP/.IP tag and its
 * own immediately-following body -- see this file's header comment's
 * "just_emitted_tag" discussion for why that last case matters. */
static int man_block_start(struct man_ctx *c)
{
	if (c->had_output && !c->just_emitted_tag) {
		if (!mbuf_appendc(&c->doc, '\n')) return 0;
	}
	c->just_emitted_tag = 0;
	c->had_output = 1;
	return 1;
}

/* Greedy word-wrap of `styled` (marker-embedded) into c->doc at the
 * given indent. If first_prefix is non-NULL it is used verbatim
 * (already padded to `indent` visible columns) as the first output
 * line's left margin instead of `indent` plain spaces -- the ".TP tag
 * short enough to share the body's first line" case. */
static int man_wrap_emit(struct man_ctx *c, const char *styled, int indent, const char *first_prefix)
{
	size_t n = strlen(styled);
	size_t i = 0;
	int cols = c->width - indent;
	int line_cols = 0;
	int any_word = 0;

	if (cols < 20) cols = 20;

	if (first_prefix) {
		if (!mbuf_appendstr(&c->doc, first_prefix)) return 0;
	} else {
		if (!mbuf_appendn(&c->doc, indent, ' ')) return 0;
	}

	while (i < n) {
		size_t wstart;
		size_t wlen;

		while (i < n && styled[i] == ' ') i++;
		if (i >= n) break;
		wstart = i;
		while (i < n && styled[i] != ' ') i++;
		wlen = i - wstart;

		{
			size_t wcols = man_vislen(styled + wstart, wlen);
			int sep = any_word ? 1 : 0;

			if (any_word && (size_t)(line_cols + sep) + wcols > (size_t)cols) {
				if (!mbuf_appendc(&c->doc, '\n')) return 0;
				if (!mbuf_appendn(&c->doc, indent, ' ')) return 0;
				line_cols = 0;
				sep = 0;
			}
			if (sep) { if (!mbuf_appendc(&c->doc, ' ')) return 0; line_cols++; }
			if (!mbuf_append(&c->doc, styled + wstart, wlen)) return 0;
			line_cols += (int)wcols;
			any_word = 1;
		}
	}
	if (!any_word && first_prefix) {
		/* Nothing followed a deferred tag before the next flush: trim
		 * the trailing pad spaces first_prefix carried so the tag
		 * doesn't leave a ragged trailing-space-only line. */
		while (c->doc.len > 0 && c->doc.data[c->doc.len - 1] == ' ') c->doc.len--;
		c->doc.data[c->doc.len] = 0;
	}
	if (!mbuf_appendc(&c->doc, '\n')) return 0;
	return 1;
}

/* Ends the current paragraph/tag-body accumulator, if any, or an
 * outstanding deferred short tag with no body at all. Always safe to
 * call with nothing pending (no-op). */
static int man_flush_paragraph(struct man_ctx *c)
{
	if (c->acc.len == 0 && !c->pending_prefix) return 1;

	if (!c->just_emitted_tag) { if (!man_block_start(c)) return 0; }
	else { c->just_emitted_tag = 0; c->had_output = 1; }

	if (c->acc.len == 0) {
		/* Deferred tag, never followed by a body: emit it alone. */
		char *p = c->pending_prefix;
		size_t l = strlen(p);
		while (l > 0 && p[l - 1] == ' ') l--;
		if (!mbuf_append(&c->doc, p, l)) return 0;
		if (!mbuf_appendc(&c->doc, '\n')) return 0;
	} else {
		int indent = c->rs_indent + c->extra_indent;
		if (!man_wrap_emit(c, c->acc.data, indent, c->pending_prefix)) return 0;
	}
	free(c->pending_prefix);
	c->pending_prefix = 0;
	mbuf_reset(&c->acc);
	return 1;
}

/* Turns the current accumulator into a .TP/.IP tag: either deferred
 * (short enough to share the body's first output line -- resolved by
 * man_flush_paragraph() once the body text is known) or emitted
 * immediately on its own line (too long to share). */
static int man_flush_as_tag(struct man_ctx *c, int width)
{
	size_t tag_vis;
	if (!man_block_start(c)) return 0;

	tag_vis = man_vislen(c->acc.data, c->acc.len);
	if ((int)tag_vis + 1 <= width) {
		struct man_buf p;
		memset(&p, 0, sizeof p);
		if (!mbuf_appendn(&p, c->rs_indent, ' ')) { mbuf_free(&p); return 0; }
		if (!mbuf_append(&p, c->acc.data, c->acc.len)) { mbuf_free(&p); return 0; }
		if (!mbuf_appendn(&p, width - (int)tag_vis, ' ')) { mbuf_free(&p); return 0; }
		free(c->pending_prefix);
		c->pending_prefix = p.data; /* transfer ownership */
	} else {
		if (!mbuf_appendn(&c->doc, c->rs_indent, ' ')) return 0;
		if (!mbuf_append(&c->doc, c->acc.data, c->acc.len)) return 0;
		if (!mbuf_appendc(&c->doc, '\n')) return 0;
	}
	mbuf_reset(&c->acc);
	c->just_emitted_tag = 1;
	return 1;
}

/* Appends `text` (raw, not yet escape-decoded) to the accumulator in
 * font `font` (0 = current/roman, MAN_M_BOLD, MAN_M_ITAL), inserting a
 * single space first if the accumulator is non-empty -- the .B/.I
 * (single-font, space-joined-with-args) shape. */
static int man_acc_add_font(struct man_ctx *c, const char *text, int font)
{
	if (c->acc.len > 0) { if (!mbuf_appendc(&c->acc, ' ')) return 0; }
	if (font) { if (!mbuf_appendc(&c->acc, (char)font)) return 0; }
	if (!decode_text(&c->acc, text, strlen(text))) return 0;
	if (font) { if (!mbuf_appendc(&c->acc, MAN_M_ROMAN)) return 0; }
	return 1;
}

/* If a .TP tag is pending, whatever content the caller just appended
 * to the (until-now-empty) accumulator becomes that tag. Call after
 * any content-appending line (text, .B/.I/.BI/.BR/.IR/.IB/.RB/.RI). */
static int man_maybe_consume_tag(struct man_ctx *c)
{
	if (!c->pending_tag) return 1;
	c->pending_tag = 0;
	if (c->acc.len == 0) return 1;
	return man_flush_as_tag(c, c->tag_width);
}

/* ==== macro dispatch ==================================================== */

struct man_render {
	struct man_buf title, section, date, source, manual;
};

static void man_th(struct man_ctx *c, struct man_argv *a,
                    struct man_buf *title, struct man_buf *section,
                    struct man_buf *date, struct man_buf *source, struct man_buf *manual)
{
	size_t i;
	struct man_buf *slots[5];
	slots[0] = title; slots[1] = section; slots[2] = date; slots[3] = source; slots[4] = manual;

	c->rs_depth = 0;
	c->rs_indent = MAN_BASE_INDENT;
	c->extra_indent = 0;
	c->pending_tag = 0;
	c->fill = 1;

	for (i = 0; i < 5; i++) mbuf_reset(slots[i]);
	for (i = 0; i < a->n && i < 5; i++)
		if (!decode_text(slots[i], a->v[i], strlen(a->v[i]))) return;
}

static int man_center3(struct man_buf *doc, int width, const char *l, const char *ctr, const char *r)
{
	int ll = (int)strlen(l), cl = (int)strlen(ctr), rl = (int)strlen(r);
	int pad1, pad2;

	pad1 = (width - ll - cl - rl) / 2;
	if (pad1 < 1) pad1 = 1;
	pad2 = width - ll - cl - rl - pad1;
	if (pad2 < 1) pad2 = 1;

	if (!mbuf_appendstr(doc, l)) return 0;
	if (!mbuf_appendn(doc, pad1, ' ')) return 0;
	if (!mbuf_appendstr(doc, ctr)) return 0;
	if (!mbuf_appendn(doc, pad2, ' ')) return 0;
	if (!mbuf_appendstr(doc, r)) return 0;
	if (!mbuf_appendc(doc, '\n')) return 0;
	return 1;
}

/* Splits one physical input line into a (possibly empty) macro name
 * and the raw remainder, for lines whose first byte is '.'. */
static void man_split_request(const char *line, char *name, size_t namesz, const char **rest)
{
	size_t i = 1, k = 0;
	while (line[i] && line[i] != ' ' && line[i] != '\t' && k + 1 < namesz)
		name[k++] = line[i++];
	name[k] = 0;
	while (line[i] == ' ' || line[i] == '\t') i++;
	*rest = line + i;
}

/* Truncates `line` (in place) at the first unescaped `\"` sequence
 * (troff's "comment to end of line", valid anywhere, not just at the
 * start of a `.\"` request line). */
static void man_strip_comment(char *line)
{
	char *p = line;
	while ((p = strchr(p, '\\')) != 0) {
		if (p[1] == '"') { *p = 0; return; }
		if (p[1] == 0) return;
		p += 2;
	}
}

static int man_process_line(struct man_ctx *c, struct man_render *r, char *line)
{
	man_strip_comment(line);

	if (c->in_macro_def) {
		const char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (p[0] == '.' && p[1] == '.' &&
		    (p[2] == 0 || p[2] == ' ' || p[2] == '\t')) c->in_macro_def = 0;
		return 1;
	}

	if (line[0] != '.') {
		/* Plain text line. */
		if (!c->fill) {
			struct man_buf tmp;
			int ok = 1;
			memset(&tmp, 0, sizeof tmp);
			if (!decode_text(&tmp, line, strlen(line))) { mbuf_free(&tmp); return 0; }
			if (tmp.len == 0) {
				ok = mbuf_appendc(&c->doc, '\n');
			} else {
				if (!c->nf_started) {
					ok = man_block_start(c);
					c->nf_started = 1;
				}
				if (ok) ok = mbuf_appendn(&c->doc, c->rs_indent + c->extra_indent, ' ');
				if (ok) ok = mbuf_append(&c->doc, tmp.data, tmp.len);
				if (ok) ok = mbuf_appendc(&c->doc, '\n');
			}
			mbuf_free(&tmp);
			return ok;
		}
		{
			size_t k = 0;
			while (line[k] == ' ' || line[k] == '\t') k++;
			if (line[k] == 0) return man_flush_paragraph(c);
		}
		if (!man_acc_add_font(c, line, 0)) return 0;
		return man_maybe_consume_tag(c);
	}

	{
		/* Zero-initialized defensively: man_split_request() always
		 * NUL-terminates within bounds, so every byte the code below
		 * actually reads is real, but a static analyzer cannot always
		 * correlate "name[1] is read only once !strcmp(name,\"BI\")
		 * (etc, all exactly 2 bytes) has already succeeded" with
		 * name's true length -- zero-initializing removes the
		 * ambiguity for free rather than arguing with the tool. */
		char name[16] = { 0 };
		const char *rest;
		struct man_argv a;
		int ok = 1;

		man_split_request(line, name, sizeof name, &rest);

		if (!strcmp(name, "de") || !strcmp(name, "de1") || !strcmp(name, "ig")) {
			c->in_macro_def = 1;
			return 1;
		}

		if (!man_tokenize(rest, &a)) return 0;

		if (!strcmp(name, "TH")) {
			man_th(c, &a, &r->title, &r->section, &r->date, &r->source, &r->manual);
		} else if (!strcmp(name, "SH") || !strcmp(name, "SS")) {
			struct man_buf heading;
			size_t i;
			memset(&heading, 0, sizeof heading);
			if (!man_flush_paragraph(c)) { man_argv_free(&a); return 0; }
			for (i = 0; i < a.n && ok; i++) {
				if (i && !mbuf_appendc(&heading, ' ')) ok = 0;
				if (ok && !decode_text(&heading, a.v[i], strlen(a.v[i]))) ok = 0;
			}
			if (ok) {
				int is_sh = !strcmp(name, "SH");
				ok = man_block_start(c);
				if (ok) ok = mbuf_appendn(&c->doc, is_sh ? 0 : MAN_SS_COL, ' ');
				if (ok) ok = mbuf_appendc(&c->doc, MAN_M_BOLD);
				if (ok) ok = mbuf_append(&c->doc, heading.data, heading.len);
				if (ok) ok = mbuf_appendc(&c->doc, MAN_M_ROMAN);
				if (ok) ok = mbuf_appendc(&c->doc, '\n');
				/* A heading's own immediately-following paragraph sits
				 * flush against it, no blank line -- real troff's an.
				 * tmac SH/SS macros put the blank line BEFORE a
				 * heading (man_block_start() above), never after;
				 * confirmed against a real system man page's actual
				 * rendered output, not assumed. Reuses the same
				 * suppress-next-leading-blank flag .TP/.IP tags and
				 * .br already rely on. */
				if (ok) c->just_emitted_tag = 1;
			}
			mbuf_free(&heading);
			if (!strcmp(name, "SH")) {
				c->rs_depth = 0;
				c->rs_indent = MAN_BASE_INDENT;
			}
			c->extra_indent = 0;
			c->pending_tag = 0;
		} else if (!strcmp(name, "PP") || !strcmp(name, "LP")) {
			ok = man_flush_paragraph(c);
			c->extra_indent = 0;
			c->pending_tag = 0;
		} else if (!strcmp(name, "TP")) {
			long w = MAN_BASE_INDENT;
			if (a.n > 0) { char *end; long v = strtol(a.v[0], &end, 10); if (*end == 0 && v > 0) w = v; }
			ok = man_flush_paragraph(c);
			c->extra_indent = 0;
			c->pending_tag = 1;
			c->tag_width = (int)w;
		} else if (!strcmp(name, "IP")) {
			long w = MAN_BASE_INDENT;
			size_t tagn = a.n;
			if (tagn > 0) {
				char *end;
				long v = strtol(a.v[tagn - 1], &end, 10);
				if (*end == 0 && v > 0 && tagn > 1) { w = v; tagn--; }
			}
			ok = man_flush_paragraph(c);
			if (ok) {
				c->extra_indent = 0;
				c->pending_tag = 0;
				{
					size_t i;
					for (i = 0; i < tagn && ok; i++) ok = man_acc_add_font(c, a.v[i], 0);
				}
				if (ok) ok = man_flush_as_tag(c, (int)w);
			}
			c->extra_indent = (int)w;
		} else if (!strcmp(name, "RS")) {
			long v = MAN_BASE_INDENT;
			if (a.n > 0) { char *end; long vv = strtol(a.v[0], &end, 10); if (*end == 0 && vv > 0) v = vv; }
			ok = man_flush_paragraph(c);
			c->extra_indent = 0;
			c->pending_tag = 0;
			if (ok && c->rs_depth < MAN_MAX_RS_DEPTH) {
				c->rs_stack[c->rs_depth++] = c->rs_indent;
				c->rs_indent += (int)v;
			}
		} else if (!strcmp(name, "RE")) {
			int level = 0;
			if (a.n > 0) { char *end; long v = strtol(a.v[0], &end, 10); if (*end == 0 && v > 0) level = (int)v; }
			ok = man_flush_paragraph(c);
			c->extra_indent = 0;
			c->pending_tag = 0;
			if (ok) {
				if (level > 0) {
					while (c->rs_depth >= level && c->rs_depth > 0) c->rs_indent = c->rs_stack[--c->rs_depth];
				} else if (c->rs_depth > 0) {
					c->rs_indent = c->rs_stack[--c->rs_depth];
				}
			}
		} else if (!strcmp(name, "nf")) {
			ok = man_flush_paragraph(c);
			c->fill = 0;
			c->nf_started = 0;
		} else if (!strcmp(name, "fi")) {
			c->fill = 1;
			c->nf_started = 0;
		} else if (!strcmp(name, "br")) {
			/* .br: force a line break WITHOUT starting a new block --
			 * no blank line, no indent/tag-state reset, unlike .PP.
			 * Added beyond the task's originally-named macro set: a
			 * real-world necessity, not scope creep -- both this
			 * project's own man/man1/ fixtures' multi-form
			 * SYNOPSIS sections and the real GNU grep.1 excerpt this
			 * file is tested against use `.br` between alternative
			 * invocation forms, and without it those forms would
			 * wrongly run together into one flowed line. */
			if (c->acc.len > 0) {
				int indent = c->rs_indent + c->extra_indent;
				ok = man_wrap_emit(c, c->acc.data, indent, c->pending_prefix);
				free(c->pending_prefix);
				c->pending_prefix = 0;
				mbuf_reset(&c->acc);
				c->had_output = 1;
				/* Reuses the exact "don't insert a blank line before
				 * the next flush" suppression man_flush_as_tag() uses
				 * for a tag's own body continuation -- semantically
				 * the same situation: whatever comes next is this
				 * line's continuation, not a new block, so the next
				 * man_flush_paragraph() (e.g. the one .SH triggers
				 * when this section ends) must not treat it as one. */
				c->just_emitted_tag = 1;
			}
		} else if (!strcmp(name, "B") || !strcmp(name, "I")) {
			int font = !strcmp(name, "B") ? MAN_M_BOLD : MAN_M_ITAL;
			size_t i;
			if (!c->fill) {
				struct man_buf tmp;
				memset(&tmp, 0, sizeof tmp);
				ok = mbuf_appendc(&tmp, (char)font);
				for (i = 0; i < a.n && ok; i++) {
					if (i && !mbuf_appendc(&tmp, ' ')) { ok = 0; break; }
					ok = decode_text(&tmp, a.v[i], strlen(a.v[i]));
				}
				if (ok) ok = mbuf_appendc(&tmp, MAN_M_ROMAN);
				if (ok && tmp.len > 0) {
					if (!c->nf_started) { ok = man_block_start(c); c->nf_started = 1; }
					if (ok) ok = mbuf_appendn(&c->doc, c->rs_indent + c->extra_indent, ' ');
					if (ok) ok = mbuf_append(&c->doc, tmp.data, tmp.len);
					if (ok) ok = mbuf_appendc(&c->doc, '\n');
				}
				mbuf_free(&tmp);
			} else {
				for (i = 0; i < a.n && ok; i++) ok = man_acc_add_font(c, a.v[i], font);
				if (ok) ok = man_maybe_consume_tag(c);
			}
		} else if (!strcmp(name, "BI") || !strcmp(name, "IB") || !strcmp(name, "BR") ||
		           !strcmp(name, "RB") || !strcmp(name, "IR") || !strcmp(name, "RI")) {
			/* Alternating-font macros: args concatenated with NO
			 * inserted separator, alternating font1/font2/font1/...
			 * -- see this file's own header comment for why all six
			 * standard combinations are handled by this one shared
			 * shape rather than just the three the task named. */
			int f1 = (name[0] == 'B') ? MAN_M_BOLD : (name[0] == 'I') ? MAN_M_ITAL : MAN_M_ROMAN;
			int f2 = (name[1] == 'B') ? MAN_M_BOLD : (name[1] == 'I') ? MAN_M_ITAL : MAN_M_ROMAN;
			size_t i;
			/* Joins to whatever the accumulator already holds (e.g. a
			 * preceding .B/.I line in the same fill-mode paragraph)
			 * with a single space, the same implicit inter-line join
			 * every plain text line gets -- only the args WITHIN one
			 * alternating-macro call are concatenated with no
			 * separator, per real troff's own BI/BR/etc semantics. */
			if (c->acc.len > 0) { if (!mbuf_appendc(&c->acc, ' ')) ok = 0; }
			for (i = 0; i < a.n && ok; i++) {
				int font = (i % 2 == 0) ? f1 : f2;
				if (!mbuf_appendc(&c->acc, (char)font)) { ok = 0; break; }
				if (!decode_text(&c->acc, a.v[i], strlen(a.v[i]))) { ok = 0; break; }
				if (!mbuf_appendc(&c->acc, MAN_M_ROMAN)) { ok = 0; break; }
			}
			if (ok) ok = man_maybe_consume_tag(c);
		}
		/* Any other request name (.ds, .nr, .if, .ie, .el, .TS, .EQ,
		 * .ad, .na, .hy, .sp, .br, .ce, .in, .ll, ...): unimplemented,
		 * silently skipped -- see this file's own "UNKNOWN-MACRO
		 * DEGRADATION" header comment. */
		man_argv_free(&a);
		return ok;
	}
}

/* Builds the .TH header line -- see this file's own header comment
 * ("the well-known three-field header/footer layout") -- into `out`
 * (a fresh buffer, NOT c->doc: c->doc already holds the whole
 * formatted body by the time this runs, and the header line belongs
 * *before* that, so man_format() below prepends this rather than
 * appending it). A page with no .TH (unusual but not fatal) leaves
 * `out` empty. */
static int man_emit_header(struct man_buf *out, int width, struct man_render *r)
{
	struct man_buf left;
	int ok;

	if (r->title.len == 0) return 1;

	memset(&left, 0, sizeof left);
	ok = mbuf_appendstr(&left, r->title.data);
	if (ok && r->section.len) { ok = mbuf_appendc(&left, '('); if (ok) ok = mbuf_appendstr(&left, r->section.data); if (ok) ok = mbuf_appendc(&left, ')'); }
	if (ok) {
		const char *ctr = r->manual.len ? r->manual.data : (r->source.len ? r->source.data : "");
		ok = man_center3(out, width, left.data, ctr, left.data);
	}
	mbuf_free(&left);
	return ok;
}

static int man_emit_footer(struct man_ctx *c, struct man_render *r)
{
	struct man_buf left;
	int ok;

	if (r->title.len == 0) return 1;

	memset(&left, 0, sizeof left);
	ok = mbuf_appendstr(&left, r->title.data);
	if (ok && r->section.len) { ok = mbuf_appendc(&left, '('); if (ok) ok = mbuf_appendstr(&left, r->section.data); if (ok) ok = mbuf_appendc(&left, ')'); }
	if (ok) ok = mbuf_appendc(&c->doc, '\n');
	if (ok) {
		const char *l = r->date.len ? r->date.data : "";
		const char *ctr = r->source.len ? r->source.data : "";
		ok = man_center3(&c->doc, c->width, l, ctr, left.data);
	}
	mbuf_free(&left);
	return ok;
}

/* ==== top-level: format one already-read page's text into c->doc ======= */

static int man_format(const char *text, size_t len, int width, struct man_buf *out)
{
	struct man_ctx c;
	struct man_render r;
	char *copy;
	char *line;
	int ok = 1;

	if (!man_ctx_init(&c, width)) return 0;
	memset(&r, 0, sizeof r);

	copy = malloc(len + 1);
	if (!copy) { man_ctx_free(&c); return 0; }
	for (size_t i = 0; i < len; i++) copy[i] = text[i];
	copy[len] = 0;

	line = copy;
	while (ok && line) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = 0;
		ok = man_process_line(&c, &r, line);
		line = nl ? nl + 1 : 0;
	}
	if (ok) ok = man_flush_paragraph(&c);

	if (ok) {
		struct man_buf full;
		memset(&full, 0, sizeof full);
		ok = man_emit_header(&full, width, &r) &&
		     mbuf_append(&full, c.doc.data ? c.doc.data : "", c.doc.len);
		if (ok) {
			mbuf_free(&c.doc);
			c.doc = full;
			c.had_output = 1;
			full.data = 0;
		} else {
			mbuf_free(&full);
		}
	}
	if (ok) ok = man_emit_footer(&c, &r);

	if (ok) { out->data = c.doc.data; out->len = c.doc.len; out->cap = c.doc.cap; c.doc.data = 0; }

	free(copy);
	mbuf_free(&r.title); mbuf_free(&r.section); mbuf_free(&r.date);
	mbuf_free(&r.source); mbuf_free(&r.manual);
	man_ctx_free(&c);
	return ok;
}

/* ==== terminal geometry ================================================ */

static int man_env_positive(const char *name, int fallback)
{
	const char *v = getenv(name);
	char *end;
	long n;
	if (!v || !*v) return fallback;
	n = strtol(v, &end, 10);
	if (*end || end == v || n <= 0 || n > 100000) return fallback;
	return (int)n;
}

static int man_term_width(void)
{
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
	return man_env_positive("COLUMNS", 80);
}

static int man_term_height(void)
{
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) return ws.ws_row;
	return man_env_positive("LINES", 24);
}

/* ==== rendering: marker bytes -> real terminal bytes ==================== */

enum man_render_mode { MAN_RENDER_ANSI, MAN_RENDER_OVERSTRIKE };

static int man_render_write(FILE *out, const char *styled, size_t len, enum man_render_mode mode)
{
	size_t i;
	int font = 0; /* 0 = roman, MAN_M_BOLD, MAN_M_ITAL */

	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)styled[i];

		if (c == (unsigned char)MAN_M_BOLD || c == (unsigned char)MAN_M_ITAL ||
		    c == (unsigned char)MAN_M_ROMAN) {
			if (mode == MAN_RENDER_ANSI) {
				if (c == (unsigned char)MAN_M_BOLD) { if (fputs("\033[1m", out) < 0) return -1; }
				else if (c == (unsigned char)MAN_M_ITAL) { if (fputs("\033[4m", out) < 0) return -1; }
				else { if (fputs("\033[0m", out) < 0) return -1; }
			}
			font = (c == (unsigned char)MAN_M_ROMAN) ? 0 : (int)c;
			continue;
		}

		if (mode == MAN_RENDER_OVERSTRIKE && font && c != ' ' && c != '\n') {
			int uc = (font == MAN_M_BOLD) ? c : '_';
			if (fputc(uc, out) == EOF) return -1;
			if (fputc('\b', out) == EOF) return -1;
		}
		if (fputc(c, out) == EOF) return -1;
	}
	if (mode == MAN_RENDER_ANSI) { if (fputs("\033[0m", out) < 0) return -1; }
	return 0;
}

/* ==== built-in "--More--" pager ========================================= */

static int man_builtin_pager(const char *styled, size_t len, int height)
{
	const char *p = styled, *end = styled + len;
	int rows = 0;
	int page_rows = height > 2 ? height - 1 : 1;

	while (p < end) {
		const char *nl = memchr(p, '\n', (size_t)(end - p));
		size_t linelen = nl ? (size_t)(nl - p) : (size_t)(end - p);

		if (man_render_write(stdout, p, linelen, MAN_RENDER_ANSI) < 0) return -1;
		if (fputc('\n', stdout) == EOF) return -1;
		rows++;
		p = nl ? nl + 1 : end;

		if (rows >= page_rows && p < end) {
			char resp[64];
			if (fflush(stdout) < 0) return -1;
			fputs("\033[1m--More--\033[0m", stderr);
			fflush(stderr);
			if (!fgets(resp, sizeof resp, stdin)) { fputc('\n', stderr); break; }
			fputc('\r', stderr);
			if (resp[0] == 'q' || resp[0] == 'Q') break;
			rows = 0;
		}
	}
	return 0;
}

/* ==== external $PAGER via a real temp file ============================= */

static const char *man_tmpdir(void)
{
	const char *d = getenv("TMPDIR");
	if (!d || !*d) d = getenv("TMP");
	if (!d || !*d) d = getenv("TEMP");
	if (!d || !*d) d = ".";
	return d;
}

static int man_run_external_pager(const char *pager, const char *styled, size_t len)
{
	const char *dir = man_tmpdir();
	char *tmpl;
	size_t dn = strlen(dir);
	int fd;
	FILE *f;
	int rc = -1;
	char *argv[64];
	int argc = 0;
	char *pcopy, *tok, *save = 0;
	char *resolved;
	int pid, status;

	tmpl = malloc(dn + sizeof "/ntlibc-manXXXXXX");
	if (!tmpl) return -1;
	snprintf(tmpl, dn + sizeof "/ntlibc-manXXXXXX",
	    "%s/ntlibc-manXXXXXX", dir);
	fd = mkstemp(tmpl);
	if (fd < 0) { free(tmpl); return -1; }
	f = fdopen(fd, "wb");
	if (!f) { close(fd); unlink(tmpl); free(tmpl); return -1; }
	if (man_render_write(f, styled, len, MAN_RENDER_OVERSTRIKE) < 0) { fclose(f); unlink(tmpl); free(tmpl); return -1; }
	if (fclose(f) != 0) { unlink(tmpl); free(tmpl); return -1; }

	pcopy = strdup(pager);
	if (!pcopy) { unlink(tmpl); free(tmpl); return -1; }
	/* Split $PAGER on whitespace only -- no shell-quoting support, a
	 * deliberate, documented limit (see this file's own header
	 * comment). */
	for (tok = strtok_r(pcopy, " \t", &save); tok && argc < 62; tok = strtok_r(0, " \t", &save))
		argv[argc++] = tok;
	if (argc == 0) { free(pcopy); unlink(tmpl); free(tmpl); return -1; }
	argv[argc++] = tmpl;
	argv[argc] = 0;

	resolved = __find_program(argv[0], 1);
	if (resolved) {
		pid = __spawn(resolved, argv, environ);
		free(resolved);
		if (pid >= 0 && waitpid(pid, &status, 0) >= 0) rc = 0;
	}

	free(pcopy);
	unlink(tmpl);
	free(tmpl);
	return rc;
}

/* Shows one already-formatted page, choosing direct/external-pager/
 * built-in-pager per this file's own header comment ("PAGING"). */
static void man_display(struct man_buf *formatted)
{
	int tty = isatty(STDOUT_FILENO);
	const char *pager = tty ? getenv("PAGER") : 0;

	if (!tty) {
		(void)man_render_write(stdout, formatted->data ? formatted->data : "", formatted->len, MAN_RENDER_OVERSTRIKE);
		return;
	}
	if (pager && *pager) {
		if (man_run_external_pager(pager, formatted->data ? formatted->data : "", formatted->len) == 0) return;
		/* $PAGER failed to run at all: fall back to the built-in one
		 * rather than losing the page entirely. */
	}
	(void)man_builtin_pager(formatted->data ? formatted->data : "", formatted->len, man_term_height());
}

/* ==== finding a page on MANPATH ========================================= */

#define MAN_DEFAULT_MANPATH "/usr/share/man:/usr/local/share/man"
#define MAN_DEFAULT_SECTIONS "1:2:3:4:5:6:7:8:9"

static int man_looks_like_section(const char *s)
{
	if (!isdigit((unsigned char)s[0])) return 0;
	for (s++; *s; s++) if (!isalnum((unsigned char)*s)) return 0;
	return 1;
}

/* Splits a colon-separated string into a NULL-terminated, malloc'd
 * array of malloc'd component strings. */
static char **man_split_colon(const char *s)
{
	struct man_argv a;
	size_t i = 0, n = strlen(s);
	memset(&a, 0, sizeof a);
	while (i <= n) {
		size_t start = i;
		while (i < n && s[i] != ':') i++;
		if (i > start) { if (!man_argv_push(&a, s + start, i - start)) { man_argv_free(&a); return 0; } }
		if (i >= n) break;
		i++;
	}
	if (!man_argv_push(&a, "", 0)) { man_argv_free(&a); return 0; } /* NULL terminator slot */
	free(a.v[a.n - 1]);
	a.v[a.n - 1] = 0;
	return a.v;
}

static void man_free_strv(char **v)
{
	size_t i;
	if (!v) return;
	for (i = 0; v[i]; i++) free(v[i]);
	free(v);
}

/* Looks for <dir>/man<section>/<name>.<section> under every directory
 * in `manpath`. On success returns a malloc'd path; on "found, but
 * only as a .gz" sets *gz_only and returns NULL (see this file's own
 * header comment on why gzip decompression is out of scope). */
static char *man_find_one(char **manpath, const char *section, const char *name, int *gz_only)
{
	size_t i;
	for (i = 0; manpath[i]; i++) {
		char path[4096];
		int n = snprintf(path, sizeof path, "%s/man%s/%s.%s", manpath[i], section, name, section);
		struct stat st;
		if (n < 0 || (size_t)n >= sizeof path) continue;
		if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) return strdup(path);
		{
			char gzpath[4096 + 3];
			int gn = snprintf(gzpath, sizeof gzpath, "%s.gz", path);
			if (gn > 0 && (size_t)gn < sizeof gzpath && stat(gzpath, &st) == 0 && S_ISREG(st.st_mode))
				*gz_only = 1;
		}
	}
	return 0;
}

static char *man_find_page(char **manpath, char **sections, const char *name, char **out_section)
{
	size_t i;
	int gz_only = 0;

	for (i = 0; sections[i]; i++) {
		char *p = man_find_one(manpath, sections[i], name, &gz_only);
		if (p) { *out_section = sections[i]; return p; }
	}
	if (gz_only)
		__util_diagf("man: %s: found but is gzip-compressed; this implementation does not decompress pages\n", name);
	return 0;
}

/* Reads a whole file into a malloc'd buffer (bounded, see
 * MAN_MAX_PAGE_SIZE). Returns 1/0; out and outlen are both valid on
 * success. */
static int man_read_file(const char *path, char **out, size_t *outlen)
{
	FILE *f = fopen(path, "rb");
	struct man_buf b;
	char chunk[65536];
	size_t r;

	if (!f) return 0;
	memset(&b, 0, sizeof b);
	while ((r = fread(chunk, 1, sizeof chunk, f)) > 0) {
		if (b.len + r > MAN_MAX_PAGE_SIZE) {
			__util_diagf("man: %s: page too large, truncating at %d bytes\n", path, MAN_MAX_PAGE_SIZE);
			break;
		}
		if (!mbuf_append(&b, chunk, r)) { mbuf_free(&b); fclose(f); return 0; }
	}
	fclose(f);
	*out = b.data ? b.data : strdup("");
	*outlen = b.len;
	return *out != 0;
}

/* ==== -k: apropos-style NAME-line scan (see this file's own header ====
 * comment for why this is a real, honest degrade rather than a real
 * whatis database). */

static int man_apropos(char **manpath, char **keywords, size_t nkeywords)
{
	size_t mi;
	int any = 0;

	for (mi = 0; manpath[mi]; mi++) {
		char secdir[4096];
		int sec;
		for (sec = 1; sec <= 9; sec++) {
			DIR *dp;
			struct dirent *de;
			int n = snprintf(secdir, sizeof secdir, "%s/man%d", manpath[mi], sec);
			if (n < 0 || (size_t)n >= sizeof secdir) continue;
			dp = opendir(secdir);
			if (!dp) continue;
			while ((de = readdir(dp)) != 0) {
				char path[4096 + 256];
				char *text; size_t tlen;
				size_t nl = strlen(de->d_name);
				size_t k;
				if (nl < 3 || de->d_name[nl - 2] != '.') continue; /* need "<base>.<digit>" */
				n = snprintf(path, sizeof path, "%s/%s", secdir, de->d_name);
				if (n < 0 || (size_t)n >= sizeof path) continue;
				if (!man_read_file(path, &text, &tlen)) continue;
				for (k = 0; k < nkeywords; k++) {
					const char *namehit = 0, *p = text;
					const char *end = text + tlen;
					while (p < end) {
						const char *nl2 = memchr(p, '\n', (size_t)(end - p));
						size_t linelen = nl2 ? (size_t)(nl2 - p) : (size_t)(end - p);
						if (linelen == 8 && !strncmp(p, ".SH NAME", 8)) { namehit = p; break; }
						p = nl2 ? nl2 + 1 : end;
					}
					if (namehit) {
						const char *q = memchr(namehit, '\n', (size_t)(end - namehit));
						q = q ? q + 1 : end;
						{
							const char *qend = memchr(q, '\n', (size_t)(end - q));
							size_t qlen = qend ? (size_t)(qend - q) : (size_t)(end - q);
							size_t x, klen = strlen(keywords[k]);
							for (x = 0; klen > 0 && x + klen <= qlen; x++) {
								if (strncasecmp(q + x, keywords[k], klen) == 0) {
									char base[256];
									size_t bn = nl - 2;
									if (bn >= sizeof base) bn = sizeof base - 1;
									for (size_t bi = 0; bi < bn; bi++)
										base[bi] = de->d_name[bi];
									base[bn] = 0;
									printf("%s(%d) - %.*s\n", base, sec, (int)qlen, q);
									any = 1;
									break;
								}
							}
						}
					}
				}
				free(text);
			}
			closedir(dp);
		}
	}
	return any;
}

/* ==== entry point ======================================================= */

int __util_man_main(
	int argc, char **argv elements_withtok(null_terminated, argc))
{
	int i;
	int opt_k = 0;
	int width;
	char **manpath = 0;
	char **sections = 0;
	const char *forced_section = 0;
	int had_error = 0;
	int shown_any = 0;
	struct man_argv names;
	size_t ni;

	memset(&names, 0, sizeof names);

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--")) { i++; break; }
		if (!strcmp(argv[i], "-k")) { opt_k = 1; continue; }
		if (argv[i][0] == '-' && argv[i][1] != 0) {
			__util_diagf("man: invalid option -- '%s'\n", argv[i]);
			return 1;
		}
		break;
	}

	if (!opt_k && i < argc && (argc - i) >= 2 && man_looks_like_section(argv[i]))
		forced_section = argv[i++];

	for (; i < argc; i++)
		if (!man_argv_push(&names, argv[i], strlen(argv[i]))) { man_argv_free(&names); return 1; }

	if (names.n == 0) {
		__util_diagf("man: what manual page do you want?\n");
		man_argv_free(&names);
		return 1;
	}

	{
		const char *mp = getenv("MANPATH");
		manpath = man_split_colon(mp && *mp ? mp : MAN_DEFAULT_MANPATH);
	}
	{
		const char *ms = getenv("MANSECT");
		sections = forced_section ? 0 : man_split_colon(ms && *ms ? ms : MAN_DEFAULT_SECTIONS);
	}
	if (!manpath || (!forced_section && !sections)) {
		__util_diagf("man: out of memory\n");
		man_free_strv(manpath); man_free_strv(sections); man_argv_free(&names);
		return 1;
	}

	width = man_term_width();

	if (opt_k) {
		if (!man_apropos(manpath, names.v, names.n)) {
			__util_diagf("man: nothing appropriate\n");
			had_error = 1;
		}
		man_free_strv(manpath); man_free_strv(sections); man_argv_free(&names);
		return had_error ? 1 : 0;
	}

	{
		char *forced_arr[2];
		char **use_sections = sections;
		if (forced_section) { forced_arr[0] = (char *)forced_section; forced_arr[1] = 0; use_sections = forced_arr; }

		for (ni = 0; ni < names.n; ni++) {
			char *found_section = 0;
			char *path = man_find_page(manpath, use_sections, names.v[ni], &found_section);
			char *text; size_t tlen;
			struct man_buf formatted;

			if (!path) {
				__util_diagf("man: No manual entry for %s\n", names.v[ni]);
				had_error = 1;
				continue;
			}
			if (!man_read_file(path, &text, &tlen)) {
				__util_diagf("man: %s: %s\n", path, strerror(errno));
				free(path);
				had_error = 1;
				continue;
			}
			free(path);

			memset(&formatted, 0, sizeof formatted);
			if (!man_format(text, tlen, width, &formatted)) {
				__util_diagf("man: %s: formatting failed (out of memory)\n", names.v[ni]);
				free(text);
				had_error = 1;
				continue;
			}
			free(text);
			man_display(&formatted);
			mbuf_free(&formatted);
			shown_any = 1;
		}
	}

	man_free_strv(manpath);
	man_free_strv(sections);
	man_argv_free(&names);
	return (had_error || !shown_any) ? 1 : 0;
}

// NOLINTEND(misc-include-cleaner)
