/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform interface src/fcntl/{open,fcntl,fadvise}.c's POSIX-
 * facing front doors call into instead of a raw NtCreateFile/NtLockFile/
 * NtUnlockFile/NtQueryInformationFile/NtSetInformationFile/
 * NtQueryVolumeInformationFile call.  See src/fcntl/nt/plat_fcntl.c for
 * the implementations these declare.
 *
 * Every function here takes POSIX-shaped arguments and returns a
 * POSIX-shaped result -- errno already set on failure, never a raw
 * platform status for the front door to interpret (posix_fallocate()'s
 * own XSI convention -- returning the error NUMBER, not -1/errno -- is
 * the one documented exception below, and is the front door's own
 * adaptation of that result, not a different backend contract).
 *
 * `struct __ntpath *` appears directly in two signatures below rather
 * than something more abstract.  That is deliberate, not an oversight:
 * path resolution (src/internal/path.c) is explicitly out of scope for
 * this migration and stays exactly what it always was -- a step that
 * only NT (or a close Windows relative) could ever need in the first
 * place, done once by the front door via __ntpath_at()/__ntpath() -- so
 * there is no POSIX-shaped way to describe "a resolved path" to hand a
 * future backend; a non-NT backend will need an entirely different
 * front door for path resolution regardless of anything decided here.
 */
#ifndef _NTLIBC_PLAT_FCNTL_H
#define _NTLIBC_PLAT_FCNTL_H

#include <sys/types.h>
#include "plat_handle.h"

struct __ntpath;

/* open()/openat(): translate `flags`/`mode` into NT's own
 * DesiredAccess/CreateDisposition/CreateOptions/FileAttributes and
 * call NtCreateFile against the already-resolved `np` -- see
 * src/fcntl/open.c's own banner for the access-mode/synchronous/share-
 * mode policy this encodes.  `mode` is the already-umask-applied
 * creation mode (0 unless O_CREAT is set, meaningful here only for the
 * FILE_ATTRIBUTE_READONLY decision); `ea`/`ea_len` is the $LXMOD
 * extended-attribute buffer (__lxmod_create_buffer(),
 * src/stat/lxmod.c) when O_CREAT is set, NULL/0 otherwise.
 *
 * Two NT-specific special cases live here, not in the front door: a
 * directory opened for reading without O_DIRECTORY (POSIX-legal; reads
 * then fail with EISDIR) needs a second NtCreateFile with different
 * access/options after the first refuses FILE_NON_DIRECTORY_FILE
 * against a directory, and NtCreateFile's own name-collision status
 * (FILE_CREATE against an existing name) reports as EEXIST rather than
 * through the generic status table.
 *
 * On success, *out/*typeout are filled (*typeout is __FD_DIR when the
 * result is a directory, 0 otherwise) and 0 is returned.  On failure,
 * -1/errno. */
int __plat_create_file(struct __ntpath *np, int flags, unsigned mode,
                        void *ea, unsigned ea_len,
                        __plat_handle_t *out, int *typeout);

/* fcntl(F_GETLK): does a lock of the given range/exclusivity conflict
 * with anything already held?  NT has no separate "would this
 * succeed" query -- NtLockFile IS the test, immediately released again
 * on success (see fcntl.c's record_lock()).  0 with *conflicting=0
 * means the probe round-tripped clean (no conflict; the caller reports
 * F_UNLCK); 0 with *conflicting=1 means NT refused with a lock-
 * conflict status (the caller reports l_pid=-1: NT does not expose an
 * owning process for a byte-range lock); -1/errno is a genuine
 * failure, neither of the above. */
int __plat_lock_probe(__plat_handle_t h, long long off, long long len, int exclusive, int *conflicting);

/* fcntl(F_SETLK/F_SETLKW): places a byte-range lock over [off,off+len).
 * `wait` nonzero selects F_SETLKW's blocking behaviour. 0/-1(errno). */
int __plat_lock_set(__plat_handle_t h, long long off, long long len, int exclusive, int wait);

/* fcntl(F_SETLK with F_UNLCK): removes a byte-range lock over
 * [off,off+len). 0/-1(errno). */
int __plat_lock_clear(__plat_handle_t h, long long off, long long len);

/* posix_fallocate()'s [EFBIG]: the volume's maximum representable file
 * size -- see fadvise.c's own banner for why this must be computed
 * rather than queried directly (NT reports "no room", never "too big
 * for this file system").  LLONG_MAX on any query failure or a zero-
 * cluster answer: an unrecognised volume must not be treated as having
 * a limit of zero. */
long long __plat_volume_max_file_size(__plat_handle_t h);

/* posix_fallocate()'s current allocation size and logical end-of-file
 * for `h` (FileStandardInformation), which the front door compares
 * against the requested extent to decide what (if anything) needs
 * growing -- see fadvise.c's own comment on why the two are checked
 * separately rather than just against the request. 0/-1(errno). */
int __plat_file_extent(__plat_handle_t h, long long *alloc_size, long long *eof);

/* posix_fallocate()'s two-step storage reservation for an extent ending
 * at `want`: first the allocation size (only when `grow_alloc` is
 * nonzero -- the front door's own data-loss interlock on when growing
 * the allocation is safe, see fadvise.c's banner, decides this before
 * calling), then the logical end-of-file (only when `want` exceeds
 * `eof`).  Unlike every other function in this header, returns a
 * POSIX ERROR NUMBER directly on failure (0 on success), matching
 * posix_fallocate()'s own XSI return convention -- the error is
 * returned, not signalled via -1/errno. */
int __plat_fallocate(__plat_handle_t h, long long want, long long eof, int grow_alloc);

#endif
