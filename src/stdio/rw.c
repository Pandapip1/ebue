/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Byte- and block-oriented reads and writes: fgetc/fputc and their many
 * aliases, fread/fwrite, fgets/fputs/puts, ungetc, getdelim/getline.
 * There is no locking (see flockfile in file.c), so the plain and
 * _unlocked names are the same function.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "stdio_impl.h"

int __fgetc(FILE *f)
{
	if (f->nunget) return f->unget[--f->nunget];
	if (!f->readable) { errno = EBADF; f->err = 1; return EOF; }
	if (__toread(f) < 0) return EOF;
	if (f->bufmode == _IONBF) {
		unsigned char c;
		ssize_t n = __file_read(f, &c, 1);
		if (n < 0) { f->err = 1; return EOF; }
		if (n == 0) { f->eof = 1; return EOF; }
		return c;
	}
	if (f->rpos >= f->rend && __fill(f) <= 0) return EOF;
	return f->buf[f->rpos++];
}

int __fputc(int c, FILE *f)
{
	unsigned char ch = (unsigned char)c;
	if (!f->writable) { errno = EBADF; f->err = 1; return EOF; }
	if (__towrite(f) < 0) return EOF;
	if (f->bufmode == _IONBF) {
		ssize_t n = __file_write(f, &ch, 1);
		if (n < 1) { f->err = 1; return EOF; }
		return ch;
	}
	__ensure_buf(f);
	if (!f->buf) { f->err = 1; return EOF; }
	f->buf[f->wpos++] = ch;
	if (f->wpos >= f->bufsz || (f->bufmode == _IOLBF && ch == '\n')) {
		if (__fflush_locked(f) < 0) return EOF;
	}
	return ch;
}

int fgetc(FILE *f) { return __fgetc(f); }
int getc(FILE *f) { return __fgetc(f); }
int getchar(void) { return __fgetc(stdin); }
int getc_unlocked(FILE *f) { return __fgetc(f); }
int getchar_unlocked(void) { return __fgetc(stdin); }

int fputc(int c, FILE *f) { return __fputc(c, f); }
int putc(int c, FILE *f) { return __fputc(c, f); }
int putchar(int c) { return __fputc(c, stdout); }
int putc_unlocked(int c, FILE *f) { return __fputc(c, f); }
int putchar_unlocked(int c) { return __fputc(c, stdout); }

int ungetc(int c, FILE *f)
{
	if (c == EOF || !f->readable) return EOF;
	if (f->nunget >= (int)(sizeof f->unget / sizeof f->unget[0])) return EOF;
	f->unget[f->nunget++] = (unsigned char)c;
	f->eof = 0;
	return (unsigned char)c;
}

size_t __fread(void *ptr, size_t size, size_t nmemb, FILE *f)
{
	size_t total = size * nmemb, got = 0;
	unsigned char *p = ptr;

	if (!size || !nmemb) return 0;
	if (!f->readable) { errno = EBADF; f->err = 1; return 0; }
	while (f->nunget && got < total) p[got++] = (unsigned char)f->unget[--f->nunget];
	if (got < total && __toread(f) < 0) return got / size;
	while (got < total) {
		size_t avail = f->rend - f->rpos;
		if (avail) {
			size_t n = total - got;
			if (n > avail) n = avail;
			memcpy(p + got, f->buf + f->rpos, n);
			f->rpos += n;
			got += n;
			continue;
		}
		if (total - got >= (f->bufsz ? f->bufsz : BUFSIZ)) {
			ssize_t n = __file_read(f, p + got, total - got);
			if (n < 0) { f->err = 1; break; }
			if (n == 0) { f->eof = 1; break; }
			got += (size_t)n;
			continue;
		}
		if (__fill(f) <= 0) break;
	}
	return got / size;
}

size_t __fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f)
{
	size_t total = size * nmemb, put = 0;
	const unsigned char *p = ptr;

	if (!size || !nmemb) return 0;
	if (!f->writable) { errno = EBADF; f->err = 1; return 0; }
	if (__towrite(f) < 0) return 0;
	while (put < total) {
		size_t room, n;
		__ensure_buf(f);
		if (!f->buf) { f->err = 1; break; }
		if (f->wpos == 0 && total - put >= f->bufsz) {
			ssize_t r = __file_write(f, p + put, total - put);
			if (r <= 0) { f->err = 1; break; }
			put += (size_t)r;
			continue;
		}
		room = f->bufsz - f->wpos;
		n = total - put;
		if (n > room) n = room;
		{
			int nl = 0;
			if (f->bufmode == _IOLBF) {
				size_t k;
				for (k = 0; k < n; k++) if (p[put + k] == '\n') { nl = 1; break; }
			}
			memcpy(f->buf + f->wpos, p + put, n);
			f->wpos += n;
			put += n;
			if (f->wpos >= f->bufsz || nl) {
				if (__fflush_locked(f) < 0) break;
			}
		}
	}
	return put / size;
}

size_t fread(void *__restrict ptr, size_t size, size_t nmemb, FILE *__restrict f) { return __fread(ptr, size, nmemb, f); }
size_t fwrite(const void *__restrict ptr, size_t size, size_t nmemb, FILE *__restrict f) { return __fwrite(ptr, size, nmemb, f); }

char *fgets(char *__restrict s, int n, FILE *__restrict f)
{
	int i = 0, c;
	if (n <= 0) return 0;
	if (!f->readable) { errno = EBADF; f->err = 1; return 0; }
	for (; i < n - 1; ) {
		c = __fgetc(f);
		if (c == EOF) { if (i == 0) return 0; break; }
		s[i++] = (char)c;
		if (c == '\n') break;
	}
	s[i] = 0;
	return s;
}

#if __STDC_VERSION__ < 201112L
char *gets(char *s)
{
	int i = 0, c;
	for (;;) {
		c = __fgetc(stdin);
		if (c == EOF) { if (i == 0) return 0; break; }
		if (c == '\n') break;
		s[i++] = (char)c;
	}
	s[i] = 0;
	return s;
}
#endif

int fputs(const char *__restrict s, FILE *__restrict f)
{
	size_t n = strlen(s);
	return __fwrite(s, 1, n, f) == n ? 0 : EOF;
}

int puts(const char *s)
{
	if (fputs(s, stdout) == EOF) return EOF;
	if (__fputc('\n', stdout) == EOF) return EOF;
	return 0;
}

ssize_t getdelim(char **__restrict buf, size_t *__restrict n, int delim, FILE *__restrict f)
{
	size_t cap, len = 0;
	char *b;
	int c;

	/* getdelim.html ERRORS: "[EINVAL] lineptr or n is a null pointer."
	 * The rest of this function stores through both unconditionally, so
	 * the check has to come first rather than being half-applied. */
	if (!buf || !n) { errno = EINVAL; return -1; }
	cap = *buf ? *n : 0;
	b = *buf;

	if (!f->readable) { errno = EBADF; f->err = 1; return -1; }
	if (!b || cap == 0) {
		cap = 128;
		b = malloc(cap);
		if (!b) { errno = ENOMEM; return -1; }
	}
	for (;;) {
		c = __fgetc(f);
		if (c == EOF) {
			if (len == 0) { *buf = b; *n = cap; return -1; }
			break;
		}
		if (len + 1 >= cap) {
			size_t nc = cap * 2;
			char *nb = realloc(b, nc);
			if (!nb) { *buf = b; *n = cap; errno = ENOMEM; return -1; }
			b = nb; cap = nc;
		}
		b[len++] = (char)c;
		if (c == delim) break;
	}
	b[len] = 0;
	*buf = b;
	*n = cap;
	return (ssize_t)len;
}

ssize_t getline(char **__restrict buf, size_t *__restrict n, FILE *__restrict f)
{
	return getdelim(buf, n, '\n', f);
}
