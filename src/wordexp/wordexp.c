/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wordexp(): the genuine, shell-free subset of XCU Word Expansions --
 * see include/wordexp.h's header comment for which pieces those are
 * and, in particular, the reasoning for what this returns when it
 * meets a construct (command substitution / arithmetic expansion)
 * that genuinely needs a POSIX shell this platform does not have.
 *
 * One left-to-right scan of `words` does almost everything at once,
 * because the pieces are not actually separable: whether a character
 * is "quoted" has to be known before deciding whether it can start a
 * parameter expansion, end a field, or become a live glob
 * metacharacter, and that quote state can only be tracked by walking
 * the string in order. Per *raw* field (split on unquoted IFS
 * whitespace -- see include/wordexp.h on why that part of field
 * splitting is in scope), the scan builds two parallel arrays of the
 * same length: the literal bytes the field expands to, and a same-
 * length "was this byte quoted/escaped" flag.  A byte with that flag
 * clear is live: it came from outside any quotes, unescaped, so if it
 * is '*', '?' or '[' it is a real glob metacharacter, and if the
 * field contains any such byte at all, the field is handed to glob()
 * for pathname expansion; a field with none is used exactly as
 * scanned (that is quote removal). Parameter expansion ($VAR/${VAR})
 * substitutes environ text as *live* bytes (matching real shells: an
 * unquoted $VAR's value is itself eligible for pathname expansion);
 * tilde expansion substitutes the home directory as *quoted* bytes
 * (matching real shells: the result of ~ expansion is never re-glob-
 * scanned).
 */
#include <wordexp.h>
#include <glob.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "libc.h"

/* ---- growable byte buffer: the field being built, plus a parallel
 * "quoted/escaped" flag per byte --------------------------------------- */
struct fbuf {
	char *data;
	unsigned char *lit;
	size_t n, cap;
};

static int fbuf_push(struct fbuf *b, char c, int literal)
{
	if (b->n == b->cap) {
		size_t nc = b->cap ? b->cap * 2 : 64;
		char *nd = __malloc(nc);
		unsigned char *nl = __malloc(nc);
		if (!nd || !nl) { __free(nd); __free(nl); return -1; }
		if (b->data) { memcpy(nd, b->data, b->n); memcpy(nl, b->lit, b->n); }
		__free(b->data);
		__free(b->lit);
		b->data = nd;
		b->lit = nl;
		b->cap = nc;
	}
	b->data[b->n] = c;
	b->lit[b->n] = (unsigned char)literal;
	b->n++;
	return 0;
}

static void fbuf_free(struct fbuf *b)
{
	__free(b->data);
	__free(b->lit);
	b->data = 0;
	b->lit = 0;
	b->n = b->cap = 0;
}

static int fbuf_push_str(struct fbuf *b, const char *s, int literal)
{
	for (; *s; s++)
		if (fbuf_push(b, *s, literal)) return -1;
	return 0;
}

/* ---- growable word-pointer vector, same shape as src/glob/glob.c's
 * private one (not shared: neither module is meant to depend on the
 * other's internals) ----------------------------------------------------- */
struct pv {
	char **v;
	size_t n, cap;
};

static int pv_push(struct pv *p, char *s)
{
	if (!s) return -1;
	if (p->n == p->cap) {
		size_t nc = p->cap ? p->cap * 2 : 16;
		char **nv = __malloc(nc * sizeof *nv);
		if (!nv) { __free(s); return -1; }
		if (p->v) memcpy(nv, p->v, p->n * sizeof *nv);
		__free(p->v);
		p->v = nv;
		p->cap = nc;
	}
	p->v[p->n++] = s;
	return 0;
}

static void pv_free_all(struct pv *p)
{
	size_t i;
	for (i = 0; i < p->n; i++) __free(p->v[i]);
	__free(p->v);
	p->v = 0;
	p->n = p->cap = 0;
}

/* Frees only the entries [from, n) (words *this* call itself added --
 * used when [0, from) still belongs to a WRDE_APPEND caller's
 * untouched pwordexp), plus the array wrapper itself, which is always
 * this call's own allocation regardless of from. */
static void pv_free_from(struct pv *p, size_t from)
{
	size_t i;
	for (i = from; i < p->n; i++) __free(p->v[i]);
	__free(p->v);
	p->v = 0;
	p->n = p->cap = 0;
}

static char *xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = __malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

static int is_ifs(char c) { return c == ' ' || c == '\t' || c == '\n'; }
static int is_namestart(char c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_namechar(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* Reads a $NAME or ${NAME} parameter expansion starting at *pp (which
 * points at the '$'). Advances *pp past it. Appends the value (as live,
 * unquoted bytes) to b. Returns 0, or a WRDE_* error code. */
static int expand_param(const char **pp, struct fbuf *b, int flags)
{
	const char *p = *pp + 1;
	const char *start;
	char name[256];
	size_t len;
	const char *val;
	int braced = 0;

	if (*p == '{') {
		braced = 1;
		p++;
	}
	start = p;
	if (!is_namestart(*p)) {
		/* "$" not followed by a name: not a parameter expansion this
		 * implementation supports (see include/wordexp.h -- only bare
		 * $VAR/${VAR}, not the special parameters). Treat '$' as a
		 * literal character rather than fail the whole expansion. */
		*pp = *pp + 1;
		return fbuf_push(b, '$', 0) ? WRDE_NOSPACE : 0;
	}
	while (is_namechar(*p)) p++;
	len = (size_t)(p - start);
	if (len >= sizeof name) return WRDE_SYNTAX;
	memcpy(name, start, len);
	name[len] = 0;
	if (braced) {
		if (*p != '}') return WRDE_SYNTAX;
		p++;
	}
	*pp = p;

	val = getenv(name);
	if (!val) {
		if (flags & WRDE_UNDEF) return WRDE_BADVAL;
		return 0;
	}
	return fbuf_push_str(b, val, 0) ? WRDE_NOSPACE : 0;
}

/* Reads ~ or ~user starting at *pp (pointing at '~'), only valid when
 * called at the very start of a field. Advances *pp past it. Appends
 * the home directory (as quoted/literal bytes -- tilde-expansion
 * results are not re-scanned for pathname expansion) to b. If the
 * user is unknown, '~'/"~user" is left unexpanded, matching every
 * shell's fallback. */
static int expand_tilde(const char **pp, struct fbuf *b)
{
	const char *p = *pp + 1;
	const char *start = p;
	char name[256];
	size_t len;
	const char *home = 0;

	while (*p && *p != '/' && *p != ' ' && *p != '\t' && *p != '\n' &&
	       *p != '"' && *p != '\'')
		p++;
	len = (size_t)(p - start);

	if (len == 0) {
		home = getenv("HOME");
	} else if (len < sizeof name) {
		struct passwd *pw;
		memcpy(name, start, len);
		name[len] = 0;
		pw = getpwnam(name);
		if (pw) home = pw->pw_dir;
	}

	if (!home) {
		/* Unknown user, or bare ~ with no $HOME: leave the '~' itself
		 * literal and do not consume the rest of the candidate name --
		 * a later '$' etc. in it still gets its own expansion. */
		*pp = *pp + 1;
		return fbuf_push(b, '~', 1) ? WRDE_NOSPACE : 0;
	}
	*pp = p;
	return fbuf_push_str(b, home, 1) ? WRDE_NOSPACE : 0;
}

/* Turns one already-expanded field (b->data[0..n), with b->lit[i] true
 * for bytes that must stay literal) into one or more output words,
 * pushing them onto out. Live '*'/'?'/'[' bytes trigger glob(); no live
 * metacharacters means the field is used exactly as scanned. */
static int emit_field(struct fbuf *b, struct pv *out)
{
	size_t i;
	int has_meta = 0;
	char *plain;
	struct fbuf pat;

	for (i = 0; i < b->n; i++)
		if (!b->lit[i] && (b->data[i] == '*' || b->data[i] == '?' || b->data[i] == '['))
			{ has_meta = 1; break; }

	plain = __malloc(b->n + 1);
	if (!plain) return WRDE_NOSPACE;
	memcpy(plain, b->data, b->n);
	plain[b->n] = 0;

	if (!has_meta) return pv_push(out, plain) ? WRDE_NOSPACE : 0;

	pat.data = 0; pat.lit = 0; pat.n = pat.cap = 0;
	for (i = 0; i < b->n; i++) {
		char c = b->data[i];
		if (b->lit[i] && (c == '*' || c == '?' || c == '[' || c == '\\')) {
			if (fbuf_push(&pat, '\\', 0)) goto nospace;
		}
		if (fbuf_push(&pat, c, 0)) goto nospace;
	}
	if (fbuf_push(&pat, 0, 0)) goto nospace;

	{
		glob_t g;
		int rc = glob(pat.data, 0, 0, &g);
		fbuf_free(&pat);
		if (rc == 0) {
			size_t j;
			for (j = 0; j < g.gl_pathc; j++) {
				char *w = xstrdup(g.gl_pathv[j]);
				if (!w || pv_push(out, w)) { globfree(&g); __free(plain); return WRDE_NOSPACE; }
			}
			globfree(&g);
			__free(plain);
			return 0;
		}
		if (rc == GLOB_NOMATCH) return pv_push(out, plain) ? WRDE_NOSPACE : 0;
		__free(plain);
		return WRDE_NOSPACE; /* GLOB_ABORTED can't happen: no errfunc, no GLOB_ERR */
	}
nospace:
	fbuf_free(&pat);
	__free(plain);
	return WRDE_NOSPACE;
}

int wordexp(const char *words, wordexp_t *pwordexp, int flags)
{
	struct pv out;
	struct fbuf field;
	const char *p = words;
	enum { Q_NONE, Q_SINGLE, Q_DOUBLE } q = Q_NONE;
	int active = 0;	/* current field has at least one byte, or was opened by a quote */
	int rc;
	size_t base = 0;

	if (flags & WRDE_REUSE) wordfree(pwordexp);

	out.v = 0; out.n = out.cap = 0;
	if (flags & WRDE_APPEND) {
		/* pwordexp->we_wordv is deliberately NOT freed here, even
		 * though its pointers are copied into out.v below: RETURN
		 * VALUE says a non-WRDE_NOSPACE error leaves these fields
		 * "unmodified", which has to mean the memory they point to
		 * stays valid, not just that the pointer variable itself is
		 * untouched. So the old array is only actually freed once a
		 * path below commits to replacing it (success, or
		 * WRDE_NOSPACE, which is explicitly allowed to update these
		 * fields) -- see the "other errors" branch of fail: below. */
		out.n = out.cap = pwordexp->we_wordc;
		if (out.n) {
			out.v = __malloc(out.n * sizeof *out.v);
			if (!out.v) { errno = ENOMEM; return WRDE_NOSPACE; }
			memcpy(out.v, pwordexp->we_wordv + pwordexp->we_offs, out.n * sizeof *out.v);
		}
		base = out.n;
	}

	field.data = 0; field.lit = 0; field.n = field.cap = 0;
#define FLUSH() do { \
		if (active) { \
			rc = emit_field(&field, &out); \
			fbuf_free(&field); \
			active = 0; \
			if (rc) goto fail; \
		} \
	} while (0)

	while (*p) {
		char c = *p;
		if (q == Q_NONE) {
			if (is_ifs(c)) { FLUSH(); p++; continue; }
			if (c == '\'') { q = Q_SINGLE; active = 1; p++; continue; }
			if (c == '"') { q = Q_DOUBLE; active = 1; p++; continue; }
			if (c == '\\') {
				if (!p[1]) { rc = WRDE_SYNTAX; goto fail; }
				if (fbuf_push(&field, p[1], 1)) { rc = WRDE_NOSPACE; goto fail; }
				active = 1;
				p += 2;
				continue;
			}
			if (c == '$' && p[1] == '(') { rc = WRDE_CMDSUB; goto fail; }
			if (c == '`') { rc = WRDE_CMDSUB; goto fail; }
			if (c == '$') {
				active = 1;
				rc = expand_param(&p, &field, flags);
				if (rc) goto fail;
				continue;
			}
			if (c == '~' && !active) {
				active = 1;
				rc = expand_tilde(&p, &field);
				if (rc) goto fail;
				continue;
			}
			if (c == '|' || c == '&' || c == ';' || c == '<' || c == '>' ||
			    c == '(' || c == ')' || c == '{' || c == '}') {
				rc = WRDE_BADCHAR;
				goto fail;
			}
			if (fbuf_push(&field, c, 0)) { rc = WRDE_NOSPACE; goto fail; }
			active = 1;
			p++;
		} else if (q == Q_SINGLE) {
			if (c == '\'') { q = Q_NONE; p++; continue; }
			if (fbuf_push(&field, c, 1)) { rc = WRDE_NOSPACE; goto fail; }
			p++;
		} else { /* Q_DOUBLE */
			if (c == '"') { q = Q_NONE; p++; continue; }
			if (c == '\\' && p[1] && strchr("\"\\$`\n", p[1])) {
				if (fbuf_push(&field, p[1], 1)) { rc = WRDE_NOSPACE; goto fail; }
				p += 2;
				continue;
			}
			if (c == '$' && p[1] == '(') { rc = WRDE_CMDSUB; goto fail; }
			if (c == '`') { rc = WRDE_CMDSUB; goto fail; }
			if (c == '$') {
				rc = expand_param(&p, &field, flags);
				if (rc) goto fail;
				continue;
			}
			if (fbuf_push(&field, c, 1)) { rc = WRDE_NOSPACE; goto fail; }
			p++;
		}
	}
	if (q != Q_NONE) { rc = WRDE_SYNTAX; goto fail; }
	FLUSH();
#undef FLUSH

	{
		size_t offs = (flags & WRDE_DOOFFS) ? pwordexp->we_offs : 0;
		size_t i, total = offs + out.n + 1;
		char **v = __malloc(total * sizeof *v);
		if (!v) { rc = WRDE_NOSPACE; goto fail; }
		for (i = 0; i < offs; i++) v[i] = 0;
		for (i = 0; i < out.n; i++) v[offs + i] = out.v[i];
		v[offs + out.n] = 0;
		__free(out.v);
		if (flags & WRDE_APPEND) __free(pwordexp->we_wordv);
		pwordexp->we_wordv = v;
		pwordexp->we_wordc = out.n;
		if (!(flags & WRDE_DOOFFS) && !(flags & WRDE_APPEND)) pwordexp->we_offs = offs;
	}
	return 0;

fail:
	fbuf_free(&field);
	if (rc == WRDE_NOSPACE) {
		/* RETURN VALUE: on WRDE_NOSPACE, we_wordc/we_wordv are updated
		 * to reflect the words successfully expanded so far. */
		size_t offs = (flags & WRDE_DOOFFS) ? pwordexp->we_offs : 0;
		size_t i, total = offs + out.n + 1;
		char **v = __malloc(total * sizeof *v);
		if (v) {
			for (i = 0; i < offs; i++) v[i] = 0;
			for (i = 0; i < out.n; i++) v[offs + i] = out.v[i];
			v[offs + out.n] = 0;
			__free(out.v);
			if (flags & WRDE_APPEND) __free(pwordexp->we_wordv);
			pwordexp->we_wordv = v;
			pwordexp->we_wordc = out.n;
		} else {
			/* Could not even allocate room to report partial
			 * success: fall back to leaving pwordexp exactly as it
			 * was (same reasoning as the "other errors" branch
			 * below), and free only what this call itself added. */
			pv_free_from(&out, base);
		}
	} else {
		/* RETURN VALUE: "on other errors ... these fields remain
		 * unmodified" -- pwordexp->we_wordv (if WRDE_APPEND) was
		 * deliberately left untouched above, so free only the words
		 * *this* call added (out.v[base..n)), not the carried-over
		 * ones out.v[0..base) that still belong to it, plus the out.v
		 * array wrapper itself (a separate allocation from
		 * pwordexp->we_wordv, safe to free either way). */
		pv_free_from(&out, base);
	}
	if (rc == WRDE_NOSPACE) errno = ENOMEM;
	return rc;
}

void wordfree(wordexp_t *pwordexp)
{
	size_t i, offs;

	if (!pwordexp || !pwordexp->we_wordv) return;
	offs = pwordexp->we_offs;
	for (i = 0; i < pwordexp->we_wordc; i++) __free(pwordexp->we_wordv[offs + i]);
	__free(pwordexp->we_wordv);
	pwordexp->we_wordv = 0;
	pwordexp->we_wordc = 0;
}
