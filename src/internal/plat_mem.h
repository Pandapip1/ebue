/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform-memory interface src/mman/mman.c's POSIX-facing front
 * door calls into instead of a raw Nt*VirtualMemory/Nt*Section call.
 * See src/mman/nt/plat_mem.c for the implementation these declare and
 * the tools/lint.sh `PLATFORM` build axis (Makefile's PLAT_GLOBS) that
 * selects it.
 *
 * Every function here takes POSIX-shaped arguments (the PROT_ and MAP_
 * constants from <sys/mman.h>, plain size_t/off_t) and returns a
 * POSIX-shaped result
 * -- 0/-1 with errno already set on failure, never a raw NTSTATUS for
 * the front door to interpret, since a future backend has no NTSTATUS
 * to hand back.  mman.c's own reservation-tracking bookkeeping (the
 * `maps[]`/`live`/`locked` table) is NOT part of this interface: that
 * table is this library's own POSIX-partial-munmap strategy, shared
 * verbatim by whichever backend is compiled in, not something each
 * backend reimplements.
 */
#ifndef _NTLIBC_PLAT_MEM_H
#define _NTLIBC_PLAT_MEM_H

#include <stddef.h>
#include <sys/types.h>
#include "plat_handle.h"

/* Reserve and commit `len` bytes of fresh anonymous memory.
 * *base_inout is a hint on entry (NULL for "anywhere"); the actual base
 * on success.  Corresponds to NT's MEM_RESERVE|MEM_COMMIT in one call
 * -- mmap()'s anonymous path never reserves without also committing. */
int __plat_mem_reserve(void **base_inout, size_t len, int prot);

/* MAP_FIXED against memory this process already reserved: discard
 * [base, base+len) and commit it again with `prot`, so old contents
 * are actually discarded rather than left in place by a bare commit
 * over already-committed pages.  `base`/`len` are already page-aligned
 * by the caller. */
int __plat_mem_commit_fixed(void *base, size_t len, int prot);

/* Decommit [base, base+len) without releasing the underlying
 * reservation -- munmap()'s page-granular partial unmap. */
int __plat_mem_decommit(void *base, size_t len);

/* Release a reservation `base` owns in its entirety (MEM_RELEASE);
 * called only once no page of it is still live.  `len` is the
 * reservation's own size in bytes (the caller always has it -- the
 * mapping's npages*MMAP_PAGE, in mman.c's own bookkeeping): NT's
 * MEM_RELEASE does not need it (a reservation knows its own extent),
 * but a backend whose native release call is page-granular munmap()
 * does, so the interface carries it for every backend's sake rather
 * than only the ones that happen to need it. */
int __plat_mem_release(void *base, size_t len);

/* mprotect(): change [addr, addr+len)'s protection to `prot`. */
int __plat_mem_protect(void *addr, size_t len, int prot);

/* mlock()/munlock(): make [addr, addr+len) resident (or release that
 * residency guarantee).  addr/len are already page-aligned/-rounded by
 * the caller. */
int __plat_mem_lock(void *addr, size_t len);
int __plat_mem_unlock(void *addr, size_t len);

/* Map a view of the object `fh` refers to, honoring `flags`'s
 * MAP_PRIVATE/MAP_SHARED and `prot`, at byte offset `off` for
 * `viewbytes` bytes (already page-rounded by the caller).
 * *base_inout is a hint/fixed base as above.  Every platform-specific
 * quirk in satisfying this -- NT/Wine's unreliable tail zero-fill
 * past a file's real EOF, the two-step section-then-view dance,
 * falling back to a read-only section when the handle cannot support
 * a writable one -- lives entirely inside this call, not the front
 * door.  On failure this sets errno to exactly ENOMEM or ENOTSUP
 * (mmap.html has no broader vocabulary for a failed file-backed
 * mapping), not whatever a generic status-to-errno table would say. */
int __plat_mem_map_file(__plat_handle_t fh, int prot, int flags, off_t off,
                        size_t viewbytes, void **base_inout);

/* Remove a view `__plat_mem_map_file` created.  `len` is the view's own
 * size in bytes, for the same reason __plat_mem_release() above takes
 * one: NT's NtUnmapViewOfSection does not need it, but a backend that
 * removes a mapping via munmap() does. */
int __plat_mem_unmap_view(void *base, size_t len);

/* msync(): flush [addr, addr+len)'s dirty pages back to the object,
 * and make sure the object's mtime reflects it.  `writeback` is the
 * independent writable handle mmap() retained for exactly this call
 * (see mman.c's banner on why one is needed at all) -- NULL/
 * __PLAT_HANDLE_NULL is never passed; the front door only calls this
 * for a mapping that has one.  A backend where flushing already
 * updates the object's timestamp reliably can make the timestamp half
 * of this a no-op. */
int __plat_mem_flush_view(void *addr, size_t len, __plat_handle_t writeback);

#endif
