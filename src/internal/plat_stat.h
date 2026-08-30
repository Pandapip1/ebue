/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform interface src/stat/{chmod,lxmod,mkdir,stat,statvfs,
 * utimensat}.c's POSIX-facing front doors call into instead of a raw
 * NtCreateFile/NtOpenFile/NtQueryInformationFile/NtSetInformationFile/
 * NtQueryVolumeInformationFile/NtQueryEaFile/NtSetEaFile/NtQueryObject
 * call.  See src/stat/nt/plat_stat.c for the implementations these
 * declare.
 *
 * Every function here takes POSIX-shaped arguments and returns a
 * POSIX-shaped result -- errno already set on failure, never a raw
 * platform status for the front door to interpret.
 *
 * As in src/internal/plat_fcntl.h, `struct __ntpath *` appears directly
 * in several signatures below.  Path resolution (src/internal/path.c)
 * is explicitly out of scope for this migration and stays exactly what
 * it always was -- an NT-specific step the front door performs once via
 * __ntpath_at()/__ntpath() before handing the resolved result to one of
 * these functions to open and act on.
 *
 * $LXMOD (WSL's NTFS extended attribute persisting a POSIX mode) is
 * this library's own choice of how to remember a mode NT has no native
 * concept of -- the same kind of front-door bookkeeping mman.c's
 * reservation table is (see plat_mem.h).  __lxmod_create_buffer()
 * (src/stat/lxmod.c) therefore stays a front-door function, declared in
 * src/internal/libc.h alongside the other cross-module helpers, not
 * here; only the two functions that actually read/write the EA over the
 * wire (NtQueryEaFile/NtSetEaFile) are platform calls and are declared
 * below.
 */
#ifndef _NTLIBC_PLAT_STAT_H
#define _NTLIBC_PLAT_STAT_H

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include "plat_handle.h"

struct __ntpath;

/* WSL/ntfs3's $LXMOD extended attribute: 1 found (*mode filled), 0
 * absent/invalid (nothing this library can use; not an error). */
int __plat_lxmod_get(__plat_handle_t h, unsigned *mode);
/* Write $LXMOD. 0/-1(errno). */
int __plat_lxmod_set(__plat_handle_t h, unsigned mode);

/* chmod() on an already-open handle: sets FILE_ATTRIBUTE_READONLY to
 * match the aggregate write bits and writes $LXMOD -- see chmod.c's own
 * banner. 0/-1(errno). */
int __plat_chmod(__plat_handle_t h, mode_t mode);
/* fchmodat(): resolves `np` to a handle (with the FILE_WRITE_ATTRIBUTES-
 * denied-on-a-read-only-file Wine fallback chmod.c's own comment
 * documents), calls __plat_chmod(), and closes it. 0/-1(errno). */
int __plat_chmodat(struct __ntpath *np, int flags, mode_t mode);

/* mkdirat(): creates the directory named by the already-resolved `np`
 * with the $LXMOD `ea`/`ea_len` buffer, mapping NT's name-collision
 * status to EEXIST -- see mkdir.c's own banner on why that status,
 * rather than a type-mismatch one, is what NT actually reports here.
 * 0/-1(errno). */
int __plat_mkdir(struct __ntpath *np, void *ea, unsigned ea_len);

/* The guts of stat()/fstat(): fill *st from an open handle of __FD_*
 * type `type` -- see stat.c's own banner for the $LXMOD/compatibility-
 * default mode policy and the synthetic st_dev/st_ino this assigns a
 * pipe/console/char/unknown handle. 0/-1(errno). */
int __plat_fstat(__plat_handle_t h, int type, struct stat *st);
/* fstatat(): resolves `np` to a handle (with stat.c's own reparse-tag/
 * permission fallback cascade) and calls __plat_fstat(). 0/-1(errno). */
int __plat_fstatat(struct __ntpath *np, int flags, struct stat *st);

/* fstatvfs() on an already-open handle -- see statvfs.c's own banner
 * for the field-by-field derivation. 0/-1(errno). */
int __plat_statvfs(__plat_handle_t h, struct statvfs *buf);
/* statvfs(): resolves `np` to a handle and calls __plat_statvfs().
 * 0/-1(errno). */
int __plat_statvfs_path(struct __ntpath *np, struct statvfs *buf);

/* futimens() on an already-open handle: the Wine READONLY-clearing
 * workaround utimensat.c's own comment documents lives here.
 * 0/-1(errno). */
int __plat_set_times(__plat_handle_t h, const struct timespec ts[2]);
/* utimensat(): resolves `np` to a handle (with the FILE_WRITE_ATTRIBUTES-
 * denied-on-a-read-only-file Wine fallback utimensat.c's own comment
 * documents) and calls __plat_set_times(). 0/-1(errno). */
int __plat_set_times_at(struct __ntpath *np, int flags, const struct timespec ts[2]);

#endif
