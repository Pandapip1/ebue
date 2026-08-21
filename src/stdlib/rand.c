/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* rand/srand/rand_r: a 64-bit LCG (musl's constants) returning the top
 * 31 bits, so low-order bits are not the usual LCG garbage. */
#include <stdlib.h>
#include <stdint.h>

static uint64_t seed = 1;

void srand(unsigned s) { seed = s - 1; }

int rand(void)
{
	seed = 6364136223846793005ULL * seed + 1;
	return (int)(seed >> 33);
}

/* rand_r only has 32 bits of state; use a xorshift-ish temper on a
 * 32-bit LCG. */
int rand_r(unsigned *s)
{
	unsigned x = *s;
	x = x * 1103515245u + 12345u;
	*s = x;
	x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
	return (int)(x & 0x7fffffff);
}
