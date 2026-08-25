/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Resolving a program name for execvp.
 *
 * A name with a directory part (a '/', a '\\', or a drive letter) is
 * taken as-is; __spawn reports ENOENT if it does not exist.  Anything
 * else is looked up in each directory of PATH, trying the name and then
 * the name with ".exe" appended, which is what Windows expects an image
 * to be called.  PATH here is the Windows variable, whose entries are
 * separated by ';' -- a ':' cannot be the separator because every
 * absolute entry ("C:\Windows") contains one.  An empty entry means the
 * current directory, as on Unix.
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "libc.h"

static int has_dir(const char *name)
{
	if (strchr(name, '/') || strchr(name, '\\')) return 1;
	if (((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')) && name[1] == ':') return 1;
	return 0;
}

static char *try_dir(const char *dir, size_t dlen, const char *name)
{
	size_t nlen = strlen(name);
	/* dlen/nlen come from a PATH entry and the program name; the taint
	 * checker can't see that the allocation size below exactly matches
	 * what the writes that follow need (dlen + optional separator +
	 * nlen+1 + up to ".exe\0"), with no clamp needed since malloc simply
	 * fails on an absurd PATH rather than overflowing. */
	char *p = malloc(dlen + 1 + nlen + 4 + 1); // NOLINT(clang-analyzer-optin.taint.TaintedAlloc)
	if (!p) return 0;
	if (dlen) {
		memcpy(p, dir, dlen);
		if (p[dlen-1] != '/' && p[dlen-1] != '\\') p[dlen++] = '\\'; // NOLINT(clang-analyzer-security.ArrayBound)
	}
	memcpy(p + dlen, name, nlen + 1);
	if (access(p, X_OK) == 0) return p;
	memcpy(p + dlen + nlen, ".exe", 5);
	if (access(p, X_OK) == 0) return p;
	free(p);
	return 0;
}

char *__find_program(const char *name, int use_path)
{
	const char *path, *p;
	char *r;
	/* The empty string names nothing, and it has to be answered here
	 * rather than left to the search below.  exec.html's [ENOENT] is
	 * explicit -- "A component of path or file does not name an
	 * existing file or path or file is an empty string" -- but "" has
	 * no directory part, so has_dir() sends it into the PATH loop,
	 * where try_dir() appends it to a PATH entry and produces
	 * `<entry>\`: the directory itself, with nothing after it.
	 * access(X_OK) is satisfied by a directory (src/unistd/access.c --
	 * NTFS has no execute bit this library maps X_OK onto), so the
	 * loop *succeeds* on its first entry, and execvp("") ends up
	 * asking NT to run a directory as a process image.  The errno that
	 * comes back is then whatever status the failed image section maps
	 * to (EBADF under Wine, EIO on NT per fexecve()'s note in
	 * src/process/exec.c) -- never the ENOENT the clause requires. */
	if (!name[0]) { errno = ENOENT; return 0; }
	if (!use_path || has_dir(name)) {
		r = malloc(strlen(name) + 1);
		if (r) strcpy(r, name);
		return r;
	}
	path = getenv("PATH");
	if (!path) path = "";
	p = path;
	for (;;) {
		const char *e = strchr(p, ';');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		r = try_dir(p, len, name);
		if (r) return r;
		if (!e) break;
		p = e + 1;
	}
	errno = ENOENT;
	return 0;
}
