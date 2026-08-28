/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
typedef unsigned long ULONG;
typedef unsigned short USHORT;
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);

void *multiplied_allocation(size_t count)
{
	return malloc(count * sizeof(int)); /* sizearith-expect: allocation-arithmetic */
}

void *added_allocation(size_t length)
{
	return malloc(length + 1); /* sizearith-expect: allocation-arithmetic */
}

void *added_counted_allocation(size_t count)
{
	return calloc(count + 1, sizeof(int)); /* sizearith-expect: allocation-arithmetic */
}

void *multiplied_reallocation(void *p, size_t cap)
{
	return realloc( /* sizearith-expect: allocation-arithmetic */
		p,
		cap * sizeof(int));
}

size_t raw_growth(size_t cap)
{
	return cap ? cap * 2 : 8; /* sizearith-expect: unchecked-growth */
}

size_t raw_linear_growth(size_t cap, size_t amount)
{
	cap += amount; /* sizearith-expect: unchecked-growth */
	return cap;
}

size_t division_guard_does_not_prove_addition(size_t cap)
{
	if (cap > (size_t)-1 / 2) { return 0; }
	cap += 2; /* sizearith-expect: unchecked-growth */
	return cap;
}

size_t late_guarded_growth(size_t cap)
{
	size_t result = cap * 2; /* sizearith-expect: unchecked-growth */
	if (cap > (size_t)-1 / 2) { return 0; }
	return result;
}

ULONG raw_ulong_narrowing(size_t length)
{
	return (ULONG)length;
}

int raw_int_narrowing(size_t byte_count)
{
	return (int)byte_count;
}

USHORT raw_ushort_narrowing(size_t length)
{
	return (USHORT)length;
}

USHORT late_guarded_ushort_narrowing(size_t length)
{
	USHORT result = (USHORT)length;
	if (length > 0xffffu) return 0;
	return result;
}

/* sizearith-safe: sizearith-expect: forbidden-escape */
size_t obsolete_escape_marker(size_t value)
{
	return value;
}
