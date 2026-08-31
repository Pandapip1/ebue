/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
long read(int, void *, size_t);
void *__malloc(size_t);
size_t strlen(const char *);
size_t strnlen(const char *, size_t);
char *strdup(const char *);

#define STRING_CONTRACT __attribute__((annotate("ntlibc.string")))
#define SPAN_CONTRACT(size_parameter) \
	__attribute__((annotate("ntlibc.span:" #size_parameter)))

/* Contracted callees are analyzed with their checked preconditions. */
size_t contracted_length(const char *text STRING_CONTRACT)
{
	return strlen(text);
}

void contracted_copy(char *out SPAN_CONTRACT(3),
	const char *in SPAN_CONTRACT(3), size_t length)
{
	memcpy(out, in, length);
}

void satisfy_contracts(void)
{
	char source[8], destination[8];
	(void)contracted_length("known string");
	contracted_copy(destination, source, sizeof source);
}

/* A user function that merely shares a recognized libc name must not make
 * BeginFunction index nonexistent builtin-contract parameters. */
static int send(void)
{
	return 0;
}

int call_shadow_send(void)
{
	return send();
}

void bounded_operations(int fd)
{
	char source[16], destination[16];
	memcpy(destination, source, sizeof source);
	memmove(source + 1, source, 8);
	memset(destination, 0, sizeof destination);
	read(fd, destination, sizeof destination);
}

/* strndup.c's own shape: this tree's own allocator (__malloc, not the
 * literally-named "malloc" clang's builtin modeling already knows how to
 * give a real dynamic extent) is sized as `l + 1`, and the destination
 * span's length is the same `l` symbol on its own -- the "allocate
 * len+1, write the terminator at len" idiom repeated throughout
 * src/string and src/sh. The SOURCE span (reading `l` bytes from `s`,
 * where `l` came from strnlen(s, n)) is proven by a separate lemma:
 * strnlen's own contract guarantees `s` has at least `l` readable bytes
 * (whether or not it found a real terminator within the first n). */
char *dup_prefix(const char *s, size_t n)
{
	size_t l = strnlen(s, n);
	if (l == (size_t)-1) return 0;
	char *d = __malloc(l + 1);
	if (!d) return 0;
	memcpy(d, s, l);
	d[l] = 0;
	return d;
}

/* xstrdup's own shape, duplicated across src/glob/glob.c,
 * src/sh/execute.c, src/sh/parse.c, and src/wordexp/wordexp.c (and
 * src/string/strdup.c itself): the allocation and the memcpy length are
 * the SAME compound expression (`n`, itself `strlen(s) + 1`) on both
 * sides, and the source span is proven because strlen(s)'s own
 * byte-count contract guarantees `s` has at least strlen(s) bytes plus
 * its terminator -- exactly `n` bytes. `s` is a string literal, not a
 * parameter, so this stays focused on the MemoryContract span lemma:
 * a bare `const char *` parameter's own strlen() argument is a separate,
 * unrelated ntlibc.StringSentinel obligation this fixture is not about. */
char *dup_all(void)
{
	const char *s = "example";
	size_t n = strlen(s) + 1;
	char *p = __malloc(n);
	if (!p) return 0;
	memcpy(p, s, n);
	return p;
}

/* A string-producing API's result retains the API's sentinel contract.
 * The second strlen must not forget what strdup established. */
size_t duplicated_length(void)
{
	char *s = strdup("example");
	return s ? strlen(s) : 0;
}
