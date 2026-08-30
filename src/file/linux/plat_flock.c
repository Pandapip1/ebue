/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_flock.h -- see that header
 * for the contract each function makes, and src/mman/linux/plat_mem.c's
 * banner for the general discipline this file follows (raw syscall(2),
 * no host libc, -nostdinc against ntlibc's own headers, aarch64
 * syscall numbers confirmed against this host's own <sys/syscall.h> as
 * an oracle).
 *
 * Both functions here operate purely on an already-open __plat_handle_t
 * -- src/file/flock.c's flock() never resolves a path itself, it only
 * ever reaches into the fd table -- so unlike src/fcntl/linux/
 * plat_fcntl.c's __plat_create_file() or src/stat/linux/plat_stat.c's
 * *_at() functions, there is no NT-only path-resolution gap here at
 * all: this is fully portable.
 *
 * Unlike NT, which has no native "lock the whole open file description"
 * primitive and has to fake it with a byte-range NtLockFile()/
 * NtUnlockFile() pair over [0, LLONG_MAX) (see src/file/nt/
 * plat_flock.c's own banner for the two Wine landmines that dance works
 * around), Linux's flock(2) IS the exact whole-open-file-description
 * lock POSIX's flock() describes -- a single native syscall, no
 * emulation, no landmines to work around. LOCK_SH/LOCK_EX/LOCK_NB/
 * LOCK_UN are ntlibc's own <sys/file.h> values, unchanged: they already
 * match the Linux kernel ABI exactly (1/2/4/8, the same values every
 * Linux libc uses), so this needs no translation table either -- the
 * same "already matches the ABI" situation plat_mem.c's own banner
 * describes for PROT_/MAP_.
 */
#include <sys/file.h>
#include <errno.h>
#include "plat_flock.h"

/* aarch64 Linux syscall number (arch/arm64/include/uapi/asm/unistd.h,
 * via the generic modern ABI's asm-generic/unistd.h) -- confirmed
 * against this host's own <sys/syscall.h> rather than assumed; see
 * plat_mem.c's banner for why this file cannot include that header
 * itself. */
#define SYS_flock 32

extern long syscall(long number, ...);

static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

static int unbox(__plat_handle_t h)
{
	return (int)((long)h - 1);
}

int __plat_flock_lock(__plat_handle_t h, int nb, int exclusive)
{
	int op = (exclusive ? LOCK_EX : LOCK_SH) | (nb ? LOCK_NB : 0);
	long ret = syscall(SYS_flock, unbox(h), (long)op);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_flock_unlock(__plat_handle_t h)
{
	long ret = syscall(SYS_flock, unbox(h), (long)LOCK_UN);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}
