/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* rand/srand/rand_r: a 64-bit LCG (musl's constants) returning the top
 * 31 bits, so low-order bits are not the usual LCG garbage. */
#include <stdlib.h>
#include <stdint.h>
#include <features.h>

/* rand() before srand() must use the same state as srand(1). */
static uint64_t seed;

/* s - 1 wraps to UINT_MAX for srand(0): still a perfectly usable 64-bit
 * seed once widened, not a bug the caller can observe. */
__wraps void srand(unsigned s) { seed = s - 1; }

/* The LCG multiply is meant to overflow -- that is the whole generator,
 * a fresh 64-bit state every call, taken modulo 2**64 by construction. */
__wraps int rand(void)
{
	seed = 6364136223846793005ULL * seed + 1;
	return (int)(seed >> 33);
}

/* rand_r only has 32 bits of state; use a xorshift-ish temper on a
 * 32-bit LCG.  Both the LCG step and the multiplicative mixing below
 * are meant to overflow modulo 2**32. */
__wraps int rand_r(unsigned *s)
{
	unsigned x = *s;
	x = x * 1103515245u + 12345u;
	*s = x;
	x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
	return (int)(x & 0x7fffffff);
}
