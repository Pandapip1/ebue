/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 64-bit integer helpers tcc's i386 code generator calls for /, % and
 * variable-count shifts on long long.  Everything here is written with
 * explicit 32-bit halves so that compiling this file cannot itself emit
 * calls to the functions being defined.
 */
#include "rtlib.h"

union u64 {
	unsigned long long ll;
	long long sll;
	struct { unsigned lo, hi; } h;   /* little-endian */
};

/* q = n / d, r = n % d, by plain binary long division on halves. */
static void udivmod64(union u64 n, union u64 d, union u64 *q, union u64 *r) // NOLINT(bugprone-easily-swappable-parameters) -- dividend, divisor, quotient, and remainder have distinct arithmetic roles
{
	union u64 rem, quo;
	int i;
	rem.h.lo = rem.h.hi = 0;
	quo.h.lo = quo.h.hi = 0;
	for (i = 0; i < 64; i++) {
		unsigned bit;
		/* rem = rem<<1 | top bit of n; n <<= 1 */
		bit = n.h.hi >> 31;
		n.h.hi = (n.h.hi << 1) | (n.h.lo >> 31);
		n.h.lo <<= 1;
		rem.h.hi = (rem.h.hi << 1) | (rem.h.lo >> 31);
		rem.h.lo = (rem.h.lo << 1) | bit;
		/* quo <<= 1 */
		quo.h.hi = (quo.h.hi << 1) | (quo.h.lo >> 31);
		quo.h.lo <<= 1;
		/* if (rem >= d) { rem -= d; quo |= 1; } */
		if (rem.h.hi > d.h.hi ||
		    (rem.h.hi == d.h.hi && rem.h.lo >= d.h.lo)) {
			unsigned borrow = rem.h.lo < d.h.lo;
			rem.h.lo -= d.h.lo;
			rem.h.hi -= d.h.hi + borrow;
			quo.h.lo |= 1;
		}
	}
	if (q) *q = quo;
	if (r) *r = rem;
}

static void neg64(union u64 *a)
{
	a->h.lo = ~a->h.lo;
	a->h.hi = ~a->h.hi;
	a->h.lo++;
	if (!a->h.lo) a->h.hi++;
}

unsigned long long __udivdi3(unsigned long long u, unsigned long long v) // NOLINT(bugprone-easily-swappable-parameters) -- fixed compiler-runtime helper contract
{
	union u64 n, d, q;
	n.ll = u; d.ll = v;
	udivmod64(n, d, &q, 0);
	return q.ll;
}

unsigned long long __umoddi3(unsigned long long u, unsigned long long v) // NOLINT(bugprone-easily-swappable-parameters) -- fixed compiler-runtime helper contract
{
	union u64 n, d, r;
	n.ll = u; d.ll = v;
	udivmod64(n, d, 0, &r);
	return r.ll;
}

long long __divdi3(long long u, long long v) // NOLINT(bugprone-easily-swappable-parameters) -- fixed compiler-runtime helper contract
{
	union u64 n, d, q;
	int neg = 0;
	n.sll = u; d.sll = v;
	if (n.h.hi & 0x80000000u) { neg64(&n); neg = !neg; }
	if (d.h.hi & 0x80000000u) { neg64(&d); neg = !neg; }
	udivmod64(n, d, &q, 0);
	if (neg) neg64(&q);
	return q.sll;
}

long long __moddi3(long long u, long long v) // NOLINT(bugprone-easily-swappable-parameters) -- fixed compiler-runtime helper contract
{
	union u64 n, d, r;
	int neg = 0;
	n.sll = u; d.sll = v;
	if (n.h.hi & 0x80000000u) { neg64(&n); neg = 1; }
	if (d.h.hi & 0x80000000u) neg64(&d);
	udivmod64(n, d, 0, &r);
	if (neg) neg64(&r);
	return r.sll;
}

/* Shifts.  The count tcc passes may be any int; hardware behaviour for
 * counts >= 64 is unspecified, so just handle 0..63 sensibly. */
long long __ashldi3(long long a, int b) // NOLINT(bugprone-easily-swappable-parameters) -- fixed compiler-runtime helper contract
{
	union u64 x;
	x.sll = a;
	b &= 63;
	if (b >= 32) {
		x.h.hi = x.h.lo << (b - 32);
		x.h.lo = 0;
	} else if (b) {
		x.h.hi = (x.h.hi << b) | (x.h.lo >> (32 - b));
		x.h.lo <<= b;
	}
	return x.sll;
}

unsigned long long __lshrdi3(unsigned long long a, int b) // NOLINT(bugprone-easily-swappable-parameters) -- fixed compiler-runtime helper contract
{
	union u64 x;
	x.ll = a;
	b &= 63;
	if (b >= 32) {
		x.h.lo = x.h.hi >> (b - 32);
		x.h.hi = 0;
	} else if (b) {
		x.h.lo = (x.h.lo >> b) | (x.h.hi << (32 - b));
		x.h.hi >>= b;
	}
	return x.ll;
}

long long __ashrdi3(long long a, int b) // NOLINT(bugprone-easily-swappable-parameters) -- fixed compiler-runtime helper contract
{
	union u64 x;
	x.sll = a;
	b &= 63;
	if (b >= 32) {
		x.h.lo = (unsigned)((int)x.h.hi >> (b - 32));
		x.h.hi = (unsigned)((int)x.h.hi >> 31);
	} else if (b) {
		x.h.lo = (x.h.lo >> b) | (x.h.hi << (32 - b));
		x.h.hi = (unsigned)((int)x.h.hi >> b);
	}
	return x.sll;
}
