/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/ipc.h> -- the common header behind XSI (System V) IPC's three
 * mechanisms. ipc_perm and the IPC_* constants are the shared vocabulary
 * of shmget()/shmctl(), msgget()/msgctl() and semget()/semctl(); ftok()
 * is the one function this header declares, in src/ipc/ftok.c.
 *
 * IPC_CREAT/IPC_EXCL/IPC_NOWAIT and IPC_RMID/IPC_SET/IPC_STAT match the
 * Linux kernel's own <linux/ipc.h> numbering: src/ipc/linux/
 * plat_sysvipc.c passes them straight through, unexamined, as the raw
 * shmget/msgget/semget flag bits and shmctl/msgctl/semctl cmd argument.
 * The NT backend has no kernel meaning of its own for these, but sharing
 * one set of values means a struct or constant never has to ask which
 * platform compiled it.
 */
#ifndef _SYS_IPC_H
#define _SYS_IPC_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_uid_t
#define __NEED_gid_t
#define __NEED_mode_t
#define __NEED_key_t

#include <bits/alltypes.h>

/* No more members than POSIX requires: this project has no read side
 * (no ipcs(1)) that would want the kernel's key/sequence bookkeeping
 * surfaced here. */
struct ipc_perm {
	uid_t uid;
	gid_t gid;
	uid_t cuid;
	gid_t cgid;
	mode_t mode;
};

/* Resource get request flags: shmget/msgget/semget's *flg argument. */
#define IPC_CREAT  01000
#define IPC_EXCL   02000
#define IPC_NOWAIT 04000

/* Matches the kernel's own definition, key_t 0. */
#define IPC_PRIVATE ((key_t)0)

/* Control commands, shared by shmctl()/msgctl()/semctl(). */
#define IPC_RMID 0
#define IPC_SET  1
#define IPC_STAT 2

key_t ftok(const char *, int);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
