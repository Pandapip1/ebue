/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/mman.h> clause audit, against the shipped header
 * (include/sys/mman.h, src/mman/mman.c).  Pass 1: anonymous mappings
 * only, file-backed refused at the door.
 *
 * test/posix-dl.c used to carry six fenced mmap tests, written while the
 * header did not exist.  Every one of them called
 * `mmap(0, n, prot, MAP_PRIVATE, -1, 0)` and asserted success.  That
 * expectation was wrong, and wrong in the direction that matters:
 * POSIX Issue 7 -- the edition this tree speaks, 48 citations against 4
 * for Issue 8 -- has no anonymous mapping at all, and makes "[EBADF] The
 * fildes argument is not a valid open file descriptor" a SHALL FAIL.
 * Measured against glibc, that exact call returns MAP_FAILED/EBADF.  So
 * the fences asserted success for a case the standard requires to fail.
 * The clauses moved here and gained MAP_ANONYMOUS, which moves them
 * toward the specification rather than away from it.
 *
 * The one thing this file is most careful about: [EBADF] and [ENODEV]
 * are asserted SEPARATELY and never as "it failed somehow".  They are
 * different refusals -- one says "that is not a descriptor", the other
 * says "that is a descriptor for a file type Pass 1 does not support" --
 * and a test satisfied by either cannot tell a correct refusal from a
 * wrong one.
 */
#include "test-policy.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>

/* The extension gate is load-bearing (see include/sys/mman.h): tests are
 * built with no feature-test macro, so features.h's default arm defines
 * _BSD_SOURCE and MAP_ANONYMOUS must be visible.  If a future edit
 * ungates it, or gates it more tightly than _BSD_SOURCE/_GNU_SOURCE,
 * this stops the build rather than silently changing what is tested. */
#ifndef MAP_ANONYMOUS
#error "MAP_ANONYMOUS not visible: the _BSD_SOURCE/_GNU_SOURCE gate in <sys/mman.h> has changed"
#endif

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define PG 4096

/* ---------------------------------------------------------------- */
/* mmap.html -- the anonymous success path                           */
/* ---------------------------------------------------------------- */

/* DESCRIPTION: "The mmap() function shall establish a mapping between
 * the address space of the process ... and a memory object."  RETURN
 * VALUE: "shall return the address at which the mapping was placed". */
static void test_mmap_anonymous_basic(void)
{
	char *p = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(p != MAP_FAILED);
	if (p == MAP_FAILED) return;

	/* Page-aligned, as the whole model assumes. */
	CHECK(((unsigned long)(size_t)p & (PG - 1)) == 0);

	/* A fresh anonymous mapping reads as zero.  Asserted rather than
	 * assumed: it is what makes MAP_FIXED's "modifications shall be
	 * discarded" observable below, so if it were not true that test
	 * would be checking nothing. */
	CHECK(p[0] == 0 && p[PG - 1] == 0);

	/* PROT_WRITE means writable, and the bytes stay put. */
	p[0] = 'x';
	p[PG - 1] = 'z';
	CHECK(p[0] == 'x' && p[PG - 1] == 'z');

	CHECK(munmap(p, PG) == 0);
}

/* MAP_SHARED and MAP_PRIVATE are indistinguishable for an anonymous
 * mapping -- there is no underlying object -- but both are accepted, and
 * that is worth pinning: refusing one would be a gratuitous divergence,
 * and accepting neither is the [EINVAL] tested further down. */
static void test_mmap_shared_and_private_both_accepted(void)
{
	char *a = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	char *b = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	CHECK(a != MAP_FAILED);
	CHECK(b != MAP_FAILED);
	/* Two separate mmap() calls are two separate mappings, whatever the
	 * sharing flag says: NT gives each its own reservation. */
	if (a != MAP_FAILED && b != MAP_FAILED) CHECK(a != b);
	if (a != MAP_FAILED) CHECK(munmap(a, PG) == 0);
	if (b != MAP_FAILED) CHECK(munmap(b, PG) == 0);
}

/* ---------------------------------------------------------------- */
/* mmap.html ERRORS -- the two refusals, kept apart                  */
/* ---------------------------------------------------------------- */

/* "[EBADF] The fildes argument is not a valid open file descriptor."
 * (shall fail)  This is the call that LOOKS anonymous and is not: no
 * MAP_ANONYMOUS, so Issue 7 reads it as a file-backed request against
 * descriptor -1.  glibc answers EBADF here; so do we. */
static void test_mmap_no_anon_flag_is_ebadf(void)
{
	void *p;
	errno = 0;
	p = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE, -1, 0);
	CHECK(p == MAP_FAILED);
	CHECK(errno == EBADF);
	/* Specifically NOT ENODEV -- there is no file here whose type could
	 * be unsupported.  Asserted so the two refusals can never collapse
	 * into each other unnoticed. */
	CHECK(errno != ENODEV);
}

/* "[ENODEV] The fildes argument refers to a file whose type is not
 * supported by mmap()."  Used at its literal reading: Pass 1 supports no
 * file type, and a regular file -- the canonical mmap-able type -- is
 * refused with it.  Unusual, deliberate, and stated in
 * include/sys/mman.h so it is not mistaken for an oversight. */
static void test_mmap_regular_file_is_enodev(const char *path)
{
	int fd = open(path, O_RDONLY);
	void *p;
	CHECK(fd >= 0);
	if (fd < 0) return;
	errno = 0;
	p = mmap(0, PG, PROT_READ, MAP_PRIVATE, fd, 0);
	CHECK(p == MAP_FAILED);
	CHECK(errno == ENODEV);
	/* and specifically NOT EBADF: the descriptor is perfectly valid,
	 * which is the whole difference between this case and the one
	 * above. */
	CHECK(errno != EBADF);
	close(fd);
}

/* "[EINVAL] The value of len is zero." (shall fail) */
static void test_mmap_zero_len_is_einval(void)
{
	void *p;
	errno = 0;
	p = mmap(0, 0, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(p == MAP_FAILED);
	CHECK(errno == EINVAL);
}

/* "[EINVAL] The value of flags is invalid (neither MAP_PRIVATE nor
 * MAP_SHARED is set)." */
static void test_mmap_no_sharing_flag_is_einval(void)
{
	void *p;
	errno = 0;
	p = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
	CHECK(p == MAP_FAILED);
	CHECK(errno == EINVAL);
	/* Both at once is not a described state either. */
	errno = 0;
	p = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(p == MAP_FAILED);
	CHECK(errno == EINVAL);
}

/* ---------------------------------------------------------------- */
/* munmap.html                                                       */
/* ---------------------------------------------------------------- */

/* DESCRIPTION + RETURN VALUE: removes the mappings, returns 0. */
static void test_munmap_success(void)
{
	char *p = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(p != MAP_FAILED);
	if (p != MAP_FAILED) CHECK(munmap(p, PG) == 0);
}

/* ERRORS: "[EINVAL] The addr argument is not a multiple of the page size
 * as returned by sysconf()", and "[EINVAL] The len argument is 0."
 * Those two plus "addresses ... outside the valid range" are the ENTIRE
 * ERRORS section -- which is the fact the whole Pass 1 design turns on:
 * there is no errno for a partial munmap, so a partial munmap must
 * work. */
static void test_munmap_einval(void)
{
	char *p = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	errno = 0;
	CHECK(munmap((void *)1, PG) == -1);
	CHECK(errno == EINVAL);
	if (p != MAP_FAILED) {
		errno = 0;
		CHECK(munmap(p, 0) == -1);
		CHECK(errno == EINVAL);
		CHECK(munmap(p, PG) == 0);
	}
}

/* "If there are no mappings in the specified address range, then
 * munmap() has no effect."  A page-aligned range that was never mapped
 * is therefore a SUCCESS, not an error -- the easy bug is to report
 * EINVAL for it, which would be inventing an error POSIX does not have
 * for a call POSIX says succeeds. */
static void test_munmap_unmapped_range_succeeds(void)
{
	char *p = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(p != MAP_FAILED);
	if (p == MAP_FAILED) return;
	CHECK(munmap(p, PG) == 0);
	/* second unmap of the same, now-unmapped, still-page-aligned range */
	CHECK(munmap(p, PG) == 0);
}

/* THE CLAUSE THE DESIGN EXISTS FOR: a partial unmap.  "The munmap()
 * function shall remove any mappings for those entire pages containing
 * any part of the address space of the process starting at addr and
 * continuing for len bytes."  Unmapping the first page of a two-page
 * mapping must leave the second page mapped, at its own address, with
 * its contents intact.
 *
 * The surviving half is what is asserted.  The unmapped half is NOT read
 * back -- that would fault, and a test that deliberately faults this
 * process proves nothing the surviving half does not. */
static void test_munmap_partial_keeps_the_rest(void)
{
	char *p = mmap(0, 2 * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(p != MAP_FAILED);
	if (p == MAP_FAILED) return;

	p[0] = 'A';
	p[PG] = 'B';
	p[2 * PG - 1] = 'C';

	/* drop the first page only */
	CHECK(munmap(p, PG) == 0);

	/* the second page is still there, still writable, still holding
	 * what was written before the partial unmap */
	CHECK(p[PG] == 'B');
	CHECK(p[2 * PG - 1] == 'C');
	p[PG] = 'D';
	CHECK(p[PG] == 'D');

	CHECK(munmap(p + PG, PG) == 0);
}

/* ---------------------------------------------------------------- */
/* mmap.html MAP_FIXED                                               */
/* ---------------------------------------------------------------- */

/* "If MAP_FIXED is set ... any previous mappings in [addr, addr+len)
 * are discarded", and "If a mapping to be replaced was private, ... the
 * modifications shall be discarded."
 *
 * TWO assertions, not one.  The address being honoured and the old
 * contents being discarded are different promises, and an implementation
 * that does a bare MEM_COMMIT over already-committed pages satisfies the
 * first while silently failing the second.  A test checking only the
 * returned address cannot tell those apart. */
static void test_mmap_fixed_replaces_and_discards(void)
{
	char *base = mmap(0, 2 * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	char *p;
	CHECK(base != MAP_FAILED);
	if (base == MAP_FAILED) return;

	base[0] = 'a';
	base[PG] = 'b';          /* the page NOT being replaced */
	CHECK(base[0] == 'a');

	p = mmap(base, PG, PROT_READ | PROT_WRITE,
	         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	CHECK(p == base);                 /* the address is honoured */
	if (p == base) CHECK(base[0] == 0); /* and the modification is discarded */

	/* the neighbouring page inside the same reservation is untouched --
	 * MAP_FIXED replaces [addr,addr+len), not the whole mapping */
	CHECK(base[PG] == 'b');

	CHECK(munmap(base, 2 * PG) == 0);
}

/* MAP_FIXED at a page-aligned address we do not own.  Pass 1 honours
 * MAP_FIXED only inside its own reservations -- NT can commit
 * page-granularly inside a reservation, but a fresh reservation is
 * placed at 64 KiB allocation granularity, so an arbitrary page-aligned
 * address cannot be honoured at all.  "[ENOMEM] MAP_FIXED was specified,
 * and the range [addr,addr+len) exceeds that allowed for the address
 * space of a process."  Reported, not faked into success. */
static void test_mmap_fixed_outside_is_enomem(void)
{
	void *p;
	errno = 0;
	p = mmap((void *)(size_t)0x00010000, PG, PROT_READ | PROT_WRITE,
	         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	CHECK(p == MAP_FAILED);
	CHECK(errno == ENOMEM);
}

/* "[EINVAL] MAP_FIXED was specified, and ... addr is not a multiple of
 * the page size." */
static void test_mmap_fixed_misaligned_is_einval(void)
{
	char *base = mmap(0, 2 * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	void *p;
	CHECK(base != MAP_FAILED);
	if (base == MAP_FAILED) return;
	errno = 0;
	p = mmap(base + 1, PG, PROT_READ | PROT_WRITE,
	         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	CHECK(p == MAP_FAILED);
	CHECK(errno == EINVAL);
	CHECK(munmap(base, 2 * PG) == 0);
}

#if NTLIBC_TEST(UNIMPL, posix_mman_mmap_file_backed) /* DECLINED FOR NOW (Pass 2), not impossible and not a platform
       * limit -- a file-backed mmap().  Pass 1 answers [ENODEV] for
       * every file type (asserted above); this is the clause that
       * refusal defers, recorded so the next reader meets the argument
       * instead of rediscovering it.
       *
       * What makes it hard is not the mapping, it is the UNMAPPING.
       * NtCreateSection()/NtMapViewOfSection() would map a file today,
       * and NtProtectVirtualMemory() already handles mprotect().  But
       * NtUnmapViewOfSection() takes a view BASE ADDRESS and drops the
       * WHOLE view -- there is no "unmap these three pages out of the
       * middle" for a section view the way MEM_DECOMMIT gives one for a
       * private reservation.  So a file-backed mapping could not honour
       * a partial munmap(), and munmap.html's ERRORS are exactly three
       * (addr not page-aligned, range outside the address space, len of
       * zero): there is no errno for "I cannot unmap part of this".
       * Returning [EINVAL] for a legal partial unmap would be a spec
       * violation dressed as a documented limitation, which is precisely
       * why Pass 1 refuses at the door instead -- mmap.html DOES give
       * [ENODEV] for a file type an implementation does not support.
       *
       * THE ROUTE OUT, so this is a decision and not a dead end:
       * Windows 10 1803 added placeholder reservations --
       * VirtualAlloc2() with MEM_RESERVE_PLACEHOLDER, split with
       * MEM_PRESERVE_PLACEHOLDER, and MapViewOfFile3()/
       * NtMapViewOfSectionEx() with MEM_REPLACE_PLACEHOLDER to drop a
       * file view into one.  A placeholder CAN be split, so a view
       * placed in one can be partially unmapped -- which is exactly the
       * primitive munmap() needs and NtUnmapViewOfSection() lacks.
       *
       * The cost is the reason it is Pass 2 rather than Pass 1: it is a
       * hard Windows 10 1803+ floor (this library otherwise targets
       * nothing so recent), those entry points are kernel32/kernelbase
       * rather than pure ntdll except for NtMapViewOfSectionEx, and none
       * of the MEM_*_PLACEHOLDER constants or the Ex entry point are
       * declared in src/internal/nt.h today.  Whether to take a version
       * floor for it is a decision, not an oversight. */
static void test_mmap_file_backed(const char *path)
{
	int fd = open(path, O_RDONLY);
	char *p;
	CHECK(fd >= 0);
	if (fd < 0) return;
	p = mmap(0, PG, PROT_READ, MAP_PRIVATE, fd, 0);
	CHECK(p != MAP_FAILED);
	if (p != MAP_FAILED) {
		/* the first bytes of the mapping are the first bytes of the
		 * file -- the whole point of a file-backed mapping */
		char buf[8];
		CHECK(pread(fd, buf, sizeof buf, 0) == (ssize_t)sizeof buf);
		CHECK(memcmp(p, buf, sizeof buf) == 0);
		CHECK(munmap(p, PG) == 0);
	}
	close(fd);
}
#endif

/* ---------------------------------------------------------------- */
/* mprotect.html                                                     */
/* ---------------------------------------------------------------- */

/* DESCRIPTION: "shall change the access protections to be that specified
 * by prot for those whole pages containing any part of the address
 * space".  The round trip is what is checked; the fault that a write to
 * a PROT_READ page should take is deliberately NOT provoked -- it would
 * kill this process, and test/posix-alloc.c declines the same way. */
static void test_mprotect_roundtrip(void)
{
	char *p = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(p != MAP_FAILED);
	if (p == MAP_FAILED) return;
	p[0] = 'q';
	CHECK(mprotect(p, PG, PROT_READ) == 0);
	CHECK(p[0] == 'q');                       /* still readable */
	CHECK(mprotect(p, PG, PROT_READ | PROT_WRITE) == 0);
	p[0] = 'r';                               /* writable again */
	CHECK(p[0] == 'r');
	CHECK(mprotect(p, PG, PROT_NONE) == 0);   /* PROT_NONE is a real state */
	CHECK(mprotect(p, PG, PROT_READ | PROT_WRITE) == 0);
	CHECK(munmap(p, PG) == 0);
}

/* ERRORS: "[EINVAL] The addr argument is not a multiple of the page size
 * as returned by sysconf()." */
static void test_mprotect_einval(void)
{
	errno = 0;
	CHECK(mprotect((void *)1, PG, PROT_READ) == -1);
	CHECK(errno == EINVAL);
}

/* ---------------------------------------------------------------- */
/* msync.html                                                        */
/* ---------------------------------------------------------------- */

/* An honest no-op that returns success, on src/termios/termios.c's
 * tcflush() precedent.  Under anonymous-only mappings there is no
 * underlying object, so "writes all modified copies of pages ... back to
 * the filesystem" holds vacuously and 0 states something true.  -1 would
 * be worse than useless: msync.html has no error meaning "nothing to
 * do", so any failure returned here would be indistinguishable from a
 * genuine one. */
static void test_msync_anonymous_succeeds(void)
{
	char *p = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(p != MAP_FAILED);
	if (p == MAP_FAILED) return;
	CHECK(msync(p, PG, MS_SYNC) == 0);
	CHECK(msync(p, PG, MS_ASYNC) == 0);
	CHECK(msync(p, PG, MS_SYNC | MS_INVALIDATE) == 0);
	CHECK(munmap(p, PG) == 0);
}

/* ERRORS: "[EINVAL] The value of addr is not a multiple of the page size
 * as returned by sysconf()", "[EINVAL] The value of flags is invalid",
 * and "[EINVAL] The value of flags includes both MS_ASYNC and MS_SYNC."
 * The arguments are validated even though the operation is a no-op: a
 * misaligned address is a caller error whether or not there is anything
 * to flush, and a no-op that accepts anything teaches callers nothing. */
static void test_msync_einval(void)
{
	char *p = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	errno = 0;
	CHECK(msync((void *)1, PG, MS_SYNC) == -1);
	CHECK(errno == EINVAL);
	if (p == MAP_FAILED) return;
	errno = 0;
	CHECK(msync(p, PG, MS_ASYNC | MS_SYNC) == -1);
	CHECK(errno == EINVAL);
	errno = 0;
	CHECK(msync(p, PG, 0x1000) == -1);
	CHECK(errno == EINVAL);
	CHECK(munmap(p, PG) == 0);
}

/* ---------------------------------------------------------------- */
/* mlock.html                                                        */
/* ---------------------------------------------------------------- */

/* mlock()/munlock() go to NtLockVirtualMemory()/NtUnlockVirtualMemory(),
 * which are real exports and really implemented -- on NT, and in Wine,
 * where NtLockVirtualMemory calls the host's mlock(2) for the current
 * process (dlls/ntdll/unix/virtual.c:6254, measured at wine 855d92781)
 * rather than returning STATUS_NOT_IMPLEMENTED.
 *
 * BUT SUCCESS IS AN ENVIRONMENT PROPERTY, NOT A PLATFORM ONE.  Locking
 * is bounded by a resource limit on both sides -- a working-set quota on
 * NT, RLIMIT_MEMLOCK on the host under Wine -- so the same binary
 * succeeds on a machine with a generous limit and fails on one with the
 * common 64 KiB default.  Asserting success unconditionally would give a
 * gate that passes here and on the CI Server 2025 leg and fails on
 * somebody's laptop, which is the definition of a flaky gate.
 *
 * So this MEASURES THE CAPABILITY rather than branching on which system
 * it thinks it is on, the same shape test/sparse-zerodata.c uses for
 * FSCTL_SET_ZERO_DATA and test/posix-grp.c for child CPU times.  Note
 * getrlimit(RLIMIT_MEMLOCK) is NOT the oracle here: src/misc/resource.c
 * answers RLIM_INFINITY for it unconditionally and never reflects the
 * host limit, so asking it would be asking a question nothing measures.
 * The lock attempt itself is the measurement.
 *
 * Returns 1 if the clause was verified, 0 if it could not be. */
static int test_mlock_munlock(void)
{
	char *p = mmap(0, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	int e;
	CHECK(p != MAP_FAILED);
	if (p == MAP_FAILED) return 0;

	errno = 0;
	if (mlock(p, PG) != 0) {
		e = errno;
		printf("SKIP posix-mman mlock/munlock: this environment does not "
		       "permit locking one page (mlock -> errno %d). Locking is "
		       "bounded by a working-set quota on NT and by RLIMIT_MEMLOCK "
		       "on the host under Wine; that is a property of the machine "
		       "this ran on, not of the platform, so the clause is "
		       "unverified here rather than failed. Measured by attempting "
		       "the lock -- getrlimit(RLIMIT_MEMLOCK) is not consulted "
		       "because src/misc/resource.c answers RLIM_INFINITY for it "
		       "unconditionally and never reflects the host limit.\n", e);
		munmap(p, PG);
		return 0;
	}

	/* "shall cause those whole pages ... to be memory-resident": the
	 * mapping is still perfectly usable while locked. */
	p[0] = 'L';
	CHECK(p[0] == 'L');
	CHECK(munlock(p, PG) == 0);
	CHECK(p[0] == 'L');

	/* mlock.html ERRORS: "[EINVAL] ... the len argument is zero." */
	errno = 0;
	CHECK(mlock(p, 0) == -1);
	CHECK(errno == EINVAL);

	CHECK(munmap(p, PG) == 0);
	return 1;
}

/* ---------------------------------------------------------------- */

int main(int argc, char **argv)
{
	int mlock_verified;
	(void)argc;

	test_mmap_anonymous_basic();
	test_mmap_shared_and_private_both_accepted();

	test_mmap_no_anon_flag_is_ebadf();
	test_mmap_regular_file_is_enodev(argv[0]);
	test_mmap_zero_len_is_einval();
	test_mmap_no_sharing_flag_is_einval();

	test_munmap_success();
	test_munmap_einval();
	test_munmap_unmapped_range_succeeds();
	test_munmap_partial_keeps_the_rest();

	test_mmap_fixed_replaces_and_discards();
	test_mmap_fixed_outside_is_enomem();
	test_mmap_fixed_misaligned_is_einval();

	test_mprotect_roundtrip();
	test_mprotect_einval();

	test_msync_anonymous_succeeds();
	test_msync_einval();

	mlock_verified = test_mlock_munlock();

	if (fails) { printf("%d check(s) failed\n", fails); return 1; }
	if (!mlock_verified) {
		printf("UNVERIFIED posix-mman: mlock/munlock could not be measured "
		       "in this environment (see SKIP above); everything else passed\n");
		return 77;
	}
	printf("all checks passed\n");
	return 0;
}
