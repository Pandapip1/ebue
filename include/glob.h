/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <glob.h>: pathname expansion built on <fnmatch.h> plus ntlibc's
 * existing, working opendir/readdir/stat layer (src/dirent/,
 * src/unistd/stat.c).  See src/glob/glob.c for the matcher itself.
 *
 * Tilde expansion is deliberately NOT here: glob.html APPLICATION USAGE
 * says outright "Applications that need tilde and parameter expansion
 * should use wordexp()", so a leading '~' is just an ordinary pattern
 * character to the base glob() below (test/posix-glob.c's N/A-fenced
 * test_glob_tilde_not_base_scope documents this explicitly).  ntlibc
 * does not implement glibc's non-standard GLOB_TILDE extension, which
 * is the only thing that would pull getpwnam() (include/pwd.h) into
 * this file; that lookup happens instead in <wordexp.h>, where POSIX
 * actually places it.
 *
 * glob_t layout and the GLOB_* flags plus GLOB_ABORTED/GLOB_NOMATCH/
 * GLOB_NOSPACE values are fixed at test/posix-glob.c's choices (that file predates
 * this header and declares its own local, unmodified copies of them);
 * matching them here is what lets that test call glob() through its
 * own local prototype and still agree on what every flag bit and every
 * return value means. POSIX itself only requires these be "defined
 * constants", nothing about their numeric values.
 */
#ifndef _GLOB_H
#define _GLOB_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_size_t
#include <bits/alltypes.h>

typedef struct {
	size_t gl_pathc;	/* count of paths matched */
	char **gl_pathv;	/* list of matched pathnames, NULL-terminated */
	size_t gl_offs;		/* slots to reserve at gl_pathv's front, if GLOB_DOOFFS */
} glob_t;

#define GLOB_APPEND	0x001
#define GLOB_DOOFFS	0x002
#define GLOB_ERR	0x004
#define GLOB_MARK	0x008
#define GLOB_NOCHECK	0x010
#define GLOB_NOESCAPE	0x020
#define GLOB_NOSORT	0x040

#define GLOB_ABORTED	1
#define GLOB_NOMATCH	2
#define GLOB_NOSPACE	3

/* pattern/pglob required: src/glob/glob.c's own body dereferences
 * pattern unconditionally at entry (`*pat == '/'`, pat == pattern) and
 * eventually reaches finish(), which writes through pglob
 * unconditionally on every real return path -- no branch of glob()
 * leaves pglob untouched. errfunc is deliberately NOT required:
 * glob.html is explicit that "[i]f errfunc is a null pointer, it shall
 * not be called", and every real call in this tree that does not want
 * error reporting (src/wordexp/wordexp.c's own glob(pat.data, 0, 0,
 * &g), test/posix-glob.c's many `NULL` calls) relies on that being a
 * real, honoured NULL, not decoration. */
int glob(const char *__restrict, int, int (*)(const char *, int), glob_t *__restrict)
    __attribute__((nonnull(1, 4)));
/* pglob deliberately NOT required: src/glob/glob.c's own
 * `if (!pglob || !pglob->gl_pathv) return;` is real and load-bearing,
 * matching the setenv()/unsetenv() "genuinely optional, defensively
 * checked" precedent this tree already applies elsewhere. */
void globfree(glob_t *);

#ifdef __cplusplus
}
#endif
#endif
