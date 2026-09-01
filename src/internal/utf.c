/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * UTF-8 is the library's only character encoding: that is what every
 * char* a program hands in or gets back is.  ntdll wants UTF-16, and
 * ntdll also provides the two conversions, so they are used rather than
 * written again.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <stdlib.h>
#include <string.h>
#include "libc.h"

__attribute__((ownership_returns(internal_malloc)))
WCHAR *__utf8_to_utf16(const char *s, size_t *wlen)
{
	size_t length = strlen(s), allocation;
	ULONG inlen, outlen = 0;
	WCHAR *w;
	NTSTATUS st;

	/* UTF-16 is never longer in code units than UTF-8 is in bytes. */
	if (!__utf8_to_utf16_allocation(length, &allocation)) {
		errno = EOVERFLOW;
		return 0;
	}
	inlen = (ULONG)length;
	w = __malloc(allocation);
	if (!w) { errno = ENOMEM; return 0; }
	if (inlen) {
		st = RtlUTF8ToUnicodeN(w, (ULONG)(allocation - sizeof(WCHAR)),
		                       &outlen, s, inlen);
		if (!NT_SUCCESS(st)) { __free(w); errno = EILSEQ; return 0; }
	}
	w[outlen / sizeof(WCHAR)] = 0;
	if (wlen) *wlen = outlen / sizeof(WCHAR);
	return w;
}

int __utf16_to_utf8_buf(const WCHAR *w, size_t n, char *out, size_t outsz)
{
	ULONG outlen = 0;
	size_t input_bytes;
	NTSTATUS st;

	if (!outsz) { errno = ERANGE; return -1; }
	if (!__utf16_input_bytes(n, &input_bytes) || outsz - 1 > UINT32_MAX) {
		errno = EOVERFLOW;
		return -1;
	}
	if (n) {
		st = RtlUnicodeToUTF8N(out, (ULONG)(outsz - 1), &outlen, w,
		                       (ULONG)input_bytes);
		if (st == STATUS_BUFFER_TOO_SMALL) { errno = ERANGE; return -1; }
		if (!NT_SUCCESS(st)) { errno = EILSEQ; return -1; }
	}
	out[outlen] = 0;
	return (int)outlen;
}

__attribute__((ownership_returns(internal_malloc)))
char *__utf16_to_utf8(const WCHAR *w, size_t n)
{
	/* UTF-8 is at most 3 bytes per UTF-16 code unit (4 per surrogate pair,
	 * which is two units). */
	size_t cap;
	if (!__utf16_to_utf8_capacity(n, &cap)) { errno = EOVERFLOW; return 0; }
	char *s = __malloc(cap);
	if (!s) { errno = ENOMEM; return 0; }
	if (__utf16_to_utf8_buf(w, n, s, cap) < 0) { __free(s); return 0; }
	return s;
}

// NOLINTEND(misc-include-cleaner)
