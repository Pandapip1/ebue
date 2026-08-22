<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# `wchar.h` / multibyte conversion coverage ledger

Clause-by-clause POSIX.1-2017 audit against
`https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html`.
New test file: `test/posix-wchar.c`. Sanity coverage (uncited) still lives
in `test/string.c` and was not removed.

## The one fact that shapes this whole audit

`wchar_t` here is a **16-bit unsigned UTF-16 code unit** (`WCHAR_MAX ==
0xffff`, `include/wchar.h`), not the 32-bit type that holds one whole
Unicode scalar value most POSIX text assumes. A code point above U+FFFF
is two `wchar_t` (a surrogate pair). Every divergence this causes is
listed below; none are fenced as bugs, because the alternative (matching
the letter of a clause written for a 32-bit `wchar_t`) is not achievable
without changing the ABI. `src/stdlib/mbrtowc.c`'s header comment
already documents the design; this file records where it costs
conformance.

## Table

| function | clause checked | status | test |
|---|---|---|---|
| wcslen | count excl. terminator | covered | test/posix-wchar.c |
| wcsnlen | not implemented (no `wcsnlen` in include/wchar.h at all, though POSIX.1-2017 base wchar.h requires it) | N/A (missing from this libc) | — |
| wcscpy | copies incl. NUL, returns ws1 | covered | test/posix-wchar.c |
| wcpcpy | not implemented | N/A (missing) | — |
| wcsncpy | pads with NUL to n if shorter; no NUL appended if source >= n; returns ws1 | covered | test/posix-wchar.c |
| wcpncpy | not implemented (wcsncpy's return-value alternative) | N/A (missing) | — |
| wcscat | appends incl. NUL, returns ws1 | covered | test/posix-wchar.c |
| wcsncat | appends <= n, always NUL-terminates, returns ws1 | covered | test/posix-wchar.c |
| wcscmp | sign from first differing code | covered | test/posix-wchar.c |
| wcsncmp | same, bounded to n, stops at NUL | covered | test/posix-wchar.c |
| wcschr | first occurrence; NUL is part of the string | covered | test/posix-wchar.c |
| wcsrchr | last occurrence; same NUL rule | covered | test/posix-wchar.c |
| wcscoll / wcsxfrm / wcscasecmp / wcsncasecmp / wcspbrk / wcscspn / wcsspn / wcsstr / wcstok / wcsdup / wcsftime / wcswidth / wcwidth | not implemented | N/A (missing from this libc's wchar.h) | — |
| wcstol/wcstoll/wcstoul/wcstoull/wcstod/wcstof/wcstold | not implemented | N/A (missing) | — |
| wmemchr | locates wc in first n; NUL not special; n==0 -> not found | covered | test/posix-wchar.c |
| wmemcmp | compares n as unsigned-value wchar_t; n==0 -> 0 | covered | test/posix-wchar.c |
| wmemcpy | copies n, n==0 valid pointers/no copy, returns ws1 | covered | test/posix-wchar.c |
| wmemmove | overlap-safe (as-if temp array), returns ws1 | covered | test/posix-wchar.c |
| wmemset | fills n with wc, returns ws | covered | test/posix-wchar.c |
| mbsinit | true if ps NULL or initial state | covered | test/posix-wchar.c |
| mbrtowc | 0/1..n/-1(EILSEQ)/-2(incomplete); overlong+surrogate+>U+10FFFF rejected; s==NULL behaves as mbrtowc(NULL,"",1,ps); errno unchanged on success | covered, plus a documented divergence | test/posix-wchar.c |
| mbrtowc surrogate pairs | **DIVERGENCE**: a 4-byte UTF-8 sequence (code point > U+FFFF) cannot be returned as one `wchar_t`. ntlibc returns the high surrogate from the call that consumes the bytes, then on the *next* call returns `(size_t)-3` — a 5th return value POSIX's mbrtowc contract (0, 1..n, -1, -2) does not define — consuming 0 bytes and delivering the low surrogate. A strictly conforming caller cannot anticipate -3. Documented in `src/stdlib/mbrtowc.c`'s header comment as deliberate; recorded here as a real non-conformance, not silently accepted. | test/posix-wchar.c `test_mbrtowc_surrogate_pair_divergence` |
| wcrtomb | byte count incl. shift seq.; NUL -> 1 byte stored; s==NULL equivalent to wcrtomb(buf,L'\0',ps); errno unchanged on success; EILSEQ for invalid wc | covered, plus a documented divergence | test/posix-wchar.c |
| wcrtomb surrogate pairs | **DIVERGENCE**: mirrors mbrtowc. A lone high surrogate is, per the spec's RETURN VALUE clause, simply "not a valid wide-character code" -> EILSEQ. ntlibc instead treats it as valid but incomplete: stashes it in `*ps` and returns 0, a return value the spec never assigns any meaning to for wcrtomb (unlike mbrtowc's -2, there is no "incomplete input" concept in wcrtomb's contract — the input is one already-complete `wchar_t`). Only a subsequent call with the matching low surrogate emits real bytes. | test/posix-wchar.c `test_wcrtomb_surrogate_pair_divergence` |
| mbrlen | equivalent to mbrtowc(NULL,...) | covered (sanity) | test/posix-wchar.c |
| mbtowc | s==NULL -> 0 (no state-dependent encoding); 0/byte-count/-1(EILSEQ); <= n and <= MB_CUR_MAX | covered | test/posix-wchar.c |
| wctomb | s==NULL -> 0; byte-count/-1(EILSEQ); <= MB_CUR_MAX | covered, plus divergence note | test/posix-wchar.c |
| wctomb lone high surrogate | Not part of the POSIX contract either way (wctomb has no cross-call state at all, unlike wcrtomb's mbstate_t), so failing outright with EILSEQ is the only sound choice ntlibc could make here; not counted as a divergence since there is no conforming alternative to diverge from. | test/posix-wchar.c `test_wctomb` |
| mblen | equivalent to mbtowc(0,s,n) | covered | test/posix-wchar.c |
| mbstowcs | converts <= n; returns count excl. NUL or (size_t)-1/EILSEQ; pwcs==NULL -> length only | covered | test/posix-wchar.c |
| wcstombs | converts, stopping at n bytes or NUL, never splitting a character; returns byte count excl. NUL or (size_t)-1/EILSEQ; s==NULL -> length only | covered | test/posix-wchar.c |
| mbsrtowcs | converts via mbrtowc(); *src set to NULL (terminator) or just-past-converted; dst==NULL -> length only; (size_t)-1/EILSEQ; errno unchanged on success | covered | test/posix-wchar.c |
| wcsrtombs | converts via wcrtomb(); never splits a character across the len boundary (verified directly against a surrogate pair that needs 4 bytes); *src set to NULL or just-past-converted; dst==NULL -> length only; (size_t)-1/EILSEQ | covered | test/posix-wchar.c |
| mbsnrtowcs / wcsnrtombs | not implemented | N/A (missing) | — |
| btowc | WEOF for EOF or invalid one-byte char in initial shift state; otherwise the wide-char value | covered, plus a documented divergence | test/posix-wchar.c |
| btowc POSIX-locale byte range | **DIVERGENCE**: "In the POSIX locale, btowc() shall not return WEOF if c has a value in the range 0 to 255 inclusive." ntlibc's only locale is named "POSIX"/"C", but its multibyte encoding is UTF-8, not the traditional single-byte-identity encoding the clause's "POSIX locale" assumes. Under UTF-8, bytes 0x80-0xFF are never a complete one-byte character by themselves (lead/continuation bytes of a multibyte sequence), so `btowc(0x80)` correctly returns WEOF here — consistent with every EILSEQ case mbrtowc correctly rejects for a bare high byte, but not with this clause's literal text. Satisfying the clause as written would mean claiming byte 0x80 alone is U+0080, which would be wrong. Recorded, not fenced. | test/posix-wchar.c `test_btowc` |
| wctob | EOF unless c has a length-1 representation in the initial shift state; else that byte as unsigned char->int | covered | test/posix-wchar.c |
| iswalnum / iswalpha / iswcntrl / iswctype / iswdigit / iswgraph / iswlower / iswprint / iswpunct / iswspace / iswupper / iswxdigit / towlower / towupper / wctype / wctrans / towctrans | **`<wctype.h>` does not exist in this library at all** (`find include -iname wctype*` finds nothing) | N/A (whole header missing) | — |
| wcstoimax / wcstoumax | equivalent to wcstol/wcstoll/wcstoul/wcstoull family; 0 on no conversion, endptr reset to nptr; {INTMAX_MAX,INTMAX_MIN,UINTMAX_MAX}+ERANGE on overflow; EINVAL on unsupported base; base-0 auto-detection (0x->hex, leading 0->octal) | covered | test/posix-wchar.c |

## Bugs found this session

None. `src/internal/utf.c` (the UTF-8<->UTF-16 core used by argv/environ/
filename paths, not directly by wchar.h) was read but not separately unit
tested here: it delegates entirely to `RtlUTF8ToUnicodeN` /
`RtlUnicodeToUTF8N` (ntdll), doing no validation of its own beyond
malloc-failure and buffer-size bookkeeping, so there was no independent
decision logic to extract and test the way `__errno_from_status` models —
its correctness rides entirely on ntdll's conversion routines, which
`test/posix-wchar.c`'s mbrtowc/wcrtomb tests already exercise indirectly
end-to-end (mbrtowc/wcrtomb are ntlibc's own hand-rolled UTF-8 codec in
`src/stdlib/mbrtowc.c`, separate from utf.c's ntdll-backed one — both
now have coverage, from two different code paths).

## Extractions made

None needed. Both conversion cores (`src/stdlib/mbrtowc.c`'s hand-rolled
UTF-8 state machine, `src/internal/utf.c`'s ntdll wrapper) were already
reachable and testable through their public API without extracting
private helpers.

## Native asan-build gotcha for anyone extending this file

Never pass an `L"..."` wide-string literal as a `wchar_t*` argument in
this test file. `L"..."` is a *compiler* intrinsic whose element type is
the compiler's own `wchar_t` (4 bytes on the Linux host `make asan`
targets), completely independent of `include/wchar.h`'s `typedef ...
unsigned short wchar_t`. The target (tcc) build gets away with it by
coincidence — its own `wchar_t` matches the header — but the native asan
build silently reads garbage past the first character. Use the `W()`
helper in `test/posix-wchar.c` (copies a narrow ASCII literal into a
correctly-typed buffer) or a `{ 'a', 'b', 0 }`-style initializer for any
non-ASCII content; never `wchar_t x[] = L"...";` either (same problem,
plus tcc/clang both reject it outright as "array initializer must be an
initializer list" once the header's `wchar_t` and the compiler's differ).

## What was not reached

- `wcsnlen`, `wcpcpy`/`wcpncpy`, `wcscoll`/`wcsxfrm` and their `_l`
  variants, `wcscasecmp`/`wcsncasecmp` and their `_l` variants,
  `wcspbrk`/`wcscspn`/`wcsspn`/`wcsstr`/`wcstok`, `wcsdup`, `wcsftime`,
  `wcswidth`/`wcwidth`, `wcstol`/`wcstoll`/`wcstoul`/`wcstoull`/
  `wcstod`/`wcstof`/`wcstold`, `mbsnrtowcs`/`wcsnrtombs`, and all of
  `<wctype.h>` (`isw*`, `tow*`, `wctype`, `wctrans`) are simply not
  implemented by this library, so no clause audit applies — confirmed by
  grepping `include/` and `src/string/`+`src/stdlib/` rather than assumed.
  If any of these get implemented later, this ledger's "not yet reached"
  successor should start here.
- `src/internal/utf.c` was read in full but not clause-audited on its own
  terms (it has no POSIX function page of its own — it backs
  `execve`/`getcwd`/etc argument marshalling, not `<wchar.h>`); flagged
  for whichever agent owns those call sites, not fenced as a gap here.
