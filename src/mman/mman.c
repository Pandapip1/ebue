/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/mman.h>, Pass 1: anonymous mappings only.
 *
 * See include/sys/mman.h's banner for the edition question that shapes
 * this file (Issue 7 has no anonymous mapping; MAP_ANONYMOUS is a gated
 * extension here) and for why file-backed mmap() is refused at the door
 * with [ENODEV] rather than half-supported.  The short form of that
 * argument, because it is the one load-bearing decision in the file:
 * munmap.html's ERRORS are exactly three -- addr not page-aligned, range
 * outside the address space, len of zero -- so THERE IS NO ERRNO FOR A
 * PARTIAL munmap.  An implementation that could not honour a partial
 * unmap would have to return [EINVAL] for a legal call, which is a spec
 * violation dressed as a documented limitation.  So partial unmap is
 * honoured here, properly, and the file-backed case is declined up front
 * where mmap.html does give us an error to decline with.
 *
 *
 * One reservation per mapping, and why
 * ------------------------------------
 *
 * NT's reserve/commit split is what makes a conforming partial munmap()
 * possible.  Each mmap() takes its OWN reservation via
 * NtAllocateVirtualMemory(MEM_RESERVE|MEM_COMMIT).  A partial munmap()
 * then MEM_DECOMMITs just the subrange -- page-granular, which is what
 * POSIX needs -- and keeps the reservation, so the surviving pages of
 * the same mapping are untouched and still at their own addresses.  Only
 * when the last live page of a mapping goes does the reservation itself
 * get MEM_RELEASEd.  MEM_RELEASE cannot free part of a reservation
 * (it takes the whole thing, base address and size zero), which is
 * exactly why one reservation per mapping rather than one big arena:
 * a shared arena could never release anything until every mapping in it
 * died.
 *
 * THE DIVERGENCE THIS BUYS, WRITTEN DOWN BECAUSE IT IS UNOBSERVABLE.
 * After a partial munmap() the decommitted pages stay RESERVED, not
 * free.  So the address space is not returned to the system, and a later
 * mmap(NULL, ...) will not reuse those addresses.  No conforming program
 * can detect this -- POSIX gives no way to ask "is this address
 * available", and mmap(NULL, ...) promises only *some* address -- but an
 * unobservable divergence is exactly the kind that becomes observable
 * later, when someone adds an address-space accounting call or a program
 * unmaps and remaps in a loop.  It is a leak of reservation, bounded by
 * the number of live mappings, not of committed memory.
 *
 * The 64 KiB question, which bites reservations and not commits.
 * NtAllocateVirtualMemory rounds a *reservation* base up to the
 * allocation granularity (64 KiB, SYSTEM_INFO.AllocationGranularity),
 * not to the 4 KiB page.  That constraint binds only when a reservation
 * is created.  MEM_COMMIT and MEM_DECOMMIT over a subrange of an
 * existing reservation are page-granular, which is why MAP_FIXED can be
 * honoured at page granularity inside a mapping we already own, and why
 * it cannot be honoured anywhere else.
 *
 *
 * MAP_FIXED
 * ---------
 *
 * Honoured at page granularity inside our own reservations; [ENOMEM]
 * outside them.  mmap.html: "If MAP_FIXED is set ... any previous
 * mappings in [addr, addr+len) are discarded", and "If a mapping to be
 * replaced was private, ... the modifications shall be discarded".  Both
 * halves are implemented, the second one deliberately: the range is
 * MEM_DECOMMITted and then MEM_COMMITted again, which is what actually
 * discards the old contents (NT zero-fills a freshly committed page).  A
 * bare MEM_COMMIT over already-committed pages succeeds and leaves the
 * old bytes in place, so it would have satisfied "the address is
 * honoured" while quietly failing "the modifications shall be
 * discarded" -- a test checking only the returned address cannot tell
 * those two apart.
 *
 *
 * msync
 * -----
 *
 * Explicit success, and an honest no-op rather than a fabricated one --
 * the precedent is src/termios/termios.c's tcflush() on the output side.
 * Under anonymous-only mappings there is never anything to flush: there
 * is no underlying object for "writes all modified copies of pages ...
 * back to the filesystem" to write back to.  The postcondition holds
 * vacuously, so returning 0 states something true.  Returning -1 would
 * be worse than useless: msync.html gives no error meaning "there was
 * nothing to do", so any failure this returned would be indistinguishable
 * from a genuine one.
 *
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/mmap.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/munmap.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/mprotect.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/msync.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/mlock.html
 */
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

#define MMAP_PAGE 4096u

/* One live mapping.  `live` is one byte per page: a page is 1 while it
 * is mapped and 0 once munmap()/MAP_FIXED has decommitted it.  A byte
 * rather than a bitmap because mappings here are small and the loop that
 * asks "is anything still live" is the only hot path, and it is not
 * hot. */
struct mapping {
	char *base;
	size_t npages;
	unsigned char *live;
};

/* mmap.html ERRORS: "[EMFILE] The number of mapped regions would exceed
 * an implementation-defined limit (per process or per system)."  This is
 * that limit, and it is the reason a fixed table is honest rather than
 * lazy: POSIX provides an error for running out of mapping slots, so
 * having a bound and reporting it is conforming. */
#define MMAP_MAX 256
static struct mapping maps[MMAP_MAX];

static size_t pground(size_t n) { return (n + MMAP_PAGE - 1) & ~(size_t)(MMAP_PAGE - 1); }
static int pgaligned(const void *p) { return ((uintptr_t)p & (MMAP_PAGE - 1)) == 0; }

/* mmap.html "Protection Options" -> NT page protection.  PROT_WRITE
 * without PROT_READ has no NT spelling (there is no write-only page
 * protection), so it widens to read/write; POSIX permits that outright:
 * "an implementation may permit accesses other than those specified by
 * prot". */
static ULONG prot_to_page(int prot)
{
	if (prot & PROT_EXEC) {
		if (prot & PROT_WRITE) return PAGE_EXECUTE_READWRITE;
		if (prot & PROT_READ)  return PAGE_EXECUTE_READ;
		return PAGE_EXECUTE;
	}
	if (prot & PROT_WRITE) return PAGE_READWRITE;
	if (prot & PROT_READ)  return PAGE_READONLY;
	return PAGE_NOACCESS;
}

/* The mapping owning [p, p+len), or NULL.  A range that straddles two
 * mappings belongs to neither: POSIX lets one munmap() span several
 * mappings, but MAP_FIXED replacement is defined against the mapping it
 * lands in, so the two callers want different things and only this one
 * wants containment. */
static struct mapping *find_containing(const void *p, size_t len)
{
	int i;
	const char *a = p;
	for (i = 0; i < MMAP_MAX; i++) {
		struct mapping *m = &maps[i];
		if (!m->base) continue;
		if (a >= m->base && a + len <= m->base + m->npages * MMAP_PAGE) return m;
	}
	return NULL;
}

static struct mapping *find_slot(void)
{
	int i;
	for (i = 0; i < MMAP_MAX; i++) if (!maps[i].base) return &maps[i];
	return NULL;
}

/* Release the whole reservation once no page of it is live. */
static void drop_if_dead(struct mapping *m)
{
	size_t i;
	PVOID b;
	SIZE_T z = 0;
	for (i = 0; i < m->npages; i++) if (m->live[i]) return;
	b = m->base;
	NtFreeVirtualMemory(NtCurrentProcess(), &b, &z, MEM_RELEASE);
	free(m->live);
	m->base = NULL;
	m->live = NULL;
	m->npages = 0;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	struct mapping *m;
	PVOID base;
	SIZE_T size;
	NTSTATUS st;
	size_t npages;
	int anon;

	/* "[EINVAL] The value of len is zero." (shall fail) */
	if (len == 0) { errno = EINVAL; return MAP_FAILED; }

	/* "[EINVAL] The value of flags is invalid (neither MAP_PRIVATE nor
	 * MAP_SHARED is set)."  Exactly one, not merely at least one --
	 * both together is not a described state. */
	if ((flags & (MAP_SHARED | MAP_PRIVATE)) != MAP_SHARED &&
	    (flags & (MAP_SHARED | MAP_PRIVATE)) != MAP_PRIVATE) {
		errno = EINVAL;
		return MAP_FAILED;
	}

	/* __MAP_ANONYMOUS, not MAP_ANONYMOUS: the user-visible spelling is
	 * gated behind _BSD_SOURCE/_GNU_SOURCE, and this file is compiled
	 * with different feature-test macros in different builds.  Reading
	 * the gated name here made the library's behaviour depend on its own
	 * compile flags -- see include/sys/mman.h. */
	anon = (flags & __MAP_ANONYMOUS) != 0;

	if (!anon) {
		/* Issue 7 has no anonymous mapping, so a caller who did not
		 * ask for the extension is making a file-backed request and
		 * gets one of the two file-backed answers.  These are two
		 * DIFFERENT failures and are kept apart deliberately -- a
		 * caller cannot tell a correct refusal from a wrong one if
		 * both come back the same way:
		 *
		 *   no valid descriptor -> [EBADF], mmap.html's shall-fail
		 *     "The fildes argument is not a valid open file
		 *     descriptor".  This is the fd = -1 case, i.e. the call
		 *     that *looks* anonymous but is not.
		 *
		 *   a valid descriptor  -> [ENODEV], "The fildes argument
		 *     refers to a file whose type is not supported by
		 *     mmap()", at its literal reading: Pass 1 supports no
		 *     file type.  Unusual -- a regular file is the canonical
		 *     mmap-able type -- and said plainly in the header so it
		 *     reads as a decision rather than an oversight. */
		if (!__fd_get(fd)) { errno = EBADF; return MAP_FAILED; }
		errno = ENODEV;
		return MAP_FAILED;
	}

	/* "[EINVAL] ... off is not a multiple of the page size".  For an
	 * anonymous mapping there is no object to offset into, so the only
	 * meaningful offset is zero; anything else is a caller error rather
	 * than something to silently ignore. */
	if (off != 0) { errno = EINVAL; return MAP_FAILED; }

	npages = pground(len) / MMAP_PAGE;

	if (flags & MAP_FIXED) {
		size_t first, i;
		PVOID p;
		SIZE_T z;
		/* "[EINVAL] MAP_FIXED was specified, and ... addr is not a
		 * multiple of the page size." */
		if (!pgaligned(addr)) { errno = EINVAL; return MAP_FAILED; }
		/* Page-granular commit works only inside a reservation we
		 * already own; there is no way to plant a mapping at an
		 * arbitrary address otherwise, and inventing one would mean
		 * reserving at 64 KiB granularity and lying about the base.
		 * "[ENOMEM] MAP_FIXED was specified, and the range
		 * [addr,addr+len) exceeds that allowed for the address space
		 * of a process." */
		m = find_containing(addr, len);
		if (!m) { errno = ENOMEM; return MAP_FAILED; }

		first = (size_t)(((char *)addr - m->base) / MMAP_PAGE);

		/* Decommit then commit, so the previous mapping's
		 * modifications are actually discarded -- see the banner. */
		p = addr;
		z = npages * MMAP_PAGE;
		NtFreeVirtualMemory(NtCurrentProcess(), &p, &z, MEM_DECOMMIT);

		p = addr;
		z = npages * MMAP_PAGE;
		st = NtAllocateVirtualMemory(NtCurrentProcess(), &p, 0, &z,
		                             MEM_COMMIT, prot_to_page(prot));
		if (!NT_SUCCESS(st)) {
			/* The old contents are gone either way; the pages are
			 * dead, and saying so keeps the bookkeeping true. */
			for (i = 0; i < npages && first + i < m->npages; i++) m->live[first + i] = 0;
			drop_if_dead(m);
			__set_errno_status(st);
			return MAP_FAILED;
		}
		for (i = 0; i < npages && first + i < m->npages; i++) m->live[first + i] = 1;
		return addr;
	}

	m = find_slot();
	if (!m) { errno = EMFILE; return MAP_FAILED; }

	base = NULL;
	size = npages * MMAP_PAGE;
	st = NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size,
	                             MEM_RESERVE | MEM_COMMIT, prot_to_page(prot));
	if (!NT_SUCCESS(st)) { __set_errno_status(st); return MAP_FAILED; }

	m->live = malloc(npages);
	if (!m->live) {
		PVOID b = base;
		SIZE_T z = 0;
		NtFreeVirtualMemory(NtCurrentProcess(), &b, &z, MEM_RELEASE);
		errno = ENOMEM;
		return MAP_FAILED;
	}
	memset(m->live, 1, npages);
	m->base = base;
	m->npages = npages;
	return base;
}

int munmap(void *addr, size_t len)
{
	size_t npages, i;
	int k;
	char *a = addr;

	/* munmap.html ERRORS, all three of them:
	 *   "[EINVAL] Addresses in the range [addr,addr+len) are outside
	 *     the valid range for the address space of a process."
	 *   "[EINVAL] The len argument is 0."
	 *   "[EINVAL] The addr argument is not a multiple of the page size
	 *     as returned by sysconf()."
	 * That is the whole list -- which is the reason this implementation
	 * is shaped the way it is; see the banner. */
	if (len == 0) { errno = EINVAL; return -1; }
	if (!pgaligned(addr)) { errno = EINVAL; return -1; }

	npages = pground(len) / MMAP_PAGE;

	/* "The munmap() function shall remove any mappings for those entire
	 * pages containing any part of the address space of the process
	 * starting at addr and continuing for len bytes ... If there are no
	 * mappings in the specified address range, then munmap() has no
	 * effect."  So an unmapped-but-valid range is a SUCCESS, not an
	 * error, and a range spanning several mappings unmaps all of them.
	 * Both fall out of walking the table rather than requiring one
	 * mapping to contain the range. */
	for (k = 0; k < MMAP_MAX; k++) {
		struct mapping *m = &maps[k];
		char *lo, *hi;
		if (!m->base) continue;
		lo = a > m->base ? a : m->base;
		hi = a + npages * MMAP_PAGE;
		if (hi > m->base + m->npages * MMAP_PAGE) hi = m->base + m->npages * MMAP_PAGE;
		if (lo >= hi) continue;
		{
			PVOID p = lo;
			SIZE_T z = (SIZE_T)(hi - lo);
			size_t first = (size_t)(lo - m->base) / MMAP_PAGE;
			size_t n = (size_t)(hi - lo) / MMAP_PAGE;
			/* MEM_DECOMMIT over the subrange: page-granular, keeps
			 * the reservation, leaves the rest of the mapping
			 * exactly where it was. */
			NtFreeVirtualMemory(NtCurrentProcess(), &p, &z, MEM_DECOMMIT);
			for (i = 0; i < n; i++) m->live[first + i] = 0;
			drop_if_dead(m);
		}
	}
	return 0;
}

int mprotect(void *addr, size_t len, int prot)
{
	PVOID p = addr;
	SIZE_T z;
	ULONG old = 0;
	NTSTATUS st;

	/* mprotect.html ERRORS: "[EINVAL] The addr argument is not a
	 * multiple of the page size as returned by sysconf()." */
	if (!pgaligned(addr)) { errno = EINVAL; return -1; }
	if (len == 0) return 0;

	z = pground(len);
	st = NtProtectVirtualMemory(NtCurrentProcess(), &p, &z, prot_to_page(prot), &old);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int msync(void *addr, size_t len, int flags)
{
	/* An honest no-op, not a fabricated success -- see the banner.
	 * The arguments are still validated, because a caller who passes a
	 * misaligned address has made an error whether or not there is
	 * anything to flush, and msync.html does give an error for it:
	 * "[EINVAL] The value of addr is not a multiple of the page size as
	 * returned by sysconf()", and "[EINVAL] The value of flags is
	 * invalid". */
	if (!pgaligned(addr)) { errno = EINVAL; return -1; }
	if ((flags & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE)) != 0) { errno = EINVAL; return -1; }
	/* "[EINVAL] The value of flags includes both MS_ASYNC and MS_SYNC." */
	if ((flags & MS_ASYNC) && (flags & MS_SYNC)) { errno = EINVAL; return -1; }
	(void)len;
	return 0;
}

/* mlock.html: "shall cause those whole pages containing any part of the
 * address space ... to be memory-resident until unlocked".
 *
 * NtLockVirtualMemory is a real export and really implemented, on NT and
 * in Wine alike (see src/internal/nt.h's note), so this is a genuine
 * lock rather than a wrapper that reports success without doing
 * anything.  It is bounded by a resource limit on both -- a working-set
 * quota on NT, RLIMIT_MEMLOCK on the host under Wine -- and that limit is
 * an ENVIRONMENT property, not a platform one: the same binary succeeds
 * on a machine with a generous limit and fails on one without.  That is
 * why test/posix-mman.c keys its skip on the limit it measures rather
 * than on which system it believes it is running. */
static int lock_range(const void *addr, size_t len, int lock)
{
	PVOID p = (PVOID)(uintptr_t)addr;
	SIZE_T z;
	NTSTATUS st;

	if (len == 0) { errno = EINVAL; return -1; }
	z = pground(len + ((uintptr_t)addr & (MMAP_PAGE - 1)));
	p = (PVOID)((uintptr_t)addr & ~(uintptr_t)(MMAP_PAGE - 1));
	st = lock ? NtLockVirtualMemory(NtCurrentProcess(), &p, &z, 1)
	          : NtUnlockVirtualMemory(NtCurrentProcess(), &p, &z, 1);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int mlock(const void *addr, size_t len)   { return lock_range(addr, len, 1); }
int munlock(const void *addr, size_t len) { return lock_range(addr, len, 0); }
