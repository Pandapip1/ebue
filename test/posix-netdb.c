/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Coverage fences for <netdb.h> -- UPDATE (this pass): ntlibc now HAS
 * a <netdb.h>, on Linux (include/netdb.h; src/netdb/linux/); see that
 * header's own banner for exact scope (getaddrinfo()/freeaddrinfo()/
 * gai_strerror() plus gethostbyname() as a disclosed legacy addition,
 * backed by a real /etc/hosts parser, a real minimal UDP DNS stub
 * resolver, and a real /etc/nsswitch.conf parser). The
 * posix_netdb_getaddrinfo_loopback and posix_netdb_gai_strerror_text
 * fences below are UNWRAPPED accordingly -- they now run for real,
 * every call site's original clause citations kept verbatim (nothing
 * about what they assert changed, only whether the code compiles).
 * posix_netdb_getnameinfo_numeric and every database-enumeration fence
 * (service/protocol/host/network) are UNCHANGED, still UNIMPL: none of
 * getnameinfo()/the four enumerable databases are part of this pass's
 * own scope (see include/netdb.h's banner for exactly why each one is
 * deferred, not silently dropped). New, non-POSIX-clause-audit
 * coverage for this pass's own real backends -- the /etc/hosts and
 * /etc/nsswitch.conf fixture behavior, and a real UDP round trip
 * against a hermetic fake DNS server this file spins up itself -- is
 * appended after the original POSIX audit content, not mixed into it.
 *
 * POSIX.1-2017 (IEEE Std 1003.1-2017, The Open Group Base
 * Specifications Issue 7, 2018 Edition), served at
 * https://pubs.opengroup.org/onlinepubs/9699919799/ ; clause text read
 * from Ubuntu's manpages-posix-dev 2017a-2, which reprints that
 * edition verbatim (pubs.opengroup.org is unreachable from here).
 *
 * ==================== the gap ========================================
 *
 * The name-level cross-index behind test/posix-pthread.c finds 22
 * <netdb.h> interfaces with no mention anywhere in test/*.c.  (Its
 * per-header bucketing puts 20 of them under <netdb.h> and two --
 * freeaddrinfo, getnameinfo -- under <sys/socket.h>, because it buckets
 * by the FIRST #include in a page's SYNOPSIS and freeaddrinfo.html
 * lists <sys/socket.h> first.  The 2017a page's own NAME section also
 * carries a typo, `getprotent`, which the index faithfully counts and
 * which is not a real interface; it is dropped here.  Both are index
 * artefacts, recorded rather than quietly fixed.)  The 22:
 *
 *   name resolution   freeaddrinfo gai_strerror getnameinfo
 *   host database     sethostent gethostent endhostent
 *   network database  setnetent getnetent getnetbyaddr getnetbyname
 *                     endnetent
 *   protocol database setprotoent getprotoent getprotobyname
 *                     getprotobynumber endprotoent
 *   service database  setservent getservent getservbyname
 *                     getservbyport endservent
 *
 * getaddrinfo() itself is absent too but reads as covered by the index,
 * because the identifier appears once in prose at
 * test/posix-socket.c:431 -- in a list of things a v6 path would need,
 * not in an assertion.  That is the index's known over-report; it is
 * noted here rather than silently corrected, and getaddrinfo() is
 * fenced below alongside freeaddrinfo(), which shares its page.
 *
 * This is a live gap rather than a decline: ntlibc HAS a socket layer
 * (src/socket/, include/netinet/in.h, include/arpa/inet.h, and
 * test/posix-socket*.c's six files), so a program that can already
 * open and connect a socket here still cannot turn a name into an
 * address, or a port number into a service name, by any POSIX route.
 * The <netdb.h> databases are also the part of this header that needs
 * no resolver at all: XSH endservent.html says only that the data "is
 * considered to be stored in a database that can be accessed
 * sequentially or randomly.  The implementation of this database is
 * unspecified" -- so the well-known-services table can be a static
 * array, and NT ships %SystemRoot%\system32\drivers\etc\services in
 * exactly the /etc/services format.
 *
 * ==================== how these fail today ===========================
 *
 * include/ has no netdb.h, so every fence dies on its own #include and
 * that is the ABSENCE these assert -- UNIMPL, not BUG.
 * tools/test-policy.py --pedantic re-decides each one; this comment is
 * not the authority for it.
 *
 * Not fenced here, with reasons -- see the report accompanying this
 * file: h_errno and the gethostbyname()/gethostbyaddr() pair.  Checked,
 * not assumed: the 2017a <netdb.h> page's "The following shall be
 * declared as functions" block lists 21 names and none of the three is
 * among them, and manpages-posix-dev ships no gethostbyname page at
 * all.  POSIX removed them in this edition, so their absence here is
 * conformant and a fence would assert the wrong thing.  Also not
 * fenced: <net/if.h>'s
 * if_nametoindex family (a separate header, and a separate unit).
 */

/* setenv()/unsetenv() (this pass's own new hermetic-fixture tests,
 * further down) are gated behind a feature-test macro in this
 * project's own headers -- see test/posix-time.c/test/posix-unistd.c
 * for the same pattern already established across this test suite. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include "test-policy.h"

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; \
	printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ==================================================================
 * Name resolution -- .../functions/freeaddrinfo.html (which specifies
 * getaddrinfo too), getnameinfo.html, gai_strerror.html
 * ================================================================== */

/* UNWRAPPED (was NTLIBC_TEST(UNIMPL, posix_netdb_getaddrinfo_loopback)):
 * include/netdb.h now exists; see this file's own top banner.
 * <arpa/inet.h> (for htons()/htonl(), used below) was missing from
 * this fence's own original include list -- never caught while the
 * whole block was `#if 0`'d out; caught now, unwrapping it. */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static void test_posix_netdb_getaddrinfo_loopback(void)
{
	struct addrinfo hints, *res = NULL, *p;
	int seen = 0;

	/* freeaddrinfo.html: "The getaddrinfo() function shall translate
	 * the name of a service location (for example, a host name) and/or
	 * a service name and shall return a set of socket addresses and
	 * associated information to be used in creating a socket with
	 * which to address the specified service."  A numeric address with
	 * AI_NUMERICHOST needs no resolver, so this clause is decidable
	 * with no network at all: "if the AI_NUMERICHOST flag is
	 * specified, then a non-null nodename string shall be a numeric
	 * host address string." */
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST;

	CHECK(getaddrinfo("127.0.0.1", "80", &hints, &res) == 0);
	CHECK(res != NULL);

	/* "shall return a set of socket addresses" -- a list linked by
	 * ai_next, each entry self-describing through ai_family,
	 * ai_socktype, ai_addr and ai_addrlen. */
	for (p = res; p != NULL; p = p->ai_next) {
		CHECK(p->ai_family == AF_INET);
		CHECK(p->ai_socktype == SOCK_STREAM);
		CHECK(p->ai_addr != NULL);
		CHECK(p->ai_addrlen == (socklen_t)sizeof(struct sockaddr_in));
		if (p->ai_addr != NULL) {
			struct sockaddr_in sin;

			memcpy(&sin, p->ai_addr, sizeof sin);
			CHECK(sin.sin_family == AF_INET);
			/* servname "80" is a port number, in network byte
			 * order in the returned sockaddr. */
			CHECK(sin.sin_port == htons(80));
			CHECK(sin.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
			seen++;
		}
	}
	CHECK(seen >= 1);

	/* "The freeaddrinfo() function shall free one or more addrinfo
	 * structures returned by getaddrinfo(), along with any additional
	 * storage associated with those structures.  If the ai_next field
	 * of the structure is not null, the entire list of structures
	 * shall be freed." */
	freeaddrinfo(res);

	/* ERRORS: "[EAI_NONAME] The name does not resolve for the supplied
	 * parameters" -- which is what AI_NUMERICHOST on a non-numeric
	 * name must produce, again without any resolver. */
	res = NULL;
	CHECK(getaddrinfo("not.a.numeric.address", "80", &hints, &res)
	      == EAI_NONAME);
}

#if NTLIBC_TEST(UNIMPL, posix_netdb_getnameinfo_numeric)
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

static void test_posix_netdb_getnameinfo_numeric(void)
{
	struct sockaddr_in sin;
	char node[NI_MAXHOST];
	char serv[NI_MAXSERV];

	memset(&sin, 0, sizeof sin);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(80);
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	/* getnameinfo.html: "shall translate a socket address to a node
	 * name and service location ... If the node argument is non-NULL
	 * and the nodelen argument is non-zero, then the node argument
	 * shall point to a buffer able to contain up to nodelen bytes that
	 * receives the node name as a null-terminated string."  With
	 * NI_NUMERICHOST -- "the numeric form of the node's address is
	 * returned instead of its name" -- and NI_NUMERICSERV -- "the
	 * numeric form of the service address is returned ... instead of
	 * its name" -- no resolver is consulted, so the answer is fixed. */
	memset(node, 0, sizeof node);
	memset(serv, 0, sizeof serv);
	CHECK(getnameinfo((const struct sockaddr *)&sin, sizeof sin,
			  node, sizeof node, serv, sizeof serv,
			  NI_NUMERICHOST | NI_NUMERICSERV) == 0);
	CHECK(strcmp(node, "127.0.0.1") == 0);
	CHECK(strcmp(serv, "80") == 0);

	/* "The NI_MAXHOST and NI_MAXSERV [constants] ... shall be defined
	 * in <netdb.h>" and are the buffer sizes this interface is
	 * specified against, so they must be large enough for what it
	 * returns. */
	CHECK(NI_MAXHOST > 0 && NI_MAXSERV > 0);
	CHECK(strlen(node) < (size_t)NI_MAXHOST);

	/* "If the node argument is NULL and the nodelen argument is zero,
	 * ... the node name shall not be returned" -- and likewise for
	 * serv; at least one of the two must be requested. */
	memset(serv, 0, sizeof serv);
	CHECK(getnameinfo((const struct sockaddr *)&sin, sizeof sin,
			  NULL, 0, serv, sizeof serv, NI_NUMERICSERV) == 0);
	CHECK(strcmp(serv, "80") == 0);
}
#endif

/* UNWRAPPED (was NTLIBC_TEST(UNIMPL, posix_netdb_gai_strerror_text)):
 * <netdb.h> already included above. */
static void test_posix_netdb_gai_strerror_text(void)
{
	static const int codes[] = {
		EAI_AGAIN, EAI_BADFLAGS, EAI_FAIL, EAI_FAMILY, EAI_MEMORY,
		EAI_NONAME, EAI_SERVICE, EAI_SOCKTYPE, EAI_SYSTEM, EAI_OVERFLOW
	};
	size_t i, j;

	/* gai_strerror.html: "shall return a text string describing an
	 * error value for the getaddrinfo() and getnameinfo() functions
	 * listed in the <netdb.h> header."  RETURN VALUE: "Upon successful
	 * completion, gai_strerror() returns a pointer to a string
	 * describing the error value.  If the error value is not one of
	 * those listed above, the function returns a pointer to a string
	 * indicating an unknown error." -- so it never returns NULL, and
	 * never an empty string. */
	for (i = 0; i < sizeof codes / sizeof codes[0]; i++) {
		const char *s = gai_strerror(codes[i]);

		CHECK(s != NULL);
		if (s != NULL)
			CHECK(s[0] != '\0');
	}

	/* The codes themselves are distinct: getaddrinfo() returns them as
	 * its own return value, so two that collide would be
	 * indistinguishable to a caller. */
	for (i = 0; i < sizeof codes / sizeof codes[0]; i++)
		for (j = i + 1; j < sizeof codes / sizeof codes[0]; j++)
			CHECK(codes[i] != codes[j]);

	/* "a string indicating an unknown error" for a value that is not
	 * one of them -- still a string. */
	CHECK(gai_strerror(0x5eed) != NULL);
	CHECK(gai_strerror(0x5eed)[0] != '\0');
}

/* ==================================================================
 * The service database -- .../functions/endservent.html, which
 * specifies setservent/getservent/getservbyname/getservbyport too
 * ================================================================== */

#if NTLIBC_TEST(UNIMPL, posix_netdb_getservbyname_wellknown)
#include <netdb.h>
#include <netinet/in.h>

static void test_posix_netdb_getservbyname_wellknown(void)
{
	struct servent *se;

	/* endservent.html: "The getservbyname() function shall search the
	 * database from the beginning and find the first entry for which
	 * the service name specified by name matches the s_name member and
	 * the protocol name specified by proto matches the s_proto member
	 * ... If proto is a null pointer, any value of the s_proto member
	 * shall be matched."
	 *
	 * "http/80/tcp" is an IANA well-known assignment, present in every
	 * /etc/services and in NT's own
	 * %SystemRoot%\system32\drivers\etc\services, so this is a fact
	 * about the database rather than about one host. */
	se = getservbyname("http", "tcp");
	CHECK(se != NULL);
	if (se != NULL) {
		/* <netdb.h> defines servent's members: "s_port: A value
		 * which, when converted to uint16_t, yields the port
		 * number in network byte order at which the service
		 * resides." */
		CHECK(strcmp(se->s_name, "http") == 0);
		CHECK((uint16_t)se->s_port == htons(80));
		CHECK(se->s_proto != NULL && strcmp(se->s_proto, "tcp") == 0);
		/* "s_aliases: A pointer to an array of pointers to
		 * alternative service names, terminated by a null
		 * pointer." -- possibly empty, but never a null array. */
		CHECK(se->s_aliases != NULL);
	}

	/* getservbyport(): "shall search the database from the beginning
	 * and find the first entry for which the port specified by port
	 * matches the s_port member" -- the inverse of the above, and the
	 * port argument is in network byte order. */
	se = getservbyport((int)htons(80), "tcp");
	CHECK(se != NULL);
	if (se != NULL)
		CHECK(strcmp(se->s_name, "http") == 0);

	/* RETURN VALUE: "a null pointer if the end of the database was
	 * reached or the requested entry was not found." */
	CHECK(getservbyname("no-such-service-name", "tcp") == NULL);

	/* "The setservent() function shall open a connection to the
	 * database, and set the next entry to the first entry", "The
	 * getservent() function shall read the next entry of the
	 * database", "The endservent() function shall close the connection
	 * to the database".  Walking from the start must reach at least
	 * one entry, and must terminate. */
	setservent(1);
	se = getservent();
	CHECK(se != NULL);
	if (se != NULL)
		CHECK(se->s_name != NULL);
	endservent();
}
#endif

/* ==================================================================
 * The protocol database -- .../functions/endprotoent.html
 * ================================================================== */

#if NTLIBC_TEST(UNIMPL, posix_netdb_getprotobyname_tcp)
#include <netdb.h>
#include <netinet/in.h>

static void test_posix_netdb_getprotobyname_tcp(void)
{
	struct protoent *pe;

	/* endprotoent.html: "The getprotobyname() function shall search
	 * the database from the beginning and find the first entry for
	 * which the protocol name specified by name matches the p_name
	 * member".  TCP is protocol number 6 by IANA assignment, which
	 * <netinet/in.h> already spells IPPROTO_TCP in this tree. */
	pe = getprotobyname("tcp");
	CHECK(pe != NULL);
	if (pe != NULL) {
		CHECK(strcmp(pe->p_name, "tcp") == 0);
		CHECK(pe->p_proto == IPPROTO_TCP);
		CHECK(pe->p_aliases != NULL);
	}

	/* "The getprotobynumber() function shall search the database from
	 * the beginning and find the first entry for which the protocol
	 * number specified by proto matches the p_proto member." */
	pe = getprotobynumber(IPPROTO_UDP);
	CHECK(pe != NULL);
	if (pe != NULL) {
		CHECK(strcmp(pe->p_name, "udp") == 0);
		CHECK(pe->p_proto == IPPROTO_UDP);
	}

	CHECK(getprotobyname("no-such-protocol-name") == NULL);

	/* setprotoent()/getprotoent()/endprotoent(), the sequential
	 * access the same page specifies. */
	setprotoent(1);
	pe = getprotoent();
	CHECK(pe != NULL);
	if (pe != NULL)
		CHECK(pe->p_name != NULL);
	endprotoent();
}
#endif

/* ==================================================================
 * The host and network databases --
 * .../functions/endhostent.html, endnetent.html
 * ================================================================== */

#if NTLIBC_TEST(UNIMPL, posix_netdb_hostent_sequential_access)
#include <netdb.h>
#include <sys/socket.h>

static void test_posix_netdb_hostent_sequential_access(void)
{
	struct hostent *he;
	int entries = 0;

	/* endhostent.html: "The sethostent() function shall open a
	 * connection to the database and set the next entry for retrieval
	 * to the first entry in the database ... The gethostent()
	 * function shall read the next entry in the database ... Entries
	 * shall be returned in hostent structures ... The endhostent()
	 * function shall close the connection to the database, releasing
	 * any open file descriptor."
	 *
	 * RETURN VALUE: "a pointer to a hostent structure if the requested
	 * entry was found, and a null pointer if the end of the database
	 * was reached".  The database may legitimately be empty, so the
	 * assertion is about the SHAPE of what comes back and about
	 * termination -- not about any host existing. */
	sethostent(1);
	while ((he = gethostent()) != NULL && entries < 64) {
		/* <netdb.h>'s hostent: "h_name: Official name of the
		 * host", "h_aliases: ... terminated by a null pointer",
		 * "h_length: The length, in bytes, of the address",
		 * "h_addr_list: ... network addresses (in network byte
		 * order) for the host, terminated by a null pointer." */
		CHECK(he->h_name != NULL);
		CHECK(he->h_aliases != NULL);
		CHECK(he->h_addr_list != NULL);
		CHECK(he->h_addrtype == AF_INET || he->h_addrtype == AF_INET6);
		if (he->h_addrtype == AF_INET)
			CHECK(he->h_length == 4);
		if (he->h_addrtype == AF_INET6)
			CHECK(he->h_length == 16);
		entries++;
	}
	/* Terminated rather than ran to the guard: the null-pointer
	 * end-of-database clause is what stops this loop. */
	CHECK(entries < 64);
	endhostent();
}
#endif

#if NTLIBC_TEST(UNIMPL, posix_netdb_netent_lookup)
#include <netdb.h>
#include <sys/socket.h>

static void test_posix_netdb_netent_lookup(void)
{
	struct netent *ne;
	int entries = 0;

	/* endnetent.html: "The setnetent() function shall open and rewind
	 * the database ... The getnetent() function shall read the next
	 * entry of the database ... The endnetent() function shall close
	 * the database."  Same shape-and-termination discipline as the
	 * host database: the network database is routinely empty, so
	 * asserting an entry exists would be asserting a fact about the
	 * host, not about the interface. */
	setnetent(1);
	while ((ne = getnetent()) != NULL && entries < 64) {
		/* <netdb.h>'s netent: "n_name: Official, fully-qualified
		 * ... name", "n_aliases: ... terminated by a null
		 * pointer", "n_addrtype: The address type of the
		 * network", "n_net: The network number, in host byte
		 * order." */
		CHECK(ne->n_name != NULL);
		CHECK(ne->n_aliases != NULL);
		CHECK(ne->n_addrtype == AF_INET);
		entries++;
	}
	CHECK(entries < 64);
	endnetent();

	/* "The getnetbyaddr() function shall search the database from the
	 * beginning, and find the first entry for which the network number
	 * specified by net matches the n_net member and the address type
	 * specified by type matches the n_addrtype member ... The
	 * getnetbyname() function shall search the database from the
	 * beginning and find the first entry for which the network name
	 * specified by name matches the n_name member".  RETURN VALUE
	 * gives the miss case, which is the one guaranteed to be
	 * reachable on a host with no network database: "a null pointer if
	 * the end of the database was reached or the requested entry was
	 * not found." */
	CHECK(getnetbyname("no-such-network-name") == NULL);
	CHECK(getnetbyaddr(0xfffffffeUL, AF_INET) == NULL);
}
#endif

/* ==================================================================
 * Not a POSIX-clause fence: real coverage of THIS PASS'S OWN backends
 * (src/netdb/linux/) -- the /etc/hosts parser, the /etc/nsswitch.conf
 * parser, and the UDP DNS stub resolver's real wire-format round trip
 * -- against hermetic fixtures, per this file's own top banner update
 * and src/internal/nss_paths.h's disclosed NTLIBC_TEST_*_PATH seam.
 * ================================================================== */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>

static void fixture_write(const char *path, const char *content)
{
	FILE *f = fopen(path, "w");
	CHECK(f != NULL);
	if (!f) return;
	CHECK(fputs(content, f) >= 0);
	fclose(f);
}

/* /etc/hosts (src/netdb/linux/hosts.c), positive and negative lookups,
 * plus AI_CANONNAME -- a small, fully-controlled two-line fixture
 * (plus one comment line, to prove '#' comments are actually skipped
 * rather than merely never appearing in a fixture). */
static void test_netdb_hosts_fixture(void)
{
	struct addrinfo hints, *res;
	struct sockaddr_in sin;

	fixture_write("nd-hosts",
		"127.0.0.1 localhost\n"
		"10.20.30.40 myhost.example myhost\n"
		"# a comment line, must not be parsed as a record\n"
		"10.20.30.41 second.example\n");
	fixture_write("nd-nsswitch.conf", "hosts: files dns\npasswd: files\ngroup: files\n");
	CHECK(setenv("NTLIBC_TEST_HOSTS_PATH", "nd-hosts", 1) == 0);
	CHECK(setenv("NTLIBC_TEST_NSSWITCH_PATH", "nd-nsswitch.conf", 1) == 0);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_CANONNAME;

	res = NULL;
	CHECK(getaddrinfo("myhost", NULL, &hints, &res) == 0);
	CHECK(res != NULL);
	if (res) {
		memcpy(&sin, res->ai_addr, sizeof sin);
		CHECK(sin.sin_addr.s_addr ==
		      htonl((10u << 24) | (20u << 16) | (30u << 8) | 40u));
		CHECK(res->ai_canonname != NULL &&
		      strcmp(res->ai_canonname, "myhost.example") == 0);
		freeaddrinfo(res);
	}

	res = NULL;
	CHECK(getaddrinfo("second.example", NULL, &hints, &res) == 0);
	if (res) freeaddrinfo(res);

	unsetenv("NTLIBC_TEST_HOSTS_PATH");
	unsetenv("NTLIBC_TEST_NSSWITCH_PATH");
}

/* /etc/nsswitch.conf (src/netdb/linux/nsswitch.c): an admin who
 * configures "hosts: files" (dns removed) gets an honest EAI_NONAME
 * for a name that is genuinely nowhere in the fixture hosts file --
 * deterministic, unlike leaving dns enabled and pointing it at
 * nothing, because the miss never falls through to any network step
 * at all. */
static void test_netdb_nsswitch_hosts_files_only(void)
{
	struct addrinfo hints, *res = NULL;

	fixture_write("nd-hosts", "127.0.0.1 localhost\n");
	fixture_write("nd-nsswitch.conf", "hosts: files\n");
	CHECK(setenv("NTLIBC_TEST_HOSTS_PATH", "nd-hosts", 1) == 0);
	CHECK(setenv("NTLIBC_TEST_NSSWITCH_PATH", "nd-nsswitch.conf", 1) == 0);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	CHECK(getaddrinfo("nowhere.example", NULL, &hints, &res) == EAI_NONAME);

	unsetenv("NTLIBC_TEST_HOSTS_PATH");
	unsetenv("NTLIBC_TEST_NSSWITCH_PATH");
}

/* gethostbyname(): a real, disclosed legacy extension outside this
 * edition of POSIX (see this file's top banner and include/netdb.h's
 * own banner) -- a second, thinner front door onto the identical
 * hosts-fixture walk the getaddrinfo() tests above already exercise,
 * so this checks the h_errno/struct hostent shape specifically rather
 * than re-proving the lookup itself. */
static void test_netdb_gethostbyname_fixture(void)
{
	struct hostent *he;
	struct in_addr a;

	fixture_write("nd-hosts", "10.1.2.3 gethost.example ghalias\n");
	fixture_write("nd-nsswitch.conf", "hosts: files\n");
	CHECK(setenv("NTLIBC_TEST_HOSTS_PATH", "nd-hosts", 1) == 0);
	CHECK(setenv("NTLIBC_TEST_NSSWITCH_PATH", "nd-nsswitch.conf", 1) == 0);

	he = gethostbyname("ghalias");
	CHECK(he != NULL);
	if (he) {
		CHECK(he->h_addrtype == AF_INET);
		CHECK(he->h_length == 4);
		CHECK(he->h_addr_list != NULL && he->h_addr_list[0] != NULL);
		memcpy(&a, he->h_addr_list[0], 4);
		CHECK(a.s_addr == htonl((10u << 24) | (1u << 16) | (2u << 8) | 3u));
		CHECK(he->h_aliases != NULL && he->h_aliases[0] == NULL);
	}

	h_errno = 0;
	he = gethostbyname("nowhere.example");
	CHECK(he == NULL);
	CHECK(h_errno == HOST_NOT_FOUND);

	unsetenv("NTLIBC_TEST_HOSTS_PATH");
	unsetenv("NTLIBC_TEST_NSSWITCH_PATH");
}

/* ==================================================================
 * A real UDP round trip against a hermetic fake DNS server -- proves
 * src/netdb/linux/resolv.c's own real socket(2)/connect(2)/sendto(2)/
 * recvfrom(2) syscalls and its RFC 1035 wire-format response parser
 * (including the one label-compression shape a real server's
 * response actually uses: a pointer back to the echoed question name)
 * against genuine bytes on the wire, not just a parser fed by hand.
 *
 * The server side needs raw socket(2)/bind(2)/getsockname(2)/
 * recvfrom(2)/sendto(2) syscalls of its own, following the exact same
 * pattern src/netdb/linux/resolv.c's client side already uses (see
 * that file's own banner for why: the public socket() front door is
 * AF_INET/SOCK_STREAM-only today, so nothing UDP-shaped is reachable
 * through it at all, client or server side). Fork()ed as a genuinely
 * separate process rather than a thread so it has its own address
 * space and cannot be confused with the parent's own env vars/fixture
 * files mid-test.
 * ================================================================== */

#if defined(__aarch64__)
#define ND_SYS_socket      198
#define ND_SYS_bind        200
#define ND_SYS_getsockname 204
#define ND_SYS_sendto      206
#define ND_SYS_recvfrom    207
#define ND_SYS_close       57
#elif defined(__x86_64__)
#define ND_SYS_socket      41
#define ND_SYS_bind        49
#define ND_SYS_getsockname 51
#define ND_SYS_sendto      44
#define ND_SYS_recvfrom    45
#define ND_SYS_close       3
#endif

#if defined(__aarch64__)
static long nd_raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a1;
	register long x1 __asm__("x1") = a2;
	register long x2 __asm__("x2") = a3;
	register long x3 __asm__("x3") = a4;
	register long x4 __asm__("x4") = a5;
	register long x5 __asm__("x5") = a6;
	__asm__ volatile("svc #0"
		: "+r"(x0)
		: "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
		: "memory", "cc");
	return x0;
}
#elif defined(__x86_64__)
static long nd_raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
	long ret;
	register long r10 __asm__("r10") = a4;
	register long r8  __asm__("r8")  = a5;
	register long r9  __asm__("r9")  = a6;
	__asm__ volatile("syscall"
	                 : "=a"(ret)
	                 : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
	                 : "rcx", "r11", "memory");
	return ret;
}
#endif

/* fake_dns_server(): runs in the forked child. Binds a UDP socket to
 * 127.0.0.1:0 (kernel-assigned ephemeral port), writes that port to
 * `portfd` as plain decimal text so the parent can build a resolv.conf
 * fixture around it (this pass's own "nameserver ip:port" testability
 * extension -- see src/netdb/linux/resolv.c's parse_resolv_conf()),
 * then answers exactly one query with a hand-built response: the
 * query's own ID, RCODE 0, one answer RR (A, TTL 60, RDATA
 * 203.0.113.55 -- an RFC 5737 TEST-NET-3 address, real network-address
 * shape, guaranteed not to route anywhere) whose owner name is a
 * compression pointer back to the echoed question section rather than
 * a second literal copy of it -- the one compression case a real
 * server's response genuinely uses. */
static void fake_dns_server(int portfd)
{
	long fd, r;
	struct sockaddr_in addr, client;
	unsigned char query[512], resp[512];
	socklen_t clientlen, addrlen;
	int qlen, rlen, port;
	char portbuf[16];

	fd = nd_raw_syscall(ND_SYS_socket, AF_INET, SOCK_DGRAM, IPPROTO_UDP, 0, 0, 0);
	if (fd < 0) _exit(10);

	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;
	r = nd_raw_syscall(ND_SYS_bind, fd, (long)&addr, sizeof addr, 0, 0, 0);
	if (r < 0) _exit(11);

	addrlen = sizeof addr;
	r = nd_raw_syscall(ND_SYS_getsockname, fd, (long)&addr, (long)&addrlen, 0, 0, 0);
	if (r < 0) _exit(12);
	port = ntohs(addr.sin_port);

	{
		int n = 0, p = port, digs[8], i;
		if (p == 0) digs[n++] = 0;
		while (p > 0) { digs[n++] = p % 10; p /= 10; }
		for (i = 0; i < n; i++) portbuf[i] = (char)('0' + digs[n - 1 - i]);
		portbuf[n] = '\n';
		write(portfd, portbuf, (size_t)(n + 1));
	}
	close(portfd);

	clientlen = sizeof client;
	memset(&client, 0, sizeof client);
	r = nd_raw_syscall(ND_SYS_recvfrom, fd, (long)query, sizeof query, 0,
	                    (long)&client, (long)&clientlen);
	if (r < 12) { nd_raw_syscall(ND_SYS_close, fd, 0, 0, 0, 0, 0); _exit(13); }
	qlen = (int)r;

	resp[0] = query[0]; resp[1] = query[1];       /* ID, echoed */
	resp[2] = 0x81; resp[3] = 0x80;               /* QR=1 RD=1 RA=1 RCODE=0 */
	resp[4] = 0x00; resp[5] = 0x01;               /* QDCOUNT=1 */
	resp[6] = 0x00; resp[7] = 0x01;               /* ANCOUNT=1 */
	resp[8] = 0x00; resp[9] = 0x00;               /* NSCOUNT=0 */
	resp[10] = 0x00; resp[11] = 0x00;             /* ARCOUNT=0 */
	memcpy(resp + 12, query + 12, (size_t)(qlen - 12));
	rlen = 12 + (qlen - 12);
	resp[rlen++] = 0xC0; resp[rlen++] = 0x0C;     /* name: pointer to offset 12 */
	resp[rlen++] = 0x00; resp[rlen++] = 0x01;     /* TYPE=A */
	resp[rlen++] = 0x00; resp[rlen++] = 0x01;     /* CLASS=IN */
	resp[rlen++] = 0x00; resp[rlen++] = 0x00; resp[rlen++] = 0x00; resp[rlen++] = 0x3C; /* TTL=60 */
	resp[rlen++] = 0x00; resp[rlen++] = 0x04;     /* RDLENGTH=4 */
	resp[rlen++] = 203; resp[rlen++] = 0; resp[rlen++] = 113; resp[rlen++] = 55;

	nd_raw_syscall(ND_SYS_sendto, fd, (long)resp, rlen, 0, (long)&client, clientlen);
	nd_raw_syscall(ND_SYS_close, fd, 0, 0, 0, 0, 0);
	_exit(0);
}

static void test_netdb_dns_udp_roundtrip(void)
{
	int pfd[2];
	pid_t child;
	char linebuf[32];
	ssize_t n;
	unsigned long port = 0;
	int i, status;
	char resolvbuf[64];
	struct addrinfo hints, *res = NULL;

	if (pipe(pfd) != 0) { CHECK(0 && "pipe() failed"); return; }

	child = fork();
	CHECK(child >= 0);
	if (child == 0) {
		close(pfd[0]);
		fake_dns_server(pfd[1]);
		_exit(99); /* unreachable: fake_dns_server() always _exit()s */
	}
	close(pfd[1]);

	/* Read "<port>\n" written by the child -- strtoul(), not atoi():
	 * atoi() (src/stdlib/atoi.c) is implemented on top of strtod(),
	 * which on this project's aarch64 native-clang verification build
	 * pulls in a soft-float128 long-double helper this -nostdlib
	 * environment has no compiler-rt to satisfy (see src/netdb/linux/
	 * resolv.c's own identical comment on its "nameserver ip:port"
	 * parser, which hit the exact same real link failure first).
	 * strtoul() has no such dependency. */
	n = read(pfd[0], linebuf, sizeof linebuf - 1);
	close(pfd[0]);
	CHECK(n > 0);
	if (n > 0) {
		linebuf[n] = '\0';
		port = strtoul(linebuf, NULL, 10);
	}
	CHECK(port > 0 && port <= 65535);

	snprintf(resolvbuf, sizeof resolvbuf, "nameserver 127.0.0.1:%lu\n", port);
	fixture_write("nd-resolv.conf", resolvbuf);
	fixture_write("nd-hosts", "127.0.0.1 localhost\n"); /* deliberately no match: force the dns step */
	fixture_write("nd-nsswitch.conf", "hosts: files dns\n");
	CHECK(setenv("NTLIBC_TEST_HOSTS_PATH", "nd-hosts", 1) == 0);
	CHECK(setenv("NTLIBC_TEST_NSSWITCH_PATH", "nd-nsswitch.conf", 1) == 0);
	CHECK(setenv("NTLIBC_TEST_RESOLV_PATH", "nd-resolv.conf", 1) == 0);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	i = getaddrinfo("dnstest.example", NULL, &hints, &res);
	CHECK(i == 0);
	if (i == 0 && res) {
		struct sockaddr_in sin;
		memcpy(&sin, res->ai_addr, sizeof sin);
		CHECK(sin.sin_addr.s_addr == htonl((203u << 24) | (0u << 16) | (113u << 8) | 55u));
		freeaddrinfo(res);
	}

	unsetenv("NTLIBC_TEST_HOSTS_PATH");
	unsetenv("NTLIBC_TEST_NSSWITCH_PATH");
	unsetenv("NTLIBC_TEST_RESOLV_PATH");

	CHECK(waitpid(child, &status, 0) == child);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main(void)
{
	test_posix_netdb_getaddrinfo_loopback();
	test_posix_netdb_gai_strerror_text();

	test_netdb_hosts_fixture();
	test_netdb_nsswitch_hosts_files_only();
	test_netdb_gethostbyname_fixture();
	test_netdb_dns_udp_roundtrip();

	/* Every remaining case below main() in source order is still
	 * fenced (getnameinfo() and the four enumerable databases -- see
	 * this file's own top banner for exactly what this pass built and
	 * what it deliberately left for later). tools/test-policy.py
	 * --pedantic re-decides each one; this comment is not the
	 * authority for it. */
	if (!fails) printf("posix-netdb: all tests passed\n");
	return fails != 0;
}
