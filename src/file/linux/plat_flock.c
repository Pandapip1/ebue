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

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/file.h>
#include <errno.h>
#include "plat_flock.h"

/* aarch64 Linux syscall number (arch/arm64/include/uapi/asm/unistd.h,
 * via the generic modern ABI's asm-generic/unistd.h) -- confirmed
 * against this host's own <sys/syscall.h> rather than assumed; see
 * plat_mem.c's banner for why this file cannot include that header
 * itself. */
#define SYS_flock 32

/* A minimal 6-argument raw syscall: `svc #0` directly, no host libc in
 * the call path at all -- NOT `extern long syscall(long, ...)`, which
 * is satisfied by the HOST's real glibc at link time in a non-
 * freestanding build and collapses every failure to exactly -1 with
 * glibc's OWN errno rather than the raw kernel -errno this file's
 * is_sys_error()/`errno = (int)-ret` translation requires -- see
 * src/mman/linux/plat_mem.c's fix (commit 299458a) for the fuller
 * account, confirmed independently across six other Linux backends.
 * aarch64's syscall calling convention: x8 = syscall number, x0..x5 =
 * up to 6 arguments, result (or -errno in [-4095,-1]) in x0. */
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

static int unbox(__plat_handle_t h)
{
	return (int)((long)h - 1);
}

int __plat_flock_lock(__plat_handle_t h, int nb, int exclusive)
{
	int op = (exclusive ? LOCK_EX : LOCK_SH) | (nb ? LOCK_NB : 0);
	long ret = raw_syscall(SYS_flock, (long)unbox(h), (long)op, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_flock_unlock(__plat_handle_t h)
{
	long ret = raw_syscall(SYS_flock, (long)unbox(h), (long)LOCK_UN, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

// NOLINTEND(misc-include-cleaner)
