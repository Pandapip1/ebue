/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_size_t
#define __NEED_time_t
#define __NEED_suseconds_t
#define __NEED_struct_timeval
#define __NEED_struct_timespec
#define __NEED_sigset_t

#include <bits/alltypes.h>

#define FD_SETSIZE 1024

typedef unsigned long fd_mask;

typedef struct {
	unsigned long fds_bits[FD_SETSIZE / 8 / sizeof(long)];
} fd_set;

#define FD_ZERO(s) do { int __i; unsigned long *__b=(s)->fds_bits; for(__i=sizeof (fd_set)/sizeof (long); __i; __i--) *__b++=0; } while(0)
#define FD_SET(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] |= (1UL<<((d)%(8*sizeof(long)))))
#define FD_CLR(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] &= ~(1UL<<((d)%(8*sizeof(long)))))
#define FD_ISSET(d, s) !!((s)->fds_bits[(d)/(8*sizeof(long))] & (1UL<<((d)%(8*sizeof(long)))))

int select (int, fd_set *__restrict, fd_set *__restrict, fd_set *__restrict, struct timeval *__restrict);  /* undefined-ok:
	sized up, not ruled out -- a real select() over this library's three
	handle shapes is plausible on pure ntdll
	(NtWaitForMultipleObjects is already declared in src/internal/nt.h),
	but each shape needs its own readiness test, and two of the three
	don't have a "just wait on the handle" answer:
	  - regular files/directories (__FD_FILE/__FD_DIR): always ready,
	    trivially, like Linux;
	  - console input (__FD_CONSOLE): the input handle genuinely is a
	    waitable NT object that becomes signalled when an input record
	    is queued, so NtWaitForMultipleObjects works directly -- no
	    kernel32 needed;
	  - pipes (__FD_PIPE, named or anonymous): the read/write handle
	    itself is *not* signalled on data arrival in NT (unlike a
	    console), so readiness needs polling via
	    NtQueryInformationFile(FilePipeLocalInformation)'s
	    ReadDataAvailable/WriteQuotaAvailable (already declared:
	    FILE_PIPE_LOCAL_INFORMATION in nt.h) on a timeslice loop merged
	    with the console wait -- and exceptfds has no honest answer for
	    any of the three, so it would report nothing, same as Linux does
	    for these shapes in practice.
	None of this changes by reaching for kernel32 -- WaitForMultipleObjects
	and PeekConsoleInput/PeekNamedPipe are themselves thin wrappers over
	the same ntdll calls and the same lack of a pipe-readable signal, so
	kernel32 buys nothing here.  What is missing is not a primitive but
	the polling-loop plumbing and, especially, test coverage: a correct
	implementation needs to be exercised with a live child process
	writing to a pipe on a timer while the parent blocks in select(),
	which only runs under Wine/real Windows (test/process-win.c's
	pattern), not as a quick standalone program. Estimated at 1-2 days:
	roughly half a day for the polling core, half a day for console
	input, and the rest for the multi-process pipe tests and the
	inevitable Wine-vs-real-Windows timing surprises. Left undefined
	rather than shipped half-tested. */

#ifdef __cplusplus
}
#endif
#endif
