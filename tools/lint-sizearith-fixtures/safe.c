/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
void *malloc(size_t);
void *calloc(size_t, size_t);
void *reallocarray(void *, size_t, size_t);
int __size_mul_checked(size_t, size_t, size_t *);
int __array_next_capacity(size_t, size_t, size_t, size_t, size_t, size_t *);

void *checked_allocation(size_t count)
{
	size_t bytes;
	if (!__size_mul_checked(count, sizeof(int), &bytes)) return 0;
	return malloc(bytes);
}

void *counted_reallocation(void *p, size_t count)
{
	return reallocarray(p, count, sizeof(int));
}

void *counted_allocation(size_t count)
{
	return calloc(count, sizeof(int));
}

size_t checked_growth(size_t cap)
{
	size_t next;
	return __array_next_capacity(cap, cap, 1, 8, sizeof(int), &next) ? next : 0;
}

void *fixed_object(void)
{
	int *p;
	return malloc(sizeof *p);
}

size_t proved_growth(size_t cap)
{
	/* sizearith-safe: fixture proves the explicit documented escape. */
	return cap * 2;
}

const char *noncode_is_ignored(void)
{
	/* malloc(count + 1); cap *= 2; (ULONG)length */
	return "realloc(p, cap * sizeof(int))";
}
