/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __hosts_lookup(): the "files" service for the "hosts" NSS database
 * -- a real /etc/hosts(5) parser. See src/netdb/linux/netdb_internal.h
 * for this function's exact contract.
 *
 * Deliberately NOT built: reverse (address -> name) lookup. Nothing in
 * this pass's public surface needs it -- getaddrinfo()/gethostbyname()
 * are both forward-only, and getnameinfo() (the POSIX interface that
 * WOULD need it) is out of scope for this pass entirely (see
 * include/netdb.h's own banner). A reverse walk over this same file
 * would be a small addition on top of the forward scan below, not a
 * different parser; left for whenever getnameinfo() is built.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include "nss_paths.h"
#include "netdb_internal.h"

#define HOSTS_LINE_MAX 512

/* True if `tok` (a NUL-terminated whitespace-delimited field already
 * split out of the line) is a real IPv6 literal this pass does not
 * parse -- detected the cheap, sufficient way: a dotted-quad IPv4
 * address never contains ':', and nothing else legitimately appears
 * in /etc/hosts's address column. */
static int looks_like_v6(const char *tok)
{
	return strchr(tok, ':') != NULL;
}

int __hosts_lookup(const char *name, struct in_addr *addrs, int maxaddrs,
                    char *canon, size_t canonsz)
{
	FILE *f;
	char line[HOSTS_LINE_MAX];
	int found = 0;

	f = fopen(__NSS_HOSTS_PATH(), "r");
	if (!f) return 0;

	while (fgets(line, sizeof line, f) != NULL) {
		char *p = line;
		char *hash = strchr(line, '#');
		char *addrtok, *nametok;
		struct in_addr a;
		int matched;

		if (hash) *hash = '\0';

		while (*p == ' ' || *p == '\t') p++;
		addrtok = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
		if (p == addrtok) continue; /* blank/comment-only line */
		if (*p) *p++ = '\0';

		if (looks_like_v6(addrtok)) continue;
		if (inet_pton(AF_INET, addrtok, &a) != 1) continue;

		/* Remaining whitespace-separated fields: canonical name
		 * first, then zero or more aliases -- hosts(5)'s own
		 * documented layout. `name` matches any of them. */
		matched = 0;
		nametok = NULL;
		for (;;) {
			char *tok;
			while (*p == ' ' || *p == '\t') p++;
			if (*p == '\0' || *p == '\n') break;
			tok = p;
			while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
			if (*p) *p++ = '\0';
			if (!nametok) nametok = tok;
			if (strcasecmp(tok, name) == 0) matched = 1;
		}
		if (!matched || !nametok) continue;

		if (found == 0 && canon && canonsz > 0) {
			size_t n = strlen(nametok);
			if (n >= canonsz) n = canonsz - 1;
			memcpy(canon, nametok, n);
			canon[n] = '\0';
		}
		if (found < maxaddrs) addrs[found] = a;
		found++;
	}

	fclose(f);
	return found;
}
