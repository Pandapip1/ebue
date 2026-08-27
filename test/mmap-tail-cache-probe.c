/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NOT a pass/fail gate -- always exits 0.  A one-shot real-NT oracle
 * probe for test/posix-opts-expected.txt's mmap/11-4 row, which stays
 * BUG against Wine: mmap.html requires that a write into the zero-filled
 * partial page past a file's end never reach the file, and re-mapping
 * the SAME file from a fresh descriptor should see the zero fill again,
 * not the earlier write. Under the patched Wine build
 * (build-wow64/wine) this reproduces WITHOUT fork() -- open, ftruncate,
 * mmap MAP_SHARED, write past EOF, msync(MS_SYNC), munmap, close,
 * reopen, mmap again: the second mapping's tail byte still reads the
 * first mapping's write, even though fstat() shows the file's logical
 * length never grew (checked separately with a standalone repro).  That
 * reads as the cache manager -- or Wine's emulation of it -- retaining
 * the file's shared cache page across two independent NtCreateSection()
 * calls despite the intervening close()+reopen().
 *
 * Whether real NT does the same thing is not yet known, and printf is
 * the only oracle available for it: this only runs under `runtime=wine`
 * in CI's posix-optsrun job (the Open POSIX Test Suite is Linux+Wine
 * only there), but this FILE is an ordinary ntlibc test, so it also
 * builds into test-exes and runs on real Windows Server 2025 in CI's
 * windows-test job.  Grep that job's log for TAILCACHE to read the
 * verdict without needing another agent to log into a Windows box.
 *
 *   TAILCACHE: reproduced      -- real NT retains the tail write too;
 *     the LTP expectation does not hold on this platform, and
 *     mmap/11-4 becomes a defensible NA (cite this line, not a Wine
 *     run) rather than a BUG.
 *   TAILCACHE: not-reproduced  -- real NT zero-fills correctly and
 *     Wine's cache-manager emulation is what diverges; mmap/11-4 stays
 *     BUG against `runtime=wine` specifically, and per the project's
 *     standing rule (always patch Wine to match real NT, not work
 *     around it in ntlibc) the fix belongs in the Wine tree at
 *     ~/Projects/wine, not here.
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
	if (fd < 0) { printf("TAILCACHE: UNRESOLVED open: %s\n", strerror(errno)); return 0; }
	if (ftruncate(fd, len) == -1) { printf("TAILCACHE: UNRESOLVED ftruncate: %s\n", strerror(errno)); return 0; }

	pa = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pa == MAP_FAILED) { printf("TAILCACHE: UNRESOLVED 1st mmap: %s\n", strerror(errno)); return 0; }
	pa[len + 1] = 'b';
	msync(pa, len, MS_SYNC);
	munmap(pa, len);
	close(fd);

	fd = open(fn, O_RDWR, 0);
	if (fd < 0) { printf("TAILCACHE: UNRESOLVED 2nd open: %s\n", strerror(errno)); return 0; }
	pa = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pa == MAP_FAILED) { printf("TAILCACHE: UNRESOLVED 2nd mmap: %s\n", strerror(errno)); return 0; }

	tail = (unsigned char)pa[len + 1];
	/* tools/run-tests.py only echoes a PASSing test's stdout for lines
	 * carrying its MEASURE_PREFIX ("measure:") -- everything else on a
	 * PASS is treated as noise and dropped, which is why the first push
	 * of this probe produced "PASS (rc=0)" on windows-test with no
	 * TAILCACHE line to grep. */
	if (tail == 'b')
		printf("measure: TAILCACHE: reproduced (2nd mapping's tail byte is 'b', page_size=%ld)\n", page_size);
	else
		printf("measure: TAILCACHE: not-reproduced (2nd mapping's tail byte is %u, page_size=%ld)\n", tail, page_size);

	munmap(pa, len);
	close(fd);
	unlink(fn);
	return 0;
}
