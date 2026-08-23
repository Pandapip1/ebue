/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * readv()/writev(): implemented as a loop over this library's own
 * read()/write() (src/unistd/read.c, src/unistd/write.c), one iovec at
 * a time.
 *
 * That is a deliberate, documented divergence from POSIX, not an
 * oversight. XBD 2.9.7 "Thread Interactions with Regular File
 * Operations" requires read(), write(), readv(), and writev() (among
 * others) to be atomic with respect to each other on a regular file:
 * "If two threads each call one of these functions, each call shall
 * either see all of the specified effects of the other call, or none
 * of them." A loop of separate NtReadFile()/NtWriteFile() calls cannot
 * give that guarantee -- another thread's write() can land in the
 * middle of this readv()'s buffers, or another thread's read()/write()
 * can observe this writev() only partially applied.
 *
 * The alternative NT actually offers, NtReadFileScatter()/
 * NtWriteFileGather(), was considered and rejected: both are
 * page-granular (every element must be page-aligned and a whole
 * number of pages -- see MAP_FIXED-adjacent constraints documented for
 * these calls), which an arbitrary struct iovec from a real caller
 * essentially never is. Restricting readv()/writev() to page-aligned,
 * page-sized buffers would satisfy the atomicity clause but reject
 * ordinary vectors, which the callers this exists for (any C program
 * built expecting POSIX readv/writev) do not send. A loop that works
 * for the vectors real callers actually pass, with the atomicity gap
 * documented, was judged more useful than a "conformant" version that
 * only ever accepts single-page-aligned buffers.
 *
 * What *is* preserved: "always fill/write a complete area before
 * proceeding to the next" (readv.html/writev.html DESCRIPTION) --
 * this loop does exactly that, in order, one whole iovec at a time.
 *
 * Two checks are made honestly before any I/O happens:
 *
 *   - iovcnt outside [1, IOV_MAX] -> EINVAL (readv.html/writev.html
 *     ERRORS, "may fail"; IOV_MAX from include/limits.h -- basedefs/
 *     sys_uio.h.html: "The symbol {IOV_MAX} defined in <limits.h>
 *     should always be used").
 *   - the sum of iov_len overflowing ssize_t/exceeding SSIZE_MAX ->
 *     EINVAL, "and no data shall be transferred" (writev.html
 *     DESCRIPTION/ERRORS; readv.html ERRORS has the matching EINVAL for
 *     the read side). SSIZE_MAX comes from each arch's own
 *     bits/limits.h via <limits.h>, not assumed to be a fixed width --
 *     see that file's own comment on why ssize_t's range differs per
 *     arch here.
 */
#include <sys/uio.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>

static ssize_t check_iov(const struct iovec *iov, int iovcnt)
{
	size_t sum = 0;
	int i;

	if (iovcnt <= 0 || iovcnt > IOV_MAX) { errno = EINVAL; return -1; }
	if (!iov) { errno = EFAULT; return -1; }

	for (i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len > (size_t)SSIZE_MAX - sum) { errno = EINVAL; return -1; }
		sum += iov[i].iov_len;
	}
	return 0;
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
	ssize_t total = 0;
	int i;

	if (check_iov(iov, iovcnt) < 0) return -1;

	for (i = 0; i < iovcnt; i++) {
		ssize_t r;
		if (!iov[i].iov_len) continue;
		r = read(fd, iov[i].iov_base, iov[i].iov_len);
		/* read.html's own partial-transfer contract: on error after
		 * some data has already moved, report the bytes that really
		 * did move rather than losing that fact by returning -1. */
		if (r < 0) return total ? total : -1;
		total += r;
		if ((size_t)r < iov[i].iov_len) break;   /* short read == EOF */
	}
	return total;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
	ssize_t total = 0;
	int i;

	if (check_iov(iov, iovcnt) < 0) return -1;

	for (i = 0; i < iovcnt; i++) {
		ssize_t w;
		if (!iov[i].iov_len) continue;
		w = write(fd, iov[i].iov_base, iov[i].iov_len);
		if (w < 0) return total ? total : -1;
		total += w;
		if ((size_t)w < iov[i].iov_len) break;   /* short write */
	}
	return total;
}
