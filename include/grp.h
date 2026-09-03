/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <grp.h>: same story as <pwd.h>, one gid deep. getgid()/getegid() always
 * agree, so "the current group" is the only group this library can
 * honestly answer about; it cleanly refuses any other gid or name rather
 * than fabricating a record. */
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
/* Same shape as getpwnam_r()/getpwuid_r() in <pwd.h>. */
int getgrnam_r(const char *, struct group *, char *, size_t, struct group **)
    __attribute__((nonnull(2, 5)));
int getgrgid_r(gid_t, struct group *, char *, size_t, struct group **)
    __attribute__((nonnull(2, 5)));

/* XSI; same reasoning as <pwd.h>'s getpwent() family -- the group
 * database genuinely has exactly one entry, so enumerating it is honest. */
struct group *getgrent(void);
void setgrent(void);
void endgrent(void);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
