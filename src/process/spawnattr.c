/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * posix_spawnattr_t: the accessors posix_spawn()'s first consumers
 * need -- _init/_destroy/_setflags/_setsigmask.  The other six
 * (the getters, and spawn-pgroup/spawn-sigdefault/spawn-schedparam/
 * spawn-schedpolicy) follow.
 *
 * These are attribute *storage* and nothing else.  Every one of them is
 * specified as "get/set the spawn-<x> attribute of the object referenced
 * by attr", with no requirement that the implementation be able to act
 * on the value -- posix_spawn() is where acting on it is specified, and
 * where this implementation reports what it cannot do (posix_spawn.c).
 *
 *
 * Each function returns an error number and does not set errno (each
 * page's RETURN VALUE: "shall return zero; otherwise, an error number
 * shall be returned").  None of them can fail here: there is no
 * allocation, and POSIX makes the [EINVAL] "the value specified by attr
 * is invalid" case a *may fail*, which this implementation does not
 * take up -- a posix_spawnattr_t that was never _init()ed is undefined
 * behaviour, not a detectable error.
 *
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/posix_spawnattr_init.html
 */
#include <spawn.h>
#include <string.h>
#include <signal.h>
#include "libc.h"

/* posix_spawnattr_init.html DESCRIPTION: "the resulting spawn
 * attributes object ... contains ... the default values" -- and the
 * default for every attribute this header has is the "do nothing"
 * value: no flags set, so nothing else is even consulted. */
int posix_spawnattr_init(posix_spawnattr_t *a)
{
	memset(a, 0, sizeof *a);
	return 0;
}

/* Nothing is allocated by any setter, so destroy has nothing to
 * release.  It still exists, is still required, and callers must still
 * call it: what it destroys is the caller's licence to keep using the
 * object, not a heap block. */
int posix_spawnattr_destroy(posix_spawnattr_t *a)
{
	(void)a;
	return 0;
}

int posix_spawnattr_setflags(posix_spawnattr_t *a, short flags)
{
	a->__flags = flags;
	return 0;
}

int posix_spawnattr_setsigmask(posix_spawnattr_t *__restrict a, const sigset_t *__restrict set)
{
	a->__sigmask = *set;
	return 0;
}
