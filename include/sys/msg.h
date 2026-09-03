/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/msg.h> -- XSI message queues.
 *
 * Not to be confused with <mqueue.h>'s POSIX message queues: this is the
 * older, keyed-by-integer-identifier XSI mechanism, with its own distinct
 * API -- type-selective msgrcv() (msgtyp) rather than priority-ordered
 * mq_receive(), and a caller-defined message shape (any struct beginning
 * with `long mtype`) rather than a fixed max size negotiated at open time.
 *
 * On Linux, the message buffer's `long mtype` + trailing bytes shape is
 * the exact kernel struct msgbuf ABI, so it crosses the syscall boundary
 * unmarshalled (src/ipc/linux/plat_sysvipc.c). src/ipc/nt/plat_msg.c is a
 * genuine emulation storing each queue's slot table in a shared
 * backing-file record under one named NT mutant.
 */
#ifndef _SYS_MSG_H
#define _SYS_MSG_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <sys/ipc.h>

#define __NEED_pid_t
#define __NEED_time_t
#define __NEED_size_t
#define __NEED_ssize_t

#include <bits/alltypes.h>

typedef unsigned long msgqnum_t;
typedef unsigned long msglen_t;

struct msqid_ds {
	struct ipc_perm msg_perm;
	msgqnum_t msg_qnum;
	msglen_t msg_qbytes;
	pid_t msg_lspid;
	pid_t msg_lrpid;
	time_t msg_stime;
	time_t msg_rtime;
	time_t msg_ctime;
};

/* msgrcv() msgflg; pairs with IPC_NOWAIT (<sys/ipc.h>). */
#define MSG_NOERROR 010000

int msgctl(int, int, struct msqid_ds *);
int msgget(key_t, int);
ssize_t msgrcv(int, void *, size_t, long, int);
int msgsnd(int, const void *, size_t, int);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
