/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Cross-process signal delivery, phase 1.
 *
 * src/signal/signal.c's file banner says it outright: kill() to another
 * process used to consult only that process's DEFAULT disposition,
 * never any sa_handler/SIG_IGN it had installed, because there was no
 * channel to tell a running process "a signal arrived, go look at your
 * own tables". This file is that channel.
 *
 * The design, in one sentence: every process that has run __signal_init()
 * owns a named pipe (\Device\NamedPipe\ntlibc-sig.<pid>) and one
 * dedicated OS thread blocked reading it; kill() to another ntlibc
 * process writes a small packet to *its* pipe instead of guessing, and
 * that process's own delivery thread queues the packet process-wide.
 * An eligible application thread drains it at a signal-aware safe point,
 * so that thread's sigprocmask() state controls delivery.
 *
 * What this deliberately does NOT do -- thread-context hijacking
 * (SuspendThread/GetThreadContext/SetThreadContext + instruction-pointer
 * rewrite, the way a real kernel or Cygwin's exceptions.cc interrupts a
 * thread mid-instruction) -- is why a signal that arrives while the
 * application thread is off running ordinary code between syscalls
 * stays pending rather than interrupting it: nothing here ever touches
 * the application thread's register state. Only src/select/select.c's
 * poll loop is taught to notice a fresh delivery early, because it
 * already polls; every other blocking call keeps the exact latency it
 * had before this file existed. That is an accepted, documented gap,
 * not an oversight -- see EXPLICITLY OUT OF SCOPE in this change's
 * commit message. NtCancelSynchronousIoFile, which would let a blocked
 * read()/write() be interrupted the same way select() now is, is a
 * `@ stdcall -stub` in ReactOS's dll/ntdll/def/ntdll.spec and is not
 * used here for that reason.
 *
 * Two NT mechanics this file leans on hard enough to be worth stating
 * up front, both measured against ReactOS's own npfs.sys source
 * (drivers/filesystems/npfs/{create,read,statesup,fsctrl}.c) since that
 * is the only place the wire behaviour of NtCreateNamedPipeFile /
 * NtReadFile / NtFsControlFile against a named pipe is written down in
 * anything this project can grep:
 *
 *   - Opening a pipe path that names no listener is a plain object-
 *     manager name-lookup failure (STATUS_OBJECT_NAME_NOT_FOUND),
 *     resolved synchronously by the parse routine -- it is NOT the
 *     Win32 CreateFile "wait for a free instance" behaviour, which is a
 *     distinct, opt-in mechanism (FSCTL_PIPE_WAIT / kernel32's
 *     WaitNamedPipe) that sig_try_deliver_remote() below never invokes.
 *     So a kill() to a pid with no listener -- no such process, a
 *     non-ntlibc process, or an ntlibc process that has not finished
 *     __signal_init() yet -- fails fast, never hangs.
 *   - A named pipe server instance is only ever readable while its
 *     NamedPipeState is "connected". A fresh instance starts
 *     "listening", and NtReadFile against a merely-listening instance
 *     fails immediately with STATUS_PIPE_LISTENING rather than
 *     blocking for a client (read.c). FSCTL_PIPE_LISTEN
 *     (src/internal/nt.h) is the operation that actually blocks until a
 *     client connects. Skipping it -- reading straight after create --
 *     works exactly once, for the very first sender, because a freshly
 *     created instance happens to already be listening; every sender
 *     after that gets STATUS_PIPE_LISTENING and is silently never
 *     delivered. This was caught by reading npfs before writing the
 *     loop below, not by testing: a test that only ever sends one
 *     signal per process pair would not have caught it either.
 *
 * The loop below sidesteps the companion question -- whether a *reused*
 * instance can be safely handed back to FSCTL_PIPE_LISTEN once its
 * client has disconnected -- rather than answering it. ReactOS's
 * NpSetDisconnectedPipeState (statesup.c) has no case for a CCB a
 * client has already closed out from under (state FILE_PIPE_CLOSING_STATE);
 * that falls to its `default: NpBugCheck(...)`, i.e. the client closing
 * its handle before the server gets around to FSCTL_PIPE_DISCONNECT --
 * which was exactly kill()'s original one-way write-then-NtClose pattern
 * -- is a plausible route to a driver bugcheck on that implementation. Whether
 * real Windows's npfs.sys shares that gap is not something this project
 * can inspect; ReactOS is not this library's actual runtime target
 * (README.md: Windows 7+; CONTRIBUTING.md is explicit that a name
 * existing in ReactOS or Wine is not evidence about Microsoft's ntdll),
 * so the honest position is "untested, avoid it" rather than "safe,
 * assumed". So sig_delivery_thread() below never reuses an instance or
 * calls FSCTL_PIPE_DISCONNECT at all. Instead it overlaps two instances
 * during each handoff: after reading request A, it creates listening
 * instance B before acknowledging A, then closes A and waits on B.
 * NtCreateNamedPipeFile uses FILE_OPEN_IF for B because the named-pipe
 * object already exists; FILE_CREATE means "the object name must be new"
 * and correctly rejects a second instance.
 *
 * A per-target named NT mutant serializes request/reply exchanges made by
 * every sending process. The server acknowledges every packet only after
 * the replacement instance exists, so the next mutant owner cannot reach
 * NtOpenFile during a close/recreate gap. If that next owner connects to B
 * before the server reaches FSCTL_PIPE_LISTEN, NT reports
 * STATUS_PIPE_CONNECTED; that is successful handoff, not an error. This
 * explicit request/reply ordering is the reason kill() needs no timed
 * retry. One extra NtCreateNamedPipeFile+NtClose pair per signal remains
 * immaterial for a control channel rather than a hot data path.
 *
 * Locking. Before this file, signal.c's own header truthfully said "no
 * threading support to speak of" -- sigwait()'s banner already
 * documents one narrow exception (the NTLIBC_USE_KERNEL32 console-
 * control handler thread), unguarded. This file adds a second, ordinary
 * one: sig_delivery_thread() calls __raise_internal() concurrently with
 * whatever the application thread is doing, and both sides read and
 * write signal.c's shared dispositions and process-pending queue.
 * Thread masks, thread-pending state and alternate stacks are TLS.
 * __sig_lock()/__sig_unlock() below are a
 * RECURSIVE mutex built from a SynchronizationEvent (auto-reset: the
 * first waiter to see it signalled is the only one released, the "P"/"V"
 * a binary semaphore needs) plus an owning-thread id and a re-entry
 * depth -- signal.c acquires it around every external entry point that
 * touches that state (sigaction(), signal(), sigprocmask(),
 * sigpending(), sigaltstack()) and around every call to
 * __raise_internal() (raise(), the vectored exception handler, the
 * optional console-control handler, and sig_delivery_thread() below).
 * __raise_internal() itself never locks -- it assumes its caller
 * already holds the lock, which sigprocmask()'s own internal call to it
 * (draining newly-unblocked pending signals) depends on.
 *
 * Recursive is still load-bearing for nested internal delivery before and
 * after the application callback. The callback itself runs outside the lock:
 * otherwise a handler waiting for another thread deadlocks when that thread
 * reaches any signal-aware cancellation point. The owner-id-plus-depth pair
 * lets the SAME thread walk back in without waiting on itself while a
 * genuinely DIFFERENT thread still blocks for real; lock_owner/lock_depth are
 * touched only by whichever thread currently owns the lock, or is in
 * the middle of acquiring it, where a torn read of a stale owner value
 * only ever costs one spurious real wait, never a wrong grant -- actual
 * exclusion is still lock_event, an NT synchronization primitive.
 *
 * Per-thread masks, pending queues, wait state, fault metadata, and alternate
 * stacks make concurrent handlers independent while dispositions and the
 * process-pending queue remain protected by this lock.
 *
 * __sig_lock()/__sig_unlock() are no-ops if the mutex event was never
 * created (lock_event == 0): a process whose __sig_delivery_init() ran
 * before this file existed cannot happen (there is no such build), but
 * a process on a platform where even NtCreateEvent fails is not
 * something this file should turn into a crash -- see __sig_delivery_init()
 * below for why that stays possible in principle and degrades instead
 * of failing __signal_init() outright. */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <signal.h>
#include <unistd.h>
#include <unistd.h>
#include <string.h>
#include "libc.h"
#include "plat_signal.h"
#include "plat_fd.h"

/* Wire format: one fixed-size NT message per signal. FILE_PIPE_MESSAGE_TYPE
 * (below) is what makes "one NtWriteFile call == one NtReadFile call"
 * an NT-enforced guarantee rather than a hope about write sizes staying
 * under some byte-stream atomicity threshold -- unlike
 * src/unistd/pipe.c's anonymous pipes, which are byte-stream because
 * they only ever have the one reader this library itself created and
 * never interleave writers. */
struct sigpacket {
	ULONG magic;
	ULONG signo;
	ULONG flags;
	ULONG sender_pid;
	ULONG sender_uid;
	LONG code;
	union sigval value;
};
#define SIGPACKET_MAGIC 0x736c746eu /* "ntls", little-endian as stored */
#define SIGPACKET_NONDEFAULT_ONLY 1u

static __plat_handle_t wake_event;   /* auto-reset; set on every packet arrival. 0 = not running. */
static HANDLE lock_event;   /* auto-reset-as-mutex, initially signalled (free). 0 = no locking done.
                              * Kept as a raw HANDLE, and __sig_lock()/__sig_unlock() below kept
                              * calling Nt{Wait,Set}Event on it directly: those four functions are
                              * explicitly out of scope for this migration (see this file's own
                              * Locking banner and plat_signal.h's file banner). */
static __plat_handle_t send_mutant;  /* named per target; serializes clients across processes. */

/* RECURSIVE, deliberately: internal signal paths can nest before or after a
 * user handler, even though the handler callback itself temporarily drops the
 * lock. Tracking the owning thread and a re-entry depth makes the same thread
 * able to walk back in without waiting on itself, while a genuinely different
 * thread still blocks for real. lock_owner/lock_depth are touched only by whichever
 * thread currently owns the lock (or is in the middle of acquiring it,
 * where a torn read of a stale owner value only ever costs a spurious
 * real wait, never a wrong grant -- the actual exclusion is still
 * lock_event, an NT synchronization primitive). */
static pid_t lock_owner;
static int lock_depth;

/* __plat_handle_t __sig_delivery_event(void), declared in libc.h, is
 * select()'s read of wake_event -- deliberately not exposed as a
 * variable so a caller outside this file can never accidentally close
 * or signal it directly. */
__plat_handle_t __sig_delivery_event(void) { return wake_event; }

/* State-checking wait loops in signal.c and sleep.c use the same event as
 * select(), but wait alertably so timer APCs remain deliverable. A set that
 * lands between a caller's state check and this wait is retained by the
 * auto-reset event, closing the lost-wakeup window without a polling slice.
 *
 * The NTSTATUS return and LARGE_INTEGER* parameter stay exactly as they
 * were: src/unistd/sleep.c, outside this migration, calls this directly
 * with a LARGE_INTEGER it builds itself, so the signature cannot change.
 * No caller of this function (here or in signal.c/sleep.c) ever inspects
 * the returned status, so folding every real outcome into one reported
 * STATUS_SUCCESS below is not a behavior change -- see plat_signal.h's
 * file banner. */
NTSTATUS __sig_wait_delivery(LARGE_INTEGER *timeout)
{
	__plat_signal_wait(wake_event, timeout != 0, timeout ? (long long)*timeout : 0);
	return STATUS_SUCCESS;
}

void __sig_notify_delivery(void)
{
	if (wake_event) __plat_event_set(wake_event);
}

/* NTLIBC_NO_THREAD_SAFETY_ANALYSIS: this and the three functions below it
 * are __ntlibc_sig_lock_token's actual implementation -- the raw NT
 * primitives (lock_event/lock_owner/lock_depth) a lockset checker cannot
 * see through are exactly what "holding the capability" means here, so
 * asking it to re-derive that from these bodies is circular.  Every
 * caller still sees the ACQUIRE()/RELEASE() contract from libc.h; only
 * self-checking these four definitions' own insides is turned off. */
void __sig_lock(void) NTLIBC_NO_THREAD_SAFETY_ANALYSIS
{
	pid_t me;
	if (!lock_event) return;
	/* A defer region, not an unsafe one: __sig_lock()/__sig_unlock() run
	 * on the way into and out of ordinary, POSIX-legal blocking calls
	 * (sleep(), sigwait(), and friends), not only inside calls the
	 * application chose to make async-cancel-unsafe on purpose.  Marking
	 * this region unsafe (as it was before this fix, for eight minutes
	 * of this codebase's own history -- see [[wine-clone-process-hazards]]
	 * and 3d8ff6c, which converted the PEB lock to defer immediately
	 * after and should have moved this one too) meant any async
	 * cancellation landing while a thread was merely checking pending
	 * signals hit cancel_unsafe_abort() and took the whole process down
	 * with it: a regression this project's own OPTS legs (pthread_cancel/
	 * 2-1, 3-1, 4-1, pthread_cleanup_push/1-2, all "expected PASS" ->
	 * ABNORMAL) caught directly. Deferring instead lets the redirect
	 * wait for this lock to be released and then deliver promptly, the
	 * same as the PEB lock already does. */
	__pthread_cancel_defer_enter();
	me = gettid();
	if (lock_depth > 0 && lock_owner == me) { lock_depth++; return; }
	NtWaitForSingleObject(lock_event, 0, 0);
	lock_owner = me;
	lock_depth = 1;
}

void __sig_unlock(void) NTLIBC_NO_THREAD_SAFETY_ANALYSIS
{
	LONG prev;
	if (!lock_event) return;
	if (--lock_depth > 0) {
		__pthread_cancel_defer_leave();
		return;
	}
	lock_owner = 0;
	NtSetEvent(lock_event, &prev);
	__pthread_cancel_defer_leave();
}

/* Publish delivery state before entering application code, then let other
 * threads make progress while the handler runs. Preserve recursion depth so
 * the internal caller which eventually unwinds still owns exactly the
 * acquisitions it made before the callback. */
int __sig_unlock_for_handler(void) NTLIBC_NO_THREAD_SAFETY_ANALYSIS
{
	LONG previous;
	int depth;
	if (!lock_event) return 0;
	depth = lock_depth;
	lock_depth = 0;
	lock_owner = 0;
	NtSetEvent(lock_event, &previous);
	return depth;
}

void __sig_relock_after_handler(int depth) NTLIBC_NO_THREAD_SAFETY_ANALYSIS
{
	if (!lock_event || depth <= 0) return;
	__sig_lock();
	lock_depth = depth;
}

/* \Device\NamedPipe\ntlibc-sig.<pid, 8 hex digits> -- the same prefix-
 * plus-hex-pid shape src/unistd/pipe.c's __pipe_handles() uses for its
 * anonymous pipes, minus the serial suffix: there is exactly one
 * signal pipe per process, so the pid alone is the whole name a sender
 * needs, and it is exactly the pid __sig_try_deliver_remote()'s caller
 * (kill()) already has in hand. */
static void sig_pipe_name(pid_t pid, WCHAR *name, UNICODE_STRING *us)
{
	static const char pfx[] = "\\Device\\NamedPipe\\ntlibc-sig.";
	unsigned upid = (unsigned)pid;
	int i = 0, n;

	for (; pfx[i]; i++) name[i] = (unsigned char)pfx[i];
	for (n = 8; n > 0;) { n--; name[i++] = (unsigned char)"0123456789abcdef"[(upid >> (n * 4)) & 15]; }
	name[i] = 0;
	us->Buffer = name;
	if ((size_t)i > __US_MAX_WCHARS) {
		us->Length = us->MaximumLength = 0;
		return;
	}
	us->Length = (USHORT)(i * sizeof(WCHAR));
	us->MaximumLength = (USHORT)(us->Length + sizeof(WCHAR));
}

/* All clients of one target take this named kernel mutant around their
 * request/reply exchange.  A process-private pthread lock cannot order
 * kill() calls made by different processes, while an NT mutant is shared by
 * name and is released by the kernel if a sender dies while owning it. */
static void sig_send_lock_name(pid_t pid, WCHAR *name, UNICODE_STRING *us)
{
	static const char pfx[] = "\\BaseNamedObjects\\ntlibc-sig-send.";
	unsigned upid = (unsigned)pid;
	int i = 0, n;

	for (; pfx[i]; i++) name[i] = (unsigned char)pfx[i];
	for (n = 8; n > 0;) {
		n--;
		name[i++] = (unsigned char)"0123456789abcdef"[(upid >> (n * 4)) & 15];
	}
	name[i] = 0;
	us->Buffer = name;
	if ((size_t)i > __US_MAX_WCHARS) {
		us->Length = us->MaximumLength = 0;
		return;
	}
	us->Length = (USHORT)(i * sizeof(WCHAR));
	us->MaximumLength = (USHORT)(us->Length + sizeof(WCHAR));
}

/* The delivery thread: one per process, started by __sig_delivery_init()
 * and never joined or cancelled -- NT tears down every thread of a
 * process at exit, which is the only "shutdown" this loop ever needs.
 * `arg` is the first listening pipe, created synchronously by init before
 * application startup can report itself ready. Publishing that instance in
 * the creating thread closes the old interval in which a target had installed
 * a handler but kill() could still miss its not-yet-scheduled listener thread
 * and fall back to the signal's default action. */
static ULONG NTAPI sig_delivery_thread(PVOID arg)
{
	pid_t pid = getpid();
	WCHAR name[40];
	UNICODE_STRING us;
	__plat_handle_t pipe = (__plat_handle_t)arg;

	sig_pipe_name(pid, name, &us);

	for (;;) {
		if (!pipe) pipe = __plat_signal_pipe_create(&us);
		if (!pipe) {
			/* Nowhere to report this to -- a background service thread
			 * with no caller waiting on it. A transient failure (heap
			 * pressure, a name collision with a not-yet-torn-down
			 * previous instance) is worth a short backoff and another
			 * try rather than giving up and leaving this process deaf
			 * to cross-process signals for the rest of its life. */
			__plat_signal_backoff();
			continue;
		}

		/* Block until a client connects -- see this file's banner for
		 * why a plain read here would only ever work once. */
		if (__plat_signal_pipe_listen(pipe)) {
			struct sigpacket pkt;
			size_t got = 0;
			memset(&pkt, 0, sizeof pkt);
			if (__plat_signal_pipe_read(pipe, &pkt, sizeof pkt, &got) == 0 && got == sizeof pkt) {
				siginfo_t si;
				__plat_handle_t next;
				unsigned char accepted = pkt.magic == SIGPACKET_MAGIC &&
					pkt.signo > 0 && pkt.signo < _NSIG;

				if (accepted && (pkt.flags & SIGPACKET_NONDEFAULT_ONLY)) {
					__sig_lock();
					if (__sig_disposition_is_default((int)pkt.signo))
						accepted = 0;
					__sig_unlock();
				}

				/* Publish the next listening instance before acknowledging this
				 * request.  send_mutant keeps every other process targeting this
				 * pid outside NtOpenFile until this reply releases its owner, so
				 * there is no close/recreate interval for a sender to race. */
				next = __plat_signal_pipe_create(&us);
				if (!next) accepted = 0;

				if (accepted) {
					memset(&si, 0, sizeof si);
					si.si_signo = (int)pkt.signo;
					si.si_code = (int)pkt.code;
					si.si_pid = (pid_t)pkt.sender_pid;
					si.si_uid = (uid_t)pkt.sender_uid;
					si.si_value = pkt.value;
					/* Never deliver on this service thread: its empty TLS mask is
					 * unrelated to every POSIX application thread's mask. */
					__sig_queue_process_info((int)pkt.signo, &si);
					/* Publish the wake only after the record is visible.  A waiter
					 * which drains immediately must not observe an empty queue. */
					__sig_notify_delivery();
				}

				{
					size_t sent = 0;
					__plat_signal_pipe_write(pipe, &accepted, sizeof accepted, &sent);
				}
				__plat_close(pipe);
				pipe = next;
				continue;
			}
		}

		/* Even a malformed or disconnected client gets an overlapped
		 * replacement when possible. A legitimate sender still owns the
		 * named mutant until its read fails, so publishing first preserves
		 * the same handoff invariant as the acknowledged path above. */
		{
			__plat_handle_t next = __plat_signal_pipe_create(&us);
			__plat_close(pipe);
			pipe = next;
		}
	}
	/* Unreachable: the loop above has no break/return, so this line
	 * never runs. Present only because a NTAPI (stdcall) thread
	 * function is declared returning ULONG, and this build's gcc pass
	 * (tools/lint.sh) does not credit `for (;;)` with never falling
	 * through the way tcc, the compiler this library actually ships
	 * with, evidently does -- -Wreturn-type fired without it. */
	return 0;
}

/* __signal_init() (src/signal/signal.c) calls this once at process
 * startup, after __teb()/__peb are live (crt/crt1.c) so getpid() below
 * is real. Every failure here degrades rather than aborting startup:
 * a process that cannot get a listener still runs exactly as it did
 * before this file existed -- __sig_try_deliver_remote() below simply
 * never succeeds for callers targeting it, and kill() falls back to
 * today's default-disposition-only behaviour, same as if the target
 * were not an ntlibc process at all. */
void __sig_delivery_init(void)
{
	UNICODE_STRING lock_us, pipe_us;
	WCHAR lock_name[48], pipe_name[40];
	__plat_handle_t ev, mutant, pipe, thr;
	pid_t pid;

	/* The mutex is created independently of everything below it: every
	 * signal.c entry point calls __sig_lock()/__sig_unlock() regardless
	 * of whether this process ever gets a working pipe or delivery
	 * thread (a second real thread already exists under
	 * NTLIBC_USE_KERNEL32 -- see this file's banner -- so the mutex earns
	 * its keep even when the rest of this function gives up below). */
	ev = __plat_sigevent_create(1);
	if (ev) lock_event = ev;

	ev = __plat_sigevent_create(0);
	if (!ev) return;

	pid = getpid();
	sig_send_lock_name(pid, lock_name, &lock_us);
	mutant = __plat_signal_mutant_create(&lock_us);
	if (!mutant) { __plat_close(ev); return; }
	send_mutant = mutant;

	/* Create and publish the first server instance here, not in the new
	 * thread. NtCreateThreadEx returning says only that the thread object
	 * exists; it does not say the thread has run. A child can therefore
	 * install a handler and notify its parent while the listener is still
	 * unscheduled. The parent then sees no pipe and applies the default
	 * action despite the installed handler. Passing an already-named pipe
	 * to the thread makes init's return the explicit publication boundary.
	 * A client which connects before the thread reaches FSCTL_PIPE_LISTEN is
	 * handled as STATUS_PIPE_CONNECTED by the loop above. */
	sig_pipe_name(pid, pipe_name, &pipe_us);
	pipe = __plat_signal_pipe_create(&pipe_us);
	if (!pipe) {
		send_mutant = __PLAT_HANDLE_NULL;
		__plat_close(mutant);
		__plat_close(ev);
		return;
	}

	if (__plat_thread_start((void *)sig_delivery_thread, pipe, &thr) < 0) {
		send_mutant = __PLAT_HANDLE_NULL;
		__plat_close(pipe);
		__plat_close(mutant);
		__plat_close(ev);
		return;
	}

	/* Published only once the thread that will act on it exists and has
	 * been handed its own pid -- wake_event is the one piece of this
	 * subsystem select() reads from another thread's writes, so there is
	 * no benefit to publishing it earlier and a real (if narrow) benefit
	 * to not: a select() that observed a non-zero wake_event before
	 * sig_delivery_thread() could ever set it would just see an event
	 * that is never signalled yet, which is indistinguishable from
	 * "not running" for every purpose select() has for it. */
	wake_event = ev;
	__plat_close(thr);  /* the thread runs detached; nothing here ever waits on or terminates it */
}

/* fork.c's STATUS_PROCESS_CLONED arm calls this unconditionally, the
 * same way it calls __rusage_children_reset()/__alarm_reset_after_fork()/
 * __child_forget_stops() beside it -- none of those check "already
 * done" first, they just re-run, and neither does this.
 *
 * RtlCloneUserProcess clones only the calling thread (src/process/fork.c's
 * banner), so sig_delivery_thread() -- a *different* thread -- simply
 * does not exist in the child; wake_event, lock_event, and send_mutant are stale
 * copies of the parent's numeric handle values, naming nothing live in
 * this process (or, worse, naming whatever the kernel has since
 * recycled onto that same handle-table slot -- fork.c's own discussion
 * of this exact hazard for descriptor and child-process handles applies
 * unchanged here). They are never NtClose()'d or waited on: that would
 * touch whatever unrelated object now sits there. Just forget the
 * values and build fresh ones, keyed by this process's OWN pid --
 * getpid() reads it live off __teb()->ClientId.UniqueProcess
 * (src/unistd/getpid.c), which RtlCloneUserProcess did set correctly for
 * the child even though nothing else about this subsystem survived the
 * clone intact. */
void __sig_delivery_reinit_after_fork(void)
{
	wake_event = 0;
	lock_event = 0;
	send_mutant = 0;
	__sig_pending_reset_after_fork();
	__timer_reinit_after_fork();
	__sig_delivery_init();
}

/* kill()'s cross-process arm (src/signal/signal.c), called before that
 * function's existing default-action-only path
 * (NtTerminateProcess). Returns nonzero if the packet was handed to the
 * target's own listener -- in which case kill() returns success and
 * skips its old path entirely, because the target's delivery thread
 * will now apply that process's REAL disposition, which is strictly
 * more correct than this process's blind guess ever was. Returns 0 for
 * any failure at all -- not just STATUS_OBJECT_NAME_NOT_FOUND -- so
 * kill() can fall straight through unchanged: a target with no
 * listener (no such process, a non-ntlibc process, or an ntlibc process
 * still inside __signal_init()) is exactly the case the existing
 * default-action path exists to handle, and this function draws no
 * distinction between "no listener" and any other reason the attempt
 * did not land.
 *
 * Every packet receives a reply after its replacement listener is ready.
 * Catchable stop signals additionally set nondefault_only: the target
 * snapshots its disposition under __sig_lock(), then replies zero when the
 * sender must use the default NtSuspendProcess action. A zero reply leaves
 * the packet undelivered and tells kill() to use that fallback. */
static int sig_try_deliver_remote_info(int pid, int sig, const void *data, // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
				       int nondefault_only)
{
	const siginfo_t *si = data;
	WCHAR name[48];
	UNICODE_STRING us, lock_us;
	__plat_handle_t h, mutant;
	struct sigpacket pkt;
	unsigned char accepted = 0;
	size_t sent, got;

	sig_send_lock_name(pid, name, &lock_us);
	mutant = __plat_signal_mutant_create(&lock_us);
	if (!mutant) return 0;
	if (!__plat_wait_acquire(mutant)) { __plat_close(mutant); return 0; }

	sig_pipe_name(pid, name, &us);
	/* See this file's banner: a missing name resolves synchronously. The
	 * named mutant plus replacement-before-reply handoff guarantees that a
	 * cooperating sender never observes a missing or busy instance between
	 * two successful requests; no retry or timeout is involved. */
	h = __plat_signal_pipe_open(&us);
	if (!h) goto out;

	pkt.magic = SIGPACKET_MAGIC;
	pkt.signo = (ULONG)sig;
	pkt.flags = nondefault_only ? SIGPACKET_NONDEFAULT_ONLY : 0;
	pkt.sender_pid = (ULONG)(si ? si->si_pid : getpid());
	pkt.sender_uid = (ULONG)(si ? si->si_uid : getuid());
	pkt.code = (LONG)(si ? si->si_code : SI_USER);
	if (si) pkt.value = si->si_value;
	else memset(&pkt.value, 0, sizeof pkt.value);
	sent = 0; got = 0;
	if (__plat_signal_pipe_write(h, &pkt, sizeof pkt, &sent) == 0 && sent == sizeof pkt) {
		if (__plat_signal_pipe_read(h, &accepted, sizeof accepted, &got) != 0 || got != sizeof accepted)
			accepted = 0;
	}
	__plat_close(h);
out:
	__plat_mutant_release(mutant);
	__plat_close(mutant);
	return accepted != 0;
}

int __sig_try_deliver_remote_info(int pid, int sig, const void *data)
{
	return sig_try_deliver_remote_info(pid, sig, data, 0);
}

int __sig_try_deliver_remote(int pid, int sig)
{
	return __sig_try_deliver_remote_info(pid, sig, 0);
}

int __sig_try_deliver_remote_nondefault(int pid, int sig)
{
	return sig_try_deliver_remote_info(pid, sig, 0, 1);
}

// NOLINTEND(misc-include-cleaner)
