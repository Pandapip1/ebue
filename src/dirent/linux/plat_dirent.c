/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_dirent.h -- see that
 * header, and src/mman/linux/plat_mem.c's/src/unistd/linux/plat_fd.c's
 * own banners for the general discipline this file follows (raw
 * syscall(2), no host libc, -nostdinc against ntlibc's own headers,
 * aarch64 syscall numbers confirmed against this host).
 *
 * src/dirent/readdir.c's __dirstream_next() and src/dirent/getdents.c's
 * getdents() decode through the backend-neutral __plat_dir_decode_one()
 * (src/internal/plat_dirent.h) rather than hardcoding NT's
 * FILE_ID_BOTH_DIR_INFORMATION, so this backend just needs its own real
 * getdents64(2) plus a decoder for linux_dirent64 -- no separate front
 * door required.
 *
 * linux_dirent64's exact layout (d_ino: u64 @0, d_off: u64 @8, d_reclen:
 * u16 @16, d_type: u8 @18, d_name[]: NUL-terminated @19, chained by
 * d_reclen rather than a separate NextEntryOffset field) was confirmed
 * against this host, not assumed -- a throwaway oracle program called
 * the real getdents64(2) syscall on a real directory containing a
 * regular file and a subdirectory, printed offsetof() for a hand-rolled
 * guess at the struct, and walked the real returned bytes by that
 * guess's d_reclen field end to end, landing on exactly the file names
 * created and readable NUL-terminated strings at every step -- not just
 * a layout that compiles, one that a real kernel's own bytes agree with.
 * The same program also printed glibc's own <dirent.h> struct dirent64's
 * offsetof()s as a second, independent cross-check: identical numbers.
 *
 * d_type's values need NO translation table, unlike NT's FileAttributes:
 * the real kernel's d_type field already uses the exact same numeric
 * convention as dirent.h's own DT_* constants (DT_REG=8, DT_DIR=4, etc
 * -- confirmed by the same oracle program, which printed both the raw
 * d_type byte for each entry AND glibc's DT_REG/DT_DIR macro values
 * side by side: 8 and 4 respectively, matching). This is not a
 * coincidence this file is relying on -- IFTODT()/DTTOIF() in ntlibc's
 * own <dirent.h> ((x)>>12 & 017) are the textbook Linux d_type
 * derivation from a raw inode mode's S_IFMT bits, which is where these
 * numbers come from on a real kernel in the first place. A directory
 * entry on a filesystem that does not report d_type natively (some
 * network/FUSE filesystems) comes back as DT_UNKNOWN (0) from the
 * kernel itself; passed through unchanged here rather than "fixed" with
 * a synthetic fstatat() per entry, matching glibc's own readdir()
 * behavior for the same case (see readdir(3), "d_type... may be
 * DT_UNKNOWN").
 *
 * rewinddir()'s effect (`restart`) has no getdents64(2)-level flag the
 * way NT's RestartScan is a direct argument to NtQueryDirectoryFile:
 * Linux directory fds instead expose the same lseek(2) interface a
 * regular file fd does, and lseek(fd, 0, SEEK_SET) is the documented way
 * to rewind a directory stream's read position back to the beginning
 * (see readdir(3)/rewinddir(3), and glibc's own rewinddir()
 * implementation, which does exactly this on Linux). One extra syscall
 * versus NT's single combined call, only paid when `restart` is set.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <errno.h>
#include <string.h>
#include "plat_dirent.h"
#include "ownership_stubs.h"

/* aarch64 Linux syscall numbers -- see plat_mem.c's banner for why
 * these are hardcoded rather than pulled from a host header.
 * getdents64 = 61 (confirmed against this host's own asm-generic/
 * unistd.h and via this file's own oracle program using glibc's
 * SYS_getdents64 macro -- both agree). lseek = 62, matching
 * src/unistd/linux/plat_fd.c's own SYS_lseek. */
#define SYS_getdents64 61
#define SYS_lseek      62

/* A minimal 6-argument raw syscall: `svc #0` directly, no host libc in
 * the call path at all -- see src/mman/linux/plat_mem.c's own banner
 * for the fuller account of why `extern long syscall(long, ...)` is
 * wrong here (glibc's own syscall() translates the raw kernel -errno
 * convention into -1-with-its-own-errno-set, breaking this file's
 * is_sys_error()/`errno = (int)-ret` translation). aarch64's syscall
 * calling convention: x8 = syscall number, x0..x5 = up to 6 arguments,
 * result (or -errno in [-4095,-1]) in x0. */
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

ssize_t __plat_dir_read(__plat_handle_t h, void *buf, size_t bufsize, int restart) // NOLINT(bugprone-easily-swappable-parameters) -- fixed platform-backend contract; parameter names distinguish roles
{
	int fd = unbox(h);
	long ret;

	if (restart) {
		/* See this file's own banner: lseek(fd, 0, SEEK_SET) is the
		 * real Linux idiom for rewinding a directory stream, there
		 * being no per-call restart flag on getdents64(2) itself. */
		ret = raw_syscall(SYS_lseek, (long)fd, 0L, 0L /* SEEK_SET */, 0L, 0L, 0L);
		if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	}

	ret = raw_syscall(SYS_getdents64, (long)fd, (long)buf, (long)bufsize, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (ssize_t)ret; /* 0 at end-of-directory already matches
	                      * getdents64(2)'s own convention -- no
	                      * STATUS_NO_MORE_FILES-style translation
	                      * needed, unlike NT's __plat_dir_read(). */
}

/* The kernel's raw linux_dirent64 -- confirmed field-for-field against
 * this host, see this file's own banner. Declared with a flexible array
 * member (d_name[]) rather than a fixed-size buffer: the compiler's own
 * natural (unpacked) layout for u64/u64/u16/u8/char[] already puts
 * d_name at offset 19 with no padding, matching the real kernel layout
 * exactly, so no explicit packing pragma is needed either. */
struct __lx_dirent64 { // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling mirrors the Linux kernel ABI layout
	unsigned long long d_ino;
	long long d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[];
};

int __plat_dir_decode_one(const void *buf, size_t buflen, size_t *pos, struct __dirent_raw *out)
{
	const struct __lx_dirent64 *d;
	size_t namecap = sizeof out->name - 1;
	size_t i;

	if (*pos >= buflen) return 0;
	/* d->d_ino below is a disclosed, deliberately unmarked residual:
	 * d is `buf + *pos`, a local computed by pointer arithmetic, not
	 * a parameter of this function -- buf itself is already required
	 * (see src/internal/plat_dirent.h's own comment) and there is no
	 * signature for `nonnull` to describe a further-derived local on,
	 * the same "struct/local-derived pointer, not a parameter" class
	 * crt/delayload2.c's own find_mapped_module() comment already
	 * established. Verified sound by hand regardless: `*pos < buflen`
	 * just above proves this offset is within buf's own real,
	 * getdents64(2)-filled extent, and buf itself is real (never NULL)
	 * per plat_dirent.h's own comment. */
	d = (const struct __lx_dirent64 *)((const unsigned char *)buf + *pos);

	/* Zeroed up front so every byte of `out`, including name[] past the
	 * NUL terminator written below, is well-defined -- the caller
	 * memcpy()s this whole struct (readdir.c's make_real(), getdents.c's
	 * own copy), and this file has no business handing back uninitialized
	 * stack bytes through it. See src/dirent/nt/plat_dirent.c's own
	 * __plat_dir_decode_one() for the fuller reasoning (same fix,
	 * same struct, independently needed here). */
	memset(out, 0, sizeof *out);

	out->ino = (ino_t)d->d_ino;
	out->type = d->d_type; /* already the same DT_* numeric convention as
	                        * dirent.h's own constants -- see this file's
	                        * own banner; no translation table needed. */

	/* d_name is already NUL-terminated by the kernel, but this is
	 * untrusted kernel-filled memory this file has no independent
	 * length bound on beyond d_reclen itself, so it is copied by hand
	 * rather than trusted to a bare strcpy -- same caution
	 * __utf16_to_utf8_buf() applies on the NT side for the same
	 * reason (a name this backend did not itself bound-check). */
	for (i = 0; i < namecap && d->d_name[i]; i++)
		out->name[i] = d->d_name[i];
	out->name[i] = 0;

	*pos += d->d_reclen;
	return 1;
}

// NOLINTEND(misc-include-cleaner)
