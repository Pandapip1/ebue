/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * gethostbyname()/h_errno: a legacy/XSI extension outside this edition
 * of POSIX, kept as a real, disclosed addition on this pass's own
 * explicit request -- see include/netdb.h's own banner for why, and
 * for why herror()/hstrerror() are not included alongside it.
 *
 * This is a second, thinner front door onto the exact same
 * __hosts_resolve() walk src/netdb/linux/addrinfo.c's getaddrinfo()
 * already uses -- not a second resolver, and not a second nsswitch
 * order decision.
 *
 * Non-reentrant, growable shared static storage: the same pattern
 * src/misc/pwd.c's fill_shared()/g_pw/g_pwbuf already establishes for
 * exactly the same "POSIX-sanctioned static-storage function, no
 * caller-supplied buffer to size, but no bound on the real answer's
 * size either" shape (there, %USERPROFILE%/%ComSpec%; here, an
 * unbounded number of A records for a real DNS name). h_name,
 * h_addr_list (n+1 pointers, each pointing at a 4-byte address, the
 * array NUL-pointer-terminated per <netdb.h>'s own DESCRIPTION) and
 * their backing bytes all live in one packed, growable buffer;
 * h_aliases is a separate static {NULL} array (this pass tracks no
 * alias list -- see src/netdb/linux/hosts.c's own banner on why
 * reverse/alias enumeration is out of scope -- but the member itself
 * must still be a valid non-NULL "terminated by a null pointer" array
 * per DESCRIPTION, which a single static {NULL} already is).
 */
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "netdb_internal.h"

int h_errno;

#define MAX_RESULT_ADDRS 32

static struct hostent g_he;
static char *g_hebuf;
static size_t g_hebufsz;
static char *g_he_aliases[1]; /* always {NULL}; see this file's banner */

/* fill_he(): packs gethostbyname()'s answer for `name` into buf
 * (bufsz bytes). Returns 1 on success, 0 on a clean failure (h_errno
 * already set to which one), or ERANGE if buf is too small (*needp
 * set, h_errno left untouched -- not yet a real failure). pw/buf
 * follow src/misc/pwd.c's fill_current() precedent exactly: g_he is
 * required (every real call site passes &g_he, unconditional writes
 * below), buf is deliberately not required (the `if (need > bufsz)`
 * guard covers every dereference of it, and gethostbyname()'s own
 * first probing call legitimately passes bufsz == 0). */
static int fill_he(const char *name, char *buf, size_t bufsz, size_t *needp)
    __attribute__((nonnull(1)));
static int fill_he(const char *name, char *buf, size_t bufsz, size_t *needp)
{
	struct in_addr addrs[MAX_RESULT_ADDRS];
	char canon[256];
	int eai = 0;
	int n = __hosts_resolve(name, addrs, MAX_RESULT_ADDRS, canon, sizeof canon, &eai);
	const char *hname;
	size_t namelen, pad, ptrbytes, databytes, need;
	char *p;
	char **addrlist;
	int i;

	if (n < 0) {
		h_errno = (eai == EAI_AGAIN) ? TRY_AGAIN : NO_RECOVERY;
		return 0;
	}
	if (n == 0) {
		h_errno = HOST_NOT_FOUND;
		return 0;
	}

	hname = canon[0] ? canon : name;
	namelen = strlen(hname) + 1;
	pad = (sizeof(char *) - ((uintptr_t)(buf + namelen) % sizeof(char *))) % sizeof(char *);
	ptrbytes = (size_t)(n + 1) * sizeof(char *);
	databytes = (size_t)n * 4;
	need = namelen + pad + ptrbytes + databytes;
	if (need > bufsz) { if (needp) *needp = need; return ERANGE; }

	p = buf;
	memcpy(p, hname, namelen);
	g_he.h_name = p;
	p += namelen + pad;

	addrlist = (char **)(void *)p;
	p += ptrbytes;
	for (i = 0; i < n; i++) {
		memcpy(p, &addrs[i], 4);
		addrlist[i] = p;
		p += 4;
	}
	addrlist[n] = NULL;

	g_he.h_aliases = g_he_aliases;
	g_he.h_addrtype = AF_INET;
	g_he.h_length = 4;
	g_he.h_addr_list = addrlist;
	return 1;
}

struct hostent *gethostbyname(const char *name)
{
	size_t need = 0;
	int r = fill_he(name, g_hebuf, g_hebufsz, &need);
	char *nb;

	if (r == ERANGE) {
		nb = realloc(g_hebuf, need);
		if (!nb) { h_errno = TRY_AGAIN; return NULL; }
		g_hebuf = nb;
		g_hebufsz = need;
		r = fill_he(name, g_hebuf, g_hebufsz, &need);
		if (r == ERANGE) { h_errno = TRY_AGAIN; return NULL; }
	}
	if (r != 1) return NULL;
	h_errno = 0;
	return &g_he;
}
