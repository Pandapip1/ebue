/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
char *strcpy(char *, const char *);
void *__malloc(size_t);
size_t strlen(const char *);
size_t strnlen(const char *, size_t);
int strcmp(const char *, const char *);

#define STRING_CONTRACT __attribute__((annotate("ntlibc.string")))
#define SPAN_CONTRACT(size_parameter) \
	__attribute__((annotate("ntlibc.span:" #size_parameter)))

size_t contracted_length(const char *text STRING_CONTRACT);
void contracted_copy(char *out SPAN_CONTRACT(3),
	const char *in SPAN_CONTRACT(3), size_t length);

static void contracted_fill(char *out SPAN_CONTRACT(2), size_t length)
{
	memset(out, 0, length);
}

void violate_contracts(char *text)
{
	char source[4], destination[4];
	(void)contracted_length(text); /* memory-contract-expect */
	contracted_copy(destination, source, 8); /* memory-contract-expect */
}

/* Only this caller violates the contract.  Once diagnosed, the assumed
 * exact-region span must prevent a duplicate report in contracted_fill's
 * inlined body, even though destination + 2 is an interior region. */
void violate_inline_contract(void)
{
	char destination[4];
	contracted_fill(destination + 2, 3); /* memory-contract-expect */
}

/* The first call must report the unproved sentinel.  If it returns
 * normally, that call itself establishes the same pointer's string
 * postcondition, so repeating the identical obligation would be noise. */
void repeated_string_contract(char *text)
{
	(void)strlen(text); /* memory-contract-expect */
	(void)strlen(text);
}

/* strcmp traverses both strings before a normal return.  checkPreCall emits
 * one call-site obligation (rather than duplicate diagnostics for the same
 * expression), while checkPostCall must retain both established sentinels so
 * neither later strlen repeats that already-discharged precondition. */
int repeated_two_string_contract(char *left, char *right)
{
	int order = strcmp(left, right); /* memory-contract-expect */
	(void)strlen(left);
	(void)strlen(right);
	return order;
}

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

/* Sharing an affine root does not prove the larger expression is larger:
 * unsigned addition wraps, so n == SIZE_MAX allocates zero bytes here. */
void wrapped_allocator_extent(size_t n)
{
	if (n != (size_t)-1) return;
	char *d = __malloc(n + 1);
	if (!d) return;
	memset(d, 0, n); /* memory-contract-expect */
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
