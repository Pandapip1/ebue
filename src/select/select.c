/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * select()/pselect(): https://pubs.opengroup.org/onlinepubs/9699919799/
 * functions/select.html and .../pselect.html.
 *
 * NT has no single primitive that waits on this library's three open
 * descriptor shapes the way select() wants to:
 *
 *   - __FD_CONSOLE: a console input handle really is a waitable NT
 *     object -- it becomes signalled the moment an input record is
 *     queued -- so NtWaitForSingleObject/NtWaitForMultipleObjects work
 *     on it directly, no polling needed.  Console *output* has no
 *     analogous backpressure in this library (writes to it never
 *     block), so it is treated like a regular file: always ready.
 *   - __FD_PIPE (named or anonymous -- see src/unistd/pipe.c): the
 *     read/write handle itself is not signalled when data arrives or
 *     drains, unlike a console.  The only way to know readability is
 *     to ask, via NtQueryInformationFile(FilePipeLocalInformation)'s
 *     ReadDataAvailable -- so this is polled.  Writability has no
 *     working answer at all: the same call's WriteQuotaAvailable is
 *     documented for exactly this, but reads back 0 always under
 *     Wine (the environment `make check` runs in) whether or not the
 *     pipe actually has room, so it is not used -- see __fd_probe()'s
 *     comment for the fallback this forces.
 *   - __FD_FILE/__FD_DIR/__FD_CHAR: "File descriptors associated with
 *     regular files shall always select true for ready to read, ready
 *     to write" (select.html DESCRIPTION) -- applied here to __FD_CHAR
 *     too (NUL, COM, ...) for the same reason POSIX gives regular
 *     files a free pass: nothing in this library ever blocks a read or
 *     write to one of these shapes past the syscall itself.
 *
 * The wait-vs-poll design: each pass first probes everything that can
 * be checked without blocking (__fd_probe(): an instant
 * NtQueryInformationFile for every pipe, an instant "always ready" for
 * files, plus a zero-timeout NtWaitForSingleObject peek at every
 * pending console-read handle).  If anything is ready, return
 * immediately -- no sleep, however long the timeout.  If nothing is
 * ready and there is still time on the clock, sleep and try again:
 *
 *   - If the only outstanding descriptors are console reads (no pipes
 *     pending), the sleep *is* the wait: NtWaitForMultipleObjects on
 *     the console handles for the full remaining timeout (or
 *     indefinitely for a NULL timeout).  This wakes instantly the
 *     moment input arrives -- zero added latency, zero CPU while
 *     idle.
 *   - If any pipe is pending, there is nothing to wait on for it, so
 *     the sleep is capped at POLL_INTERVAL_TICKS (20ms) before trying
 *     again -- NtDelayExecution for a pipe-only wait, or
 *     NtWaitForMultipleObjects with a 20ms timeout if console reads
 *     are pending too (so a console event still wakes it early).
 *
 * 20ms bounds pipe-readiness latency to at most that (usually much
 * less, since most calls have something ready on the first probe) and
 * caps CPU cost at 50 wasted syscalls/sec in the worst case (nothing
 * ever becomes ready and the caller passed a long or no timeout) --
 * negligible next to a busy-loop, and short enough that no test here
 * needed to wait past it.  A busy-loop (0ms) was rejected as
 * needlessly burning CPU; anything much coarser starts showing up as
 * added latency in interactive pipe use.
 *
 * Timeout semantics (select.html DESCRIPTION): a NULL timeout blocks
 * indefinitely; a non-NULL, zero-valued timeval "effect[s] a poll"
 * (checked here, then returns immediately without ever calling the
 * wait primitive at all).  A negative or out-of-range timeval/timespec
 * is EINVAL.  Per DESCRIPTION's permissive wording ("may update...to
 * reflect the amount of time not slept"), the timeval/timespec object
 * passed in is never written back here -- POSIX permits but does not
 * require it, and not touching it is simpler and matches what most of
 * this library's timeout-taking calls already do (e.g.
 * nanosleep()'s `rem` is only filled with zero, never a real
 * remainder).
 *
 * In-place modification of the descriptor sets (select.html
 * DESCRIPTION: "shall modify the objects pointed to...to indicate
 * which file descriptors are ready"): the sets are never touched
 * incrementally.  A local snapshot of the caller's *rfds, *wfds and *efds
 * is taken once up front (used read-only by every poll pass), fresh
 * local output sets are built from it each pass, and only the final
 * result is copied back into the caller's sets -- including on
 * timeout, when the output sets are all-clear.  This is deliberate:
 * the common bug this project's test suite specifically checks for is
 * leaving the caller's sets *unmodified* (still showing the
 * originally-requested bits) after a timeout instead of clearing them.
 * exceptfds is always cleared to empty: none of the three descriptor
 * shapes above has an honest exceptional-condition signal on this
 * platform (documented on the FD_SETSIZE banner in
 * include/sys/select.h), so it reports nothing, the same as Linux
 * does for these shapes in practice.
 *
 * EINTR (select.html ERRORS): never returned.  This library delivers
 * signals synchronously only (src/signal/signal.c's file banner) --
 * there is no other thread or async delivery mechanism that could
 * interrupt a blocked NtWaitForMultipleObjects/NtDelayExecution call
 * out from under it, and alarm() is a documented no-op stub
 * (src/unistd/sleep.c).  So the situation EINTR describes cannot
 * arise here; this is a real, not merely untested, absence.
 *
 * pselect()'s sigmask (pselect.html DESCRIPTION: "shall be equivalent
 * to atomically saving the current signal mask, installing sigmask...
 * blocking until...ready or a signal is caught, then restoring the
 * signal mask", the point being no race between unblocking a signal
 * and starting the wait): the mask *is* installed before the wait and
 * restored after, both via the same sigprocmask() everyone else uses,
 * so the net effect on the mask is correct.  But the race the spec is
 * actually solving for -- a signal handler firing in the gap between
 * unblocking it and starting to block -- cannot happen on this
 * platform for the reason given for EINTR above: delivery here is
 * synchronous, driven by this thread's own code running, not an
 * asynchronous interrupt of it, so there is no other-thread/signal
 * gap for the "atomically" to be closing in the first place. Honestly:
 * on ntlibc pselect()'s sigmask is exactly sigprocmask(2) bracketing
 * the wait, no more and no less -- correct, but not solving a race
 * that does not exist here rather than solving the one POSIX
 * describes.
 *
 * select() and poll() (src/select/poll.c) share this file's two
 * building blocks, __fd_probe() and __fd_wait_or_delay() (declared in
 * src/internal/libc.h) -- the per-descriptor instantaneous readiness
 * check and the wait/sleep primitive.  They do not share one outer
 * polling loop: select's is shaped around fd_set bit ranges (0..nfds)
 * and poll's around a struct pollfd array, and translating between
 * the two shapes just to share a loop would have added more code than
 * it removed.
 *
 * Sockets are a fourth descriptor shape, added alongside the three
 * above once ntlibc grew sockets (src/socket/): __fd_probe()'s
 * __FD_SOCKET case below issues a single non-blocking IOCTL_AFD_SELECT
 * per probe, the "instantaneous, no wait" shape every other case here
 * already has -- no change to the wait-vs-poll design above was needed,
 * a socket is always resolved in the same probe pass as everything
 * else, never added to console_h/console_fd. See
 * test/networking-audit.md sec 3 for the design writeup this followed.
 */
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include "libc.h"
#include "afd.h"

/* 20ms, in 100ns units (see file banner for why). */
#define POLL_INTERVAL_TICKS 200000LL

/* See src/internal/libc.h for the contract. */
void __fd_probe(struct __fd *f, int *canread, int *canwrite, int *hup)
{
	*hup = 0;
	switch (f->type) {
	case __FD_PIPE: {
		IO_STATUS_BLOCK io;
		FILE_PIPE_LOCAL_INFORMATION pli;
		NTSTATUS st = NtQueryInformationFile(f->h, &io, &pli, sizeof pli, FilePipeLocalInformation);
		if (!NT_SUCCESS(st) || pli.NamedPipeState != FILE_PIPE_CONNECTED_STATE) {
			/* Broken/disconnected: a read() would return 0 (EOF) or an
			 * error, a write() would fail -- neither would block, so
			 * both count as ready, same as a hung-up descriptor does
			 * on Linux. */
			*canread = 1; *canwrite = 1; *hup = 1;
			break;
		}
		*canread = pli.ReadDataAvailable > 0;
		/* WriteQuotaAvailable is documented as exactly the field for
		 * this, but is not usable in practice: verified against Wine
		 * (the environment `make check` runs in) it reads back 0
		 * always, before and after writes, whether or not the pipe
		 * has room -- unimplemented there, not merely untested.  A
		 * pipe write here is also always synchronous (NtWriteFile
		 * blocks internally until room exists rather than returning
		 * a distinct "would block" status), so there is no second
		 * source to cross-check it against either.  Given no honest
		 * signal is available, the write side is treated like a
		 * regular file's: always ready.  This can only ever be
		 * over-eager (reporting ready when a subsequent write would
		 * in fact block on a full pipe), never the reverse, which
		 * matches this project's stance elsewhere on "no honest
		 * answer" cases (see exceptfds, above the FD_SETSIZE banner
		 * in include/sys/select.h). */
		*canwrite = 1;
		break;
	}
	case __FD_CONSOLE:
		/* Read side is resolved by the caller waiting on f->h
		 * directly (see select_core() below and poll.c's own
		 * loop) -- a console input handle really is an NT wait
		 * object.  Output is
		 * always ready, like a regular file: nothing in this
		 * library ever blocks a write to it. */
		*canread = 0;
		*canwrite = 1;
		break;
	case __FD_SOCKET: {
		/* test/networking-audit.md sec 3: a single non-blocking
		 * IOCTL_AFD_SELECT (== Wine's IOCTL_AFD_POLL, same wire
		 * request -- src/internal/afd.h) against just this one
		 * socket, Timeout=0 so it never waits.  AFD_POLL_READ_BITS/
		 * WRITE_BITS (afd.h) are the same fd_set-bit mapping
		 * ReactOS's WSPSelect uses. A close/abort/disconnect event
		 * counts as both readable and writable and as hup, the same
		 * way a broken pipe does above -- a read or write on it
		 * would return immediately rather than block. */
		/* `pi` is storage only -- correctly aligned and large
		 * enough.  Every field is written and read through
		 * src/internal/afd.h's AFD_POLL_REQ_OFF_* and AFD_POLL_H_OFF_*
		 * offsets, because ReactOS's ULONG_PTR Exclusive puts
		 * Handles at +24 on x86_64, where the AFD driver's own
		 * source, phnt, wepoll and libuv all put it at +16; see
		 * that header's poll banner, which also records that
		 * nothing reaches this case yet. */
		AFD_POLL_INFO pi;
		unsigned long len = __afd_poll_request_size(1);
		uint32_t events;
		NTSTATUS st;

		/* Timeout 0: never wait, just sample. */
		__afd_build_poll_request(&pi, 0, 1);
		__afd_poll_set_handle(&pi, 0, f->h, AFD_POLL_READ_BITS | AFD_POLL_WRITE_BITS);

		/* __afd_poll_request_size(1), not sizeof(pi), which rounds
		 * the tail up for Timeout's alignment. */
		st = __afd_ioctl(f->h, IOCTL_AFD_SELECT, &pi, (ULONG)len, &pi, (ULONG)len, 0);
		if (!NT_SUCCESS(st)) { *canread = 0; *canwrite = 0; break; }

		events = __afd_poll_get_events(&pi, 0);
		*canread = (events & AFD_POLL_READ_BITS) != 0;
		*canwrite = (events & AFD_POLL_WRITE_BITS) != 0;
		if (events & (AFD_EVENT_CLOSE | AFD_EVENT_ABORT | AFD_EVENT_DISCONNECT)) {
			*canread = 1; *canwrite = 1; *hup = 1;
		}
		break;
	}
	case __FD_FILE:
	case __FD_DIR:
	case __FD_CHAR:
	default:
		/* select.html DESCRIPTION: "File descriptors associated
		 * with regular files shall always select true for ready to
		 * read, ready to write". Applied here to __FD_CHAR too. */
		*canread = 1;
		*canwrite = 1;
		break;
	}
}

/* See src/internal/libc.h for the contract. */
void __fd_wait_or_delay(HANDLE *console_handles, int ncons, long long wait_ticks, int infinite)
{
	LARGE_INTEGER t;

	if (ncons > 0) {
		if (infinite) {
			NtWaitForMultipleObjects((ULONG)ncons, console_handles, 1 /* WaitAny */, 0, 0);
		} else {
			t = -wait_ticks;
			NtWaitForMultipleObjects((ULONG)ncons, console_handles, 1 /* WaitAny */, 0, &t);
		}
		return;
	}
	if (infinite) {
		/* NtDelayExecution has no NULL-means-forever convention (unlike
		 * the Nt*Wait*Object family read.c relies on); an absolute
		 * deadline far in the future is this library's existing idiom
		 * for "forever" -- see pause() in src/unistd/sleep.c. */
		LARGE_INTEGER never = 0x7fffffffffffffffLL;
		NtDelayExecution(0, &never);
	} else {
		t = -wait_ticks;
		if (!t) t = -1;
		NtDelayExecution(0, &t);
	}
}

/* One poll pass: build fresh output sets from the (unchanging) snapshot
 * *in_r, *in_w and *in_e, probing every requested descriptor without
 * blocking.  Returns the total ready count (select()'s return value
 * for this pass).  Descriptors requested for read on a console are not
 * resolved here except via a zero-timeout peek; still-pending ones are
 * left in console_h/console_fd (up to *ncons of them) for the caller to
 * wait on.  *have_pipe is set when any requested pipe is still
 * outstanding, telling the caller whether it may sleep the full
 * remaining timeout or must cap it at POLL_INTERVAL_TICKS. */
static int poll_pass(int nfds, const fd_set *in_r, const fd_set *in_w, const fd_set *in_e,
                      fd_set *out_r, fd_set *out_w, int *have_pipe,
                      HANDLE *console_h, int *console_fd, int *ncons)
{
	int d, total = 0, n = 0, hp = 0;

	FD_ZERO(out_r);
	FD_ZERO(out_w);
	for (d = 0; d < nfds; d++) {
		int wantr = in_r && FD_ISSET(d, in_r);
		int wantw = in_w && FD_ISSET(d, in_w);
		int wante = in_e && FD_ISSET(d, in_e);
		struct __fd *f;
		int cr, cw, hup;

		if (!wantr && !wantw && !wante) continue;
		f = __fd_get(d);  /* already known open: validated before the loop */

		if (f->type == __FD_PIPE) {
			if (wantr || wantw) hp = 1;
			__fd_probe(f, &cr, &cw, &hup);
			if (wantr && cr) { FD_SET(d, out_r); total++; }
			if (wantw && cw) { FD_SET(d, out_w); total++; }
		} else if (f->type == __FD_CONSOLE) {
			if (wantw) { FD_SET(d, out_w); total++; }
			if (wantr) { console_h[n] = f->h; console_fd[n] = d; n++; }
		} else {
			/* __FD_FILE/__FD_DIR/__FD_CHAR: always ready */
			if (wantr) { FD_SET(d, out_r); total++; }
			if (wantw) { FD_SET(d, out_w); total++; }
		}
	}

	/* Zero-timeout peek at every still-pending console read: cheap,
	 * and lets a console that was already signalled before this call
	 * be reported ready without ever sleeping. */
	{
		int i;
		LARGE_INTEGER zero = 0;
		for (i = 0; i < n; i++)
			if (NtWaitForSingleObject(console_h[i], 0, &zero) == STATUS_WAIT_0) {
				FD_SET(console_fd[i], out_r);
				total++;
			}
	}

	*have_pipe = hp;
	*ncons = n;
	return total;
}

/* The shared core: nfds and *rfds, *wfds, *efds have already been validated
 * (EBADF is checked here, once, before the first poll pass -- nfds and
 * timeout range validation is select()/pselect()'s job, done before
 * this is even called).  *remaining is the timeout budget in 100ns
 * ticks, decremented as time is spent sleeping; ignored when infinite
 * is set (a NULL timeout).  Returns the ready count, or -1 with
 * errno=EBADF. */
static int select_core(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, long long *remaining, int infinite)
{
	fd_set in_r, in_w, in_e, out_r, out_w;
	HANDLE console_h[FD_SETSIZE];
	int console_fd[FD_SETSIZE];
	int d, total, have_pipe, ncons;

	FD_ZERO(&in_r); FD_ZERO(&in_w); FD_ZERO(&in_e);
	if (rfds) in_r = *rfds;
	if (wfds) in_w = *wfds;
	if (efds) in_e = *efds;

	for (d = 0; d < nfds; d++) {
		if ((rfds && FD_ISSET(d, &in_r)) || (wfds && FD_ISSET(d, &in_w)) || (efds && FD_ISSET(d, &in_e))) {
			if (!__fd_get(d)) return -1;  /* EBADF: not a valid open descriptor */
		}
	}

	for (;;) {
		total = poll_pass(nfds, rfds ? &in_r : 0, wfds ? &in_w : 0, efds ? &in_e : 0,
		                   &out_r, &out_w, &have_pipe, console_h, console_fd, &ncons);
		if (total > 0) break;
		if (!infinite && *remaining == 0) break;  /* "to effect a poll": return promptly, nothing ready */

		{
			long long wait_ticks;
			int wait_infinite;

			if (infinite) {
				wait_infinite = !have_pipe;
				wait_ticks = have_pipe ? POLL_INTERVAL_TICKS : 0;
			} else {
				wait_infinite = 0;
				wait_ticks = have_pipe && *remaining > POLL_INTERVAL_TICKS ? POLL_INTERVAL_TICKS : *remaining;
			}
			__fd_wait_or_delay(console_h, ncons, wait_ticks, wait_infinite);
			if (!infinite) *remaining -= wait_ticks;
		}
	}

	if (rfds) *rfds = out_r;
	if (wfds) *wfds = out_w;
	if (efds) FD_ZERO(efds);
	return total;
}

int select(int nfds, fd_set *__restrict rfds, fd_set *__restrict wfds, fd_set *__restrict efds, struct timeval *__restrict timeout)
{
	long long remaining;
	int infinite;

	if (nfds < 0 || nfds > FD_SETSIZE) { errno = EINVAL; return -1; }
	if (timeout) {
		if (timeout->tv_sec < 0 || timeout->tv_usec < 0 || timeout->tv_usec >= 1000000) { errno = EINVAL; return -1; }
		remaining = (long long)timeout->tv_sec * __TICKS_PER_SEC + (long long)timeout->tv_usec * 10;
		infinite = 0;
	} else {
		remaining = 0;
		infinite = 1;
	}
	return select_core(nfds, rfds, wfds, efds, &remaining, infinite);
}

int pselect(int nfds, fd_set *__restrict rfds, fd_set *__restrict wfds, fd_set *__restrict efds,
            const struct timespec *__restrict timeout, const sigset_t *__restrict sigmask)
{
	long long remaining;
	int infinite, r;
	sigset_t omask;

	if (nfds < 0 || nfds > FD_SETSIZE) { errno = EINVAL; return -1; }
	if (timeout) {
		if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L) { errno = EINVAL; return -1; }
		remaining = (long long)timeout->tv_sec * __TICKS_PER_SEC + (timeout->tv_nsec + 99) / 100;
		infinite = 0;
	} else {
		remaining = 0;
		infinite = 1;
	}

	if (sigmask) sigprocmask(SIG_SETMASK, sigmask, &omask);
	r = select_core(nfds, rfds, wfds, efds, &remaining, infinite);
	if (sigmask) sigprocmask(SIG_SETMASK, &omask, 0);
	return r;
}
