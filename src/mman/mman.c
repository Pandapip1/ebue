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

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "libc.h"
#include "plat_mem.h"
#include "plat_fd.h"

#define MMAP_PAGE 4096u

/* One live mapping. `live` is a one-bit-per-page bitmap, allocated only
 * after the first partial change; NULL means every page is live. `locked`
 * uses two bits per page (0 unlocked, 1 locked, 2 newly locked by the
 * current mlockall transaction) and is likewise allocated only when a
 * lock must be recorded. Lazy allocation matters for vmfill-style huge
 * reservations: bookkeeping should not consume memory in proportion to
 * a mapping which has never been split or locked. */
struct mapping {
	char *base;
	size_t npages;
	size_t live_pages;
	size_t next_free;
	unsigned char *live;
	unsigned char *locked;
	int filebacked;         /* section view, not a private anonymous
	                          * reservation -- see plat_mem.h */
	__plat_handle_t writeback; /* independent writable MAP_SHARED file handle */
	/* The fildes/off mmap() itself was called with, kept only for
	 * posix_mem_offset() to hand back (posix_mem_offset.html: "the
	 * descriptor used (via mmap()) to establish the mapping").
	 * Meaningless when !filebacked -- posix_mem_offset() never reads
	 * these for an anonymous mapping, it answers EACCES first (see
	 * this file's posix_mem_offset()). */
	int mm_fd;
	off_t mm_off;
};

/* POSIX permits an implementation-defined mapping limit and EMFILE at
 * that limit, but an arbitrary 256-entry ceiling made otherwise valid
 * address-space exhaustion probes stop early. The registry now grows as
 * needed; allocation failure is its only limit. */
static struct mapping *maps;
static size_t maps_len;
static size_t maps_cap;
static size_t maps_free = (size_t)-1;
static size_t maps_recent = (size_t)-1;
static int lock_future;

#ifdef __clang_analyzer__
#define returns_element_of(registry) \
	__attribute__((annotate("ntlibc_relation_returns_element_of:" #registry)))
#define parameter_element_of(index, registry) \
	__attribute__((annotate("ntlibc_relation_parameter_element_of:" #index ":" #registry)))
#else
#define returns_element_of(registry)
#define parameter_element_of(index, registry)
#endif

/* Mapping pointers below are either checked lookup results or entries in
 * `maps`. find_slot() is the only operation which may grow and relocate
 * that array, and callers hold no mapping pointer across a find_slot(). */

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

static int page_live(const struct mapping *m, size_t page)
{
	return !m->live || (m->live[page >> 3] & (1u << (page & 7))) != 0;
}

static int ensure_live_bitmap(struct mapping *m)
{
	size_t bytes;
	if (m->live) return 0;
	if (m->npages > (size_t)-1 - 7) { errno = ENOMEM; return -1; }
	bytes = (m->npages + 7) >> 3;
	m->live = malloc(bytes);
	if (!m->live) return -1;
	memset(m->live, 0xff, bytes);
	return 0;
}

static void set_page_live(struct mapping *m, size_t page, int live) // NOLINT(bugprone-easily-swappable-parameters) -- page selects a bitmap slot while live is its boolean state
{
	unsigned char mask = (unsigned char)(1u << (page & 7));
	unsigned char *byte = &m->live[page >> 3];
	int old = (*byte & mask) != 0;
	if (old == !!live) return;
	if (live) {
		*byte |= mask;
		m->live_pages++;
	} else {
		*byte &= (unsigned char)~mask;
		m->live_pages--;
	}
}

static unsigned page_lock_state(const struct mapping *m, size_t page)
{
	unsigned char byte;
	if (!m->locked) return 0;
	byte = m->locked[page >> 2];
	switch (page & 3) {
	case 0: return byte & 3u;
	case 1: return (byte >> 2) & 3u;
	case 2: return (byte >> 4) & 3u;
	default: return (byte >> 6) & 3u;
	}
}

static int ensure_lock_bitmap(struct mapping *m)
{
	size_t bytes;
	if (m->locked) return 0;
	if (m->npages > (size_t)-1 - 3) { errno = ENOMEM; return -1; }
	bytes = (m->npages + 3) >> 2;
	m->locked = calloc(bytes, 1);
	return m->locked ? 0 : -1;
}

static void set_page_lock_state(struct mapping *m, size_t page, unsigned state) // NOLINT(bugprone-easily-swappable-parameters) -- page selects a bitmap slot while state supplies its two-bit value
{
	unsigned char *byte = &m->locked[page >> 2];
	state &= 3u;
	switch (page & 3) {
	case 0:
		*byte = (unsigned char)((*byte & ~3u) | state);
		break;
	case 1:
		*byte = (unsigned char)((*byte & ~(3u << 2)) | (state << 2));
		break;
	case 2:
		*byte = (unsigned char)((*byte & ~(3u << 4)) | (state << 4));
		break;
	default:
		*byte = (unsigned char)((*byte & ~(3u << 6)) | (state << 6));
		break;
	}
}

/* Wine reports a whole page beyond a mapped file's end as an access
 * violation on an uncommitted address, rather than NT's more descriptive
 * EXCEPTION_IN_PAGE_ERROR.  The signal bridge asks this after such a fault
 * so it can preserve POSIX's SIGBUS distinction.  A page decommitted by
 * munmap() is deliberately excluded: it is no longer part of the mapping
 * and remains SIGSEGV/SEGV_MAPERR. */
int __mman_fault_is_object_error(const void *p)
{
	size_t i;
	for (i = 0; i < maps_len; i++) {
		struct mapping *m = &maps[i];
		size_t page;
		if (!m->base || !m->filebacked || addr_lt(p, m->base) ||
		    addr_ge(p, m->base + m->npages * MMAP_PAGE)) continue;
		page = addr_diff(p, m->base) / MMAP_PAGE;
		return page_live(m, page);
	}
	return 0;
}

int __mman_address_is_live(const void *p)
{
	size_t i;
	for (i = 0; i < maps_len; i++) {
		struct mapping *m = &maps[i];
		size_t page;
		if (!m->base || addr_lt(p, m->base) ||
		    addr_ge(p, m->base + m->npages * MMAP_PAGE)) continue;
		page = addr_diff(p, m->base) / MMAP_PAGE;
		return page_live(m, page);
	}
	return 0;
}

int __mman_range_is_live(const void *p, size_t len)
{
	size_t i;
	const char *a = p;
	if (!len || (uintptr_t)a > (uintptr_t)-1 - len) return 0;
	for (i = 0; i < maps_len; i++) {
		struct mapping *m = &maps[i];
		size_t first, last, page;
		if (!m->base || addr_lt(a, m->base) ||
		    addr_gt(a + len, m->base + m->npages * MMAP_PAGE)) continue;
		first = addr_diff(a, m->base) / MMAP_PAGE;
		last = (addr_diff(a, m->base) + len - 1) / MMAP_PAGE;
		for (page = first; page <= last; page++)
			if (!page_live(m, page)) return 0;
		return 1;
	}
	return 0;
}

static struct mapping *find_containing(const void *p, size_t len)
	returns_element_of(maps);
static struct mapping *find_containing(const void *p, size_t len)
{
	size_t i;
	const char *a = p;
	for (i = 0; i < maps_len; i++) {
		struct mapping *m = &maps[i];
		if (!m->base) continue;
		if (addr_ge(a, m->base) && addr_le(a + len, m->base + m->npages * MMAP_PAGE)) return m;
	}
	return NULL;
}

static struct mapping *find_slot(void) returns_element_of(maps);
static struct mapping *find_slot(void)
{
	size_t i, cap;
	struct mapping *grown;
	if (maps_free != (size_t)-1) {
		i = maps_free;
		maps_free = maps[i].next_free;
		memset(&maps[i], 0, sizeof maps[i]);
		return &maps[i];
	}
	if (maps_len == maps_cap) {
		cap = maps_cap ? maps_cap * 2 : 16;
		if (cap < maps_cap || cap > (size_t)-1 / sizeof *maps) {
			errno = ENOMEM;
			return NULL;
		}
		grown = realloc(maps, cap * sizeof *maps);
		if (!grown) return NULL;
		for (i = maps_cap; i < cap; i++)
			grown[i] = (struct mapping){0};
		maps = grown;
		maps_cap = cap;
	}
	return &maps[maps_len++];
}

static void release_slot(struct mapping *m) parameter_element_of(0, maps);
static void release_slot(struct mapping *m)
{
	size_t i = (size_t)(m - maps);
	if (maps_recent == i) maps_recent = (size_t)-1;
	memset(m, 0, sizeof *m);
	m->next_free = maps_free;
	maps_free = i;
}

static void mark_recent(struct mapping *m) parameter_element_of(0, maps);
static void mark_recent(struct mapping *m)
{
	maps_recent = (size_t)(m - maps);
}

static void init_page_state(struct mapping *m, size_t npages)
{
	m->npages = npages;
	m->live_pages = npages;
	m->live = NULL;
	m->locked = NULL;
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
 * `&maps[k]`/`&maps[i]`, the address of a registry entry -- never NULL
 * either way. */
static void drop_if_dead(struct mapping *m)
	__attribute__((nonnull(1))) parameter_element_of(0, maps);
static void drop_if_dead(struct mapping *m)
{
	if (m->live_pages) return;
	if (m->filebacked) __plat_mem_unmap_view(m->base, m->npages * MMAP_PAGE);
	else __plat_mem_release(m->base, m->npages * MMAP_PAGE);
	if (m->writeback) __plat_close(m->writeback);
	free(m->live);
	free(m->locked);
	release_slot(m);
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
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
	if (len > (size_t)-1 - (MMAP_PAGE - 1)) {
		errno = ENOMEM;
		return MAP_FAILED;
	}

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
				release_slot(m);
				return MAP_FAILED;
			}
			free(m->live);
			free(m->locked);
			init_page_state(m, npages);
			m->base = base;
			m->filebacked = 1;
			m->writeback = writeback;
			m->mm_fd = fd;
			m->mm_off = off;
			if (lock_future && mlock(base, npages * MMAP_PAGE) < 0) {
				__plat_mem_unmap_view(base, npages * MMAP_PAGE);
				if (m->writeback) __plat_close(m->writeback);
				free(m->live);
				free(m->locked);
				release_slot(m);
				errno = EAGAIN;
				return MAP_FAILED;
			}
			mark_recent(m);
			return base;
		}

		first = addr_diff(addr, m->base) / MMAP_PAGE;
		/* Commit failure discards the old pages too, so reserve the
		 * bitmap needed to record either outcome before changing them. */
		if (ensure_live_bitmap(m) < 0) return MAP_FAILED;

		if (__plat_mem_commit_fixed(addr, npages * MMAP_PAGE, prot) < 0) {
			/* The old contents are gone either way; the pages are
			 * dead, and saying so keeps the bookkeeping true. */
			for (i = 0; i < npages && first + i < m->npages; i++)
				set_page_live(m, first + i, 0);
			drop_if_dead(m);
			return MAP_FAILED;
		}
		for (i = 0; i < npages && first + i < m->npages; i++) {
			set_page_live(m, first + i, 1);
			if (m->locked) set_page_lock_state(m, first + i, 0);
		}
		if (lock_future && mlock(addr, npages * MMAP_PAGE) < 0) {
			__plat_mem_decommit(addr, npages * MMAP_PAGE);
			for (i = 0; i < npages && first + i < m->npages; i++)
				set_page_live(m, first + i, 0);
			drop_if_dead(m);
			errno = EAGAIN;
			return MAP_FAILED;
		}
		mark_recent(m);
		return addr;
	}

	m = find_slot();
	if (!m) { errno = ENOMEM; return MAP_FAILED; }

	if (!anon) {
		if ((flags & MAP_SHARED) && (f->flags & O_ACCMODE) == O_RDWR) {
			if (__plat_dup(f->h, 0, &writeback) < 0) {
				release_slot(m);
				return MAP_FAILED;
			}
		}
		base = NULL;
		if (__plat_mem_map_file(f->h, prot, flags, off, npages * MMAP_PAGE, &base) < 0) {
			if (writeback) __plat_close(writeback);
			release_slot(m);
			return MAP_FAILED;
		}
		init_page_state(m, npages);
		m->base = base;
		m->filebacked = 1;
		m->writeback = writeback;
		m->mm_fd = fd;
		m->mm_off = off;
		if (lock_future && mlock(base, npages * MMAP_PAGE) < 0) {
			__plat_mem_unmap_view(base, npages * MMAP_PAGE);
			if (m->writeback) __plat_close(m->writeback);
			free(m->live);
			free(m->locked);
			release_slot(m);
			errno = EAGAIN;
			return MAP_FAILED;
		}
		mark_recent(m);
		return base;
	}

	base = NULL;
	size = npages * MMAP_PAGE;
	if (__plat_mem_reserve(&base, size, prot) < 0) {
		release_slot(m);
		return MAP_FAILED;
	}

	init_page_state(m, npages);
	m->base = base;
	m->filebacked = 0;
	m->writeback = __PLAT_HANDLE_NULL;
	m->mm_fd = -1;
	m->mm_off = 0;
	if (lock_future && mlock(base, npages * MMAP_PAGE) < 0) {
		__plat_mem_release(base, size);
		free(m->live);
		free(m->locked);
		release_slot(m);
		errno = EAGAIN;
		return MAP_FAILED;
	}
	mark_recent(m);
	return base;
}

int munmap(void *addr, size_t len)
{
	size_t npages, i;
	size_t k;
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

	/* The overwhelmingly common operation, including vmfill's address-
	 * space probes, is to unmap the mapping just returned by mmap(). Avoid
	 * walking an arbitrarily large registry for that exact whole mapping. */
	if (maps_recent != (size_t)-1) {
		struct mapping *m = &maps[maps_recent];
		if (m->base == a && m->npages == npages) {
			__plat_mem_decommit(a, npages * MMAP_PAGE);
			m->live_pages = 0;
			drop_if_dead(m);
			return 0;
		}
	}

	/* "The munmap() function shall remove any mappings for those entire
	 * pages containing any part of the address space of the process
	 * starting at addr and continuing for len bytes ... If there are no
	 * mappings in the specified address range, then munmap() has no
	 * effect."  So an unmapped-but-valid range is a SUCCESS, not an
	 * error, and a range spanning several mappings unmaps all of them.
	 * Both fall out of walking the table rather than requiring one
	 * mapping to contain the range. */
	/* Allocate any bitmap needed for a partial change before changing
	 * platform mappings, so bookkeeping allocation failure is atomic. */
	for (k = 0; k < maps_len; k++) {
		struct mapping *m = &maps[k];
		char *lo, *hi;
		if (!m->base) continue;
		lo = addr_gt(a, m->base) ? a : m->base;
		hi = a + npages * MMAP_PAGE;
		if (addr_gt(hi, m->base + m->npages * MMAP_PAGE))
			hi = m->base + m->npages * MMAP_PAGE;
		if (addr_ge(lo, hi)) continue;
		if (lo != m->base || hi != m->base + m->npages * MMAP_PAGE)
			if (ensure_live_bitmap(m) < 0) return -1;
	}

	for (k = 0; k < maps_len; k++) {
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
			if (first == 0 && n == m->npages) {
				m->live_pages = 0;
			} else {
				for (i = 0; i < n; i++) {
					set_page_live(m, first + i, 0);
					if (m->locked)
						set_page_lock_state(m, first + i, 0);
				}
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

int msync(void *addr, size_t len, int flags) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	size_t k;
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
		for (k = 0; k < maps_len; k++) {
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
			for (i = 0; i < n; i++) if (page_lock_state(m, first + i)) {
				errno = EBUSY;
				return -1;
			}
		}
	}
	for (k = 0; k < maps_len; k++) {
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
static int lock_range(const void *addr, size_t len, int lock) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	char *base;
	size_t z;

	if (len == 0) { errno = EINVAL; return -1; }
	z = pground(len + ((uintptr_t)addr & (MMAP_PAGE - 1)));
	base = (char *)((uintptr_t)addr & ~(uintptr_t)(MMAP_PAGE - 1));
	if (lock) {
		char *end = base + z;
		size_t k;
		/* Make recording a successful platform lock infallible. */
		for (k = 0; k < maps_len; k++) {
			struct mapping *m = &maps[k];
			if (!m->base || addr_ge(base, m->base + m->npages * MMAP_PAGE) ||
			    addr_ge(m->base, end)) continue;
			if (ensure_lock_bitmap(m) < 0) return -1;
		}
	}
	if (lock ? __plat_mem_lock(base, z) : __plat_mem_unlock(base, z))
		return -1;
	{
		char *a = base;
		char *end = a + z;
		size_t k;
		for (k = 0; k < maps_len; k++) {
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
			for (i = 0; i < n; i++) if (page_live(m, first + i)) {
				if (lock) set_page_lock_state(m, first + i, 1);
				else if (m->locked) set_page_lock_state(m, first + i, 0);
			}
		}
	}
	return 0;
}

int mlock(const void *addr, size_t len)   { return lock_range(addr, len, 1); }
int munlock(const void *addr, size_t len) { return lock_range(addr, len, 0); }

int mlockall(int flags)
{
	size_t k;

	if (!flags || (flags & ~(MCL_CURRENT | MCL_FUTURE))) {
		errno = EINVAL;
		return -1;
	}
	if (flags & MCL_CURRENT) {
		for (k = 0; k < maps_len; k++) {
			struct mapping *m = &maps[k];
			size_t first, n;
			if (!m->base) continue;
			for (first = 0; first < m->npages; first += n) {
				while (first < m->npages &&
				       (!page_live(m, first) || page_lock_state(m, first))) first++;
				if (first == m->npages) break;
				for (n = 1; first + n < m->npages &&
				     page_live(m, first + n) && !page_lock_state(m, first + n); n++);
				if (mlock(m->base + first * MMAP_PAGE, n * MMAP_PAGE) < 0) {
					int saved = errno;
					size_t j;
					/* A failed mlockall() must not leave the
					 * successfully visited prefix locked. State 2
					 * distinguishes locks acquired by this call from
					 * locks that predated it. */
					for (j = 0; j < maps_len; j++) {
						struct mapping *r = &maps[j];
						size_t a, z;
						if (!r->base) continue;
						for (a = 0; a < r->npages; a += z) {
							while (a < r->npages && page_lock_state(r, a) != 2) a++;
							if (a == r->npages) break;
							for (z = 1; a + z < r->npages && page_lock_state(r, a + z) == 2; z++);
							munlock(r->base + a * MMAP_PAGE, z * MMAP_PAGE);
						}
					}
					errno = saved;
					return -1;
				}
				for (n = 0; first + n < m->npages &&
				     page_lock_state(m, first + n) == 1; n++)
					set_page_lock_state(m, first + n, 2);
			}
		}
		for (k = 0; k < maps_len; k++) {
			struct mapping *m = &maps[k];
			size_t i;
			if (!m->base) continue;
			for (i = 0; i < m->npages; i++)
				if (page_lock_state(m, i) == 2) set_page_lock_state(m, i, 1);
		}
	}
	if (flags & MCL_FUTURE) lock_future = 1;
	return 0;
}

int munlockall(void)
{
	size_t k;
	int failed = 0;
	int saved = 0;

	lock_future = 0;
	for (k = 0; k < maps_len; k++) {
		struct mapping *m = &maps[k];
		size_t first, n;
		if (!m->base) continue;
		for (first = 0; first < m->npages; first += n) {
			while (first < m->npages && !page_lock_state(m, first)) first++;
			if (first == m->npages) break;
			for (n = 1; first + n < m->npages && page_lock_state(m, first + n); n++);
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

/* posix_madvise(): https://pubs.opengroup.org/onlinepubs/9699919799/
 * functions/posix_madvise.html.  DESCRIPTION: "shall advise the
 * implementation on the expected behavior...with respect to the data in
 * the memory starting at address addr, and continuing for len bytes,"
 * and "shall have no effect on the semantics of access to memory in the
 * specified range, although it may affect the performance of access."
 * This implementation has no page-replacement heuristic for any advice
 * value to steer, so every valid one is genuinely a no-op -- see
 * <sys/mman.h>'s banner for why that is not the same thing as a stub.
 * ERRORS: "[EINVAL] The value of advice is invalid" and "[ENOMEM]
 * Addresses in the range starting at addr and continuing for len bytes
 * are partly or completely outside the range allowed for the address
 * space of the calling process" -- the latter checked against the same
 * mapping registry mmap()/munmap()/mlock() already maintain, via the
 * same find_containing() munmap()'s MAP_FIXED path uses. */
int posix_madvise(void *addr, size_t len, int advice)
{
	if (advice != POSIX_MADV_NORMAL && advice != POSIX_MADV_SEQUENTIAL &&
	    advice != POSIX_MADV_RANDOM && advice != POSIX_MADV_WILLNEED &&
	    advice != POSIX_MADV_DONTNEED)
		return EINVAL;

	if (!find_containing(addr, len))
		return ENOMEM;

	return 0;
}

/* posix_typed_mem_open(): https://pubs.opengroup.org/onlinepubs/
 * 9699919799/functions/posix_typed_mem_open.html.  "shall establish a
 * connection between the typed memory object specified by...name and a
 * file descriptor."  Which typed memory pools exist is entirely
 * implementation-defined (RATIONALE), and this implementation ships
 * none -- there is no NT concept this could honestly wire to (a "typed
 * memory object" names a distinct, bounded pool of physical memory with
 * its own characteristics, e.g. a DMA-capable region; ntlibc has one
 * general-purpose virtual address space and no such pools to name) --
 * so ERRORS' "[ENOENT] The named typed memory object does not exist" is
 * this system's real, permanent answer for every name, not a stand-in
 * for one that will exist later.  See <sys/mman.h>'s banner. */
int posix_typed_mem_open(const char *name, int oflag, int tflag)
{
	(void)name;
	(void)oflag;

	if (tflag != POSIX_TYPED_MEM_ALLOCATE &&
	    tflag != POSIX_TYPED_MEM_ALLOCATE_CONTIG &&
	    tflag != POSIX_TYPED_MEM_MAP_ALLOCATABLE) {
		errno = EINVAL;
		return -1;
	}

	errno = ENOENT;
	return -1;
}

/* posix_typed_mem_get_info(): https://pubs.opengroup.org/onlinepubs/
 * 9699919799/functions/posix_typed_mem_get_info.html.  Since
 * posix_typed_mem_open() above never succeeds, no fildes value this
 * process could hold was ever established as a typed memory descriptor
 * -- ERRORS' "[EBADF] The fildes argument is not a valid open file
 * descriptor" (for typed-memory purposes; no other kind honestly
 * reaches this function) is therefore this implementation's only real
 * outcome for any input. */
int posix_typed_mem_get_info(int fildes, struct posix_typed_mem_info *info)
{
	(void)fildes;
	(void)info;
	errno = EBADF;
	return -1;
}

/* posix_mem_offset(): https://pubs.opengroup.org/onlinepubs/9699919799/
 * functions/posix_mem_offset.html.  "shall return in the variable
 * pointed to by off a value that identifies the offset...of the memory
 * block currently mapped at addr[, and] in the variable pointed to by
 * fildes, the descriptor used (via mmap()) to establish the mapping."
 * Answered from the same mapping registry mmap() already keeps (struct
 * mapping's mm_fd/mm_off, set at every file-backed mmap() call site).
 * ERRORS: "[EACCES] The region...was not established via a memory
 * object" -- an anonymous mapping, mm_fd/mm_off meaningless for it, see
 * struct mapping's own comment -- and "[ENOMEM] The addresses in the
 * range...are outside the range allowed for the address space," which
 * find_containing() returning NULL covers whether addr is unmapped
 * entirely or the range crosses into a different mapping. */
int posix_mem_offset(const void *__restrict addr, size_t len,
                      off_t *__restrict off, size_t *__restrict contig_len,
                      int *__restrict fildes)
{
	struct mapping *m = find_containing(addr, len);
	size_t remaining;

	if (!m) { errno = ENOMEM; return -1; }
	if (!m->filebacked) { errno = EACCES; return -1; }

	*off = m->mm_off + (off_t)addr_diff(addr, m->base);
	*fildes = m->mm_fd;
	remaining = m->npages * MMAP_PAGE - addr_diff(addr, m->base);
	*contig_len = remaining < len ? remaining : len;
	return 0;
}

void __mman_reset_after_fork(void)
{
	size_t k;
	lock_future = 0;
	for (k = 0; k < maps_len; k++) {
		struct mapping *m = &maps[k];
		if (m->base) {
			free(m->locked);
			m->locked = NULL;
		}
	}
}

// NOLINTEND(misc-include-cleaner)
