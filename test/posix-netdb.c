/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Coverage fences for <netdb.h>, which ntlibc does not have.
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

#if NTLIBC_TEST(UNIMPL, posix_netdb_getaddrinfo_loopback)
#include <netdb.h>
#include <netinet/in.h>
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
#endif

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

#if NTLIBC_TEST(UNIMPL, posix_netdb_gai_strerror_text)
#include <netdb.h>

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
#endif

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

int main(void)
{
	/* Every case here is fenced: include/ has no netdb.h, so none of
	 * these translation units resolve.  tools/test-policy.py
	 * --pedantic re-decides each one, and the day the header appears
	 * the probe stops agreeing and each fence must be re-adjudicated
	 * against what the implementation actually does. */
	if (!fails) printf("posix-netdb: all tests passed\n");
	return fails != 0;
}
