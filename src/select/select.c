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
 *     ReadDataAvailable -- so this is polled.  Writability comes from
 *     the same call's WriteQuotaAvailable, which is the field
 *     documented for it and which real NT populates correctly; it is
 *     used only once wqa_works() below has confirmed, by positive
 *     control, that this platform populates it at all, because
 *     wine-9.0 and older return a hard 0 there for every pipe and
 *     reading that as "full" would report an empty pipe unwritable.
 *   - __FD_SOCKET: like a pipe, the handle itself is not a waitable
 *     NT object for this purpose, so it is polled -- by a single
 *     zero-timeout IOCTL_AFD_SELECT per pass (see __fd_probe() below).
 *     Unlike every other shape here, a socket has an honest answer for
 *     *both* directions: AFD reports send-side room as well as
 *     receive-side data.
 *   - __FD_FILE/__FD_DIR/__FD_CHAR/__FD_UNKNOWN: "File descriptors
 *     associated with regular files shall always select true for ready
 *     to read, ready to write" (select.html DESCRIPTION) -- applied
 *     here to __FD_CHAR too (NUL, COM, ...) for the same reason POSIX
 *     gives regular files a free pass: nothing in this library ever
 *     blocks a read or write to one of these shapes past the syscall
 *     itself.  __FD_UNKNOWN joins them because there is no probe that
 *     could apply to a handle __handle_type() could not classify.
 *     These are the only shapes for which "always ready" is a real
 *     answer rather than an absence of one.
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
 * EINTR (select.html ERRORS): returned when a signal is CAUGHT (a real
 * handler actually runs) during the wait -- src/signal/sigdelivery.c is
 * what makes that possible at all: its per-process delivery thread is
 * the one asynchronous, other-thread signal source this library has,
 * driven by another process's kill(). select_core() below (this file)
 * tells caught-during-this-call apart from already-pending or never-
 * happened by comparing src/signal/signal.c's __sig_caught_count()
 * across the wait -- see that function's own comment. SA_RESTART does
 * NOT suppress this: select.html leaves the SA_RESTART interaction
 * implementation-defined ("it is implementation-defined whether the
 * function restarts or returns [EINTR]"), and this chooses the same
 * answer Linux's select()/poll() do -- EINTR always, regardless of the
 * flag -- documented in sigaction()'s own SA_RESTART comment
 * (src/signal/signal.c). A signal that arrives BLOCKED still just sets
 * `pending`, same as always, and does not interrupt this wait at all
 * (__sig_caught_count() only advances for an actual handler entry) --
 * it is drained later by sigprocmask()'s existing unblock path, same as
 * everywhere else in this library.
 *
 * What this does NOT cover: alarm() is still a documented no-op stub
 * (src/unistd/sleep.c), so nothing here ever races that; and a signal
 * generated by THIS thread's own code (raise(), a hardware fault) still
 * cannot interrupt a wait this same thread is blocked inside, for the
 * ordinary reason a single thread cannot run two things at once --
 * exactly as before this file's cross-process mechanism existed.
 *
 * pselect()'s sigmask (pselect.html DESCRIPTION: "shall be equivalent
 * to atomically saving the current signal mask, installing sigmask...
 * blocking until...ready or a signal is caught, then restoring the
 * signal mask", the point being no race between unblocking a signal
 * and starting the wait): the mask *is* installed before the wait and
 * restored after, both via the same sigprocmask() everyone else uses.
 * The one gap left, honestly: sigprocmask()'s own drain of newly-
 * unblocked pending signals happens strictly before select_core()
 * below captures its "caught so far" baseline, so a cross-process
 * signal that is delivered (unblocked, real handler, caught by
 * sig_delivery_thread on its own thread) in the narrow window between
 * those two steps is not reported as EINTR here -- its handler still
 * ran, correctly, just without pselect() noticing the interruption.
 * Closing that would mean capturing the baseline under the same lock
 * sigprocmask() itself holds while draining, which is a real change to
 * make, not a documentation fix; left open rather than silently assumed
 * fixed.
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
 *
 * That __FD_SOCKET case was, for a while, dead code: poll_pass() below
 * and poll.c's loop both routed *only* __FD_PIPE to __fd_probe() and
 * dropped everything else -- sockets included -- into the "always
 * ready" branch that only __FD_FILE/__FD_DIR/__FD_CHAR earn.  Every
 * socket therefore reported readable and writable unconditionally, and
 * __fd_probe()'s socket path was never reached to contradict it.  The
 * routing is now by what can actually be probed (pipes and sockets),
 * not by a single named type, and the "always ready" branch is reached
 * only by the shapes POSIX and this platform genuinely make always
 * ready.  test/posix-select-socket.c is the regression assertion:
 * an idle socket must not be reported ready, which is the one claim
 * the old code could not make.
 *
 * That routing change then exposed a second way to report a socket
 * always-ready, this time on the *success* path: the ioctl was issued
 * with one buffer as both input and output, and the reply was read as
 * Handles[0].PollEvents with no reference to the reply's own
 * NumberOfHandles.  AfdPoll() reports "nothing is ready" by setting
 * NumberOfHandles to zero and completing with
 * IoStatus.Information == 16 -- the header alone -- and
 * IOCTL_AFD_SELECT is METHOD_BUFFERED, so nothing past +16 is copied
 * back into the caller's buffer.  Aliased with the request, that slot
 * still held the *requested* mask, so every idle probe read back as
 * "everything fired".  The __FD_SOCKET case below now uses a separate,
 * zeroed reply buffer and reads it through __afd_poll_events_for(),
 * which bounds by the reply's count and matches on the handle; see
 * src/internal/afd.h's poll banner for the driver source behind each
 * of those, and test/posix-socket-poll.c's check_reply() for the
 * device-free negative control.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "plat_select.h"

/* 20ms, in 100ns units (see file banner for why). */
#define POLL_INTERVAL_TICKS 200000LL

/* Is FILE_PIPE_LOCAL_INFORMATION's WriteQuotaAvailable populated on the
 * platform this process is running on?
 *
 * This has to be asked, and asked behaviourally, because a 0 in that
 * field is ambiguous and the two readings are opposites:
 *
 *   - on a platform that implements it, 0 means "the write direction is
 *     completely full", i.e. do NOT report writable; and
 *   - on one that does not, 0 is simply what the field always holds, and
 *     reporting not-writable on it would make select()/poll() claim a
 *     perfectly empty pipe is unwritable -- a caller that waits for
 *     writability before writing then waits forever.
 *
 * Both readings are live in environments this library is built and
 * tested in.  Wine's server hardcoded WriteQuotaAvailable to a literal
 * 0, marked FIXME, until commit 4cbb92cfb ("server: Improve returned
 * value in member WriteQuotaAvailable.", 2024-11-16), which first
 * shipped in wine-10.0;
 * wine-9.0 -- the version `make check` runs against on the primary
 * development machine -- predates it and returns a hard 0 for every
 * pipe, empty or full.  Reading that 0 as "full" is precisely the
 * always-wrong direction, so the field cannot be trusted blind.
 *
 * The discriminator is a positive control, run once per process and
 * cached: make a private pipe, write nothing to it, and ask.  A freshly
 * created pipe with a non-zero quota has its entire write direction
 * free, so an implementation that populates the field MUST report a
 * non-zero WriteQuotaAvailable there.  Anything else -- a 0, or a query
 * that fails -- means the field carries no signal on this platform, and
 * the old always-ready fallback is used instead.  That fallback is
 * over-eager only (it can report ready when a write would block, never
 * the reverse), which is the direction this project accepts elsewhere
 * for "no honest answer" cases.
 *
 * Note which end is probed.  src/unistd/pipe.c makes the read end the
 * pipe's server end and the write end its client end, so the handle a
 * writability probe queries is always a CLIENT end.  The probe below
 * therefore queries the client end too, matching what __fd_probe() will
 * actually be asked about rather than the more commonly measured server
 * end.
 *
 * Returns 1 if the field is usable, 0 if not. */
static int wqa_works(void)
{
	/* -1 = not yet asked.  A benign race here would only repeat the
	 * probe and reach the same answer; this library is single-threaded
	 * regardless (see the banner in src/internal/libc.h). */
	static int cached = -1;

	if (cached < 0) cached = __plat_pipe_wqa_trustworthy();
	return cached;
}

/* See src/internal/libc.h for the contract. */
void __fd_probe(struct __fd *f, int *canread, int *canwrite, int *hup)
{
	*hup = 0;
	switch (f->type) {
	case __FD_PIPE: {
		unsigned long read_avail, write_quota;
		if (!__plat_pipe_probe(f->h, &read_avail, &write_quota)) {
			/* Broken/disconnected: a read() would return 0 (EOF) or an
			 * error, a write() would fail -- neither would block, so
			 * both count as ready, same as a hung-up descriptor does
			 * on Linux.
			 *
			 * This test deliberately stays AHEAD of the
			 * WriteQuotaAvailable consult below, and the ordering is
			 * load-bearing rather than incidental.  NT does not carry
			 * the buffered-bytes reduction past a peer disconnect:
			 * measured on Server 2025, a pipe with 32768 bytes still
			 * buffered whose reading end had closed reported its full
			 * 65536 quota as available.  A disconnected pipe therefore
			 * reads as "plenty of room", which must not be taken as
			 * authoritative writability -- the write() that follows
			 * will fail, not succeed.
			 *
			 * Both paths happen to land on ready here, which is what
			 * POSIX wants (write.html: the write() is what reports
			 * [EPIPE], so select() must not hide it by withholding
			 * readiness), so the observed NT behaviour is convenient
			 * rather than dangerous.  It is still resolved here, by
			 * the explicit state test, so that it stays correct if NT
			 * ever reports 0 in that cell instead. */
			*canread = 1; *canwrite = 1; *hup = 1;
			break;
		}
		*canread = read_avail > 0;
		/* WriteQuotaAvailable is the field documented for exactly
		 * this, and on real NT it works: measured on Windows Server
		 * 2025 build 26100, an end's WriteQuotaAvailable is its
		 * write-direction quota minus the bytes currently buffered in
		 * that direction, tracking live and restored exactly by a
		 * drain.  So consult it -- but only once this process has
		 * established that the field is populated at all here; see
		 * wqa_works() for why that check is not optional and why a 0
		 * cannot be read as "full" without it.
		 *
		 * Scope of what this field is being trusted for here.
		 * src/unistd/pipe.c has the library's only
		 * NtCreateNamedPipeFile call, and it is always byte-stream
		 * (FILE_PIPE_BYTE_STREAM_TYPE/_MODE) with both quotas at
		 * 65536.  A __FD_PIPE inherited from a non-ntlibc parent
		 * could be another shape, so the two that differ are worth
		 * naming:
		 *
		 *   - message mode needs no special case.  NT was measured
		 *     charging data bytes only, with no per-message overhead:
		 *     eight cells writing the same 16384-byte total into a
		 *     65536 quota, split 1x16384, 64x256, 256x64 and 512x32,
		 *     all reported the same 49152, so nothing scales with
		 *     message count.  (That sweep bounds per-message charge,
		 *     not framing itself -- it cannot separate "framing is
		 *     free" from "NT coalesced the writes" -- but the charge
		 *     is what this comparison depends on.)
		 *   - a zero-quota pipe is a genuinely zero-size buffer on
		 *     NT rather than a system default, and the rule is not
		 *     established there.  This library cannot create one, and
		 *     an inherited one would read 0 here and be reported
		 *     not-writable, which for a buffer that can never accept
		 *     an unread byte is the conservative answer.
		 *
		 * No arithmetic is done on the field -- it is compared, not
		 * reduced -- so the underflow that an unclamped
		 * quota-minus-buffered subtraction is prone to (a transiently
		 * over-quota buffered count wrapping into "enormous room
		 * available", i.e. today's wrong answer by another route)
		 * cannot arise on this side.  That clamp is the pipe
		 * implementation's job; see fuzz/ntstubs.c for this project's
		 * own instance of it. */
		*canwrite = wqa_works() ? write_quota > 0 : 1;
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
	case __FD_SOCKET:
		/* test/networking-audit.md sec 3: a single non-blocking
		 * IOCTL_AFD_SELECT (== Wine's IOCTL_AFD_POLL, same wire
		 * request -- src/internal/afd.h) against just this one
		 * socket, Timeout=0 so it never waits.  A close/abort/
		 * disconnect event counts as both readable and writable and
		 * as hup, the same way a broken pipe does above -- a read or
		 * write on it would return immediately rather than block.
		 * See src/select/nt/plat_select.c's __plat_socket_probe() for
		 * the full reasoning behind the request/reply shape. */
		__plat_socket_probe(f->h, canread, canwrite, hup);
		break;
	case __FD_FILE:
	case __FD_DIR:
	case __FD_CHAR:
	case __FD_UNKNOWN:
	default:
		/* select.html DESCRIPTION: "File descriptors associated
		 * with regular files shall always select true for ready to
		 * read, ready to write". Applied here to __FD_CHAR too
		 * (NUL, COM, ...): nothing in this library ever blocks a
		 * read or write to one past the syscall itself, so "always
		 * ready" is the *correct* answer for these shapes, not a
		 * fallback for an answer that could not be obtained.
		 *
		 * __FD_UNKNOWN is spelled out rather than left to
		 * `default` to record that it was decided, not forgotten:
		 * it is a handle __handle_type() could not classify, so by
		 * construction there is no probe that would apply to it,
		 * and no evidence it would ever block either.  Always
		 * ready is the only non-arbitrary answer available, and it
		 * errs the same way everything else here does when there
		 * is no honest signal -- over-eager rather than hanging. */
		*canread = 1;
		*canwrite = 1;
		break;
	}
}

/* See src/internal/libc.h for the contract.
 *
 * Also waits on src/signal/sigdelivery.c's per-process "a packet
 * arrived" event, when this process has one (0 if __sig_delivery_init()
 * never got a working listener -- see that function). This is the
 * entire mechanism behind select()/pselect()'s new EINTR path
 * (select_core() in this file): without it, a cross-process signal
 * could still set `pending` correctly, but this wait would not notice
 * until it next woke up on its own -- immediately for a console-only
 * wait with nothing pending, but up to POLL_INTERVAL_TICKS (20ms) or
 * the full timeout otherwise, and forever for an infinite wait with no
 * console descriptors and no pipes/sockets outstanding. Folding the
 * event into the same NtWaitForMultipleObjects call the console
 * descriptors already use costs nothing extra when it is not signalled,
 * and turns every one of those latencies into "immediately" when it
 * is. */
void __fd_wait_or_delay(__plat_handle_t *console_handles, int ncons, long long wait_ticks, int infinite) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	__plat_handle_t handles[FD_SETSIZE + 1];
	__plat_handle_t sigev = __sig_delivery_event();
	int n = 0, i;

	for (i = 0; i < ncons; i++) handles[n++] = console_handles[i];
	if (sigev) handles[n++] = sigev;

	if (n > 0) {
		__plat_wait_multiple(handles, n, wait_ticks, infinite);
		return;
	}
	__plat_delay(wait_ticks, infinite);
}

/* One poll pass: build fresh output sets from the (unchanging) snapshot
 * *in_r, *in_w and *in_e, probing every requested descriptor without
 * blocking.  Returns the total ready count (select()'s return value
 * for this pass).  Descriptors requested for read on a console are not
 * resolved here except via a zero-timeout peek; still-pending ones are
 * left in console_h/console_fd (up to *ncons of them) for the caller to
 * wait on.  *have_poll is set when any requested pipe *or socket* is
 * still outstanding, telling the caller whether it may sleep the full
 * remaining timeout or must cap it at POLL_INTERVAL_TICKS -- neither
 * shape has a waitable NT object behind it, so both are re-probed on
 * that timer rather than waited on. */
/* out_r/out_w/have_poll/ncons required: FD_ZERO(out_r)/FD_ZERO(out_w)
 * are the first two statements in this function's body (dereferenced
 * through the fd_set macros in include/sys/select.h), and
 * *have_poll = hp / *ncons = n are both written unconditionally just
 * before returning, on every path through the function. in_r/in_w/in_e
 * are deliberately NOT required: each is only ever touched behind its
 * own `in_r &&`/`in_w &&`/`in_e &&` guard, matching select_core()'s own
 * `if (rfds) in_r = *rfds;` shape one level up -- select()/pselect()'s
 * rfds/wfds/efds are genuinely optional per POSIX, and that optionality
 * is threaded all the way through rather than defended against only at
 * the top. console_h/console_fd are also not required: both are only
 * ever written inside `if (wantr)`/read inside a loop bounded by `n`,
 * which can legitimately be 0 -- never unconditional the way out_r/
 * out_w/have_poll/ncons are, even though every real call site (this
 * file's select_core()) happens to pass real fixed-size local arrays
 * for them too. */
static int poll_pass(int nfds, const fd_set *in_r, const fd_set *in_w, const fd_set *in_e,
                      fd_set *out_r, fd_set *out_w, int *have_poll,
                      __plat_handle_t *console_h, int *console_fd, int *ncons)
    __attribute__((nonnull(5, 6, 7, 10)));
static int poll_pass(int nfds, const fd_set *in_r, const fd_set *in_w, const fd_set *in_e,
                      fd_set *out_r, fd_set *out_w, int *have_poll,
                      __plat_handle_t *console_h, int *console_fd, int *ncons) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
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

		if (f->type == __FD_PIPE || f->type == __FD_SOCKET) {
			/* The two shapes __fd_probe() has a real, instantaneous
			 * answer for: a pipe's ReadDataAvailable and a socket's
			 * IOCTL_AFD_SELECT.  Neither handle is an NT wait object
			 * that becomes signalled on its own (unlike a console),
			 * so both must be re-probed on a timer -- hence hp. */
			if (wantr || wantw) hp = 1;
			__fd_probe(f, &cr, &cw, &hup);
			if (wantr && cr) { FD_SET(d, out_r); total++; }
			if (wantw && cw) { FD_SET(d, out_w); total++; }
		} else if (f->type == __FD_CONSOLE) {
			if (wantw) { FD_SET(d, out_w); total++; }
			if (wantr) { console_h[n] = f->h; console_fd[n] = d; n++; }
		} else {
			/* __FD_FILE/__FD_DIR/__FD_CHAR/__FD_UNKNOWN: always
			 * ready.  Not a fallback for "we could not check" --
			 * it is the right answer for these shapes.  See the
			 * file banner and __fd_probe()'s default case. */
			if (wantr) { FD_SET(d, out_r); total++; }
			if (wantw) { FD_SET(d, out_w); total++; }
		}
	}

	/* Zero-timeout peek at every still-pending console read: cheap,
	 * and lets a console that was already signalled before this call
	 * be reported ready without ever sleeping. */
	{
		int i;
		for (i = 0; i < n; i++)
			if (__plat_wait_ready(console_h[i])) {
				FD_SET(console_fd[i], out_r);
				total++;
			}
	}

	*have_poll = hp;
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
	__plat_handle_t console_h[FD_SETSIZE];
	int console_fd[FD_SETSIZE];
	int d, total, have_poll, ncons;
	/* EINTR (select.html ERRORS): see this file's banner for why this
	 * can happen at all now, and for the implementation-defined choice
	 * made below.  __sig_caught_count() (src/signal/signal.c) only
	 * advances when a signal-catching function is actually entered --
	 * never for SIG_IGN, and never for a signal that arrived blocked and
	 * only went to `pending` -- so this is exactly "a signal was caught
	 * during select()", not "a signal arrived at all". */
	unsigned long sig_start = __sig_caught_count();

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
		__sig_drain_pending();
		if (__sig_caught_count() != sig_start) { errno = EINTR; return -1; }
		total = poll_pass(nfds, rfds ? &in_r : 0, wfds ? &in_w : 0, efds ? &in_e : 0,
		                   &out_r, &out_w, &have_poll, console_h, console_fd, &ncons);
		if (total > 0) break;
		if (!infinite && *remaining == 0) break;  /* "to effect a poll": return promptly, nothing ready */

		{
			long long wait_ticks;
			int wait_infinite;
			long long before, after;

			if (infinite) {
				wait_infinite = !have_poll;
				wait_ticks = have_poll ? POLL_INTERVAL_TICKS : 0;
			} else {
				wait_infinite = 0;
				wait_ticks = have_poll && *remaining > POLL_INTERVAL_TICKS ? POLL_INTERVAL_TICKS : *remaining;
			}
			/* Real elapsed time, not wait_ticks: __fd_wait_or_delay()
			 * can now return long before its requested budget is up,
			 * woken early by src/signal/sigdelivery.c's wake_event for
			 * a signal that turned out to be blocked (still just sets
			 * `pending`, no different from before this file's EINTR
			 * path existed) or ignored (no different either). Charging
			 * the full wait_ticks against *remaining regardless -- what
			 * this did before an early wake was possible, when every
			 * wait genuinely ran its full requested length -- would
			 * make a spurious wakeup exhaust the entire timeout budget
			 * in one wrongly-accounted step, turning what should be a
			 * routine extra poll_pass() into an immediate, wrong
			 * timeout return. NtQuerySystemTime is the same clock
			 * src/time/clock_gettime.c's CLOCK_REALTIME already reads;
			 * good enough for a same-process, sub-second interval like
			 * this one. */
			if (!infinite) before = __plat_now_100ns();
			__fd_wait_or_delay(console_h, ncons, wait_ticks, wait_infinite);
			if (!infinite) {
				long long elapsed;
				after = __plat_now_100ns();
				elapsed = after - before;
				if (elapsed < 0) elapsed = 0;
				if (elapsed > *remaining) elapsed = *remaining;
				*remaining -= elapsed;
			}
		}

		/* __fd_wait_or_delay() above also waits on
		 * src/signal/sigdelivery.c's wake_event (see that function),
		 * so a cross-process signal wakes this promptly instead of
		 * waiting out the rest of POLL_INTERVAL_TICKS or the timeout.
		 * sig_delivery_thread() publishes the pending record before it sets
		 * that event.  The empty lock/unlock also rendezvous with other signal
		 * producers; the drain then runs any eligible handler on this
		 * application thread. */
		__sig_lock();
		__sig_unlock();
		__sig_drain_pending();
		if (__sig_caught_count() != sig_start) { errno = EINTR; return -1; }
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
		remaining = __duration_ticks(timeout->tv_sec,
			(long)timeout->tv_usec * 1000L);
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
		remaining = __duration_ticks(timeout->tv_sec, timeout->tv_nsec);
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

// NOLINTEND(misc-include-cleaner)
