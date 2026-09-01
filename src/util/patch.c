/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * patch(1p): `patch [-blNR] [-c|-e|-n|-u] [-d dir] [-D define] [-i
 * patchfile] [-o outfile] [-p num] [-r rejectfile] [file]` -- apply a
 * diff(1)-style difference report to a file.  Citations below are to
 * the real Open Group Base Specifications Issue 7 (POSIX.1-2017) XCU
 * patch(1p) page (fetched directly, not reconstructed from memory or
 * from GNU patch's much larger option set -- several GNU-only options,
 * e.g. --dry-run, -F/--fuzz=, -B/--backup-if-mismatch, are simply not
 * here at all, and are not claimed to be).
 *
 * ---- FORMATS ---------------------------------------------------------
 *
 * DESCRIPTION: the utility "reads patch files containing diff output in
 * ... normal, copied context, unified context, or in the style of ed
 * ... and applies those differences" -- all four are implemented
 * (parse_normal_section(), parse_context_section(), parse_unified_
 * section(), parse_ed_section() below), with automatic format detection
 * (detect_at()) when none of -c/-e/-n/-u is given, matching "[the
 * utility] attempts automatic format detection unless overridden."
 *
 * ---- MATCHING / FUZZ --------------------------------------------------
 *
 * "For each hunk, the patch utility shall begin to search for the place
 * to apply the patch at the line number at the beginning of the hunk,
 * plus or minus any offset used in applying the previous hunk.  If
 * lines matching the hunk context are not found, patch shall scan both
 * forwards and backwards at least 1000 bytes for a set of lines that
 * match the hunk context." -- find_match() below implements exactly
 * this: an exact try at the offset-adjusted expected position, then an
 * outward forward/backward scan.  This implementation's scan covers the
 * *entire* remaining buffer rather than stopping at a fixed byte count,
 * a strict superset of "at least 1000 bytes" -- except that the scan is
 * bounded below by `src`, the position already consumed by the previous
 * *successfully applied* hunk (apply_section() below), so a later hunk
 * can never match backward into content an earlier hunk already
 * emitted.  Real patch(1p) does not document this restriction either
 * way; it is a deliberate, narrow simplification here to keep hunk
 * application a single left-to-right pass with no reordering, which
 * this implementation's whole design (build the output by copying
 * source spans between matched hunks, see apply_section()) depends on.
 *
 * "already-applied" detection (a hunk's *new* content, not its old
 * content, already present at the expected location) is implemented the
 * same way, per -N's own wording below.
 *
 * When nothing matches at all -- neither the old content nor,
 * distinguishing this from a genuine mismatch, the already-applied new
 * content -- "the patch utility shall append the hunk to the reject
 * file" (write_rejects() below).  POSIX does not mandate the reject
 * file's own byte format (only that unmatched hunks land there), so
 * this implementation always writes rejects in one fixed, simple
 * unified-diff-shaped block (`@@ -old,oldcount +new,newcount @@` plus
 * ` `/`-`/`+`-prefixed lines), regardless of which format the input
 * patch used -- a real, deliberate simplification over round-tripping
 * each format's own reject syntax byte for byte.
 *
 * ---- OPTIONS IMPLEMENTED ----------------------------------------------
 *
 *  -b            "Save a copy of the original contents of each modified
 *                file, before the differences are applied, in a file of
 *                the same name with the suffix .orig appended to it."
 *                Implemented for the in-place (no -o) case only, which
 *                is the only case where a file is actually "modified"
 *                in place; -o redirects all output elsewhere and never
 *                touches the target file, so -b is a no-op alongside it
 *                (matching the literal "each modified file" wording).
 *  -c/-e/-n/-u   Force copied-context / ed / normal / unified
 *                interpretation instead of auto-detecting.
 *  -d dir        "Change the current directory to dir before
 *                processing."  The patch input itself (-i or stdin) is
 *                opened and fully read *before* the chdir(), matching
 *                conventional patch behaviour that -i's own pathname is
 *                resolved relative to the invoking directory, not the
 *                relocated one; every other pathname (the file operand,
 *                -o, -r, and each patch header's own filename) is
 *                resolved after the chdir(), which is the whole point
 *                of the option.
 *  -D define     "Mark changes with one of the following C preprocessor
 *                constructs: #ifdef define ... #endif" (pure addition)
 *                or "#ifndef define ... #endif" (pure deletion); a
 *                genuine change (old lines replaced by new ones) wraps
 *                both halves in a single #ifndef/#else/#endif.  Refused
 *                (loud diagnostic, nonzero exit -- this project's usual
 *                "never silently do something other than what was
 *                asked" rule, see e.g. src/util/sort.c's -m or
 *                src/util/dd.c's conv= for the same convention) when
 *                combined with -e: an ed script's 'd' command never
 *                records the text it deletes (see ---- ED SCRIPTS ----
 *                below), so there is no old-line text available to wrap
 *                in the #ifndef branch.
 *  -i patchfile  "Read the patch information from the file named by the
 *                pathname patchfile, rather than the standard input."
 *  -l            "Cause any sequence of <blank> characters in the
 *                difference script to match any sequence of <blank>
 *                characters in the input file."  Applied only to the
 *                *matching* step (ws_loose_equal() below); every byte
 *                actually written to the patched output -- context
 *                lines, and old-line text wrapped by -D -- is always
 *                copied from the target file's own literal bytes, never
 *                from the patch file's copy, so -l never rewrites
 *                whitespace into the result the way a naive
 *                text-substitution implementation might.
 *  -N            "Ignore patches where the differences have already
 *                been applied to the file; by default, already-applied
 *                patches shall be rejected."  Both halves implemented:
 *                default behaviour treats an already-applied hunk as a
 *                reject (same accounting as a real mismatch, exit
 *                status 1), and -N instead copies the already-new
 *                content through untouched and continues.
 *  -o outfile    "Instead of modifying the files ... directly, write a
 *                copy of the file referenced by each patch, with the
 *                appropriate differences applied, to outfile."  The
 *                spec continues: "Multiple patches for a single file
 *                shall be applied to the intermediate versions of the
 *                file created by any previous patches, and shall result
 *                in multiple, concatenated versions of the file being
 *                written to outfile" -- describing the case of several
 *                *independent* patch files, each named on its own patch
 *                invocation, sharing one outfile across several runs.
 *                Within a *single* invocation, when an explicit file
 *                operand is given together with a patch stream that
 *                itself contains more than one file-header section (an
 *                unusual combination -- normally a stream with several
 *                sections has no operand at all, letting each section
 *                pick its own target from its own header), this
 *                implementation applies every hunk from every section,
 *                in stream order, to that one operand file as a single
 *                combined pass and writes one result, rather than
 *                producing the "multiple, concatenated versions"
 *                literal reading for that specific corner case -- a
 *                real, deliberate narrowing documented here because
 *                nothing in this project's own usage needs the
 *                concatenated-copies behaviour and it does not fit this
 *                implementation's single-pass design.
 *  -p num        "For all pathnames in the patch file that indicate the
 *                names of files to be patched, delete num pathname
 *                components from the beginning of each pathname."
 *                (strip_components() below; components are '/'-
 *                separated, and stripping more than exist in a name
 *                just leaves its bare basename rather than erroring.)
 *  -R            "Reverse the sense of the patch script; that is,
 *                assume that the difference script was created from the
 *                new version to the old version."  Refused when
 *                combined with -e (an ed script has no old-line text to
 *                reconstruct a reverse application from, and the
 *                specification's own OPTIONS text for -R notes the ed-
 *                script case is excluded).
 *  -r rejectfile "Override the default reject filename.  In the default
 *                case, the reject file shall have the same name as the
 *                output file, with the suffix .rej appended to it."
 *  file          OPERANDS: "A pathname of a file to patch."  When
 *                given, it overrides whatever filename a context/
 *                unified header would otherwise select (see ---- NAME
 *                SELECTION ---- below), and is *required* -- rather
 *                than merely preferred -- for the normal and ed
 *                formats, neither of which carries any filename at all
 *                in its own patch text.
 *
 * ---- NAME SELECTION (context/unified, no file operand) ----------------
 *
 * The real spec's EXTENDED DESCRIPTION describes a five-step filename-
 * determination algorithm (context-diff markers, then the "---"/"+++"
 * pair, then an "Index:" header, then an SCCS-retrieval attempt, and
 * finally an interactive prompt).  This implementation narrows that to
 * the first, cheapest, always-available part of it: after -p stripping,
 * pick_target_name() below prefers the "old" name if a file by that
 * name already exists, otherwise falls back to the "new" name.  SCCS/
 * RCS retrieval and interactive prompting are both deliberately not
 * implemented -- this utility, like every other one in this project, is
 * meant to run with no fork/exec dependency during early bootstrap
 * (src/internal/util.h's own header comment), where prompting a
 * terminal that may not exist yet is not a meaningful fallback anyway.
 *
 * ---- ED SCRIPTS --------------------------------------------------------
 *
 * A `diff -e` script carries no context at all: each command is just
 * `addr1[,addr2]{a,c,d}`, optionally followed by literal replacement
 * text terminated by a line containing exactly ".".  Because such a
 * script's commands are always emitted in descending order of original
 * line number (each one operates on lines strictly below every command
 * still to come), apply_ed_section() below applies them directly,
 * splicing into a single in-memory copy of the file addressed by each
 * command's literal 1-based line numbers, with no matching or offset
 * bookkeeping at all -- unlike the other three formats, there is
 * nothing here for -l's whitespace looseness or the fuzzy scan above to
 * apply to, since there is no context to compare against.
 *
 * ---- EXIT STATUS -------------------------------------------------------
 *
 * "0 Successful completion." / "1 One or more lines were written to a
 * reject file." / ">1 An error occurred." -- note that 1 is reserved for
 * the specific "hunks were rejected" outcome, not a generic error, so
 * usage errors and I/O failures below always return 2, never 1 (the
 * same distinction src/util/sort.c's own header comment makes for its
 * -c option's exit status).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include "util.h"

/* ==== line storage ========================================================
 *
 * Every line of text this file handles -- patch-file input, a target
 * file's content, or a hunk's own stored context/old/new text -- is
 * kept the same way: a heap copy of the bytes with no trailing newline,
 * plus a flag for whether the source actually had one (the only place
 * this matters is the very last line written to any output; see
 * write_linebuf_stream() below). */

struct pline { char *text; size_t len; int has_nl; };
struct linebuf { struct pline *v; size_t n, cap; };

static int lb_push(struct linebuf *lb, const char *text, size_t len, int has_nl)
{
	char *copy;
	size_t bytes;
	struct pline *g;

	if (!__util_size_add(len, 1, &bytes)) return 0;
	copy = malloc(bytes);
	if (!copy) return 0;
	memcpy(copy, text, len);
	copy[len] = 0;

	if (lb->n >= lb->cap) {
		size_t newcap;
		if (!__util_array_capacity(lb->cap, lb->n, 1, 64, sizeof *lb->v, &newcap)) { free(copy); return 0; }
		g = __util_reallocarray(lb->v, newcap, sizeof *lb->v);
		if (!g) { free(copy); return 0; }
		lb->v = g; lb->cap = newcap;
	}
	lb->v[lb->n].text = copy;
	lb->v[lb->n].len = len;
	lb->v[lb->n].has_nl = has_nl;
	lb->n++;
	return 1;
}

static int lb_push_str(struct linebuf *lb, const char *s) { return lb_push(lb, s, strlen(s), 1); }

static int lb_push_fmt(struct linebuf *lb, const char *fmt, const char *arg)
{
	char buf[512];
	snprintf(buf, sizeof buf, fmt, arg);
	return lb_push_str(lb, buf);
}

static void free_linebuf(struct linebuf *lb)
{
	size_t i;
	for (i = 0; i < lb->n; i++) free(lb->v[i].text);
	free(lb->v);
	lb->v = 0; lb->n = 0; lb->cap = 0;
}

static int read_all_lines(FILE *f, struct linebuf *lb)
{
	char *buf = 0;
	size_t bufcap = 0;
	ssize_t got;

	while ((got = getline(&buf, &bufcap, f)) >= 0) {
		size_t len = (size_t)got;
		int has_nl = (len && buf[len - 1] == '\n');
		if (has_nl) len--;
		if (!lb_push(lb, buf, len, has_nl)) { free(buf); return -1; }
	}
	free(buf);
	return ferror(f) ? -1 : 0;
}

static int write_linebuf_stream(FILE *f, const struct linebuf *lb)
{
	size_t i;
	for (i = 0; i < lb->n; i++) {
		if (lb->v[i].len && fwrite(lb->v[i].text, 1, lb->v[i].len, f) != lb->v[i].len) return -1;
		/* Only the true last line of the whole output may legitimately
		 * lack a trailing newline; every earlier line always gets one
		 * regardless of its own stored flag (a real file cannot have a
		 * missing newline in the middle without corrupting every line
		 * after it, so this is a safe simplification, not a loss). */
		if (i + 1 < lb->n || lb->v[i].has_nl) { if (fputc('\n', f) == EOF) return -1; }
	}
	return 0;
}

static int write_linebuf(const char *path, const struct linebuf *lb)
{
	FILE *f = fopen(path, "wb");
	int rc;
	if (!f) return -1;
	rc = write_linebuf_stream(f, lb);
	if (fclose(f) != 0) return -1;
	return rc;
}

static void linebuf_remove_range(struct linebuf *lb, size_t lo, size_t hi)
{
	size_t i;
	if (hi <= lo) return;
	for (i = lo; i < hi; i++) free(lb->v[i].text);
	memmove(&lb->v[lo], &lb->v[hi], (lb->n - hi) * sizeof *lb->v);
	lb->n -= (hi - lo);
}

static int linebuf_insert_block(struct linebuf *lb, size_t at, const struct linebuf *block)
{
	size_t need = block->n, i;
	if (!need) return 1;
	if (lb->n + need > lb->cap) {
		size_t newcap;
		struct pline *g;
		if (!__util_array_capacity(lb->cap, lb->n, need, 64, sizeof *lb->v, &newcap)) return 0;
		g = __util_reallocarray(lb->v, newcap, sizeof *lb->v);
		if (!g) return 0;
		lb->v = g; lb->cap = newcap;
	}
	memmove(&lb->v[at + need], &lb->v[at], (lb->n - at) * sizeof *lb->v);
	for (i = 0; i < need; i++) {
		char *copy;
		size_t bytes;
		if (!__util_size_add(block->v[i].len, 1, &bytes)) return 0;
		copy = malloc(bytes);
		if (!copy) return 0;
		memcpy(copy, block->v[i].text, block->v[i].len);
		copy[block->v[i].len] = 0;
		lb->v[at + i].text = copy;
		lb->v[at + i].len = block->v[i].len;
		lb->v[at + i].has_nl = block->v[i].has_nl;
	}
	lb->n += need;
	return 1;
}

/* ==== hunks (normal/context/unified share this one representation) ======= */

enum hop_kind { HOP_CTX, HOP_DEL, HOP_ADD };
struct hop { enum hop_kind kind; struct pline p; };

struct hunk {
	long old_start, old_count, new_start, new_count;
	struct hop *v; size_t n, cap;
	int old_no_nl, new_no_nl;
};

static int hunk_push(struct hunk *h, enum hop_kind kind, const char *text, size_t len, int has_nl)
{
	char *copy;
	size_t bytes;
	struct hop *g;

	if (!__util_size_add(len, 1, &bytes)) return 0;
	copy = malloc(bytes);
	if (!copy) return 0;
	memcpy(copy, text, len);
	copy[len] = 0;

	if (h->n >= h->cap) {
		size_t newcap;
		if (!__util_array_capacity(h->cap, h->n, 1, 32, sizeof *h->v, &newcap)) { free(copy); return 0; }
		g = __util_reallocarray(h->v, newcap, sizeof *h->v);
		if (!g) { free(copy); return 0; }
		h->v = g; h->cap = newcap;
	}
	h->v[h->n].kind = kind;
	h->v[h->n].p.text = copy;
	h->v[h->n].p.len = len;
	h->v[h->n].p.has_nl = has_nl;
	h->n++;
	return 1;
}

static void free_hunk(struct hunk *h)
{
	size_t i;
	for (i = 0; i < h->n; i++) free(h->v[i].p.text);
	free(h->v);
	h->v = 0; h->n = 0; h->cap = 0;
}

static void reverse_hunk(struct hunk *h)
{
	size_t i;
	long t;
	int tn;

	t = h->old_start; h->old_start = h->new_start; h->new_start = t;
	t = h->old_count; h->old_count = h->new_count; h->new_count = t;
	tn = h->old_no_nl; h->old_no_nl = h->new_no_nl; h->new_no_nl = tn;
	for (i = 0; i < h->n; i++) {
		if (h->v[i].kind == HOP_DEL) h->v[i].kind = HOP_ADD;
		else if (h->v[i].kind == HOP_ADD) h->v[i].kind = HOP_DEL;
	}
}

/* ==== ed script commands ================================================== */

struct edcmd { long a1, a2; char op; struct linebuf text; };

/* ==== one file's worth of patch (one or more hunks/ed commands) ========== */

enum diff_format { FMT_UNKNOWN = 0, FMT_NORMAL, FMT_CONTEXT, FMT_UNIFIED, FMT_ED };

struct patchfile {
	enum diff_format fmt;
	char *old_name, *new_name;
	struct hunk *hunks; size_t nhunks, hcap;
	struct edcmd *eds; size_t neds, ecap;
};

static int patchfile_push_hunk(struct patchfile *pf, const struct hunk *h)
{
	if (pf->nhunks >= pf->hcap) {
		size_t newcap;
		struct hunk *g;
		if (!__util_array_capacity(pf->hcap, pf->nhunks, 1, 16, sizeof *pf->hunks, &newcap)) return 0;
		g = __util_reallocarray(pf->hunks, newcap, sizeof *pf->hunks);
		if (!g) return 0;
		pf->hunks = g; pf->hcap = newcap;
	}
	pf->hunks[pf->nhunks++] = *h;
	return 1;
}

static int patchfile_push_ed(struct patchfile *pf, const struct edcmd *e)
{
	if (pf->neds >= pf->ecap) {
		size_t newcap;
		struct edcmd *g;
		if (!__util_array_capacity(pf->ecap, pf->neds, 1, 16, sizeof *pf->eds, &newcap)) return 0;
		g = __util_reallocarray(pf->eds, newcap, sizeof *pf->eds);
		if (!g) return 0;
		pf->eds = g; pf->ecap = newcap;
	}
	pf->eds[pf->neds++] = *e;
	return 1;
}

static void free_patchfile(struct patchfile *pf)
{
	size_t i;
	free(pf->old_name); free(pf->new_name);
	for (i = 0; i < pf->nhunks; i++) free_hunk(&pf->hunks[i]);
	free(pf->hunks);
	for (i = 0; i < pf->neds; i++) free_linebuf(&pf->eds[i].text);
	free(pf->eds);
}

/* ==== small text-matching helpers ========================================= */

static int starts_with(const struct pline *pl, const char *prefix)
{
	size_t plen = strlen(prefix);
	return pl->len >= plen && memcmp(pl->text, prefix, plen) == 0;
}

static int is_all_stars(const struct pline *pl)
{
	size_t k;
	if (pl->len < 3) return 0;
	for (k = 0; k < pl->len; k++) if (pl->text[k] != '*') return 0;
	return 1;
}

static const char *parse_uint(const char *s, long *out)
{
	long v = 0;
	if (!isdigit((unsigned char)*s)) return 0;
	while (isdigit((unsigned char)*s)) { v = v * 10 + (*s - '0'); s++; }
	*out = v;
	return s;
}

static int is_normal_header(const struct pline *pl, long *o1, long *o2, char *cmd, long *n1, long *n2)
{
	const char *s = pl->text;
	long a, b = -1, c, d = -1;

	s = parse_uint(s, &a); if (!s) return 0;
	if (*s == ',') { s++; s = parse_uint(s, &b); if (!s) return 0; }
	if (*s != 'a' && *s != 'c' && *s != 'd') return 0;
	*cmd = *s; s++;
	s = parse_uint(s, &c); if (!s) return 0;
	if (*s == ',') { s++; s = parse_uint(s, &d); if (!s) return 0; }
	if (*s != 0) return 0;
	*o1 = a; *o2 = (b >= 0 ? b : a);
	*n1 = c; *n2 = (d >= 0 ? d : c);
	return 1;
}

static int is_normal_header_line(const struct pline *pl)
{
	long o1, o2, n1, n2; char cmd;
	return is_normal_header(pl, &o1, &o2, &cmd, &n1, &n2);
}

static int is_ed_header(const struct pline *pl, long *a1, long *a2, char *op)
{
	const char *s = pl->text;
	long a, b = -1;

	s = parse_uint(s, &a); if (!s) return 0;
	if (*s == ',') { s++; s = parse_uint(s, &b); if (!s) return 0; }
	if (*s != 'a' && *s != 'c' && *s != 'd') return 0;
	*op = *s; s++;
	if (*s != 0) return 0;
	*a1 = a; *a2 = (b >= 0 ? b : a);
	return 1;
}

static int is_ed_header_line(const struct pline *pl)
{
	long a1, a2; char op;
	return is_ed_header(pl, &a1, &a2, &op);
}

static int parse_at_header(const struct pline *pl, long *o1, long *oc, long *n1, long *nc)
{
	const char *s = pl->text;
	if (!starts_with(pl, "@@ -")) return 0;
	s += 4;
	s = parse_uint(s, o1); if (!s) return 0;
	if (*s == ',') { s++; s = parse_uint(s, oc); if (!s) return 0; } else *oc = 1;
	if (*s != ' ') return 0; s++;
	if (*s != '+') return 0; s++;
	s = parse_uint(s, n1); if (!s) return 0;
	if (*s == ',') { s++; s = parse_uint(s, nc); if (!s) return 0; } else *nc = 1;
	if (*s != ' ') return 0; s++;
	if (*s != '@' || s[1] != '@') return 0;
	return 1;
}

static int parse_ctx_range(const struct pline *pl, const char *pfx, const char *sfx, long *lo, long *hi)
{
	size_t plen = strlen(pfx), slen = strlen(sfx);
	const char *s;
	if (pl->len < plen + slen) return 0;
	if (memcmp(pl->text, pfx, plen) != 0) return 0;
	if (memcmp(pl->text + pl->len - slen, sfx, slen) != 0) return 0;
	s = pl->text + plen;
	s = parse_uint(s, lo); if (!s) return 0;
	if (*s == ',') { s++; s = parse_uint(s, hi); if (!s) return 0; } else *hi = *lo;
	return (size_t)(s - pl->text) == pl->len - slen;
}

static int parse_name_line(const struct pline *pl, const char *pfx, char **out)
{
	size_t plen = strlen(pfx), namelen, i;
	const char *p;
	if (pl->len < plen || memcmp(pl->text, pfx, plen) != 0) return 0;
	p = pl->text + plen;
	namelen = pl->len - plen;
	for (i = 0; i < namelen; i++) if (p[i] == '\t') { namelen = i; break; }
	*out = malloc(namelen + 1);
	if (!*out) return 0;
	memcpy(*out, p, namelen);
	(*out)[namelen] = 0;
	return 1;
}

/* Whitespace-run-insensitive comparison for -l: "any sequence of <blank>
 * characters ... match any sequence of <blank> characters". */
static int ws_loose_equal(const char *a, const char *b)
{
	for (;;) {
		if (isblank((unsigned char)*a) && isblank((unsigned char)*b)) {
			while (isblank((unsigned char)*a)) a++;
			while (isblank((unsigned char)*b)) b++;
			continue;
		}
		if (*a != *b) return 0;
		if (!*a) return 1;
		a++; b++;
	}
}

/* ==== normal-format hunk parsing ========================================== */

static int parse_normal_hunk(struct linebuf *L, size_t *ip, struct hunk *h)
{
	long o1, o2, n1, n2; char cmd; long k;

	if (!is_normal_header(&L->v[*ip], &o1, &o2, &cmd, &n1, &n2)) return 0;
	(*ip)++;
	memset(h, 0, sizeof *h);
	if (cmd == 'a') { h->old_start = o1; h->old_count = 0; h->new_start = n1; h->new_count = n2 - n1 + 1; }
	else if (cmd == 'd') { h->old_start = o1; h->old_count = o2 - o1 + 1; h->new_start = n1; h->new_count = 0; }
	else { h->old_start = o1; h->old_count = o2 - o1 + 1; h->new_start = n1; h->new_count = n2 - n1 + 1; }

	if (cmd == 'd' || cmd == 'c') {
		for (k = 0; k < h->old_count; k++) {
			struct pline *pl = (*ip < L->n) ? &L->v[*ip] : 0;
			if (!pl || !starts_with(pl, "< ")) return 0;
			if (!hunk_push(h, HOP_DEL, pl->text + 2, pl->len - 2, 1)) return 0;
			(*ip)++;
		}
	}
	if (cmd == 'c') {
		if (*ip >= L->n || L->v[*ip].len != 3 || memcmp(L->v[*ip].text, "---", 3) != 0) return 0;
		(*ip)++;
	}
	if (cmd == 'a' || cmd == 'c') {
		for (k = 0; k < h->new_count; k++) {
			struct pline *pl = (*ip < L->n) ? &L->v[*ip] : 0;
			if (!pl || !starts_with(pl, "> ")) return 0;
			if (!hunk_push(h, HOP_ADD, pl->text + 2, pl->len - 2, 1)) return 0;
			(*ip)++;
		}
	}
	return 1;
}

static int parse_normal_section(struct linebuf *L, size_t *ip, struct patchfile *pf)
{
	pf->fmt = FMT_NORMAL;
	while (*ip < L->n && is_normal_header_line(&L->v[*ip])) {
		struct hunk h;
		if (!parse_normal_hunk(L, ip, &h)) return 0;
		if (!patchfile_push_hunk(pf, &h)) { free_hunk(&h); return 0; }
	}
	return pf->nhunks > 0;
}

/* ==== unified-format hunk parsing ========================================= */

static int parse_unified_hunk(struct linebuf *L, size_t *ip, struct hunk *h)
{
	long o1, oc, n1, nc, oleft, nleft;

	if (!parse_at_header(&L->v[*ip], &o1, &oc, &n1, &nc)) return 0;
	(*ip)++;
	memset(h, 0, sizeof *h);
	h->old_start = o1; h->old_count = oc; h->new_start = n1; h->new_count = nc;
	oleft = oc; nleft = nc;

	while (oleft > 0 || nleft > 0) {
		struct pline *pl;
		if (*ip >= L->n) return 0;
		pl = &L->v[*ip];
		if (pl->len == 0) {
			if (!hunk_push(h, HOP_CTX, "", 0, 1)) return 0;
			oleft--; nleft--;
		} else if (pl->text[0] == ' ') {
			if (!hunk_push(h, HOP_CTX, pl->text + 1, pl->len - 1, 1)) return 0;
			oleft--; nleft--;
		} else if (pl->text[0] == '-') {
			if (!hunk_push(h, HOP_DEL, pl->text + 1, pl->len - 1, 1)) return 0;
			oleft--;
		} else if (pl->text[0] == '+') {
			if (!hunk_push(h, HOP_ADD, pl->text + 1, pl->len - 1, 1)) return 0;
			nleft--;
		} else return 0;
		(*ip)++;
		if (*ip < L->n && starts_with(&L->v[*ip], "\\ ")) {
			h->v[h->n - 1].p.has_nl = 0;
			if (h->v[h->n - 1].kind != HOP_ADD) h->old_no_nl = 1;
			if (h->v[h->n - 1].kind != HOP_DEL) h->new_no_nl = 1;
			(*ip)++;
		}
	}
	return 1;
}

static int parse_unified_section(struct linebuf *L, size_t *ip, struct patchfile *pf)
{
	if (!parse_name_line(&L->v[*ip], "--- ", &pf->old_name)) return 0;
	(*ip)++;
	if (*ip >= L->n || !parse_name_line(&L->v[*ip], "+++ ", &pf->new_name)) return 0;
	(*ip)++;
	pf->fmt = FMT_UNIFIED;
	while (*ip < L->n && starts_with(&L->v[*ip], "@@ -")) {
		struct hunk h;
		if (!parse_unified_hunk(L, ip, &h)) return 0;
		if (!patchfile_push_hunk(pf, &h)) { free_hunk(&h); return 0; }
	}
	return pf->nhunks > 0;
}

/* ==== context-format hunk parsing ========================================= */

static int parse_ctx_side_lines(struct linebuf *L, size_t *ip, struct hunk *h, int is_old)
{
	for (;;) {
		struct pline *pl;
		enum hop_kind kind;
		if (*ip >= L->n) break;
		pl = &L->v[*ip];
		if (pl->len >= 2 && pl->text[0] == ' ' && pl->text[1] == ' ') kind = HOP_CTX;
		else if (is_old && pl->len >= 2 && pl->text[0] == '-' && pl->text[1] == ' ') kind = HOP_DEL;
		else if (is_old && pl->len >= 2 && pl->text[0] == '!' && pl->text[1] == ' ') kind = HOP_DEL;
		else if (!is_old && pl->len >= 2 && pl->text[0] == '+' && pl->text[1] == ' ') kind = HOP_ADD;
		else if (!is_old && pl->len >= 2 && pl->text[0] == '!' && pl->text[1] == ' ') kind = HOP_ADD;
		else break;
		if (!hunk_push(h, kind, pl->text + 2, pl->len - 2, 1)) return 0;
		(*ip)++;
		if (*ip < L->n && starts_with(&L->v[*ip], "\\ ")) {
			h->v[h->n - 1].p.has_nl = 0;
			if (is_old) h->old_no_nl = 1; else h->new_no_nl = 1;
			(*ip)++;
		}
	}
	return 1;
}

/* Merge the separately-parsed old-side and new-side blocks of one
 * context-diff hunk into this file's single ops[] representation.
 * Context lines appear, identically, in both blocks in the same
 * relative order (they are literally unchanged text); a run of
 * non-context lines in the old block always corresponds to the next
 * run of non-context lines in the new block.  See this file's header
 * comment for how "!" (changed) lines fold into DEL for the old side
 * and ADD for the new side, which is what makes this two-pointer walk
 * correct without needing a third "changed" op kind at all. */
static int merge_context_sides(struct hunk *h, const struct hunk *oldb, const struct hunk *newb)
{
	size_t io = 0, inw = 0;

	while (io < oldb->n || inw < newb->n) {
		if (io < oldb->n && oldb->v[io].kind == HOP_CTX) {
			if (!hunk_push(h, HOP_CTX, oldb->v[io].p.text, oldb->v[io].p.len, oldb->v[io].p.has_nl)) return 0;
			io++;
			if (inw < newb->n && newb->v[inw].kind == HOP_CTX) inw++;
			continue;
		}
		while (io < oldb->n && oldb->v[io].kind != HOP_CTX) {
			if (!hunk_push(h, HOP_DEL, oldb->v[io].p.text, oldb->v[io].p.len, oldb->v[io].p.has_nl)) return 0;
			io++;
		}
		while (inw < newb->n && newb->v[inw].kind != HOP_CTX) {
			if (!hunk_push(h, HOP_ADD, newb->v[inw].p.text, newb->v[inw].p.len, newb->v[inw].p.has_nl)) return 0;
			inw++;
		}
	}
	return 1;
}

static int parse_context_hunk(struct linebuf *L, size_t *ip, struct hunk *h)
{
	long olo, ohi, nlo, nhi;
	struct hunk oldb, newb;
	int ok;

	if (*ip >= L->n || !is_all_stars(&L->v[*ip])) return 0;
	(*ip)++;
	if (*ip >= L->n || !parse_ctx_range(&L->v[*ip], "*** ", " ****", &olo, &ohi)) return 0;
	(*ip)++;

	memset(&oldb, 0, sizeof oldb);
	if (!parse_ctx_side_lines(L, ip, &oldb, 1)) { free_hunk(&oldb); return 0; }

	if (*ip >= L->n || !parse_ctx_range(&L->v[*ip], "--- ", " ----", &nlo, &nhi)) { free_hunk(&oldb); return 0; }
	(*ip)++;

	memset(&newb, 0, sizeof newb);
	if (!parse_ctx_side_lines(L, ip, &newb, 0)) { free_hunk(&oldb); free_hunk(&newb); return 0; }

	memset(h, 0, sizeof *h);
	h->old_start = olo; h->old_count = ohi >= olo ? ohi - olo + 1 : 0;
	h->new_start = nlo; h->new_count = nhi >= nlo ? nhi - nlo + 1 : 0;
	h->old_no_nl = oldb.old_no_nl; h->new_no_nl = newb.new_no_nl;

	ok = merge_context_sides(h, &oldb, &newb);
	free_hunk(&oldb); free_hunk(&newb);
	if (!ok) { free_hunk(h); return 0; }
	return 1;
}

static int parse_context_section(struct linebuf *L, size_t *ip, struct patchfile *pf)
{
	if (!parse_name_line(&L->v[*ip], "*** ", &pf->old_name)) return 0;
	(*ip)++;
	if (*ip >= L->n || !parse_name_line(&L->v[*ip], "--- ", &pf->new_name)) return 0;
	(*ip)++;
	pf->fmt = FMT_CONTEXT;
	while (*ip < L->n && is_all_stars(&L->v[*ip])) {
		struct hunk h;
		if (!parse_context_hunk(L, ip, &h)) return 0;
		if (!patchfile_push_hunk(pf, &h)) { free_hunk(&h); return 0; }
	}
	return pf->nhunks > 0;
}

/* ==== ed script parsing ==================================================== */

static int parse_ed_cmd(struct linebuf *L, size_t *ip, struct edcmd *e)
{
	long a1, a2; char op;

	if (!is_ed_header(&L->v[*ip], &a1, &a2, &op)) return 0;
	(*ip)++;
	memset(e, 0, sizeof *e);
	e->a1 = a1; e->a2 = a2; e->op = op;

	if (op == 'a' || op == 'c') {
		for (;;) {
			struct pline *pl;
			if (*ip >= L->n) return 0;
			pl = &L->v[*ip];
			if (pl->len == 1 && pl->text[0] == '.') { (*ip)++; break; }
			if (!lb_push(&e->text, pl->text, pl->len, 1)) return 0;
			(*ip)++;
		}
	}
	return 1;
}

static int parse_ed_section(struct linebuf *L, size_t *ip, struct patchfile *pf)
{
	pf->fmt = FMT_ED;
	while (*ip < L->n && is_ed_header_line(&L->v[*ip])) {
		struct edcmd e;
		if (!parse_ed_cmd(L, ip, &e)) return 0;
		if (!patchfile_push_ed(pf, &e)) { free_linebuf(&e.text); return 0; }
	}
	/* Stray trailing ed(1) session commands (some diff -e output ends
	 * with "w" and/or "q") are simply not recognized by
	 * is_ed_header_line() above, so the enclosing scan loop just stops
	 * here without treating them as an error -- they carry no meaning
	 * for in-memory patching. */
	return pf->neds > 0;
}

/* ==== top-level format detection and stream splitting ===================== */

static int section_starts_here(enum diff_format fmt, const struct linebuf *L, size_t ip)
{
	const struct pline *pl = &L->v[ip];
	switch (fmt) {
	case FMT_CONTEXT: return starts_with(pl, "*** ") && !is_all_stars(pl);
	case FMT_UNIFIED: return starts_with(pl, "--- ");
	case FMT_NORMAL:  return is_normal_header_line(pl);
	case FMT_ED:      return is_ed_header_line(pl);
	default: return 0;
	}
}

static enum diff_format detect_at(const struct linebuf *L, size_t i)
{
	const struct pline *pl = &L->v[i];
	if (starts_with(pl, "*** ") && !is_all_stars(pl)) return FMT_CONTEXT;
	if (starts_with(pl, "--- ")) return FMT_UNIFIED;
	if (is_normal_header_line(pl)) return FMT_NORMAL;
	if (is_ed_header_line(pl)) return FMT_ED;
	return FMT_UNKNOWN;
}

static int push_section(struct patchfile **arr, size_t *n, size_t *cap, const struct patchfile *pf)
{
	if (*n >= *cap) {
		size_t newcap;
		struct patchfile *g;
		if (!__util_array_capacity(*cap, *n, 1, 8, sizeof **arr, &newcap)) return 0;
		g = __util_reallocarray(*arr, newcap, sizeof **arr);
		if (!g) return 0;
		*arr = g; *cap = newcap;
	}
	(*arr)[(*n)++] = *pf;
	return 1;
}

static int parse_patch_stream(struct linebuf *L, enum diff_format forced, struct patchfile **out, size_t *nout)
{
	size_t ip = 0, cap = 0, n = 0;
	struct patchfile *arr = 0;
	enum diff_format fmt = forced;

	if (fmt == FMT_UNKNOWN) {
		size_t k;
		for (k = 0; k < L->n; k++) { fmt = detect_at(L, k); if (fmt != FMT_UNKNOWN) break; }
		if (fmt == FMT_UNKNOWN) return -1;
	}

	for (;;) {
		struct patchfile pf;
		int ok;

		while (ip < L->n && !section_starts_here(fmt, L, ip)) ip++;
		if (ip >= L->n) break;

		memset(&pf, 0, sizeof pf);
		if (fmt == FMT_CONTEXT) ok = parse_context_section(L, &ip, &pf);
		else if (fmt == FMT_UNIFIED) ok = parse_unified_section(L, &ip, &pf);
		else if (fmt == FMT_NORMAL) ok = parse_normal_section(L, &ip, &pf);
		else ok = parse_ed_section(L, &ip, &pf);

		if (!ok) { free_patchfile(&pf); goto fail; }
		if (!push_section(&arr, &n, &cap, &pf)) { free_patchfile(&pf); goto fail; }

		if (fmt == FMT_NORMAL || fmt == FMT_ED) break; /* whole stream is one section */
	}

	if (!n) return -1;
	*out = arr; *nout = n;
	return 0;
fail:
	{ size_t i; for (i = 0; i < n; i++) free_patchfile(&arr[i]); }
	free(arr);
	return -1;
}

/* ==== filename determination (context/unified, no operand given) ========= */

withtok(heap_allocated)
static char *strip_components(const char *name, long strip)
{
	const char *p = name;
	long k;
	if (!name) return 0;
	for (k = 0; k < strip; k++) {
		const char *slash = strchr(p, '/');
		if (!slash) break;
		p = slash + 1;
	}
	return strdup(p);
}

withtok(heap_allocated)
static char *pick_target_name(const char *old_name, const char *new_name, long strip)
{
	char *o = strip_components(old_name, strip);
	char *n = strip_components(new_name, strip);

	if (o && access(o, F_OK) == 0) { free(n); return o; }
	if (n) { free(o); return n; }
	return o;
}

/* ==== hunk matching / application ========================================= */

static int side_matches(const struct hunk *h, int want_old, const struct linebuf *target, size_t pos, int loose)
{
	size_t i, t = pos;
	for (i = 0; i < h->n; i++) {
		enum hop_kind k = h->v[i].kind;
		if (want_old ? (k == HOP_ADD) : (k == HOP_DEL)) continue;
		if (t >= target->n) return 0;
		if (loose) { if (!ws_loose_equal(h->v[i].p.text, target->v[t].text)) return 0; }
		else { if (h->v[i].p.len != target->v[t].len || memcmp(h->v[i].p.text, target->v[t].text, h->v[i].p.len) != 0) return 0; }
		t++;
	}
	return 1;
}

static long find_match(const struct hunk *h, int want_old, const struct linebuf *target, long expected, size_t lo_bound, int loose)
{
	long n = (long)target->n, lo = (long)lo_bound, d;

	if (expected < lo) expected = lo;
	if (expected <= n && side_matches(h, want_old, target, (size_t)expected, loose)) return expected;

	for (d = 1; ; d++) {
		long fwd = expected + d, bwd = expected - d;
		int any = 0;
		if (fwd <= n) { any = 1; if (side_matches(h, want_old, target, (size_t)fwd, loose)) return fwd; }
		if (bwd >= lo) { any = 1; if (side_matches(h, want_old, target, (size_t)bwd, loose)) return bwd; }
		if (!any) break;
	}
	return -1;
}

static int emit_hunk(const struct hunk *h, const char *define, const struct linebuf *target, size_t pos, struct linebuf *outbuf)
{
	size_t i = 0, t = pos;

	while (i < h->n) {
		if (h->v[i].kind == HOP_CTX) {
			if (t >= target->n) return 0;
			if (!lb_push(outbuf, target->v[t].text, target->v[t].len, target->v[t].has_nl)) return 0;
			i++; t++;
			continue;
		}
		{
			size_t del_start = i, del_end, add_start, add_end, tdel_start = t, k;
			int have_del, have_add;

			while (i < h->n && h->v[i].kind == HOP_DEL) { i++; t++; }
			del_end = i;
			add_start = i;
			while (i < h->n && h->v[i].kind == HOP_ADD) i++;
			add_end = i;
			have_del = del_end > del_start;
			have_add = add_end > add_start;

			if (!define) {
				for (k = add_start; k < add_end; k++)
					if (!lb_push(outbuf, h->v[k].p.text, h->v[k].p.len, h->v[k].p.has_nl)) return 0;
				continue;
			}

			if (have_del && !lb_push_fmt(outbuf, "#ifndef %s", define)) return 0;
			if (have_del) {
				for (k = 0; k < del_end - del_start; k++) {
					const struct pline *pl = &target->v[tdel_start + k];
					if (!lb_push(outbuf, pl->text, pl->len, pl->has_nl)) return 0;
				}
			}
			if (have_del && have_add && !lb_push_str(outbuf, "#else")) return 0;
			if (have_add) {
				if (!have_del && !lb_push_fmt(outbuf, "#ifdef %s", define)) return 0;
				for (k = add_start; k < add_end; k++)
					if (!lb_push(outbuf, h->v[k].p.text, h->v[k].p.len, h->v[k].p.has_nl)) return 0;
			}
			if ((have_del || have_add) && !lb_push_str(outbuf, "#endif")) return 0;
		}
	}
	return 1;
}

static int push_reject(struct hunk ***rejects, size_t *n, size_t *cap, struct hunk *h)
{
	if (*n >= *cap) {
		size_t newcap;
		struct hunk **g;
		if (!__util_array_capacity(*cap, *n, 1, 8, sizeof **rejects, &newcap)) return 0;
		g = __util_reallocarray(*rejects, newcap, sizeof **rejects);
		if (!g) return 0;
		*rejects = g; *cap = newcap;
	}
	(*rejects)[(*n)++] = h;
	return 1;
}

/* Apply every hunk of one normal/context/unified section, in order,
 * building the result into `outbuf` by copying the untouched source
 * spans between matched hunks (see this file's header comment for why
 * matching is bounded below by `src`, never revisiting content an
 * earlier hunk already consumed). */
static int apply_section(struct patchfile *pf, struct linebuf *target, const char *define, int loose,
                          int ignore_applied, struct linebuf *outbuf, struct hunk ***rejects, size_t *nrejects, size_t *rejcap)
{
	size_t hi, src = 0;
	long offset = 0;

	for (hi = 0; hi < pf->nhunks; hi++) {
		struct hunk *h = &pf->hunks[hi];
		/* old_start is 1-based and, for a non-empty old-side range,
		 * names its first line -- 0-based split point old_start-1.
		 * A pure insertion (old_count==0) instead uses old_start as
		 * "insert after old line old_start" (e.g. normal diff's
		 * "2a3", or unified's "@@ -2,0 +3,2 @@"), whose 0-based split
		 * point is old_start itself, one past the "-1" case above. */
		long expected = (h->old_count == 0 ? h->old_start : h->old_start - 1) + offset;
		long pos = find_match(h, 1, target, expected, src, loose);

		if (pos < 0) {
			long npos = find_match(h, 0, target, expected, src, loose);
			if (npos >= 0 && ignore_applied) {
				size_t k, end = (size_t)npos + (size_t)h->new_count;
				if (end > target->n) end = target->n;
				for (k = src; k < end; k++)
					if (!lb_push(outbuf, target->v[k].text, target->v[k].len, target->v[k].has_nl)) return -1;
				src = end;
				offset += h->new_count - h->old_count;
				continue;
			}
			if (!push_reject(rejects, nrejects, rejcap, h)) return -1;
			continue;
		}

		{
			size_t k;
			for (k = src; k < (size_t)pos; k++)
				if (!lb_push(outbuf, target->v[k].text, target->v[k].len, target->v[k].has_nl)) return -1;
			if (!emit_hunk(h, define, target, (size_t)pos, outbuf)) return -1;
			src = (size_t)pos + (size_t)h->old_count;
			offset += h->new_count - h->old_count;
		}
	}

	{
		size_t k;
		for (k = src; k < target->n; k++)
			if (!lb_push(outbuf, target->v[k].text, target->v[k].len, target->v[k].has_nl)) return -1;
	}
	return 0;
}

/* Ed scripts splice directly into a mutable working copy of the target
 * -- see this file's header comment for why no matching is needed. */
static int apply_ed_section(struct patchfile *pf, struct linebuf *target, struct linebuf *outbuf)
{
	struct linebuf work;
	size_t i;

	memset(&work, 0, sizeof work);
	for (i = 0; i < target->n; i++)
		if (!lb_push(&work, target->v[i].text, target->v[i].len, target->v[i].has_nl)) { free_linebuf(&work); return 0; }

	for (i = 0; i < pf->neds; i++) {
		struct edcmd *e = &pf->eds[i];

		if (e->op == 'a') {
			size_t ins = (e->a1 > 0) ? (size_t)e->a1 : 0;
			if (ins > work.n) ins = work.n;
			if (!linebuf_insert_block(&work, ins, &e->text)) { free_linebuf(&work); return 0; }
			continue;
		}

		{
			size_t lo = (e->a1 > 0) ? (size_t)(e->a1 - 1) : 0;
			size_t hi = (size_t)e->a2;
			if (hi > work.n) hi = work.n;
			if (lo > hi) lo = hi;
			linebuf_remove_range(&work, lo, hi);
			if (e->op == 'c' && !linebuf_insert_block(&work, lo, &e->text)) { free_linebuf(&work); return 0; }
		}
	}

	for (i = 0; i < work.n; i++)
		if (!lb_push(outbuf, work.v[i].text, work.v[i].len, work.v[i].has_nl)) { free_linebuf(&work); return 0; }
	free_linebuf(&work);
	return 1;
}

static int write_rejects(const char *rejpath, struct hunk **rejects, size_t n)
{
	FILE *f;
	size_t i;

	if (!n) return 0;
	f = fopen(rejpath, "wb");
	if (!f) return -1;
	for (i = 0; i < n; i++) {
		struct hunk *h = rejects[i];
		size_t k;
		if (fprintf(f, "@@ -%ld,%ld +%ld,%ld @@\n", h->old_start, h->old_count, h->new_start, h->new_count) < 0) { fclose(f); return -1; }
		for (k = 0; k < h->n; k++) {
			char pfx = h->v[k].kind == HOP_CTX ? ' ' : (h->v[k].kind == HOP_DEL ? '-' : '+');
			if (fprintf(f, "%c%s\n", pfx, h->v[k].p.text) < 0) { fclose(f); return -1; }
		}
	}
	return fclose(f) == 0 ? 0 : -1;
}

/* ==== option parsing and top-level driver ================================= */

struct patch_opts {
	int b, l, N, R;
	enum diff_format forced_fmt;
	const char *d, *D, *i, *o, *r;
	long p;
};

int __util_patch_main(int argc, char **argv)
{
	struct patch_opts o;
	int argi = 1;
	const char *operand = 0;
	FILE *pf_in;
	struct linebuf patch_lines;
	struct patchfile *sections = 0;
	size_t nsections = 0;
	int exit_status = 0;
	FILE *outfile_f = 0;
	size_t si;

	memset(&o, 0, sizeof o);
	o.forced_fmt = FMT_UNKNOWN;

	for (; argi < argc; argi++) {
		char *a = argv[argi];
		if (a[0] != '-' || a[1] == 0) break;
		if (!strcmp(a, "--")) { argi++; break; }
		if (!strcmp(a, "-b")) { o.b = 1; continue; }
		if (!strcmp(a, "-l")) { o.l = 1; continue; }
		if (!strcmp(a, "-N")) { o.N = 1; continue; }
		if (!strcmp(a, "-R")) { o.R = 1; continue; }
		if (!strcmp(a, "-c") || !strcmp(a, "-e") || !strcmp(a, "-n") || !strcmp(a, "-u")) {
			enum diff_format f = a[1] == 'c' ? FMT_CONTEXT : a[1] == 'e' ? FMT_ED : a[1] == 'n' ? FMT_NORMAL : FMT_UNIFIED;
			if (o.forced_fmt != FMT_UNKNOWN && o.forced_fmt != f) {
				__util_diagf("patch: only one of -c/-e/-n/-u may be given\n");
				return 2;
			}
			o.forced_fmt = f;
			continue;
		}
		if (!strcmp(a, "-d")) { if (argi + 1 >= argc) { __util_diagf("patch: -d: option requires an argument\n"); return 2; } o.d = argv[++argi]; continue; }
		if (!strcmp(a, "-D")) { if (argi + 1 >= argc) { __util_diagf("patch: -D: option requires an argument\n"); return 2; } o.D = argv[++argi]; continue; }
		if (!strcmp(a, "-i")) { if (argi + 1 >= argc) { __util_diagf("patch: -i: option requires an argument\n"); return 2; } o.i = argv[++argi]; continue; }
		if (!strcmp(a, "-o")) { if (argi + 1 >= argc) { __util_diagf("patch: -o: option requires an argument\n"); return 2; } o.o = argv[++argi]; continue; }
		if (!strcmp(a, "-r")) { if (argi + 1 >= argc) { __util_diagf("patch: -r: option requires an argument\n"); return 2; } o.r = argv[++argi]; continue; }
		if (!strcmp(a, "-p")) {
			char *end;
			if (argi + 1 >= argc) { __util_diagf("patch: -p: option requires an argument\n"); return 2; }
			o.p = strtol(argv[++argi], &end, 10);
			if (*end || o.p < 0) { __util_diagf("patch: -p: invalid strip count\n"); return 2; }
			continue;
		}
		__util_diagf("patch: %s: invalid option\n", a);
		return 2;
	}

	if (argi < argc) operand = argv[argi++];
	if (argi < argc) { __util_diagf("patch: too many operands\n"); return 2; }

	if (o.i) {
		pf_in = fopen(o.i, "rb");
		if (!pf_in) { __util_diagf("patch: %s: %s\n", o.i, strerror(errno)); return 2; }
	} else {
		pf_in = stdin;
	}
	memset(&patch_lines, 0, sizeof patch_lines);
	{
		int rc = read_all_lines(pf_in, &patch_lines);
		if (pf_in != stdin) fclose(pf_in);
		if (rc < 0) {
			__util_diagf("patch: error reading patch input\n");
			free_linebuf(&patch_lines);
			return 2;
		}
	}

	if (o.d && chdir(o.d) != 0) {
		__util_diagf("patch: %s: %s\n", o.d, strerror(errno));
		free_linebuf(&patch_lines);
		return 2;
	}

	if (parse_patch_stream(&patch_lines, o.forced_fmt, &sections, &nsections) != 0) {
		__util_diagf("patch: unrecognized or malformed patch input\n");
		free_linebuf(&patch_lines);
		return 2;
	}
	free_linebuf(&patch_lines);

	if (o.o) {
		outfile_f = fopen(o.o, "wb");
		if (!outfile_f) {
			__util_diagf("patch: %s: %s\n", o.o, strerror(errno));
			for (si = 0; si < nsections; si++) free_patchfile(&sections[si]);
			free(sections);
			return 2;
		}
	}

	for (si = 0; si < nsections && exit_status < 2; si++) {
		struct patchfile *pf = &sections[si];
		char *path;
		struct linebuf target, outbuf;
		int have_target;

		if ((pf->fmt == FMT_NORMAL || pf->fmt == FMT_ED) && !operand) {
			__util_diagf("patch: %s diff format carries no filename of its own -- "
				"a file operand is required\n", pf->fmt == FMT_NORMAL ? "normal" : "ed");
			exit_status = 2;
			break;
		}
		if (o.R && pf->fmt == FMT_ED) { __util_diagf("patch: -R cannot be used with ed scripts\n"); exit_status = 2; break; }
		if (o.D && pf->fmt == FMT_ED) { __util_diagf("patch: -D cannot be used with ed scripts\n"); exit_status = 2; break; }

		path = operand ? strdup(operand) : pick_target_name(pf->old_name, pf->new_name, o.p);
		if (!path) { __util_diagf("patch: out of memory\n"); exit_status = 2; break; }

		memset(&target, 0, sizeof target);
		{
			FILE *tf = fopen(path, "rb");
			if (tf) {
				have_target = 1;
				if (read_all_lines(tf, &target) < 0) {
					__util_diagf("patch: %s: error reading file\n", path);
					fclose(tf); free_linebuf(&target); free(path); exit_status = 2; break;
				}
				fclose(tf);
			} else {
				have_target = 0;
			}
		}

		memset(&outbuf, 0, sizeof outbuf);

		if (pf->fmt == FMT_ED) {
			if (!apply_ed_section(pf, &target, &outbuf)) {
				__util_diagf("patch: %s: out of memory applying patch\n", path);
				exit_status = 2;
				free_linebuf(&target); free_linebuf(&outbuf); free(path);
				break;
			}
		} else {
			struct hunk **rejects = 0;
			size_t nrej = 0, rejcap = 0;

			if (o.R) { size_t hi; for (hi = 0; hi < pf->nhunks; hi++) reverse_hunk(&pf->hunks[hi]); }

			if (apply_section(pf, &target, o.D, o.l, o.N, &outbuf, &rejects, &nrej, &rejcap) != 0) {
				__util_diagf("patch: %s: out of memory applying patch\n", path);
				exit_status = 2;
				free(rejects); free_linebuf(&target); free_linebuf(&outbuf); free(path);
				break;
			}
			if (nrej) {
				char rejpath[4096];
				if (o.r) snprintf(rejpath, sizeof rejpath, "%s", o.r);
				else snprintf(rejpath, sizeof rejpath, "%s.rej", path);
				if (write_rejects(rejpath, rejects, nrej) != 0) {
					__util_diagf("patch: %s: cannot write reject file\n", rejpath);
					exit_status = 2;
				} else if (exit_status < 1) {
					exit_status = 1;
				}
			}
			free(rejects);
		}

		if (o.b && have_target && !o.o) {
			char bpath[4096];
			snprintf(bpath, sizeof bpath, "%s.orig", path);
			if (write_linebuf(bpath, &target) != 0) {
				__util_diagf("patch: %s: cannot write backup file\n", bpath);
				exit_status = 2;
			}
		}

		if (o.o) {
			if (write_linebuf_stream(outfile_f, &outbuf) != 0) {
				__util_diagf("patch: %s: write error\n", o.o);
				exit_status = 2;
			}
		} else if (write_linebuf(path, &outbuf) != 0) {
			__util_diagf("patch: %s: cannot write patched file\n", path);
			exit_status = 2;
		}

		free_linebuf(&target);
		free_linebuf(&outbuf);
		free(path);
	}

	if (outfile_f && fclose(outfile_f) != 0 && exit_status < 2) exit_status = 2;

	for (si = 0; si < nsections; si++) free_patchfile(&sections[si]);
	free(sections);
	return exit_status;
}
