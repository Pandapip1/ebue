/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/ipc.h> -- the common header behind the XSI (System V) interprocess-
 * communication option group's three mechanisms:
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/sys_ipc.h.html
 *
 * ipc_perm and the seven IPC_* constants below are the vocabulary shmget()/
 * shmctl(), msgget()/msgctl() and semget()/semctl() (<sys/shm.h>,
 * <sys/msg.h>, <sys/sem.h>) all share; ftok() is the one function this
 * header itself declares, implemented in src/ipc/ftok.c.
 *
 * IPC_CREAT/IPC_EXCL/IPC_NOWAIT are deliberately given the exact bit
 * patterns the Linux kernel's own <linux/ipc.h> uses (0001000, 0002000,
 * 0004000 octal): src/ipc/linux/plat_sysvipc.c passes an application's
 * shmflg/msgflg/semflg straight through to shmget(2)/msgget(2)/semget(2)
 * as the low bits of the *flg argument, unexamined and untranslated, the
 * same way include/sys/mman.h's PROT_/MAP_ values already match the
 * kernel ABI so src/mman/linux/plat_mem.c needs no translation table for
 * them either. IPC_RMID/IPC_SET/IPC_STAT (0/1/2) are small control-command
 * integers, not flag bits, and match the kernel's numbering for the same
 * reason: the Linux backend forwards them as the raw shmctl(2)/msgctl(2)/
 * semctl(2) cmd argument. The NT backend (src/ipc/nt/) does not talk to a
 * kernel that assigns these numbers any meaning of its own, but keeping
 * one set of values across both backends means a struct or constant never
 * has to ask which platform compiled it.
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

/* sys_ipc.h.html DESCRIPTION: "the ipc_perm structure, which shall
 * include the following members: uid_t uid ... gid_t gid ... uid_t cuid
 * ... gid_t cgid ... mode_t mode". No more, no fewer are required, and
 * this project has no read side (no ipcs(1)) that would want the
 * kernel's internal key/sequence bookkeeping surfaced here, so none is
 * added -- see src/ipc/ftok.c and the per-mechanism *.c files for where
 * the key associated with an identifier is actually tracked. */
struct ipc_perm {
	uid_t uid;
	gid_t gid;
	uid_t cuid;
	gid_t cgid;
	mode_t mode;
};

/* "resource get request flags", ftok.html/shmget.html/msgget.html/
 * semget.html's *flg argument. */
#define IPC_CREAT  01000
#define IPC_EXCL   02000
#define IPC_NOWAIT 04000

/* sys_ipc.h.html: "IPC_PRIVATE -- A special value used to create a private
 * key of type key_t. This constant is used by an application when it
 * wants to create a new [XSI resource] without associating an access key
 * with the identifier." Every path in this tree that creates a new
 * identifier without going through a real key (see the *get() front
 * doors) compares against this exact value, matching the kernel's own
 * definition, key_t 0. */
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
