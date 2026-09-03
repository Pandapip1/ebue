/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * parse_response() -- src/netdb/linux/resolv.c's DNS wire-format reader,
 * the one piece of this stub resolver that touches genuinely
 * attacker-controlled bytes: whatever a UDP socket handed recvfrom() in
 * try_one_server(), unvalidated, including the RFC 1035 sec 4.1.4
 * label-compression pointer walk (skip_name()) that is the classic place
 * for an infinite loop or an out-of-bounds read in a DNS parser.
 *
 * parse_response() and skip_name() are both `static`, and src/netdb/
 * linux/ isn't part of the library ../tools/asan-build.sh builds for
 * every other harness (this native harness exercises the NT backend
 * through ntstubs.c, and src/netdb/nt/plat_netdb.c never calls
 * parse_response()/__resolv_query_a()/__nsswitch_order() at all) -- so
 * the function is simply absent from that library today. Un-static-ing
 * it or widening asan-build.sh's file selection were both rejected as
 * bigger, separate changes than adding a harness deserves; instead this
 * file #includes the real resolv.c directly. Not a copy -- the exact
 * bytes are compiled here, so a future edit to resolv.c changes what
 * this harness fuzzes automatically, and resolv.c itself stays
 * untouched. fuzz_shparse.c already reaches into src/sh/ by relative
 * path for the same reason, one level further here since the thing
 * under test has no header declaration to reach through.
 *
 * try_one_server()'s own raw_syscall()-based socket()/connect()/
 * sendto()/recvfrom() calls come along for the ride but are never
 * invoked here: this harness calls parse_response() directly on fuzzer
 * bytes, never __resolv_query_a(), so no real socket is ever opened.
 */
#include <string.h>
#include <stdint.h>
#include <netinet/in.h>

#include "../src/netdb/linux/resolv.c"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

/* try_one_server()'s own rbuf[1500]: a real UDP response can never be
 * larger than what recvfrom() was given room for, so this is the
 * realistic cap on what parse_response() is ever actually handed. */
#define CAP 1500
#define GUARD 0xAB

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	unsigned char msg[CAP];
	struct {
		struct in_addr a[8];
		unsigned char guard[16];
	} out;
	uint16_t id;
	int maxaddrs, n, i;
	enum __dns_fail reason;
	int is_nx;
	size_t n_copy;

	if (size < 2) return 0;

	/* id is taken FROM the message itself: parse_response's first real
	 * check is `be16(msg) != id`, and a harness-chosen constant id
	 * would fail that check on all but 1-in-65536 inputs, fuzzing
	 * nothing past line one of the function. */
	id = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);

	n_copy = size < CAP ? size : CAP;
	memcpy(msg, data, n_copy);

	maxaddrs = (data[0] & 0x07) + 1;   /* 1..8, within out.a's capacity */
	memset(&out, GUARD, sizeof out);
	reason = (enum __dns_fail)-1;      /* sentinel: parse_response must
	                                     * overwrite this with a real
	                                     * value whenever it returns -1. */
	is_nx = -1;                        /* likewise for *is_nxdomain, which
	                                     * the function sets unconditionally
	                                     * as its very first statement. */

	n = parse_response(msg, (int)n_copy, id, out.a, maxaddrs, &reason, &is_nx);

	/* *is_nxdomain is set to 0 or 1 before parse_response does anything
	 * else with `msg`, on every path including the len<12 one -- so this
	 * holds regardless of what `n` came back as. */
	if (is_nx != 0 && is_nx != 1)
		oracle_mismatch_i("parse_response left *is_nxdomain unset", "",
		                  (long long)is_nx, 0);

	/* Documented range: -2 (stray/mismatched reply, caller retries), -1
	 * (hard failure, *reason set) or a found-count in [0, maxaddrs]. */
	if (n < -2) {
		oracle_mismatch_i("parse_response returned below its documented range",
		                  "", (long long)n, -2);
	} else if (n == -1) {
		switch (reason) {
		case __DNS_IOERR: case __DNS_FORMERR: case __DNS_SERVFAIL:
		case __DNS_REFUSED: case __DNS_NOSERVERS: case __DNS_TIMEOUT:
		case __DNS_NXDOMAIN:
			break;
		default:
			oracle_mismatch_i("parse_response returned -1 without a real *reason",
			                  "", (long long)reason, 0);
		}
	} else if (n > maxaddrs) {
		oracle_mismatch_i("parse_response collected more addresses than maxaddrs",
		                  "", (long long)n, (long long)maxaddrs);
	}

	/* Never writes past out.a[maxaddrs): in ubsan mode there is no
	 * shadow memory to catch a heap/stack overflow on its own, so this
	 * checks it by hand, like fuzz_string.c/fuzz_inet.c already do. */
	for (i = 0; i < 16; i++)
		if (out.guard[i] != GUARD)
			oracle_mismatch_i("parse_response wrote past its addrs array",
			                  "", (long long)i, GUARD);
	for (i = maxaddrs; i < 8; i++) {
		unsigned char *p = (unsigned char *)&out.a[i];
		size_t j;
		for (j = 0; j < sizeof out.a[i]; j++)
			if (p[j] != GUARD) {
				oracle_mismatch_i("parse_response wrote past maxaddrs",
				                  "", (long long)i, (long long)maxaddrs);
				break;
			}
	}

	return 0;
}
