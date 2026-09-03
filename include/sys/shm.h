/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/shm.h> -- XSI shared memory.
 *
 * Not to be confused with <sys/mman.h>'s shm_open()/shm_unlink() (POSIX
 * realtime shared memory): this is the older, keyed-by-integer-identifier
 * XSI mechanism, a completely separate namespace and API. NT has no
 * kernel object that is a shared-memory segment addressed by a small
 * integer identifier the way Linux's ipc_ids table is, so
 * src/ipc/nt/plat_shm.c emulates it over a private-namespace backing file
 * plus this library's own mmap().
 */
#ifndef _SYS_SHM_H
#define _SYS_SHM_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <sys/ipc.h>

#define __NEED_size_t
#define __NEED_pid_t
#define __NEED_time_t

#include <bits/alltypes.h>

typedef unsigned long shmatt_t;

struct shmid_ds {
	struct ipc_perm shm_perm;
	size_t shm_segsz;
	pid_t shm_lpid;
	pid_t shm_cpid;
	shmatt_t shm_nattch;
	time_t shm_atime;
	time_t shm_dtime;
	time_t shm_ctime;
};

/* shmat() shmflg. SHM_RDONLY is honoured by both backends (mapped
 * PROT_READ only); SHM_RND/SHMLBA exist for header conformance, but
 * neither backend's shmat() currently accepts a caller-supplied shmaddr
 * other than NULL. */
#define SHM_RDONLY 010000
#define SHM_RND    020000
#define SHMLBA     4096

void *shmat(int, const void *, int);
int shmctl(int, int, struct shmid_ds *);
int shmdt(const void *);
int shmget(key_t, size_t, int);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
