/* C library internals and platform ABI fields intentionally use the
 * implementation-reserved namespace so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform-descriptor interface src/unistd/{close,read,write,lseek,
 * dup}.c's POSIX-facing front doors call into instead of a raw
 * Nt{Close,ReadFile,WriteFile,QueryInformationFile,SetInformationFile,
 * DuplicateObject} call.  See src/unistd/nt/plat_fd.c for the
 * implementation these declare.
 *
 * Every function here takes POSIX-shaped arguments and returns a
 * POSIX-shaped result -- errno already set on failure, never a raw
 * platform status for the front door to interpret.  Every NT-specific
 * interpretation step lives entirely inside the backend function's
 * body: STATUS_PENDING waits, end-of-file/broken-pipe detection,
 * translating "the write's starting position was at the offset
 * maximum" into EFBIG.  The fd-table bookkeeping around these calls
 * (__fd_get, __fd_pos_save/restore, __fd_install_at, ...) is NOT part
 * of this interface -- it stays in the front door, unchanged, exactly
 * like mman.c's own reservation table (see plat_mem.h).
 */
#ifndef _NTLIBC_PLAT_FD_H
#define _NTLIBC_PLAT_FD_H

#include <stddef.h>
#include <sys/types.h>
#include "plat_handle.h"

int __plat_close(__plat_handle_t h);

ssize_t __plat_read(__plat_handle_t h, void *buf, size_t count);
ssize_t __plat_pread(__plat_handle_t h, void *buf, size_t count, off_t off);

/* `append` is O_APPEND && the descriptor is a regular file -- exactly
 * write()'s own condition for "position the write past the current
 * end of file rather than at the descriptor's saved position",
 * which is where FILE_WRITE_TO_END_OF_FILE (an NT-only positioning
 * token) is decided, entirely inside the backend. A failure whose
 * starting position was at or past the POSIX offset maximum reports
 * EFBIG; a broken/disconnected/closing pipe raises SIGPIPE and
 * reports EPIPE.  The signal is raised inside the backend, not by the
 * front door testing errno==EPIPE afterward: the generic status-to-
 * errno mapping also produces EPIPE for other statuses that must NOT
 * raise SIGPIPE, and only the backend still has the real status in
 * hand to tell them apart. */
ssize_t __plat_write(__plat_handle_t h, const void *buf, size_t count, int append);
ssize_t __plat_pwrite(__plat_handle_t h, const void *buf, size_t count, off_t off);

/* The descriptor's current byte position (`at_eof` zero) or the
 * object's end-of-file offset (`at_eof` nonzero) -- lseek()'s
 * SEEK_CUR and SEEK_END queries, respectively.  -1/errno on failure. */
long long __plat_seek_query(__plat_handle_t h, int at_eof);
/* Set the descriptor's byte position to exactly `target` (already
 * computed and overflow-checked by the front door).  0/-1(errno). */
int __plat_seek_set(__plat_handle_t h, long long target);

/* Duplicate `h`; the new handle is inheritable by a child process iff
 * `inheritable` is nonzero.  0/-1(errno) via *out.
 *
 * out required: both real implementations write `*out = ...`
 * unconditionally on the success path, with no NULL check of out
 * itself anywhere. Every real call site (src/unistd/dup.c, src/mman/
 * mman.c, src/process/fork.c, src/process/posix_spawn.c) always passes
 * the address of its own local, never NULL. */
int __plat_dup(__plat_handle_t h, int inheritable, __plat_handle_t *out)
    __attribute__((nonnull(3)));

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
