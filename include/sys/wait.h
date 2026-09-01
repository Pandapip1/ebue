/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef	_SYS_WAIT_H
#define	_SYS_WAIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_pid_t
#define __NEED_id_t
#include <bits/alltypes.h>

typedef enum {
	P_ALL = 0,
	P_PID = 1,
	P_PGID = 2,
	P_PIDFD = 3
} idtype_t;

pid_t wait (int *);
pid_t waitpid (pid_t, int *, int );

/* waitid.html.  <signal.h> for siginfo_t, which basedefs/sys_wait.h.html
 * requires this header to define "as described in <signal.h>".
 *
 * Guarded by the same feature-test set <signal.h> guards siginfo_t
 * itself with -- under -std=c99 with no feature-test macro set (which
 * defines __STRICT_ANSI__, so features.h defaults nothing on), that
 * type does not exist and this declaration must not either.  This is
 * what tools/hdr-hygiene.sh compiles for, and it is also what a strict
 * ISO C program gets: waitid() is not an ISO C interface. */
#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
#include <signal.h>
int waitid (idtype_t, id_t, siginfo_t *, int);
#endif

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#include <sys/resource.h>
pid_t wait3 (int *, int, struct rusage *);
pid_t wait4 (pid_t, int *, int, struct rusage *);
#endif

#define WNOHANG    1
#define WUNTRACED  2

#define WSTOPPED   2
#define WEXITED    4
#define WCONTINUED 8
#define WNOWAIT    0x1000000

#define __WNOTHREAD 0x20000000
#define __WALL      0x40000000
#define __WCLONE    0x80000000

#define WEXITSTATUS(s) (((s) & 0xff00) >> 8)
#define WTERMSIG(s) ((s) & 0x7f)
#define WSTOPSIG(s) WEXITSTATUS(s)
#define WCOREDUMP(s) ((s) & 0x80)
#define WIFEXITED(s) (!WTERMSIG(s))
#define WIFSTOPPED(s) ((short)((((unsigned)(s)&0xffff)*0x10001u)>>8) > 0x7f00)
#define WIFSIGNALED(s) (((s)&0xffff)-1U < 0xffu)
#define WIFCONTINUED(s) ((s) == 0xffff)

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
