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
 * makes). 0/-1(errno) via *out. */
int __plat_fionread_pipe(__plat_handle_t h, int *out);

/* The two raw quantities FIONREAD on a regular file is computed from:
 * FileStandardInformation's EndOfFile and FilePositionInformation's
 * CurrentByteOffset.  The front door does the actual subtraction (and
 * its own [EOVERFLOW] check, which has nothing to do with NT) via
 * __file_remaining_count(). 0/-1(errno) via *eof/*pos. */
int __plat_file_eof_and_pos(__plat_handle_t h, long long *eof, long long *pos);

#endif
