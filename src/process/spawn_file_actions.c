/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * posix_spawn_file_actions_t: the recorded open/close/dup2 list.
 *
 * This file is only the *recording* half -- a growable array of actions
 * in the order they were added, which is the order posix_spawn() is
 * required to replay them in
 * (https://pubs.opengroup.org/onlinepubs/9699919799/functions/posix_spawn.html
 * DESCRIPTION: "The file actions specified by the spawn file actions
 * object shall be performed in the order in which they were added").
 * The replay itself is in posix_spawn.c, which is where the interesting
 * part -- doing it without a fork() to do it in -- lives.
 *
 * Every function here returns an error number and does not set errno,
 * per each function's own RETURN VALUE section ("Upon successful
 * completion, ... shall return zero; otherwise, an error number shall
 * be returned to indicate the error").  malloc()/free()/strlen() are
 * the only calls made, and only malloc() can fail, so preserving the
 * caller's errno is a matter of not clobbering it: __spawn_fa_grow()
 * saves and restores it around the allocation.
 */
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "libc.h"
#include "spawn_internal.h"

/* Grow *fa by one entry, returning a pointer to it or NULL on ENOMEM.
 *
 * errno is saved and restored across the realloc: these functions
 * report by return value, and a caller that had a meaningful errno
 * sitting in it is entitled to still have it afterwards. */
static struct __spawn_action *fa_push(posix_spawn_file_actions_t *fa)
    __attribute__((nonnull(1)));
static struct __spawn_action *fa_push(posix_spawn_file_actions_t *fa)
{
	int e = errno;
	struct __spawn_action *n;
	if (fa->__len == fa->__cap) {
		int cap = fa->__cap ? fa->__cap * 2 : 8;
		/* The list is bounded only by the caller; keep the doubling
		 * away from signed overflow rather than trusting it not to
		 * get there. */
		if (cap <= fa->__cap || cap > INT_MAX / (int)sizeof *n) { errno = e; return 0; }
		n = realloc(fa->__actions, (size_t)cap * sizeof *n);
		if (!n) { errno = e; return 0; }
		fa->__actions = n;
		fa->__cap = cap;
	}
	/* fa->__actions is non-NULL here either way: either the growth
	 * branch above just set it (realloc succeeded, checked above), or
	 * fa->__len != fa->__cap already meant fa->__cap > 0, which by this
	 * function's own invariant only ever holds after an earlier
	 * successful growth already set fa->__actions. Not expressible via
	 * nonnull on fa itself (already marked, and this is a fact about one
	 * of fa's FIELDS, not fa) -- a local proof the checker cannot follow
	 * through the conditional reassignment. */
	n = &fa->__actions[fa->__len++];
	memset(n, 0, sizeof *n);
	errno = e;
	return n;
}

/* [EBADF] "The value specified by fildes ... is negative or greater
 * than or equal to {OPEN_MAX}."  FD_MAX is this library's {OPEN_MAX}
 * (src/internal/libc.h; sysconf(_SC_OPEN_MAX) reports it). */
static int fd_ok(int fd) { return fd >= 0 && fd < FD_MAX; }

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *fa)
{
	fa->__len = 0;
	fa->__cap = 0;
	fa->__actions = 0;
	return 0;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *fa)
{
	int i, e = errno;
	/* fa->__actions[i]: same fa->__len > 0 implies fa->__actions != NULL
	 * field invariant as fa_push()'s own comment above establishes; not
	 * expressible via nonnull on fa (already marked in spawn.h) since
	 * this is about one of fa's fields, not fa itself. */
	for (i = 0; i < fa->__len; i++) free(fa->__actions[i].path);
	free(fa->__actions);
	fa->__actions = 0;
	fa->__len = fa->__cap = 0;
	errno = e;
	return 0;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *fa, int fd)
{
	struct __spawn_action *a;
	if (!fd_ok(fd)) return EBADF;
	a = fa_push(fa);
	if (!a) return ENOMEM;
	a->kind = __SPAWN_CLOSE;
	a->fd = fd;
	return 0;
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *fa, int fd, int newfd)
{
	struct __spawn_action *a;
	if (!fd_ok(fd) || !fd_ok(newfd)) return EBADF;
	a = fa_push(fa);
	if (!a) return ENOMEM;
	a->kind = __SPAWN_DUP2;
	a->fd = fd;
	a->newfd = newfd;
	return 0;
}

int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *__restrict fa,
                                     int fd, const char *__restrict path, int oflag, mode_t mode)
{
	struct __spawn_action *a;
	char *copy;
	size_t n;
	int e = errno;
	if (!fd_ok(fd)) return EBADF;
	/* posix_spawn_file_actions_addopen.html DESCRIPTION: "The string
	 * described by path shall be copied by the
	 * posix_spawn_file_actions_addopen() function." -- so the caller
	 * may free or reuse its buffer the moment this returns. */
	n = strlen(path) + 1;
	copy = malloc(n);
	if (!copy) { errno = e; return ENOMEM; }
	memcpy(copy, path, n);
	a = fa_push(fa);
	if (!a) { free(copy); errno = e; return ENOMEM; }
	a->kind = __SPAWN_OPEN;
	a->fd = fd;
	a->path = copy;
	a->oflag = oflag;
	a->mode = mode;
	errno = e;
	return 0;
}
