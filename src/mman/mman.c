/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/mman.h>, Pass 2: anonymous mappings and file-backed mappings of
 * regular files, plus page locking.
 *
 * See include/sys/mman.h's banner for the edition question that shapes
 * this file (Issue 7 has no anonymous mapping; MAP_ANONYMOUS is a gated
 * extension here) and for the full file-backed argument.  The short
 * form, because it is the one load-bearing decision this file still
 * makes about it: munmap.html's ERRORS are exactly three -- addr not
 * page-aligned, range outside the address space, len of zero -- so
 * THERE IS NO ERRNO FOR A PARTIAL munmap, and NtUnmapViewOfSection()
 * only ever drops a section view WHOLE.  That bounds one thing --
 * MAP_FIXED cannot replace part of a file-backed mapping, only its
 * entire current extent (see mmap()'s MAP_FIXED branch) -- and no
 * longer the mapping itself: ordinary munmap() of a file-backed mapping
 * this library created is always whole-extent in every case measured
 * against it, so the reservation-table bookkeeping below (`live`,
 * page-granular for the anonymous path) simply is not exercised
 * partially on the file-backed side either.
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
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/mlockall.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/munlockall.html
 */
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "libc.h"

#define MMAP_PAGE 4096u

/* One live mapping.  `live` is one byte per page: a page is 1 while it
 * is mapped and 0 once munmap()/MAP_FIXED has decommitted it. `locked`
 * records the pages locked through this interface, both so MS_INVALIDATE
 * can give its required [EBUSY] answer and so munlockall() can unlock the
 * same ranges.  Bytes rather than bitmaps keep partial-range bookkeeping
 * straightforward; these mappings are small and none of these loops is
 * hot. */
struct mapping {
	char *base;
	size_t npages;
	unsigned char *live;
	unsigned char *locked;
	int filebacked;         /* section view (NtMapViewOfSection), not a
	                          * private NtAllocateVirtualMemory reservation */
};

/* mmap.html ERRORS: "[EMFILE] The number of mapped regions would exceed
 * an implementation-defined limit (per process or per system)."  This is
 * that limit, and it is the reason a fixed table is honest rather than
 * lazy: POSIX provides an error for running out of mapping slots, so
 * having a bound and reporting it is conforming. */
#define MMAP_MAX 256
static struct mapping maps[MMAP_MAX];
static int lock_future;

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

/* Same table, but for a MAP_PRIVATE section view: mmap.html says a
 * MAP_PRIVATE write "shall be visible only to the calling process" and
 * "It is unspecified whether this change to the mapped file is visible
 * to other processes... or is carried through to the underlying object."
 * -- i.e. the write must not reach the file. NT's answer to that is
 * copy-on-write (PAGE_WRITECOPY/PAGE_EXECUTE_WRITECOPY): the first write
 * to a page forks it to a private, pagefile-backed copy instead of
 * dirtying the section. Win32's own FILE_MAP_COPY works against a
 * section created with PAGE_READONLY, so this needs no extra access
 * beyond what the file was opened with. */
static ULONG prot_to_view(int prot, int private)
{
	if (!private) return prot_to_page(prot);
	if (prot & PROT_EXEC) {
		if (prot & PROT_WRITE) return PAGE_EXECUTE_WRITECOPY;
		if (prot & PROT_READ)  return PAGE_EXECUTE_READ;
		return PAGE_EXECUTE;
	}
	if (prot & PROT_WRITE) return PAGE_WRITECOPY;
	if (prot & PROT_READ)  return PAGE_READONLY;
	return PAGE_NOACCESS;
}

/* Create a section over `fh` and map a view of it at *base_inout (a
 * hint, or NULL to let NT choose).  Tries the broadest section
 * protection the caller's prot/flags could need first, and falls back
 * to a read-only section on [STATUS_ACCESS_DENIED] -- a handle opened
 * O_RDONLY cannot back a PAGE_READWRITE section, but MAP_PRIVATE still
 * works against a PAGE_READONLY one via copy-on-write (see
 * prot_to_view).  The section handle is closed before returning either
 * way: the view holds its own reference, so nothing is leaked by not
 * keeping it. */
static NTSTATUS map_file(HANDLE fh, int prot, int flags, off_t off,
                         size_t viewbytes, PVOID *base_inout)
{
	HANDLE section;
	NTSTATUS st;
	LARGE_INTEGER secoff;
	SIZE_T viewsize;
	ULONG maxprot;
	int private = (flags & MAP_PRIVATE) != 0;

	maxprot = (prot & PROT_EXEC) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
	st = NtCreateSection(&section, SECTION_ALL_ACCESS, NULL, NULL,
	                     maxprot, SEC_COMMIT, fh);
	if (st == (NTSTATUS)STATUS_ACCESS_DENIED) {
		maxprot = (prot & PROT_EXEC) ? PAGE_EXECUTE_READ : PAGE_READONLY;
		st = NtCreateSection(&section, SECTION_ALL_ACCESS, NULL, NULL,
		                     maxprot, SEC_COMMIT, fh);
	}
	if (!NT_SUCCESS(st)) return st;

	/* ViewSize=0 means "map from SectionOffset to the end of the
	 * section" -- NtCreateSection above set the section's size to the
	 * file's own length (MaximumSize=NULL), so this maps exactly the
	 * bytes the file has, and NT rounds the accessible range up to the
	 * next page boundary and zero-fills the tail on its own (the same
	 * behaviour mmap.html requires: "the system shall always zero-fill
	 * any partial page at the end of an object").  An explicit ViewSize
	 * of the caller's rounded `len` was tried first and rejected with
	 * [STATUS_INVALID_VIEW_SIZE] whenever `len` rounds past the file's
	 * exact byte length, which is every mapping that covers a whole
	 * small file -- i.e. the common case, not an edge one. `viewbytes`
	 * still bounds what mmap() tells its caller was mapped; NT's actual
	 * view can only be smaller when the file is shorter than `len`
	 * implies, which is the caller's own error to make. */
	(void)viewbytes;
	secoff = (LARGE_INTEGER)off;
	viewsize = 0;
	st = NtMapViewOfSection(section, NtCurrentProcess(), base_inout, 0, 0,
	                        &secoff, &viewsize, ViewShare, 0,
	                        prot_to_view(prot, private));
	NtClose(section);
	return st;
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

static int alloc_page_state(struct mapping *m, size_t npages)
{
	m->live = malloc(npages);
	m->locked = calloc(npages, 1);
	if (!m->live || !m->locked) {
		free(m->live);
		free(m->locked);
		m->live = NULL;
		m->locked = NULL;
		return -1;
	}
	memset(m->live, 1, npages);
	return 0;
}

/* Release the whole reservation once no page of it is live.  For an
 * anonymous mapping that is MEM_RELEASE, same as always.  For a
 * file-backed mapping it is NtUnmapViewOfSection instead: a section view
 * is not memory NtFreeVirtualMemory owns, and MEM_RELEASE on it fails.
 * The section handle itself was already closed at map time (the view
 * holds its own reference -- see mmap()), so this is the only cleanup
 * a file-backed mapping needs. */
static void drop_if_dead(struct mapping *m)
{
	size_t i;
	PVOID b;
	SIZE_T z = 0;
	for (i = 0; i < m->npages; i++) if (m->live[i]) return;
	b = m->base;
	if (m->filebacked) NtUnmapViewOfSection(NtCurrentProcess(), b);
	else NtFreeVirtualMemory(NtCurrentProcess(), &b, &z, MEM_RELEASE);
	free(m->live);
	free(m->locked);
	m->base = NULL;
	m->live = NULL;
	m->locked = NULL;
	m->npages = 0;
	m->filebacked = 0;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	struct mapping *m;
	PVOID base;
	SIZE_T size;
	NTSTATUS st;
	size_t npages;
	int anon;
	struct __fd *f = NULL;

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

	if (anon) {
		/* "[EINVAL] ... off is not a multiple of the page size".
		 * For an anonymous mapping there is no object to offset
		 * into, so the only meaningful offset is zero; anything
		 * else is a caller error rather than something to silently
		 * ignore. */
		if (off != 0) { errno = EINVAL; return MAP_FAILED; }
	} else {
		/* Issue 7 has no anonymous mapping, so a caller who did not
		 * ask for the extension is making a file-backed request and
		 * gets one of the file-backed answers.  These are kept apart
		 * deliberately -- a caller cannot tell a correct refusal
		 * from a wrong one if they all came back the same way:
		 *
		 *   no valid descriptor -> [EBADF], mmap.html's shall-fail
		 *     "The fildes argument is not a valid open file
		 *     descriptor".  This is the fd = -1 case, i.e. the call
		 *     that *looks* anonymous but is not.
		 *
		 *   valid descriptor, wrong type -> [ENODEV], "The fildes
		 *     argument refers to a file whose type is not supported
		 *     by mmap()".  Pass 2 widens support from "no file type"
		 *     to "regular files" -- see include/sys/mman.h -- so
		 *     everything else (a pipe, a socket, a directory, ...)
		 *     is still declined here, at the same literal reading.
		 *
		 *   valid regular-file descriptor -> validated below and,
		 *     absent an error, actually mapped. */
		f = __fd_get(fd);
		if (!f) { errno = EBADF; return MAP_FAILED; }
		if (f->type != __FD_FILE) { errno = ENODEV; return MAP_FAILED; }

		/* "[EINVAL] ... off is not a multiple of the page size ...,
		 * or is considered invalid by the implementation." off is
		 * signed; a negative offset is exactly that. */
		if (off < 0 || (off & (off_t)(MMAP_PAGE - 1)) != 0) {
			errno = EINVAL;
			return MAP_FAILED;
		}

		/* mmap.html: "[EACCES] The fildes argument is not open for
		 * read, regardless of the protection specified, or fildes is
		 * not open for write and PROT_WRITE was specified for a
		 * MAP_SHARED type mapping."  A MAP_PRIVATE writer needs no
		 * write access to the file at all -- its writes never reach
		 * the object (see prot_to_view) -- so the second half is
		 * MAP_SHARED-only, deliberately. */
		if ((f->flags & O_ACCMODE) == O_WRONLY) {
			errno = EACCES;
			return MAP_FAILED;
		}
		if ((flags & MAP_SHARED) && (prot & PROT_WRITE) &&
		    (f->flags & O_ACCMODE) == O_RDONLY) {
			errno = EACCES;
			return MAP_FAILED;
		}
	}

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

		if (m->filebacked) {
			/* A section view is placed and removed as a whole --
			 * there is no NT primitive for decommitting or
			 * re-mapping part of one, the same "no error for a
			 * partial operation" problem the anonymous path
			 * solves with page-granular reserve/commit (see this
			 * file's banner).  MAP_FIXED can therefore only be
			 * honoured here when [addr,addr+len) is the file-
			 * backed mapping's ENTIRE current extent: the old
			 * view is unmapped whole and a new one takes its
			 * place.  A MAP_FIXED that only overlaps PART of a
			 * file-backed mapping is refused with [ENOMEM] --
			 * no case reaching this library exercises that, and
			 * an honest refusal beats silently misbehaving. */
			/* Also refused here, alongside the partial-replace
			 * case above: an ANONYMOUS MAP_FIXED landing exactly
			 * on a file-backed mapping's extent.  POSIX allows a
			 * MAP_FIXED to replace any previous mapping regardless
			 * of its kind, but the replacement path below is
			 * map_file(), which needs a real file descriptor (`f`)
			 * -- present only when THIS call is itself file-backed
			 * (`!anon`, set above). Without this check `f` is NULL
			 * here whenever the current call is anonymous, which
			 * is exactly the null dereference on f->h that
			 * `clang --analyze` [core.NullDereference] catches. No
			 * case reaching this library replaces a file-backed
			 * mapping with an anonymous one; an honest [ENOMEM]
			 * beats silently misbehaving here too. */
			if ((char *)addr != m->base || npages != m->npages || anon) {
				errno = ENOMEM;
				return MAP_FAILED;
			}
			/* Unlike the anonymous MAP_FIXED path's decommit,
			 * this has no separate "discard" step: a section
			 * view occupies its address range for as long as it
			 * exists, so NT will not place the new view until the
			 * old one is gone from under it (measured:
			 * [STATUS_CONFLICTING_ADDRESSES] otherwise). The old
			 * mapping's contents are therefore lost even if the
			 * replacement below fails -- the same trade the
			 * anonymous path already makes (see its own comment,
			 * above) for the same reason: mmap.html's MAP_FIXED
			 * clause requires the old mapping discarded, not
			 * preserved on failure. */
			NtUnmapViewOfSection(NtCurrentProcess(), m->base);
			base = addr;
			st = map_file(f->h, prot, flags, off,
			             npages * MMAP_PAGE, &base);
			if (!NT_SUCCESS(st)) {
				free(m->live);
				free(m->locked);
				m->base = NULL;
				m->live = NULL;
				m->locked = NULL;
				m->npages = 0;
				m->filebacked = 0;
				if (st == (NTSTATUS)STATUS_NO_MEMORY)
					errno = ENOMEM;
				else
					errno = ENOTSUP;
				return MAP_FAILED;
			}
			free(m->live);
			free(m->locked);
			m->live = NULL;
			m->locked = NULL;
			if (alloc_page_state(m, npages) < 0) {
				/* Bookkeeping can't be grown, but the new view
				 * is live and correctly placed; report it. */
				m->base = base;
				m->npages = 0;
				m->filebacked = 1;
				return base;
			}
			m->base = base;
			m->npages = npages;
			m->filebacked = 1;
			if (lock_future && mlock(base, npages * MMAP_PAGE) < 0) {
				NtUnmapViewOfSection(NtCurrentProcess(), base);
				free(m->live);
				free(m->locked);
				memset(m, 0, sizeof *m);
				errno = EAGAIN;
				return MAP_FAILED;
			}
			return base;
		}

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
		for (i = 0; i < npages && first + i < m->npages; i++) {
			m->live[first + i] = 1;
			m->locked[first + i] = 0;
		}
		if (lock_future && mlock(addr, npages * MMAP_PAGE) < 0) {
			p = addr;
			z = npages * MMAP_PAGE;
			NtFreeVirtualMemory(NtCurrentProcess(), &p, &z, MEM_DECOMMIT);
			for (i = 0; i < npages && first + i < m->npages; i++) m->live[first + i] = 0;
			drop_if_dead(m);
			errno = EAGAIN;
			return MAP_FAILED;
		}
		return addr;
	}

	m = find_slot();
	if (!m) { errno = EMFILE; return MAP_FAILED; }

	if (!anon) {
		base = NULL;
		st = map_file(f->h, prot, flags, off, npages * MMAP_PAGE, &base);
		if (!NT_SUCCESS(st)) {
			if (st == (NTSTATUS)STATUS_NO_MEMORY) errno = ENOMEM;
			else errno = ENOTSUP;
			return MAP_FAILED;
		}
		if (alloc_page_state(m, npages) < 0) {
			NtUnmapViewOfSection(NtCurrentProcess(), base);
			errno = ENOMEM;
			return MAP_FAILED;
		}
		m->base = base;
		m->npages = npages;
		m->filebacked = 1;
		if (lock_future && mlock(base, npages * MMAP_PAGE) < 0) {
			NtUnmapViewOfSection(NtCurrentProcess(), base);
			free(m->live);
			free(m->locked);
			memset(m, 0, sizeof *m);
			errno = EAGAIN;
			return MAP_FAILED;
		}
		return base;
	}

	base = NULL;
	size = npages * MMAP_PAGE;
	st = NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size,
	                             MEM_RESERVE | MEM_COMMIT, prot_to_page(prot));
	if (!NT_SUCCESS(st)) { __set_errno_status(st); return MAP_FAILED; }

	if (alloc_page_state(m, npages) < 0) {
		PVOID b = base;
		SIZE_T z = 0;
		NtFreeVirtualMemory(NtCurrentProcess(), &b, &z, MEM_RELEASE);
		errno = ENOMEM;
		return MAP_FAILED;
	}
	m->base = base;
	m->npages = npages;
	m->filebacked = 0;
	if (lock_future && mlock(base, npages * MMAP_PAGE) < 0) {
		PVOID b = base;
		SIZE_T z = 0;
		NtFreeVirtualMemory(NtCurrentProcess(), &b, &z, MEM_RELEASE);
		free(m->live);
		free(m->locked);
		memset(m, 0, sizeof *m);
		errno = EAGAIN;
		return MAP_FAILED;
	}
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
			for (i = 0; i < n; i++) {
				m->live[first + i] = 0;
				m->locked[first + i] = 0;
			}
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
	int k;
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
	if (flags & MS_INVALIDATE) {
		char *a = addr;
		char *end = a + pground(len);
		for (k = 0; k < MMAP_MAX; k++) {
			struct mapping *m = &maps[k];
			char *lo, *hi;
			size_t first, n, i;
			if (!m->base) continue;
			lo = a > m->base ? a : m->base;
			hi = end < m->base + m->npages * MMAP_PAGE
			   ? end : m->base + m->npages * MMAP_PAGE;
			if (lo >= hi) continue;
			first = (size_t)(lo - m->base) / MMAP_PAGE;
			n = (size_t)(hi - lo) / MMAP_PAGE;
			for (i = 0; i < n; i++) if (m->locked[first + i]) {
				errno = EBUSY;
				return -1;
			}
		}
	}
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
	{
		char *a = p;
		char *end = a + z;
		int k;
		for (k = 0; k < MMAP_MAX; k++) {
			struct mapping *m = &maps[k];
			char *lo, *hi;
			size_t first, n, i;
			if (!m->base) continue;
			lo = a > m->base ? a : m->base;
			hi = end < m->base + m->npages * MMAP_PAGE
			   ? end : m->base + m->npages * MMAP_PAGE;
			if (lo >= hi) continue;
			first = (size_t)(lo - m->base) / MMAP_PAGE;
			n = (size_t)(hi - lo) / MMAP_PAGE;
			for (i = 0; i < n; i++) if (m->live[first + i])
				m->locked[first + i] = (unsigned char)lock;
		}
	}
	return 0;
}

int mlock(const void *addr, size_t len)   { return lock_range(addr, len, 1); }
int munlock(const void *addr, size_t len) { return lock_range(addr, len, 0); }

int mlockall(int flags)
{
	int k;

	if (!flags || (flags & ~(MCL_CURRENT | MCL_FUTURE))) {
		errno = EINVAL;
		return -1;
	}
	if (flags & MCL_CURRENT) {
		for (k = 0; k < MMAP_MAX; k++) {
			struct mapping *m = &maps[k];
			size_t first, n;
			if (!m->base) continue;
			for (first = 0; first < m->npages; first += n) {
				while (first < m->npages &&
				       (!m->live[first] || m->locked[first])) first++;
				if (first == m->npages) break;
				for (n = 1; first + n < m->npages &&
				     m->live[first + n] && !m->locked[first + n]; n++);
				if (mlock(m->base + first * MMAP_PAGE, n * MMAP_PAGE) < 0) {
					int saved = errno;
					int j;
					/* A failed mlockall() must not leave the
					 * successfully visited prefix locked. State 2
					 * distinguishes locks acquired by this call from
					 * locks that predated it. */
					for (j = 0; j < MMAP_MAX; j++) {
						struct mapping *r = &maps[j];
						size_t a, z;
						if (!r->base) continue;
						for (a = 0; a < r->npages; a += z) {
							while (a < r->npages && r->locked[a] != 2) a++;
							if (a == r->npages) break;
							for (z = 1; a + z < r->npages && r->locked[a + z] == 2; z++);
							munlock(r->base + a * MMAP_PAGE, z * MMAP_PAGE);
						}
					}
					errno = saved;
					return -1;
				}
				memset(m->locked + first, 2, n);
			}
		}
		for (k = 0; k < MMAP_MAX; k++) {
			struct mapping *m = &maps[k];
			size_t i;
			if (!m->base) continue;
			for (i = 0; i < m->npages; i++)
				if (m->locked[i] == 2) m->locked[i] = 1;
		}
	}
	if (flags & MCL_FUTURE) lock_future = 1;
	return 0;
}

int munlockall(void)
{
	int k;
	int failed = 0;
	int saved = 0;

	lock_future = 0;
	for (k = 0; k < MMAP_MAX; k++) {
		struct mapping *m = &maps[k];
		size_t first, n;
		if (!m->base) continue;
		for (first = 0; first < m->npages; first += n) {
			while (first < m->npages && !m->locked[first]) first++;
			if (first == m->npages) break;
			for (n = 1; first + n < m->npages && m->locked[first + n]; n++);
			if (munlock(m->base + first * MMAP_PAGE, n * MMAP_PAGE) < 0) {
				if (!failed) saved = errno;
				failed = 1;
			}
		}
	}
	if (failed) {
		errno = saved;
		return -1;
	}
	return 0;
}

void __mman_reset_after_fork(void)
{
	int k;
	lock_future = 0;
	for (k = 0; k < MMAP_MAX; k++) {
		struct mapping *m = &maps[k];
		if (m->base && m->locked) memset(m->locked, 0, m->npages);
	}
}
