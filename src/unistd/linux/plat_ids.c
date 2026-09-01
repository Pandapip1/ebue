/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Real Linux backends for the four res*id() calls and euidaccess()/
 * eaccess(), all declared in include/unistd.h under _GNU_SOURCE and
 * marked "undefined-ok" there for NT specifically (see that header's
 * own updated comments, and syscall()'s comment in src/unistd/linux/
 * plat_unistd.c for the precedent this file follows): NT's single-
 * fixed-identity model (src/unistd/ids.c's own getuid()/geteuid()) has
 * no ruid/euid/suid triple to report, and no effective-vs-real access-
 * check distinction either -- both reasons stay true of, and are only
 * checked against, the NT build. Linux has real, distinct ruid/euid/
 * suid (setresuid(2)/getresuid(2)/setresgid(2)/getresgid(2)) and a real
 * effective-id access check (faccessat2(2)'s AT_EACCESS), so this
 * backend implements both for real rather than leaving Linux stuck with
 * NT's reasoning.
 *
 * Same discipline as every other Linux backend in this tree: raw
 * syscall(2) via `svc #0`, no host libc, aarch64 syscall numbers
 * confirmed against this host's own <asm-generic/unistd.h> (see
 * src/mman/linux/plat_mem.c's banner for the fuller rationale and why
 * `extern long syscall(long, ...)` cannot be reused here).
 *
 * getresuid()/getresgid() ask the kernel directly rather than going
 * through src/unistd/ids.c's own cached getuid()/getgid() (that cache
 * answers a single, front-door-modeled identity -- see ids.c's banner
 * -- and would give a stale or synthetic answer for the real ruid/euid/
 * suid triple this function has to report). setresuid()/setresgid(),
 * conversely, DO reach back into ids.c: a successful call can move this
 * process's real uid/gid out from under that cache, so both call
 * __ids_creds_cache_invalidate() (src/unistd/ids.c) on success -- see
 * that function's own comment.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "libc.h"

/* aarch64 Linux syscall numbers -- confirmed against this host's own
 * <asm-generic/unistd.h>, not assumed (see plat_mem.c's banner for why
 * these are hardcoded rather than pulled from a host header). */
#define SYS_setresuid   147
#define SYS_getresuid   148
#define SYS_setresgid   149
#define SYS_getresgid   150
#define SYS_faccessat2  439

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

int setresuid(uid_t ruid, uid_t euid, uid_t suid) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	long ret = raw_syscall(SYS_setresuid, (long)ruid, (long)euid, (long)suid, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	__ids_creds_cache_invalidate();
	return 0;
}

int setresgid(gid_t rgid, gid_t egid, gid_t sgid) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	long ret = raw_syscall(SYS_setresgid, (long)rgid, (long)egid, (long)sgid, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	__ids_creds_cache_invalidate();
	return 0;
}

/* ruid/euid/suid required: the kernel writes directly through all three
 * pointers on the success path with no NULL check of its own -- a NULL
 * argument here would be handed straight to the kernel as the
 * destination of a real write, faulting inside the syscall itself. Every
 * real call site in this tree (test/posix-unistd-ids.c) passes the
 * address of a real local, never NULL. */
int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
	long ret = raw_syscall(SYS_getresuid, (long)ruid, (long)euid, (long)suid, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* rgid/egid/sgid required: same reasoning as getresuid() above. */
int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
	long ret = raw_syscall(SYS_getresgid, (long)rgid, (long)egid, (long)sgid, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* euidaccess()/eaccess(): faccessat2(2) with AT_EACCESS asks the kernel
 * to run its real permission check against the caller's EFFECTIVE ids,
 * rather than the real ones access()/faccessat() (src/unistd/access.c)
 * use -- and, being a genuine kernel check, it is also strictly more
 * correct than that front door's own simplified owner-blind rwx-bit
 * test (see access.c's own comment: it never distinguishes owner from
 * group from other, and never checks R_OK at all). This asks the real
 * thing instead of approximating it.
 *
 * ENOSYS is real and reachable here: faccessat2(2) is a Linux 5.8+
 * addition, so a pre-5.8 kernel refuses the syscall outright rather
 * than merely mishandling AT_EACCESS. The fallback below reimplements
 * the same owner/group/other decision by hand against geteuid()/
 * getegid() (both real on this backend -- see src/unistd/linux/
 * plat_unistd.c's own __plat_detect_gid()) rather than getuid()/
 * getgid(), which is the entire point of the "e" in euidaccess(). */
static int manual_eaccess(const char *path, int mode)
{
	struct stat st;
	uid_t eu;
	gid_t eg;
	mode_t bits;

	if (stat(path, &st) < 0) return -1;
	eu = geteuid();
	if (eu == 0) {
		/* root: X_OK still needs at least one real execute bit set
		 * (the one exception access(2)'s own semantics carve out for
		 * root); R_OK/W_OK are unconditional. */
		if ((mode & X_OK) && !(st.st_mode & 0111)) { errno = EACCES; return -1; }
		return 0;
	}
	eg = getegid();
	if (st.st_uid == eu) bits = (mode_t)((st.st_mode >> 6) & 7);
	else if (st.st_gid == eg) bits = (mode_t)((st.st_mode >> 3) & 7);
	else bits = (mode_t)(st.st_mode & 7);
	if ((mode & R_OK) && !(bits & 4)) { errno = EACCES; return -1; }
	if ((mode & W_OK) && !(bits & 2)) { errno = EACCES; return -1; }
	if ((mode & X_OK) && !(bits & 1)) { errno = EACCES; return -1; }
	return 0;
}

int euidaccess(const char *path, int mode)
{
	long ret = raw_syscall(SYS_faccessat2, (long)AT_FDCWD, (long)path, (long)mode, (long)AT_EACCESS, 0L, 0L);
	if (!is_sys_error(ret)) return 0;
	if ((int)-ret != ENOSYS) { errno = (int)-ret; return -1; }
	return manual_eaccess(path, mode);
}

int eaccess(const char *path, int mode) { return euidaccess(path, mode); }

// NOLINTEND(misc-include-cleaner)
