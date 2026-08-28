/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Signals, as far as they can be had.
 *
 * What a single program generates for itself -- raise(), abort(), a
 * write to a broken pipe, and hardware faults (SIGSEGV, SIGFPE, SIGILL,
 * SIGBUS), which arrive as NT exceptions and are turned into signals by
 * a vectored exception handler installed at startup -- has always been
 * synchronous, and still is: every one of those still calls
 * __raise_internal() directly, on the thread that generated the signal,
 * exactly as before.
 *
 * kill() to ANOTHER process is different, in one specific and bounded
 * way, as of src/signal/sigdelivery.c: it first tries to hand the target
 * process's own listener a small packet naming the signal, so that
 * process's own delivery thread can drive its own real disposition
 * (sa_handler/SIG_IGN, not just the default action) through
 * __raise_internal() -- see that file's banner for the whole mechanism,
 * including the fast-fail guarantee for a target with no listener. What
 * this does NOT do is interrupt the target thread wherever it happens
 * to be: nothing here touches another thread's register state
 * (thread-context hijacking -- SuspendThread/GetThreadContext/
 * SetThreadContext plus an instruction-pointer rewrite, the way a real
 * kernel or Cygwin's exceptions.cc does it -- is out of scope; see
 * sigdelivery.c's banner for why). A signal delivered this way is only
 * guaranteed to be acted on the next time the target thread reaches a
 * point that checks for one: sig_delivery_thread() calling
 * __raise_internal() itself (immediately, on its own thread, for a
 * signal that is not blocked), or src/select/select.c's poll loop,
 * which is taught to notice one early because it already polls. Code
 * running ordinary instructions between syscalls on the application
 * thread does not get interrupted out of them by this.
 *
 * Ctrl-C and Ctrl-Break are a separate story: csrss delivers those
 * through kernel32's console control mechanism, not as NT exceptions,
 * and there is no ntdll path to it at all (see CONTRIBUTING.md).  With
 * NTLIBC_USE_KERNEL32, __signal_init() registers a handler with
 * SetConsoleCtrlHandler() that turns CTRL_C_EVENT/CTRL_BREAK_EVENT into
 * SIGINT via __raise_internal(), same as the vectored handler does for
 * DBG_CONTROL_C/DBG_CONTROL_BREAK.  Without it (the default build),
 * Ctrl-C is never turned into a signal; the default console behaviour,
 * which ends the process, stays in effect.
 */
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include "libc.h"
#ifdef NTLIBC_USE_KERNEL32
#include "kernel32.h"
#endif

static void (*handlers[_NSIG])(int);
static __thread sigset_t blocked;

/* Standard signals coalesce while pending. Real-time signals retain one
 * record per generation, in FIFO order within a signal number.  Selection
 * scans signal numbers first, which gives POSIX's lowest-real-time-signal
 * priority without disturbing that FIFO. */
struct pending_state {
	sigset_t set;
	siginfo_t info[SIGQUEUE_MAX];
	int count;
};
static struct pending_state process_pending;
static __thread struct pending_state thread_pending;
/* raise() is thread-directed.  Other entries into __raise_internal_info()
 * are process-directed and retain the shared pending queue used by the
 * cross-process delivery thread. */
static __thread int thread_directed;
static sigset_t waiting_set;
static int wait_active;

/* Per-signal sa_mask/sa_flags, as installed by sigaction().  signal()
 * and sigset() leave these at their zero-initialized defaults (empty
 * mask, no flags), matching their simpler contract. */
static sigset_t act_mask[_NSIG];
static int act_flags[_NSIG];

static int sig_valid(int sig) { return sig > 0 && sig < _NSIG; }

void __sig_current_mask_copy(sigset_t *mask)
{
	*mask = blocked;
}

void __sig_current_mask_install(const sigset_t *mask)
{
	blocked = *mask;
	memset(&thread_pending, 0, sizeof thread_pending);
}

/* Set only by exception_handler(), immediately around its call into
 * __raise_internal(), so a SIGSEGV/SIGBUS/SIGILL/SIGFPE handler
 * installed with SA_SIGINFO can be told this delivery came from a
 * hardware fault rather than kill()/raise().  Guarded by
 * __sig_lock()/__sig_unlock() at exception_handler()'s call site: the
 * delivery thread can otherwise be inside its own signal delivery while
 * these values are being changed. */
static int fault_active;
static void *fault_addr;
static int fault_si_code;   /* computed by exception_handler(), see there */

static void make_siginfo(siginfo_t *si, int sig)
{
	memset(si, 0, sizeof *si);
	si->si_signo = sig;
	if (fault_active) {
		si->si_code = fault_si_code;
		si->si_addr = fault_addr;
	} else {
		si->si_code = SI_USER;
		si->si_pid = getpid();
		si->si_uid = getuid();
	}
}

static int queue_pending_to(struct pending_state *state, int sig,
			    const siginfo_t *si)
{
	int realtime = sig >= SIGRTMIN && sig <= SIGRTMAX;
	if (!realtime && sigismember(&state->set, sig)) return 0;
	if (state->count >= SIGQUEUE_MAX) { errno = EAGAIN; return -1; }
	state->info[state->count++] = *si;
	sigaddset(&state->set, sig);
	return 0;
}

static int queue_pending(int sig, const siginfo_t *si)
{
	return queue_pending_to(thread_directed ? &thread_pending : &process_pending,
	                        sig, si);
}

static int take_pending_signal_from(struct pending_state *state, int sig,
				    siginfo_t *si)
{
	int i;
	for (i = 0; i < state->count; i++) {
		if (state->info[i].si_signo != sig) continue;
		if (si) *si = state->info[i];
		state->count--;
		memmove(&state->info[i], &state->info[i + 1],
		        (size_t)(state->count - i) * sizeof state->info[0]);
		for (i = 0; i < state->count; i++)
			if (state->info[i].si_signo == sig) return 1;
		sigdelset(&state->set, sig);
		return 1;
	}
	/* Accommodate pending state copied from an older process image or
	 * produced before the queue existed: it has no payload, but it is
	 * still a real pending signal and must remain consumable. */
	if (sigismember(&state->set, sig)) {
		if (si) make_siginfo(si, sig);
		sigdelset(&state->set, sig);
		return 1;
	}
	return 0;
}

static int pending_member(int sig)
{
	return sigismember(&thread_pending.set, sig) ||
	       sigismember(&process_pending.set, sig);
}

static int take_pending_signal(int sig, siginfo_t *si)
{
	if (sigismember(&thread_pending.set, sig))
		return take_pending_signal_from(&thread_pending, sig, si);
	return take_pending_signal_from(&process_pending, sig, si);
}

static int take_pending_from_set(const sigset_t *set, siginfo_t *si)
{
	int sig;
	for (sig = 1; sig < _NSIG; sig++)
		if (sigismember(set, sig) && pending_member(sig)) {
			take_pending_signal(sig, si);
			return sig;
		}
	return 0;
}

/* How many times a signal-catching function has been entered.  Read
 * through __sig_caught_count() by src/unistd/sleep.c, whose alertable
 * waits have to tell "a handler ran, so the sleep ends with [EINTR]"
 * from "the signal was ignored, so the interval keeps running":
 * sleep.html and nanosleep.html both end a wait only for a signal
 * "whose action is to invoke a signal-catching function or to terminate
 * the process", and __raise_internal() below answers 0 for the handled
 * and the ignored case alike.  A counter rather than a flag so a caller
 * can compare against a value taken before the wait and needs nothing
 * cleared afterwards.  The process counter preserves polling for the
 * background delivery thread; the thread counter keeps one thread's handler
 * from spuriously interrupting another thread's semaphore wait. */
static unsigned long caught_count;
static __thread unsigned long thread_caught_count;
static __thread unsigned long thread_restart_count;

unsigned long __sig_caught_count(void) { return caught_count; }
unsigned long __sig_thread_caught_count(void) { return thread_caught_count; }
unsigned long __sig_thread_restart_count(void) { return thread_restart_count; }

static int default_action(int sig);
static int sig_stops(int sig);

/* sigaction.html, SA_NOCLDWAIT: "If ... set for SIGCHLD ... and the
 * calling process subsequently forks, ... the behavior is unspecified
 * if ... the process either simultaneously has SA_NOCLDWAIT set or has
 * SIGCHLD set to SIG_IGN" -- the useful clause is over in wait.html
 * ERRORS instead: "the calling process has SA_NOCLDWAIT set ... [ECHILD]
 * ... status information is not retained".  A child born while this is
 * set must not become something a later wait()/waitpid() can find, so
 * src/process/children.c's __child_add() consults this before adding an
 * entry at all -- the same "leave it untracked, let it run" degrade
 * fork.c and spawn.c already use when the table itself cannot grow. */
int __sigchld_nocldwait(void) { return (act_flags[SIGCHLD] & SA_NOCLDWAIT) != 0; }

/* Called by sig_delivery_thread() with the signal lock held.  Stop-shaped
 * signals need this distinction before kill() chooses between running the
 * target's caught/ignored disposition and applying the default NT process
 * suspension itself. */
int __sig_disposition_is_default(int sig)
{
	return !sig_valid(sig) || handlers[sig] == SIG_DFL;
}

void (*signal(int sig, void (*h)(int)))(int)
{
	void (*old)(int);
	if (!sig_valid(sig) || sig == SIGKILL || sig == SIGSTOP) { errno = EINVAL; return SIG_ERR; }
	__sig_lock();
	old = handlers[sig];
	handlers[sig] = h;
	if (h == SIG_IGN || (h == SIG_DFL && !default_action(sig))) {
		sigdelset(&process_pending.set, sig);
		sigdelset(&thread_pending.set, sig);
	}
	__sig_unlock();
	return old;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *old)
{
	if (!sig_valid(sig) || sig == SIGKILL || sig == SIGSTOP) { errno = EINVAL; return -1; }
	/* Locked for the whole read-then-write: sig_delivery_thread()
	 * (src/signal/sigdelivery.c) reads handlers[]/act_mask[]/act_flags[]
	 * inside __raise_internal() on its own thread, and a caller reading
	 * `old` back here is entitled to a consistent snapshot rather than
	 * one torn between an in-flight update and a concurrent delivery. */
	__sig_lock();
	if (old) {
		memset(old, 0, sizeof *old);
		old->sa_handler = handlers[sig];
		old->sa_mask = act_mask[sig];
		old->sa_flags = act_flags[sig];
	}
	if (act) {
		handlers[sig] = act->sa_handler;
		if (act->sa_handler == SIG_IGN ||
		    (act->sa_handler == SIG_DFL && !default_action(sig))) {
			sigdelset(&process_pending.set, sig);
			sigdelset(&thread_pending.set, sig);
		}
		act_mask[sig] = act->sa_mask;
		/* SA_RESTART: meaningful for exactly one caller now,
		 * src/select/select.c's select()/pselect() -- see that file's
		 * banner for the choice it makes with the flag once a signal
		 * really can interrupt it. Every OTHER blocking point in this
		 * library still cannot be interrupted mid-syscall (no
		 * NtCancelSynchronousIoFile -- see src/signal/sigdelivery.c's
		 * banner for why not), so for everything but select()/pselect()
		 * this remains what it always was: accepted and remembered
		 * (round-trips through sigaction(..., &old)) but otherwise a
		 * no-op.
		 *
		 * SA_ONSTACK: implemented. sigaltstack() below keeps the
		 * registered stack and sig_dispatch() switches to it around
		 * the handler call. Synchronous delivery is what makes that
		 * cheap: a kernel has to build a signal frame on the alternate
		 * stack and return through sigreturn, whereas this library owns
		 * the call site, so an alternate stack is a stack switch and
		 * nothing more (src/signal/$ARCH/altstack.S).
		 *
		 * SA_NOCLDSTOP suppresses the SIGCHLD notification generated when
		 * kill() stops or continues a tracked child. SA_NOCLDWAIT is
		 * consumed by __child_add(); SA_RESTORER remains stored but has
		 * no NT role. SA_NODEFER, SA_RESETHAND and SA_SIGINFO are likewise
		 * meaningful under synchronous, in-process delivery and are
		 * genuinely implemented. */
		act_flags[sig] = act->sa_flags;
	}
	__sig_unlock();
	return 0;
}

static int default_action(int sig)
{
	switch (sig) {
	case SIGCHLD: case SIGURG: case SIGWINCH: case SIGCONT:
		return 0;   /* ignore */
	default:
		return 1;   /* terminate */
	}
}

/* A process that stops itself cannot update its parent's private child
 * table.  Publish the transition through an auto-reset named event before
 * entering NtSuspendProcess; waitpid() consumes that one notification and
 * records the ordinary __W_STOPPED status in its own table. */
static HANDLE self_stop_event;
static pid_t self_stop_owner;
static int self_stop_signal;

static void stop_event_name(pid_t pid, int sig, WCHAR name[56],
			    UNICODE_STRING *us)
{
	static const char prefix[] = "\\BaseNamedObjects\\ntlibc-stop.";
	unsigned upid = (unsigned)pid;
	unsigned usig = (unsigned)sig;
	int i = 0, n;

	for (; prefix[i]; i++) name[i] = (unsigned char)prefix[i];
	for (n = 8; n > 0;) {
		n--;
		name[i++] = (unsigned char)"0123456789abcdef"[(upid >> (n * 4)) & 15];
	}
	name[i++] = '.';
	name[i++] = (unsigned char)"0123456789abcdef"[(usig >> 4) & 15];
	name[i++] = (unsigned char)"0123456789abcdef"[usig & 15];
	name[i] = 0;
	us->Buffer = name;
	/* USHORT-safe: the fixed-format name fits in the 56-WCHAR buffer. */
	us->Length = (USHORT)(i * sizeof(WCHAR));
	/* USHORT-safe: us->Length plus one WCHAR has the same fixed bound. */
	us->MaximumLength = (USHORT)(us->Length + sizeof(WCHAR));
}

static int stop_self(int sig)
{
	OBJECT_ATTRIBUTES oa;
	UNICODE_STRING us;
	WCHAR name[56];
	NTSTATUS st;
	LONG previous;
	LARGE_INTEGER zero = 0;
	pid_t pid = getpid();

	/* RtlCloneUserProcess copied only the numeric value of a parent's
	 * private handle.  A different pid makes that copy stale, just as for
	 * the process-group publication event in src/unistd/ids.c. */
	if (self_stop_owner != pid) self_stop_event = 0;
	if (!self_stop_event || self_stop_signal != sig) {
		if (self_stop_event) NtClose(self_stop_event);
		stop_event_name(pid, sig, name, &us);
		InitializeObjectAttributes(&oa, &us,
		                           OBJ_CASE_INSENSITIVE | OBJ_OPENIF, 0, 0);
		st = NtCreateEvent(&self_stop_event, EVENT_ALL_ACCESS, &oa,
		                   SynchronizationEvent, FALSE);
		if (!NT_SUCCESS(st)) return __set_errno_status(st);
		self_stop_owner = pid;
		self_stop_signal = sig;
	}
	st = NtSetEvent(self_stop_event, &previous);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	st = NtSuspendProcess(NtCurrentProcess());
	if (!NT_SUCCESS(st)) {
		/* Retract a notification for a stop that did not happen. */
		NtWaitForSingleObject(self_stop_event, 0, &zero);
		return __set_errno_status(st);
	}
	/* Reached only after another process sends SIGCONT. */
	return 0;
}

int __sig_consume_child_stop(pid_t pid)
{
	static const int stops[] = { SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU };
	OBJECT_ATTRIBUTES oa;
	UNICODE_STRING us;
	WCHAR name[56];
	LARGE_INTEGER zero = 0;
	HANDLE h;
	NTSTATUS st;
	size_t i;

	for (i = 0; i < sizeof stops / sizeof stops[0]; i++) {
		stop_event_name(pid, stops[i], name, &us);
		InitializeObjectAttributes(&oa, &us,
		                           OBJ_CASE_INSENSITIVE | OBJ_OPENIF, 0, 0);
		st = NtCreateEvent(&h, EVENT_ALL_ACCESS, &oa,
		                   SynchronizationEvent, FALSE);
		if (!NT_SUCCESS(st)) continue;
		if (st == STATUS_OBJECT_NAME_EXISTS &&
		    NtWaitForSingleObject(h, 0, &zero) == STATUS_SUCCESS) {
			NtClose(h);
			return stops[i];
		}
		NtClose(h);
	}
	return 0;
}

/* Deliver a signal to this process now.  Returns 0 if it was handled or
 * ignored and control may continue; does not return if the default
 * action is to die. */
static stack_t alt_stack;   /* ss_sp == 0 means none is installed */
static int alt_active;      /* nonzero while a handler runs on it */

/* Defined in src/signal/$ARCH/altstack.S -- PE builds only.
 *
 * tools/asan-build.sh and tools/fuzz.sh compile the C sources under
 * src/ natively with clang and link no .S at all, so the symbol simply
 * does not exist there.
 * Nor could those files be reused if it did: the x86_64 one takes its
 * arguments in rcx/rdx/r8 per the Windows x64 ABI, which is not where a
 * SysV ELF caller puts them. */
#ifdef _WIN32
void __sig_call_on_altstack(void *sp, void (*fn)(void *), void *arg);
#endif

/* One shape for both handler signatures, so the stack switch below has a
 * single void(*)(void *) to call whichever kind of handler is installed. */
struct sig_delivery {
	void (*h)(int);
	void (*hsi)(int, siginfo_t *, void *);
	siginfo_t *si;
	int sig;
};

static void sig_deliver(void *p)
{
	struct sig_delivery *d = p;
	if (d->hsi) d->hsi(d->sig, d->si, NULL);
	else d->h(d->sig);
}

/* Run one delivery, on the alternate stack where the disposition asked
 * for it and one is installed.
 *
 * The !alt_active term is not belt-and-braces: a signal raised from
 * inside a handler that is already running on the alternate stack must
 * keep using the stack it is on, or the nested delivery would reset the
 * stack pointer to the top and write over its own caller's frames.
 * sigaltstack.html says the alternate stack is in use "until the handler
 * returns", and SS_ONSTACK is what a handler reads to detect this. */
static void sig_dispatch(struct sig_delivery *d, int flags)
{
#ifdef _WIN32
	if ((flags & SA_ONSTACK) && alt_stack.ss_sp && !alt_active) {
		void *top = (char *)alt_stack.ss_sp + alt_stack.ss_size;
		alt_active = 1;
		__sig_call_on_altstack(top, sig_deliver, d);
		alt_active = 0;
	} else {
		sig_deliver(d);
	}
#else
	/* Native (AddressSanitizer / libFuzzer) build: deliver on the
	 * current stack.
	 *
	 * Not merely because the asm above is absent. A raw stack switch is
	 * actively wrong under ASan, which tracks frames on the stack it
	 * knows about and offers __sanitizer_start_switch_fiber() precisely
	 * so a program that changes stacks can tell it; switching without
	 * that produces false reports about the stack it lost track of. The
	 * subject of this build is the memory safety of OS-independent code,
	 * not signal delivery.
	 *
	 * alt_active is deliberately NOT set here. Setting it would make
	 * sigaltstack() report SS_ONSTACK -- "a handler is running on the
	 * alternate stack" -- during a delivery that is doing no such thing,
	 * which is the exact lie this whole change removed. A native build
	 * reports honestly that it is not on the alternate stack; SA_ONSTACK
	 * is covered by `make check` under Wine and on real Windows, where
	 * the switch is real. */
	(void)flags;
	sig_deliver(d);
#endif
}

int __raise_internal_info(int sig, const void *data)
{
	void (*h)(int);
	siginfo_t generated;
	const siginfo_t *supplied = data;
	if (!sig_valid(sig)) { errno = EINVAL; return -1; }
	if (!supplied) {
		make_siginfo(&generated, sig);
		supplied = &generated;
	}
	if (sigismember(&blocked, sig) ||
	    (wait_active && sigismember(&waiting_set, sig)))
		return queue_pending(sig, supplied);
	h = handlers[sig];
	if (h == SIG_IGN) return 0;
	if (h == SIG_DFL) {
		if (sig_stops(sig)) return stop_self(sig);
		if (!default_action(sig)) return 0;
		/* SIGABRT ONLY, and the asymmetry is the whole point.
		 *
		 * XSH 2.4.3 Signal Actions: "If the default action is to
		 * terminate the process abnormally, the process is terminated
		 * as if by a call to _exit(), except that the status made
		 * available to wait(), waitid(), and waitpid() indicates
		 * abnormal termination by the signal."  And _exit() itself,
		 * DESCRIPTION: "Open streams shall NOT be flushed.  Whether
		 * open streams are closed (without flushing) is
		 * implementation-defined" -- with its RATIONALE confirming the
		 * scope: those consequences "occur regardless of whether the
		 * process called _exit() ... or instead was terminated due to a
		 * signal."  So for a default-terminate signal, flushing here is
		 * not merely unnecessary, it is forbidden.
		 *
		 * abort() is the exception the standard writes out.
		 * abort.html DESCRIPTION: "The abnormal termination processing
		 * shall include the default actions defined for SIGABRT and MAY
		 * INCLUDE an attempt to effect fclose() on all open streams."
		 * (Its RATIONALE records the softening from "shall include the
		 * effect of fclose()" for async-signal-safety.)  A *may*, so
		 * flushing on SIGABRT is a permitted choice rather than a
		 * requirement -- and it is the useful one: src/exit/abort.c
		 * reaches this path, and a program that dies on a failed
		 * assertion having silently dropped its diagnostics is worse to
		 * debug for no conformance gain.  Pinned by
		 * test_abort_flushes_stdio() rather than left to be rediscovered.
		 *
		 * This used to flush unconditionally, with no recorded reason at
		 * either call site and no ledger row.  That is also how it came
		 * to be the second half of a SIGPIPE recursion (src/stdio/file.c
		 * has the measurement); the re-entrancy guard there stays
		 * regardless of this, since the failure mode was a stack
		 * overflow that reported success. */
		if (sig == SIGABRT) __stdio_exit();
		__nt_exit(__NT_SIGNAL_EXIT(sig));
	}
	/* else: h is a real handler.  Written as an else rather than a
	 * fallthrough because __nt_exit is _Noreturn (libc.h) but cppcheck
	 * does not track that, and so reads the fallthrough as h(sig) being
	 * reachable with h == SIG_DFL, which is NULL.
	 *
	 * BSD semantics: the disposition stays installed across delivery,
	 * unless the caller asked for SA_RESETHAND -- unlike plain System V,
	 * which would always restore SIG_DFL before calling the handler. */
	else {
		sigset_t saved = blocked;
		int flags = act_flags[sig];
		struct sig_delivery d;
		int i;

		/* Zeroed so sig_deliver() can tell the two handler shapes
		 * apart by which pointer is set; it lives on the ordinary
		 * stack and is read from the alternate one, which is fine --
		 * switching stacks does not unmap the old one. */
		memset(&d, 0, sizeof d);
		d.sig = sig;

		/* sigaction.html DESCRIPTION, SA_RESETHAND: "the disposition of
		 * the signal shall be reset to SIG_DFL ... on entry to the
		 * signal handler." Reset before calling h(), same as the real
		 * clause requires, and drop the recorded mask/flags with it --
		 * they belong to the sigaction() call that installed this
		 * handler, and a plain SIG_DFL has neither. */
		if (flags & SA_RESETHAND) {
			handlers[sig] = SIG_DFL;
			sigemptyset(&act_mask[sig]);
			act_flags[sig] = 0;
		}

		/* sigaction.html DESCRIPTION: "the signal being delivered ...
		 * shall be added to [the thread's signal mask] unless
		 * SA_NODEFER ... was specified", along with every signal in
		 * sa_mask, for the duration of the handler. This can only ever
		 * matter for a signal raised from *within* the handler -- see
		 * this file's header comment -- but that is exactly the case
		 * POSIX describes, since delivery here is always synchronous. */
		/* sigorset() would say this in one call, but its prototype is
		 * only visible under _BSD_SOURCE/_GNU_SOURCE (signal.h), which
		 * this file does not define -- so fold sa_mask in a signal at a
		 * time with sigaddset()/sigismember(), which are always
		 * declared. */
		for (i = 1; i < _NSIG; i++)
			if (sigismember(&act_mask[sig], i)) sigaddset(&blocked, i);
		if (!(flags & SA_NODEFER)) sigaddset(&blocked, sig);
		sigdelset(&blocked, SIGKILL);
		sigdelset(&blocked, SIGSTOP);

		/* Counted before the call, not after: a handler that never
		 * returns (longjmp out, _exit) still ran, and a sleep it
		 * interrupted still has to see that it did. */
		caught_count++;
		thread_caught_count++;
		if (flags & SA_RESTART) thread_restart_count++;

		/* sigaction.html DESCRIPTION: "If SA_SIGINFO is set ...
		 * sa_sigaction ... specif[ies] a signal-catching function" that
		 * takes (int, siginfo_t *, void *) instead of (int). sigaction()
		 * stores act->sa_handler into handlers[sig] (above), which reads
		 * the same bits as act->sa_sigaction -- both are members of the
		 * same union slot in struct sigaction (include/signal.h) -- so
		 * h already holds the right function pointer; it is only cast
		 * back to its real, three-argument type here. */
		if (flags & SA_SIGINFO) {
			void (*hsi)(int, siginfo_t *, void *) =
				(void (*)(int, siginfo_t *, void *))(void *)h;
			d.hsi = hsi;
			d.si = (siginfo_t *)supplied;
			sig_dispatch(&d, flags);
		} else {
			d.h = h;
			sig_dispatch(&d, flags);
		}

		blocked = saved;
		/* raise() and pthread_kill() are thread-directed.  If the handler
		 * deferred one of those signals, restoring its entry mask is the
		 * point at which the newly-unblocked pending signal is delivered. */
		for (i = 1; i < _NSIG; i++) {
			while (sigismember(&thread_pending.set, i) &&
			       !sigismember(&blocked, i)) {
				siginfo_t info;
				take_pending_signal_from(&thread_pending, i, &info);
				thread_directed++;
				__raise_internal_info(i, &info);
				thread_directed--;
			}
		}
	}
	return 0;
}

int __raise_internal(int sig) { return __raise_internal_info(sig, 0); }

/* Queue a process-directed signal without consulting this helper thread's
 * signal mask.  In particular, the cross-process listener is an internal NT
 * service thread whose empty TLS mask must not make a signal eligible; an
 * application thread drains this record against its own mask at a safe point. */
int __sig_queue_process_info(int sig, const void *data)
{
	siginfo_t generated;
	const siginfo_t *supplied = data;
	int result;

	if (!sig_valid(sig)) { errno = EINVAL; return -1; }
	if (!supplied) {
		make_siginfo(&generated, sig);
		supplied = &generated;
	}
	__sig_lock();
	result = queue_pending_to(&process_pending, sig, supplied);
	__sig_unlock();
	return result;
}

int __raise_thread_internal(int sig)
{
	int pending_sig;
	int result;

	/* A process-directed signal generated by a thread which blocks it is
	 * left process-pending.  A later thread-directed delivery is a safe
	 * point on the target thread: claim every process-pending signal that
	 * this thread can accept before delivering the requested one. */
	for (pending_sig = 1; pending_sig < _NSIG; pending_sig++) {
		while (sigismember(&process_pending.set, pending_sig) &&
		       !sigismember(&blocked, pending_sig)) {
			siginfo_t info;
			take_pending_signal_from(&process_pending, pending_sig, &info);
			__raise_internal_info(pending_sig, &info);
		}
	}
	thread_directed++;
	result = __raise_internal(sig);
	thread_directed--;
	return result;
}

int raise(int sig)
{
	int r;
	__sig_lock();
	r = __raise_thread_internal(sig);
	__sig_unlock();
	return r < 0 ? -1 : 0;
}

/* The signals whose default action is "stop the process" -- action S in
 * signal.h.html's Default Action column: SIGSTOP, and the three
 * terminal-related stops SIGTSTP, SIGTTIN and SIGTTOU.
 *
 * All four stop by default, not just the uncatchable SIGSTOP.  The other
 * three first go through the target-disposition handshake below because
 * their caught and ignored dispositions override that default. */
static int sig_stops(int sig)
{
	return sig == SIGSTOP || sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU;
}

/* kill.html's stop and continue signals, for a child of this process.
 *
 * NT has no job control and no signal delivery, but it does have the
 * thing job control is actually made of: NtSuspendProcess and
 * NtResumeProcess (src/internal/nt.h) suspend and resume every thread
 * of a target process.  What is missing is only the *notification* --
 * an NT process object becomes signalled once, on termination, so
 * nothing tells a waiter that a stop happened.  Nothing has to: the
 * stop is the one this library just performed, so it is recorded in the
 * child table on the spot and waitpid(WUNTRACED)/waitid(WSTOPPED) read
 * it back from there (src/process/wait.c).  A child suspended by
 * something outside this library -- a debugger, another process calling
 * NtSuspendProcess -- is still invisible, and always will be; it is
 * also not what kill()/wait() describe.
 *
 * `c` is the child-table entry, or 0 for a process that is not a child
 * of ours.  A non-child is suspended or resumed just the same -- the
 * NT call does not care -- but nothing is recorded, because there is no
 * entry to record it in and no wait() that could ever report it.
 *
 * The two guards keep the kernel's suspend count and this library's
 * one-bit view of it in step.  NT's count is a counter: two suspends
 * need two resumes.  POSIX's is not -- a second SIGSTOP to a stopped
 * process changes nothing, and a SIGCONT continues it once and for all
 * -- so a repeated SIGSTOP must not deepen the suspension into one a
 * single SIGCONT can no longer undo.  Returns 1 for a real transition,
 * 0 for an already-satisfied state, and -1 for an NT failure. */
static int sig_job_control(struct __child *c, HANDLE h, int sig)
{
	NTSTATUS st;

	if (sig == SIGCONT) {
		/* kill.html: SIGCONT continues a stopped process.  Sent to one
		 * that is already running it does nothing -- and in particular
		 * produces no WCONTINUED status, which is reserved for a child
		 * that actually was continued. */
		if (c && !c->stopsig) return 0;
		st = NtResumeProcess(h);
		if (!NT_SUCCESS(st)) return __set_errno_status(st);
		if (c) { c->stopsig = 0; c->jobstat = __W_CONTINUED; }
		return 1;
	}
	if (c && c->stopsig) return 0;
	st = NtSuspendProcess(h);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	if (c) { c->stopsig = sig; c->jobstat = __W_STOPPED(sig); }
	return 1;
}

/* sigaction.html: unless SA_NOCLDSTOP is set, stopping a child or
 * continuing a stopped child generates SIGCHLD in the parent, with the
 * transition described by siginfo_t.  NT does not announce suspension
 * changes, but kill() is the actor performing this one and therefore has
 * the complete child identity and transition at the point it succeeds. */
void __sigchld_job_control(struct __child *c, int sig)
{
	siginfo_t si;

	if (!c) return;
	__sig_lock();
	if (!(act_flags[SIGCHLD] & SA_NOCLDSTOP)) {
		memset(&si, 0, sizeof si);
		si.si_signo = SIGCHLD;
		si.si_code = sig == SIGCONT ? CLD_CONTINUED : CLD_STOPPED;
		si.si_pid = c->pid;
		si.si_uid = getuid();
		si.si_status = sig;
		__raise_internal_info(SIGCHLD, &si);
	}
	__sig_unlock();
}

int kill(pid_t pid, int sig)
{
	struct __child *c;
	HANDLE h;
	NTSTATUS st;
	ACCESS_MASK want;
	OBJECT_ATTRIBUTES oa;
	CLIENT_ID cid;

	/* kill.html ERRORS: "[EINVAL] The value of the sig argument is an
	 * invalid or unsupported signal number." sig==0 is exempted --
	 * kill.html's own DESCRIPTION carves it out as "no signal is sent"
	 * but the existence/permission checks below it still apply, and
	 * sig_valid() answers false for 0 (it means "no signal", not "a
	 * signal"), which would otherwise reject the one call kill(pid, 0)
	 * exists to make. This used to validate sig only inside
	 * __raise_internal() -- and the cross-process arm below
	 * (src/signal/sigdelivery.c's __sig_try_deliver_remote()) only ever
	 * reaches that check inside the TARGET process, on a thread this
	 * one has no way to read an error back from -- so kill()/killpg() to
	 * another pid with a bogus sig reached NtTerminateProcess()/
	 * NtSuspendProcess() (or, now, a delivered-but-silently-dropped
	 * packet) instead of ever being rejected here. */
	if (sig != 0 && !sig_valid(sig)) { errno = EINVAL; return -1; }

	/* kill.html DESCRIPTION: pid == 0 reaches "all processes ... whose
	 * process group ID is equal to the process group ID of the
	 * sender", and pid == -1 reaches "all processes ... for which the
	 * process has permission to send that signal" -- both defined in
	 * terms of a set of processes ntlibc has no way to enumerate (see
	 * src/unistd/ids.c: every process is its own group of one here, and
	 * this library tracks no process list beyond its own children).
	 * What both sets provably contain, on any POSIX system, is the
	 * caller itself: a process always has permission to signal itself,
	 * and is always a member of its own process group. Under the
	 * group-of-one model that is also *all* either set can ever
	 * contain, so "send to every process in {caller}" is not a
	 * degenerate stand-in for the real thing -- it is the real thing,
	 * fully enumerated. -1 is folded in here, alongside the existing
	 * 0 case, rather than falling into the general "pid < 0 -> process
	 * group, ESRCH" catch-all below.
	 *
	 * killpg(getpgrp(), sig) is kill(getpgrp(), sig) (killpg() is
	 * exactly that, below), and under the same group-of-one model
	 * getpgrp() names a set that provably contains only the caller --
	 * so it belongs in this same fast path.  It is not folded into the
	 * pid==0 case as a simplification, because getpgrp() is NOT pid==0
	 * and, per src/unistd/ids.c's banner, is not pid==getpid() either:
	 * a process that never called setpgrp()/setsid() answers 1, a
	 * sentinel chosen specifically so it CANNOT equal any real pid.
	 * Without this arm, killpg(getpgrp(), 0) fell through to the
	 * cross-process branch below and tried to NtOpenProcess a pid
	 * (1) that names no real process -- ESRCH for a call POSIX
	 * requires to succeed against the caller's own, real, group. */
	if (pid == getpid() || pid == getpgrp() || pid == 0 || pid == -1) {
		int result;
		if (!sig) return 0;
		/* kill() is process-directed even when its target set contains only
		 * this process.  Do not route it through raise(), whose pending state
		 * belongs specifically to the calling thread.  The shared internal
		 * path still handles self-stops by publishing a waitable marker for
		 * the parent before suspending this process. */
		__sig_lock();
		result = __raise_internal(sig);
		__sig_unlock();
		return result < 0 ? -1 : 0;
	}
	if (pid < 0) { errno = ESRCH; return -1; }
	c = __child_find(pid);
	if (c) h = c->h;
	else {
		InitializeObjectAttributes(&oa, 0, 0, 0, 0);
		cid.UniqueProcess = (HANDLE)(ULONG_PTR)pid;
		cid.UniqueThread = 0;
		/* The access mask below decides which errno the caller sees,
		 * so it is load-bearing and not merely "enough rights for
		 * what we do next".  kill.html's "[EPERM] The process does
		 * not have permission to send the signal" is produced here
		 * by NT's own access check on the target process object --
		 * ntlibc performs no identity comparison of its own, and
		 * src/unistd/ids.c's single token-derived uid has nothing to do
		 * with it.  Measured on real Windows 11 Pro 22621 against the
		 * System process (pid 4), from an ELEVATED token:
		 *
		 *   PROCESS_TERMINATE | QUERY_LIMITED_INFORMATION -> c0000022
		 *                                        (ACCESS_DENIED)
		 *   PROCESS_QUERY_LIMITED_INFORMATION alone       -> 00000000
		 *                                        (SUCCESS)
		 *   PROCESS_TERMINATE alone                       -> c0000022
		 *                                        (ACCESS_DENIED)
		 *
		 * The denial is specific to PROCESS_TERMINATE on a protected
		 * process, not a blanket refusal to touch pid 4.  Narrowing
		 * this mask to query-only would therefore turn a correct
		 * EPERM into a silent success, and would break
		 * test/posix-kill-perm-win.c for a reason that looks
		 * unrelated to the change.  Keep PROCESS_TERMINATE in the
		 * mask even if a future caller only needs to query.
		 *
		 * The ESRCH arm below is a genuinely different status, not a
		 * second reading of the same failure: a nonexistent pid
		 * answered STATUS_INVALID_CID (c000000b) in the same run. */
		want = PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION;
		/* PROCESS_SUSPEND_RESUME is asked for only when the signal
		 * actually needs it, and not folded into the mask above for
		 * every kill(): a right that is not needed can still be
		 * refused, and one more bit in the mask would turn the
		 * measured EPERM/ESRCH answers documented above into an EPERM
		 * for targets that today accept a plain signal. */
		if (sig_stops(sig) || sig == SIGCONT) want |= PROCESS_SUSPEND_RESUME;
		st = NtOpenProcess(&h, want, &oa, &cid);
		if (!NT_SUCCESS(st)) { errno = st == STATUS_ACCESS_DENIED ? EPERM : ESRCH; return -1; }
	}
	if (!sig) { if (!c) NtClose(h); return 0; }
	if (sig == SIGSTOP) {
		int changed = sig_job_control(c, h, sig);
		if (changed > 0) __sigchld_job_control(c, sig);
		if (!c) NtClose(h);
		return changed < 0 ? -1 : 0;
	}
	/* SIGCONT resumes before it is delivered, even when caught or ignored.
	 * Its default action is already ignore, so ordinary one-way delivery is
	 * sufficient and avoids waiting for a disposition acknowledgement from
	 * a child that may have stopped while holding its signal lock. */
	if (sig == SIGCONT) {
		int changed = sig_job_control(c, h, sig);
		if (changed < 0) { if (!c) NtClose(h); return -1; }
		if (changed > 0) __sigchld_job_control(c, sig);
		if (__sig_try_deliver_remote((int)pid, sig)) {
			if (!c) NtClose(h);
			return 0;
		}
		if (!c) NtClose(h);
		return 0;
	}
	/* SIGTSTP, SIGTTIN and SIGTTOU are catchable.  Ask the target to
	 * accept the packet only when its disposition is non-default.  If it
	 * declines, retain the NT suspend/resume fallback which implements the
	 * default job-control action. */
	if (sig_stops(sig) && __sig_try_deliver_remote_nondefault((int)pid, sig)) {
		if (!c) NtClose(h);
		return 0;
	}
	if (sig_stops(sig)) {
		int changed = sig_job_control(c, h, sig);
		if (changed > 0) __sigchld_job_control(c, sig);
		if (!c) NtClose(h);
		return changed < 0 ? -1 : 0;
	}
	/* Try the target's own listener before falling back to this
	 * process's blind default-action guess. src/signal/sigdelivery.c's
	 * __sig_try_deliver_remote() hands the packet to `pid`'s own
	 * delivery thread, which applies THAT process's real disposition
	 * (sa_handler/SIG_IGN, not just whatever default_action() would
	 * assume here) through the same __raise_internal() raise() uses --
	 * so success here is strictly more correct than the
	 * NtTerminateProcess() path below, and this function is done: no
	 * fallthrough on success. Failure (no listener -- no such process
	 * under this name, a non-ntlibc process, or one still inside
	 * __signal_init() -- or any other reason the packet did not land)
	 * falls straight through to the existing behaviour unchanged; see
	 * that function's own comment for why it does not try to
	 * distinguish those cases.  The catchable job-control signals already
	 * took their acknowledgement-based disposition path above. */
	if (__sig_try_deliver_remote((int)pid, sig)) {
		if (!c) NtClose(h);
		return 0;
	}
	st = NtTerminateProcess(h, __NT_SIGNAL_EXIT(sig));
	if (!c) NtClose(h);
	if (!NT_SUCCESS(st) && st != STATUS_PROCESS_IS_TERMINATING) return __set_errno_status(st);
	return 0;
}

int killpg(pid_t pg, int sig) { return kill(pg, sig); }

int sigemptyset(sigset_t *s) { memset(s, 0, sizeof *s); return 0; }
int sigfillset(sigset_t *s) { memset(s, 0xff, sizeof *s); return 0; }
int sigaddset(sigset_t *s, int sig) { if (!sig_valid(sig)) { errno = EINVAL; return -1; } s->__bits[sig / (8 * sizeof(long))] |= 1UL << (sig % (8 * sizeof(long))); return 0; }
int sigdelset(sigset_t *s, int sig) { if (!sig_valid(sig)) { errno = EINVAL; return -1; } s->__bits[sig / (8 * sizeof(long))] &= ~(1UL << (sig % (8 * sizeof(long)))); return 0; }
int sigismember(const sigset_t *s, int sig) { if (!sig_valid(sig)) { errno = EINVAL; return -1; } return !!(s->__bits[sig / (8 * sizeof(long))] & (1UL << (sig % (8 * sizeof(long))))); }
int sigisemptyset(const sigset_t *s) { size_t i; for (i = 0; i < sizeof s->__bits / sizeof s->__bits[0]; i++) if (s->__bits[i]) return 0; return 1; }
int sigorset(sigset_t *d, const sigset_t *a, const sigset_t *b) { size_t i; for (i = 0; i < sizeof d->__bits / sizeof d->__bits[0]; i++) d->__bits[i] = a->__bits[i] | b->__bits[i]; return 0; }

/* Called with the signal lock held.  Delivery may run a handler, which is
 * safe because the lock is recursive on the owning thread. */
static void drain_unblocked_pending(void)
{
	int i;
	for (i = 1; i < _NSIG; i++) {
		while (pending_member(i) && !sigismember(&blocked, i)) {
			siginfo_t si;
			take_pending_signal(i, &si);
			__raise_internal_info(i, &si);
		}
	}
}

void __sig_drain_pending(void)
{
	__sig_lock();
	drain_unblocked_pending();
	__sig_unlock();
}

int sigprocmask(int how, const sigset_t *set, sigset_t *old)
{
	int i;
	/* Locked for the whole call, `old` snapshot included: this is also
	 * why __raise_internal() below is safe to call without locking
	 * itself -- see src/signal/sigdelivery.c's banner for the invariant
	 * (__raise_internal() assumes its caller already holds the lock).
	 * __sig_lock() is recursive (same file), which is what lets THIS
	 * call site nest safely even when it is itself reached from inside
	 * a signal handler that raise() is still running under the same
	 * lock for -- see sigdelivery.c's banner for exactly that shape
	 * (test/posix-signal.c's mask_check_handler()). */
	__sig_lock();
	if (old) *old = blocked;
	if (set) {
		switch (how) {
		case SIG_BLOCK: sigorset(&blocked, &blocked, set); break;
		case SIG_UNBLOCK: for (i = 1; i < _NSIG; i++) if (sigismember(set, i)) sigdelset(&blocked, i); break;
		case SIG_SETMASK: blocked = *set; break;
		default: __sig_unlock(); errno = EINVAL; return -1;
		}
		sigdelset(&blocked, SIGKILL);
		sigdelset(&blocked, SIGSTOP);
		/* deliver anything unblocked and pending */
		drain_unblocked_pending();
	}
	__sig_unlock();
	return 0;
}

/* pthread_sigmask.html gives this the same mask operation as
 * sigprocmask(), but pthread interfaces return the error number directly
 * and do not report it through errno.  There is one process-wide mask while
 * ntlibc has only its initial thread; keeping the wrapper here makes that
 * contract usable now and leaves the storage boundary obvious when real
 * per-thread masks arrive. */
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old)
{
	int saved = errno;
	int result = sigprocmask(how, set, old);
	int error = errno;

	errno = saved;
	return result == 0 ? 0 : error;
}

int sigpending(sigset_t *s)
{
	__sig_lock();
	sigorset(s, &process_pending.set, &thread_pending.set);
	__sig_unlock();
	return 0;
}

int __sig_pending_member(int sig) { return pending_member(sig); }

int sigsuspend(const sigset_t *s)
{
	sigset_t old;
	unsigned long caught;

	if (!s) { errno = EFAULT; return -1; }
	/* Installing the temporary mask and taking the handler-count snapshot
	 * are one locked operation.  A remote delivery can therefore happen
	 * either before both or after both, never in the lost-wakeup gap that
	 * would leave this wait asleep after its signal already ran. */
	__sig_lock();
	old = blocked;
	caught = caught_count;
	blocked = *s;
	sigdelset(&blocked, SIGKILL);
	sigdelset(&blocked, SIGSTOP);
	drain_unblocked_pending();
	__sig_unlock();

	/* Polling keeps this thread parked without needing register-context
	 * injection.  Drain on this application thread so its temporary mask,
	 * not the listener thread's TLS mask, decides which signals are eligible. */
	while (__sig_caught_count() == caught) {
		LARGE_INTEGER delay = -100000; /* 10ms */
		__sig_drain_pending();
		if (__sig_caught_count() != caught) break;
		NtDelayExecution(TRUE, &delay);
	}

	/* Restoring the old mask may itself release a signal that arrived
	 * while the temporary mask blocked it; sigprocmask() performs that
	 * pending delivery before returning. */
	(void)sigprocmask(SIG_SETMASK, &old, NULL);
	errno = EINTR;
	return -1;
}

void __sig_pending_reset_after_fork(void)
{
	memset(&process_pending, 0, sizeof process_pending);
	memset(&thread_pending, 0, sizeof thread_pending);
	wait_active = 0;
}

int sigqueue(pid_t pid, int sig, union sigval value)
{
	siginfo_t si;
	int r;

	if (sig < 0 || sig >= _NSIG) { errno = EINVAL; return -1; }
	if (!sig) return kill(pid, 0);
	memset(&si, 0, sizeof si);
	si.si_signo = sig;
	si.si_code = SI_QUEUE;
	si.si_pid = getpid();
	si.si_uid = getuid();
	si.si_value = value;

	if (pid == getpid()) {
		__sig_lock();
		r = __raise_internal_info(sig, &si);
		__sig_unlock();
		return r < 0 ? -1 : 0;
	}
	/* kill(pid, 0) supplies the common existence and permission check
	 * without generating anything.  Once it succeeds, failure to hand
	 * the payload to the target listener means the bounded delivery
	 * resource was unavailable. */
	if (kill(pid, 0) < 0) return -1;
	if (__sig_try_deliver_remote_info((int)pid, sig, &si)) return 0;
	errno = EAGAIN;
	return -1;
}
/* sigwait.html DESCRIPTION: "shall select a pending signal from set,
 * atomically clear it from the system's set of pending signals, and
 * return that signal number in the location referenced by sig ... If no
 * signal in set is pending at the time of the call, the thread shall be
 * suspended until one or more becomes pending."
 *
 * This was `{ errno = EINVAL; return EINVAL; }` -- a degenerate stub
 * that failed for every argument, including a set it had no grounds to
 * reject, and additionally set errno, which RETURN VALUE does not
 * provide for: "an error number shall be returned to indicate the
 * error", through the return value alone.  errno is saved and restored
 * here so that stays true on every path.
 *
 * Selection is lowest-numbered-first, which also satisfies the one
 * ordering clause the page states outright ("Should any of the multiple
 * pending signals in the range SIGRTMIN to SIGRTMAX be selected, it
 * shall be the lowest numbered one").
 *
 * Signal numbers in set that are outside [1, _NSIG) are ignored rather
 * than rejected.  ERRORS makes "[EINVAL] The set argument contains an
 * invalid or unsupported signal number" a *may fail*, so both answers
 * conform, and two things decide it:
 *
 *   - sigfillset() above is memset(0xff) over a 128-byte sigset_t, so it
 *     sets 1024 bits for 64 real signals.  A sigwait() that rejected
 *     stray bits would fail `sigfillset(&s); sigwait(&s, &sig);` -- the
 *     commonest sigwait idiom there is -- every single time.
 *   - Measured, not derived: glibc does not reject them.  A raw
 *     memset(0xff) sigset_t with SIGUSR1 pending returns 0 and sig=10,
 *     as does glibc's own sigfillset().
 *
 * So this sigwait() has no failure mode at all, which is a legal shape
 * for a page whose only error is a may-fail.
 *
 * The suspend path is a real wait, not a fabricated return.  Nothing on
 * the main thread can make a signal pending while it is parked inside
 * this loop -- self-generated delivery is synchronous (see this file's
 * banner) -- but two other threads reach the process-pending queue through
 * __raise_internal() from outside this loop: the NTLIBC_USE_KERNEL32
 * console-control handler kernel32 creates, and (as of
 * src/signal/sigdelivery.c) this process's own cross-process-signal
 * delivery thread, driven by another process's kill(). So a blocked
 * signal genuinely can arrive here from outside. This loop does not
 * wake early for either -- it is a 100ms poll, not a wait on
 * sigdelivery.c's wake_event the way select() is taught to be -- so a
 * signal delivered this way is still seen, just on this loop's own
 * schedule rather than the instant it arrives; that is within the
 * 100ms-poll design already documented below, not a new gap. Where
 * nothing can ever signal this process from outside either, this waits
 * forever, which is what POSIX specifies for a thread that asks for a
 * signal nothing will ever send; inventing an EINTR or an EAGAIN to
 * escape would be reporting an event that did not happen.
 * NtDelayExecution() rather than a spin keeps that wait off the CPU. */
int sigwait(const sigset_t *s, int *sig)
{
	int saved_errno = errno;
	int selected;

	__sig_lock();
	waiting_set = *s;
	wait_active = 1;
	__sig_unlock();
	for (;;) {
		__sig_lock();
		selected = take_pending_from_set(s, 0);
		if (selected) {
			wait_active = 0;
			__sig_unlock();
			if (sig) *sig = selected;
			errno = saved_errno;
			return 0;
		}
		__sig_unlock();
		{
			/* LARGE_INTEGER is a plain LONGLONG here (src/internal/nt.h);
			 * negative means relative, in 100ns units, so this is 100ms
			 * -- the same convention src/unistd/sleep.c uses. */
			LARGE_INTEGER d = -1000000;
			NtDelayExecution(TRUE, &d);
		}
	}
}

int sigwaitinfo(const sigset_t *set, siginfo_t *info)
{
	int selected;
	__sig_lock();
	waiting_set = *set;
	wait_active = 1;
	__sig_unlock();
	for (;;) {
		__sig_lock();
		selected = take_pending_from_set(set, info);
		if (selected) {
			wait_active = 0;
			__sig_unlock();
			return selected;
		}
		__sig_unlock();
		{
			LARGE_INTEGER d = -100000; /* 10ms */
			NtDelayExecution(TRUE, &d);
		}
	}
}

int sigtimedwait(const sigset_t *set, siginfo_t *info, const struct timespec *timeout)
{
	struct timespec start, now;
	long long limit, elapsed;
	int selected;

	if (!timeout || timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
	    timeout->tv_nsec >= 1000000000L) {
		errno = EINVAL;
		return -1;
	}
	limit = (long long)timeout->tv_sec * 1000000000LL + timeout->tv_nsec;
	clock_gettime(CLOCK_MONOTONIC, &start);
	__sig_lock();
	waiting_set = *set;
	wait_active = 1;
	__sig_unlock();
	for (;;) {
		__sig_lock();
		selected = take_pending_from_set(set, info);
		if (selected) {
			wait_active = 0;
			__sig_unlock();
			return selected;
		}
		__sig_unlock();
		clock_gettime(CLOCK_MONOTONIC, &now);
		elapsed = (long long)(now.tv_sec - start.tv_sec) * 1000000000LL
		        + now.tv_nsec - start.tv_nsec;
		if (elapsed >= limit) {
			__sig_lock();
			wait_active = 0;
			__sig_unlock();
			errno = EAGAIN;
			return -1;
		}
		{
			long long left = limit - elapsed;
			LARGE_INTEGER d = -(left < 10000000LL ? (left + 99) / 100 : 100000);
			NtDelayExecution(TRUE, &d);
		}
	}
}
/* siginterrupt.html ERRORS, shall-fail: "[EINVAL] The sig argument is not
 * a valid signal number."  The effect the page names -- clearing or
 * setting SA_RESTART -- is still a no-op here, but the reasoning
 * changed: a signal CAN now interrupt a blocked call mid-wait --
 * select()/pselect(), from another process's kill() routed through
 * src/signal/sigdelivery.c -- and that call's own banner explains why
 * it is chosen to behave as Linux's select()/poll() do: EINTR
 * regardless of SA_RESTART, one of the two answers select.html leaves
 * implementation-defined. So the flag genuinely has nothing to steer
 * there either, for a different reason than "nothing is ever
 * interrupted" -- every OTHER blocking call in this library still
 * cannot be interrupted mid-syscall at all (no NtCancelSynchronousIoFile;
 * see sigdelivery.c's banner), so for those the original reasoning
 * still holds outright. Either way this does not excuse dropping the
 * argument check: [EINVAL] is a clause about the argument, not about
 * the effect, and a caller that passes a bad signal number is entitled
 * to hear about it here exactly as it would from signal() or
 * sigaction(). Unlike those two, SIGKILL and SIGSTOP are accepted: this
 * page lists no uncatchable-signal error, and with the flag a no-op
 * there is nothing about them to refuse. */
int siginterrupt(int sig, int flag) { if (!sig_valid(sig)) { errno = EINVAL; return -1; } (void)flag; return 0; }
/* The alternate signal stack, and whether a handler is running on it.
 *
 * This used to be a stub that ignored ss, reported SS_DISABLE, and
 * returned 0 -- success for work it had not done. That is the worst
 * shape an unimplemented function can take: a caller that checks the
 * return value is told its stack was installed, and only finds out
 * otherwise by reading back a stack that is not there. 26 Open POSIX
 * sigaction cases died on exactly that, exiting 255 rather than failing
 * cleanly, which is why they read as ABNORMAL rather than FAIL.
 *
 * Locked (__sig_lock()/__sig_unlock(), src/signal/sigdelivery.c):
 * alt_stack/alt_active used to need no locking because delivery was
 * synchronous and single-threaded; sig_delivery_thread() can now run a
 * handler on alt_stack (via sig_dispatch(), inside __raise_internal())
 * concurrently with this function reading or changing it. */

int sigaltstack(const stack_t *ss, stack_t *old)
{
	__sig_lock();
	/* sigaltstack.html: old is filled in before any change from ss is
	 * applied, so a caller may swap stacks in one call. */
	if (old) {
		old->ss_sp = alt_stack.ss_sp;
		old->ss_size = alt_stack.ss_size;
		old->ss_flags = alt_stack.ss_sp
			? (alt_active ? SS_ONSTACK : 0)
			: SS_DISABLE;
	}
	if (ss) {
		/* "[EPERM] An attempt was made to modify an active stack." */
		if (alt_active) { __sig_unlock(); errno = EPERM; return -1; }
		/* "[EINVAL] The ss argument is not a null pointer, and the
		 * ss_flags member ... contains flags other than SS_DISABLE." */
		if (ss->ss_flags & ~(int)SS_DISABLE) { __sig_unlock(); errno = EINVAL; return -1; }
		if (ss->ss_flags & SS_DISABLE) {
			alt_stack.ss_sp = 0;
			alt_stack.ss_size = 0;
			alt_stack.ss_flags = SS_DISABLE;
		} else {
			/* "[ENOMEM] The size of the alternate stack area is less
			 * than MINSIGSTKSZ." */
			if (ss->ss_size < MINSIGSTKSZ) { __sig_unlock(); errno = ENOMEM; return -1; }
			if (!ss->ss_sp) { __sig_unlock(); errno = EINVAL; return -1; }
			alt_stack.ss_sp = ss->ss_sp;
			alt_stack.ss_size = ss->ss_size;
			alt_stack.ss_flags = 0;
		}
	}
	__sig_unlock();
	return 0;
}

int __libc_current_sigrtmin(void) { return 35; }
int __libc_current_sigrtmax(void) { return _NSIG - 1; }

/* sigignore.html is the disposition-only half of sigset(sig, SIG_IGN):
 * it makes sig ignored without changing whether it is blocked.  Keeping
 * this as sigaction(), rather than spelling it as signal(), also makes the
 * POSIX requirement explicit: SIGKILL, SIGSTOP and invalid signal numbers
 * fail with EINVAL through the same validation used by every other
 * disposition-setting interface. */
int sigignore(int sig)
{
	struct sigaction act;

	memset(&act, 0, sizeof act);
	act.sa_handler = SIG_IGN;
	return sigaction(sig, &act, 0);
}

/* sigaddset() is the only place these two ever look at sig, so its
 * failure is the whole of sigset.html's "[EINVAL] The sig argument is an
 * illegal signal number" for them: dropping it does not degrade to a
 * failed sigprocmask(), because the set is then simply left empty and an
 * empty mask is a legal argument that sigprocmask() reports success for.
 * Returning here also leaves the process mask untouched, as a shall-fail
 * call must. */
int sighold(int sig) { sigset_t s; sigemptyset(&s); if (sigaddset(&s, sig) < 0) return -1; return sigprocmask(SIG_BLOCK, &s, 0); }
int sigrelse(int sig) { sigset_t s; sigemptyset(&s); if (sigaddset(&s, sig) < 0) return -1; return sigprocmask(SIG_UNBLOCK, &s, 0); }

/* sigset.html is not signal() with a different name, and the difference
 * is entirely about the signal mask.  RETURN VALUE: "Upon successful
 * completion, sigset() shall return SIG_HOLD if the signal had been
 * blocked and the signal's previous disposition if it had not been
 * blocked."  DESCRIPTION, for the ordinary disposition-setting case:
 * "sig shall be removed from the calling process' signal mask" -- the
 * SIG_HOLD return is how the caller learns that just happened, which is
 * why the two clauses have to be implemented together.  And for
 * func == SIG_HOLD: "sig shall be added to the calling process' signal
 * mask and its disposition shall remain unchanged" -- the one call that
 * moves the mask the other way and installs nothing.
 *
 * The unblock is done after the new disposition is in place, not before:
 * sigprocmask(SIG_UNBLOCK) delivers whatever became deliverable, and a
 * signal that arrived while sig was held belongs to the handler the
 * caller is installing now, not to the one it is replacing. */
void (*sigset(int sig, void (*h)(int)))(int)
{
	void (*old)(int);
	int was_blocked;
	sigset_t one;

	if (!sig_valid(sig) || sig == SIGKILL || sig == SIGSTOP) { errno = EINVAL; return SIG_ERR; }
	was_blocked = sigismember(&blocked, sig);
	sigemptyset(&one);
	sigaddset(&one, sig);

	if (h == SIG_HOLD) {
		old = handlers[sig];   /* read before the mask moves: sigprocmask()
		                        * runs whatever became deliverable, and a
		                        * handler may install a new disposition */
		if (sigprocmask(SIG_BLOCK, &one, 0) < 0) return SIG_ERR;
		return was_blocked ? SIG_HOLD : old;
	}

	old = signal(sig, h);
	if (old == SIG_ERR) return SIG_ERR;
	if (!was_blocked) return old;
	if (sigprocmask(SIG_UNBLOCK, &one, 0) < 0) return SIG_ERR;
	return SIG_HOLD;
}
int sigpause(int sig)
{
	sigset_t mask;

	if (!sig_valid(sig)) { errno = EINVAL; return -1; }
	mask = blocked;
	sigdelset(&mask, sig);
	return sigsuspend(&mask);
}

/* SEGV_MAPERR vs SEGV_ACCERR (signal.h.html siginfo_t DESCRIPTION) for
 * an EXCEPTION_ACCESS_VIOLATION/EXCEPTION_IN_PAGE_ERROR fault: NT's
 * EXCEPTION_RECORD only says whether the access was a read/write/
 * execute, not whether the page was unmapped or merely off-limits --
 * but NtQueryVirtualMemory(MemoryBasicInformation) on the faulting
 * address answers exactly that, through State (src/internal/nt.h):
 *
 *   MEM_FREE     nothing is mapped there at all         -> SEGV_MAPERR
 *   MEM_RESERVE  address space reserved, never committed
 *                (no backing page to access either)      -> SEGV_MAPERR
 *   MEM_COMMIT   a real page exists; the fault is
 *                Protect denying this exact access       -> SEGV_ACCERR
 *
 * NOT ATOMIC / TOCTOU: this call happens after the fault, not as part
 * of it -- nothing stops another thread from mapping, unmapping or
 * reprotecting the same address in between (VirtualAlloc/VirtualFree/
 * VirtualProtect-equivalent). For a synchronous fault handled on the
 * faulting thread with no other thread racing that address -- the
 * ordinary case, and the only one this library's own tests provoke --
 * the two states cannot practically diverge; it is not a guarantee
 * either POSIX or this library can make in the general, multithreaded
 * case. If the query itself fails (STATUS_ACCESS_DENIED touching a
 * kernel address, an already-torn-down process, etc.) SEGV_MAPERR is
 * the honest fallback: "cannot even ask" is closer to "not mapped"
 * than to "mapped but protected". */
static int segv_code(void *addr)
{
	MEMORY_BASIC_INFORMATION mbi;
	SIZE_T ret = 0;
	NTSTATUS st;

	memset(&mbi, 0, sizeof mbi);
	st = NtQueryVirtualMemory(NtCurrentProcess(), addr, MemoryBasicInformation, &mbi, sizeof mbi, &ret);
	if (!NT_SUCCESS(st)) return SEGV_MAPERR;
	return mbi.State == MEM_COMMIT ? SEGV_ACCERR : SEGV_MAPERR;
}

/* NT exceptions that correspond to synchronous signals, and (for the
 * fault-shaped ones) the si_code that names the fault precisely --
 * signal.h.html siginfo_t DESCRIPTION requires si_code to be one of
 * these fault-specific values, not left as a generic SI_KERNEL, for
 * SIGILL/SIGFPE/SIGSEGV/SIGBUS "generated by the implementation for
 * some reason not covered by [SI_USER etc.]". Most codes fall straight
 * out of ExceptionCode; only SEGV_MAPERR/SEGV_ACCERR need the extra
 * NtQueryVirtualMemory() lookup above. */
static LONG NTAPI exception_handler(EXCEPTION_POINTERS *ep)
{
	int sig, code;
	ULONG excode = ep->ExceptionRecord->ExceptionCode;

	switch (excode) {
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_IN_PAGE_ERROR:
		sig = SIGSEGV;
		code = segv_code((void *)ep->ExceptionRecord->ExceptionInformation[1]);
		break;
	case EXCEPTION_STACK_OVERFLOW:
		/* Running off the end of the reserved stack region: by
		 * definition there is no committed page to have been denied
		 * access to, so this is a mapping failure, not a protection
		 * one -- SEGV_MAPERR, without needing (or trusting) a query:
		 * EXCEPTION_STACK_OVERFLOW's EXCEPTION_RECORD does not
		 * reliably carry a faulting address the way access-violation
		 * does (ExceptionInformation[1] is meaningful only for
		 * EXCEPTION_ACCESS_VIOLATION/EXCEPTION_IN_PAGE_ERROR), and
		 * this exception is already unambiguous on its own. */
		sig = SIGSEGV;
		code = SEGV_MAPERR;
		break;
	case EXCEPTION_DATATYPE_MISALIGNMENT: sig = SIGBUS; code = BUS_ADRALN; break;
	case EXCEPTION_ILLEGAL_INSTRUCTION: sig = SIGILL; code = ILL_ILLOPC; break;
	case EXCEPTION_PRIV_INSTRUCTION: sig = SIGILL; code = ILL_PRVOPC; break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO: sig = SIGFPE; code = FPE_INTDIV; break;
	case EXCEPTION_INT_OVERFLOW: sig = SIGFPE; code = FPE_INTOVF; break;
	case EXCEPTION_FLT_DIVIDE_BY_ZERO: sig = SIGFPE; code = FPE_FLTDIV; break;
	case EXCEPTION_FLT_INVALID_OPERATION: sig = SIGFPE; code = FPE_FLTINV; break;
	case EXCEPTION_FLT_OVERFLOW: sig = SIGFPE; code = FPE_FLTOVF; break;
	case EXCEPTION_FLT_UNDERFLOW: sig = SIGFPE; code = FPE_FLTUND; break;
	case EXCEPTION_FLT_INEXACT_RESULT: sig = SIGFPE; code = FPE_FLTRES; break;
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		/* FPE_FLTSUB is signal.h.html's "subscript out of range", which
		 * is what #BR reports, so this is a name-for-name match rather
		 * than the closest-sounding pick the denormal case below
		 * refuses to make. SIGFPE and not SIGILL: the instruction was
		 * legal and executed, it was the operand that was out of range.
		 * Only i386 can reach it (long mode has no BOUND), but the case
		 * is unconditional -- dispatch here is on the exception code,
		 * and an #ifdef would only make the two arches disagree about a
		 * status neither can see the other raise. */
		sig = SIGFPE;
		code = FPE_FLTSUB;
		break;
	case EXCEPTION_FLT_DENORMAL_OPERAND:
		/* A real FP condition (an operand was a denormal), but POSIX's
		 * FPE_* list (signal.h.html) has no member for it -- INTDIV/
		 * INTOVF/FLTDIV/FLTOVF/FLTUND/FLTRES/FLTINV are the whole set,
		 * and none of them is "denormal operand". Rather than pick the
		 * closest-sounding one and misreport the cause, fall back to
		 * SI_KERNEL: honest ("not from kill()/raise()"), not a
		 * fabricated FPE_* subcode. */
		sig = SIGFPE;
		code = SI_KERNEL;
		break;
	case EXCEPTION_BREAKPOINT: sig = SIGTRAP; code = SI_KERNEL; break;
	case DBG_CONTROL_C:
	case DBG_CONTROL_BREAK: sig = SIGINT; code = SI_KERNEL; break;
	case STATUS_GUARD_PAGE_VIOLATION:
		/* A distinct exception from EXCEPTION_ACCESS_VIOLATION (0x80000001
		 * vs 0xC0000005), raised on touching a PAGE_GUARD page -- most
		 * commonly something outside ntlibc probing a thread stack's
		 * own guard region. This library never sets PAGE_GUARD itself
		 * (no PAGE_GUARD bit among src/internal/nt.h's PAGE_* constants),
		 * so there is no case here to fold into SIGSEGV; treated like
		 * any other exception this handler does not claim, by falling
		 * through to the next handler instead of guessing a signal. */
	default: return EXCEPTION_CONTINUE_SEARCH;
	}
	if (handlers[sig] == SIG_DFL) {
		/* No flush, unconditionally -- and for two independent reasons.
		 *
		 * The clause is the one at __raise_internal()'s SIG_DFL branch
		 * above: XSH 2.4.3 makes a default-terminate signal behave "as
		 * if by a call to _exit()", and _exit() says open streams
		 * "shall not be flushed".  SIGABRT is the only signal POSIX
		 * exempts (abort.html's "may include an attempt to effect
		 * fclose()"), and nothing reaching THIS function is SIGABRT:
		 * every case above maps an NT exception to SIGSEGV, SIGBUS,
		 * SIGILL, SIGFPE, SIGTRAP or SIGINT.  So the exemption cannot
		 * apply here and the prohibition always does.
		 *
		 * Independently of conformance: this runs inside a vectored
		 * exception handler, on whatever stack is left at the moment of
		 * the fault.  For EXCEPTION_STACK_OVERFLOW that is by
		 * definition almost none, and __stdio_exit() walks every open
		 * FILE calling fflush().  Flushing was how the handler for a
		 * stack overflow used to re-enter the very cycle that caused
		 * it -- see src/stdio/file.c. */
		__nt_exit(__NT_SIGNAL_EXIT(sig));
	}
	if (handlers[sig] == SIG_IGN) {
		/* Ignoring a fault would loop forever; POSIX says undefined. Die. */
		if (sig != SIGINT && sig != SIGTRAP) __nt_exit(__NT_SIGNAL_EXIT(sig));
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	/* Tell __raise_internal() this delivery is not a kill()/raise() --
	 * see the siginfo_t construction there. si_addr (signal.h.html) is
	 * only meaningful for the access-violation-shaped exceptions, whose
	 * EXCEPTION_RECORD documents ExceptionInformation[1] as the
	 * faulting address (src/internal/nt.h); the others (misalignment,
	 * illegal instruction, arithmetic traps, breakpoints, Ctrl-C) carry
	 * no such address, so fault_addr stays NULL for them. */
	/* Locked from here through the __raise_internal() call: fault_active/
	 * fault_addr/fault_si_code are read back inside __raise_internal()'s
	 * SA_SIGINFO construction, and now that sig_delivery_thread()
	 * (src/signal/sigdelivery.c) can be inside its own __raise_internal()
	 * call on another thread at the same moment, an unlocked write here
	 * could be read back by that call instead of this one. */
	__sig_lock();
	fault_active = 1;
	fault_addr = (excode == EXCEPTION_ACCESS_VIOLATION || excode == EXCEPTION_IN_PAGE_ERROR)
	           ? (void *)ep->ExceptionRecord->ExceptionInformation[1] : NULL;
	fault_si_code = code;
	__raise_internal(sig);
	fault_active = 0;
	__sig_unlock();
	/* A handler that returns from a fault re-executes the instruction,
	 * as on Unix; for SIGINT/SIGTRAP continuing is the right thing. */
	return EXCEPTION_CONTINUE_EXECUTION;
}

#ifdef NTLIBC_USE_KERNEL32
/* Runs on a thread kernel32 creates for the purpose, not on the main
 * thread -- so this races a main thread that is, say, in the middle of
 * sigprocmask() touching the same `handlers`, `blocked`, and
 * `process_pending`
 * globals, exactly the way src/signal/sigdelivery.c's delivery thread
 * does. __sig_lock()/__sig_unlock() (defined there) cover both: this
 * was the first real extra thread in this library, before this change
 * added a second one on purpose, and both are now handled the same way
 * rather than only the new one. The unlocked `handlers[SIGINT] ==
 * SIG_DFL` read below is a deliberate exception, not an oversight: it
 * is a fast-path check for "let the default action run" that
 * __raise_internal() (locked, below) re-derives correctly regardless of
 * what this read saw, so a torn read here costs at most one redundant
 * dispatch, never a wrong outcome. */
static BOOL NTAPI ctrl_handler(DWORD type)
{
	switch (type) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		/* Same signal for both, same as the vectored handler does
		 * for DBG_CONTROL_C/DBG_CONTROL_BREAK: this library has no
		 * SIGBREAK, and neither does POSIX. */
		if (handlers[SIGINT] == SIG_DFL) return FALSE;  /* let the default action run */
		__sig_lock();
		__raise_internal(SIGINT);
		__sig_unlock();
		return TRUE;
	default:
		/* CTRL_CLOSE_EVENT/CTRL_LOGOFF_EVENT/CTRL_SHUTDOWN_EVENT:
		 * no POSIX signal maps cleanly onto any of these (they are
		 * closer to being told the terminal hung up while nobody's
		 * home), and the handler thread is on a short clock before
		 * kernel32 kills the process regardless.  Leave the default
		 * behaviour -- the process ends -- in effect. */
		return FALSE;
	}
}
#endif

#ifdef NTLIBC_USE_KERNEL32
/* kernel32 is reached with LdrLoadDll()/LdrGetProcedureAddress() -- both
 * ntdll exports -- rather than by linking against kernel32's import
 * library.  That keeps NTLIBC_USE_KERNEL32 a purely load-time decision:
 * a binary built with it still only *links* against ntdll, and only
 * pulls kernel32 into its address space if it actually runs on a build
 * where this was requested.  (It also means there's no kernel32.def-vs-
 * tcc's-search-path question to worry about at link time -- see
 * CONTRIBUTING.md for why kernel32 is meant to be the exception, not
 * a routine dependency.) */
static void install_ctrl_handler(void)
{
	UNICODE_STRING dllname;
	ANSI_STRING procname;
	PVOID kernel32, proc;

	RtlInitUnicodeString(&dllname, L"kernel32.dll");
	if (!NT_SUCCESS(LdrLoadDll(NULL, NULL, &dllname, &kernel32))) return;

	procname.Buffer = "SetConsoleCtrlHandler";
	/* A 21-byte string literal, not anything a caller supplies, so this
	 * narrowing to the ANSI_STRING's USHORT lengths cannot wrap.
	 * USHORT-safe: 21-byte string literal. */
	procname.Length = procname.MaximumLength = (USHORT)strlen(procname.Buffer);
	if (!NT_SUCCESS(LdrGetProcedureAddress(kernel32, &procname, 0, &proc))) return;

	((BOOL (NTAPI *)(PHANDLER_ROUTINE, BOOL))proc)(ctrl_handler, TRUE);
}
#endif

void __signal_init(void)
{
	RtlAddVectoredExceptionHandler(1, exception_handler);
#ifdef NTLIBC_USE_KERNEL32
	install_ctrl_handler();
#else
	/* No ntdll path to console control events exists (see
	 * CONTRIBUTING.md); nothing to install.  Ctrl-C keeps ending the
	 * process via the console's own default handling. */
#endif
	/* Cross-process signal delivery (src/signal/sigdelivery.c): the
	 * mutex, listener pipe and delivery thread this process's own
	 * kill()/select() need. Last, deliberately: it starts a real second
	 * thread that can immediately begin calling __raise_internal(), so
	 * every other piece of this process's signal state (the vectored
	 * handler above, and NTLIBC_USE_KERNEL32's console handler) is
	 * already installed before that thread could possibly race it. */
	__sig_delivery_init();
}

/* psignal.html DESCRIPTION: "shall write a message to the standard
 * error stream... If the argument message is not a null pointer,
 * message ... followed by a colon character and a <space> character ...
 * If message is a null pointer or points to the null string, the error
 * message shall consist only of [the strsignal() text]." Needs nothing
 * this file does not already have: strsignal() (src/string/strsignal.c)
 * for the text, stdio for the write. */
void psignal(int sig, const char *s)
{
	if (s && *s) fprintf(stderr, "%s: %s\n", s, strsignal(sig));
	else fprintf(stderr, "%s\n", strsignal(sig));
}

/* psiginfo.html DESCRIPTION: same message shape as psignal(), driven
 * off a siginfo_t's si_signo instead of a bare signal number -- "as if
 * generated by strsignal()". RETURN VALUE: "no return value" for
 * either. */
void psiginfo(const siginfo_t *pinfo, const char *s)
{
	psignal(pinfo->si_signo, s);
}
