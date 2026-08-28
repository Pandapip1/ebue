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
 * write signal.c's shared handlers[]/act_mask[]/act_flags[]/blocked/
 * pending/alt_stack/alt_active. __sig_lock()/__sig_unlock() below are a
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
 * Recursive is not a nicety here, it is load-bearing: __raise_internal()
 * holds the lock for the FULL duration of a caught handler's call (see
 * below), and an ordinary, POSIX-sanctioned handler is free to call
 * straight back into sigprocmask()/sigaction()/raise()/sigpending()/
 * sigaltstack() -- all on the async-signal-safe list, signal.h.html --
 * from inside itself, on the SAME thread that is already holding the
 * lock. test/posix-signal.c's mask_check_handler() does exactly that
 * (sigprocmask() from inside a handler raise() invoked). A first version
 * of this was a plain, non-recursive acquire, and it hung every one of
 * those calls forever the moment it was actually exercised --
 * test/posix-signal.exe timing out under real testing is what caught
 * it, not inspection. The owner-id-plus-depth pair is what lets the
 * SAME thread walk back in without waiting on itself while a genuinely
 * DIFFERENT thread (sig_delivery_thread() below, the reason this lock
 * exists at all) still blocks for real; lock_owner/lock_depth are
 * touched only by whichever thread currently owns the lock, or is in
 * the middle of acquiring it, where a torn read of a stale owner value
 * only ever costs one spurious real wait, never a wrong grant -- actual
 * exclusion is still lock_event, an NT synchronization primitive.
 *
 * The one deliberate consequence of holding the lock across the whole
 * handler call: a handler that blocks for a while genuinely stalls the
 * OTHER thread's next signal-related call (same-thread reentrance is
 * free; cross-thread contention is not). That is a real, documented
 * tradeoff, not an accident -- it is also what keeps two handlers from
 * ever running concurrently and racing sig_dispatch()'s alt_active,
 * which phase 1 has no other way to rule out.
 *
 * __sig_lock()/__sig_unlock() are no-ops if the mutex event was never
 * created (lock_event == 0): a process whose __sig_delivery_init() ran
 * before this file existed cannot happen (there is no such build), but
 * a process on a platform where even NtCreateEvent fails is not
 * something this file should turn into a crash -- see __sig_delivery_init()
 * below for why that stays possible in principle and degrades instead
 * of failing __signal_init() outright. */
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include "libc.h"

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

static HANDLE wake_event;   /* auto-reset; set on every packet arrival. 0 = not running. */
static HANDLE lock_event;   /* auto-reset-as-mutex, initially signalled (free). 0 = no locking done. */
static HANDLE send_mutant;  /* named per target; serializes clients across processes. */

/* RECURSIVE, deliberately -- a plain acquire-per-call mutex over
 * lock_event self-deadlocks the instant any signal handler calls back
 * into signal.c, which is completely ordinary, POSIX-sanctioned code:
 * sigprocmask(), sigaction(), raise(), sigpending() and sigaltstack()
 * are all on the async-signal-safe list (signal.h.html), and
 * test/posix-signal.c's own mask_check_handler() does exactly this --
 * calls sigprocmask() from inside a handler while raise() is still on
 * the stack. __raise_internal() holds this lock for the full duration
 * of the handler call (see this file's banner: that is deliberate, not
 * incidental), so raise() -> lock -> __raise_internal() -> the handler
 * -> sigprocmask() -> lock() again, ALL ON THE SAME THREAD, is not a
 * rare race -- it is the ordinary shape of a large fraction of this
 * library's own signal test suite. A non-recursive version of this
 * hung every one of those calls forever the first time it was actually
 * run (caught by test/posix-signal.exe timing out under real testing,
 * not by inspection); tracking the owning thread and a re-entry depth
 * is what makes the same thread able to walk back in without waiting
 * on itself, while a genuinely different thread (src/signal/sigdelivery.c's
 * delivery thread, the common case this lock exists for at all) still
 * blocks for real. lock_owner/lock_depth are touched only by whichever
 * thread currently owns the lock (or is in the middle of acquiring it,
 * where a torn read of a stale owner value only ever costs a spurious
 * real wait, never a wrong grant -- the actual exclusion is still
 * lock_event, an NT synchronization primitive). */
static pid_t lock_owner;
static int lock_depth;

/* HANDLE __sig_delivery_event(void), declared in libc.h, is select()'s
 * read of wake_event -- deliberately not exposed as a variable so a
 * caller outside this file can never accidentally NtClose() or NtSetEvent()
 * it. */
HANDLE __sig_delivery_event(void) { return wake_event; }

/* State-checking wait loops in signal.c and sleep.c use the same event as
 * select(), but wait alertably so timer APCs remain deliverable. A set that
 * lands between a caller's state check and this wait is retained by the
 * auto-reset event, closing the lost-wakeup window without a polling slice. */
NTSTATUS __sig_wait_delivery(LARGE_INTEGER *timeout)
{
	if (wake_event)
		return NtWaitForSingleObject(wake_event, TRUE, timeout);
	if (timeout) return NtDelayExecution(TRUE, timeout);
	/* Event creation failure is a degraded startup path. Keep indefinite
	 * signal waits functional there, at their old latency, rather than spin. */
	{
		LARGE_INTEGER fallback = -1000000; /* 100 ms */
		return NtDelayExecution(TRUE, &fallback);
	}
}

void __sig_notify_delivery(void)
{
	if (wake_event) {
		LONG previous;
		NtSetEvent(wake_event, &previous);
	}
}

void __sig_lock(void)
{
	pid_t me;
	if (!lock_event) return;
	me = gettid();
	if (lock_depth > 0 && lock_owner == me) { lock_depth++; return; }
	NtWaitForSingleObject(lock_event, 0, 0);
	lock_owner = me;
	lock_depth = 1;
}

void __sig_unlock(void)
{
	LONG prev;
	if (!lock_event) return;
	if (--lock_depth > 0) return;
	lock_owner = 0;
	NtSetEvent(lock_event, &prev);
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
	/* Fixed 29-character prefix plus 8 hex digits, 37 WCHARs total,
	 * three orders of magnitude below the USHORT Length this narrows
	 * into -- same reasoning pipe.c's own name gives. */
	/* USHORT-safe: fixed 37-WCHAR name, see above. */
	us->Length = (USHORT)(i * sizeof(WCHAR));
	/* USHORT-safe: us->Length plus one WCHAR, same bound as above. */
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
	/* USHORT-safe: fixed prefix plus eight pid digits fits name[48]. */
	us->Length = (USHORT)(i * sizeof(WCHAR));
	/* USHORT-safe: us->Length plus one WCHAR has the same fixed bound. */
	us->MaximumLength = (USHORT)(us->Length + sizeof(WCHAR));
}

static HANDLE sig_create_pipe(UNICODE_STRING *us)
{
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK io;
	LARGE_INTEGER timeout = -1200000000LL;
	HANDLE pipe;
	NTSTATUS st;

	InitializeObjectAttributes(&oa, us, OBJ_CASE_INSENSITIVE, 0, 0);
	st = NtCreateNamedPipeFile(&pipe,
		GENERIC_READ | GENERIC_WRITE | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
		&oa, &io, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF,
		FILE_SYNCHRONOUS_IO_NONALERT, FILE_PIPE_MESSAGE_TYPE,
		FILE_PIPE_MESSAGE_MODE, FILE_PIPE_QUEUE_OPERATION, 2, 4096, 4096,
		&timeout);
	return NT_SUCCESS(st) ? pipe : 0;
}

/* The delivery thread: one per process, started by __sig_delivery_init()
 * and never joined or cancelled -- NT tears down every thread of a
 * process at exit, which is the only "shutdown" this loop ever needs.
 * `arg` is the pid this thread's pipe name was built for, captured at
 * creation time rather than re-read with getpid() on every cycle so a
 * fork()'d child's brand-new thread (see __sig_delivery_reinit_after_fork()
 * below) can never end up racing its own pid against a stale capture. */
static ULONG NTAPI sig_delivery_thread(PVOID arg)
{
	pid_t pid = (pid_t)(ULONG_PTR)arg;
	WCHAR name[40];
	UNICODE_STRING us;
	HANDLE pipe = 0;

	sig_pipe_name(pid, name, &us);

	for (;;) {
		IO_STATUS_BLOCK io;
		NTSTATUS st;

		if (!pipe) pipe = sig_create_pipe(&us);
		if (!pipe) {
			/* Nowhere to report this to -- a background service thread
			 * with no caller waiting on it. A transient failure (heap
			 * pressure, a name collision with a not-yet-torn-down
			 * previous instance) is worth a short backoff and another
			 * try rather than giving up and leaving this process deaf
			 * to cross-process signals for the rest of its life. */
			LARGE_INTEGER d = -1000000; /* 100ms, src/unistd/sleep.c's own idiom */
			NtDelayExecution(0, &d);
			continue;
		}

		/* Block until a client connects -- see this file's banner for
		 * why a plain NtReadFile here would only ever work once. */
		st = NtFsControlFile(pipe, 0, 0, 0, &io, FSCTL_PIPE_LISTEN, 0, 0, 0, 0);
		if (st == STATUS_PENDING) { NtWaitForSingleObject(pipe, 0, 0); st = io.Status; }
		/* A serialized sender can connect to the replacement instance after
		 * it is created but before this thread reaches LISTEN.  The instance
		 * is already connected in that case, which is the desired state. */
		if (st == STATUS_PIPE_CONNECTED) st = STATUS_SUCCESS;

		if (NT_SUCCESS(st)) {
			struct sigpacket pkt;
			memset(&pkt, 0, sizeof pkt);
			st = NtReadFile(pipe, 0, 0, 0, &io, &pkt, sizeof pkt, 0, 0);
			if (st == STATUS_PENDING) { NtWaitForSingleObject(pipe, 0, 0); st = io.Status; }
			if (NT_SUCCESS(st) && io.Information == sizeof pkt) {
				siginfo_t si;
				HANDLE next;
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
				next = sig_create_pipe(&us);
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

				st = NtWriteFile(pipe, 0, 0, 0, &io, &accepted,
				                 sizeof accepted, 0, 0);
				if (st == STATUS_PENDING)
					NtWaitForSingleObject(pipe, 0, 0);
				NtClose(pipe);
				pipe = next;
				continue;
			}
		}

		/* Even a malformed or disconnected client gets an overlapped
		 * replacement when possible. A legitimate sender still owns the
		 * named mutant until its read fails, so publishing first preserves
		 * the same handoff invariant as the acknowledged path above. */
		{
			HANDLE next = sig_create_pipe(&us);
			NtClose(pipe);
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
	OBJECT_ATTRIBUTES oa;
	UNICODE_STRING lock_us;
	WCHAR lock_name[48];
	HANDLE ev, mutant, thr;
	NTSTATUS st;
	pid_t pid;

	/* The mutex is created independently of everything below it: every
	 * signal.c entry point calls __sig_lock()/__sig_unlock() regardless
	 * of whether this process ever gets a working pipe or delivery
	 * thread (a second real thread already exists under
	 * NTLIBC_USE_KERNEL32 -- see this file's banner -- so the mutex earns
	 * its keep even when the rest of this function gives up below). */
	InitializeObjectAttributes(&oa, 0, 0, 0, 0);
	if (NT_SUCCESS(NtCreateEvent(&ev, EVENT_ALL_ACCESS, &oa, SynchronizationEvent, TRUE)))
		lock_event = ev;

	InitializeObjectAttributes(&oa, 0, 0, 0, 0);
	if (!NT_SUCCESS(NtCreateEvent(&ev, EVENT_ALL_ACCESS, &oa, SynchronizationEvent, FALSE)))
		return;

	pid = getpid();
	sig_send_lock_name(pid, lock_name, &lock_us);
	InitializeObjectAttributes(&oa, &lock_us,
		OBJ_CASE_INSENSITIVE | OBJ_OPENIF, 0, 0);
	st = NtCreateMutant(&mutant, MUTANT_ALL_ACCESS, &oa, FALSE);
	if (!NT_SUCCESS(st)) { NtClose(ev); return; }
	send_mutant = mutant;

	st = NtCreateThreadEx(&thr, THREAD_ALL_ACCESS, 0, NtCurrentProcess(),
	                       (PVOID)sig_delivery_thread, (PVOID)(ULONG_PTR)pid,
	                       0, 0, 0, 0, 0);
	if (!NT_SUCCESS(st)) {
		send_mutant = 0;
		NtClose(mutant);
		NtClose(ev);
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
	NtClose(thr);  /* the thread runs detached; nothing here ever waits on or terminates it */
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
static int sig_try_deliver_remote_info(int pid, int sig, const void *data,
				       int nondefault_only)
{
	const siginfo_t *si = data;
	WCHAR name[48];
	UNICODE_STRING us, lock_us;
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK io;
	HANDLE h, mutant;
	NTSTATUS st;
	struct sigpacket pkt;
	unsigned char accepted = 0;

	sig_send_lock_name(pid, name, &lock_us);
	InitializeObjectAttributes(&oa, &lock_us,
		OBJ_CASE_INSENSITIVE | OBJ_OPENIF, 0, 0);
	st = NtCreateMutant(&mutant, MUTANT_ALL_ACCESS, &oa, FALSE);
	if (!NT_SUCCESS(st)) return 0;
	st = NtWaitForSingleObject(mutant, 0, 0);
	if (!NT_SUCCESS(st)) { NtClose(mutant); return 0; }

	sig_pipe_name(pid, name, &us);
	InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, 0, 0);
	/* See this file's banner: a missing name resolves synchronously. The
	 * named mutant plus replacement-before-reply handoff guarantees that a
	 * cooperating sender never observes a missing or busy instance between
	 * two successful requests; no retry or timeout is involved. */
	st = NtOpenFile(&h, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &io,
	                FILE_SHARE_READ | FILE_SHARE_WRITE,
	                FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
	if (!NT_SUCCESS(st)) goto out;

	pkt.magic = SIGPACKET_MAGIC;
	pkt.signo = (ULONG)sig;
	pkt.flags = nondefault_only ? SIGPACKET_NONDEFAULT_ONLY : 0;
	pkt.sender_pid = (ULONG)(si ? si->si_pid : getpid());
	pkt.sender_uid = (ULONG)(si ? si->si_uid : getuid());
	pkt.code = (LONG)(si ? si->si_code : SI_USER);
	if (si) pkt.value = si->si_value;
	else memset(&pkt.value, 0, sizeof pkt.value);
	st = NtWriteFile(h, 0, 0, 0, &io, &pkt, sizeof pkt, 0, 0);
	if (st == STATUS_PENDING) { NtWaitForSingleObject(h, 0, 0); st = io.Status; }
	if (NT_SUCCESS(st) && io.Information == sizeof pkt) {
		st = NtReadFile(h, 0, 0, 0, &io, &accepted, sizeof accepted, 0, 0);
		if (st == STATUS_PENDING) { NtWaitForSingleObject(h, 0, 0); st = io.Status; }
		if (!NT_SUCCESS(st) || io.Information != sizeof accepted)
			accepted = 0;
	}
	NtClose(h);
out:
	NtReleaseMutant(mutant, 0);
	NtClose(mutant);
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
