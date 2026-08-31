/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform interface src/ioctl/ioctl.c's FIONREAD handling calls
 * into instead of a raw NtQueryInformationFile call.  See
 * src/ioctl/nt/plat_ioctl.c for the implementation this declares.
 * ioctl()'s other two requests, TIOCGWINSZ and FIONBIO, need no
 * platform call of this kind: FIONBIO only ever touches this library's
 * own fd-table flag, and TIOCGWINSZ already goes through the separate,
 * out-of-scope NTLIBC_USE_KERNEL32 path (src/internal/kernel32.h), not
 * an Nt* or Zw* call.
 */
#ifndef _NTLIBC_PLAT_IOCTL_H
#define _NTLIBC_PLAT_IOCTL_H

#include "plat_handle.h"

/* Bytes immediately readable from a pipe (FilePipeLocalInformation's
 * ReadDataAvailable, clamped to INT_MAX -- see ioctl.c's own banner
 * for why this reuses the exact query select()'s __fd_probe() already
 * makes). 0/-1(errno) via *out.
 *
 * out is required: both backends (src/ioctl/{nt,linux}/plat_ioctl.c)
 * write through it unconditionally on the success path (`*out = ...;
 * return 0;`), with no NULL check of the pointer itself. Its one real
 * call site, src/ioctl/ioctl.c's own static fionread_pipe(), forwards
 * its own out parameter verbatim, which is in turn always ioctl()'s
 * own `&n`, a real local's address, never NULL. */
int __plat_fionread_pipe(__plat_handle_t h, int *out)
    __attribute__((nonnull(2)));

/* The two raw quantities FIONREAD on a regular file is computed from:
 * FileStandardInformation's EndOfFile and FilePositionInformation's
 * CurrentByteOffset.  The front door does the actual subtraction (and
 * its own [EOVERFLOW] check, which has nothing to do with NT) via
 * __file_remaining_count(). 0/-1(errno) via *eof/*pos.
 *
 * eof/pos are both required: both backends write through both
 * unconditionally on the success path (NT: `*eof = si.EndOfFile; *pos
 * = pi.CurrentByteOffset;`; Linux: `*eof = ... stx_size; *pos =
 * ret;`), with no NULL check of either pointer. Its one real call
 * site, src/ioctl/ioctl.c's own static fionread_file(), always passes
 * `&eof, &pos`, two real locals' addresses, never NULL. */
int __plat_file_eof_and_pos(__plat_handle_t h, long long *eof, long long *pos)
    __attribute__((nonnull(2, 3)));

#endif
