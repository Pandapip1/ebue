/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/msg.h> -- XSI message queues:
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/sys_msg.h.html
 *
 * Not to be confused with <mqueue.h>'s POSIX message queues (src/thread/
 * mqueue.c): this is the older, keyed-by-integer-identifier XSI
 * mechanism, with its own distinct API -- type-selective msgrcv()
 * (msgtyp) rather than mqueue.h's priority-ordered mq_receive(), and a
 * message shape the caller defines itself (any struct beginning with
 * `long mtype`) rather than a fixed max message size negotiated at
 * mq_open() time.
 *
 * msgget()/msgsnd()/msgrcv()/msgctl() are implemented once per OS
 * backend: src/ipc/linux/plat_sysvipc.c over the real msgget(2)/
 * msgsnd(2)/msgrcv(2)/msgctl(2) syscalls (the message buffer's
 * `long mtype` + trailing bytes shape is the exact kernel struct msgbuf
 * ABI, so it crosses the syscall boundary unmarshalled), and
 * src/ipc/nt/plat_msg.c, a genuine emulation storing each queue's slot
 * table directly in a shared backing-file record (the same private-
 * namespace-directory-plus-backing-file technique src/ipc/nt/plat_shm.c
 * and src/ipc/nt/plat_sem.c both use) under one named NT mutant -- see
 * plat_msg.c's own banner for why msgrcv()'s type-selective receive is
 * a poll-and-retry lock holder rather than a semaphore wait the way
 * src/thread/mqueue.c's own always-take-the-head model can be.
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

/* sys_msg.h.html: "msgqnum_t ... msglen_t ... Unsigned integer types used
 * for the number of messages on a message queue and the number of bytes
 * allowed on a message queue, respectively." */
typedef unsigned long msgqnum_t;
typedef unsigned long msglen_t;

/* sys_msg.h.html DESCRIPTION: the msqid_ds structure "shall include the
 * following members: struct ipc_perm msg_perm ... msgqnum_t msg_qnum ...
 * msglen_t msg_qbytes ... pid_t msg_lspid ... pid_t msg_lrpid ... time_t
 * msg_stime ... time_t msg_rtime ... time_t msg_ctime". */
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

/* msgrcv() msgflg: "[ENOMSG] ... ENOMSG" pairs with IPC_NOWAIT
 * (<sys/ipc.h>); MSG_NOERROR is the one msgrcv()-specific flag
 * sys_msg.h.html itself requires the header to define. */
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
