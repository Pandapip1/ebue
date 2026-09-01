/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of <sys/shm.h>, <sys/msg.h> and <sys/sem.h>:
 * shmget()/shmat()/shmdt()/shmctl(), msgget()/msgsnd()/msgrcv()/
 * msgctl(), semget()/semop()/semctl().
 *
 * System V IPC is a native Linux kernel facility -- shmget(2), msgget(2)
 * and semget(2) really are syscalls, with a real in-kernel identifier
 * table behind them -- so unlike src/ipc/nt/ (which has to build that
 * table itself out of a backing file, because NT has none), this file
 * is eleven THIN wrappers, each one raw syscall, issued directly via
 * syscall(2) rather than through any host libc wrapper, for exactly the
 * reason src/mman/linux/plat_mem.c's own banner gives at length for the
 * identical technique: this file is compiled under -nostdinc against
 * ntlibc's OWN generated headers, never glibc's, and the final link
 * step still pulls in the host's real glibc, whose extern syscall(3)
 * wrapper does its own errno translation into a DIFFERENT memory
 * location than ntlibc's own errno global. Calling it here would silently
 * misreport every failure. is_sys_error()/raw_syscall() below are copied
 * from that file's own vetted implementation rather than re-derived.
 *
 * ALL SIX PUBLIC STRUCTS (struct sembuf and the raw kernel-ABI k_*
 * structs below) ARE PART OF THE KERNEL'S SYSCALL ABI, NOT A CHOICE THIS
 * FILE MADE. struct sembuf is already laid out identically to the
 * kernel's -- see <sys/sem.h>'s own comment -- so semop() passes an
 * application's array straight through unmarshalled. struct msgbuf
 * (a caller struct beginning with `long mtype`) is likewise the exact
 * ABI msgsnd(2)/msgrcv(2) expect, so those two also pass their buffer
 * pointer straight through. shmid_ds/msqid_ds/semid_ds are NOT -- POSIX's
 * shape (this header's own struct shmid_ds etc.) is not the kernel's
 * internal shmid64_ds/msqid64_ds/semid64_ds (asm-generic/{shmbuf,msgbuf,
 * sembuf}.h, confirmed against this host's own kernel headers rather
 * than assumed, the same discipline plat_mem.c's own banner insists on
 * for its syscall numbers) -- so IPC_STAT/IPC_SET/GETALL/SETALL DO need
 * one field-by-field translation each, done by to_user_*()/to_kernel_*()
 * below.
 *
 * aarch64 (and every other architecture without the legacy ipc(2)
 * multiplexer -- CONFIG_ARCH_WANT_IPC_PARSE_VERSION unset) always
 * speaks the "new" 64-bit ABI on these direct syscalls; there is no
 * IPC_64 flag to OR into cmd the way i386's ipc() dispatch needs, so
 * none is added here.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <stdarg.h>
#include <errno.h>

/* aarch64 Linux syscall numbers (arch/arm64/include/uapi/asm/unistd.h,
 * via the generic modern ABI's asm-generic/unistd.h) -- confirmed
 * against this host's own kernel headers, the same discipline
 * plat_mem.c's own banner describes. */
#define SYS_msgget     186
#define SYS_msgctl     187
#define SYS_msgrcv     188
#define SYS_msgsnd     189
#define SYS_semget     190
#define SYS_semctl     191
#define SYS_semop      193
#define SYS_shmget     194
#define SYS_shmctl     195
#define SYS_shmat      196
#define SYS_shmdt      197

/* A minimal 6-argument raw syscall: `svc #0` directly, no host libc in
 * the call path at all. See plat_mem.c's own comment on why this is a
 * static local rather than `extern long syscall(long, ...)`: aarch64's
 * syscall calling convention is x8 = syscall number, x0..x5 = up to 6
 * arguments, result (or -errno in [-4095,-1]) in x0. */
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6) // NOLINT(bugprone-easily-swappable-parameters) -- raw syscall ABI slots are positional and semantically distinct
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a1;
	register long x1 __asm__("x1") = a2;
	register long x2 __asm__("x2") = a3;
	register long x3 __asm__("x3") = a4;
	register long x4 __asm__("x4") = a5;
	register long x5 __asm__("x5") = a6;
	__asm__ volatile("svc #0"
		: "+r"(x0)
		: "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
		: "memory", "cc");
	return x0;
}

static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

/* asm-generic/ipcbuf.h's struct ipc64_perm. __kernel_mode_t is 4 bytes
 * on every architecture this header would need to know about (it is
 * `unsigned int` in asm-generic/posix_types.h), so the kernel struct's
 * own `__pad1[4 - sizeof(__kernel_mode_t)]` is a zero-length array --
 * omitted here entirely rather than spelled out as an invalid C array,
 * with the same total size and member offsets (natural alignment, no
 * packing, matching how the kernel header itself is compiled). */
struct k_ipc64_perm {
	int key;
	unsigned uid, gid, cuid, cgid;
	unsigned mode;
	unsigned short seq;
	unsigned short __pad2;
	unsigned long __unused1;
	unsigned long __unused2;
};

/* asm-generic/shmbuf.h's struct shmid64_ds, __BITS_PER_LONG == 64 half. */
struct k_shmid64_ds {
	struct k_ipc64_perm shm_perm;
	unsigned long shm_segsz;
	long shm_atime;
	long shm_dtime;
	long shm_ctime;
	int shm_cpid;
	int shm_lpid;
	unsigned long shm_nattch;
	unsigned long __unused4;
	unsigned long __unused5;
};

/* asm-generic/msgbuf.h's struct msqid64_ds, __BITS_PER_LONG == 64 half. */
struct k_msqid64_ds {
	struct k_ipc64_perm msg_perm;
	long msg_stime;
	long msg_rtime;
	long msg_ctime;
	unsigned long msg_cbytes;
	unsigned long msg_qnum;
	unsigned long msg_qbytes;
	int msg_lspid;
	int msg_lrpid;
	unsigned long __unused4;
	unsigned long __unused5;
};

/* asm-generic/sembuf.h's struct semid64_ds, __BITS_PER_LONG == 64 half. */
struct k_semid64_ds {
	struct k_ipc64_perm sem_perm;
	long sem_otime;
	long sem_ctime;
	unsigned long sem_nsems;
	unsigned long __unused3;
	unsigned long __unused4;
};

static void perm_to_user(struct ipc_perm *u, const struct k_ipc64_perm *k)
{
	u->uid = (uid_t)k->uid;
	u->gid = (gid_t)k->gid;
	u->cuid = (uid_t)k->cuid;
	u->cgid = (gid_t)k->cgid;
	u->mode = (mode_t)k->mode;
}

static void perm_to_kernel(struct k_ipc64_perm *k, const struct ipc_perm *u)
{
	k->uid = (unsigned)u->uid;
	k->gid = (unsigned)u->gid;
	k->cuid = (unsigned)u->cuid;
	k->cgid = (unsigned)u->cgid;
	k->mode = (unsigned)u->mode;
}

/* ---- <sys/shm.h> --------------------------------------------------- */

int shmget(key_t key, size_t size, int shmflg)
{
	long ret = raw_syscall(SYS_shmget, (long)key, (long)size, (long)shmflg, 0, 0, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

void *shmat(int shmid, const void *shmaddr, int shmflg)
{
	long ret = raw_syscall(SYS_shmat, (long)shmid, (long)shmaddr, (long)shmflg, 0, 0, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return (void *)-1; }
	return (void *)ret;
}

int shmdt(const void *shmaddr)
{
	long ret = raw_syscall(SYS_shmdt, (long)shmaddr, 0, 0, 0, 0, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
	struct k_shmid64_ds k;
	long ret;

	if (cmd == IPC_SET) {
		if (!buf) { errno = EFAULT; return -1; }
		__builtin_memset(&k, 0, sizeof k);
		perm_to_kernel(&k.shm_perm, &buf->shm_perm);
		ret = raw_syscall(SYS_shmctl, (long)shmid, (long)cmd, (long)&k, 0, 0, 0);
		if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
		return 0;
	}
	if (cmd == IPC_STAT) {
		if (!buf) { errno = EFAULT; return -1; }
		__builtin_memset(&k, 0, sizeof k);
		ret = raw_syscall(SYS_shmctl, (long)shmid, (long)cmd, (long)&k, 0, 0, 0);
		if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
		perm_to_user(&buf->shm_perm, &k.shm_perm);
		buf->shm_segsz = (size_t)k.shm_segsz;
		buf->shm_lpid = (pid_t)k.shm_lpid;
		buf->shm_cpid = (pid_t)k.shm_cpid;
		buf->shm_nattch = (shmatt_t)k.shm_nattch;
		buf->shm_atime = (time_t)k.shm_atime;
		buf->shm_dtime = (time_t)k.shm_dtime;
		buf->shm_ctime = (time_t)k.shm_ctime;
		return 0;
	}
	/* IPC_RMID and every ipcs(1)-only cmd (SHM_LOCK, SHM_UNLOCK, ...)
	 * take no translated buffer at all; forward buf verbatim (IPC_RMID
	 * ignores it). */
	ret = raw_syscall(SYS_shmctl, (long)shmid, (long)cmd, (long)buf, 0, 0, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

/* ---- <sys/msg.h> ----------------------------------------------------- */

int msgget(key_t key, int msgflg)
{
	long ret = raw_syscall(SYS_msgget, (long)key, (long)msgflg, 0, 0, 0, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

int msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg)
{
	long ret = raw_syscall(SYS_msgsnd, (long)msqid, (long)msgp, (long)msgsz, (long)msgflg, 0, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

ssize_t msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg)
{
	long ret = raw_syscall(SYS_msgrcv, (long)msqid, (long)msgp, (long)msgsz, msgtyp, (long)msgflg, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (ssize_t)ret;
}

int msgctl(int msqid, int cmd, struct msqid_ds *buf)
{
	struct k_msqid64_ds k;
	long ret;

	if (cmd == IPC_SET) {
		if (!buf) { errno = EFAULT; return -1; }
		__builtin_memset(&k, 0, sizeof k);
		perm_to_kernel(&k.msg_perm, &buf->msg_perm);
		k.msg_qbytes = (unsigned long)buf->msg_qbytes;
		ret = raw_syscall(SYS_msgctl, (long)msqid, (long)cmd, (long)&k, 0, 0, 0);
		if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
		return 0;
	}
	if (cmd == IPC_STAT) {
		if (!buf) { errno = EFAULT; return -1; }
		__builtin_memset(&k, 0, sizeof k);
		ret = raw_syscall(SYS_msgctl, (long)msqid, (long)cmd, (long)&k, 0, 0, 0);
		if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
		perm_to_user(&buf->msg_perm, &k.msg_perm);
		buf->msg_qnum = (msgqnum_t)k.msg_qnum;
		buf->msg_qbytes = (msglen_t)k.msg_qbytes;
		buf->msg_lspid = (pid_t)k.msg_lspid;
		buf->msg_lrpid = (pid_t)k.msg_lrpid;
		buf->msg_stime = (time_t)k.msg_stime;
		buf->msg_rtime = (time_t)k.msg_rtime;
		buf->msg_ctime = (time_t)k.msg_ctime;
		return 0;
	}
	ret = raw_syscall(SYS_msgctl, (long)msqid, (long)cmd, (long)buf, 0, 0, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

/* ---- <sys/sem.h> ----------------------------------------------------- */

int semget(key_t key, int nsems, int semflg)
{
	long ret = raw_syscall(SYS_semget, (long)key, (long)nsems, (long)semflg, 0, 0, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

int semop(int semid, struct sembuf *sops, size_t nsops)
{
	long ret = raw_syscall(SYS_semop, (long)semid, (long)sops, (long)nsops, 0, 0, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* semctl.html: "If cmd is IPC_STAT, IPC_SET, GETALL, or SETALL, the
 * fourth argument shall be of type union semun." SETVAL's argument is
 * an int; every other cmd (GETVAL/GETPID/GETNCNT/GETZCNT/IPC_RMID) takes
 * no fourth argument at all, so va_arg() is only ever reached on a cmd
 * that actually supplied one. */
int semctl(int semid, int semnum, int cmd, ...)
{
	struct k_semid64_ds k;
	unsigned short *array;
	long ret;
	va_list ap;

	switch (cmd) {
	case IPC_SET: {
		struct semid_ds *buf;
		va_start(ap, cmd); buf = va_arg(ap, struct semid_ds *); va_end(ap);
		if (!buf) { errno = EFAULT; return -1; }
		__builtin_memset(&k, 0, sizeof k);
		perm_to_kernel(&k.sem_perm, &buf->sem_perm);
		ret = raw_syscall(SYS_semctl, (long)semid, (long)semnum, (long)cmd, (long)&k, 0, 0);
		if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
		return 0;
	}
	case IPC_STAT: {
		struct semid_ds *buf;
		va_start(ap, cmd); buf = va_arg(ap, struct semid_ds *); va_end(ap);
		if (!buf) { errno = EFAULT; return -1; }
		__builtin_memset(&k, 0, sizeof k);
		ret = raw_syscall(SYS_semctl, (long)semid, (long)semnum, (long)cmd, (long)&k, 0, 0);
		if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
		perm_to_user(&buf->sem_perm, &k.sem_perm);
		buf->sem_nsems = (unsigned short)k.sem_nsems;
		buf->sem_otime = (time_t)k.sem_otime;
		buf->sem_ctime = (time_t)k.sem_ctime;
		return 0;
	}
	case GETALL:
	case SETALL:
		va_start(ap, cmd); array = va_arg(ap, unsigned short *); va_end(ap);
		if (!array) { errno = EFAULT; return -1; }
		ret = raw_syscall(SYS_semctl, (long)semid, (long)semnum, (long)cmd, (long)array, 0, 0);
		if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
		return (int)ret;
	case SETVAL: {
		int val;
		va_start(ap, cmd); val = va_arg(ap, int); va_end(ap);
		ret = raw_syscall(SYS_semctl, (long)semid, (long)semnum, (long)cmd, (long)val, 0, 0);
		if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
		return 0;
	}
	default:
		/* GETVAL, GETPID, GETNCNT, GETZCNT, IPC_RMID, and every
		 * ipcs(1)-only cmd: no fourth argument. */
		ret = raw_syscall(SYS_semctl, (long)semid, (long)semnum, (long)cmd, 0, 0, 0);
		if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
		return (int)ret;
	}
}
// NOLINTEND(misc-include-cleaner)
