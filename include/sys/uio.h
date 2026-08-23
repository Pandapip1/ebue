/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/uio.h>: readv()/writev() are implemented as a loop over this
 * library's own read()/write() -- there is no NT primitive that gives
 * true scatter/gather over arbitrary, unaligned, arbitrary-length
 * buffers (NtReadFileScatter/NtWriteFileGather exist, but are
 * page-granular: every buffer must be page-aligned and a whole number
 * of pages, which an arbitrary struct iovec is not).  See
 * src/misc/uio.c's header comment for exactly what that costs against
 * POSIX's atomicity requirement (XBD 2.9.7) and why it was accepted
 * anyway. */
#ifndef _SYS_UIO_H
#define _SYS_UIO_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_size_t
#define __NEED_ssize_t
#include <bits/alltypes.h>

/* sys_uio.h.html: "at least" these two members. */
struct iovec {
	void *iov_base;
	size_t iov_len;
};

ssize_t readv(int, const struct iovec *, int);
ssize_t writev(int, const struct iovec *, int);

#ifdef __cplusplus
}
#endif
#endif
