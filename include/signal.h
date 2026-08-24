/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _SIGNAL_H
#define _SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)

#ifdef _GNU_SOURCE
#define __ucontext ucontext
#endif

#define __NEED_size_t
#define __NEED_pid_t
#define __NEED_uid_t
#define __NEED_struct_timespec
#define __NEED_pthread_t
#define __NEED_pthread_attr_t
#define __NEED_time_t
#define __NEED_clock_t
#define __NEED_sigset_t

#include <bits/alltypes.h>

#define SIG_BLOCK     0
#define SIG_UNBLOCK   1
#define SIG_SETMASK   2

#define SI_ASYNCNL (-60)
#define SI_TKILL (-6)
#define SI_SIGIO (-5)
#define SI_ASYNCIO (-4)
#define SI_MESGQ (-3)
#define SI_TIMER (-2)
#define SI_QUEUE (-1)
#define SI_USER 0
#define SI_KERNEL 128

/* si_code values for the hardware-fault signals (SIGILL/SIGFPE/SIGSEGV/
 * SIGBUS), populated by src/signal/signal.c's exception_handler() from
 * the NT EXCEPTION_RECORD -- see the derivation comments there for how
 * each is actually computed. Numeric values match musl/glibc's
 * bits/signal.h so a program that hardcodes these numbers instead of
 * the names still behaves the same as on Linux; POSIX itself only
 * requires the names, not particular values.
 *
 * Only macros this library can genuinely fill are declared here. The
 * ones glibc/musl also define that ntlibc leaves out -- ILL_ILLOPN,
 * ILL_ILLADR, ILL_ILLTRP, ILL_PRVREG, ILL_COPROC, ILL_BADSTK,
 * FPE_FLTSUB, SEGV_BNDERR, SEGV_PKUERR, SEGV_MTEAERR, SEGV_MTESERR,
 * BUS_ADRERR, BUS_OBJERR, BUS_MCEERR_AR, BUS_MCEERR_AO -- have no NT
 * exception that reliably maps onto them, so declaring them would
 * promise a fault subcode this library can never actually produce. */
#define ILL_ILLOPC 1   /* EXCEPTION_ILLEGAL_INSTRUCTION */
#define ILL_PRVOPC 5   /* EXCEPTION_PRIV_INSTRUCTION */

#define FPE_INTDIV 1   /* EXCEPTION_INT_DIVIDE_BY_ZERO */
#define FPE_INTOVF 2   /* EXCEPTION_INT_OVERFLOW */
#define FPE_FLTDIV 3   /* EXCEPTION_FLT_DIVIDE_BY_ZERO */
#define FPE_FLTOVF 4   /* EXCEPTION_FLT_OVERFLOW */
#define FPE_FLTUND 5   /* EXCEPTION_FLT_UNDERFLOW */
#define FPE_FLTRES 6   /* EXCEPTION_FLT_INEXACT_RESULT */
#define FPE_FLTINV 7   /* EXCEPTION_FLT_INVALID_OPERATION */

#define SEGV_MAPERR 1  /* EXCEPTION_ACCESS_VIOLATION/EXCEPTION_IN_PAGE_ERROR
                         * on unmapped/reserved-but-uncommitted memory */
#define SEGV_ACCERR 2  /* same, but on a committed page whose protection
                         * denied the access */

#define BUS_ADRALN 1   /* EXCEPTION_DATATYPE_MISALIGNMENT */

/* si_code values for SIGCHLD (waitid.html, basedefs/signal.h.html).
 * Unlike the fault subcodes above, all six are defined even though
 * ntlibc produces only three of them, and the distinction is not
 * arbitrary: a fault subcode is asked for by a handler already doing
 * platform-specific fault analysis, whereas CLD_* is the ordinary
 * vocabulary of wait-family status reporting, which any portable
 * SIGCHLD handler may switch over without doing anything
 * platform-specific.  A `switch (si_code)` covering all six is
 * guaranteed to compile by POSIX, and breaking that is a hard build
 * failure, not a graceful fallback.  Defining a value promises only
 * that it is a value si_code may hold, never that this system produces
 * it -- the same way defining EROFS promises no errno will ever be it.
 *
 * waitid() produces CLD_EXITED, CLD_KILLED and CLD_DUMPED.
 *
 * CLD_TRAPPED, CLD_STOPPED and CLD_CONTINUED are defined for source
 * compatibility and are never produced here, because no child on this
 * platform can be stopped or continued: kill(pid, SIGSTOP) is
 * NtTerminateProcess(h, __NT_SIGNAL_EXIT(SIGSTOP)) (src/signal/signal.c)
 * -- it ends the child rather than suspending it, NT having no job
 * control to suspend into -- and even a process suspended by other
 * means could not be reported, since an NT process object transitions
 * to signalled exactly once, on termination: there is no waitable stop
 * or continue transition for NtWaitForSingleObject to return, and
 * NtSuspendProcess is not part of the surface this library declares.
 * Numeric values match musl/glibc, as above. */
#define CLD_EXITED    1
#define CLD_KILLED    2
#define CLD_DUMPED    3
#define CLD_TRAPPED   4
#define CLD_STOPPED   5
#define CLD_CONTINUED 6

typedef struct sigaltstack stack_t;

#define SA_NOCLDSTOP  1
#define SA_NOCLDWAIT  2
#define SA_SIGINFO    4
#define SA_ONSTACK    0x08000000
#define SA_RESTART    0x10000000
#define SA_NODEFER    0x40000000
#define SA_RESETHAND  0x80000000
#define SA_RESTORER   0x04000000

#define MINSIGSTKSZ 2048
#define SIGSTKSZ 8192

union sigval {
	int sival_int;
	void *sival_ptr;
};

typedef struct {
	int si_signo, si_errno, si_code;
	union {
		char __pad[128 - 2*sizeof(int) - sizeof(long)];
		struct {
			union {
				struct {
					pid_t si_pid;
					uid_t si_uid;
				} __piduid;
				struct {
					int si_timerid;
					int si_overrun;
				} __timer;
			} __first;
			union {
				union sigval si_value;
				struct {
					int si_status;
					clock_t si_utime, si_stime;
				} __sigchld;
			} __second;
		} __si_common;
		struct {
			void *si_addr;
			short si_addr_lsb;
		} __sigfault;
		struct {
			long si_band;
			int si_fd;
		} __sigpoll;
	} __si_fields;
} siginfo_t;
#define si_pid     __si_fields.__si_common.__first.__piduid.si_pid
#define si_uid     __si_fields.__si_common.__first.__piduid.si_uid
#define si_status  __si_fields.__si_common.__second.__sigchld.si_status
#define si_utime   __si_fields.__si_common.__second.__sigchld.si_utime
#define si_stime   __si_fields.__si_common.__second.__sigchld.si_stime
#define si_value   __si_fields.__si_common.__second.si_value
#define si_addr    __si_fields.__sigfault.si_addr
#define si_band    __si_fields.__sigpoll.si_band
#define si_fd      __si_fields.__sigpoll.si_fd

struct sigaction {
	union {
		void (*sa_handler)(int);
		void (*sa_sigaction)(int, siginfo_t *, void *);
	} __sa_handler;
	sigset_t sa_mask;
	int sa_flags;
	void (*sa_restorer)(void);
};
#define sa_handler   __sa_handler.sa_handler
#define sa_sigaction __sa_handler.sa_sigaction

struct sigaltstack {
	void *ss_sp;
	int ss_flags;
	size_t ss_size;
};

struct sigevent {
	union sigval sigev_value;
	int sigev_signo;
	int sigev_notify;
	void (*sigev_notify_function)(union sigval);
	void *sigev_notify_attributes;
};

#define SIGEV_SIGNAL 0
#define SIGEV_NONE 1
#define SIGEV_THREAD 2

int __libc_current_sigrtmin(void);
int __libc_current_sigrtmax(void);

#define SIGRTMIN  (__libc_current_sigrtmin())
#define SIGRTMAX  (__libc_current_sigrtmax())

int kill(pid_t, int);

int sigemptyset(sigset_t *);
int sigfillset(sigset_t *);
int sigaddset(sigset_t *, int);
int sigdelset(sigset_t *, int);
int sigismember(const sigset_t *, int);

int sigprocmask(int, const sigset_t *__restrict, sigset_t *__restrict);
int sigsuspend(const sigset_t *);
int sigaction(int, const struct sigaction *__restrict, struct sigaction *__restrict);
int sigpending(sigset_t *);
int sigwait(const sigset_t *__restrict, int *__restrict);
int sigwaitinfo(const sigset_t *__restrict, siginfo_t *__restrict);  /* undefined-ok:
	src/signal/signal.c's sigwait() is already a permanent stub
	(errno = EINVAL) for the same reason this would be -- there is no
	per-process queue of pending signals with siginfo payloads to wait
	on. The library's own top-of-file comment in signal.c is explicit:
	"There is no asynchronous delivery from other processes"; the only
	signals that ever arrive are self-generated (raise()/abort()/a
	broken-pipe write) or synchronous CPU exceptions, both of which run
	the handler immediately rather than becoming something a separate
	wait call could pick up later. kernel32 has no POSIX-signal-queue
	primitive either -- this is a Windows/NT architecture gap, not an
	ntdll-vs-kernel32 one */
int sigtimedwait(const sigset_t *__restrict, siginfo_t *__restrict, const struct timespec *__restrict);  /* undefined-ok: see sigwaitinfo */
int sigqueue(pid_t, int, union sigval);  /* undefined-ok: needs the same
	per-process queued-signal-with-payload delivery sigwaitinfo() does
	(see above), plus the ability to deliver into *another* process,
	which this library's kill() (src/signal/signal.c) explicitly cannot
	do -- "kill() can only end a process, not interrupt it". Nothing
	kernel32 offers changes that: Windows simply has no cross-process
	POSIX-signal-with-data delivery mechanism to reach, from either
	layer */

int sigaltstack(const stack_t *__restrict, stack_t *__restrict);

void psiginfo(const siginfo_t *, const char *);

#endif

#if defined(_XOPEN_SOURCE) || defined(_BSD_SOURCE) || defined(_GNU_SOURCE)
#define NSIG _NSIG
typedef void (*sig_t)(int);
int killpg(pid_t, int);
int sigpause(int);
int siginterrupt(int, int);
int sighold(int);
int sigrelse(int);
void (*sigset(int, void (*)(int)))(int);
#define TRAP_BRKPT 1
#define TRAP_TRACE 2
#define POLL_IN 1
#define POLL_OUT 2
#define POLL_MSG 3
#define POLL_ERR 4
#define POLL_PRI 5
#define POLL_HUP 6
#define SS_ONSTACK    1
#define SS_DISABLE    2
#endif

#if defined(_BSD_SOURCE) || defined(_GNU_SOURCE)
typedef void (*sighandler_t)(int);
void psignal(int, const char *);
int sigisemptyset(const sigset_t *);
int sigorset (sigset_t *, const sigset_t *, const sigset_t *);

#define SA_NOMASK SA_NODEFER
#define SA_ONESHOT SA_RESETHAND
#endif

#define SIG_ERR  ((void (*)(int))-1)
#define SIG_DFL  ((void (*)(int)) 0)
#define SIG_IGN  ((void (*)(int)) 1)

typedef int sig_atomic_t;

void (*signal(int, void (*)(int)))(int);
int raise(int);

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGIOT    SIGABRT
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGSTKFLT 16
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
#define SIGWINCH  28
#define SIGIO     29
#define SIGPOLL   29
#define SIGPWR    30
#define SIGSYS    31
#define SIGUNUSED SIGSYS

#define _NSIG 65

#ifdef __cplusplus
}
#endif

#endif
