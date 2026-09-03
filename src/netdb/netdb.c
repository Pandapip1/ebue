/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <netdb.h> pieces with no OS dependency at all, shared between every
 * platform. gai_strerror() is a pure code->string table. freeaddrinfo()
 * only ever walks a list this library's own getaddrinfo() built out of
 * malloc()'d nodes, so the same walk is correct whether it came from
 * the real Linux resolver or NT's stub (which never allocates, so the
 * walk safely no-ops).
 */
#include <netdb.h>
#include <stdlib.h>

void freeaddrinfo(struct addrinfo *res)
{
	while (res) {
		struct addrinfo *next = res->ai_next;
		free(res->ai_addr);
		free(res->ai_canonname);
		free(res);
		res = next;
	}
}

const char *gai_strerror(int code)
{
	switch (code) {
	case 0:             return "Success";
	case EAI_AGAIN:     return "Temporary failure in name resolution";
	case EAI_BADFLAGS:  return "Invalid value for ai_flags";
	case EAI_FAIL:      return "Non-recoverable failure in name resolution";
	case EAI_FAMILY:    return "ai_family not supported";
	case EAI_MEMORY:    return "Memory allocation failure";
	case EAI_NONAME:    return "Name or service not known";
	case EAI_SERVICE:   return "Servname not supported for ai_socktype";
	case EAI_SOCKTYPE:  return "ai_socktype not supported";
	case EAI_SYSTEM:    return "System error";
	case EAI_OVERFLOW:  return "Argument buffer overflow";
	default:            return "Unknown error";
	}
}
