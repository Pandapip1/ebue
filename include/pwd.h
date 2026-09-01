/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

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
/* pwd (arg 2) is forwarded, unconditionally and with no guard of its
 * own, into the static fill_current() in src/misc/pwd.c, whose own
 * write to pw->pw_name (and friends) is unconditional on the
 * name-known-and-buffer-big-enough path -- the identical "readdir_r
 * entry forwarded into fill(), also required" shape the 9be895e sweep
 * established. result (arg 5) is dereferenced directly (set to zero)
 * at the top of both real bodies, no guard. name (arg 1) and buffer
 * (arg 3) are deliberately NOT marked: name has a real, live NULL
 * check in getpwnam_r() (uid has no such question for getpwuid_r(),
 * it is not a pointer), and buffer is forwarded into the buf argument
 * of fill_current(), which is safe with a NULL buffer exactly when
 * bufsize is 0 (the size comparison ahead of every dereference), not
 * merely undescribed. */
int getpwnam_r(const char *, struct passwd *, char *, size_t, struct passwd **)
    __attribute__((nonnull(2, 5)));
int getpwuid_r(uid_t, struct passwd *, char *, size_t, struct passwd **)
    __attribute__((nonnull(2, 5)));

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

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
