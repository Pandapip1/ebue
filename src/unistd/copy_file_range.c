/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * copy_file_range(): a Linux/glibc extension, not POSIX, so there is no
 * single normative spec to cite -- glibc's own manual page describes it
 * as copying up to `len` bytes from fd_in to fd_out, using and advancing
 * *off_in/*off_out when given or the descriptor's own file position
 * otherwise, and returning the number of bytes actually copied (which
 * may be less than len, including 0 at EOF).
 *
 * There is no NT primitive this can forward to for an accelerated,
 * same-filesystem copy (CopyFileEx is a kernel32 shell-level API over a
 * *path*, not descriptors, and this library treats kernel32 as an
 * exception rather than a routine dependency -- see
 * src/signal/signal.c's header comment).  So this is the ordinary
 * fallback every implementation uses when it cannot accelerate: a
 * read/write loop, using pread/pwrite when an offset pointer is given so
 * the descriptor's own position is left untouched, matching what the
 * offset arguments promise.  `flags` is reserved and must be 0.
 */
#include <unistd.h>
#include <errno.h>

ssize_t copy_file_range(int fd_in, off_t *off_in, int fd_out, off_t *off_out, size_t len, unsigned flags)
{
	char buf[65536];
	size_t total = 0;

	if (flags) { errno = EINVAL; return -1; }

	while (total < len) {
		size_t chunk = len - total < sizeof buf ? len - total : sizeof buf;
		ssize_t n, w;

		n = off_in ? pread(fd_in, buf, chunk, *off_in) : read(fd_in, buf, chunk);
		if (n < 0) return total ? (ssize_t)total : -1;
		if (n == 0) break;   /* EOF on the source */

		w = off_out ? pwrite(fd_out, buf, (size_t)n, *off_out) : write(fd_out, buf, (size_t)n);
		if (w < 0) return total ? (ssize_t)total : -1;

		if (off_in) *off_in += w;
		if (off_out) *off_out += w;
		total += (size_t)w;
		if (w < n) break;   /* short write: stop rather than desync src/dst */
	}
	return (ssize_t)total;
}
