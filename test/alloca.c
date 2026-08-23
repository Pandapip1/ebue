/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * alloca(), which had no test at all until an unrelated full-source
 * bootstrap tripped over it.
 *
 * include/alloca.h used to `#define alloca __builtin_alloca`
 * unconditionally.  tcc has no such builtin, so every call became
 * `unresolved reference to '__builtin_alloca'` at link time -- and the
 * define defeated the very thing each arch's src/alloca.S exists for,
 * since that file is written so tcc can call alloca as an ordinary
 * cdecl function.  Nothing caught it because nothing in test/ called
 * alloca.
 *
 * So this is deliberately a *linkage* test as much as a behavioural
 * one: on tcc it only builds if alloca.S is reachable, and on a
 * compiler that does have the builtin it only builds if the header
 * still hands over to it.
 */
#include <alloca.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Not static: keep the allocation in a frame that really returns, so a
 * broken implementation that leaks the stack pointer shows up as the
 * next call reusing or corrupting it. */
static int fill(int n, unsigned char tag)
{
	unsigned char *p = alloca((size_t)n);
	int i;

	if (!p) return 0;
	for (i = 0; i < n; i++) p[i] = (unsigned char)(tag ^ (unsigned char)i);
	for (i = 0; i < n; i++)
		if (p[i] != (unsigned char)(tag ^ (unsigned char)i)) return 0;
	return 1;
}

int main(void)
{
	char *a, *b;
	int i;

	/* Basic: usable, writable, readable back. */
	a = alloca(64);
	CHECK(a != 0);
	if (a) { memset(a, 0x5a, 64); CHECK(a[0] == 0x5a && a[63] == 0x5a); }

	/* Two live allocations in one frame must not overlap. */
	b = alloca(64);
	CHECK(b != 0);
	if (a && b) {
		memset(a, 0x11, 64);
		memset(b, 0x22, 64);
		CHECK(a[0] == 0x11 && a[63] == 0x11);
		CHECK(b[0] == 0x22 && b[63] == 0x22);
	}

	/* Repeated use across frames that return: the stack must be
	 * reclaimed each time rather than growing without bound. */
	for (i = 0; i < 2000; i++)
		CHECK(fill(1 + (i % 300), (unsigned char)i));

	/* Sizes around the alignment boundary alloca.S rounds to. */
	for (i = 1; i <= 40; i++) CHECK(fill(i, 0xa5));

	if (fails) printf("%d failures\n", fails);
	else printf("alloca: all tests passed\n");
	return fails ? 1 : 0;
}
