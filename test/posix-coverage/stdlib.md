<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# stdlib.h coverage fragment

Priority group 2 of `test/POSIX-COVERAGE.md`: `stdlib.h` numeric
conversions, `qsort`/`bsearch`, and the rest of `stdlib.h`. This
fragment is written standalone for the coordinator to merge; it does
not edit `test/POSIX-COVERAGE.md` itself.

New clause-cited audit: `test/posix-stdlib.c` (96 `CHECK()` assertions).
Pre-existing coverage checked against the spec rather than duplicated:
`test/strto.c`, `test/qsort.c`, `test/stdlib.c`, `test/malloc.c`,
`test/posix-parse.c`. `test/misc.c` (owned by another agent) already
covers `atexit`/`exit`/`_Exit`/`abort` child-process behaviour;
not duplicated here.

| function | clause checked | status | test |
|---|---|---|---|
| strtol / strtoul / strtoll / strtoull / strtoimax / strtoumax | subject-sequence decomposition, base-0/base-16 prefix rules, negation, ERANGE clamping to \*_MIN/\*_MAX, no-conversion endptr==nptr | covered | test/strto.c, test/posix-parse.c |
| strtol / strtoul family | RETURN VALUE + ERRORS: unsupported base (not 0, not 2-36) returns 0 with errno=EINVAL (required, not "may fail") | covered | test/posix-stdlib.c (bases 1, 37, -1) |
| strtol / strtoul / strtod | "shall not change the setting of errno if successful" | covered | test/posix-stdlib.c (sentinel errno=12345 across a clean parse) |
| strtod / strtof / strtold | rounding (exactly-correct to the ulp, overflow/underflow boundaries, inf/nan spellings, hundreds-of-digits inputs) | covered | test/strto.c (exhaustive, incl. a glibc-cross-checked 86-case table) |
| atoi / atol / atoll / atof | "equivalent to (int) strtol(str, NULL, 10)" / strtod(str, NULL) — inherits leading-whitespace-skip, sign, and trailing-junk-ignored behaviour; "No errors are defined" | covered | test/strto.c (basic), test/posix-stdlib.c (whitespace/sign/junk/empty forms) |
| qsort / qsort_r | ascending order, arbitrary element size, comparator consistency (exercised via 1000-element random + reversed + all-equal + odd-size-struct arrays) | covered | test/qsort.c |
| qsort / qsort_r | DESCRIPTION: "If nel has the value zero, the comparison function ... shall not be called" | covered | test/posix-stdlib.c (counting comparator, nel=0, array left untouched) |
| bsearch | found/not-found/first/last/empty-array cases | covered | test/qsort.c |
| bsearch | DESCRIPTION: nel==0 → comparator not called, no match; comparator called with (key, member) in that fixed order | covered | test/posix-stdlib.c |
| malloc / calloc / realloc / free / posix_memalign / aligned_alloc / memalign / valloc / reallocarray / malloc_usable_size | out of this agent's `src/` area (lives in `src/malloc/`, not `src/stdlib/`) and out of scope for `test/posix-stdlib.c` (`test/malloc.c` belongs to another agent); already exercises every C99 7.20.3 / POSIX clause we could find (NULL+ENOMEM, zero-size, alignment power-of-2/multiple-of-pointer-size EINVAL cases, contents preserved across realloc, ASan-aware ENOMEM path) | covered elsewhere | test/malloc.c |
| abs / labs / llabs / imaxabs | \|x\| for representable x; div/ldiv/lldiv/imaxdiv quot/rem pairs incl. truncation-toward-zero on negative operands | covered | test/stdlib.c |
| abs family | abs(INT_MIN) etc.: C99 7.20.6.1p2 leaves this undefined behaviour (result not representable) — deliberately not tested | N/A (undefined behaviour, C99 7.20.6.1p2) | — |
| rand / srand / rand_r | reproducibility for a fixed seed; range implied by RAND_MAX | covered | test/stdlib.c |
| RAND_MAX >= 32767 (C99 7.20.2.1) | `#define RAND_MAX (0x7fffffff)`, a compile-time constant far above the floor — verified by inspection of include/stdlib.h, not worth a runtime assertion | N/A (verified by inspection) | include/stdlib.h |
| drand48 / erand48 | RETURN VALUE: "[0.0,1.0)" | covered | test/stdlib.c (basic), test/posix-stdlib.c (range over 200 draws, both the default-state and explicit-array forms) |
| lrand48 / nrand48 | RETURN VALUE: "[0,2**31)" | covered | test/posix-stdlib.c |
| mrand48 / jrand48 | RETURN VALUE: "[-2**31,2**31)" | covered | test/posix-stdlib.c |
| seed48 | RETURN VALUE: "a pointer to ... the previous value" of the 48-bit state | covered | test/posix-stdlib.c (checked against srand48()'s documented Xi=seed:0x330E encoding, then against a second seed48() call) |
| lcong48 | a/c parameters actually take effect (checked via a=1,c=0 fixed-point construction) | covered | test/posix-stdlib.c |
| random / srandom | reproducibility for a fixed seed | covered | test/stdlib.c |
| initstate | DESCRIPTION: size<8 fails (returns NULL); valid sizes (8/16/32/64/128/256, rounded down) succeed and return the previous state pointer | covered | test/posix-stdlib.c |
| setstate | RETURN VALUE: "a pointer to the previous state array" | covered | test/posix-stdlib.c |
| getenv | "a pointer to ... the value" / NULL if not found | covered | test/posix-stdlib.c |
| setenv | ERRORS: EINVAL for empty name or name containing '='; overwrite==0 leaves an existing var unchanged; overwrite!=0 replaces it | covered | test/posix-stdlib.c |
| unsetenv | RETURN VALUE 0 on success (incl. a no-op removal of a missing var); ERRORS: EINVAL for empty name or name containing '=' | covered | test/posix-stdlib.c |
| putenv | DESCRIPTION: "the string ... shall become part of the environment, so altering the string shall change the environment" (not copied) | covered | test/posix-stdlib.c (mutate the caller's buffer in place after putenv(), observe getenv() follow it) |
| environ | reflects live additions (sanity, not a distinct POSIX clause beyond getenv/setenv/unsetenv themselves) | covered (sanity) | test/posix-stdlib.c |
| clearenv / secure_getenv | not POSIX.1-2017 base (XSI legacy / GNU extension respectively) | N/A (not POSIX.1-2017 base) | — |
| atexit / exit / _Exit / abort / quick_exit / at_quick_exit | LIFO order, abort()-under-SIGABRT-ignored still terminates, exit() vs _Exit() exit-code propagation to a waiting parent | covered elsewhere (child-process spawn pattern) | test/misc.c (owned by another agent) |
| quick_exit / at_quick_exit | not exercised anywhere (glibc/C11, no child-process test written this session) | not yet reached | — |
| mkstemp / mkstemps / mkdtemp | template validation (EINVAL for no XXXXXX), successful creation, O_EXCL-style uniqueness | covered | test/stdlib.c |
| mkstemp family | EINVAL for a template with fewer than six trailing X's (five, not just zero) | covered | test/posix-stdlib.c |
| mkostemp / mkostemps | DESCRIPTION: created "as if" by open() with O_RDWR forced regardless of any access-mode bits in flags (glibc/ntlibc `flags &= ~O_ACCMODE`, src/stdlib/mktemp.c); resulting descriptor is a regular file open for read+write | covered | test/posix-stdlib.c |
| mkstemp permission bits (open(..., S_IRUSR\|S_IWUSR)) | not independently checked: NT has no faithful S_IRUSR/S_IWUSR mode-bit model to assert against without risking a Wine-vs-real-Windows false finding (see the environment note about read-only chmod/unlink under Wine) | N/A (no reliable NT permission-bit oracle under Wine) | — |
| realpath | NULL resolved_name buffer allocates and returns a heap pointer; ENOENT for a missing path; drive-letter-absolute, forward-slash-normalized result | covered | test/stdlib.c |
| realpath | RETURN VALUE: with a caller-supplied (non-NULL) resolved_name buffer, "a pointer to the resolved_name argument" is returned | covered | test/posix-stdlib.c |
| system | RETURN VALUE: system(NULL) always non-zero when a shell is available; command's wait-status decoded through WIFEXITED/WEXITSTATUS | covered | test/posix-stdlib.c (system(NULL), and `cmd.exe /c "exit N"` for N=0,1,5) |
| system | "as if the command interpreter terminated using exit(127)" when the shell itself cannot be executed | not independently triggerable (system()'s own shell-discovery success is a precondition for every other case tested) | test/posix-stdlib.c (comment only) |
| a64l | RETURN VALUE: 0L for an empty string; DESCRIPTION digit mapping ('.'=0 through 'z'=63); first character is the least-significant digit; only the first six characters are used | covered | test/stdlib.c (empty string, round-trip), test/posix-stdlib.c (full digit table, lsd-first, >6-char truncation) |
| l64a | RETURN VALUE: pointer to an empty string for 0L | covered | test/stdlib.c, test/posix-stdlib.c |
| getsubopt | DESCRIPTION: "shall not modify the keylistp vector"; single-suboption case leaves \*optionp pointing at the terminating NUL; token-not-matched / token-matched-no-value / token-matched-with-value return codes | covered | test/stdlib.c (token/value cases), test/posix-stdlib.c (keylistp untouched, single-suboption end-of-string) |
| getloadavg | not POSIX.1-2017 (BSD/glibc extension); no loadavg source on NT, so it always returns -1 | N/A (not POSIX.1-2017) | test/stdlib.c |
| ecvt / fcvt / gcvt | not POSIX.1-2017 base (SVID/XSI legacy, removed as a base requirement) | N/A (XSI legacy, not base) | test/stdlib.c |
| mblen / mbtowc / wctomb / mbrtowc / wcrtomb / mbstowcs / wcstombs / btowc / wctob | UTF-8 <-> UTF-16 conversion, partial/surrogate-pair sequences, mbstate_t | covered | test/stdlib.c (deferred to the wchar.h pass per test/POSIX-COVERAGE.md; not re-audited clause-by-clause here) |

## Bugs found this session

None. Every clause checked against `strtol.html`, `atoi.html`,
`qsort.html`, `bsearch.html`, `drand48.html`, `random.html`,
`getenv.html`, `setenv.html`, `putenv.html`, `unsetenv.html`,
`mkstemp.html`, `realpath.html`, `system.html`, `a64l.html`, and
`getsubopt.html` matched ntlibc's `src/stdlib/` (and, for
getenv/setenv/exit, `src/env/` and `src/exit/`) implementation.

## `src/` changes

None needed. Every clause of interest was already reachable through
the public API (no wrapper hid a decision that needed extracting), so
`src/stdlib/` is untouched this session.

## What was not reached

- `quick_exit`/`at_quick_exit`: declared and implemented
  (`src/exit/exit.c`), but no test exercises them anywhere in the tree
  (`atexit`/`exit`/`abort`/`_Exit` are covered via `test/misc.c`'s
  child-spawn pattern, which a successor could extend for
  `quick_exit`).
- `system()`'s "shell could not be executed after the child was
  created -> as if exit(127)" clause: not independently triggerable
  without breaking the very shell-discovery this session's other
  `system()` tests depend on.
- mkstemp/mkostemp file permission bits (S_IRUSR|S_IWUSR): left
  unchecked rather than risk a false finding against NT's
  approximation of POSIX permission bits under Wine.
