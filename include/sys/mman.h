/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/mman.h> -- memory management:
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/sys_mman.h.html
 * and the mmap()/munmap()/mprotect()/msync()/mlock() pages linked from
 * there.  Implemented in src/mman/mman.c.  Pass 1: ANONYMOUS MAPPINGS
 * ONLY.
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
 * What Pass 1 does and does not do
 * --------------------------------
 *
 * Anonymous mappings, fully and correctly, over
 * NtAllocateVirtualMemory()/NtFreeVirtualMemory().  Three distinct,
 * correct outcomes rather than a function that never works:
 *
 *   MAP_ANONYMOUS          -> a real mapping
 *   fd = -1, no MAP_ANONYMOUS -> [EBADF]   (the Issue 7 shall-fail)
 *   a valid descriptor     -> [ENODEV]     (declined, see below)
 *
 * File-backed mmap() is refused at the door with [ENODEV], "The fildes
 * argument refers to a file whose type is not supported by mmap()",
 * USED AT ITS LITERAL READING: Pass 1 supports *no* file type.  That is
 * defensible but unusual -- a regular file is the canonical mmap-able
 * type -- so it is said plainly here rather than left to look like an
 * oversight someone should tidy up.  The reason it is refused at the
 * door rather than half-supported is munmap(): munmap.html's ERRORS are
 * exactly three (addr not page-aligned, range outside the address
 * space, len of zero), so THERE IS NO ERRNO FOR A PARTIAL munmap.  A
 * legal partial unmap returning [EINVAL] would be a spec violation
 * dressed as a documented limitation.  Refusing at the door is
 * conforming; failing in the middle is not.
 *
 * Only six of the header's fourteen interfaces are declared.  mlockall,
 * munlockall, posix_madvise, posix_mem_offset, posix_typed_mem_get_info,
 * posix_typed_mem_open, shm_open and shm_unlink are deliberately absent,
 * on the same ground as <sched.h>'s omissions: declaring one so it could
 * return an error is worse than not declaring it, because a probe that
 * finds the symbol concludes the facility is present.
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

#ifdef __cplusplus
}
#endif
#endif
