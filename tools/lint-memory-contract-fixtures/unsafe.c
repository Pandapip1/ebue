/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
char *strcpy(char *, const char *);
void *__malloc(size_t);
size_t strlen(const char *);
size_t strnlen(const char *, size_t);

void oversized(void)
{
	char source[4], destination[4];
	memcpy(destination, source, 8); /* memory-contract-expect */
}

void opaque(void *buffer, size_t length)
{
	memset(buffer, 0, length); /* memory-contract-expect */
}

void overlapping(void)
{
	char buffer[16];
	memcpy(buffer + 1, buffer, 8); /* memory-contract-expect */
}

/* A genuinely too-small __malloc'd allocation must still be caught once
 * this tree's own allocator family gets a real (as opposed to placeholder)
 * dynamic extent -- the same regression guard 8a56a66 pinned for the
 * sibling ValidPointerChecker's own analogous fix. */
void too_small_heap_allocation(const char *s)
{
	char *d = __malloc(4);
	memcpy(d, s, 8); /* memory-contract-expect */
}

/* strnlen(s, n)'s contract is looser than strlen(s)'s: if it walked all
 * n bytes without finding a terminator, only those n bytes (not n + 1)
 * are known-safe to read back from s -- reading one byte past that is
 * NOT proven, unlike the strlen()-derived case in safe.c's dup_all(). */
void too_much_from_strnlen(const char *s, size_t n)
{
	size_t l = strnlen(s, n);
	char *d = __malloc(l + 1);
	if (!d) return;
	memcpy(d, s, l + 1); /* memory-contract-expect */
	d[0] = 0;
}

/* Terminating an interior suffix does not prove that bytes before that
 * suffix contain any NUL at all.  Proven-string state must retain the exact
 * pointer at which the producing operation began, not its allocation base. */
void interior_string_does_not_prove_prefix(void)
{
	char buffer[8];
	strcpy(buffer + 4, "x");
	(void)strlen(buffer); /* memory-contract-expect */
}
