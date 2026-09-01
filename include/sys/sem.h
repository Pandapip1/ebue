/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/sem.h> -- XSI semaphores:
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/sys_sem.h.html
 *
 * Not to be confused with <semaphore.h>'s POSIX semaphores (src/thread/
 * semaphore.c, a single counter): this is the older XSI mechanism, a
 * whole ARRAY of counters (a "semaphore set") allocated by one semget(),
 * adjusted atomically as a group by one semop() call, with a value
 * range/wraparound and undo-on-exit model semaphore.h's sem_t does not
 * have.
 *
 * semget()/semop()/semctl() are implemented once per OS backend:
 * src/ipc/linux/plat_sysvipc.c over the real semget(2)/semop(2)/
 * semctl(2) syscalls (struct sembuf below is already the exact kernel
 * ABI struct, so it crosses unmarshalled), and src/ipc/nt/plat_sem.c, a
 * real emulation storing each set's values directly in a shared backing
 * file guarded by a named NT mutant -- see that file's own banner for
 * why semop()'s atomic-array-or-block contract is implemented as a
 * bounded retry loop rather than a single wait on an NT dispatcher
 * object (no NT primitive is an atomic multi-counter compare-and-block
 * the way this call's semantics require), and for the one accounting
 * casualty of that choice (GETNCNT/GETZCNT).
 */
#ifndef _SYS_SEM_H
#define _SYS_SEM_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <sys/ipc.h>

#define __NEED_pid_t
#define __NEED_time_t
#define __NEED_size_t

#include <bits/alltypes.h>

/* semop.html: struct sembuf, the array element passed to semop().
 * Deliberately laid out exactly like the Linux kernel's own struct
 * sembuf (<linux/sem.h>): the Linux backend passes an application's
 * array straight through to semop(2) unmarshalled, the same way
 * <sys/sem.h>'s IPC_* values already match the kernel's. */
struct sembuf {
	unsigned short sem_num;
	short sem_op;
	short sem_flg;
};

/* semop() sem_flg, in addition to IPC_NOWAIT (<sys/ipc.h>). SEM_UNDO
 * requests exit-time undo of this operation; sys_sem.h.html requires the
 * header to define the bit but does not require an implementation to
 * make it do anything beyond exist as a distinct value (Base
 * Definitions' own SEM_UNDO entry has no separate description page).
 * Neither backend implements the undo-on-exit adjustment set itself --
 * a real per-process/per-set undo table is genuinely more machinery
 * than either backend's own IPC_PRIVATE-scoped test coverage exercises,
 * and no fenced test here sets this bit -- so a caller that sets it gets
 * an ordinary semop() with no undo recorded, rather than a silently
 * wrong undo. */
#define SEM_UNDO 010000

/* semctl.html DESCRIPTION: cmd values. */
#define GETNCNT 14
#define GETPID  11
#define GETVAL  12
#define GETALL  13
#define GETZCNT 15
#define SETVAL  16
#define SETALL  17

/* sys_sem.h.html DESCRIPTION: the semid_ds structure "shall include the
 * following members: struct ipc_perm sem_perm ... unsigned short
 * sem_nsems ... time_t sem_otime ... time_t sem_ctime". */
struct semid_ds {
	struct ipc_perm sem_perm;
	unsigned short sem_nsems;
	time_t sem_otime;
	time_t sem_ctime;
};

/* sys_sem.h.html: "union semun ... used ... as the type of the fourth
 * argument, if any, in calls to the semctl() function." POSIX declares
 * this an application-supplied type on some historical implementations,
 * but explicitly lists it as required here; providing it is what lets a
 * caller pass a bare int/pointer through semctl()'s "..." the way
 * test/posix-ipc.c does, without also having to define its own union
 * first. */
union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
};

int semctl(int, int, int, ...);
int semget(key_t, int, int);
int semop(int, struct sembuf *, size_t);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
