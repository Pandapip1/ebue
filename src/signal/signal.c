/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Signals, as far as they can be had.
 *
 * There is no asynchronous delivery from other processes: kill() can
 * only end a process, not interrupt it.  What does work is what a single
 * program generates itself -- raise(), abort(), a write to a broken
 * pipe, and hardware faults (SIGSEGV, SIGFPE, SIGILL, SIGBUS), which
 * arrive as NT exceptions and are turned into signals by a vectored
 * exception handler installed at startup.
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
#include <errno.h>
#include "libc.h"
#ifdef NTLIBC_USE_KERNEL32
#include "kernel32.h"
#endif

static void (*handlers[_NSIG])(int);
static sigset_t blocked;
static sigset_t pending;

static int sig_valid(int sig) { return sig > 0 && sig < _NSIG; }

void (*signal(int sig, void (*h)(int)))(int)
{
	void (*old)(int);
	if (!sig_valid(sig) || sig == SIGKILL || sig == SIGSTOP) { errno = EINVAL; return SIG_ERR; }
	old = handlers[sig];
	handlers[sig] = h;
	return old;
}

void (*bsd_signal(int sig, void (*h)(int)))(int) { return signal(sig, h); }

int sigaction(int sig, const struct sigaction *act, struct sigaction *old)
{
	if (!sig_valid(sig) || sig == SIGKILL || sig == SIGSTOP) { errno = EINVAL; return -1; }
	if (old) {
		memset(old, 0, sizeof *old);
		old->sa_handler = handlers[sig];
	}
	if (act) handlers[sig] = act->sa_handler;
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

/* Deliver a signal to this process now.  Returns 0 if it was handled or
 * ignored and control may continue; does not return if the default
 * action is to die. */
int __raise_internal(int sig)
{
	void (*h)(int);
	if (!sig_valid(sig)) { errno = EINVAL; return -1; }
	if (sigismember(&blocked, sig)) { sigaddset(&pending, sig); return 0; }
	h = handlers[sig];
	if (h == SIG_IGN) return 0;
	if (h == SIG_DFL) {
		if (!default_action(sig)) return 0;
		__stdio_exit();
		__nt_exit(128 + sig);
	}
	if (sig != SIGILL && sig != SIGTRAP) handlers[sig] = handlers[sig];  /* BSD semantics: stays installed */
	h(sig);
	return 0;
}

int raise(int sig) { return __raise_internal(sig) < 0 ? -1 : 0; }

int kill(pid_t pid, int sig)
{
	struct __child *c;
	HANDLE h;
	NTSTATUS st;
	OBJECT_ATTRIBUTES oa;
	CLIENT_ID cid;

	if (pid == getpid() || pid == 0) {
		if (!sig) return 0;
		return raise(sig);
	}
	if (pid < 0) { errno = ESRCH; return -1; }
	c = __child_find(pid);
	if (c) h = c->h;
	else {
		InitializeObjectAttributes(&oa, 0, 0, 0, 0);
		cid.UniqueProcess = (HANDLE)(ULONG_PTR)pid;
		cid.UniqueThread = 0;
		st = NtOpenProcess(&h, PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, &oa, &cid);
		if (!NT_SUCCESS(st)) { errno = st == STATUS_ACCESS_DENIED ? EPERM : ESRCH; return -1; }
	}
	if (!sig) { if (!c) NtClose(h); return 0; }
	st = NtTerminateProcess(h, 128 + sig);
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
int sigandset(sigset_t *d, const sigset_t *a, const sigset_t *b) { size_t i; for (i = 0; i < sizeof d->__bits / sizeof d->__bits[0]; i++) d->__bits[i] = a->__bits[i] & b->__bits[i]; return 0; }

int sigprocmask(int how, const sigset_t *set, sigset_t *old)
{
	int i;
	if (old) *old = blocked;
	if (set) {
		switch (how) {
		case SIG_BLOCK: sigorset(&blocked, &blocked, set); break;
		case SIG_UNBLOCK: for (i = 1; i < _NSIG; i++) if (sigismember(set, i)) sigdelset(&blocked, i); break;
		case SIG_SETMASK: blocked = *set; break;
		default: errno = EINVAL; return -1;
		}
		sigdelset(&blocked, SIGKILL);
		sigdelset(&blocked, SIGSTOP);
		/* deliver anything unblocked and pending */
		for (i = 1; i < _NSIG; i++)
			if (sigismember(&pending, i) && !sigismember(&blocked, i)) { sigdelset(&pending, i); __raise_internal(i); }
	}
	return 0;
}

int sigpending(sigset_t *s) { *s = pending; return 0; }
int sigsuspend(const sigset_t *s) { (void)s; errno = EINTR; return -1; }
int sigwait(const sigset_t *s, int *sig) { (void)s; (void)sig; errno = EINVAL; return EINVAL; }
int siginterrupt(int sig, int flag) { (void)sig; (void)flag; return 0; }
int sigaltstack(const stack_t *ss, stack_t *old) { (void)ss; if (old) { old->ss_sp = 0; old->ss_size = 0; old->ss_flags = SS_DISABLE; } return 0; }
int __libc_current_sigrtmin(void) { return 35; }
int __libc_current_sigrtmax(void) { return _NSIG - 1; }
int sighold(int sig) { sigset_t s; sigemptyset(&s); sigaddset(&s, sig); return sigprocmask(SIG_BLOCK, &s, 0); }
int sigrelse(int sig) { sigset_t s; sigemptyset(&s); sigaddset(&s, sig); return sigprocmask(SIG_UNBLOCK, &s, 0); }
void (*sigset(int sig, void (*h)(int)))(int) { return signal(sig, h); }
int sigpause(int sig) { (void)sig; errno = EINTR; return -1; }

/* NT exceptions that correspond to synchronous signals. */
static LONG NTAPI exception_handler(EXCEPTION_POINTERS *ep)
{
	int sig;
	switch (ep->ExceptionRecord->ExceptionCode) {
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_STACK_OVERFLOW:
	case EXCEPTION_IN_PAGE_ERROR: sig = SIGSEGV; break;
	case EXCEPTION_DATATYPE_MISALIGNMENT: sig = SIGBUS; break;
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION: sig = SIGILL; break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_INT_OVERFLOW:
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
	case EXCEPTION_FLT_INVALID_OPERATION:
	case EXCEPTION_FLT_OVERFLOW: sig = SIGFPE; break;
	case EXCEPTION_BREAKPOINT: sig = SIGTRAP; break;
	case DBG_CONTROL_C:
	case DBG_CONTROL_BREAK: sig = SIGINT; break;
	default: return EXCEPTION_CONTINUE_SEARCH;
	}
	if (handlers[sig] == SIG_DFL) {
		__stdio_exit();
		__nt_exit(128 + sig);
	}
	if (handlers[sig] == SIG_IGN) {
		/* Ignoring a fault would loop forever; POSIX says undefined. Die. */
		if (sig != SIGINT && sig != SIGTRAP) __nt_exit(128 + sig);
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	__raise_internal(sig);
	/* A handler that returns from a fault re-executes the instruction,
	 * as on Unix; for SIGINT/SIGTRAP continuing is the right thing. */
	return EXCEPTION_CONTINUE_EXECUTION;
}

#ifdef NTLIBC_USE_KERNEL32
/* Runs on a thread kernel32 creates for the purpose, not on the main
 * thread -- so this can race a main thread that is, say, in the middle
 * of sigprocmask() touching the same `handlers`/`blocked`/`pending`
 * globals.  ntlibc has no locking anywhere else either (there is no
 * threading support to speak of), so this is no worse than the existing
 * exception_handler(), which has the same race against a genuine
 * hardware fault; it is just now reachable from a real extra thread
 * instead of only in theory. */
static BOOL NTAPI ctrl_handler(DWORD type)
{
	switch (type) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		/* Same signal for both, same as the vectored handler does
		 * for DBG_CONTROL_C/DBG_CONTROL_BREAK: this library has no
		 * SIGBREAK, and neither does POSIX. */
		if (handlers[SIGINT] == SIG_DFL) return FALSE;  /* let the default action run */
		__raise_internal(SIGINT);
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
}
