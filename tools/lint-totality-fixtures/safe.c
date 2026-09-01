/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>

unsigned strict_bound(unsigned n)
{
	unsigned i;
	for (i = 0; i < n; i++) {
	}
	return i;
}

unsigned affine_strict_bound(unsigned n)
{
	unsigned i;
	for (i = 0; i + 2 < n; i++) {
	}
	return i;
}

unsigned conjunctive_bound(unsigned n, int stop)
{
	unsigned i;
	for (i = 0; i < n && !stop; ++i) {
	}
	return i;
}

unsigned inclusive_constant_bound(void)
{
	unsigned i;
	for (i = 0; i <= 60; i++) {
	}
	return i;
}

int inclusive_countdown(void)
{
	int i;
	for (i = 8; i >= 0; i--) {
	}
	return i;
}

int signed_nonunit_step(int n)
{
	int i;
	for (i = 0; i <= n; i += 3) {
	}
	return i;
}

static unsigned stable_file_bound = 8;

unsigned stable_file_bound_loop(void)
{
	unsigned i;
	for (i = 0; i < stable_file_bound; i++) {
	}
	return i;
}

unsigned sentinel_pointer(const unsigned char *p)
{
	const unsigned char *start = p;
	while (*p != 0)
		p++;
	return (unsigned)(p - start);
}

unsigned sentinel_pointer_variable_step(const unsigned char *p)
{
	const unsigned char *start = p;
	while (*p != 0)
		p += (unsigned)*p + 1;
	return (unsigned)(p - start);
}

size_t sentinel_index(const unsigned char *p)
{
	size_t i = 0;
	while (p[i])
		i = i + 1;
	return i;
}

unsigned narrow_index_with_explicit_bound(const unsigned char *p,
	unsigned char n)
{
	unsigned char i = 0;
	while (i < n && p[i])
		i++;
	return i;
}

unsigned countdown(unsigned n)
{
	while (n)
		--n;
	return n;
}

unsigned radix_countdown(unsigned n)
{
	while (n)
		n /= 10;
	return n;
}

unsigned assigned_radix_countdown(unsigned n)
{
	while (n)
		n = n / 10;
	return n;
}

unsigned shifted_countdown(unsigned n)
{
	while (n)
		n >>= 3;
	return n;
}

unsigned explicit_cast_bound(unsigned n)
{
	unsigned long i;
	for (i = 0; i < (unsigned long)n; i++) {
	}
	return (unsigned)i;
}

int signed_countdown(int n)
{
	while (n)
		n--;
	return n;
}

int signed_radix_countdown(int n)
{
	while (n != 0)
		n /= 10;
	return n;
}

unsigned chunked_countdown(unsigned n)
{
	while (n >= 3)
		n -= 3;
	return n;
}

unsigned sizeof_chunked_countdown(unsigned n)
{
	for (; n >= sizeof(unsigned); n -= sizeof(unsigned)) {
	}
	return n;
}

struct counter {
	unsigned n;
};

unsigned member_countdown(struct counter *counter)
{
	while (counter->n)
		counter->n--;
	return counter->n;
}

int signed_extra_progress(int n, int skip)
{
	int i;
	for (i = 0; i < n; i++) {
		if (skip)
			i++;
	}
	return i;
}

unsigned two_variable_increment(unsigned n)
{
	unsigned i, remaining;
	for (i = 0, remaining = n; i < n; i++, remaining--) {
	}
	return remaining;
}

unsigned condition_countdown(unsigned n)
{
	unsigned sum = 0;
	while (n-- > 0)
		sum++;
	return sum;
}

unsigned progress_on_every_backedge(unsigned n, int choose)
{
	unsigned i = 0;
	while (i < n) {
		if (choose) {
			i++;
			continue;
		}
		i++;
	}
	return i;
}

unsigned progress_or_exit(unsigned n, int stop)
{
	unsigned i = 0;
	while (i < n) {
		if (stop)
			break;
		i++;
	}
	return i;
}

unsigned progress_in_both_expression_arms(unsigned n, int choose)
{
	unsigned i = 0;
	while (i < n)
		choose ? i++ : ++i;
	return i;
}

unsigned guarded_recursion(unsigned n)
{
	if (n)
		return guarded_recursion(n - 1);
	return 0;
}

unsigned guarded_else_recursion(unsigned n)
{
	if (!n)
		return 0;
	else
		return guarded_else_recursion(n - 1);
}

struct vec {
	unsigned n;
	unsigned *v;
};

unsigned member_bound_arrow(struct vec *p)
{
	unsigned i;
	for (i = 0; i < p->n; i++) {
	}
	return i;
}

unsigned member_bound_dot(struct vec v)
{
	unsigned i;
	for (i = 0; i < v.n; i++) {
	}
	return i;
}

unsigned member_bound_while(struct vec *p)
{
	unsigned i = 0;
	while (i < p->n) {
		if (p->v[i] == 0)
			return i;
		i++;
	}
	return i;
}
