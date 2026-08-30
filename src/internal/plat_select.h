/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform interface src/select/select.c and poll.c's POSIX-facing
 * front doors call into instead of raw
 * Nt{QueryInformationFile,WaitForSingleObject,WaitForMultipleObjects,
 * DelayExecution,QuerySystemTime} calls and the raw IOCTL_AFD_SELECT
 * dance (src/internal/afd.h).  See src/select/nt/plat_select.c for the
 * implementation these declare.
 *
 * Every function here takes POSIX-shaped arguments and returns a
 * POSIX-shaped result.  The wait-vs-poll design itself -- which
 * descriptor shapes are probed instantaneously vs. re-probed on a
 * timer, the console-wait-plus-signal-event bundling, the elapsed-time
 * accounting across a wait -- is this library's own strategy for
 * satisfying select()/poll() on top of NT's primitives (see select.c's
 * own banner) and stays in the front door entirely, unchanged; only the
 * raw syscalls each step of that strategy needs are relocated here.
 */
#ifndef _NTLIBC_PLAT_SELECT_H
#define _NTLIBC_PLAT_SELECT_H

#include "plat_handle.h"

/* Query a pipe end's local state for select()/poll()'s per-descriptor
 * probe.  Returns 1 and fills *read_avail/*write_quota when `h` is a
 * healthy, connected pipe whose FILE_PIPE_LOCAL_INFORMATION was
 * successfully read; 0 (out-params untouched) when the query failed or
 * the pipe is not in the connected state -- __fd_probe() (select.c)
 * treats a 0 here as "ready and hung up", the platform's own fact
 * rather than a decision this interface makes. */
int __plat_pipe_probe(__plat_handle_t h, unsigned long *read_avail, unsigned long *write_quota);

/* Behavioral, uncached probe: does this platform's pipe implementation
 * actually populate FILE_PIPE_LOCAL_INFORMATION's WriteQuotaAvailable?
 * See select.c's wqa_works() for the full reasoning (a wine-9.0-vs-
 * wine-10.0 history) and why this cannot be assumed either way. 1
 * trustworthy, 0 not (including "could not even create a private probe
 * pipe"). Caching, if wanted, is the caller's job -- this always
 * re-probes. */
int __plat_pipe_wqa_trustworthy(void);

/* Zero-timeout peek at one waitable handle (a console input handle,
 * here): 1 if it was already signalled, 0 if not. */
int __plat_wait_ready(__plat_handle_t h);

/* The IOCTL_AFD_SELECT probe for a socket -- see select.c's __fd_probe()
 * __FD_SOCKET case (moved here verbatim) for the reasoning behind every
 * field of afd.h this touches.  Always sets *canread/*canwrite/*hup;
 * there is no failure this reports outward -- a probe that could not be
 * taken at all reports ready-and-hung-up, the same over-eager stance
 * the pipe/console cases take for their own unanswerable queries. */
void __plat_socket_probe(__plat_handle_t h, int *canread, int *canwrite, int *hup);

/* Wait for any of nhandles waitable handles to become signalled, or
 * wait_ticks 100ns ticks to pass (ignored when infinite), whichever
 * comes first.  Called only when nhandles > 0; no return value, since
 * neither select() nor poll() distinguishes "woke because something
 * signalled" from "woke because the budget ran out" -- the next poll
 * pass re-probes everything either way. */
void __plat_wait_multiple(const __plat_handle_t *handles, int nhandles, long long wait_ticks, int infinite);

/* Sleep for wait_ticks 100ns ticks, or indefinitely when infinite --
 * used when there is nothing to wait on at all. */
void __plat_delay(long long wait_ticks, int infinite);

/* Current time, in the same 100ns-tick unit as everything else in this
 * interface -- select_core()'s elapsed-time accounting across a wait
 * (select.c).  The same clock CLOCK_REALTIME reads
 * (src/time/clock_gettime.c). */
long long __plat_now_100ns(void);

#endif
