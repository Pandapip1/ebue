/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * parse_response() -- src/netdb/linux/resolv.c's DNS wire-format reader,
 * the one piece of this stub resolver that touches genuinely
 * attacker-controlled bytes: whatever a UDP socket handed recvfrom() in
 * try_one_server(), unvalidated, including the RFC 1035 sec 4.1.4
 * label-compression pointer walk (skip_name(), below it in the same
 * file) that is the classic place for an infinite loop or an
 * out-of-bounds read in a DNS parser.
 *
 * WHY #include, NOT extern + link.  parse_response() and skip_name()
 * are both `static` -- resolv.c's own file-private helpers of
 * __resolv_query_a(), never meant to be called from outside that
 * translation unit -- and src/netdb/linux/ as a whole is not part of
 * the library ../tools/asan-build.sh builds for every other harness in
 * this directory in the first place: that script's own comment above
 * its `linux)` case says this native harness exercises the NT backend
 * through ntstubs.c, and every OTHER platform-split module has an nt/
 * counterpart standing in for the linux/ one it skips. src/netdb/linux/
 * has no such counterpart -- src/netdb/nt/plat_netdb.c is a
 * self-contained NT implementation that never calls parse_response(),
 * __resolv_query_a() or __nsswitch_order() at all -- so the two files
 * this pass wants fuzzed are simply absent from that library today, not
 * present under different behaviour.
 *
 * Two ways to close that gap were considered and rejected before this
 * one:
 *
 *   - Un-static parse_response() and prototype it in netdb_internal.h,
 *     the way __hosts_lookup()/__resolv_query_a() already are.  Rejected:
 *     that is a real change to resolv.c's own internal API surface for
 *     this harness's benefit alone, and this file's job is to fuzz the
 *     function as it already is, not to reshape resolv.c first.
 *   - Widen ../tools/asan-build.sh's file selection to also compile the
 *     .c files under src/netdb/linux/ into the shared fuzz library.  Rejected: that
 *     script's skip is a deliberate, load-bearing policy documented at
 *     the point of the skip itself (see the comment cited above), shared
 *     by every other harness and by `make asan`'s own native test binaries;
 *     changing it to accommodate two files is a bigger, separate decision
 *     than adding a harness.
 *
 * What is left, and what this file does: #include the real resolv.c
 * directly.  Not a copy -- the exact bytes of the file are compiled
 * here, so a future edit to resolv.c changes what this harness fuzzes
 * automatically, and resolv.c itself is untouched: no `static` removed,
 * no prototype added anywhere, no change to ../tools/asan-build.sh's
 * file selection for every other harness. fuzz_shparse.c already
 * reaches into src/sh/ by relative path (`#include "../src/sh/sh.h"`)
 * for the same "the harness lives in fuzz/, the real header lives in
 * src/" reason; this is the same idea one level further, on a .c file
 * instead of a .h, because the thing under test here has no header
 * declaration to reach through.
 *
 * try_one_server()'s own raw_syscall()-based socket()/connect()/
 * sendto()/recvfrom() calls come along for the ride (they are compiled,
 * being in the same file) but are never invoked from here: this harness
 * calls parse_response() directly on fuzzer bytes, never
 * __resolv_query_a(), so no real socket is ever opened and no real
 * network I/O ever happens.
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

	/* id is taken FROM the message itself, at exactly the offset
	 * be16(msg) reads: parse_response's first real check is
	 * `be16(msg) != id`, and a harness-chosen constant id would fail
	 * that check on all but 1-in-65536 inputs, fuzzing nothing past
	 * line one of the function. A real caller's id is whatever it put
	 * in its own query -- unrelated to the bytes of somebody else's
	 * reply -- so deriving it from the input here, rather than a fixed
	 * value, is the harness's own choice of how to reach the rest of
	 * the parser, not a change to the function's contract. */
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

	/* Never writes past out.a[maxaddrs), the "bring your own guard"
	 * discipline fuzz/Makefile's SAN_MODE comment asks for: in ubsan
	 * mode there is no shadow memory to catch a heap/stack overflow on
	 * its own, so a harness that wants one noticed here has to check it
	 * by hand, the way fuzz_string.c/fuzz_inet.c already do. */
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
