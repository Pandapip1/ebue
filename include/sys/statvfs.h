/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* Which members carry a meaningful value on this platform, and which
 * cannot, is documented in src/stat/statvfs.c's banner (POSIX leaves
 * this unspecified per file system). */

#ifndef _SYS_STATVFS_H
#define _SYS_STATVFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_fsblkcnt_t
#define __NEED_fsfilcnt_t

#include <bits/alltypes.h>

struct statvfs {
	unsigned long f_bsize;    /* file system block size */
	unsigned long f_frsize;   /* fundamental file system block size */
	fsblkcnt_t f_blocks;      /* total blocks, in units of f_frsize */
	fsblkcnt_t f_bfree;       /* total free blocks */
	fsblkcnt_t f_bavail;      /* free blocks available to a non-privileged process */
	fsfilcnt_t f_files;       /* total file serial numbers */
	fsfilcnt_t f_ffree;       /* total free file serial numbers */
	fsfilcnt_t f_favail;      /* file serial numbers available to a non-privileged process */
	unsigned long f_fsid;     /* file system ID */
	unsigned long f_flag;     /* bit mask of ST_* values */
	unsigned long f_namemax;  /* maximum filename length */
};

/* f_flag bits (basedefs/sys_statvfs.h.html) */
#define ST_RDONLY 1  /* read-only file system */
#define ST_NOSUID 2  /* does not support the ST_ISUID/ST_ISGID mode-bit semantics */

int statvfs(const char *__restrict, struct statvfs *__restrict);
int fstatvfs(int, struct statvfs *);

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
