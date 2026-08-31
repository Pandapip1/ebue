/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef	_DIRENT_H
#define	_DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_ino_t
#define __NEED_off_t
#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
#define __NEED_size_t
#define __NEED_ssize_t
#endif

#include <bits/alltypes.h>

typedef struct __dirstream DIR;

#define _DIRENT_HAVE_D_RECLEN
#define _DIRENT_HAVE_D_OFF
#define _DIRENT_HAVE_D_TYPE

struct dirent {
	ino_t d_ino;
	off_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[256];
};

#define d_fileno d_ino

/* closedir/readdir/readdir_r/rewinddir/dirfd/seekdir/telldir all take
 * their DIR * as a required, non-optional handle: POSIX documents the
 * behaviour of each as undefined unless dp designates an open directory
 * stream obtained from opendir()/fdopendir(), the same "not the callee's
 * job to validate" contract every other libc (glibc, musl) implements
 * this family under -- confirmed against this tree's own bodies
 * (src/dirent/*.c), none of which null-check dp anywhere; it is
 * unconditionally dereferenced on function entry in every one of them.
 * __attribute__((nonnull)) makes that real, pre-existing contract
 * explicit and lets GCC/Clang's own -Wnonnull catch a provably-NULL
 * caller mistake at compile time; tcc (this project's other target)
 * parses and silently ignores attribute contents it does not know (see
 * include/features.h's own comment on this), so this is free there. */
int            closedir(DIR *) __attribute__((nonnull(1)));
DIR           *fdopendir(int);
DIR           *opendir(const char *);
struct dirent *readdir(DIR *) __attribute__((nonnull(1)));
/* entry/result are required too: src/dirent/readdir.c's readdir_r()
 * writes through *result unconditionally on every path with no NULL
 * check, and passes entry straight into the file-static fill(), itself
 * marked nonnull(1,2) because IT never checks its own out parameter
 * either -- a NULL entry here would already violate fill()'s real
 * contract, not just readdir_r's. */
int            readdir_r(DIR *__restrict, struct dirent *__restrict, struct dirent **__restrict)
    __attribute__((nonnull(1, 2, 3)));
void           rewinddir(DIR *) __attribute__((nonnull(1)));
int            dirfd(DIR *) __attribute__((nonnull(1)));

/* alphasort/versionsort are qsort(3)-family comparators (src/dirent/
 * scandir.c): the C library's own qsort/qsort_r never invokes a
 * comparator with a NULL element pointer, and both bodies dereference a
 * and b unconditionally. scandir's own res is required too -- `*res =
 * list;` is unconditional on the success path with no check -- while
 * path/filter/compar are left unmarked: path is only forwarded to
 * opendir() without being dereferenced here, and filter/compar are
 * explicitly optional (`if (filter...)`/`if (compar)` guard each use).
 *
 * A residual remains past nonnull(1, 2) on both: `(*a)->d_name` and
 * `(*b)->d_name` are about the VALUE *a/*b point to (a struct dirent*
 * one level further in), not about a/b themselves -- a fact `nonnull`
 * cannot describe on any signature, since it only ever asserts that
 * the parameter's own pointer value is non-NULL, not what a double
 * pointer points to. Verified sound by hand regardless: qsort_r's own
 * contract never invokes a comparator outside the array it was given,
 * and scandir()'s own `list` is built entirely from `__malloc()`
 * results checked for failure (`copy = __malloc(...); if (!copy) goto
 * fail;`) before ever being stored into it, so every element qsort_r
 * could ever hand alphasort/versionsort is already a genuinely
 * non-NULL struct dirent* by construction. */
int alphasort(const struct dirent **, const struct dirent **)
    __attribute__((nonnull(1, 2)));
int scandir(const char *, struct dirent ***, int (*)(const struct dirent *), int (*)(const struct dirent **, const struct dirent **))
    __attribute__((nonnull(2)));

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
void           seekdir(DIR *, long) __attribute__((nonnull(1)));
long           telldir(DIR *) __attribute__((nonnull(1)));
#endif

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14
#define IFTODT(x) ((x)>>12 & 017)
#define DTTOIF(x) ((x)<<12)
int getdents(int, struct dirent *, size_t);
#endif

#ifdef _GNU_SOURCE
int versionsort(const struct dirent **, const struct dirent **)
    __attribute__((nonnull(1, 2)));
#endif

#if defined(_LARGEFILE64_SOURCE)
#define dirent64 dirent
#define readdir64 readdir
#define readdir64_r readdir_r
#define scandir64 scandir
#define alphasort64 alphasort
#define versionsort64 versionsort
#define off64_t off_t
#define ino64_t ino_t
#define getdents64 getdents
#endif

#ifdef __cplusplus
}
#endif

#endif
