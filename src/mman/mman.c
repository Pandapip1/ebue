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
 * THERE IS NO ERRNO FOR A PARTIAL munmap, and the platform's unmap-view
 * primitive only ever drops a section view WHOLE (see
 * src/internal/plat_mem.h).  That bounds one thing -- MAP_FIXED cannot
 * replace part of a file-backed mapping, only its entire current extent
 * (see mmap()'s MAP_FIXED branch) -- and no longer the mapping itself:
 * ordinary munmap() of a file-backed mapping this library created is
 * always whole-extent in every case measured against it, so the
 * reservation-table bookkeeping below (`live`, page-granular for the
 * anonymous path) simply is not exercised partially on the file-backed
 * side either.
 *
 *
 * One reservation per mapping, and why
 * ------------------------------------
 *
 * The platform's reserve/commit split (see plat_mem.h) is what makes a
 * conforming partial munmap() possible.  Each mmap() takes its OWN
 * reservation via __plat_mem_reserve().  A partial munmap() then
 * decommits just the subrange -- page-granular, which is what POSIX
 * needs -- and keeps the reservation, so the surviving pages of the
 * same mapping are untouched and still at their own addresses.  Only
 * when the last live page of a mapping goes does the reservation itself
 * get released.  Releasing a reservation cannot free part of it (it
 * takes the whole thing, base address and size zero), which is exactly
 * why one reservation per mapping rather than one big arena: a shared
 * arena could never release anything until every mapping in it died.
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
 * This is an NT reservation-granularity property (64 KiB allocation
 * granularity, page-granular commit/decommit within it); a backend
 * whose native mmap/munmap are page-granular end to end would not need
 * this reservation-table strategy at all.  It stays in this file rather
 * than behind __plat_mem_* because it is this library's OWN chosen
 * strategy for satisfying POSIX's partial-munmap requirement -- shared
 * verbatim by whichever backend is compiled in, not something each
 * backend reimplements or could opt out of.
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
 * decommitted and then committed again (__plat_mem_commit_fixed), which
 * is what actually discards the old contents (a freshly committed page
 * comes back zero-filled).  A bare commit over already-committed pages
 * succeeds and leaves the old bytes in place, so it would have satisfied
 * "the address is honoured" while quietly failing "the modifications
 * shall be discarded" -- a test checking only the returned address
 * cannot tell those two apart.
 *
 *
 * msync
 * -----
 *
 * Shared file views are flushed through __plat_mem_flush_view().  NT and
 * Wine do not reliably mark the file timestamps when a section's dirty
 * pages are flushed, so writable shared mappings retain an independent
 * file handle and that call explicitly marks LastWriteTime and
 * ChangeTime.  The independent handle matters because POSIX permits the
 * caller to close fildes after mmap().  Anonymous mappings and private
 * file views have no object to update, so success for those remains a
 * real no-op.
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
#include "plat_mem.h"
#include "plat_fd.h"

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
	int filebacked;         /* section view, not a private anonymous
	                          * reservation -- see plat_mem.h */
	__plat_handle_t writeback; /* independent writable MAP_SHARED file handle */
};

/* mmap.html ERRORS: "[EMFILE] The number of mapped regions would exceed
 * an implementation-defined limit (per process or per system)."  This is
 * that limit, and it is the reason a fixed table is honest rather than
 * lazy: POSIX provides an error for running out of mapping slots, so
 * having a bound and reporting it is conforming. */
#define MMAP_MAX 256
static struct mapping maps[MMAP_MAX];
static int lock_future;

/* A residual worth stating once rather than at each site below: mmap()/
 * munmap()/msync()/lock_range()/mlockall()/munlockall() all flag on
 * `m->live[...]`/`m->locked[...]` where m is a LOCAL, `struct mapping *m
 * = &maps[k];` (or the MAP_FIXED arm's `m = find_containing(...)`,
 * already null-checked there). None of those functions takes a struct
 * mapping * as its own parameter -- m is always the address of a
 * fixed-size static table entry or an already-verified result -- so
 * nonnull has nothing on any of their signatures to describe; the same
 * class of "not expressible via nonnull, not a parameter" residual
 * d24fe86's own commit documents for src/process/children.c's
 * __children[i] findings. drop_if_dead() just below is the one function
 * in this family where m genuinely IS a parameter, and is marked
 * accordingly. */

static size_t pground(size_t n) { return (n + MMAP_PAGE - 1) & ~(size_t)(MMAP_PAGE - 1); }
static int pgaligned(const void *p) { return ((uintptr_t)p & (MMAP_PAGE - 1)) == 0; }

/* The mapping owning [p, p+len), or NULL.  A range that straddles two
 * mappings belongs to neither: POSIX lets one munmap() span several
 * mappings, but MAP_FIXED replacement is defined against the mapping it
 * lands in, so the two callers want different things and only this one
 * wants containment. */
/* Address-range containment and intersection against this allocator's
 * own bookkept mapping bases (below, and in munmap()/msync()/
 * lock_range()/mmap()'s MAP_FIXED-replacement branch) is a flat-
 * address-space question about two independently obtained pointers --
 * a caller's argument and one of `maps[]`'s own bases, populated by an
 * earlier, unrelated mmap() call -- not a same-object relationship.
 * ISO C only defines <, <=, >, >=, and - between pointers into the same
 * array object (6.5.6p9, 6.5.8p5); comparing or subtracting across
 * `maps[]`'s entries needs uintptr_t for the same reason src/string/
 * memmove.c's copy-direction test does.  Centralised here rather than
 * cast at each of the five call sites so the reasoning is written down
 * once. */
static int addr_lt(const void *a, const void *b) { return (uintptr_t)a < (uintptr_t)b; }
static int addr_le(const void *a, const void *b) { return (uintptr_t)a <= (uintptr_t)b; }
static int addr_gt(const void *a, const void *b) { return (uintptr_t)a > (uintptr_t)b; }
static int addr_ge(const void *a, const void *b) { return (uintptr_t)a >= (uintptr_t)b; }
static size_t addr_diff(const void *a, const void *b) { return (size_t)((uintptr_t)a - (uintptr_t)b); }

static struct mapping *find_containing(const void *p, size_t len)
{
	int i;
	const char *a = p;
	for (i = 0; i < MMAP_MAX; i++) {
		struct mapping *m = &maps[i];
		if (!m->base) continue;
		if (addr_ge(a, m->base) && addr_le(a + len, m->base + m->npages * MMAP_PAGE)) return m;
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
 * anonymous mapping that is __plat_mem_release(), same as always.  For a
 * file-backed mapping it is __plat_mem_unmap_view() instead: a section
 * view is not memory a reservation-release call owns.  The section
 * handle itself was already closed at map time (the view holds its own
 * reference -- see mmap()); writable shared views also close the
 * independent writeback handle retained for msync().
 *
 * m required: the loop condition (`m->npages`) and every field write
 * below it are unconditional. Every real call site passes either `m`
 * where it was already dereferenced (mmap()'s MAP_FIXED arm, guarded by
 * its own earlier `if (!m) { errno = ENOMEM; return MAP_FAILED; }`) or
 * `&maps[k]`/`&maps[i]`, the address of a fixed-size table entry --
 * never NULL either way. */
static void drop_if_dead(struct mapping *m) __attribute__((nonnull(1)));
static void drop_if_dead(struct mapping *m)
{
	size_t i;
	/* m->live[i]: not expressible via nonnull on m itself (already
	 * marked above) -- a fact about one of m's FIELDS, not m. It is
	 * never NULL in practice: every real call site dereferences
	 * m->live directly, in the same scope, moments before calling this
	 * function (mmap()'s and munmap()'s own `m->live[first + i] = 0;`
	 * loops just above their own drop_if_dead(m) calls), which already
	 * proves it by hand at the point each call is made -- a fact this
	 * function's own signature has no way to restate. */
	for (i = 0; i < m->npages; i++) if (m->live[i]) return;
	if (m->filebacked) __plat_mem_unmap_view(m->base, m->npages * MMAP_PAGE);
	else __plat_mem_release(m->base, m->npages * MMAP_PAGE);
	if (m->writeback) __plat_close(m->writeback);
	free(m->live);
	free(m->locked);
	m->base = NULL;
	m->live = NULL;
	m->locked = NULL;
	m->npages = 0;
	m->filebacked = 0;
	m->writeback = __PLAT_HANDLE_NULL;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	struct mapping *m;
	void *base;
	size_t size;
	size_t npages;
	int anon;
	struct __fd *f = NULL;
	__plat_handle_t writeback = __PLAT_HANDLE_NULL;

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
		 * the object (see prot_to_view in the backend) -- so the
		 * second half is MAP_SHARED-only, deliberately. */
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
		/* "[EINVAL] MAP_FIXED was specified, and ... addr is not a
		 * multiple of the page size." */
		if (!pgaligned(addr)) { errno = EINVAL; return MAP_FAILED; }
		/* Page-granular commit works only inside a reservation we
		 * already own; there is no way to plant a mapping at an
		 * arbitrary address otherwise, and inventing one would mean
		 * reserving at allocation granularity and lying about the
		 * base. "[ENOMEM] MAP_FIXED was specified, and the range
		 * [addr,addr+len) exceeds that allowed for the address space
		 * of a process." */
		m = find_containing(addr, len);
		if (!m) { errno = ENOMEM; return MAP_FAILED; }

		if (m->filebacked) {
			/* A section view is placed and removed as a whole --
			 * there is no primitive for decommitting or
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
			 * __plat_mem_map_file(), which needs a real file
			 * descriptor (`f`) -- present only when THIS call is
			 * itself file-backed (`!anon`, set above). Without
			 * this check `f` is NULL here whenever the current
			 * call is anonymous, which is exactly the null
			 * dereference on f->h that `clang --analyze`
			 * [core.NullDereference] catches. No case reaching
			 * this library replaces a file-backed mapping with an
			 * anonymous one; an honest [ENOMEM] beats silently
			 * misbehaving here too. */
			if ((char *)addr != m->base || npages != m->npages || anon) {
				errno = ENOMEM;
				return MAP_FAILED;
			}
			if ((flags & MAP_SHARED) &&
			    (f->flags & O_ACCMODE) == O_RDWR) {
				if (__plat_dup(f->h, 0, &writeback) < 0)
					return MAP_FAILED;
			}
			/* Unlike the anonymous MAP_FIXED path's decommit,
			 * this has no separate "discard" step: a section
			 * view occupies its address range for as long as it
			 * exists, so the platform will not place the new view
			 * until the old one is gone from under it (measured:
			 * [STATUS_CONFLICTING_ADDRESSES] otherwise). The old
			 * mapping's contents are therefore lost even if the
			 * replacement below fails -- the same trade the
			 * anonymous path already makes (see its own comment,
			 * above) for the same reason: mmap.html's MAP_FIXED
			 * clause requires the old mapping discarded, not
			 * preserved on failure. */
			__plat_mem_unmap_view(m->base, m->npages * MMAP_PAGE);
			if (m->writeback) __plat_close(m->writeback);
			base = addr;
			if (__plat_mem_map_file(f->h, prot, flags, off,
			                        npages * MMAP_PAGE, &base) < 0) {
				if (writeback) __plat_close(writeback);
				free(m->live);
				free(m->locked);
				m->base = NULL;
				m->live = NULL;
				m->locked = NULL;
				m->npages = 0;
				m->filebacked = 0;
				m->writeback = __PLAT_HANDLE_NULL;
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
				m->writeback = writeback;
				return base;
			}
			m->base = base;
			m->npages = npages;
			m->filebacked = 1;
			m->writeback = writeback;
			if (lock_future && mlock(base, npages * MMAP_PAGE) < 0) {
				__plat_mem_unmap_view(base, npages * MMAP_PAGE);
				if (m->writeback) __plat_close(m->writeback);
				free(m->live);
				free(m->locked);
				memset(m, 0, sizeof *m);
				errno = EAGAIN;
				return MAP_FAILED;
			}
			return base;
		}

		first = addr_diff(addr, m->base) / MMAP_PAGE;

		if (__plat_mem_commit_fixed(addr, npages * MMAP_PAGE, prot) < 0) {
			/* The old contents are gone either way; the pages are
			 * dead, and saying so keeps the bookkeeping true. */
			for (i = 0; i < npages && first + i < m->npages; i++) m->live[first + i] = 0;
			drop_if_dead(m);
			return MAP_FAILED;
		}
		for (i = 0; i < npages && first + i < m->npages; i++) {
			m->live[first + i] = 1;
			m->locked[first + i] = 0;
		}
		if (lock_future && mlock(addr, npages * MMAP_PAGE) < 0) {
			__plat_mem_decommit(addr, npages * MMAP_PAGE);
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
		if ((flags & MAP_SHARED) && (f->flags & O_ACCMODE) == O_RDWR) {
			if (__plat_dup(f->h, 0, &writeback) < 0)
				return MAP_FAILED;
		}
		base = NULL;
		if (__plat_mem_map_file(f->h, prot, flags, off, npages * MMAP_PAGE, &base) < 0) {
			if (writeback) __plat_close(writeback);
			return MAP_FAILED;
		}
		if (alloc_page_state(m, npages) < 0) {
			__plat_mem_unmap_view(base, npages * MMAP_PAGE);
			if (writeback) __plat_close(writeback);
			errno = ENOMEM;
			return MAP_FAILED;
		}
		m->base = base;
		m->npages = npages;
		m->filebacked = 1;
		m->writeback = writeback;
		if (lock_future && mlock(base, npages * MMAP_PAGE) < 0) {
			__plat_mem_unmap_view(base, npages * MMAP_PAGE);
			if (m->writeback) __plat_close(m->writeback);
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
	if (__plat_mem_reserve(&base, size, prot) < 0) return MAP_FAILED;

	if (alloc_page_state(m, npages) < 0) {
		__plat_mem_release(base, size);
		errno = ENOMEM;
		return MAP_FAILED;
	}
	m->base = base;
	m->npages = npages;
	m->filebacked = 0;
	m->writeback = __PLAT_HANDLE_NULL;
	if (lock_future && mlock(base, npages * MMAP_PAGE) < 0) {
		__plat_mem_release(base, size);
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
		lo = addr_gt(a, m->base) ? a : m->base;
		hi = a + npages * MMAP_PAGE;
		if (addr_gt(hi, m->base + m->npages * MMAP_PAGE)) hi = m->base + m->npages * MMAP_PAGE;
		if (addr_ge(lo, hi)) continue;
		{
			size_t first = addr_diff(lo, m->base) / MMAP_PAGE;
			size_t n = addr_diff(hi, lo) / MMAP_PAGE;
			/* Page-granular decommit: keeps the reservation,
			 * leaves the rest of the mapping exactly where it
			 * was. */
			__plat_mem_decommit(lo, addr_diff(hi, lo));
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
	/* mprotect.html ERRORS: "[EINVAL] The addr argument is not a
	 * multiple of the page size as returned by sysconf()." */
	if (!pgaligned(addr)) { errno = EINVAL; return -1; }
	if (len == 0) return 0;

	return __plat_mem_protect(addr, pground(len), prot);
}

int msync(void *addr, size_t len, int flags)
{
	int k;
	char *a = addr;
	char *end;
	/* The arguments are validated even when the range contains no shared
	 * file view, because a caller who passes a
	 * misaligned address has made an error whether or not there is
	 * anything to flush, and msync.html does give an error for it:
	 * "[EINVAL] The value of addr is not a multiple of the page size as
	 * returned by sysconf()", and "[EINVAL] The value of flags is
	 * invalid". */
	if (!pgaligned(addr)) { errno = EINVAL; return -1; }
	if ((flags & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE)) != 0) { errno = EINVAL; return -1; }
	/* "[EINVAL] The value of flags includes both MS_ASYNC and MS_SYNC." */
	if ((flags & MS_ASYNC) && (flags & MS_SYNC)) { errno = EINVAL; return -1; }
	end = a + pground(len);
	if (flags & MS_INVALIDATE) {
		for (k = 0; k < MMAP_MAX; k++) {
			struct mapping *m = &maps[k];
			char *lo, *hi;
			size_t first, n, i;
			if (!m->base) continue;
			lo = addr_gt(a, m->base) ? a : m->base;
			hi = addr_lt(end, m->base + m->npages * MMAP_PAGE)
			   ? end : m->base + m->npages * MMAP_PAGE;
			if (addr_ge(lo, hi)) continue;
			first = addr_diff(lo, m->base) / MMAP_PAGE;
			n = addr_diff(hi, lo) / MMAP_PAGE;
			for (i = 0; i < n; i++) if (m->locked[first + i]) {
				errno = EBUSY;
				return -1;
			}
		}
	}
	for (k = 0; k < MMAP_MAX; k++) {
		struct mapping *m = &maps[k];
		char *lo, *hi;
		if (!m->base || !m->filebacked || !m->writeback) continue;
		lo = addr_gt(a, m->base) ? a : m->base;
		hi = addr_lt(end, m->base + m->npages * MMAP_PAGE)
		   ? end : m->base + m->npages * MMAP_PAGE;
		if (addr_ge(lo, hi)) continue;
		if (__plat_mem_flush_view(lo, addr_diff(hi, lo), m->writeback) < 0)
			return -1;
	}
	return 0;
}

/* mlock.html: "shall cause those whole pages containing any part of the
 * address space ... to be memory-resident until unlocked".
 *
 * __plat_mem_lock() is a real, genuine lock on every backend this
 * interface is defined against (NT and Wine alike -- see
 * src/internal/nt.h's note on NtLockVirtualMemory), rather than a
 * wrapper that reports success without doing anything.  It is bounded
 * by a resource limit on both -- a working-set quota on NT,
 * RLIMIT_MEMLOCK on the host under Wine -- and that limit is an
 * ENVIRONMENT property, not a platform one: the same binary succeeds on
 * a machine with a generous limit and fails on one without.  That is why
 * test/posix-mman.c keys its skip on the limit it measures rather than
 * on which system it believes it is running. */
static int lock_range(const void *addr, size_t len, int lock)
{
	uintptr_t base;
	size_t z;

	if (len == 0) { errno = EINVAL; return -1; }
	z = pground(len + ((uintptr_t)addr & (MMAP_PAGE - 1)));
	base = (uintptr_t)addr & ~(uintptr_t)(MMAP_PAGE - 1);
	if (lock ? __plat_mem_lock((void *)base, z) : __plat_mem_unlock((void *)base, z))
		return -1;
	{
		char *a = (char *)base;
		char *end = a + z;
		int k;
		for (k = 0; k < MMAP_MAX; k++) {
			struct mapping *m = &maps[k];
			char *lo, *hi;
			size_t first, n, i;
			if (!m->base) continue;
			lo = addr_gt(a, m->base) ? a : m->base;
			hi = addr_lt(end, m->base + m->npages * MMAP_PAGE)
			   ? end : m->base + m->npages * MMAP_PAGE;
			if (addr_ge(lo, hi)) continue;
			first = addr_diff(lo, m->base) / MMAP_PAGE;
			n = addr_diff(hi, lo) / MMAP_PAGE;
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
