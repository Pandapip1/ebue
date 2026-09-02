/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <netdb.h>'s Windows NT backend. The real, working implementation
 * (a /etc/hosts parser, a real /etc/nsswitch.conf-driven dispatcher,
 * and a real UDP DNS stub resolver over raw syscalls) was built for
 * native Linux only -- see src/netdb/linux/*.c and that pass's own
 * scope banner in include/netdb.h. That left getaddrinfo()/
 * gethostbyname() DECLARED (include/netdb.h is platform-shared) but
 * never DEFINED on NT: a real, disclosed gap `make linkcheck` catches
 * for exactly this reason (a public declaration with no reachable
 * definition is worse than no declaration at all -- a caller gets a
 * confusing link error instead of a clear "not on this platform").
 *
 * This file is the honest stand-in: every entry point compiles and
 * links, and reports a real, specified failure -- EAI_FAIL ("a
 * non-recoverable failure in name resolution occurred", the exact
 * DESCRIPTION wording for "the implementation does not support name
 * resolution on this platform") -- rather than fabricating an answer
 * or silently succeeding with garbage. A real NT resolver (DnsQuery_
 * over NTDLL, or the same /etc/hosts + resolv.conf technique the
 * Linux backend uses, ported to NT's own file-path conventions) is
 * future work, not attempted here; this file exists so the symbol
 * table is honest in the meantime, matching how other genuinely
 * NT-side-missing functions in this tree (see include/unistd.h's own
 * NA-marked entries) are handled -- fail loud and documented, never
 * silent.
 */
#include <netdb.h>
#include <stddef.h>

int h_errno;

int getaddrinfo(const char *__restrict node, const char *__restrict service,
                 const struct addrinfo *__restrict hints,
                 struct addrinfo **__restrict res)
{
	(void)node; (void)service; (void)hints;
	*res = NULL;
	return EAI_FAIL;
}

struct hostent *gethostbyname(const char *name)
{
	(void)name;
	h_errno = 3 /* NO_RECOVERY, the traditional resolver value for
	             * "a non-recoverable name server error occurred" --
	             * <netdb.h> does not itself define the h_errno
	             * constants (they are outside this edition of POSIX,
	             * per this file's own banner in the Linux backend),
	             * so this is the plain historical value every other
	             * gethostbyname() implementation uses for the same
	             * condition, not a value this tree invented. */;
	return NULL;
}
