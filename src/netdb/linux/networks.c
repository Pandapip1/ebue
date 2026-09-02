/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * setnetent()/getnetent()/endnetent()/getnetbyname()/getnetbyaddr():
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * endnetent.html. A real /etc/networks(5) parser -- same shape as this
 * directory's hosts.c/services.c/protocols.c, and the same
 * "real-database-or-honestly-empty" discipline as hosts.c's own
 * fopen()-returns-NULL-means-zero-entries handling: /etc/networks is
 * routinely just absent on a real machine (most distributions ship no
 * networks database at all -- there is no well-known IANA-style
 * assignment list for it the way /etc/services and /etc/protocols both
 * have), so a missing file here is this database's own normal empty
 * state, not an error, exactly like __hosts_lookup()'s identical
 * `if (!f) return 0;`.
 *
 * Line shape: "name net-number [alias...] [# comment]". n_net's own
 * contract is "The network number, in host byte order", and net-number
 * is parsed with <arpa/inet.h>'s own inet_addr() (inet_addr.html),
 * ntohl()'d to flip its network-byte-order result to host order --
 * *not* the classic BSD inet_network() a real /etc/networks parser
 * would traditionally use, which this tree does not have. The two are
 * NOT interchangeable for every input: inet_addr()'s own short-form
 * rules ("a", "a.b", "a.b.c" -- inet_addr.html) fill a full class-style
 * HOST address, so a short form's later parts occupy the address's
 * LOW-order bytes rather than padding zeros after the given octets
 * (checked directly against this tree's own inet_addr(), not assumed:
 * inet_addr("127") is 0.0.0.127, and inet_addr("128.10") is
 * 128.0.0.10, not "128.10.0.0"). A real /etc/networks(5) file's
 * entries are near-universally already written in full or
 * trailing-zero dotted form ("loopback 127.0.0.0", "link-local
 * 169.254.0.0", "default 0.0.0.0" -- Debian's own stock file), for
 * which inet_addr()'s and inet_network()'s results coincide exactly
 * (a "b"/"c"/"d" part of literal 0 is indistinguishable from an
 * omitted one either way), so this reuse is correct for the database
 * as it actually appears in practice; a genuinely abbreviated non-zero
 * short form (e.g. a real "128.10" meaning network 128.10.0.0, not
 * host 128.0.0.10) is a real, disclosed gap this parser does not
 * special-case rather than a silently wrong answer -- it would be
 * parsed as an address, not a network prefix, exactly as inet_addr()
 * itself defines that string.
 */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include "nss_paths.h"

#define NET_LINE_MAX 512
#define NET_MAX_ALIASES 16
#define NET_ALIASBUF_SZ 256

static struct netent g_ne;
static char g_ne_name[128];
static char g_ne_aliasbuf[NET_ALIASBUF_SZ];
static char *g_ne_aliases[NET_MAX_ALIASES + 1];

/* parse_net_line(): fills g_ne from one raw /etc/networks line. Returns
 * 1 on a real entry, 0 for a line this parser has nothing to say about
 * (blank, comment-only, or a net-number inet_addr() cannot parse --
 * inet_addr()'s own documented failure value, INADDR_NONE i.e.
 * 0xffffffff, is indistinguishable from a real "255.255.255.255"
 * network, but that address can never legitimately be a *network*
 * number -- an all-ones network would leave no host bits at all -- so
 * treating it as "unparsable, skip this line" is the correct call
 * either way, not a real ambiguity). */
static int parse_net_line(char *line)
{
	char *hash = strchr(line, '#');
	char *p = line, *nametok, *numtok;
	in_addr_t parsed;
	size_t n;
	int naliases = 0, off = 0;

	if (hash) *hash = '\0';

	while (*p == ' ' || *p == '\t') p++;
	nametok = p;
	while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
	if (p == nametok) return 0;
	if (*p) *p++ = '\0';

	while (*p == ' ' || *p == '\t') p++;
	numtok = p;
	while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
	if (p == numtok) return 0;
	if (*p) *p++ = '\0';

	parsed = inet_addr(numtok);
	if (parsed == (in_addr_t)-1) return 0;

	n = strlen(nametok);
	if (n >= sizeof g_ne_name) n = sizeof g_ne_name - 1;
	memcpy(g_ne_name, nametok, n);
	g_ne_name[n] = '\0';

	for (;;) {
		char *tok;
		size_t toklen;

		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\0' || *p == '\n') break;
		tok = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
		toklen = (size_t)(p - tok);
		if (*p) *p++ = '\0';

		if (naliases < NET_MAX_ALIASES &&
		    (size_t)off + toklen + 1 <= sizeof g_ne_aliasbuf) {
			memcpy(g_ne_aliasbuf + off, tok, toklen);
			g_ne_aliasbuf[off + (int)toklen] = '\0';
			g_ne_aliases[naliases++] = g_ne_aliasbuf + off;
			off += (int)toklen + 1;
		}
	}
	g_ne_aliases[naliases] = NULL;

	g_ne.n_name = g_ne_name;
	g_ne.n_aliases = g_ne_aliases;
	g_ne.n_addrtype = AF_INET;
	g_ne.n_net = ntohl(parsed);
	return 1;
}

struct netent *getnetbyname(const char *name)
{
	FILE *f = fopen(__NSS_NETWORKS_PATH(), "r");
	char line[NET_LINE_MAX];

	if (!f) return NULL;
	while (fgets(line, sizeof line, f) != NULL) {
		if (!parse_net_line(line)) continue;
		if (strcmp(g_ne_name, name) != 0) continue;
		fclose(f);
		return &g_ne;
	}
	fclose(f);
	return NULL;
}

struct netent *getnetbyaddr(uint32_t net, int type)
{
	FILE *f;
	char line[NET_LINE_MAX];

	if (type != AF_INET) return NULL; /* the only network address type
	                                    * this database's own parser
	                                    * ever produces -- see this
	                                    * file's banner. */
	f = fopen(__NSS_NETWORKS_PATH(), "r");
	if (!f) return NULL;
	while (fgets(line, sizeof line, f) != NULL) {
		if (!parse_net_line(line)) continue;
		if (g_ne.n_net != net) continue;
		fclose(f);
		return &g_ne;
	}
	fclose(f);
	return NULL;
}

static FILE *g_netf;

void setnetent(int stayopen)
{
	(void)stayopen; /* see services.c's setservent() identical note */
	if (g_netf) rewind(g_netf);
	else g_netf = fopen(__NSS_NETWORKS_PATH(), "r");
}

struct netent *getnetent(void)
{
	char line[NET_LINE_MAX];

	if (!g_netf) {
		g_netf = fopen(__NSS_NETWORKS_PATH(), "r");
		if (!g_netf) return NULL; /* no database: a clean, immediate
		                            * end-of-database -- see this
		                            * file's banner. */
	}
	while (fgets(line, sizeof line, g_netf) != NULL)
		if (parse_net_line(line)) return &g_ne;
	return NULL;
}

void endnetent(void)
{
	if (g_netf) { fclose(g_netf); g_netf = NULL; }
}
