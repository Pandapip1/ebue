/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getservbyname()/getservbyport()/setservent()/getservent()/endservent():
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * endservent.html. A real /etc/services(5) parser, the same
 * "real-database-or-honestly-empty" shape as src/netdb/linux/hosts.c --
 * endservent.html itself says the database's own storage "is
 * unspecified", so a flat-file parser reading the one real system
 * database every Linux distribution actually ships is squarely inside
 * that latitude, not a deviation from it.
 *
 * Line shape: "name port/proto [alias...] [# comment]" -- real
 * /etc/services entries also carry a trailing comment on the SAME line
 * as an alias would (e.g. "ssh 22/tcp # comment", with no alias), which
 * this parser's own whitespace tokenizer would otherwise swallow whole
 * words of into a bogus extra alias; the '#' cut (same as every other
 * parser in this directory) happens before any tokenizing at all, so
 * that never happens.
 *
 * s_port stores the port ALREADY in network byte order (htons()'d at
 * parse time) -- endservent.html's own struct servent member
 * description ("s_port: A value which, when converted to uint16_t,
 * yields the port number in network byte order") and this pass's own
 * test/posix-netdb.c assertion (`(uint16_t)se->s_port == htons(80)`)
 * both require exactly that, matching every real implementation.
 *
 * Non-reentrant static storage: same house style as src/netdb/linux/
 * hostent.c's g_he (endservent.html's own DESCRIPTION already documents
 * global "next entry" position state for getservent(), which is
 * unavoidably non-reentrant by construction regardless of storage
 * choice).
 */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nss_paths.h"

#define SERV_LINE_MAX 512
#define SERV_MAX_ALIASES 16
#define SERV_ALIASBUF_SZ 256

static struct servent g_se;
static char g_se_name[128];
static char g_se_proto[32];
static char g_se_aliasbuf[SERV_ALIASBUF_SZ];
static char *g_se_aliases[SERV_MAX_ALIASES + 1];

/* parse_serv_line(): fills g_se (and its backing static buffers) from
 * one raw /etc/services line. Returns 1 on a real entry, 0 for a line
 * this parser has nothing to say about (blank, comment-only, or
 * malformed -- e.g. no '/proto' suffix, or a non-numeric/out-of-range
 * port) -- every caller's per-line loop just continues past a 0. */
static int parse_serv_line(char *line)
{
	char *hash = strchr(line, '#');
	char *p = line, *nametok, *portproto, *proto, *slash, *end;
	long port;
	size_t n;
	int naliases = 0, off = 0;

	if (hash) *hash = '\0';

	while (*p == ' ' || *p == '\t') p++;
	nametok = p;
	while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
	if (p == nametok) return 0;
	if (*p) *p++ = '\0';

	while (*p == ' ' || *p == '\t') p++;
	portproto = p;
	while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
	if (p == portproto) return 0;
	if (*p) *p++ = '\0';

	slash = strchr(portproto, '/');
	if (!slash) return 0;
	*slash = '\0';
	proto = slash + 1;
	if (*proto == '\0') return 0;

	port = strtol(portproto, &end, 10);
	if (*end != '\0' || port < 0 || port > 65535) return 0;

	n = strlen(nametok);
	if (n >= sizeof g_se_name) n = sizeof g_se_name - 1;
	memcpy(g_se_name, nametok, n);
	g_se_name[n] = '\0';

	n = strlen(proto);
	if (n >= sizeof g_se_proto) n = sizeof g_se_proto - 1;
	{
		size_t i;
		for (i = 0; i < n; i++) g_se_proto[i] = proto[i];
	}
	g_se_proto[n] = '\0';

	for (;;) {
		char *tok;
		size_t toklen;

		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\0' || *p == '\n') break;
		tok = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
		toklen = (size_t)(p - tok);
		if (*p) *p++ = '\0';

		if (naliases < SERV_MAX_ALIASES &&
		    (size_t)off + toklen + 1 <= sizeof g_se_aliasbuf) {
			memcpy(g_se_aliasbuf + off, tok, toklen);
			g_se_aliasbuf[off + (int)toklen] = '\0';
			g_se_aliases[naliases++] = g_se_aliasbuf + off;
			off += (int)toklen + 1;
		}
	}
	g_se_aliases[naliases] = NULL;

	g_se.s_name = g_se_name;
	g_se.s_aliases = g_se_aliases;
	g_se.s_port = (int)htons((unsigned short)port);
	g_se.s_proto = g_se_proto;
	return 1;
}

struct servent *getservbyname(const char *name, const char *proto)
{
	FILE *f = fopen(__NSS_SERVICES_PATH(), "r");
	char line[SERV_LINE_MAX];

	if (!f) return NULL;
	while (fgets(line, sizeof line, f) != NULL) {
		if (!parse_serv_line(line)) continue;
		if (strcmp(g_se_name, name) != 0) continue;
		if (proto && strcmp(g_se_proto, proto) != 0) continue;
		fclose(f);
		return &g_se;
	}
	fclose(f);
	return NULL;
}

struct servent *getservbyport(int port, const char *proto)
{
	FILE *f = fopen(__NSS_SERVICES_PATH(), "r");
	char line[SERV_LINE_MAX];

	if (!f) return NULL;
	while (fgets(line, sizeof line, f) != NULL) {
		if (!parse_serv_line(line)) continue;
		if (g_se.s_port != port) continue;
		if (proto && strcmp(g_se_proto, proto) != 0) continue;
		fclose(f);
		return &g_se;
	}
	fclose(f);
	return NULL;
}

static FILE *g_servf;

void setservent(int stayopen)
{
	(void)stayopen; /* see sethostent()'s identical note (src/netdb/
	                  * linux/hostent.c): this implementation always
	                  * keeps the connection open until endservent(),
	                  * so stayopen has nothing to relax. */
	if (g_servf) rewind(g_servf);
	else g_servf = fopen(__NSS_SERVICES_PATH(), "r");
}

struct servent *getservent(void)
{
	char line[SERV_LINE_MAX];

	if (!g_servf) {
		g_servf = fopen(__NSS_SERVICES_PATH(), "r");
		if (!g_servf) return NULL;
	}
	while (fgets(line, sizeof line, g_servf) != NULL)
		if (parse_serv_line(line)) return &g_se;
	return NULL;
}

void endservent(void)
{
	if (g_servf) { fclose(g_servf); g_servf = NULL; }
}
