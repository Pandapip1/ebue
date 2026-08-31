/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <grp.h>: the same story as <pwd.h> (see that header's comment and
 * src/misc/pwd.c's), one gid deep instead of one uid.  getgid() and
 * getegid() (src/unistd/ids.c) always agree, so "the current group" is
 * the only group this library can ever answer about honestly, and it
 * genuinely is answerable: src/misc/grp.c fills a struct group for it
 * and refuses -- cleanly, per getgrnam.html/getgrgid.html, not with a
 * fabricated record -- to answer for any other gid or name.  See
 * src/misc/grp.c's header comment for what gr_mem contains and for the
 * getgrent()/setgrent()/endgrent() decision. */
#ifndef _GRP_H
#define _GRP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_gid_t
#define __NEED_size_t
#include <bits/alltypes.h>

/* grp.h.html: "at least" these three members. */
struct group {
	char *gr_name;
	gid_t gr_gid;
	char **gr_mem;
};

struct group *getgrnam(const char *);
struct group *getgrgid(gid_t);
/* Same shape as the identical getpwnam_r()/getpwuid_r() pair in
 * include/pwd.h -- see that comment. grp (arg 2) is forwarded,
 * unguarded, into the static fill_current() in src/misc/grp.c, whose
 * own write to gr->gr_name is unconditional on the
 * name-known-and-buffer-big-enough path; result (arg 5) is
 * dereferenced directly (set to zero) at the top of both real bodies.
 * name/buffer are left unmarked for the identical reasons (a real
 * NULL check on name in getgrnam_r(); buffer is safe NULL exactly
 * when bufsize is 0). */
int getgrnam_r(const char *, struct group *, char *, size_t, struct group **)
    __attribute__((nonnull(2, 5)));
int getgrgid_r(gid_t, struct group *, char *, size_t, struct group **)
    __attribute__((nonnull(2, 5)));

/* XSI; see src/misc/grp.c for why these are implemented rather than
 * left undeclared -- same reasoning as <pwd.h>'s getpwent() family:
 * ntlibc's group database genuinely does have exactly one entry, so
 * enumerating it is not a fabrication. */
struct group *getgrent(void);
void setgrent(void);
void endgrent(void);

#ifdef __cplusplus
}
#endif
#endif
