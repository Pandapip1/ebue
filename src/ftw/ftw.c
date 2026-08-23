/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ftw()/nftw(): a recursion driver over opendir/readdir (src/dirent/)
 * and stat/lstat (src/stat/) -- nothing here is NT-specific.
 *
 * nopenfd: naively opening one DIR per level and recursing would hold
 * as many directory streams open at once as the walk is deep, with no
 * bound at all.  Instead, every currently-open ancestor DIR is kept on
 * an LRU list (struct walkstate's lru_head/lru_tail); opening one more
 * than nopenfd allows closes the least-recently-used ancestor first,
 * remembering its position with telldir().  When the walk returns to
 * that ancestor to keep reading its entries, it is reopened with
 * opendir() and replayed back to that position with seekdir() --
 * exactly the classic historical ftw() trick (also used by 4.4BSD and
 * glibc's implementations) for honouring nopenfd without limiting how
 * deep the tree may go.
 *
 * FTW_MOUNT / st_dev: src/stat/stat.c sets st_dev from
 * FILE_FS_VOLUME_INFORMATION's VolumeSerialNumber, a real, distinct
 * value per NTFS volume -- so "same file system as path" has a real,
 * working test here (root_dev, captured from the first stat() of the
 * walk's own root, compared against every subsequent entry). This is
 * not N/A on this platform.
 *
 * FTW_CHDIR: built directly on chdir() (src/unistd/chdir.c), already
 * implemented and working.
 */
#include <ftw.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

struct level {
	struct level *lru_prev, *lru_next;	/* only meaningful while dp != NULL */
	DIR *dp;
	long pos;		/* telldir() position, valid while dp == NULL and pos != 0 */
	char *path;
};

struct walkstate {
	int nopenfd;
	int open_count;
	int flags;
	int legacy;		/* ftw(): never report FTW_SL/FTW_SLN/FTW_DP */
	int have_root_dev;
	dev_t root_dev;
	char *cwd0;		/* FTW_CHDIR only: process cwd when the walk started */
	int (*fn3)(const char *, const struct stat *, int);
	int (*fn4)(const char *, const struct stat *, int, struct FTW *);
};

/* FTW_CHDIR: "change the current working directory to each directory as
 * it reports files in that directory" (nftw.html). walk()'s own `path`
 * argument is always built relative to the walk's *original* cwd (each
 * recursive call appends "/name" to its parent's path), but by the time
 * a directory two or more levels deep is entered, chdir() has already
 * moved the process into its parent -- so chdir(path) with the full
 * accumulated path would look for "parent/parent/child" under the new
 * cwd and fail. Resolving `path` to an absolute pathname against the
 * cwd captured once, before the walk's first chdir(), sidesteps that
 * regardless of how deep the recursion is or how many directories have
 * already been entered. Same absolute-path test __ntpath_at() (src/
 * internal/path.c) uses: a leading slash/backslash, or an "X:" drive
 * letter. */
static int chdir_absolute(struct walkstate *ws, const char *path)
{
	int absolute = path[0] == '/' || path[0] == '\\' ||
		(((path[0] | 0x20) >= 'a' && (path[0] | 0x20) <= 'z') && path[1] == ':');
	if (absolute || !ws->cwd0) return chdir(path);
	{
		size_t l0 = strlen(ws->cwd0), l1 = strlen(path);
		char *full = malloc(l0 + 1 + l1 + 1);
		int r;
		if (!full) return -1;
		memcpy(full, ws->cwd0, l0);
		full[l0] = '/';
		memcpy(full + l0 + 1, path, l1 + 1);
		r = chdir(full);
		free(full);
		return r;
	}
}

/* Head/tail are tracked via two file-scope pointers threaded through the
 * recursion by argument rather than true globals, so a nested/recursive
 * call from within a callback (unusual, but not forbidden) cannot
 * corrupt an in-progress outer walk's bookkeeping. */
struct lru {
	struct level *head, *tail;
};

static void lru_unlink(struct lru *lru, struct level *lv)
{
	if (lv->lru_prev) lv->lru_prev->lru_next = lv->lru_next; else if (lru->head == lv) lru->head = lv->lru_next;
	if (lv->lru_next) lv->lru_next->lru_prev = lv->lru_prev; else if (lru->tail == lv) lru->tail = lv->lru_prev;
	lv->lru_prev = lv->lru_next = NULL;
}

static void lru_push_tail(struct lru *lru, struct level *lv)
{
	lv->lru_prev = lru->tail;
	lv->lru_next = NULL;
	if (lru->tail) lru->tail->lru_next = lv; else lru->head = lv;
	lru->tail = lv;
}

static void close_one(struct walkstate *ws, struct lru *lru, struct level *lv)
{
	lv->pos = telldir(lv->dp);
	closedir(lv->dp);
	lv->dp = NULL;
	lru_unlink(lru, lv);
	ws->open_count--;
}

/* Reopen lv if it is currently closed, evicting the LRU ancestor first
 * if that would exceed nopenfd.  Returns 0 on success, -1 with errno
 * set (from opendir()) on failure. */
static int level_open(struct walkstate *ws, struct lru *lru, struct level *lv)
{
	if (lv->dp) return 0;
	if (ws->nopenfd >= 1 && ws->open_count >= ws->nopenfd && lru->head)
		close_one(ws, lru, lru->head);
	lv->dp = opendir(lv->path);
	if (!lv->dp) return -1;
	if (lv->pos) seekdir(lv->dp, lv->pos);
	ws->open_count++;
	lru_push_tail(lru, lv);
	return 0;
}

static int base_offset(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? (int)(slash - path + 1) : 0;
}

static int report(struct walkstate *ws, const char *path, const struct stat *st, int type, int level)
{
	struct FTW f;

	if (ws->legacy && (type == FTW_SLN || type == FTW_SL || type == FTW_DP))
		type = (type == FTW_SL) ? FTW_NS : (type == FTW_SLN ? FTW_NS : FTW_D);

	if (ws->fn3) return ws->fn3(path, st, type);
	f.base = base_offset(path);
	f.level = level;
	return ws->fn4(path, st, type, &f);
}

static int mount_skip(struct walkstate *ws, const struct stat *st)
{
	if (!(ws->flags & FTW_MOUNT)) return 0;
	if (!ws->have_root_dev) { ws->root_dev = st->st_dev; ws->have_root_dev = 1; return 0; }
	return st->st_dev != ws->root_dev;
}

static int walk(struct walkstate *ws, struct lru *lru, const char *path, int level, int is_root)
{
	struct stat lst, st, zero;
	const struct stat *rst;
	int type, r;

	if (lstat(path, &lst) < 0) {
		/* ftw.html ERRORS: "[ENOENT] A component of path does not name
		 * an existing file or path is an empty string" -- for the
		 * walk's own root this is ftw()/nftw() itself failing (-1,
		 * errno left as lstat() set it), not something routed through
		 * the callback as FTW_NS. A descendant discovered by readdir()
		 * that later turns out unstatable (e.g. removed mid-walk) is
		 * exactly what FTW_NS is for, so only the root is special. */
		if (is_root) return -1;
		memset(&zero, 0, sizeof zero);
		return report(ws, path, &zero, FTW_NS, level);
	}

	if (ws->flags & FTW_PHYS) {
		rst = &lst;
		type = S_ISLNK(lst.st_mode) ? FTW_SL : S_ISDIR(lst.st_mode) ? FTW_D : FTW_F;
	} else if (stat(path, &st) < 0) {
		return report(ws, path, &lst, S_ISLNK(lst.st_mode) ? FTW_SLN : FTW_NS, level);
	} else {
		rst = &st;
		type = S_ISDIR(st.st_mode) ? FTW_D : FTW_F;
	}

	if (mount_skip(ws, rst)) return 0;

	if (type != FTW_D) return report(ws, path, rst, type, level);

	/* Directory: FTW_DEPTH reports it last (FTW_DP), after every entry;
	 * otherwise report it first, as FTW_D, before descending. */
	if (!(ws->flags & FTW_DEPTH)) {
		r = report(ws, path, rst, FTW_D, level);
		if (r) return r;
	}

	{
		struct level lv;
		struct dirent *de;
		size_t plen = strlen(path);
		int had_trailing_slash = plen > 0 && path[plen - 1] == '/';

		lv.lru_prev = lv.lru_next = NULL;
		lv.dp = NULL;
		lv.pos = 0;
		lv.path = strdup(path);
		if (!lv.path) { errno = ENOMEM; return -1; }

		if (level_open(ws, lru, &lv) < 0) {
			free(lv.path);
			return report(ws, path, rst, FTW_DNR, level);
		}

		if (ws->flags & FTW_CHDIR) chdir_absolute(ws, path);

		r = 0;
		while ((de = readdir(lv.dp)) != NULL) {
			char *child;
			size_t clen;

			if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;

			clen = plen + (had_trailing_slash ? 0 : 1) + strlen(de->d_name) + 1;
			child = malloc(clen);
			if (!child) { r = -1; errno = ENOMEM; break; }
			if (had_trailing_slash) snprintf(child, clen, "%s%s", path, de->d_name);
			else snprintf(child, clen, "%s/%s", path, de->d_name);

			/* level_open() may have closed lv.dp to make room for a
			 * descendant's own directory; reopen (and replay via
			 * seekdir()) right before the next readdir() needs it. */
			r = walk(ws, lru, child, level + 1, 0);
			free(child);
			if (r) break;

			if (level_open(ws, lru, &lv) < 0) { r = -1; break; }
		}

		if (lv.dp) close_one(ws, lru, &lv);
		free(lv.path);
		if (r) return r;
	}

	if (ws->flags & FTW_DEPTH)
		return report(ws, path, rst, FTW_DP, level);
	return 0;
}

int ftw(const char *path, int (*fn)(const char *, const struct stat *, int), int nopenfd)
{
	struct walkstate ws;
	struct lru lru;

	if (!path || !*path) { errno = ENOENT; return -1; }

	memset(&ws, 0, sizeof ws);
	ws.nopenfd = nopenfd;
	ws.fn3 = fn;
	ws.legacy = 1;
	lru.head = lru.tail = NULL;

	return walk(&ws, &lru, path, 0, 1);
}

int nftw(const char *path, int (*fn)(const char *, const struct stat *, int, struct FTW *),
	 int nopenfd, int flags)
{
	struct walkstate ws;
	struct lru lru;

	if (!path || !*path) { errno = ENOENT; return -1; }

	memset(&ws, 0, sizeof ws);
	ws.nopenfd = nopenfd;
	ws.flags = flags;
	ws.fn4 = fn;
	lru.head = lru.tail = NULL;

	if (flags & FTW_CHDIR) {
		/* getcwd()'s own required "grow the buffer until it fits"
		 * loop (getcwd.html: no fixed size may be assumed) rather
		 * than a single fixed-size guess. */
		size_t cap = 256;
		for (;;) {
			char *buf = malloc(cap);
			if (!buf) break;
			if (getcwd(buf, cap)) { ws.cwd0 = buf; break; }
			free(buf);
			if (errno != ERANGE) break;
			cap *= 2;
		}
	}

	{
		int r = walk(&ws, &lru, path, 0, 1);
		free(ws.cwd0);
		return r;
	}
}
