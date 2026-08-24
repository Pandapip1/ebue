/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/socket.h>/<netinet/in.h>/<arpa/inet.h>, AF_INET/SOCK_STREAM only
 * (test/networking-audit.md, this project's own design audit for
 * sockets, and the task that produced src/socket/). Each assertion
 * cites the clause of https://pubs.opengroup.org/onlinepubs/
 * 9699919799/functions/<name>.html or .../basedefs/<header>.html it
 * checks, same convention as test/posix-sysmisc.c.
 *
 * Byte-order and address-text conversion (src/socket/inet.c) need no OS
 * support at all and are always exercised. socket()'s own domain/type/
 * protocol validation (src/socket/socket.c) happens before any AFD
 * handle is ever opened, so it too is always exercised.  SO_REUSEADDR/
 * SO_TYPE/SO_ERROR (src/socket/sockopt.c) are pure struct __fd state
 * with no AFD ioctl involved, so they are too.
 *
 * Everything past socket() succeeding needs a real \Device\Afd
 * endpoint that actually answers AFD ioctls, and that is exactly the
 * part this project's own design audit (networking-audit.md sec 1)
 * flagged as unverifiable against Wine ahead of time: "the only place
 * ntlibc can test against is Wine, and Wine's AFD is provably not a
 * faithful clone of real Windows' AFD for at least socket creation and
 * connect."  Empirically, in this environment: opening a handle via
 * NtCreateFile+the AfdOpenPacketXX EA against \Device\Afd\Endpoint
 * (src/socket/afdsupport.c's __afd_open(); the EA recipe follows
 * ReactOS's WSPSocket, but the EA *value* is the real-Windows 24-byte
 * AFD_OPEN_PACKET, not ReactOS's 12-byte AFD_CREATE_PACKET -- see
 * src/internal/afd.h's socket-creation banner, and
 * test/posix-socket-ea.c, which asserts that buffer's layout with no
 * device involved) *succeeds* under this environment's Wine -- Wine's server
 * accepts the open -- but the very first real ioctl against that
 * handle, IOCTL_AFD_BIND, fails with STATUS_BAD_DEVICE_TYPE
 * (0xC00000CB): Wine's own AFD implementation only wires up a handle
 * opened via its own invented IOCTL_AFD_WINE_CREATE (networking-audit.md
 * sec 1); a handle opened the portable, real-Windows way is never
 * routed to that implementation, so every ioctl on it after the open
 * itself hits Wine's dispatcher with nothing behind the handle.  This
 * was confirmed by hand against both stock upstream Wine (what CI's
 * build-toolchain job produces) and this project's own locally patched
 * Wine build (~/Projects/wine, the RtlCloneUserProcess/exited-PID
 * patches noted in CONTRIBUTING.md) -- neither carries an AFD patch, so
 * both fail identically here.  This is precisely the situation
 * CONTRIBUTING.md's task anticipated ("If Wine turns out not to accept
 * the portable form, report that precisely -- it is a Wine limitation
 * worth a separate fix, not a reason to depend on Wine's private
 * control code") and precisely why the code follows ReactOS, not Wine:
 * this project's CI has a real-Windows leg that a Wine-shaped
 * implementation would silently not work on there.
 *
 * So, following configure's own precedent for an environment gap
 * (delayall.c/DELAY_ALL, documented in configure and Makefile): a
 * runtime capability probe, socket()+bind()+listen() against a fixed
 * loopback port, gates every assertion downstream of "AFD ioctls
 * actually work."  If the probe fails, this prints one SKIP line
 * naming the mechanism and observed errno and returns exit code 77
 * (a distinct "ran, but verified nothing new" outcome, not a plain
 * pass and not a FAIL either) rather than either silently skipping or
 * failing the whole suite.  tools/runtests.sh recognizes 77 and reports
 * it as its own bucket in the run summary, separate from both PASS and
 * FAIL -- the same shape asan-build.sh already uses for its "N not
 * applicable natively" bucket, just decided at run time here instead of
 * build time (this environment's Wine, unlike a native asan build,
 * *looks* capable right up until the first real ioctl).  A silent
 * `return 0` here would report PASS for a feature nothing was actually
 * checked to work, on every platform, indefinitely -- the failure mode
 * this file exists to avoid.  On real Windows (untestable here, reasoned
 * about only) the probe is expected to succeed, since the whole point of
 * following ReactOS instead of Wine was to match what real Windows' AFD
 * actually expects; there, this test exits 0 with every assertion having
 * actually run.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>

static int fails;
/* Counts each SKIP line below: an assertion group this run did not
 * actually exercise (network stack unavailable here), as opposed to
 * `fails`, which counts assertions that ran and got the wrong answer.
 * See main()'s tail for why these are reported, and exit, differently. */
static int unverified;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* htonl.html: "convert values between host and network byte order";
 * ntlibc only targets little-endian arches (arch/i386, arch/x86_64,
 * per src/socket/inet.c's banner), so network byte order (big-endian)
 * and host byte order always differ here. */
static void test_byteorder(void)
{
	CHECK(htons(0x1234) == 0x3412);
	CHECK(ntohs(0x3412) == 0x1234);
	CHECK(htonl(0x12345678UL) == 0x78563412UL);
	CHECK(ntohl(0x78563412UL) == 0x12345678UL);
	CHECK(htons(0) == 0);
	CHECK(htonl(0) == 0);
	/* htons()/ntohs() and htonl()/ntohl() are each other's inverse. */
	CHECK(ntohs(htons(0xbeef)) == 0xbeef);
	CHECK(ntohl(htonl(0xdeadbeefUL)) == 0xdeadbeefUL);
}

/* inet_addr.html: dotted forms "a.b.c.d"/"a.b.c"/"a.b"/"a", decimal
 * parts; "(in_addr_t)(-1)" on failure. */
static void test_inet_addr(void)
{
	struct in_addr expect;
	expect.s_addr = htonl(0x01020304UL);
	CHECK(inet_addr("1.2.3.4") == expect.s_addr);
	CHECK(inet_addr("127.0.0.1") == htonl(INADDR_LOOPBACK));
	CHECK(inet_addr("0.0.0.0") == INADDR_ANY);
	CHECK(inet_addr("not-an-address") == INADDR_NONE);
	CHECK(inet_addr("") == INADDR_NONE);
	CHECK(inet_addr("1.2.3.4.5") == INADDR_NONE); /* too many parts */
	CHECK(inet_addr(0) == INADDR_NONE);
	/* inet_addr.html's 1-part and 2-part short forms. */
	CHECK(inet_addr("16909060") == htonl(16909060UL)); /* "a" form: whole 32 bits */
	CHECK(inet_addr("1.258") == htonl(0x01000102UL));   /* "a.b": b is 24 bits -- 258 = 0x000102 */
}

/* inet_ntop.html/inet_pton.html: AF_INET only (AF_INET6 out of scope,
 * see <sys/socket.h>'s banner); round trip, ENOSPC, EAFNOSUPPORT,
 * malformed-input 0 return. */
static void test_inet_pton_ntop(void)
{
	struct in_addr a;
	char buf[INET_ADDRSTRLEN];
	char tiny[4];

	CHECK(inet_pton(AF_INET, "127.0.0.1", &a) == 1);
	CHECK(a.s_addr == htonl(INADDR_LOOPBACK));
	CHECK(inet_pton(AF_INET, "255.255.255.255", &a) == 1);
	CHECK(a.s_addr == htonl(INADDR_BROADCAST));
	CHECK(inet_pton(AF_INET, "1.2.3", &a) == 0);       /* inet_pton, unlike inet_addr, wants all four parts */
	CHECK(inet_pton(AF_INET, "1.2.3.4.5", &a) == 0);
	CHECK(inet_pton(AF_INET, "256.0.0.1", &a) == 0);   /* out of range octet */
	CHECK(inet_pton(AF_INET, "", &a) == 0);
	errno = 0;
	CHECK(inet_pton(AF_INET6, "::1", &a) == -1);
	CHECK(errno == EAFNOSUPPORT);

	a.s_addr = htonl(0x01020304UL);
	CHECK(inet_ntop(AF_INET, &a, buf, sizeof buf) == buf);
	CHECK(!strcmp(buf, "1.2.3.4"));

	errno = 0;
	CHECK(inet_ntop(AF_INET6, &a, buf, sizeof buf) == 0);
	CHECK(errno == EAFNOSUPPORT);

	errno = 0;
	CHECK(inet_ntop(AF_INET, &a, tiny, sizeof tiny) == 0); /* "1.2.3.4" is 7 chars + NUL, tiny holds 4 */
	CHECK(errno == ENOSPC);

	CHECK(!strcmp(inet_ntoa(a), "1.2.3.4")); /* inet_addr.html: inet_ntoa()'s static buffer */
}

/* socket.html mandatory ERRORS: EAFNOSUPPORT/EPROTOTYPE/EPROTONOSUPPORT
 * are all decided before any AFD handle is opened
 * (src/socket/socket.c), so these need no working network stack. */
static void test_socket_domain_errors(void)
{
	errno = 0;
	CHECK(socket(AF_INET6, SOCK_STREAM, 0) == -1);
	CHECK(errno == EAFNOSUPPORT);

	errno = 0;
	CHECK(socket(AF_INET, SOCK_DGRAM, 0) == -1);
	CHECK(errno == EPROTOTYPE);

	errno = 0;
	CHECK(socket(AF_INET, SOCK_STREAM, IPPROTO_UDP) == -1);
	CHECK(errno == EPROTONOSUPPORT);
}

/* setsockopt.html/<sys/socket.h>: SO_REUSEADDR/SO_TYPE/SO_ERROR are the
 * options src/socket/sockopt.c genuinely supports, and none of them
 * touch AFD (SO_REUSEADDR is only consumed by a later bind()), so this
 * needs no working AFD ioctl -- but socket() itself still opens a real
 * \Device\Afd\Endpoint handle (src/socket/afdsupport.c's __afd_open()),
 * which this project's `make asan` native-build stub environment
 * (fuzz/ntstubs.c's in-memory simulated volume, which has no such
 * device node at all) fails outright rather than merely refusing an
 * ioctl on -- so this, unlike the rest of this function, tolerates
 * socket() itself failing, the same environment-gap shape as
 * network_probe() below rather than a hard requirement. */
static void test_sockopt_no_network(void)
{
	int s, v;
	socklen_t vl;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		printf("SKIP posix-socket sockopt tests (socket() failed, errno=%d)\n", errno);
		unverified++;
		return;
	}

	v = 0; vl = sizeof(v);
	CHECK(getsockopt(s, SOL_SOCKET, SO_TYPE, &v, &vl) == 0);
	CHECK(v == SOCK_STREAM);

	v = 0; vl = sizeof(v);
	CHECK(getsockopt(s, SOL_SOCKET, SO_ERROR, &v, &vl) == 0);
	CHECK(v == 0);

	v = 0; vl = sizeof(v);
	CHECK(getsockopt(s, SOL_SOCKET, SO_REUSEADDR, &v, &vl) == 0);
	CHECK(v == 0); /* setsockopt.html: not set yet */

	v = 1;
	CHECK(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &v, sizeof v) == 0);
	v = 0; vl = sizeof(v);
	CHECK(getsockopt(s, SOL_SOCKET, SO_REUSEADDR, &v, &vl) == 0);
	CHECK(v == 1);

	errno = 0;
	CHECK(setsockopt(s, SOL_SOCKET, SO_LINGER, &v, sizeof v) == -1); /* not among the genuinely-supported options */
	CHECK(errno == ENOPROTOOPT);

	errno = 0;
	CHECK(getsockopt(0, SOL_SOCKET, SO_TYPE, &v, &vl) == -1); /* fd 0 is not a socket */
	CHECK(errno == ENOTSOCK);

	close(s);
}

/* Fixed loopback port used both by the capability probe and the main
 * end-to-end test below: getsockname() is out of scope here (see the
 * file banner's list of what src/socket/ does not implement), so there
 * is no way to ask AFD which ephemeral port a INADDR_ANY/port-0 bind()
 * landed on -- a fixed port in the dynamic/private range (RFC 6335) is
 * used instead, same trade-off any single-process loopback test in
 * this range makes. */
#define TEST_PORT 55123

static int make_loopback_addr(struct sockaddr_in *a)
{
	memset(a, 0, sizeof *a);
	a->sin_family = AF_INET;
	a->sin_port = htons(TEST_PORT);
	a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	return sizeof *a;
}

/* The runtime capability probe -- see the file banner.  Returns a bound
 * and listening socket fd, or -1 if AFD ioctls do not work here (after
 * printing the one SKIP line). */
static int network_probe(void)
{
	struct sockaddr_in addr;
	int s;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		printf("SKIP posix-socket network tests (socket() failed, errno=%d)\n", errno);
		unverified++;
		return -1;
	}
	if (bind(s, (struct sockaddr *)&addr, make_loopback_addr(&addr)) < 0) {
		/* Report the call and its errno and stop there.  This message
		 * used to blame Wine's AFD, which was true of the environment
		 * it was written in.  It is also wrong to say the same line
		 * prints on the real-Windows CI legs: measured 2026-08-24 on
		 * the windows-test (x86_64) job, this file prints
		 * "posix-socket: all ok" and exits 0 there -- exit 0 means the
		 * unverified counter is zero, so bind() and everything after
		 * it DID run.  The SKIP path below is reached under Wine only.
		 * That matters for reading a green run: the network group is
		 * verified, but on exactly one leg, so a red or absent
		 * windows-test leg takes all of it with it.  A
		 * failing bind() cannot tell the two apart from here: the
		 * ioctl either was not understood or was understood and the
		 * address rejected, and only the errno distinguishes them.
		 * test/posix-socket-bind.c checks the request's byte layout
		 * with no device at all, which is the part this cannot. */
		printf("SKIP posix-socket network tests (bind() failed, errno=%d; "
		       "IOCTL_AFD_BIND on a \\Device\\Afd\\Endpoint handle -- "
		       "see test/posix-socket-bind.c and test/networking-audit.md sec 1)\n",
		       errno);
		close(s);
		unverified++;
		return -1;
	}
	if (listen(s, 1) < 0) {
		printf("SKIP posix-socket network tests (listen() failed, errno=%d)\n", errno);
		close(s);
		unverified++;
		return -1;
	}
	return s;
}

/* The core case: a loopback connection between a listening socket and
 * a client in the same process (single-threaded and sequential is
 * enough -- connect.html's stream-socket handshake completes and
 * returns once the peer's listen() backlog can hold it, without
 * needing accept() to have run yet, so connect() before accept() here
 * is not a race). */
static void test_loopback_roundtrip(int listener)
{
	struct sockaddr_in addr, peer;
	socklen_t peerlen;
	int client, accepted;
	char buf[32];
	ssize_t n;

	client = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(client >= 0);
	if (client < 0) return;

	/* connect.html: "the initiating socket is not bound, it will be
	 * bound" -- exercised implicitly here since `client` never called
	 * bind() itself. */
	CHECK(connect(client, (struct sockaddr *)&addr, make_loopback_addr(&addr)) == 0);

	errno = 0;
	CHECK(connect(client, (struct sockaddr *)&addr, sizeof addr) == -1); /* connect.html: EISCONN */
	CHECK(errno == EISCONN);

	peerlen = sizeof peer;
	accepted = accept(listener, (struct sockaddr *)&peer, &peerlen);
	CHECK(accepted >= 0);
	if (accepted < 0) { close(client); return; }
	/* accept.html: the returned address is the peer's -- loopback,
	 * same port range this test bound the client to (an ephemeral
	 * port AFD chose, since `client` was never bind()'d itself). */
	CHECK(peer.sin_family == AF_INET);
	CHECK(peer.sin_addr.s_addr == htonl(INADDR_LOOPBACK));

	/* send.html/recv.html: a normal round trip each direction, plus
	 * read()/write() (src/unistd/read.c, write.c) going through the
	 * exact same __FD_SOCKET path. */
	CHECK(send(client, "ping", 4, 0) == 4);
	memset(buf, 0, sizeof buf);
	n = recv(accepted, buf, sizeof buf, 0);
	CHECK(n == 4);
	CHECK(!memcmp(buf, "ping", 4));

	CHECK(write(accepted, "pong!", 5) == 5);
	memset(buf, 0, sizeof buf);
	n = read(client, buf, sizeof buf);
	CHECK(n == 5);
	CHECK(!memcmp(buf, "pong!", 5));

	/* shutdown.html SHUT_WR: further sends on `client` fail, and the
	 * peer sees end-of-stream (recv.html: "0...the peer has performed
	 * an orderly shutdown"). */
	CHECK(shutdown(client, SHUT_WR) == 0);
	{
		signal(SIGPIPE, SIG_IGN); /* send.html: SIGPIPE unless MSG_NOSIGNAL; either way, no crash */
		errno = 0;
		CHECK(send(client, "x", 1, MSG_NOSIGNAL) == -1);
		CHECK(errno == EPIPE);
	}
	n = recv(accepted, buf, sizeof buf, 0);
	CHECK(n == 0);

	close(client);
	close(accepted);
}

/* accept.html: EINVAL for a socket that was never listen()'d;
 * bind.html: EINVAL for a second bind() on an already-bound socket;
 * ENOTSOCK for every socket call given a non-socket fd -- all pure
 * struct __fd state, but grouped with the network-gated tests since
 * the first two need a real listener/bound socket to check against. */
static void test_socket_state_errors(int listener)
{
	struct sockaddr_in addr;
	int s;

	s = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(s >= 0);
	errno = 0;
	CHECK(accept(s, 0, 0) == -1); /* never listen()'d */
	CHECK(errno == EINVAL);

	errno = 0;
	CHECK(bind(listener, (struct sockaddr *)&addr, make_loopback_addr(&addr)) == -1); /* already bound */
	CHECK(errno == EINVAL);

	errno = 0;
	CHECK(bind(0, (struct sockaddr *)&addr, sizeof addr) == -1); /* fd 0 is not a socket */
	CHECK(errno == ENOTSOCK);

	close(s);
}

int main(void)
{
	int listener;

	test_byteorder();
	test_inet_addr();
	test_inet_pton_ntop();
	test_socket_domain_errors();
	test_sockopt_no_network();

	listener = network_probe();
	if (listener >= 0) {
		test_socket_state_errors(listener);
		test_loopback_roundtrip(listener);
		close(listener);
	}

	/* UDP (sendto/recvfrom/SOCK_DGRAM's actual use), AF_INET6
	 * (sockaddr_in6/in6_addr/getaddrinfo's AF_INET6 path), AF_UNIX
	 * (socketpair(), struct sockaddr_un) and getsockname()/
	 * getpeername()/sockatmark() are all staged for later work, per
	 * test/networking-audit.md sec 6 (stages 4-6) -- not merely
	 * untested, genuinely not implemented, and (per this project's own
	 * standing rule, test/posix-sysmisc.c's file banner) not even
	 * declared in <sys/socket.h>/<netinet/in.h> (see that header's own
	 * banner), so none of this can even be written outside an #if 0
	 * fence. */
#if 0 /* UNIMPL: sys_socket.h.html's full function list --
	sendto()/recvfrom()/sendmsg()/recvmsg() (UDP and ancillary
	data), socketpair() (AF_UNIX) -- networking-audit.md sec 6
	stages 5/6/7. */
	{
		int sv[2];
		char b[4];
		socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
		sendto(sv[0], "ping", 4, 0, 0, 0);
		recvfrom(sv[1], b, sizeof b, 0, 0, 0);
	}
#endif
#if 0 /* UNIMPL: netinet_in.h.html's struct sockaddr_in6/struct
	in6_addr/IN6_IS_ADDR_* -- networking-audit.md sec 6 does not
	stage AF_INET6 explicitly (it stages AF_UNIX and UDP), but this
	project's task for this file scoped AF_INET/SOCK_STREAM only,
	so IPv6 is deferred the same way. */
	{
		struct sockaddr_in6 a6;
		a6.sin6_family = AF_INET6;
	}
#endif
#if 0 /* UNIMPL: getsockname.html/getpeername.html -- not in this
	project's declared scope for this stage (see <sys/socket.h>'s
	banner); this file's own network tests work around the gap with
	a fixed TEST_PORT instead of querying an ephemeral one. */
	{
		struct sockaddr_in a;
		socklen_t l = sizeof a;
		getsockname(0, (struct sockaddr *)&a, &l);
	}
#endif

	if (fails) { printf("posix-socket: failures: %d\n", fails); return 1; }
	if (unverified) {
		/* Everything that ran passed, but that is not the same claim as
		 * "all ok" -- see the file banner and the SKIP line(s) above for
		 * which assertion groups never ran at all. Exit 77 rather than 0
		 * so tools/runtests.sh reports this run in its own bucket instead
		 * of silently counting it as a pass. */
		printf("posix-socket: %d assertion group(s) unverified in this "
		       "environment (see SKIP lines above); no failures in what "
		       "did run\n", unverified);
		return 77;
	}
	printf("posix-socket: all ok\n");
	return 0;
}
