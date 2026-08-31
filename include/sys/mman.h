/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/mman.h> -- memory management:
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/sys_mman.h.html
 * and the mmap()/munmap()/mprotect()/msync()/mlock() pages linked from
 * there.  Implemented in src/mman/mman.c.  Pass 2: anonymous mappings
 * and file-backed mappings of REGULAR files.  Every other file type
 * (directory, pipe, socket, console, ...) is still declined.
 *
 *
 * Which POSIX edition this header answers to, and why it matters here
 * ------------------------------------------------------------------
 *
 * This tree speaks Issue 7 (`onlinepubs/9699919799`) -- 48 citations
 * across include/ and src/, against 4 for Issue 8 (`9799919799`).  That
 * ratio is the justification for reading Issue 7 as authoritative, and
 * it is worth stating because this header is a case where the two
 * editions genuinely differ and the difference is load-bearing:
 *
 *   ISSUE 7 HAS NO ANONYMOUS MAPPING AT ALL.  MAP_ANONYMOUS and MAP_ANON
 *   are not mentioned anywhere on mmap.html.  DESCRIPTION: mmap() "shall
 *   establish a mapping between the address space of the process ... and
 *   the memory object represented by the file descriptor fildes", and
 *   ERRORS makes "[EBADF] The fildes argument is not a valid open file
 *   descriptor" a SHALL FAIL.  Every memory object the page lists --
 *   regular file, shared memory object, typed memory object -- requires
 *   a descriptor.  Anonymous mapping arrived in Issue 8.
 *
 * So an anonymous-only mmap() implements no Issue 7 clause at all, and
 * `mmap(0, n, prot, MAP_PRIVATE, -1, 0)` -- the shape that looks
 * anonymous -- is a call Issue 7 requires to fail with [EBADF].
 * Measured against glibc rather than derived: that call returns
 * MAP_FAILED/EBADF there, while adding MAP_ANONYMOUS succeeds.
 *
 * MAP_ANONYMOUS is therefore shipped here as a documented NON-POSIX
 * EXTENSION, gated behind _BSD_SOURCE/_GNU_SOURCE, the same way
 * <signal.h> gates sigorset()/sigisemptyset() and the same house style
 * as strlcpy()/mempcpy()/strverscmp()/strchrnul()/explicit_bzero().  The
 * gate is not decoration: an ungated non-POSIX macro would mean a
 * program defining only _POSIX_C_SOURCE sees a symbol POSIX does not
 * define, and a configure probe that finds a symbol draws a conclusion
 * from it.  A visible symbol is a claim -- the same reason <sched.h>
 * declines to declare the _POSIX_PRIORITY_SCHEDULING option group.
 *
 *
 * What Pass 2 does and does not do
 * --------------------------------
 *
 * Anonymous mappings, fully and correctly, over
 * NtAllocateVirtualMemory()/NtFreeVirtualMemory():
 *
 *   MAP_ANONYMOUS          -> a real mapping
 *   fd = -1, no MAP_ANONYMOUS -> [EBADF]   (the Issue 7 shall-fail)
 *
 * File-backed mappings, also for real, over
 * NtCreateSection()/NtMapViewOfSection() (src/mman/mman.c's map_file()):
 *
 *   a __FD_FILE descriptor    -> a real mapping, MAP_SHARED or
 *                                MAP_PRIVATE (copy-on-write, via
 *                                PAGE_WRITECOPY)
 *   any other descriptor type -> [ENODEV], "The fildes argument refers
 *                                to a file whose type is not supported
 *                                by mmap()" -- a directory, pipe,
 *                                socket, or console still is not
 *
 * The one place Pass 1's original refusal still binds is MAP_FIXED
 * against a file-backed mapping.  munmap.html's ERRORS are exactly
 * three (addr not page-aligned, range outside the address space, len of
 * zero) -- THERE IS NO ERRNO FOR A PARTIAL munmap -- and
 * NtUnmapViewOfSection() takes a view's base address and drops the
 * WHOLE view; there is no NT primitive for replacing part of one the
 * way MEM_DECOMMIT+MEM_COMMIT lets the anonymous path replace part of a
 * private reservation.  So MAP_FIXED can only replace a file-backed
 * mapping's ENTIRE current extent; a MAP_FIXED that only overlaps part
 * of one is refused with [ENOMEM] rather than silently misbehaving or
 * inventing an errno munmap.html does not give it.  See
 * src/mman/mman.c's mmap() for where that boundary is enforced, and
 * test/posix-mman.c's history for the fuller argument (this used to be
 * the reason file-backed mmap() was refused altogether; it no longer
 * is, because no case measured against this library needs a partial
 * unmap of a section view).
 *
 * Ten of the header's fourteen interfaces are declared.  posix_madvise,
 * posix_mem_offset, posix_typed_mem_get_info, and posix_typed_mem_open are
 * deliberately absent, on the same ground as
 * <sched.h>'s omissions: declaring one so it could return an error is worse
 * than not declaring it, because a probe that finds the symbol concludes the
 * facility is present.  shm_open() and shm_unlink() use regular NTFS-backed
 * files in a private temporary-directory namespace; the regular-file shape
 * is intentional because mmap() already maps those through NT sections.
 */
#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_size_t
#define __NEED_off_t
#define __NEED_mode_t
#include <bits/alltypes.h>

/* mmap() prot (mmap.html DESCRIPTION, "Protection Options"). */
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

/* mmap() flags (mmap.html DESCRIPTION, "mmap Flags").  Exactly one of
 * MAP_SHARED/MAP_PRIVATE must be given.  Under an anonymous-only Pass 1
 * the two are indistinguishable -- there is no underlying object for a
 * write to be shared with or kept private from -- but both are accepted
 * rather than one refused, because the distinction is about the object,
 * and a caller naming it correctly should not be punished for it. */
#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED   0x10

#define MAP_FAILED ((void *)-1)

/* msync() flags (msync.html DESCRIPTION). */
#define MS_ASYNC      0x1
#define MS_INVALIDATE 0x2
#define MS_SYNC       0x4

/* mlockall() flags (mlockall.html DESCRIPTION). */
#define MCL_CURRENT 0x1
#define MCL_FUTURE  0x2

/* The VALUE is defined unconditionally, under a reserved-namespace name;
 * only the user-visible SPELLING is gated below.
 *
 * That split is not tidiness.  src/mman/mman.c has to test this bit on
 * every call, and it is compiled with different feature-test macros in
 * different builds -- the tcc build passes _ALL_SOURCE (so _GNU_SOURCE,
 * so the gate is open), the native ASan build passes only
 * _XOPEN_SOURCE=700 (so it is shut).  With the value behind the gate the
 * LIBRARY'S OWN BEHAVIOUR changed with the flags it happened to be
 * compiled under: under ASan every anonymous mmap() silently became a
 * file-backed request against descriptor -1 and failed [EBADF].  Caught
 * by the ASan leg, which is the only build in the tree that shuts this
 * gate.
 *
 * A feature-test macro must gate what a TRANSLATION UNIT CAN SEE, never
 * what the implementation does. */
#define __MAP_ANONYMOUS 0x20

#if defined(_BSD_SOURCE) || defined(_GNU_SOURCE)
/* NOT POSIX ISSUE 7 -- see this header's banner.  Anonymous mapping is
 * an Issue 8 / historical-BSD facility; mmap.html (Issue 7) does not
 * mention MAP_ANONYMOUS or MAP_ANON at all, and requires fildes to be a
 * valid open descriptor.  Gated so that a strictly-POSIX program cannot
 * see a symbol Issue 7 does not define.  fildes is ignored when this is
 * set; passing -1 is the convention and is what this implementation
 * expects. */
#define MAP_ANONYMOUS __MAP_ANONYMOUS
#define MAP_ANON      MAP_ANONYMOUS
#endif

void *mmap(void *, size_t, int, int, int, off_t);
int munmap(void *, size_t);
int mprotect(void *, size_t, int);
int msync(void *, size_t, int);
int mlock(const void *, size_t);
int munlock(const void *, size_t);
int mlockall(int);
int munlockall(void);
int shm_open(const char *, int, mode_t);
int shm_unlink(const char *);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
