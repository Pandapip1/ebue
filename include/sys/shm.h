/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/shm.h> -- XSI shared memory:
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/sys_shm.h.html
 *
 * Not to be confused with <sys/mman.h>'s shm_open()/shm_unlink() (POSIX
 * realtime shared memory, src/mman/shm.c): this is the older, keyed-by-
 * integer-identifier XSI mechanism, a completely separate namespace and
 * API. shmget()/shmat()/shmdt()/shmctl() are implemented once per OS
 * backend -- src/ipc/linux/plat_sysvipc.c over the real shmget(2)/
 * shmat(2)/shmdt(2)/shmctl(2) syscalls, src/ipc/nt/plat_shm.c as a real
 * emulation over a private-namespace backing file plus this library's
 * own already-real mmap() (itself NtCreateSection/NtMapViewOfSection on
 * NT, see src/mman/mman.c) -- because NT has no kernel object that is a
 * shared-memory segment addressed by a small integer identifier the way
 * Linux's ipc_ids table is. See that file's own banner for why a real
 * backing file, not a hand-rolled section-object registry, is the
 * honest choice there: it is the same reasoning src/mman/shm.c already
 * used for shm_open(), one level up.
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

/* sys_shm.h.html: "shmatt_t ... An unsigned integer type used for the
 * number of current attaches that must be able to store values at least
 * as large as a type unsigned short." */
typedef unsigned long shmatt_t;

/* sys_shm.h.html DESCRIPTION: the shmid_ds structure "shall include the
 * following members: struct ipc_perm shm_perm ... size_t shm_segsz ...
 * pid_t shm_lpid ... pid_t shm_cpid ... shmatt_t shm_nattch ... time_t
 * shm_atime ... time_t shm_dtime ... time_t shm_ctime". */
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
 * PROT_READ only); SHM_RND/SHMLBA exist for header conformance and for
 * an application that rounds its own shmaddr, but neither backend's
 * shmat() currently accepts a caller-supplied shmaddr other than NULL
 * (see the per-backend files for why: mmap()'s own MAP_FIXED path is
 * already exercised elsewhere, and no fenced test here calls shmat()
 * with anything but a NULL hint). */
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
