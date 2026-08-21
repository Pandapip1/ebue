/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * UTF-8 is the library's only character encoding: that is what every
 * char* a program hands in or gets back is.  ntdll wants UTF-16, and
 * ntdll also provides the two conversions, so they are used rather than
 * written again.
 */
#include <stdlib.h>
#include <string.h>
#include "libc.h"

WCHAR *__utf8_to_utf16(const char *s, size_t *wlen)
{
	ULONG inlen = (ULONG)strlen(s), outlen = 0;
	WCHAR *w;
	NTSTATUS st;

	/* UTF-16 is never longer in code units than UTF-8 is in bytes. */
	w = __malloc((inlen + 1) * sizeof(WCHAR));
	if (!w) { errno = ENOMEM; return 0; }
	if (inlen) {
		st = RtlUTF8ToUnicodeN(w, inlen * sizeof(WCHAR), &outlen, s, inlen);
		if (!NT_SUCCESS(st)) { __free(w); errno = EILSEQ; return 0; }
	}
	w[outlen / sizeof(WCHAR)] = 0;
	if (wlen) *wlen = outlen / sizeof(WCHAR);
	return w;
}

int __utf16_to_utf8_buf(const WCHAR *w, size_t n, char *out, size_t outsz)
{
	ULONG outlen = 0;
	NTSTATUS st;

	if (!outsz) { errno = ERANGE; return -1; }
	if (n) {
		st = RtlUnicodeToUTF8N(out, (ULONG)(outsz - 1), &outlen, w, (ULONG)(n * sizeof(WCHAR)));
		if (st == STATUS_BUFFER_TOO_SMALL) { errno = ERANGE; return -1; }
		if (!NT_SUCCESS(st)) { errno = EILSEQ; return -1; }
	}
	out[outlen] = 0;
	return (int)outlen;
}

char *__utf16_to_utf8(const WCHAR *w, size_t n)
{
	/* UTF-8 is at most 3 bytes per UTF-16 code unit (4 per surrogate pair,
	 * which is two units). */
	size_t cap = n * 3 + 1;
	char *s = __malloc(cap);
	if (!s) { errno = ENOMEM; return 0; }
	if (__utf16_to_utf8_buf(w, n, s, cap) < 0) { __free(s); return 0; }
	return s;
}
