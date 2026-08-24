/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <arpa/inet.h>: byte-order conversion and address-text helpers.  Every
 * one of these is pure C with no NT dependency at all -- ntlibc only
 * targets i386/x86_64 (arch/i386, arch/x86_64), both little-endian, so
 * "network byte order" (big-endian, historically) and "host byte
 * order" are never the same and the swap below is unconditional; there
 * is no third architecture here for a compile-time endianness probe to
 * matter for.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* htonl.html: "convert...to network byte order". */
uint32_t htonl(uint32_t h)
{
	return ((h & 0x000000ffU) << 24) | ((h & 0x0000ff00U) << 8) |
	       ((h & 0x00ff0000U) >> 8) | ((h & 0xff000000U) >> 24);
}

uint16_t htons(uint16_t h)
{
	return (uint16_t)(((h & 0x00ffU) << 8) | ((h & 0xff00U) >> 8));
}

/* htonl.html: ntohl()/ntohs() "deliver a value converted from network
 * byte order to host byte order" -- the swap is its own inverse. */
uint32_t ntohl(uint32_t n) { return htonl(n); }
uint16_t ntohs(uint16_t n) { return htons(n); }

/* inet_addr.html: dotted forms "a.b.c.d", "a.b.c", "a.b", "a"; each
 * part decimal, octal (leading 0) or hexadecimal (leading 0x/0X);
 * "(in_addr_t)(-1)" (INADDR_NONE) on a malformed string. */
in_addr_t inet_addr(const char *s)
{
	unsigned long parts[4];
	int nparts = 0;
	const char *p = s;

	if (!s) return INADDR_NONE;
	for (;;) {
		char *end;
		unsigned long v;
		if (nparts == 4) return INADDR_NONE;
		errno = 0;
		v = strtoul(p, &end, 0); /* base 0: honours "0x"/"0" prefixes, per inet_addr.html */
		if (end == p) return INADDR_NONE;
		parts[nparts++] = v;
		p = end;
		if (*p == '.') { p++; continue; }
		break;
	}
	if (*p) return INADDR_NONE; /* trailing garbage */

	switch (nparts) {
	case 1:
		if (parts[0] > 0xffffffffUL) return INADDR_NONE;
		return htonl((uint32_t)parts[0]);
	case 2:
		if (parts[0] > 0xff || parts[1] > 0xffffffUL) return INADDR_NONE;
		return htonl(((uint32_t)parts[0] << 24) | (uint32_t)parts[1]);
	case 3:
		if (parts[0] > 0xff || parts[1] > 0xff || parts[2] > 0xffffUL) return INADDR_NONE;
		return htonl(((uint32_t)parts[0] << 24) | ((uint32_t)parts[1] << 16) | (uint32_t)parts[2]);
	case 4:
		if (parts[0] > 0xff || parts[1] > 0xff || parts[2] > 0xff || parts[3] > 0xff) return INADDR_NONE;
		return htonl(((uint32_t)parts[0] << 24) | ((uint32_t)parts[1] << 16) |
		             ((uint32_t)parts[2] << 8) | (uint32_t)parts[3]);
	default:
		return INADDR_NONE;
	}
}

/* inet_addr.html/inet_ntop.html: inet_ntoa() has no ERRORS/RETURN VALUE
 * failure case in the spec at all -- it "return[s] a pointer to [a]
 * string"; POSIX documents its static buffer explicitly ("need not be
 * reentrant"), so one process-wide buffer is conforming. */
char *inet_ntoa(struct in_addr in)
{
	static char buf[INET_ADDRSTRLEN];
	unsigned char *b = (unsigned char *)&in.s_addr;
	snprintf(buf, sizeof buf, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
	return buf;
}

/* inet_ntop.html: AF_INET only here (AF_INET6 out of scope, see
 * <sys/socket.h>'s banner) -- "-1...errno...EAFNOSUPPORT" for anything
 * else; ENOSPC "size...too small". */
const char *inet_ntop(int af, const void *__restrict src, char *__restrict dst, socklen_t size)
{
	char buf[INET_ADDRSTRLEN];
	const unsigned char *b;
	int n;

	if (af != AF_INET) { errno = EAFNOSUPPORT; return 0; }
	b = (const unsigned char *)src;
	n = snprintf(buf, sizeof buf, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
	if (n < 0 || (socklen_t)n >= size) { errno = ENOSPC; return 0; }
	memcpy(dst, buf, (size_t)n + 1);
	return dst;
}

/* inet_pton.html: "1"/success, "0"/not a valid presentation string for
 * af, "-1"+EAFNOSUPPORT for an unrecognised af.  Strict dotted-quad
 * only -- unlike inet_addr(), inet_pton() is not specified to accept
 * the a/a.b/a.b.c short forms or octal/hex parts. */
int inet_pton(int af, const char *__restrict src, void *__restrict dst)
{
	unsigned parts[4];
	int i;
	const char *p = src;

	if (af != AF_INET) { errno = EAFNOSUPPORT; return -1; }
	if (!src) return 0;

	for (i = 0; i < 4; i++) {
		int digits = 0;
		unsigned v = 0;
		if (i) { if (*p != '.') return 0; p++; }
		while (*p >= '0' && *p <= '9') {
			v = v * 10 + (unsigned)(*p - '0');
			if (v > 255) return 0;
			p++; digits++;
			if (digits > 3) return 0;
		}
		if (!digits) return 0;
		parts[i] = v;
	}
	if (*p) return 0; /* trailing garbage */

	{
		unsigned char *out = (unsigned char *)dst;
		out[0] = (unsigned char)parts[0];
		out[1] = (unsigned char)parts[1];
		out[2] = (unsigned char)parts[2];
		out[3] = (unsigned char)parts[3];
	}
	return 1;
}
