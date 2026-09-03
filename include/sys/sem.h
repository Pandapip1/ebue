/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/sem.h> -- XSI semaphores.
 *
 * Not to be confused with <semaphore.h>'s POSIX semaphores (a single
 * counter): this is the older XSI mechanism, a whole ARRAY of counters (a
 * "semaphore set") allocated by one semget() and adjusted atomically as a
 * group by one semop() call.
 *
 * On Linux, struct sembuf is the exact kernel ABI struct, so it crosses
 * the semop(2) syscall unmarshalled. src/ipc/nt/plat_sem.c is a real
 * emulation storing each set's values in a shared backing file guarded by
 * a named NT mutant; since no NT primitive is an atomic multi-counter
 * compare-and-block, semop()'s atomic-array-or-block contract there is a
 * bounded retry loop instead, at the cost of GETNCNT/GETZCNT accounting.
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

struct sembuf {
	unsigned short sem_num;
	short sem_op;
	short sem_flg;
};

/* semop() sem_flg, in addition to IPC_NOWAIT (<sys/ipc.h>). SEM_UNDO
 * requests exit-time undo of this operation, but neither backend
 * implements the undo-on-exit adjustment set: a caller that sets it gets
 * an ordinary semop() with no undo recorded, rather than a silently
 * wrong undo. */
#define SEM_UNDO 010000

#define GETNCNT 14
#define GETPID  11
#define GETVAL  12
#define GETALL  13
#define GETZCNT 15
#define SETVAL  16
#define SETALL  17

struct semid_ds {
	struct ipc_perm sem_perm;
	unsigned short sem_nsems;
	time_t sem_otime;
	time_t sem_ctime;
};

/* POSIX declares union semun application-supplied on some historical
 * implementations but requires it here, letting a caller pass a bare
 * int/pointer through semctl()'s "..." without defining its own union. */
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
