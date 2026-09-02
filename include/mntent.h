/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <mntent.h>: not POSIX (glibc/BSD historical, XSI-adjacent but never
 * standardized -- there is no *.html page for it on
 * pubs.opengroup.org), included here because third_party/libc-test's
 * mntent.c corpus test needs it and because src/misc/mntent.c can give
 * it a real, non-fabricated body: every function this header declares
 * is pure stdio -- read/write formatted lines through a FILE * the
 * caller already has open -- with nothing OS-specific in the parsing
 * itself.  getmntent()/getmntent_r()/addmntent()/hasmntopt()/
 * endmntent() work identically on every platform this project targets,
 * for the same reason fscanf() does: they never look past the FILE *
 * they are handed.  That is also exactly what the corpus test
 * exercises -- every case in it hands getmntent()/getmntent_r() a
 * fmemopen()'d buffer, never a real system path -- so nothing here is
 * gated on which platform's build this is compiled into.
 *
 * setmntent() is the one function that ever touches a real path, and it
 * is one fopen() call -- MOUNTED below names /proc/mounts, a real
 * kernel-maintained pseudo-file on Linux (this project talks to the
 * kernel directly, not through a distro's /etc/mtab convention, which
 * may not even be a real file); on NT no such path exists, so
 * setmntent(MOUNTED, "r") there fails ENOENT off the back of an
 * ordinary fopen() on a path that is not there -- a correct answer,
 * not a fabricated mount table, the same shape as this project's
 * AF_INET6/SOCK_DGRAM constants (<sys/socket.h>) failing cleanly at
 * runtime rather than being left undeclared. See src/misc/mntent.c's
 * own banner for the parsing format.
 */
#ifndef _MNTENT_H
#define _MNTENT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <stdio.h>

/* MOUNTED/MNTTAB: the traditional "currently mounted" / "fstab-style
 * static table" paths.  Meaningful only on Linux -- see this header's
 * banner. */
#define MOUNTED "/proc/mounts"
#define MNTTAB  "/etc/fstab"

/* mnt_freq/mnt_passno default to 0 when a line omits them -- see
 * src/misc/mntent.c's parser. */
struct mntent {
	char *mnt_fsname;
	char *mnt_dir;
	char *mnt_type;
	char *mnt_opts;
	int mnt_freq;
	int mnt_passno;
};

/* The historical, widely-recognized option-name constants (glibc/BSD
 * both declare these; a subset, since ntlibc has no filesystem driver
 * that would give the rest -- MNTOPT_QUOTA/_USRQUOTA/_GRPQUOTA and
 * similar -- any meaning here). */
#define MNTOPT_DEFAULTS "defaults"
#define MNTOPT_RO       "ro"
#define MNTOPT_RW       "rw"
#define MNTOPT_SUID     "suid"
#define MNTOPT_NOSUID   "nosuid"
#define MNTOPT_NOAUTO   "noauto"

FILE *setmntent(const char *, const char *);
struct mntent *getmntent(FILE *);
struct mntent *getmntent_r(FILE *__restrict, struct mntent *__restrict,
                            char *__restrict, int);
int addmntent(FILE *__restrict, const struct mntent *__restrict);
int endmntent(FILE *);
char *hasmntopt(const struct mntent *, const char *);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
