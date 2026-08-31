/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * glob(): pattern matching against ntlibc's own working opendir/
 * readdir/stat layer (src/dirent/, src/unistd/stat.c), one '/'-
 * separated pattern component at a time, using fnmatch() (src/fnmatch/
 * fnmatch.c) to test each component against directory entries.  A
 * component with no unescaped '*', '?' or '[' is a literal: it is
 * unescaped and looked up directly with stat(), the same as the shell
 * never bothers to opendir() a directory just to find one name it
 * already knows.
 *
 * Hidden files: a directory entry whose name starts with '.' is only
 * matched when the pattern component itself starts with a literal '.',
 * the same convention every historical shell glob (and glibc's glob())
 * uses; "." and ".." are never matched, even then. Neither is required
 * by POSIX base glob(), which says nothing about dot-files at all, but
 * omitting it would make "*.txt" match a stray ".txt" hidden file in
 * a way no glob(1) call anyone has ever used actually behaves, so it
 * is implemented as the reasonable default a real caller expects.
 *
 * Tilde expansion is NOT here -- see include/glob.h's header comment.
 */
#include <glob.h>
#include <fnmatch.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "libc.h"

struct pv {
	char **v;
	size_t n, cap;
};

static char *xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = __malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

/* -1 on allocation failure (frees s either way it owns it).
 *
 * p is required: p->n/p->cap are read unconditionally below, and every
 * real call site passes &out, the address of a caller's own local
 * struct pv, never NULL. s is deliberately NOT required -- the
 * `if (!s) return -1;` right below is real and load-bearing: every
 * caller passes an xstrdup()/unescape() result that can genuinely be
 * NULL on allocation failure (see xstrdup's/unescape's own `if (!p)
 * return 0;`), and this is how that OOM propagates as an ordinary
 * GLOB_NOSPACE rather than a crash. */
static int pv_push(struct pv *p, char *s) __attribute__((nonnull(1)));
static int pv_push(struct pv *p, char *s)
{
	if (!s) return -1;
	if (p->n == p->cap) {
		size_t nc;
		if (!__array_next_capacity(p->cap, p->n, 1, 16,
		    sizeof *p->v, &nc)) { __free(s); errno = ENOMEM; return -1; }
		char **nv = (char **)__malloc(nc * sizeof *nv);
		if (!nv) { __free(s); return -1; }
		if (p->v) memcpy((void *)nv, (const void *)p->v, p->n * sizeof *nv);
		__free((void *)p->v);
		p->v = nv;
		p->cap = nc;
	}
	/* p->v is non-NULL here either way, by the same shape as
	 * src/process/spawn_file_actions.c's own fa_push() comment on
	 * fa->__actions: either the growth branch above just set it, or
	 * p->n != p->cap already meant p->cap > 0, which by this function's
	 * own invariant only holds after an earlier successful growth
	 * already set p->v. Not expressible via nonnull on p itself (already
	 * marked above) -- a fact about one of p's FIELDS, not p, and a
	 * local proof the checker cannot follow through the conditional
	 * reassignment. */
	p->v[p->n++] = s;
	return 0;
}

/* p required: p->n is read unconditionally by the loop condition below,
 * and every real call site (do_glob()'s/glob()'s own &out) passes the
 * address of a local struct pv, never NULL. */
static void pv_free_from(struct pv *p, size_t from) __attribute__((nonnull(1)));
static void pv_free_from(struct pv *p, size_t from)
{
	size_t i;
	for (i = from; i < p->n; i++) __free(p->v[i]);
	__free((void *)p->v);
	p->v = 0;
	p->n = p->cap = 0;
}

/* p required: `*p` is read unconditionally at the loop's own entry, and
 * its one real call site (do_glob()) passes pat, itself required (see
 * do_glob()'s own comment below), never NULL. */
static const char *find_slash(const char *p, int flags) __attribute__((nonnull(1)));
static const char *find_slash(const char *p, int flags)
{
	for (; *p; p++) {
		if (!(flags & GLOB_NOESCAPE) && *p == '\\' && p[1]) { p++; continue; }
		if (*p == '/') return p;
	}
	return 0;
}

/* s required: subscripted unconditionally (`s[i]`) whenever len >= 1,
 * and its one real call site (do_glob()) passes pat, itself required,
 * never NULL. */
static int has_meta(const char *s, size_t len, int flags) __attribute__((nonnull(1)));
static int has_meta(const char *s, size_t len, int flags)
{
	size_t i;
	for (i = 0; i < len; i++) {
		if (!(flags & GLOB_NOESCAPE) && s[i] == '\\' && i + 1 < len) { i++; continue; }
		if (s[i] == '*' || s[i] == '?' || s[i] == '[') return 1;
	}
	return 0;
}

/* s required: subscripted unconditionally (`s[i]`) whenever len >= 1,
 * and its one real call site (do_glob()) passes pat, itself required,
 * never NULL. */
static char *unescape(const char *s, size_t len, int flags) __attribute__((nonnull(1)));
static char *unescape(const char *s, size_t len, int flags)
{
	char *buf = __malloc(len + 1);
	size_t i, j = 0;
	if (!buf) return 0;
	for (i = 0; i < len; i++) {
		if (!(flags & GLOB_NOESCAPE) && s[i] == '\\' && i + 1 < len) i++;
		buf[j++] = s[i];
	}
	buf[j] = 0;
	return buf;
}

/* a/b required: this is qsort()'s own comparator, called (src/stdlib/
 * qsort.c's sift()/qsort_r()) only as cmp(base + i*sz, base + j*sz, ...)
 * for i, j inside [0, n) of the real array being sorted -- an internal
 * heapsort never invents an out-of-range index or a NULL element
 * address, so both arguments are always the address of a real char* in
 * out.v, never NULL, whenever this is actually reached. */
static int cmpstrp(const void *a, const void *b) __attribute__((nonnull(1, 2)));
static int cmpstrp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Join prefix (preflen bytes, already ending in '/' unless empty) with
 * name (namelen bytes) into out, appending a trailing '/' if
 * want_slash.  Returns 0, or -1 if it would not fit in PATH_MAX. */
static int join(char *out, const char *prefix, size_t preflen,
                 const char *name, size_t namelen, int want_slash)
{
	size_t need = preflen + namelen + (want_slash ? 1 : 0);
	if (need + 1 > PATH_MAX) return -1;
	memcpy(out, prefix, preflen);
	memcpy(out + preflen, name, namelen);
	if (want_slash) out[preflen + namelen] = '/';
	out[need] = 0;
	return 0;
}

/* Returns 0 (call handled, possibly zero matches added), 1 (GLOB_ABORTED
 * -- stop the whole scan), or -1 (GLOB_NOSPACE).
 *
 * pat required: `*pat` is read unconditionally at entry (the leading-
 * slash skip loop). Every real call site agrees: glob()'s own initial
 * call passes pat, advanced from pattern (required there -- see glob()'s
 * own comment -- and never past its own NUL, so never NULL), and both
 * recursive calls pass rest, which is only ever reached from inside an
 * `if (rest)` guard, so rest is always a live pointer into pat's own
 * string, not the 0 find_slash()/the `rest = slash ? slash + 1 : 0;`
 * assignment can otherwise produce. prefix/out are not marked here: out
 * is already required by pv_push()/finish() at its own real dereference
 * sites, and prefix, though written through in several branches, is
 * only read back conditionally per-branch (never unconditionally at
 * entry the way pat is), so there is no single unconditional dereference
 * this attribute could describe for it. */
static int do_glob(char *prefix, size_t preflen, const char *pat, int flags,
                    int (*errfunc)(const char *, int), struct pv *out)
    __attribute__((nonnull(3)));
static int do_glob(char *prefix, size_t preflen, const char *pat, int flags,
                    int (*errfunc)(const char *, int), struct pv *out)
{
	const char *slash, *rest;
	size_t seglen;
	int meta, want_slash;
	char newprefix[PATH_MAX];

	while (*pat == '/') pat++;
	if (!*pat) {
		/* Pattern exhausted mid-recursion only happens after a caller
		 * already confirmed prefix names a directory (the literal and
		 * wildcard branches below both stat() before descending), so
		 * this is always a real, existing path. */
		char *m;
		if (preflen == 1 && prefix[0] == '/') m = xstrdup("/");
		else if (preflen) {
			/* A pattern ending in a slash yields a pathname ending in
			 * a slash -- ALWAYS, not only under GLOB_MARK.
			 *
			 * This branch is where a trailing-slash pattern lands, and
			 * it used to strip the slash: glob("subdir/", ...) returned
			 * "subdir" whatever the flags.  The generated pathname is
			 * supposed to be the one that matched, and the pattern's
			 * own trailing slash is part of it; GLOB_MARK is then
			 * simply redundant for this shape rather than the thing
			 * that enables it.  Confirmed against glibc, which returns
			 * "subdir/" for both glob("subdir/", 0) and
			 * glob("subdir/", GLOB_MARK) -- and "subdir" for
			 * glob("subdir", 0), where there is no slash to keep.
			 *
			 * (An earlier version of this fix made keeping the slash
			 * conditional on GLOB_MARK, which the fence's wording
			 * suggested.  Mutation-testing said the unconditional form
			 * was indistinguishable, and measuring glibc showed why:
			 * the unconditional form is the correct one.)
			 *
			 * Nothing needs to be re-stat()ed to know this is a
			 * directory: reaching here means a caller matched a
			 * component with a trailing slash and confirmed it with
			 * stat() before descending (see the literal and wildcard
			 * branches), so prefix already ends in '/' and names a
			 * directory. */
			m = xstrdup(prefix);
		} else if (preflen == 0) m = xstrdup(".");
		else {
			char tmp[PATH_MAX];
			memcpy(tmp, prefix, preflen - 1);
			tmp[preflen - 1] = 0;
			m = xstrdup(tmp);
		}
		return pv_push(out, m) ? -1 : 0;
	}

	slash = find_slash(pat, flags);
	seglen = slash ? (size_t)(slash - pat) : strlen(pat);
	rest = slash ? slash + 1 : 0;
	meta = has_meta(pat, seglen, flags);
	want_slash = rest != 0;

	if (!meta) {
		char *name = unescape(pat, seglen, flags);
		struct stat st;
		int isdir;

		if (!name) return -1;
		if (join(newprefix, prefix, preflen, name, strlen(name), want_slash)) {
			__free(name);
			return 0; /* too long to ever exist; not a match, not an error */
		}
		__free(name);

		if (rest) {
			if (stat(newprefix, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
			return do_glob(newprefix, strlen(newprefix), rest, flags, errfunc, out);
		}
		if (stat(newprefix, &st) != 0) return 0;
		isdir = S_ISDIR(st.st_mode);
		if ((flags & GLOB_MARK) && isdir) {
			size_t l = strlen(newprefix);
			if (l + 1 < PATH_MAX) { newprefix[l] = '/'; newprefix[l + 1] = 0; }
		}
		return pv_push(out, xstrdup(newprefix)) ? -1 : 0;
	} else {
		const char *dirpath = preflen ? prefix : ".";
		char *segbuf = __malloc(seglen + 1);
		int dot_ok;
		DIR *dp;
		struct dirent *d;
		int rc = 0;

		if (!segbuf) return -1;
		memcpy(segbuf, pat, seglen);
		segbuf[seglen] = 0;
		dot_ok = seglen > 0 && (pat[0] == '.' ||
			(!(flags & GLOB_NOESCAPE) && pat[0] == '\\' && seglen > 1 && pat[1] == '.'));

		dp = opendir(dirpath);
		if (!dp) {
			int e = errno;
			__free(segbuf);
			if ((errfunc && errfunc(dirpath, e)) || (flags & GLOB_ERR)) return 1;
			return 0;
		}

		errno = 0;
		while ((d = readdir(dp))) {
			size_t namelen;
			struct stat st;

			if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) continue;
			if (d->d_name[0] == '.' && !dot_ok) continue;
			if (fnmatch(segbuf, d->d_name, (flags & GLOB_NOESCAPE) ? FNM_NOESCAPE : 0) != 0)
				continue;

			namelen = strlen(d->d_name);
			if (join(newprefix, prefix, preflen, d->d_name, namelen, want_slash))
				continue;

			if (rest) {
				if (stat(newprefix, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
				rc = do_glob(newprefix, strlen(newprefix), rest, flags, errfunc, out);
				if (rc) break;
			} else {
				int isdir = 0;
				if ((flags & GLOB_MARK) && stat(newprefix, &st) == 0) isdir = S_ISDIR(st.st_mode);
				if (isdir) {
					size_t l = strlen(newprefix);
					if (l + 1 < PATH_MAX) { newprefix[l] = '/'; newprefix[l + 1] = 0; }
				}
				if (pv_push(out, xstrdup(newprefix))) { rc = -1; break; }
			}
			errno = 0;
		}
		if (!rc && errno) {
			int e = errno;
			if ((errfunc && errfunc(dirpath, e)) || (flags & GLOB_ERR)) rc = 1;
		}
		__free(segbuf);
		(void)closedir(dp);
		return rc;
	}
}

/* out/pglob both required: out->n is read unconditionally in the loop
 * bound just below, and pglob->gl_pathv/gl_pathc are written
 * unconditionally further down (both the success path and the nospace:
 * label's own reset) -- no branch of this function leaves pglob
 * untouched. Every real call site (glob(), three of them) passes &out
 * and its own pglob parameter straight through, and glob()'s own pglob
 * is itself required (see glob()'s comment) -- never NULL either way. */
static int finish(struct pv *out, int flags, glob_t *pglob) __attribute__((nonnull(1, 3)));
static int finish(struct pv *out, int flags, glob_t *pglob)
{
	size_t offs = (flags & GLOB_DOOFFS) ? pglob->gl_offs : 0;
	size_t i, total;
	char **v;

	/* gl_offs is caller-controlled.  Check both the element count and
	 * its conversion to bytes before either can wrap into a small
	 * allocation followed by an out-of-bounds NULL-fill loop. */
	if (out->n == (size_t)-1 || offs > (size_t)-1 - out->n - 1) goto nospace;
	total = offs + out->n + 1;
	if (total > (size_t)-1 / sizeof *v) goto nospace;
	v = (char **)__malloc(total * sizeof *v);
	if (!v) goto nospace;
	for (i = 0; i < offs; i++) v[i] = 0;
	for (i = 0; i < out->n; i++) v[offs + i] = out->v[i];
	v[offs + out->n] = 0;
	__free((void *)out->v);

	pglob->gl_pathv = v;
	pglob->gl_pathc = out->n;
	if (!(flags & GLOB_DOOFFS) && !(flags & GLOB_APPEND)) pglob->gl_offs = offs;
	return 0;

nospace:
	pv_free_from(out, 0);
	/* GLOB_APPEND has already released the old wrapper, and every entry
	 * it owned is now freed above.  An empty result also gives ordinary
	 * callers a safe globfree()-able state after this failure. */
	pglob->gl_pathv = 0;
	pglob->gl_pathc = 0;
	errno = ENOMEM;
	return GLOB_NOSPACE;
}

int glob(const char *pattern, int flags, int (*errfunc)(const char *, int), glob_t *pglob)
{
	struct pv out;
	char prefix[PATH_MAX];
	size_t preflen = 0, base;
	const char *pat = pattern;
	int rc;

	out.v = 0;
	out.n = out.cap = 0;

	if (flags & GLOB_APPEND) {
		out.n = out.cap = pglob->gl_pathc;
		if (out.n) {
			out.v = (char **)__malloc(out.n * sizeof *out.v);
			if (!out.v) { errno = ENOMEM; return GLOB_NOSPACE; }
			memcpy((void *)out.v, (const void *)(pglob->gl_pathv + pglob->gl_offs), out.n * sizeof *out.v);
		}
		__free((void *)pglob->gl_pathv);
	}
	base = out.n;

	if (*pat == '/') {
		prefix[0] = '/';
		preflen = 1;
		pat++;
		while (*pat == '/') pat++;
	}
	prefix[preflen] = 0;

	/* An EMPTY pattern names no pathname, so it matches nothing --
	 * glob.html RETURN VALUE, "[GLOB_NOMATCH] The pattern does not match
	 * any existing pathname, and GLOB_NOCHECK was not set".
	 *
	 * do_glob() cannot be asked this question.  Its pattern-exhausted
	 * branch assumes it was reached part-way through a recursion, after
	 * a caller had already confirmed the prefix names a directory (its
	 * own comment says so), and synthesises "." when the prefix is
	 * empty.  Reached with an empty pattern from HERE that assumption is
	 * false, and glob("", 0, ...) returned 0 with gl_pathv[0] == "." --
	 * a pathname the caller never asked about, handed back as a
	 * successful match.
	 *
	 * Guarded at the call rather than inside do_glob() because the
	 * assumption the branch makes is correct for every recursive entry;
	 * only the initial one can violate it.  Note "/" is NOT empty and
	 * must still work: pat has already advanced past the leading slash
	 * by this point, leaving preflen == 1 and an empty pat, which is the
	 * legitimate exhausted case naming the root.  So the test is on the
	 * caller's original pattern, not on pat. */
	rc = *pattern ? do_glob(prefix, preflen, pat, flags, errfunc, &out) : 0;

	if (rc == -1) {
		/* Frees everything in out, including any entries kept alive
		 * from a previous GLOB_APPEND call: those pointers were moved
		 * out of pglob->gl_pathv (already freed above) into out.v, so
		 * this is the only remaining owner of them. */
		pv_free_from(&out, 0);
		/* A GLOB_APPEND call already freed pglob's old gl_pathv above;
		 * leaving gl_pathc/gl_pathv pointing at that freed block would
		 * be a dangling pointer, so put pglob back in a safe, empty,
		 * globfree()-is-a-no-op state. A non-APPEND call never touched
		 * pglob at all, so it is left as the caller had it. */
		if (flags & GLOB_APPEND) { pglob->gl_pathc = 0; pglob->gl_pathv = 0; }
		errno = ENOMEM;
		return GLOB_NOSPACE;
	}
	if (rc == 1) {
		int frc = finish(&out, flags, pglob);
		return frc ? frc : GLOB_ABORTED;
	}

	if (out.n == base) {
		if (flags & GLOB_NOCHECK) {
			if (pv_push(&out, xstrdup(pattern))) {
				pv_free_from(&out, 0);
				if (flags & GLOB_APPEND) { pglob->gl_pathc = 0; pglob->gl_pathv = 0; }
				errno = ENOMEM;
				return GLOB_NOSPACE;
			}
		}
	} else if (!(flags & GLOB_NOSORT)) {
		/* Sort only what THIS call added -- from `base`, the count
		 * carried over from a previous GLOB_APPEND, onwards.
		 *
		 * glob.html APPLICATION USAGE: "The new pathnames generated by
		 * a subsequent call with GLOB_APPEND are not sorted together
		 * with the previous pathnames."  Sorting the whole vector
		 * re-sorted the predecessor's results into this call's, so
		 * glob("*.log", 0) followed by glob("*.txt", GLOB_APPEND) gave
		 * "a.txt b.txt d.log" where POSIX requires "d.log a.txt
		 * b.txt".  Each call's results stay in their own sorted run.
		 *
		 * base is 0 for a non-GLOB_APPEND call, so this is the same
		 * whole-vector sort as before in the ordinary case. */
		qsort((void *)(out.v + base), out.n - base, sizeof *out.v, cmpstrp);
	}

	if (out.n == base && !(flags & GLOB_NOCHECK)) {
		/* GLOB_NOMATCH: nothing matched, and there is nothing else to
		 * allocate for it -- except under GLOB_APPEND, where the old
		 * pglob->gl_pathv was already freed above and must be replaced
		 * with *something* freeable, even if empty, so a subsequent
		 * globfree() stays well-defined. A plain (non-APPEND) call
		 * just leaves pglob's pathv/pathc at a safe, already-empty
		 * state: nothing was ever allocated for this call, so nothing
		 * needs freeing, and globfree() on a NULL gl_pathv is already
		 * a no-op (see below). */
		if (flags & GLOB_APPEND) {
			int frc = finish(&out, flags, pglob);
			if (frc) return frc;
		}
		else { pv_free_from(&out, 0); pglob->gl_pathc = 0; pglob->gl_pathv = 0; }
		return GLOB_NOMATCH;
	}
	return finish(&out, flags, pglob);
}

void globfree(glob_t *pglob)
{
	size_t i, offs;

	if (!pglob || !pglob->gl_pathv) return;
	offs = pglob->gl_offs;
	for (i = 0; i < pglob->gl_pathc; i++) __free(pglob->gl_pathv[offs + i]);
	__free((void *)pglob->gl_pathv);
	pglob->gl_pathv = 0;
	pglob->gl_pathc = 0;
}
