/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * select()/pselect() (https://pubs.opengroup.org/onlinepubs/
 * 9699919799/functions/select.html) and poll() (.../poll.html) applied
 * to a *socket*: that readiness actually tracks the socket's state,
 * rather than being asserted unconditionally.
 *
 * *** The defect this is the regression assertion for. ***
 *
 * src/select/select.c's poll_pass() and src/select/poll.c's loop each
 * called __fd_probe() only for f->type == __FD_PIPE, and dropped every
 * other descriptor type -- sockets among them -- into the branch that
 * reports "always ready" for both reading and writing.  That branch is
 * correct for a regular file (select.html DESCRIPTION: "File
 * descriptors associated with regular files shall always select true
 * for ready to read, ready to write") and it is defensible for a
 * character device, but it is simply false for a socket, which is
 * exactly the descriptor shape select() and poll() exist to wait on.
 * A program that created a socket and selected on it was told, always
 * and immediately, "readable and writable" -- so `while (select(...))
 * recv(...)` spun, and a select()-driven server treated an idle
 * connection as one with a request pending.
 *
 * __fd_probe()'s __FD_SOCKET case had been correct the whole time; it
 * was unreachable.  That is also why the IOCTL_AFD_SELECT
 * Handles-at-+16 layout defect (test/posix-socket-poll.c) was latent
 * rather than observed: a program that socket()d and then select()d
 * printed the same answer under the right layout and the wrong one,
 * because neither run issued the ioctl at all.
 *
 * So the assertions that matter most here are the *negative* ones --
 * an idle socket is NOT reported ready.  Under the old routing every
 * one of them fails; they are also what would catch the probe silently
 * breaking in future, since __fd_probe() deliberately falls back to
 * "ready, and hung up" when its ioctl fails (see the comment there for
 * why never-ready would be worse).  A positive-only test would pass
 * against a probe that had stopped working, and against no probe at
 * all.
 *
 * Environments (test/wine-ci-evaluation.md, test/networking-audit.md
 * sec 1).  Everything below needs a \Device\Afd endpoint that answers
 * real AFD ioctls:
 *
 *   - Stock Wine (CI's `test` legs): its AFD only wires up a handle
 *     opened via its own invented IOCTL_AFD_WINE_CREATE, so the
 *     portable ReactOS/real-Windows open this library uses gets a
 *     handle nothing is behind and the first ioctl (bind) fails.
 *   - This project's patched Wine (~/Projects/wine): accepts the
 *     portable open, then returns STATUS_NOT_IMPLEMENTED for the
 *     TDI-mode bind.
 *   - Real Windows (CI's `windows-test` legs): works, and is the
 *     authority for everything in this file.
 *
 * Rather than fail where the platform cannot answer, this follows
 * test/posix-socket.c exactly: a runtime capability probe
 * (socket()+bind()+listen()) gates every network assertion, a failure
 * prints one SKIP line naming the call and its errno, and the exit
 * code is 77 -- tools/run-tests.py's third bucket, "ran, verified
 * nothing new", which is neither a pass nor a failure.  A plain
 * `return 0` there would report PASS, forever and on every platform,
 * for a fix nothing had checked.
 *
 * Both call sites are covered separately throughout.  select() and
 * poll() deliberately do not share an outer loop (see
 * src/select/select.c's banner for why), so they are two independent
 * places for this defect to live, and the original had it in both.
 */
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>

static int fails;
/* Assertion groups that did not run because this environment has no
 * working AFD; see the banner and main()'s tail. */
static int unverified;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Distinct from test/posix-socket.c's 55123: tools/run-tests.py runs the
 * non-serial tests in parallel (xargs -P), so two loopback tests
 * sharing a fixed port would collide with each other rather than with
 * anything on the machine.  A fixed port in the dynamic/private range
 * (RFC 6335) is the same trade-off posix-socket.c makes, and for the
 * same reason it still makes it now that getsockname() exists: see that
 * file's TEST_PORT comment for why the ephemeral-port form was not
 * adopted. */
#define TEST_PORT 55137

/* Generous, because these are only ever waited on when the assertion
 * is that something *does* become ready; the negative assertions all
 * use a zero timeout, where any wait at all would be wrong. */
#define WAIT_MS 5000

static int make_loopback_addr(struct sockaddr_in *a)
{
	memset(a, 0, sizeof *a);
	a->sin_family = AF_INET;
	a->sin_port = htons(TEST_PORT);
	a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	return sizeof *a;
}

/* ---- the two calls under test, wrapped so each assertion reads as
 * one question about one descriptor ------------------------------- */

/* select() for readability with the given timeout in milliseconds.
 * Returns select()'s own return value; *isset receives whether the
 * descriptor's bit came back set.  Both are checked at every call
 * site: a count and a bit that disagree would be its own bug. */
static int sel_read(int fd, int ms, int *isset)
{
	fd_set r;
	struct timeval tv;
	int n;

	FD_ZERO(&r);
	FD_SET(fd, &r);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	n = select(fd + 1, &r, 0, 0, &tv);
	*isset = n > 0 && FD_ISSET(fd, &r);
	return n;
}

static int sel_write(int fd, int ms, int *isset)
{
	fd_set w;
	struct timeval tv;
	int n;

	FD_ZERO(&w);
	FD_SET(fd, &w);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	n = select(fd + 1, 0, &w, 0, &tv);
	*isset = n > 0 && FD_ISSET(fd, &w);
	return n;
}

/* poll() for one descriptor.  Returns poll()'s return value; *revents
 * receives the reported events. */
static int poll_one(int fd, short events, int ms, short *revents)
{
	struct pollfd p;
	int n;

	p.fd = fd;
	p.events = events;
	/* Poisoned, not zeroed.  poll.html DESCRIPTION: "In each pollfd
	 * structure, poll() shall clear the revents member" -- so an
	 * implementation that leaves revents alone is non-conforming, and
	 * every `CHECK(revents == 0)` in this file is a check of exactly
	 * that clause.  Seeding 0 here made those checks unfalsifiable:
	 * they read back the value this helper had just written, so they
	 * passed identically whether or not poll() touched the field. */
	p.revents = -1;
	n = poll(&p, 1, ms);
	*revents = p.revents;
	return n;
}

/* ---- the capability probe (see the banner) ---------------------- */

static int network_probe(void)
{
	struct sockaddr_in addr;
	int s;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		printf("SKIP posix-select-socket (socket() failed, errno=%d)\n", errno);
		unverified++;
		return -1;
	}
	if (bind(s, (struct sockaddr *)&addr, make_loopback_addr(&addr)) < 0) {
		printf("SKIP posix-select-socket (bind() failed, errno=%d; "
		       "IOCTL_AFD_BIND on a \\Device\\Afd\\Endpoint handle -- "
		       "see test/posix-socket.c and test/networking-audit.md sec 1)\n",
		       errno);
		close(s);
		unverified++;
		return -1;
	}
	if (listen(s, 1) < 0) {
		printf("SKIP posix-select-socket (listen() failed, errno=%d)\n", errno);
		close(s);
		unverified++;
		return -1;
	}
	return s;
}

/* ---- 1. the core negative: an idle socket is not ready ---------- *
 *
 * A listening socket with nothing connecting to it, and a brand new
 * socket that was never connected at all.  Neither has anything to
 * read, on any platform, under any interpretation -- so a zero timeout
 * ("to effect a poll", select.html DESCRIPTION) must return 0 with the
 * bit clear.  The old code returned 1 with the bit set for both, which
 * is the whole defect in one assertion.
 *
 * Readability only.  Whether an unconnected socket is *writable* is a
 * genuinely platform-dependent question (AFD sets AFD_EVENT_SEND on
 * connect completion -- ReactOS drivers/network/afd/afd/connect.c --
 * but this file has no authority for what a never-connected endpoint
 * reports), and asserting a guess would make this test a liability on
 * the one platform that can answer it. */
static void test_idle_socket_not_readable(int listener)
{
	int isset = -1, fresh;
	short revents = -1;

	/* select(): the listener, idle. */
	CHECK(sel_read(listener, 0, &isset) == 0);
	CHECK(isset == 0);

	/* poll(): the same, separately -- a different call site. */
	CHECK(poll_one(listener, POLLIN, 0, &revents) == 0);
	CHECK(revents == 0);

	/* And a socket that has never been bound, connected or listened
	 * on: nothing has ever been able to send it a byte. */
	fresh = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(fresh >= 0);
	if (fresh < 0) return;

	isset = -1;
	CHECK(sel_read(fresh, 0, &isset) == 0);
	CHECK(isset == 0);

	revents = -1;
	CHECK(poll_one(fresh, POLLIN, 0, &revents) == 0);
	CHECK(revents == 0);

	close(fresh);
}

/* ---- 2. a pending connection makes the listener readable -------- *
 *
 * select.html DESCRIPTION: a listening socket is "ready to read" when
 * a connection is pending, i.e. when accept() would not block.  This
 * is the positive half of the pair -- on its own it would pass against
 * the old always-ready code, which is exactly why test 1 above comes
 * first.
 *
 * Returns the connected client fd, or -1. */
static int test_pending_connection_readable(int listener)
{
	struct sockaddr_in addr;
	int client, isset = -1;
	short revents = -1;

	client = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(client >= 0);
	if (client < 0) return -1;

	/* connect.html: a stream connect() completes once the peer's
	 * backlog can hold it, without accept() having run -- so this is
	 * sequential, not a race (same reasoning as
	 * test/posix-socket.c's round trip). */
	CHECK(connect(client, (struct sockaddr *)&addr, make_loopback_addr(&addr)) == 0);

	CHECK(sel_read(listener, WAIT_MS, &isset) == 1);
	CHECK(isset == 1);

	/* Still pending -- accept() has not run -- so poll() must agree. */
	revents = 0;
	CHECK(poll_one(listener, POLLIN, WAIT_MS, &revents) == 1);
	CHECK((revents & POLLIN) != 0);

	return client;
}

/* ---- 3. a connected socket with no data pending ----------------- *
 *
 * The single most load-bearing case for a select()-driven server: the
 * connection is up, so it is writable, but the peer has not said
 * anything, so it is NOT readable.  The old code reported it readable,
 * and a server built on that reads a request that was never sent.
 *
 * Writability is asserted here (unlike for the never-connected socket
 * in test 1) because AFD's behaviour on this one is documented in the
 * driver source this library's request format follows: ReactOS
 * drivers/network/afd/afd/connect.c sets AFD_EVENT_SEND alongside
 * AFD_EVENT_CONNECT when a connection completes, and its AfdPoll()
 * (select.c) reports PollState without clearing it, so the bit is
 * level-triggered rather than one-shot.  __fd_probe() maps
 * AFD_EVENT_SEND|CONNECT|CONNECT_FAIL to "writable". */
static void test_connected_idle(int accepted)
{
	int isset = -1;
	short revents = -1;

	CHECK(sel_read(accepted, 0, &isset) == 0);
	CHECK(isset == 0);

	revents = -1;
	CHECK(poll_one(accepted, POLLIN, 0, &revents) == 0);
	CHECK(revents == 0);

	isset = -1;
	CHECK(sel_write(accepted, WAIT_MS, &isset) == 1);
	CHECK(isset == 1);

	revents = 0;
	CHECK(poll_one(accepted, POLLOUT, WAIT_MS, &revents) == 1);
	CHECK((revents & POLLOUT) != 0);
}

/* ---- 4. data pending, then drained ------------------------------ *
 *
 * The full cycle, which is what makes the pair of answers meaningful
 * rather than two unrelated constants: the same descriptor is not
 * readable, becomes readable when four bytes arrive, and stops being
 * readable once they are consumed.  No always-ready implementation and
 * no never-ready one can produce all three. */
static void test_data_arrives_and_drains(int client, int accepted)
{
	char buf[32];
	int isset = -1;
	short revents = 0;

	CHECK(send(client, "ping", 4, 0) == 4);

	CHECK(sel_read(accepted, WAIT_MS, &isset) == 1);
	CHECK(isset == 1);
	CHECK(poll_one(accepted, POLLIN, WAIT_MS, &revents) == 1);
	CHECK((revents & POLLIN) != 0);

	memset(buf, 0, sizeof buf);
	CHECK(recv(accepted, buf, sizeof buf, 0) == 4);
	CHECK(!memcmp(buf, "ping", 4));

	/* Drained: back to not readable, and this time it is a
	 * descriptor that demonstrably *was* readable a moment ago, so a
	 * stuck "ready" latch fails here too. */
	isset = -1;
	CHECK(sel_read(accepted, 0, &isset) == 0);
	CHECK(isset == 0);
	revents = -1;
	CHECK(poll_one(accepted, POLLIN, 0, &revents) == 0);
	CHECK(revents == 0);

	/* Once more in the other direction, so neither socket of the
	 * pair is only ever tested as a receiver. */
	CHECK(send(accepted, "pong!", 5, 0) == 5);
	isset = -1;
	CHECK(sel_read(client, WAIT_MS, &isset) == 1);
	CHECK(isset == 1);
	memset(buf, 0, sizeof buf);
	CHECK(recv(client, buf, sizeof buf, 0) == 5);
	CHECK(!memcmp(buf, "pong!", 5));
}

/* ---- 5. a socket in the same set as other descriptor shapes ----- *
 *
 * poll_pass() builds one answer for a whole fd_set, so the interesting
 * failure is not just "a socket alone is wrong" but "a socket inflates
 * the count and corrupts the answer for everything beside it".  A pipe
 * is the right neighbour: it is the one shape that was already probed
 * correctly, so if the socket's bit leaks the contrast is unambiguous.
 *
 * Both are idle first (select must return 0, not 1), then the pipe --
 * and only the pipe -- is made readable. */
static void test_mixed_set(int idle_sock)
{
	int fds[2], nfds, n;
	fd_set r;
	struct timeval tv;
	struct pollfd pfd[2];

	if (pipe(fds) < 0) { CHECK(0); return; }
	nfds = (fds[0] > idle_sock ? fds[0] : idle_sock) + 1;

	/* Neither ready: an empty pipe has no data, the socket is idle. */
	FD_ZERO(&r); FD_SET(fds[0], &r); FD_SET(idle_sock, &r);
	tv.tv_sec = 0; tv.tv_usec = 0;
	CHECK(select(nfds, &r, 0, 0, &tv) == 0);
	CHECK(!FD_ISSET(fds[0], &r));
	CHECK(!FD_ISSET(idle_sock, &r));

	pfd[0].fd = fds[0]; pfd[0].events = POLLIN; pfd[0].revents = -1;
	pfd[1].fd = idle_sock; pfd[1].events = POLLIN; pfd[1].revents = -1;
	CHECK(poll(pfd, 2, 0) == 0);
	CHECK(pfd[0].revents == 0);
	CHECK(pfd[1].revents == 0);

	/* Now exactly one of them is readable, and the count must say
	 * one -- not two. */
	CHECK(write(fds[1], "x", 1) == 1);

	FD_ZERO(&r); FD_SET(fds[0], &r); FD_SET(idle_sock, &r);
	tv.tv_sec = WAIT_MS / 1000; tv.tv_usec = 0;
	n = select(nfds, &r, 0, 0, &tv);
	CHECK(n == 1);
	CHECK(FD_ISSET(fds[0], &r));
	CHECK(!FD_ISSET(idle_sock, &r));

	pfd[0].fd = fds[0]; pfd[0].events = POLLIN; pfd[0].revents = 0;
	pfd[1].fd = idle_sock; pfd[1].events = POLLIN; pfd[1].revents = -1;
	CHECK(poll(pfd, 2, WAIT_MS) == 1);
	CHECK((pfd[0].revents & POLLIN) != 0);
	CHECK(pfd[1].revents == 0);

	{
		char c = 0;
		CHECK(read(fds[0], &c, 1) == 1);
	}
	close(fds[0]);
	close(fds[1]);
}

/* ---- 6. end of stream --------------------------------------------- *
 *
 * recv.html: a peer's orderly shutdown makes recv() return 0 -- which
 * is a return, not a block, so the descriptor is "ready to read"
 * (select.html DESCRIPTION: ready means the call "will not block").
 * This is the one case where the old always-ready answer happened to
 * be right, and it is asserted so that fixing the general case did not
 * break it: a probe that reported an EOF socket as "not readable"
 * would hang a normal read loop at exactly its last iteration.
 *
 * POLLHUP is asserted only as "not spuriously absent when POLLIN is
 * also absent": __fd_probe() sets *hup for AFD's CLOSE/ABORT/
 * DISCONNECT events, but which of those a half-close raises is AFD's
 * business, so what is required here is that poll() reports *some*
 * reason the descriptor is ready, and that recv() then agrees by
 * returning 0. */
static void test_eof_is_readable(int client, int accepted)
{
	char buf[8];
	int isset = -1;
	short revents = 0;

	CHECK(shutdown(client, SHUT_WR) == 0);

	CHECK(sel_read(accepted, WAIT_MS, &isset) == 1);
	CHECK(isset == 1);

	CHECK(poll_one(accepted, POLLIN, WAIT_MS, &revents) == 1);
	CHECK((revents & (POLLIN | POLLHUP)) != 0);

	/* ...and the readiness was honest: recv() returns rather than
	 * blocking, with recv.html's orderly-shutdown 0. */
	CHECK(recv(accepted, buf, sizeof buf, 0) == 0);
}

int main(void)
{
	int listener, client, accepted;
	struct sockaddr_in peer;
	socklen_t peerlen;

	/* send.html: a send on a shut-down socket may raise SIGPIPE.
	 * Nothing here does that deliberately, but test 6 shuts a
	 * direction down, so this keeps an unexpected one from being
	 * mistaken for a select() bug. */
	signal(SIGPIPE, SIG_IGN);

	listener = network_probe();
	if (listener < 0) {
		printf("posix-select-socket: %d assertion group(s) unverified in "
		       "this environment (see SKIP line above); nothing ran\n",
		       unverified);
		return 77;
	}

	test_idle_socket_not_readable(listener);
	test_mixed_set(listener);   /* the listener is still idle here */

	client = test_pending_connection_readable(listener);
	if (client < 0) goto out;

	peerlen = sizeof peer;
	accepted = accept(listener, (struct sockaddr *)&peer, &peerlen);
	CHECK(accepted >= 0);
	if (accepted < 0) { close(client); goto out; }

	/* Accepted: the listener has nothing pending again, which is the
	 * negative assertion re-run on a descriptor that was reported
	 * ready a moment ago -- a latch would show up here. */
	{
		int isset = -1;
		short revents = -1;
		CHECK(sel_read(listener, 0, &isset) == 0);
		CHECK(isset == 0);
		CHECK(poll_one(listener, POLLIN, 0, &revents) == 0);
		CHECK(revents == 0);
	}

	test_connected_idle(accepted);
	test_data_arrives_and_drains(client, accepted);
	test_eof_is_readable(client, accepted);

	close(accepted);
	close(client);
out:
	close(listener);

	if (fails) { printf("posix-select-socket: failures: %d\n", fails); return 1; }
	if (unverified) {
		printf("posix-select-socket: %d assertion group(s) unverified in "
		       "this environment (see SKIP lines above); no failures in "
		       "what did run\n", unverified);
		return 77;
	}
	printf("posix-select-socket: all ok\n");
	return 0;
}
