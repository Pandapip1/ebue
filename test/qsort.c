/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int cmp_int(const void *a, const void *b)
{
	int x = *(const int *)a, y = *(const int *)b;
	return (x > y) - (x < y);
}

static int cmp_str(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int cmp_int_r(const void *a, const void *b, void *dir)
{
	return *(int *)dir * cmp_int(a, b);
}

struct odd { char bytes[7]; };
static int cmp_odd(const void *a, const void *b)
{
	return memcmp(a, b, 7);
}

int main(void)
{
	int i;

	/* random-ish ints, all orders */
	{
		enum { N = 1000 };
		static int a[N];
		unsigned s = 12345;
		for (i = 0; i < N; i++) { s = s * 1103515245 + 12345; a[i] = (int)(s >> 16) % 1000 - 500; }
		qsort(a, N, sizeof a[0], cmp_int);
		for (i = 1; i < N; i++) CHECK(a[i-1] <= a[i]);
	}
	/* already sorted, reversed, all equal, tiny */
	{
		int a[100];
		for (i = 0; i < 100; i++) a[i] = i;
		qsort(a, 100, sizeof a[0], cmp_int);
		for (i = 0; i < 100; i++) CHECK(a[i] == i);
		for (i = 0; i < 100; i++) a[i] = 99 - i;
		qsort(a, 100, sizeof a[0], cmp_int);
		for (i = 0; i < 100; i++) CHECK(a[i] == i);
		for (i = 0; i < 100; i++) a[i] = 7;
		qsort(a, 100, sizeof a[0], cmp_int);
		for (i = 0; i < 100; i++) CHECK(a[i] == 7);
		a[0] = 3; a[1] = 1;
		qsort(a, 2, sizeof a[0], cmp_int);
		CHECK(a[0] == 1 && a[1] == 3);
		qsort(a, 1, sizeof a[0], cmp_int);
		qsort(a, 0, sizeof a[0], cmp_int);
	}
	/* odd element size (7 bytes) */
	{
		struct odd a[50];
		unsigned s = 999;
		int j;
		for (i = 0; i < 50; i++)
			for (j = 0; j < 7; j++) { s = s * 1103515245 + 12345; a[i].bytes[j] = (char)(s >> 24); }
		qsort(a, 50, sizeof a[0], cmp_odd);
		for (i = 1; i < 50; i++) CHECK(memcmp(&a[i-1], &a[i], 7) <= 0);
	}
	/* strings */
	{
		const char *a[] = { "pear", "apple", "orange", "banana", "kiwi" };
		qsort(a, 5, sizeof a[0], cmp_str);
		CHECK(!strcmp(a[0], "apple") && !strcmp(a[4], "pear"));
	}
	/* qsort_r */
	{
		int a[10] = { 3, 1, 4, 1, 5, 9, 2, 6, 5, 3 };
		int down = -1;
		qsort_r(a, 10, sizeof a[0], cmp_int_r, &down);
		for (i = 1; i < 10; i++) CHECK(a[i-1] >= a[i]);
	}
	/* bsearch */
	{
		int a[8] = { 2, 4, 6, 8, 10, 12, 14, 16 };
		int key = 10, *p;
		p = bsearch(&key, a, 8, sizeof a[0], cmp_int);
		CHECK(p && *p == 10 && p == a + 4);
		key = 7;
		CHECK(bsearch(&key, a, 8, sizeof a[0], cmp_int) == 0);
		key = 2;
		CHECK(bsearch(&key, a, 8, sizeof a[0], cmp_int) == a);
		key = 16;
		CHECK(bsearch(&key, a, 8, sizeof a[0], cmp_int) == a + 7);
		CHECK(bsearch(&key, a, 0, sizeof a[0], cmp_int) == 0);
	}

	if (!fails) printf("qsort: all tests passed\n");
	return fails != 0;
}
