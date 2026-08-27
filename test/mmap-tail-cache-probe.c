/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Regression coverage for test/posix-opts-expected.txt's mmap/11-4 row.
 * mmap.html requires that a write into the zero-filled
 * partial page past a file's end never reach the file, and re-mapping
 * the SAME file from a fresh descriptor should see the zero fill again,
 * not the earlier write. Before the libc fix, patched Wine reproduced
 * the failure WITHOUT fork() -- open, ftruncate,
 * mmap MAP_SHARED, write past EOF, msync(MS_SYNC), munmap, close,
 * reopen, mmap again: the second mapping's tail byte still reads the
 * first mapping's write, even though fstat() shows the file's logical
 * length never grew (checked separately with a standalone repro).  That
 * reads as the cache manager -- or Wine's emulation of it -- retaining
 * the file's shared cache page across two independent NtCreateSection()
 * calls despite the intervening close()+reopen().
 *
 * mmap() now explicitly clears the partial tail of writable MAP_SHARED
 * views after mapping, so the second mapping must see zero regardless of
 * whether the underlying NT cache manager retained the first view's byte.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

int main(void)
{
	long page_size = sysconf(_SC_PAGE_SIZE);
	size_t len = (size_t)(page_size / 2);
	static const char fn[] = "mmap-tail-cache-probe.tmp";
	int fd;
	char *pa;
	unsigned char tail;

	unlink(fn);
	fd = open(fn, O_CREAT | O_RDWR | O_EXCL, 0600);
	if (fd < 0) { printf("TAILCACHE: UNRESOLVED open: %s\n", strerror(errno)); return 1; }
	if (ftruncate(fd, len) == -1) { printf("TAILCACHE: UNRESOLVED ftruncate: %s\n", strerror(errno)); return 1; }

	pa = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pa == MAP_FAILED) { printf("TAILCACHE: UNRESOLVED 1st mmap: %s\n", strerror(errno)); return 1; }
	pa[len + 1] = 'b';
	msync(pa, len, MS_SYNC);
	munmap(pa, len);
	close(fd);

	fd = open(fn, O_RDWR, 0);
	if (fd < 0) { printf("TAILCACHE: UNRESOLVED 2nd open: %s\n", strerror(errno)); return 1; }
	pa = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pa == MAP_FAILED) { printf("TAILCACHE: UNRESOLVED 2nd mmap: %s\n", strerror(errno)); return 1; }

	tail = (unsigned char)pa[len + 1];
	if (tail != 0) {
		printf("TAILCACHE: second mapping's tail byte is %u, expected zero\n", tail);
		return 1;
	}
	printf("TAILCACHE: zero-filled on remap (page_size=%ld)\n", page_size);

	munmap(pa, len);
	close(fd);
	unlink(fn);
	return 0;
}
