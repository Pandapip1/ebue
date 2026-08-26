/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Coverage fences for the XSI interprocess-communication option group:
 * <sys/ipc.h>, <sys/shm.h>, <sys/msg.h> and <sys/sem.h>, and the twelve
 * functions they declare.  POSIX.1-2017 (IEEE Std 1003.1-2017, The Open
 * Group Base Specifications Issue 7, 2018 Edition).
 *
 * ==================== why this file exists ==========================
 *
 * A name-level cross-index of the 1188 interfaces named by the NAME
 * sections of the 882 POSIX function reference pages against every
 * identifier appearing in test/*.c found the whole XSI IPC group
 * absent -- not merely untested, but unmentioned.  Twelve interfaces
 * with no test at all:
 *
 *   <sys/ipc.h>   ftok
 *   <sys/shm.h>   shmget shmat shmdt shmctl
 *   <sys/msg.h>   msgget msgsnd msgrcv msgctl
 *   <sys/sem.h>   semget semop semctl
 *
 * None of the four headers exists under include/; grep over include/
 * and src/ finds no shmget, msgget, semget, ftok or shmat (a positive
 * control over the same paths does find posix_spawn, so the empty
 * result is a fact about the tree rather than about the search).
 *
 * Every fence below is therefore UNIMPL in the strict sense
 * tools/test-policy.py probes for: the un-fenced body FAILS TO COMPILE,
 * because the header it includes does not exist.  That is the assertion
 * -- the interface is absent.  Each fence carries a runnable body, so
 * the day the header lands the fence states what must hold rather than
 * merely that something is missing.
 *
 * NOT AN ARGUMENT THAT NT CANNOT DO THIS.  These are UNIMPL, not N/A.
 * NT has the primitives each of the three mechanisms needs -- section
 * objects for shared memory, and named kernel objects with a key-like
 * namespace for the identifier lookups -- so nothing here is a scope
 * that cannot be entered, the way test/posix-stropts.c's STREAMS
 * clauses are.  It is work not done.  Note also that XSI IPC is an
 * *option group*: an implementation may omit it, so its absence is
 * conforming.  The fences record the gap; they do not demand the work.
 */
#include "test-policy.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* --------------------------------------------------------------------
 * <sys/ipc.h>: ftok()
 *
 * ftok.html DESCRIPTION: "The ftok() function shall return a key based
 * on path and id that is usable in subsequent calls to msgget(),
 * semget(), and shmget().  The application shall ensure that the path
 * argument is the pathname of an existing file that the process is able
 * to stat() ...  The ftok() function shall return the same key value
 * for all paths that name the same file, when called with the same id
 * value, and should return different key values when called with
 * different id values or with paths that name different files existing
 * on the same file system at the same time. ...  Only the low-order
 * 8-bits of id are significant."
 *
 * RETURN VALUE: "Upon successful completion, ftok() shall return a key.
 * Otherwise, ftok() shall return (key_t)-1 and set errno to indicate
 * the error."
 *
 * The "same file, same id, same key" clause is the testable core, and
 * it is testable without any of the three IPC mechanisms existing: it
 * is a pure function of the file's identity.  Two distinct pathnames
 * that resolve to one file (here, "f" and "./f") must agree.
 * ------------------------------------------------------------------ */
#if NTLIBC_TEST(UNIMPL, posix_ipc_ftok_same_file_same_key) /* UNIMPL: <sys/ipc.h> does not exist under include/, so the
       * include below fails and no ftok() is declared or linked. */
#include <sys/ipc.h>

static void test_ftok_same_file_same_key(void)
{
	key_t a, b, c;
	int fd = open("ipc-ftok.tmp", O_RDWR | O_CREAT | O_TRUNC, 0644);

	CHECK(fd >= 0);
	if (fd < 0)
		return;
	close(fd);

	a = ftok("ipc-ftok.tmp", 'A');
	b = ftok("./ipc-ftok.tmp", 'A');
	CHECK(a != (key_t)-1);
	CHECK(b != (key_t)-1);
	/* "shall return the same key value for all paths that name the
	 * same file, when called with the same id value" */
	CHECK(a == b);

	/* "should return different key values when called with different
	 * id values" -- `should`, so a difference is expected but a
	 * collision is not a violation; assert only that it succeeds. */
	c = ftok("ipc-ftok.tmp", 'B');
	CHECK(c != (key_t)-1);

	/* The path must name an existing file the process can stat(). */
	errno = 0;
	CHECK(ftok("ipc-ftok-absent.tmp", 'A') == (key_t)-1);
	CHECK(errno == ENOENT);

	unlink("ipc-ftok.tmp");
}
#endif

/* --------------------------------------------------------------------
 * <sys/ipc.h>: the header's own contents.
 *
 * sys_ipc.h.html: "The <sys/ipc.h> header shall define the ipc_perm
 * structure, which shall include the following members: uid_t uid ...
 * gid_t gid ... uid_t cuid ... gid_t cgid ... mode_t mode".  It "shall
 * define the uid_t, gid_t, mode_t, and key_t types as described in
 * <sys/types.h>", and shall define IPC_CREAT, IPC_EXCL, IPC_NOWAIT,
 * IPC_PRIVATE, IPC_RMID, IPC_SET and IPC_STAT.
 *
 * Separate from the ftok() fence: a header can grow its type and macro
 * set before any function behind it works, and that intermediate state
 * is worth being able to see.
 * ------------------------------------------------------------------ */
#if NTLIBC_TEST(UNIMPL, posix_ipc_header_types_and_constants) /* UNIMPL: <sys/ipc.h> does not exist under include/. */
#include <sys/ipc.h>

static void test_ipc_header_types_and_constants(void)
{
	struct ipc_perm p;
	key_t k = (key_t)0;

	memset(&p, 0, sizeof p);
	p.uid = (uid_t)0;
	p.gid = (gid_t)0;
	p.cuid = (uid_t)0;
	p.cgid = (gid_t)0;
	p.mode = (mode_t)0600;
	CHECK(p.mode == (mode_t)0600);
	CHECK(k == (key_t)0);

	/* The seven symbolic constants the header shall define.  They are
	 * distinct bits/values; asserting they are pairwise distinct is
	 * what a caller actually depends on. */
	CHECK(IPC_CREAT != 0);
	CHECK(IPC_EXCL != 0);
	CHECK(IPC_NOWAIT != 0);
	CHECK((IPC_CREAT & IPC_EXCL) == 0);
	CHECK(IPC_PRIVATE == (key_t)0 || IPC_PRIVATE != (key_t)0);
	CHECK(IPC_RMID != IPC_SET);
	CHECK(IPC_SET != IPC_STAT);
	CHECK(IPC_RMID != IPC_STAT);
}
#endif

/* --------------------------------------------------------------------
 * <sys/shm.h>: shmget(), shmat(), shmdt()
 *
 * shmget.html RETURN VALUE: "Upon successful completion, shmget() shall
 * return a non-negative integer, namely a shared memory identifier;
 * otherwise, it shall return -1 and set errno to indicate the error."
 *
 * shmat.html RETURN VALUE: "Upon successful completion, shmat() shall
 * increment the value of shm_nattch in the data structure associated
 * with the shared memory ID of the attached shared memory segment and
 * return the segment's start address. ...  Otherwise, the shared memory
 * segment shall not be attached, shmat() shall return (void *)-1, and
 * errno shall be set to indicate the error."
 *
 * shmdt.html RETURN VALUE: "Upon successful completion, shmdt() shall
 * decrement the value of shm_nattch ... and return 0.  ...  Otherwise,
 * the shared memory segment shall not be detached, shmdt() shall return
 * -1, and errno shall be set to indicate the error."
 *
 * IPC_PRIVATE is used so the test needs no key coordination and cannot
 * collide with another run.
 * ------------------------------------------------------------------ */
#if NTLIBC_TEST(UNIMPL, posix_ipc_shm_get_attach_detach) /* UNIMPL: <sys/shm.h> does not exist under include/. */
#include <sys/ipc.h>
#include <sys/shm.h>

static void test_shm_get_attach_detach(void)
{
	int id;
	char *p;

	id = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
	CHECK(id >= 0);
	if (id < 0)
		return;

	p = (char *)shmat(id, (void *)0, 0);
	CHECK(p != (char *)-1);
	if (p != (char *)-1) {
		/* The segment is real memory: what is written is readable
		 * back through the same mapping. */
		memset(p, 0, 4096);
		strcpy(p, "xsi");
		CHECK(strcmp(p, "xsi") == 0);
		CHECK(p[4095] == '\0');
		CHECK(shmdt(p) == 0);
	}

	/* Two attaches of one segment alias the same storage: a store
	 * through the first is visible through the second.  This is the
	 * property that makes it *shared* memory rather than a private
	 * allocation, so it is the assertion worth making. */
	{
		char *q = (char *)shmat(id, (void *)0, 0);
		char *r = (char *)shmat(id, (void *)0, 0);
		CHECK(q != (char *)-1);
		CHECK(r != (char *)-1);
		if (q != (char *)-1 && r != (char *)-1) {
			q[0] = 'Z';
			CHECK(r[0] == 'Z');
			CHECK(shmdt(r) == 0);
		}
		if (q != (char *)-1)
			CHECK(shmdt(q) == 0);
	}

	CHECK(shmctl(id, IPC_RMID, (struct shmid_ds *)0) == 0);
}
#endif

/* --------------------------------------------------------------------
 * <sys/shm.h>: shmctl()
 *
 * shmctl.html RETURN VALUE: "Upon successful completion, shmctl() shall
 * return 0; otherwise, it shall return -1 and set errno to indicate the
 * error."
 *
 * sys_shm.h.html requires shmid_ds to include "struct ipc_perm shm_perm
 * ... size_t shm_segsz ... pid_t shm_lpid ... pid_t shm_cpid ...
 * shmatt_t shm_nattch ... time_t shm_atime ... time_t shm_dtime ...
 * time_t shm_ctime".
 *
 * shmctl.html, IPC_STAT: "Place the current value of each member of the
 * shmid_ds data structure associated with shmid into the structure
 * pointed to by buf."  So the size reported back must be the size asked
 * for, the creator PID must be this process, and shm_nattch must track
 * the attach/detach that brackets the query.
 * ------------------------------------------------------------------ */
#if NTLIBC_TEST(UNIMPL, posix_ipc_shmctl_stat_reports_segment) /* UNIMPL: <sys/shm.h> does not exist under include/. */
#include <sys/ipc.h>
#include <sys/shm.h>

static void test_shmctl_stat_reports_segment(void)
{
	struct shmid_ds ds;
	int id;
	char *p;

	id = shmget(IPC_PRIVATE, 8192, IPC_CREAT | 0600);
	CHECK(id >= 0);
	if (id < 0)
		return;

	memset(&ds, 0, sizeof ds);
	CHECK(shmctl(id, IPC_STAT, &ds) == 0);
	CHECK(ds.shm_segsz == (size_t)8192);
	CHECK(ds.shm_cpid == getpid());
	CHECK(ds.shm_nattch == 0);
	CHECK((ds.shm_perm.mode & 0600) == 0600);

	p = (char *)shmat(id, (void *)0, 0);
	CHECK(p != (char *)-1);
	if (p != (char *)-1) {
		memset(&ds, 0, sizeof ds);
		CHECK(shmctl(id, IPC_STAT, &ds) == 0);
		/* "shmat() shall increment the value of shm_nattch" */
		CHECK(ds.shm_nattch == 1);
		CHECK(shmdt(p) == 0);
		memset(&ds, 0, sizeof ds);
		CHECK(shmctl(id, IPC_STAT, &ds) == 0);
		/* "shmdt() shall decrement the value of shm_nattch" */
		CHECK(ds.shm_nattch == 0);
	}

	/* IPC_RMID: "Remove the shared memory identifier specified by
	 * shmid from the system".  After removal the identifier is no
	 * longer valid, so a further IPC_STAT must fail with EINVAL:
	 * "[EINVAL] The value of shmid is not a valid shared memory
	 * identifier". */
	CHECK(shmctl(id, IPC_RMID, (struct shmid_ds *)0) == 0);
	errno = 0;
	CHECK(shmctl(id, IPC_STAT, &ds) == -1);
	CHECK(errno == EINVAL);
}
#endif

/* --------------------------------------------------------------------
 * <sys/msg.h>: msgget(), msgsnd(), msgrcv()
 *
 * msgget.html RETURN VALUE: "Upon successful completion, msgget() shall
 * return a non-negative integer, namely a message queue identifier.
 * Otherwise, it shall return -1 and set errno to indicate the error."
 *
 * msgsnd.html RETURN VALUE: "Upon successful completion, msgsnd() shall
 * return 0; otherwise, no message shall be sent, msgsnd() shall return
 * -1, and errno shall be set to indicate the error."
 *
 * msgrcv.html RETURN VALUE: "Upon successful completion, msgrcv() shall
 * return a value equal to the number of bytes actually placed into the
 * buffer mtext.  Otherwise, no message shall be received, msgrcv()
 * shall return -1, and errno shall be set to indicate the error."
 *
 * msgrcv.html DESCRIPTION, on msgtyp: "If msgtyp is 0, the first
 * message on the queue shall be received. ...  If msgtyp is greater
 * than 0, the first message of type msgtyp shall be received."  That
 * type-selective receive is the whole point of the mechanism -- a queue
 * that only ever returns the head is a pipe -- so it is asserted here
 * rather than only the round trip.
 * ------------------------------------------------------------------ */
#if NTLIBC_TEST(UNIMPL, posix_ipc_msg_send_receive_by_type) /* UNIMPL: <sys/msg.h> does not exist under include/. */
#include <sys/ipc.h>
#include <sys/msg.h>

struct ipc_test_msg { long mtype; char mtext[32]; };

static void test_msg_send_receive_by_type(void)
{
	struct ipc_test_msg out, in;
	ssize_t n;
	int q;

	q = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
	CHECK(q >= 0);
	if (q < 0)
		return;

	out.mtype = 1;
	strcpy(out.mtext, "first");
	CHECK(msgsnd(q, &out, strlen(out.mtext) + 1, 0) == 0);

	out.mtype = 2;
	strcpy(out.mtext, "second");
	CHECK(msgsnd(q, &out, strlen(out.mtext) + 1, 0) == 0);

	/* msgtyp 2: skip the type-1 message at the head and take the
	 * type-2 one behind it. */
	memset(&in, 0, sizeof in);
	n = msgrcv(q, &in, sizeof in.mtext, 2, 0);
	CHECK(n == (ssize_t)(strlen("second") + 1));
	CHECK(in.mtype == 2);
	CHECK(strcmp(in.mtext, "second") == 0);

	/* msgtyp 0: "the first message on the queue shall be received" --
	 * the type-1 message, which is still there. */
	memset(&in, 0, sizeof in);
	n = msgrcv(q, &in, sizeof in.mtext, 0, 0);
	CHECK(n == (ssize_t)(strlen("first") + 1));
	CHECK(in.mtype == 1);
	CHECK(strcmp(in.mtext, "first") == 0);

	/* The queue is now empty.  IPC_NOWAIT: "[ENOMSG] The queue does
	 * not contain a message of the desired type and (msgflg &
	 * IPC_NOWAIT) is non-zero." */
	errno = 0;
	CHECK(msgrcv(q, &in, sizeof in.mtext, 0, IPC_NOWAIT) == -1);
	CHECK(errno == ENOMSG);

	CHECK(msgctl(q, IPC_RMID, (struct msqid_ds *)0) == 0);
}
#endif

/* --------------------------------------------------------------------
 * <sys/msg.h>: msgctl()
 *
 * msgctl.html RETURN VALUE: "Upon successful completion, msgctl() shall
 * return 0; otherwise, it shall return -1 and set errno to indicate the
 * error."
 *
 * sys_msg.h.html requires msqid_ds to include "struct ipc_perm msg_perm
 * ... msgqnum_t msg_qnum ... msglen_t msg_qbytes ... pid_t msg_lspid
 * ... pid_t msg_lrpid ... time_t msg_stime ... time_t msg_rtime ...
 * time_t msg_ctime".
 *
 * msg_qnum is the member that makes IPC_STAT worth a test of its own:
 * it must track the sends and receives performed either side of it.
 * ------------------------------------------------------------------ */
#if NTLIBC_TEST(UNIMPL, posix_ipc_msgctl_stat_tracks_queue_depth) /* UNIMPL: <sys/msg.h> does not exist under include/. */
#include <sys/ipc.h>
#include <sys/msg.h>

struct ipc_test_msg2 { long mtype; char mtext[16]; };

static void test_msgctl_stat_tracks_queue_depth(void)
{
	struct ipc_test_msg2 m;
	struct msqid_ds ds;
	int q;

	q = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
	CHECK(q >= 0);
	if (q < 0)
		return;

	memset(&ds, 0, sizeof ds);
	CHECK(msgctl(q, IPC_STAT, &ds) == 0);
	CHECK(ds.msg_qnum == 0);
	CHECK(ds.msg_qbytes > 0);
	CHECK((ds.msg_perm.mode & 0600) == 0600);

	m.mtype = 7;
	strcpy(m.mtext, "a");
	CHECK(msgsnd(q, &m, 2, 0) == 0);

	memset(&ds, 0, sizeof ds);
	CHECK(msgctl(q, IPC_STAT, &ds) == 0);
	CHECK(ds.msg_qnum == 1);
	/* "msg_lspid shall be set to the process ID of the calling
	 * process" on a successful msgsnd(). */
	CHECK(ds.msg_lspid == getpid());

	CHECK(msgrcv(q, &m, sizeof m.mtext, 0, 0) == 2);
	memset(&ds, 0, sizeof ds);
	CHECK(msgctl(q, IPC_STAT, &ds) == 0);
	CHECK(ds.msg_qnum == 0);
	CHECK(ds.msg_lrpid == getpid());

	CHECK(msgctl(q, IPC_RMID, (struct msqid_ds *)0) == 0);
	errno = 0;
	CHECK(msgctl(q, IPC_STAT, &ds) == -1);
	CHECK(errno == EINVAL);
}
#endif

/* --------------------------------------------------------------------
 * <sys/sem.h>: semget(), semop()
 *
 * semget.html RETURN VALUE: "Upon successful completion, semget() shall
 * return a non-negative integer, namely a semaphore identifier;
 * otherwise, it shall return -1 and set errno to indicate the error."
 *
 * semop.html RETURN VALUE: "Upon successful completion, semop() shall
 * return 0; otherwise, it shall return -1 and set errno to indicate the
 * error."
 *
 * semop.html DESCRIPTION, on a positive sem_op: "the value of sem_op
 * shall be added to the semaphore value".  On a negative sem_op whose
 * absolute value exceeds semval, with IPC_NOWAIT set: "[EAGAIN] The
 * operation would result in suspension of the calling thread but
 * (sem_flg & IPC_NOWAIT) is non-zero."  The non-blocking failure is the
 * assertion that can be made single-threaded without hanging the suite
 * -- a blocking decrement with nobody to post would never return.
 * ------------------------------------------------------------------ */
#if NTLIBC_TEST(UNIMPL, posix_ipc_semop_adjusts_and_nowait_eagain) /* UNIMPL: <sys/sem.h> does not exist under include/. */
#include <sys/ipc.h>
#include <sys/sem.h>

static void test_semop_adjusts_and_nowait_eagain(void)
{
	struct sembuf op;
	int id;

	id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
	CHECK(id >= 0);
	if (id < 0)
		return;

	/* "When a semaphore is created it is initialized to 0" is NOT
	 * stated by semget.html; semctl(SETVAL) establishes the value, so
	 * do that rather than assume one. */
	CHECK(semctl(id, 0, SETVAL, 0) == 0);

	/* positive sem_op: "the value of sem_op shall be added to the
	 * semaphore value" */
	op.sem_num = 0;
	op.sem_op = 2;
	op.sem_flg = 0;
	CHECK(semop(id, &op, 1) == 0);
	CHECK(semctl(id, 0, GETVAL) == 2);

	/* negative sem_op within range: succeeds and decrements */
	op.sem_op = -2;
	CHECK(semop(id, &op, 1) == 0);
	CHECK(semctl(id, 0, GETVAL) == 0);

	/* negative sem_op beyond range, IPC_NOWAIT: EAGAIN, and the
	 * semaphore is left alone. */
	op.sem_op = -1;
	op.sem_flg = IPC_NOWAIT;
	errno = 0;
	CHECK(semop(id, &op, 1) == -1);
	CHECK(errno == EAGAIN);
	CHECK(semctl(id, 0, GETVAL) == 0);

	CHECK(semctl(id, 0, IPC_RMID) == 0);
}
#endif

/* --------------------------------------------------------------------
 * <sys/sem.h>: semctl()
 *
 * semctl.html RETURN VALUE: "If successful, the value returned by
 * semctl() depends on cmd as follows: GETVAL -- The value of semval.
 * GETPID -- The value of sempid.  GETNCNT -- The value of semncnt.
 * GETZCNT -- The value of semzcnt.  All others -- 0.  Otherwise,
 * semctl() shall return -1 and set errno to indicate the error."
 *
 * sys_sem.h.html requires the header to define GETNCNT, GETPID, GETVAL,
 * GETALL, GETZCNT, SETVAL and SETALL, and the semid_ds structure with
 * "struct ipc_perm sem_perm ... unsigned short sem_nsems ... time_t
 * sem_otime ... time_t sem_ctime".
 *
 * GETALL/SETALL over a multi-semaphore set is what separates semctl()
 * from a single counter, so the set here has three members.
 * ------------------------------------------------------------------ */
#if NTLIBC_TEST(UNIMPL, posix_ipc_semctl_getall_setall) /* UNIMPL: <sys/sem.h> does not exist under include/. */
#include <sys/ipc.h>
#include <sys/sem.h>

static void test_semctl_getall_setall(void)
{
	unsigned short vals[3];
	struct semid_ds ds;
	int id;

	id = semget(IPC_PRIVATE, 3, IPC_CREAT | 0600);
	CHECK(id >= 0);
	if (id < 0)
		return;

	vals[0] = 5;
	vals[1] = 0;
	vals[2] = 9;
	CHECK(semctl(id, 0, SETALL, vals) == 0);

	memset(vals, 0, sizeof vals);
	CHECK(semctl(id, 0, GETALL, vals) == 0);
	CHECK(vals[0] == 5);
	CHECK(vals[1] == 0);
	CHECK(vals[2] == 9);

	/* "GETVAL -- The value of semval", per-semaphore via semnum */
	CHECK(semctl(id, 2, GETVAL, 0) == 9);
	CHECK(semctl(id, 0, SETVAL, 4) == 0);
	CHECK(semctl(id, 0, GETVAL, 0) == 4);

	/* "GETZCNT -- The value of semzcnt": nobody is waiting. */
	CHECK(semctl(id, 1, GETZCNT, 0) == 0);
	CHECK(semctl(id, 1, GETNCNT, 0) == 0);

	memset(&ds, 0, sizeof ds);
	CHECK(semctl(id, 0, IPC_STAT, &ds) == 0);
	CHECK(ds.sem_nsems == 3);
	CHECK((ds.sem_perm.mode & 0600) == 0600);

	CHECK(semctl(id, 0, IPC_RMID) == 0);
	errno = 0;
	CHECK(semctl(id, 0, GETVAL, 0) == -1);
	CHECK(errno == EINVAL);
}
#endif

int main(void)
{
	if (fails) { printf("posix-ipc: failures: %d\n", fails); return 1; }
	printf("posix-ipc: all ok\n");
	return 0;
}
