/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <pwd.h>: there is no /etc/passwd on NT, but the process token identifies
 * exactly one current user.  src/unistd/ids.c maps that user's SAM/AD SID
 * and RID to a uid, and setuid()/seteuid() can only retain that identity.
 * The user's name, home directory and shell are also things NT can answer.
 * src/misc/pwd.c fills a struct passwd for that user
 * and refuses -- cleanly, per getpwnam.html/getpwuid.html, not with a
 * fabricated record -- to answer for anyone else.  See src/misc/pwd.c's
 * header comment for the getpwent()/setpwent()/endpwent() decision. */
#ifndef _PWD_H
#define _PWD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_uid_t
#define __NEED_gid_t
#define __NEED_size_t
#include <bits/alltypes.h>

/* pwd.h.html: "at least" these five members. */
struct passwd {
	char *pw_name;
	uid_t pw_uid;
	gid_t pw_gid;
	char *pw_dir;
	char *pw_shell;
};

struct passwd *getpwnam(const char *);
struct passwd *getpwuid(uid_t);
int getpwnam_r(const char *, struct passwd *, char *, size_t, struct passwd **);
int getpwuid_r(uid_t, struct passwd *, char *, size_t, struct passwd **);

/* XSI; see src/misc/pwd.c for why these are implemented rather than
 * left undeclared -- ntlibc's user database genuinely does have
 * exactly one entry, so enumerating it is not a fabrication. */
struct passwd *getpwent(void);
void setpwent(void);
void endpwent(void);

#ifdef __cplusplus
}
#endif
#endif
