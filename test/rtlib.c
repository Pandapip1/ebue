/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <limits.h>
#include <stdio.h>
#include "../src/internal/rtlib.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; \
	printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
	/* Every signed float-to-64-bit helper used to negate LLONG_MIN when
	 * given the exactly representable lower endpoint.  Exercise them by
	 * name so native UBSan instruments the runtime implementation rather
	 * than relying on which helper a particular compiler chooses to emit. */
	CHECK(__fixsfdi(-0x1p63f) == LLONG_MIN);
	CHECK(__fixdfdi(-0x1p63) == LLONG_MIN);
	CHECK(__fixxfdi(-0x1p63L) == LLONG_MIN);
	CHECK(__fixsfdi(-123.0f) == -123);
	CHECK(__fixdfdi(-123.0) == -123);
	CHECK(__fixxfdi(-123.0L) == -123);

	/* Out-of-range C casts have undefined semantics, but the internal
	 * helpers promise a deterministic saturation instead of acquiring UB
	 * of their own while implementing that caller operation. */
	CHECK(__fixsfdi(0x1p63f) == LLONG_MAX);
	CHECK(__fixdfdi(0x1p63) == LLONG_MAX);
	CHECK(__fixxfdi(0x1p63L) == LLONG_MAX);
	CHECK(__fixsfdi(-0x1p64f) == LLONG_MIN);
	CHECK(__fixdfdi(-0x1p64) == LLONG_MIN);
	CHECK(__fixxfdi(-0x1p64L) == LLONG_MIN);

	if (fails) printf("rtlib: %d failure(s)\n", fails);
	else printf("rtlib: all tests passed\n");
	return !!fails;
}
