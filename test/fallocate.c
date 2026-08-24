/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * posix_fallocate(): the assertions that actually reach
 * NtSetInformationFile(FileAllocationInformation), and the invariant
 * that it never shrinks a file.
 *
 * Why this file exists rather than more lines in test/unistd.c: the
 * posix_fallocate() coverage there could not fail.  It calls
 * posix_fallocate(fd, 0, 4096) on an existing small file.  On NTFS such
 * a file already has one 4096-byte cluster allocated, so
 * `want > si.AllocationSize` in src/fcntl/fadvise.c is false and the
 * NtSetInformationFile call is never reached at all.  The test passed on
 * both arches for as long as it existed while never exercising the code
 * it appeared to cover -- including through a live WOW64 bug that made
 * that exact call return EINVAL on i386.  4096 is precisely the
 * non-discriminating value: it is one cluster, so it can neither exceed
 * the allocation of a small file nor fall below it.  Every request here
 * is deliberately chosen to be on the far side of a cluster boundary.
 *
 * POSIX clauses cited are from
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/posix_fallocate.html
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define BIG (1024 * 1024)   /* 256 clusters: cannot be confused with rounding */

int main(void)
{
	char dir[] = "fallocXXXXXX";
	char buf[16384], back[16384];
	struct stat st;
	int fd;
	size_t i;

	if (!mkdtemp(dir)) { printf("FAIL mkdtemp: %s\n", strerror(errno)); return 1; }
	if (chdir(dir) != 0) { printf("FAIL chdir: %s\n", strerror(errno)); return 1; }

	/* --- the call really is reached, and really does grow the file ---
	 * "the function shall ensure that any required storage for regular
	 * file data starting at offset and continuing for len bytes is
	 * allocated on the file system storage media.  If ... the offset+len
	 * is beyond the current file size, then posix_fallocate() shall
	 * adjust the file size."  A megabyte on an empty file is far past any
	 * plausible cluster size, so si.AllocationSize < want holds and the
	 * FileAllocationInformation set-info is genuinely issued -- which is
	 * what test/unistd.c's 4096 could not do. */
	fd = open("grow", O_RDWR | O_CREAT | O_TRUNC, 0600);
	CHECK(fd >= 0);
	CHECK(posix_fallocate(fd, 0, BIG) == 0);
	CHECK(fstat(fd, &st) == 0 && st.st_size == BIG);

	/* Same again from a non-zero offset: offset+len is what matters. */
	CHECK(posix_fallocate(fd, BIG, BIG) == 0);
	CHECK(fstat(fd, &st) == 0 && st.st_size == 2 * BIG);
	CHECK(close(fd) == 0);

	/* --- it never shrinks, and never destroys data ---
	 * POSIX adjusts the file size only when offset+len is *beyond* it; a
	 * request below the current size changes no size.  This is the
	 * assertion that guards the FileAllocationInformation truncation rule
	 * (ntifs.h FILE_ALLOCATION_INFORMATION: an allocation set below the
	 * end-of-file position drags the end-of-file position down with it).
	 * On an ordinary NTFS file the guard in fadvise.c skips the call, so
	 * this passes cheaply; on a file whose AllocationSize is below its
	 * EndOfFile -- sparse, or compressed -- it is the difference between
	 * a no-op and silently discarding the tail of the file. */
	fd = open("keep", O_RDWR | O_CREAT | O_TRUNC, 0600);
	CHECK(fd >= 0);
	for (i = 0; i < sizeof buf; i++) buf[i] = (char)(i & 0x7f);
	CHECK(write(fd, buf, sizeof buf) == (ssize_t)sizeof buf);
	CHECK(posix_fallocate(fd, 0, 100) == 0);
	CHECK(fstat(fd, &st) == 0 && st.st_size == (off_t)sizeof buf);
	CHECK(posix_fallocate(fd, 0, 4096) == 0);
	CHECK(fstat(fd, &st) == 0 && st.st_size == (off_t)sizeof buf);
	CHECK(posix_fallocate(fd, 8192, 1) == 0);
	CHECK(fstat(fd, &st) == 0 && st.st_size == (off_t)sizeof buf);
	CHECK(lseek(fd, 0, SEEK_SET) == 0);
	CHECK(read(fd, back, sizeof back) == (ssize_t)sizeof back);
	CHECK(memcmp(buf, back, sizeof buf) == 0);
	CHECK(close(fd) == 0);

	/* A request that lands exactly on the current size is also a no-op. */
	fd = open("exact", O_RDWR | O_CREAT | O_TRUNC, 0600);
	CHECK(fd >= 0);
	CHECK(write(fd, buf, sizeof buf) == (ssize_t)sizeof buf);
	CHECK(posix_fallocate(fd, 0, (off_t)sizeof buf) == 0);
	CHECK(fstat(fd, &st) == 0 && st.st_size == (off_t)sizeof buf);
	CHECK(close(fd) == 0);

	CHECK(unlink("grow") == 0);
	CHECK(unlink("keep") == 0);
	CHECK(unlink("exact") == 0);
	CHECK(chdir("..") == 0);
	CHECK(rmdir(dir) == 0);

	if (fails) { printf("%d failure(s)\n", fails); return 1; }
	printf("fallocate: all checks passed\n");
	return 0;
}
