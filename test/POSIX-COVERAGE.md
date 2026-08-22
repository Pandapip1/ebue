<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# POSIX conformance coverage ledger

Tracks, function by function, how far the clause-by-clause POSIX.1-2017
audit (`https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html`)
has gotten against what ntlibc actually implements (`src/`, declared in
`include/`). A successor should read this file before doing anything
else, pick up at the first "not yet reached" group in the priority
order below, and update this file as they go.

Status values:
- **covered** — every testable DESCRIPTION/RETURN VALUE/ERRORS clause has
  an assertion, either pre-existing or added.
- **N/A (reason)** — not a POSIX.1-2017 base function (GNU/BSD
  extension), or the LEGACY variant removed in Issue 7, or the clause
  cannot be triggered/observed under Wine or without real hardware
  failure (e.g. malloc-exhaustion ENOMEM paths).
- **BUG (fenced)** — a genuine spec violation found; test is in the file
  with `#if 0 /* BUG: ... */`, see "Bugs found" below.
- **not yet reached** — nobody has checked the spec page against the
  tests yet.

## Priority order (per the task brief)

Done (clause-by-clause audited, see the per-header sections below):

1. `string.h` / `strings.h`
2. `stdlib.h` conversions, `qsort`/`bsearch`, `getenv`/`setenv`/random family
3. `time.h` calendar and clock functions
4. `dirent.h`, plus the smaller headers audited alongside it this round:
   `ctype.h` (base `is*`/`to*` family; the XSI `isascii`/`toascii` pair is
   not re-audited, see below), `locale.h`, `libgen.h`, `setjmp.h`,
   `getopt()`
5. `stdio.h` streams
6. `unistd.h` process/file ops, `fcntl.h`, `sys/stat.h`
7. `signal.h`, `sys/wait.h`
8. `wchar.h` / multibyte conversions
9. `math.h`
10. `limits.h` / `float.h` / `stdint.h` / `inttypes.h`, plus
    `strtoimax`/`strtoumax`/`imaxabs`/`imaxdiv`
11. `malloc`/`calloc`/`realloc`/`free`/`posix_memalign` and friends,
    `exit`/`_Exit`/`abort`/`atexit`/`assert`,
    `getenv`/`setenv`/`unsetenv`/`putenv`/`environ`

Not yet reached:

- `ctype.h`'s XSI `isascii`/`toascii` pair (base `is*`/`to*` is done,
  see above)
- `strings.h` is fully covered (folded into the string.h table above)
- `utime.h` (`utime()`)
- `endian.h` (mostly macros; not clause-audited)
- `assert.h` (`assert()`/`__assert_fail`/`static_assert`)
- `sys/select.h` — `select()` is declared but not implemented
  (see `include/sys/select.h`'s own `undefined-ok` comment explaining
  why); nothing to audit until it exists
- `sys/resource.h` (`getrlimit`/`setrlimit`/`getrusage`) — only ad-hoc
  sanity coverage exists (`test/time.c`), no clause audit
- `sys/param.h` (BSD macros only, no functions)
- `getopt_long`/`getopt_long_only` (GNU extensions, no POSIX page) and
  anything else in `libgen.h`/`getopt.h` beyond what the dirent-group
  audit above covers

## string.h / strings.h

Existing sanity coverage: `test/string.c` (broad, one assertion per
function, not clause-cited). New clause-cited audit: `test/posix-string.c`
(this session). Column "test" says where the requirement is asserted.

| function | clause checked | status | test |
|---|---|---|---|
| memcpy | copies n bytes | covered | test/string.c |
| memmove | overlap defined, correct on both directions | covered | test/string.c |
| memcmp | unsigned-char interpretation of bytes and of result sign | covered | test/string.c (0xff>0x01), test/posix-string.c (n==0 -> 0) |
| memset | fills n bytes, leaves rest | covered | test/string.c |
| memchr | finds byte / returns NULL if absent | covered | test/string.c |
| memccpy | stops at first `c` or after n bytes; returns ptr-after-c or NULL if not found; n==0 copies nothing | covered | test/string.c (found/not found), test/posix-string.c (n==0) |
| mempcpy | not POSIX (GNU extension) | N/A (not POSIX.1-2017) | test/string.c (sanity only) |
| memrchr | not POSIX (GNU extension) | N/A (not POSIX.1-2017) | test/string.c (sanity only) |
| memmem | not POSIX (GNU extension) | N/A (not POSIX.1-2017) | test/string.c (sanity only) |
| strcpy | copies incl. NUL; overlap UB (untestable) | covered | test/string.c |
| stpcpy | returns ptr to terminating NUL | covered | test/string.c |
| strncpy | pads with NUL to n if source shorter; no NUL appended if source has no NUL in first n; overlap UB | covered | test/string.c (padding + truncation-no-pad) |
| stpncpy | same copy semantics as strncpy; returns ptr to written NUL, or &s1[n] if none written | covered | test/string.c (NUL-written case), test/posix-string.c (no-NUL-written case returns s1+n) |
| strcat / strncat | appends, NUL-terminates; overlap UB (untestable) | covered | test/string.c |
| strcmp | lexicographic, unsigned-char bytes | covered | test/string.c (incl. 0xff case) |
| strncmp | compares at most n bytes | covered | test/string.c |
| strcoll / strcoll_l | POSIX-locale ordering (only locale ntlibc supports) | covered | test/string.c |
| strxfrm / strxfrm_l | n==0 permits s1==NULL; result length excl. NUL; errno unchanged on success | covered | test/string.c (n==0, truncation), test/posix-string.c (errno preserved) |
| strchr | finds byte, NUL terminator is part of the search string | covered | test/string.c (`strchr(buf,0)==buf+5`) |
| strrchr | last occurrence, same NUL rule | covered | test/string.c |
| strchrnul | not POSIX (GNU extension) | N/A (not POSIX.1-2017) | test/string.c (sanity only) |
| strcspn | length of initial segment with no byte from s2 | covered | test/string.c, test/posix-string.c (single-byte boundary) |
| strspn | length of initial segment with only bytes from s2 | covered | test/string.c, test/posix-string.c (single-byte boundary) |
| strpbrk | first byte in s1 that is also in s2, or NULL | covered | test/string.c |
| strstr | first substring occurrence, empty needle matches at start | covered | test/string.c |
| strcasestr | not POSIX (GNU extension) | N/A (not POSIX.1-2017) | test/string.c (sanity only) |
| strtok | breaks on delimiter set; NUL overwrites delimiter; NULL arg continues; empty sep returns whole remainder (APPLICATION USAGE) | covered | test/string.c (basic), test/posix-string.c (empty sep) |
| strtok_r | same, with explicit save pointer | covered | test/string.c (basic), test/posix-string.c (empty sep) |
| strsep | not POSIX (BSD extension) | N/A (not POSIX.1-2017) | test/string.c (sanity only) |
| strdup | duplicate via malloc semantics; NULL + ENOMEM on failure | partially covered | test/string.c (success path only) — ENOMEM path N/A: not reliably triggerable without exhausting the address space under Wine |
| strndup | duplicate at most n bytes, always NUL-terminated | covered | test/string.c |
| strlcpy / strlcat | not POSIX (BSD extension) | N/A (not POSIX.1-2017) | test/string.c (sanity only) |
| strerror | maps errnum to message; unknown errnum still succeeds with an "unknown" message; errno unchanged on success | covered | test/string.c (known/unknown codes), test/posix-string.c (errno preserved) |
| strerror_l | same, explicit locale | covered | test/string.c |
| strerror_r | returns 0 on success or an error number (not via errno); ERANGE if buffer too small | covered | test/string.c |
| strsignal | maps signum to message; POSIX.1-2017 **base** function (verified against `strsignal.html` — an earlier pass here mis-recorded this as XSI-only), full 1.._NSIG-1 sweep, unspecified-not-crashing for an invalid signum | covered | test/string.c (spot checks), test/posix-signal.c (full sweep, invalid-signum case) |
| strverscmp | not POSIX (GNU extension) | N/A (not POSIX.1-2017) | test/string.c (sanity only) |
| strcasecmp / strcasecmp_l | case-insensitive comparison, POSIX-locale folding, ordering not just equality | covered | test/string.c (equality/inequality), test/posix-string.c (ordering sign) |
| strncasecmp / strncasecmp_l | same, bounded to n bytes | covered | test/string.c, test/posix-string.c |
| bcmp | LEGACY, removed from POSIX.1-2017 strings.h (was SUSv3) | N/A (removed in Issue 7) | test/string.c (sanity only) |
| bcopy | LEGACY, removed from POSIX.1-2017 strings.h | N/A (removed in Issue 7) | test/string.c (sanity only) |
| bzero | LEGACY, removed from POSIX.1-2017 strings.h | N/A (removed in Issue 7) | test/string.c (sanity only) |
| index | LEGACY, removed from POSIX.1-2017 strings.h | N/A (removed in Issue 7) | test/string.c (sanity only) |
| rindex | LEGACY, removed from POSIX.1-2017 strings.h | N/A (removed in Issue 7) | test/string.c (sanity only) |
| explicit_bzero | not POSIX (glibc/BSD extension) | N/A (not POSIX.1-2017) | test/string.c (sanity only) |
| ffs | XSI option group in strings.h | covered (sanity) — not re-audited clause-by-clause this session | test/string.c |
| ffsl / ffsll | not POSIX (GNU extension, `ffs` widened) | N/A (not POSIX.1-2017) | test/string.c (sanity only) |
| wcs*/wmem* (declared via string.h/wchar.h) | — | not yet reached | test/string.c (sanity only); defer clause audit to the wchar.h pass |

### Bugs found this session

None. Every clause checked against `strncpy.html`, `memccpy.html`,
`strtok.html`, `strxfrm.html`, `strerror.html`, `memcmp.html`,
`strcspn.html`, `strcasecmp.html`, `strings.h.html`, `strchr.html`,
`strpbrk.html`, `strdup.html` matched ntlibc's implementation.

### Where to resume in string.h/strings.h

The table above is essentially complete for the base POSIX.1-2017
`string.h` surface. Remaining loose end: `strdup`'s ENOMEM path is
untested (flagged N/A, not a gap to silently drop — a successor with a
way to force allocation failure under Wine could close it).

## stdlib.h (priority 2: conversions, qsort/bsearch, getenv/random family)

New clause-cited audit: `test/posix-stdlib.c` (96 `CHECK()` assertions).
Pre-existing coverage checked against the spec rather than duplicated:
`test/strto.c`, `test/qsort.c`, `test/stdlib.c`, `test/malloc.c`,
`test/posix-parse.c`. `test/misc.c` already covers `atexit`/`exit`/
`_Exit`/`abort` child-process behaviour; not duplicated here.

| function | clause checked | status | test |
|---|---|---|---|
| strtol / strtoul / strtoll / strtoull / strtoimax / strtoumax | subject-sequence decomposition, base-0/base-16 prefix rules, negation, ERANGE clamping to \*_MIN/\*_MAX, no-conversion endptr==nptr | covered | test/strto.c, test/posix-parse.c |
| strtol / strtoul family | RETURN VALUE + ERRORS: unsupported base (not 0, not 2-36) returns 0 with errno=EINVAL (required, not "may fail") | covered | test/posix-stdlib.c (bases 1, 37, -1) |
| strtol / strtoul / strtod | "shall not change the setting of errno if successful" | covered | test/posix-stdlib.c (sentinel errno=12345 across a clean parse) |
| strtod / strtof / strtold | rounding (exactly-correct to the ulp, overflow/underflow boundaries, inf/nan spellings, hundreds-of-digits inputs) | covered | test/strto.c (exhaustive, incl. an 86-case table cross-checked against glibc) |
| atoi / atol / atoll / atof | equivalent to `(int)strtol(str,NULL,10)` / `strtod(str,NULL)`; "No errors are defined" | covered | test/strto.c (basic), test/posix-stdlib.c (whitespace/sign/junk/empty forms) |
| qsort / qsort_r | ascending order, arbitrary element size, comparator consistency | covered | test/qsort.c |
| qsort / qsort_r | DESCRIPTION: "If nel has the value zero, the comparison function ... shall not be called" | covered | test/posix-stdlib.c |
| bsearch | found/not-found/first/last/empty-array cases | covered | test/qsort.c |
| bsearch | DESCRIPTION: nel==0 → comparator not called, no match; comparator called with (key, member) in that fixed order | covered | test/posix-stdlib.c |
| malloc / calloc / realloc / free / posix_memalign / aligned_alloc / memalign / valloc / reallocarray / malloc_usable_size | `src/malloc/` — see the "Memory allocation, process termination, and environment" section below for the clause-cited audit | covered — see below | test/malloc.c, test/posix-alloc.c |
| abs / labs / llabs / imaxabs / div / ldiv / lldiv / imaxdiv | \|x\| for representable x; quot/rem pairs incl. truncation-toward-zero on negative operands | covered | test/stdlib.c |
| abs family | abs(INT_MIN) etc. is UB (C99 7.20.6.1p2) — deliberately not tested | N/A (undefined behaviour, C99 7.20.6.1p2) | — |
| rand / srand / rand_r | reproducibility for a fixed seed; range implied by RAND_MAX | covered | test/stdlib.c |
| RAND_MAX >= 32767 (C99 7.20.2.1) | `#define RAND_MAX (0x7fffffff)` in include/stdlib.h — verified by inspection, not a runtime assertion | N/A (verified by inspection) | include/stdlib.h |
| drand48 / erand48 | RETURN VALUE: "[0.0,1.0)" | covered | test/stdlib.c (basic), test/posix-stdlib.c (range over 200 draws) |
| lrand48 / nrand48 | RETURN VALUE: "[0,2**31)" | covered | test/posix-stdlib.c |
| mrand48 / jrand48 | RETURN VALUE: "[-2**31,2**31)" | covered | test/posix-stdlib.c |
| seed48 | RETURN VALUE: "a pointer to ... the previous value" of the 48-bit state | covered | test/posix-stdlib.c |
| lcong48 | a/c parameters actually take effect | covered | test/posix-stdlib.c |
| random / srandom | reproducibility for a fixed seed | covered | test/stdlib.c |
| initstate | DESCRIPTION: size<8 fails (NULL); valid sizes (8/16/32/64/128/256, rounded down) succeed, return the previous state pointer | covered | test/posix-stdlib.c |
| setstate | RETURN VALUE: pointer to the previous state array | covered | test/posix-stdlib.c |
| getenv | pointer to the value / NULL if not found | covered | test/posix-stdlib.c |
| setenv | ERRORS: EINVAL for empty name or name containing '='; overwrite==0 leaves an existing var unchanged; overwrite!=0 replaces it | covered | test/posix-stdlib.c |
| unsetenv | RETURN VALUE 0 (incl. no-op removal of a missing var); ERRORS EINVAL for empty name or name containing '=' | covered | test/posix-stdlib.c |
| putenv | DESCRIPTION: the string becomes part of the environment (not copied) — mutating the caller's buffer changes what getenv() sees | covered | test/posix-stdlib.c |
| environ | reflects live additions | covered (sanity) | test/posix-stdlib.c |
| clearenv / secure_getenv | not POSIX.1-2017 base (XSI legacy / GNU respectively) | N/A (not POSIX.1-2017 base) | — |
| atexit / exit / _Exit / abort / quick_exit / at_quick_exit | LIFO order, abort()-under-SIGABRT-ignored still terminates, exit()/_Exit() exit-code propagation | covered elsewhere | test/misc.c |
| quick_exit / at_quick_exit | no child-process test written | not yet reached | — |
| mkstemp / mkstemps / mkdtemp | template validation (EINVAL for no XXXXXX), successful creation, uniqueness | covered | test/stdlib.c |
| mkstemp family | EINVAL for fewer than six trailing X's | covered | test/posix-stdlib.c |
| mkostemp / mkostemps | DESCRIPTION: created "as if" by open() with O_RDWR forced regardless of access-mode bits in flags | covered | test/posix-stdlib.c |
| mkstemp permission bits (S_IRUSR\|S_IWUSR) | not independently checked (no reliable NT permission-bit oracle under Wine) | N/A | — |
| realpath | NULL resolved_name allocates and returns a heap pointer; ENOENT for a missing path; drive-letter-absolute, forward-slash-normalized result | covered | test/stdlib.c |
| realpath | RETURN VALUE: with a caller-supplied buffer, returns that same pointer | covered | test/posix-stdlib.c |
| system | RETURN VALUE: system(NULL) always non-zero when a shell is available; wait-status decoded through WIFEXITED/WEXITSTATUS | covered | test/posix-stdlib.c |
| system | "as if exit(127)" when the shell itself cannot be executed | not independently triggerable | test/posix-stdlib.c (comment only) |
| a64l | RETURN VALUE 0L for empty string; digit mapping ('.'=0..'z'=63); first char is least-significant digit; only first six chars used | covered | test/stdlib.c, test/posix-stdlib.c |
| l64a | RETURN VALUE: pointer to an empty string for 0L | covered | test/stdlib.c, test/posix-stdlib.c |
| getsubopt | DESCRIPTION: does not modify keylistp; single-suboption case leaves \*optionp at the terminating NUL; return codes for not-matched/matched-no-value/matched-with-value | covered | test/stdlib.c, test/posix-stdlib.c |
| getloadavg | not POSIX.1-2017 (BSD/glibc); always -1 on NT | N/A (not POSIX.1-2017) | test/stdlib.c |
| ecvt / fcvt / gcvt | not POSIX.1-2017 base (SVID/XSI legacy) | N/A (XSI legacy, not base) | test/stdlib.c |
| mblen / mbtowc / wctomb / mbrtowc / wcrtomb / mbstowcs / wcstombs / btowc / wctob | UTF-8 <-> UTF-16 conversion | covered — see wchar.h section below | test/posix-wchar.c |

No bugs found in stdlib.h this session; every clause checked matched
`src/stdlib/` (and, for getenv/setenv/exit, `src/env/`/`src/exit/`).

### Not reached (stdlib.h)

`quick_exit`/`at_quick_exit` (implemented, no child-spawn test written);
`system()`'s exit(127)-on-exec-failure clause (not independently
triggerable); mkstemp/mkostemp permission bits (Wine oracle risk). The
`malloc`/`calloc`/... family is covered separately, see the "Memory
allocation, process termination, and environment" section below.

## time.h calendar and clock functions (priority 3)

New clause-cited audit: `test/posix-time.c`. Existing ad-hoc coverage:
`test/time.c` (broad, known-epoch table). `src/time/` reviewed in full
except `nanosleep()`, which lives in `src/unistd/sleep.c` (audited
under the unistd.h group, see below).

| function | clause checked | status | test |
|---|---|---|---|
| time | RETURN VALUE: returns value, stores into *tloc if non-null | covered | test/time.c |
| time | ERRORS: EOVERFLOW if seconds since Epoch don't fit time_t | N/A (untriggerable: time_t is 64-bit) | — |
| difftime | RETURN VALUE: `double`, computes time_1 - time_0 | covered | test/time.c, test/posix-time.c |
| clock | DESCRIPTION: CPU time, not wall time; CLOCKS_PER_SEC==1000000 | covered | test/time.c, test/posix-time.c |
| clock | RETURN VALUE: (clock_t)-1 on failure | N/A (no way to force a ProcessTimes query failure under Wine) | — |
| mktime | DESCRIPTION: out-of-range fields normalized; tm_wday/tm_yday ignored on input, set on output; calls tzset() | covered | test/time.c, test/posix-parse.c, test/posix-time.c |
| mktime | RETURN VALUE/ERRORS: (time_t)-1 + EOVERFLOW if unrepresentable | **fixed**, commit a750adb (`mktime()` now propagates `localtime_r()`'s NULL/EOVERFLOW instead of returning the raw out-of-range instant) | test/posix-time.c `test_mktime_overflow_returns_minus_one` |
| timegm | not POSIX.1-2017 (BSD/glibc extension) | N/A (not POSIX.1-2017) | test/time.c |
| gmtime / gmtime_r | seconds-since-Epoch -> broken-down UTC; NULL on error | covered | test/time.c, test/posix-time.c |
| localtime / localtime_r | local = UTC shifted by tzset()'s fixed offset | covered | test/time.c, test/posix-time.c |
| asctime / asctime_r | exact 26-byte format; asctime_r needs >=26-byte buf | covered | test/time.c, test/posix-time.c |
| ctime / ctime_r | equivalent to `asctime(localtime(clock))`; no errors defined | covered | test/time.c, test/posix-time.c |
| strftime / strftime_l | byte count excl. NUL, or 0 if it doesn't fit; LC_TIME-driven (POSIX-locale only) | covered | test/time.c, test/posix-parse.c |
| strftime | `%U %W %V %G %g` (ISO-8601 week-number family) | not implemented, pass-through documented | src/time/strftime.c header comment; test/time.c pins the pass-through | -- |
| strptime | pointer past last char parsed, or NULL; %y century pivot [69,99]->19xx, [00,68]->20xx | covered | test/time.c, test/posix-time.c |
| strptime | %C ("all but the last two digits of the year") | **fixed**, commit a750adb (added, composes correctly with %y regardless of order) | test/posix-time.c `test_strptime_century` |
| strptime | %U/%W (week numbers) | **fixed**, commit a750adb (parsed and discarded, matching glibc/musl — struct tm has no week-number field) | test/posix-time.c |
| tzset / tzname / daylight / timezone | TZ parsing (name[+-]offset), tzname[0]/[1], daylight, timezone | covered | test/time.c, test/posix-time.c |
| nanosleep | `src/unistd/sleep.c` | see unistd.h section below | test/posix-unistd.c-adjacent (not audited under time.h) |
| clock_nanosleep | EINVAL for bad nsec/unknown clock; relative vs TIMER_ABSTIME semantics | covered | test/posix-time.c |
| clock_nanosleep | TIMER_ABSTIME's absolute time measured against clock_id's *own* reading | **fixed**, commit a750adb (CLOCK_MONOTONIC/TIMER_ABSTIME now measured against a fresh CLOCK_MONOTONIC reading instead of run through the unix-epoch `__unix_to_nt` conversion) | test/posix-time.c `test_clock_nanosleep_monotonic_abstime` |
| clock_gettime | 0 or -1/EINVAL for unknown clock; EOVERFLOW if seconds don't fit | covered (EINVAL); EOVERFLOW N/A (64-bit time_t) | test/time.c |
| clock_settime | EINVAL for unknown/unsettable clock id, EINVAL for tv_nsec outside [0,999999999], EPERM possible | covered | test/time.c |
| clock_settime | EINVAL for out-of-range tv_nsec | **fixed**, commit a750adb (now checked before the `NtSetSystemTime` call) | test/posix-time.c `test_clock_settime_bad_nsec` (validation-only, doesn't touch the real clock) |
| clock_getres | NULL res is a legal no-op query, not an error; EINVAL for unknown clock | covered | test/time.c, test/posix-time.c |
| clock_getres | NULL res must be accepted | **fixed**, commit a750adb (was an unconditional SIGSEGV; now every branch checks `res` first) | test/posix-time.c `test_clock_getres_null` |
| clock_getcpuclockid | 0 + clock id, or ESRCH if pid unknown | covered | test/time.c |
| timespec_get | C11/POSIX.1-2024, not POSIX.1-2017 base; nonzero base on success, 0 otherwise | covered | test/time.c, test/posix-time.c |
| getdate | real getdate() reads $DATEMSK; struct tm* or NULL + getdate_err | N/A (documented reimplementation, not the POSIX algorithm — see src/time/getdate.c's header comment) | test/time.c |

All five bugs this fragment originally found were fixed in commit
`a750adb` ("Fix five POSIX time conformance bugs") and the corresponding
fenced tests in `test/posix-time.c` were un-fenced; none remain open.

### Not reached (time.h)

`nanosleep()` (audited under unistd.h below); the ISO-8601 week-number
`strftime` family (`%U %W %V %G %g`, intentionally unimplemented);
`getdate()`'s reimplementation gaps (intentional, documented in
`src/time/getdate.c`).

## dirent.h, ctype.h, locale.h, libgen.h, setjmp.h, getopt() (priority 4)

New clause-cited audit: `test/posix-misc.c`. Existing ad-hoc coverage:
`test/dirent.c`, `test/ctype.c`, `test/getopt.c`, `test/misc.c`.

### dirent.h

| function | clause checked | status | test |
|---|---|---|---|
| opendir | positioned at first entry; ENOTDIR on a non-directory path component; ENOENT on missing/empty dirname | covered | test/dirent.c, test/posix-misc.c |
| fdopendir | ENOTDIR if fd does not reference a directory | covered | test/posix-misc.c |
| readdir | errno unchanged on success and at end-of-directory | covered | test/posix-misc.c |
| readdir_r | *result == entry on success, NULL at end; return value is an error number, not errno | covered | test/posix-misc.c |
| rewinddir | resets to the beginning of the stream; void return | covered | test/dirent.c, test/posix-misc.c |
| rewinddir / readdir | whether a file added *after* opendir()/rewinddir() is visible | explicitly unspecified by readdir.html; recorded as an observation, not asserted (see below) | -- |
| telldir / seekdir | seekdir(dp, telldir(dp)) is a no-op; seeking to an earlier value reproduces that position | covered | test/dirent.c, test/posix-misc.c |
| dirfd | returns a valid, usable fd for the stream | covered | test/dirent.c, test/posix-misc.c |
| closedir | returns 0 on success | covered | test/posix-misc.c |
| scandir | entries sorted by the comparator; "." and ".." included like readdir() | covered (pre-existing) | test/dirent.c |
| alphasort | sorts by name | covered (pre-existing) | test/dirent.c |

**Observation, not a bug**: `readdir.html` DESCRIPTION explicitly leaves
unspecified whether a file added/removed after the most recent
`opendir()`/`rewinddir()` shows up in a subsequent `readdir()`.
Empirically under Wine, `rewinddir()` + `readdir()` on the *same* handle
does not see a file created in between (looks like an NT directory-handle
enumeration cache), even though a fresh `opendir()` on the same path
does. Not asserted either way since POSIX permits both.

### ctype.h

| function | clause checked | status | test |
|---|---|---|---|
| is*/to* family | argument must be representable as unsigned char or equal EOF; UB otherwise | covered | test/ctype.c (full 0..255 + EOF table), test/posix-misc.c (confirms plain `char` is signed here, EOF-returns-false-for-every-classifier, EOF-unchanged-by-to*()) |
| toupper / tolower | value with no case mapping (incl. EOF) is returned unchanged | covered | test/ctype.c, test/posix-misc.c |
| isascii / toascii | XSI extension (`_XOPEN_SOURCE`/`_GNU_SOURCE`/`_BSD_SOURCE`-gated in include/ctype.h), not POSIX.1-2017 base | not yet reached | -- |

No bugs found: ntlibc's ctype tables are branch-computed, not indexed by
a raw (possibly negative) `int`, so there is no out-of-bounds read for a
negative `char` promoted to `int` — still UB per the clause, but does
not crash here.

### locale.h

| function | clause checked | status | test |
|---|---|---|---|
| setlocale | "C"/"POSIX" recognized for every category; NULL queries without changing; unsupported name returns NULL and leaves the global locale unchanged | covered | test/misc.c, test/getopt.c, test/posix-misc.c |
| localeconv | struct lconv char members use CHAR_MAX to mean "not available"; decimal_point stays non-empty | covered | test/misc.c, test/posix-misc.c |

No bugs found.

### libgen.h

| function | clause checked | status | test |
|---|---|---|---|
| basename / dirname | full basename.html EXAMPLES table | covered | test/misc.c, test/getopt.c, test/posix-misc.c |
| basename / dirname | Windows drive-letter prefixes (`C:\`, `C:/foo`, `C:foo`) | ntlibc extension, not POSIX | test/posix-misc.c |

No bugs found. "//" -> "/" or "//" is implementation-defined by POSIX
itself; not asserted either way.

### setjmp.h

| function | clause checked | status | test |
|---|---|---|---|
| setjmp / longjmp | 0 direct return, nonzero via longjmp for several values, longjmp(env,0) yields 1 | covered | test/misc.c, test/posix-misc.c |
| longjmp | volatile automatic locals changed between setjmp/longjmp are preserved | covered | test/misc.c, test/posix-misc.c |
| sigsetjmp / siglongjmp | same value contract as setjmp/longjmp | covered | test/posix-misc.c |

No bugs found. NT has no real signal mask, so `sigsetjmp`/`siglongjmp`
share the plain `setjmp`/`longjmp` assembly body — the "restore the
signal mask" half of the contract is vacuously satisfied.

### getopt() (unistd.h / getopt.h)

| function | clause checked | status | test |
|---|---|---|---|
| getopt | "--" discarded, -1 returned, optind left at the first operand | covered | test/getopt.c, test/posix-misc.c |
| getopt | leading ':' suppresses error messages / changes '?' to ':'; unknown option -> '?' with optopt set, independent of opterr | covered | test/getopt.c, test/posix-misc.c |

No bugs found.

### Not reached (dirent.h group)

`getopt_long`/`getopt_long_only` (GNU extensions, no spec page);
`d_type`/`DT_*` (BSD/GNU extension to `struct dirent`); `EOVERFLOW`/
`ENOENT` "may fail" paths for readdir/readdir_r (not triggerable without
corrupting NT-internal state); real (non-Wine) Windows behavior for the
rewinddir-cache observation above.

## stdio.h streams (priority 5)

New clause-cited audit: `test/posix-stdio.c`. Existing broad sanity
coverage: `test/stdio.c` (~430 checks).

| function | clause checked | status | test |
|---|---|---|---|
| fopen / fdopen / freopen | mode parsing, `+`/update semantics, fd takeover | covered | test/stdio.c |
| fopen | update-stream rule (no direct read-after-write or vice versa without an intervening flush/seek, unless EOF) | covered — ntlibc's `__toread`/`__towrite` apply the implicit flush/seek on every direction switch automatically | test/posix-stdio.c |
| fclose | closes fd, frees FILE, flushes pending writes | covered | test/stdio.c |
| fread / fwrite | byte counts, partial reads, size/nmemb == 0 | covered | test/stdio.c |
| fgetc / getc / getchar | EOF at end, unsigned-char return | covered | test/stdio.c |
| ungetc | one-char guarantee, discarded by fseek/fsetpos/rewind, clears EOF indicator | covered | test/stdio.c |
| ungetc | returns EOF when the stream is not open for reading | covered | test/posix-stdio.c |
| fputc / putc / putchar | write, buffering interaction | covered | test/stdio.c |
| fgets / fputs / puts | NUL termination, newline handling, NULL at EOF-with-nothing-read | covered | test/stdio.c |
| getline / getdelim | growth, -1 at EOF, delimiter | covered | test/stdio.c |
| fseek / fseeko | SEEK_SET/CUR/END, return 0/-1, clears EOF, undoes ungetc | covered | test/stdio.c |
| ftell / ftello | position accounts for buffered-ahead/behind bytes on update streams | covered | test/stdio.c |
| rewind / fgetpos / fsetpos | round-trip; rewind clears the error indicator too | covered | test/stdio.c |
| fflush | `fflush(NULL)` flushes every open stream | covered | test/stdio.c |
| fflush | on a readable stream with an underlying fd: discards not-yet-reread ungetc() bytes and resyncs the fd offset to the stream position | covered — was a BUG (`__fflush_locked` short-circuited for any non-writable stream); **fixed in 99474ee** | test/posix-stdio.c `test_fflush_read_stream` |
| setvbuf | valid before any other operation; returns 0 | covered | test/posix-stdio.c |
| setvbuf | returns non-zero for an invalid `type` | covered — was a BUG (any `type` accepted, 0 returned unconditionally); **fixed in 99474ee**: only `_IOFBF`/`_IOLBF`/`_IONBF` accepted, else EINVAL | test/posix-stdio.c |
| setbuf / setbuffer / setlinebuf | equivalence to a specific setvbuf call | covered | test/posix-stdio.c |
| feof / ferror / clearerr | independent indicators; clearerr clears both at once | covered | test/stdio.c, test/posix-stdio.c |
| printf family | conversion table, flags/width/precision, return value = bytes transmitted | covered | test/stdio.c, test/posix-stdio.c |
| printf family | `[EILSEQ]` on invalid wide-character code | N/A — formatter is POSIX-locale-only, no wide-char encoding step to fail | -- |
| printf family | `%n$` positional arguments | N/A (documented divergence) — not implemented; `"%1$d"` parses as width 1 + unrecognized `$`, net output `"%$d"` | test/posix-stdio.c |
| scanf family | conversion table, field width, %n, assignment suppression | covered (pre-existing, ~250 lines) | test/stdio.c |
| remove / rename | success, ENOENT | covered | test/stdio.c |
| tmpfile / tmpnam | uniqueness, L_tmpnam buffer sizing, removed-on-close semantics | covered | test/stdio.c |
| perror | thin wrapper (`strerror(errno)` to stderr) | not yet reached | -- |
| fileno | returns the underlying fd | covered | test/stdio.c |
| fmemopen / open_memstream | buffer growth, NUL-termination, mode parsing | covered | test/stdio.c |
| popen / pclose | no reachable POSIX shell semantics beyond src/stdio/misc.c's documented cmd.exe substitution | N/A (platform has no POSIX shell; divergence documented in src/stdio/misc.c) | -- |

Both bugs found by this suite have since been fixed in `src/stdio/buf.c`
(commit 99474ee) and their assertions un-fenced unmodified:
`__fflush_locked` now discards pending `ungetc()` pushback and seeks the
fd back by the read-ahead distance, and `setvbuf()` rejects any `type`
outside `_IOFBF`/`_IOLBF`/`_IONBF` with EINVAL. `_IOLBF` was confirmed to
be a real, distinct mode here (`src/stdio/rw.c` flushes on `'\n'`), not a
synonym for full buffering.

### Not reached (stdio.h)

`perror` (thin, low-risk wrapper); `popen`/`pclose` (N/A, no POSIX
shell on this platform).

## unistd.h, fcntl.h, sys/stat.h (priority 6)

New clause-cited audit: `test/posix-unistd.c`. Existing ad-hoc coverage:
`test/unistd.c` (~330 assertions), `test/posix-io.c` (errno-focused).

Function-group summary (see `test/posix-unistd.c` and `test/unistd.c`
for the full per-clause breakdown — this ledger records the outcome,
not every clause line, to keep this section a manageable size):

| group | status | test |
|---|---|---|
| open / openat / creat (EEXIST, EISDIR, ENOTDIR, O_TRUNC, O_APPEND, O_CLOEXEC) | covered | test/unistd.c, test/posix-unistd.c |
| open mode bits ANDed with ~umask | **fixed**, commit 3c606a7 (`open(O_CREAT)` now ANDs mode with `~umask` via a new `__umask_get()` accessor) | test/posix-unistd.c `test_open_umask_bug` |
| close / read / write / pread / pwrite / lseek | covered | test/posix-io.c, test/unistd.c, test/posix-unistd.c |
| dup / dup2 / dup3 / fcntl (F_DUPFD, F_GETFD/SETFD, F_GETFL/SETFL, F_GETLK/SETLK no-ops, EINVAL/EBADF) | covered | test/unistd.c, test/posix-unistd.c |
| pipe / pipe2 | covered | test/unistd.c, test/posix-io.c, test/posix-unistd.c |
| stat / fstat / lstat / fstatat / chmod / fchmod | covered | test/unistd.c, test/posix-unistd.c |
| mkdir / rmdir / unlink / unlinkat | covered | test/unistd.c, test/posix-io.c, test/posix-unistd.c |
| rename / renameat: success, ENOENT, same-file no-op | covered | test/unistd.c, test/posix-io.c, test/posix-unistd.c |
| rename EISDIR (new is a dir, old isn't) | **fixed**, commit 3c606a7 (`renameat()` in `src/stdio/misc.c` now disambiguates NT's `STATUS_ACCESS_DENIED` by querying old/new's types, giving EISDIR instead of EACCES) | test/posix-unistd.c `test_rename_new_dir_old_file_eisdir` |
| rename ENOTEMPTY/EEXIST (new is a non-empty dir) | **fixed**, commit 3c606a7 (same fix, gives ENOTEMPTY instead of EACCES) | test/posix-unistd.c `test_rename_onto_nonempty_dir` |
| link / symlink / readlink | covered | test/unistd.c |
| access / faccessat (F_OK/R_OK/W_OK/X_OK, ENOENT, EACCES) | covered | test/unistd.c |
| access/stat/open/unlink/rename trailing-slash-on-non-directory ENOTDIR | **fixed**, commit 3c606a7 (`__ntpath()`/`__ntpath_at()` in `src/internal/path.c` now re-check the resolved object's type after stripping a trailing slash — a shared-path-layer fix, so it covers all of these, not just access()) | test/posix-unistd.c `test_access_trailing_slash_enotdir` |
| chdir / fchdir / getcwd (incl. ERANGE off-by-one boundary) | covered | test/unistd.c, test/posix-unistd.c |
| ftruncate / truncate | covered | test/unistd.c |
| fsync / fdatasync | covered | test/unistd.c, test/posix-io.c |
| isatty / ttyname / ttyname_r | covered, except ttyname_r ERANGE (only reachable from a real console fd; test detects and skips) | test/unistd.c, test/posix-unistd.c |
| getpid / getppid / sysconf / pathconf / fpathconf / umask | covered | test/unistd.c, test/posix-unistd.c |
| utimensat / futimens / utime / utimes / futimes / lutimes / futimesat | covered | test/unistd.c |
| nanosleep (`src/unistd/sleep.c`) | covered (sanity, via test/unistd.c; not separately clause-cited) | test/unistd.c |

All four bugs originally found here (umask, trailing-slash ENOTDIR,
rename EISDIR, rename ENOTEMPTY) were fixed in commit `3c606a7` ("Fix
four POSIX conformance bugs: umask, trailing-slash ENOTDIR, rename
EISDIR/ENOTEMPTY") and the corresponding fenced tests in
`test/posix-unistd.c` were un-fenced; none remain open.

### Not reached (unistd.h group)

`sys/stat.h`'s `st_size`/directory-mode-bits N/A cases (implementation-
defined, matches ntlibc's own documented design); real-hardware (non-
Wine) EPERM/process-group cases for `kill`/`access` (see the signal.h
section below for the process-group rationale, which applies here too).

## signal.h, sys/wait.h (priority 7)

New clause-cited audit: `test/posix-signal.c`. Existing ad-hoc coverage:
`test/misc.c`, `test/waitpid-overflow.c`. There is no asynchronous
signal delivery from another thread/process on this platform — every
signal is delivered synchronously — which shapes most of the N/A
entries below.

| function | clause checked | status | test |
|---|---|---|---|
| signal | SIG_DFL/SIG_IGN/handler set & returns previous disposition; EINVAL+SIG_ERR for invalid sig / SIGKILL / SIGSTOP | covered | test/misc.c, test/posix-signal.c |
| sig_atomic_t | assignable in a handler, visible to the caller once the delivering call returns | covered | test/posix-signal.c |
| raise | returns 0; runs the handler before returning (always synchronous here); EINVAL for invalid sig | covered | test/misc.c, test/posix-signal.c |
| kill | pid==caller routes to raise(); sig==0 existence/permission check; pid>0 real process; EINVAL/ESRCH | covered | test/misc.c, test/posix-signal.c |
| kill | pid==0 / pid<-1 (process groups) | N/A — no process-group concept on this platform (kill() treats pid==0 as self, any pid<0 as ESRCH; there is no group of >1 process to observe a difference against) | -- |
| kill | EPERM (differing real/effective uid) | N/A — not reliably triggerable under Wine without a second user | -- |
| killpg | BSD extension, `killpg(pg,sig) == kill(pg,sig)` verbatim | N/A (not POSIX.1-2017 base) | -- |
| sigaction | act==NULL queries without changing; EINVAL for SIGKILL/SIGSTOP | covered | test/posix-signal.c |
| sigaction | SA_RESETHAND: disposition reset to SIG_DFL on entry to the handler | covered — was a BUG (`sigaction()` only copied `sa_handler`); **fixed in 99474ee** via per-signal `act_flags[]` read by `__raise_internal()` | test/posix-signal.c `test_sa_resethand` |
| sigaction | implicit self-mask on entry (signal blocked against re-entering its own handler unless SA_NODEFER) | covered — same root cause; **fixed in 99474ee** (SA_NODEFER honoured) | test/posix-signal.c `test_sigaction_implicit_mask` |
| sigaction | sa_mask (blocking a *different* signal for the handler's duration) | covered — same root cause; **fixed in 99474ee** via per-signal `act_mask[]` applied around the handler call | test/posix-signal.c |
| sigaction | SA_RESTART | N/A — no blocking call is ever interrupted by an asynchronously-delivered signal on this platform, so there is nothing to restart | -- |
| sigemptyset / sigfillset / sigaddset / sigdelset / sigismember | return values, EINVAL for invalid signo | covered | test/posix-signal.c |
| sigprocmask | SIG_BLOCK/SIG_UNBLOCK/SIG_SETMASK semantics; EINVAL for bad `how`; set==NULL leaves mask unchanged; SIGKILL/SIGSTOP unblockable without error; blocked signal becomes pending, delivered on unblock | covered | test/misc.c, test/posix-signal.c |
| sigpending | empty when nothing blocked+raised; reflects a blocked+raised signal; clears once delivered | covered | test/posix-signal.c |
| sigsuspend | return value -1/EINTR (the only return this stub can produce) | covered | test/posix-signal.c |
| sigsuspend | DESCRIPTION: replace the mask, actually suspend until a signal is delivered | N/A — documented permanent stub (`{ errno = EINTR; return -1; }`); no per-thread wait primitive exists to build a real one on | -- |
| sigwait / sigtimedwait / sigqueue / sigaltstack | require per-process queued-signal-with-payload or alt-stack facilities this platform has none of | N/A — documented stubs (see include/signal.h) | -- |
| abort | never returns; overrides SIG_IGN and SIG_BLOCK; a caught SIGABRT whose handler returns normally still terminates | covered | test/misc.c, test/posix-signal.c |
| abort | "may" attempt fclose() on open streams | N/A — MAY, not SHALL | -- |
| strsignal | see the string.h table above (correction: base function, not XSI) | covered | test/string.c, test/posix-signal.c |
| psignal / psiginfo | not implemented anywhere in `src/`/`include/` | N/A (not implemented) | -- |
| wait | any child, blocks until one changes state | covered | test/misc.c, test/waitpid-overflow.c |
| waitpid | pid==-1/0 (any child — one implicit process group here), pid>0 (exactly that child), WNOHANG, ECHILD | covered | test/waitpid-overflow.c, test/posix-signal.c |
| waitpid | EINVAL for an invalid `options` value | covered — was a BUG (no validation of the other bits); **fixed in 99474ee**: rejects anything outside `WNOHANG\|WUNTRACED\|WCONTINUED`, uniformly for wait/waitpid/wait3/wait4 | test/posix-signal.c `test_waitpid_einval_options` |
| waitpid | EINTR (signal caught while waiting) | N/A — no asynchronous delivery exists to interrupt a blocking wait | -- |
| wait3 / wait4 | BSD/historical, not POSIX.1-2017 base | N/A (not POSIX.1-2017 base) | test/posix-signal.c (sanity only) |
| WIFEXITED / WEXITSTATUS / WIFSIGNALED / WTERMSIG | correct and mutually exclusive across all 256 exit codes and signal deaths | covered | test/waitpid-overflow.c, test/posix-signal.c |
| WCOREDUMP | set for core-dumping signals, clear otherwise; not a POSIX.1-2017 base macro | covered; N/A as a base requirement (BSD/glibc extension) | test/waitpid-overflow.c, test/posix-signal.c |
| WIFSTOPPED / WSTOPSIG | never true / nothing to decode (no job control on this platform) | covered / N/A | test/posix-signal.c |
| 0xE0DE00xx signal-death encoding never collides with a real exit code | covered — the exit-codes-129-192 regression this ledger's git history mentions (commit 607c289) is specifically re-checked | test/posix-signal.c |

All three of these bugs have since been fixed (commit 99474ee) and their
assertions un-fenced unmodified. `src/signal/signal.c` now keeps
per-signal `act_mask[]`/`act_flags[]` that `__raise_internal()` applies
around the handler call, making SA_RESETHAND, SA_NODEFER, the implicit
self-mask and `sa_mask` all live -- these are exactly the clauses that
can mean anything given synchronous-only delivery. `SA_RESTART`,
`SA_ONSTACK`, `SA_SIGINFO`, `SA_NOCLDSTOP`, `SA_NOCLDWAIT` are stored so
they round-trip through the old-disposition output, but are documented
no-ops rather than silently dropped. `src/process/wait.c` now validates
`options` for wait/waitpid/wait3/wait4.

### Not reached (signal.h group)

`psiginfo()` (not implemented); `SA_SIGINFO`/`sa_sigaction`/`siginfo_t`
delivery (same root cause as the `sigaction()` bugs — `sa_sigaction`
is never read either, not separately fenced-tested); real
hardware-fault signals (SIGSEGV/SIGFPE/SIGILL/SIGBUS via the vectored
exception handler) — deliberately not provoked on purpose.

## wchar.h / multibyte conversions (priority 8)

New clause-cited audit: `test/posix-wchar.c`. Sanity coverage (uncited)
still lives in `test/string.c`.

`wchar_t` here is a **16-bit unsigned UTF-16 code unit** (`WCHAR_MAX ==
0xffff`), not the 32-bit type most POSIX text assumes; a code point
above U+FFFF is a surrogate pair. This causes a few documented,
deliberate divergences (not bugs — matching the letter of a clause
written for a 32-bit `wchar_t` is not achievable without changing the
ABI), listed below.

| function | clause checked | status | test |
|---|---|---|---|
| wcslen / wcscpy / wcsncpy / wcscat / wcsncat / wcscmp / wcsncmp / wcschr / wcsrchr | copy/compare/search semantics, NUL handling | covered | test/posix-wchar.c |
| wcsnlen / wcpcpy / wcpncpy / wcscoll / wcsxfrm / wcscasecmp / wcsncasecmp / wcspbrk / wcscspn / wcsspn / wcsstr / wcstok / wcsdup / wcsftime / wcswidth / wcwidth / wcstol family / mbsnrtowcs / wcsnrtombs | not implemented by this libc's wchar.h at all | N/A (missing from this libc) | -- |
| wmemchr / wmemcmp / wmemcpy / wmemmove / wmemset | n-bounded semantics, n==0 edge cases, overlap-safety | covered | test/posix-wchar.c |
| mbsinit | true if ps NULL or initial state | covered | test/posix-wchar.c |
| mbrtowc | 0/1..n/-1(EILSEQ)/-2(incomplete); overlong+surrogate+>U+10FFFF rejected; s==NULL behaves as mbrtowc(NULL,"",1,ps); errno unchanged on success | covered, plus a documented divergence (surrogate-pair delivery uses an undocumented `-3` return not in POSIX's 0/1..n/-1/-2 contract) | test/posix-wchar.c |
| wcrtomb | byte count incl. shift seq.; NUL -> 1 byte; s==NULL equivalent form; errno unchanged on success; EILSEQ for invalid wc | covered, plus a documented divergence (a lone high surrogate is stashed rather than EILSEQ'd, mirroring mbrtowc) | test/posix-wchar.c |
| mbrlen | equivalent to mbrtowc(NULL,...) | covered (sanity) | test/posix-wchar.c |
| mbtowc / wctomb / mblen | s==NULL reset behaviour, byte-count/-1(EILSEQ) results, <= MB_CUR_MAX | covered | test/posix-wchar.c |
| mbstowcs / wcstombs | conversion counts, never splitting a character, pwcs/s==NULL -> length only | covered | test/posix-wchar.c |
| mbsrtowcs / wcsrtombs | conversion via mbrtowc()/wcrtomb(); \*src set correctly; dst==NULL -> length only | covered | test/posix-wchar.c |
| btowc | WEOF for EOF or an invalid one-byte char in the initial shift state | covered, plus a documented divergence: under UTF-8, bytes 0x80-0xFF are never a complete one-byte character, so `btowc()` correctly returns WEOF for them even though the POSIX-locale clause's literal text (written for a single-byte-identity encoding) says it shouldn't | test/posix-wchar.c |
| wctob | EOF unless c has a length-1 representation in the initial shift state | covered | test/posix-wchar.c |
| `<wctype.h>` (isw*/tow*/wctype/wctrans) | header does not exist in this library at all | N/A (whole header missing) | -- |
| wcstoimax / wcstoumax | equivalent to the wcstol/wcstoll/wcstoul/wcstoull family; overflow/EINVAL/base-0 auto-detection | covered | test/posix-wchar.c |

No bugs found this session (the two divergences above are deliberate
design choices documented in `src/stdlib/mbrtowc.c`, not spec
violations to fence).

### Not reached (wchar.h)

Every function listed "not implemented" above — confirmed by grepping
`include/` and `src/string/`/`src/stdlib/` rather than assumed. If any
get implemented later, audit them here.

## math.h (priority 9)

New clause-cited audit: `test/posix-math.c`. Existing ad-hoc coverage:
`test/math.c`. Functions implemented and audited: `fabs`,
`floor`/`ceil`/`trunc`/`round`, `sqrt`, `fmod`, `frexp`/`ldexp`/
`scalbn`, `modf`, `copysign`, `exp`, `log`/`log2`/`log10`, `sin`/`cos`/
`tan`/`atan`/`atan2`, `pow`, `fmax`/`fmin`, `hypot`, `nan`,
`fpclassify`/`isnan`/`isinf`/`isfinite`/`isnormal`/`signbit`. Not
implemented by `src/math/` at all (no coverage needed): `asin`/`acos`,
`sinh`/`cosh`/`tanh` and inverses, `cbrt`, `expm1`/`log1p`, `erf`/
`erfc`, `lgamma`/`tgamma`, Bessel functions, `remainder`/`remquo`,
`nextafter`/`nexttoward`, `fdim`, `fma`, `ilogb`/`logb`, `nearbyint`,
`scalbln`.

This session also added `include/fenv.h` + `src/math/fenv.c` (a full
C99 `<fenv.h>` against real x87/MXCSR hardware exception flags on both
arches) and fixed both bugs the previous audit found -- see the two
table rows below.

| function | clause checked | status | test |
|---|---|---|---|
| fabs / copysign | RETURN VALUE special-value tables | covered | test/posix-math.c |
| fpclassify / isnan / isinf / isfinite / isnormal / signbit | classification of each of the 5 categories, both signs | covered | test/math.c, test/posix-math.c |
| floor / ceil / trunc / round | NaN/±0/±Inf passthrough, sign-of-zero-result rule | covered | test/math.c, test/posix-math.c |
| sqrt | NaN->NaN, ±0->x, +Inf->x, negative finite/−Inf -> domain-error NaN | covered | test/math.c, test/posix-math.c |
| fmod | full sign/special-value table | covered | test/math.c, test/posix-math.c |
| frexp / ldexp / scalbn / modf | special-value tables, overflow/underflow sign | covered | test/math.c, test/posix-math.c |
| exp / log / log2 / log10 | special-value tables, pole/domain errors | covered | test/math.c, test/posix-math.c |
| sin / cos / tan / atan / atan2 | special-value tables (atan2's full ~13-clause quadrant table) | covered | test/math.c, test/posix-math.c |
| pow | full ~20-clause special-value table | covered | test/math.c (~10 sampled), test/posix-math.c (remaining ~14) |
| fmax / fmin | one-NaN-arg returns the other, both-NaN -> NaN | covered | test/math.c, test/posix-math.c |
| hypot | ±Inf wins even over a NaN co-argument; NaN with non-Inf co-arg -> NaN; overflow -> HUGE_VAL | **covered (bug fixed)** — `src/math/hypot.c` used to special-case only a NaN argument, so `hypot(Inf,Inf)` fell through to `Inf/Inf=NaN` and returned NaN instead of +Inf; fixed by moving the infinity check ahead of the NaN check (an infinity outranks a NaN per the spec's stated precedence), governing both cases with one unconditional check | test/math.c (Inf-beats-NaN case), test/posix-math.c (`test_hypot`, all cases including both-Inf, unfenced and passing) |
| nan | "a quiet NaN, if available" | covered | test/math.c, test/posix-math.c |
| math_errhandling / MATH_ERRNO / MATH_ERREXCEPT | required macro values; the conditional `<fenv.h>` requirement | **covered (bug fixed)** — `include/math.h` unconditionally defines `math_errhandling` as `MATH_ERREXCEPT` (2), which per basedefs/math.h.html obligated the implementation to provide `<fenv.h>`'s `FE_DIVBYZERO`/`FE_INVALID`/`FE_OVERFLOW`, but `include/fenv.h` did not exist. Fixed by adding a real `include/fenv.h` + `src/math/fenv.c`: the full C99 set (`feclearexcept`/`feraiseexcept`/`fetestexcept`/`fe{get,set}exceptflag`/`fe{get,set}round`/`fe{get,set}env`/`feholdexcept`/`feupdateenv`) against real hardware, aggregating the x87 status word and (on x86_64 only, `#ifndef __i386__`) MXCSR, since tcc/i386 compiles `double` arithmetic to x87 and tcc/x86_64 to SSE2 (confirmed by disassembling a trivial `double` function under both cross tcc targets) while `src/math/x87.h`'s helpers are x87 on both arches | test/posix-math.c `test_errhandling` (real hardware exceptions from both `src/math/*.c` helpers and plain compiler-emitted arithmetic, plus round-trips of every new function) |

### Not reached (math.h)

The `l`/`f` variants of the trig functions beyond spot checks;
`lround`/`llround`/`lrint`/`llrint`/`rint` (implemented, basic behaviour
covered by test/math.c, not independently re-audited clause-by-clause);
no fuzzing/property-based cross-check against glibc beyond test/math.c's
existing `NEAR()` spot checks.

## limits.h / float.h / stdint.h / inttypes.h (priority 10)

None of these four headers, nor the inttypes.h functions
strtoimax/strtoumax/imaxabs/imaxdiv, had been audited before this
session. New clause-cited audit: `test/posix-limits.c` (225 `CHECK()`
assertions, plus two `#if`-only checks on `UINT64_C`/`INTMAX_C` that
fail the build itself if wrong). No pre-existing broad sanity file
covered this ground.

Two rules held throughout: where POSIX gives a floor/ceiling
("Minimum/Maximum Acceptable Value"), the assertion checks the
*direction* the spec states, not ntlibc's exact number — a
change-detector is not a conformance check. Where a value is
genuinely arch-dependent under this target's LLP64 model (`long`
32-bit, pointer-width types 32/64-bit by arch, `wchar_t` 16-bit
UTF-16 on both), the expected value is derived from
`sizeof()`/the type's own arithmetic rather than hardcoded, so one
assertion covers both arches.

| header | what's checked | status | test |
|---|---|---|---|
| limits.h | CHAR_BIT/SCHAR/UCHAR/CHAR exact values; SHRT/INT/LONG/LLONG *_MAX/*_MIN and U*_MAX floors (direction only); MB_LEN_MAX, WORD_BIT, LONG_BIT floors | covered | test/posix-limits.c (`test_limits_numerical`) |
| limits.h | internal consistency: UINT_MAX==(unsigned)-1, *_MIN<=-*_MAX, sizeof(type)\*CHAR_BIT matching *_MAX's actual bit width (int, long long unconditionally; long guarded on __SIZEOF_LONG__==4) | covered | test/posix-limits.c (`test_limits_consistency`) |
| limits.h | SSIZE_MAX == the true max of `ssize_t` (not just >= the POSIX floor) | **covered (bug found and fixed)** | test/posix-limits.c (`test_limits_consistency`) |
| limits.h | NAME_MAX/PATH_MAX/PIPE_BUF/SYMLOOP_MAX/NGROUPS_MAX/OPEN_MAX/ARG_MAX/TZNAME_MAX/TTY_NAME_MAX/HOST_NAME_MAX/FILESIZEBITS/IOV_MAX vs their `_POSIX_*`/spec floors | covered | test/posix-limits.c (`test_limits_pathname`) |
| limits.h | `_POSIX_*`/`_POSIX2_*` "Minimum Values" table: representative cross-section (19 macros) asserted equal to the spec's literal floor; the remaining ~15 were diffed by hand against the fetched spec table and all match exactly, not each re-asserted to avoid a wall of identical-shaped CHECKs | covered | test/posix-limits.c (`test_limits_posix_floors`) |
| limits.h | `ATEXIT_MAX`, `CHILD_MAX` | N/A (spec-conformant omission) | Runtime Invariant Values "may be omitted if [the value] is indeterminate"; `CHILD_MAX` is reported only via `sysconf(_SC_CHILD_MAX)` (`src/unistd/sysconf.c`, out of this header audit's scope) |
| float.h | FLT_RADIX, FLT_ROUNDS, FLT_EVAL_METHOD domain | covered | test/posix-limits.c (`test_float_radix_and_rounds`) |
| float.h | FLT_EVAL_METHOD's *correct* value for this compiler's actual codegen | not testable portably | needs disassembly, not a documented compiler macro; asserted only to be one of the four defined values |
| float.h | FLT_DIG/MANT_DIG/MIN_10_EXP/MAX_10_EXP/MAX/MIN/EPSILON floors; the EPSILON defining property (`1+eps != 1`, `1+eps/2 == 1`) | covered, FLT and DBL | test/posix-limits.c (`test_float_flt`, `test_float_dbl`) |
| float.h | DECIMAL_DIG floor and >= DBL_DIG | covered | test/posix-limits.c (`test_float_dbl`) |
| float.h | LDBL_* floors and EPSILON property, for whichever of the two `long double` layouts this compiler actually built (80-bit x87 extended vs. an 8-byte alias for `double`) | **covered (bug found and fixed)** | test/posix-limits.c (`test_float_ldbl`) |
| stdint.h | int8/16/32/64_t, uint8/16/32/64_t: exact sizeof, no padding, two's complement (*_MIN/*_MAX exact, (u)-1==*_MAX) | covered | test/posix-limits.c (`test_stdint_exact_width`) |
| stdint.h | int_least\*/int_fast\* families: magnitude floor vs the exact-width type, and the *_MAX macro matching its own type's real range | covered | test/posix-limits.c (`test_stdint_least_fast`) |
| stdint.h | intmax_t/uintmax_t: floor, two's-complement identity, >= long long | covered | test/posix-limits.c (`test_stdint_max`) |
| stdint.h | intptr_t/uintptr_t/ptrdiff_t/size_t: pointer-width split (LLP64), each *_MAX/*_MIN derived from `sizeof(intptr_t)`/`sizeof(ptrdiff_t)` rather than hardcoded, plus the POSIX stdint.h floors (65535) | covered | test/posix-limits.c (`test_stdint_pointer_width`) |
| stdint.h | wchar_t: 16-bit UTF-16 (not the 32-bit-on-Linux convention), WCHAR_MIN/MAX derived from `sizeof(wchar_t)`/`(wchar_t)-1`, not hardcoded to either signedness convention | covered | test/posix-limits.c (`test_stdint_wchar`) |
| stdint.h | wint_t (WINT_MIN/MAX), sig_atomic_t (SIG_ATOMIC_MIN/MAX) floors + exact-width identity | covered | test/posix-limits.c (`test_stdint_wchar`) |
| stdint.h | INTN_C/UINTN_C/INTMAX_C/UINTMAX_C: `#if`-usability (literal `#error` guards) and runtime value/width | covered | test/posix-limits.c (`test_stdint_c_macros`) |
| inttypes.h | PRId/i/o/u/x/X8/16/32/64, SCNd/i/o/u/x8/16/32/64: round-tripped through real `sprintf`/`sscanf`, not just compiled | covered | test/posix-limits.c (`test_inttypes_pri_fixed`, `test_inttypes_scn_fixed`) |
| inttypes.h | PRI/SCN LEAST64/FAST64/FAST32 families | covered (representative; the length-modifier machinery is shared by width class, see `__PRI64` in include/inttypes.h, so these exercise every distinct modifier the header emits) | test/posix-limits.c (`test_inttypes_pri_scn_least_fast`) |
| inttypes.h | PRI/SCN dMAX/uMAX, PRI/SCN d/u/xPTR — the LLP64 pointer-width family, round-tripped at the arch's actual `INTPTR_MAX`/`INTPTR_MIN`/`UINTPTR_MAX` | covered | test/posix-limits.c (`test_inttypes_pri_scn_max_ptr`) |
| inttypes.h | imaxdiv_t struct, wcstoimax/wcstoumax declarations | not independently re-audited | `wcstoimax`/`wcstoumax` live in `src/stdlib/wcstoimax.c`, out of this audit's scope; `imaxdiv_t`'s layout is exercised structurally by every `imaxdiv()` call in `test_imaxdiv` |

### strtoimax / strtoumax / imaxabs / imaxdiv

Implementation: `src/stdlib/strtol.c` (strtoimax/strtoumax share the
`strtox()`/`parse()` machinery with strtol/strtoul/strtoll/strtoull),
`src/stdlib/abs.c` (imaxabs), `src/stdlib/div.c` (imaxdiv).

| function | clause checked | status | test |
|---|---|---|---|
| strtoimax/strtoumax | RETURN VALUE/ERRORS: ERANGE + saturation at INTMAX_MAX/INTMAX_MIN/UINTMAX_MAX on overflow; clean conversion at the exact boundary values; EINVAL (required "shall fail", not "may fail") for an unsupported base, with endptr==nptr; no-conversion endptr==nptr; base-0 prefix auto-detection; errno unchanged on a clean conversion | covered | test/posix-limits.c (`test_strtoimax_errors`) |
| imaxabs | RETURN VALUE: absolute value, for 0/positive/negative/INTMAX_MAX | covered | test/posix-limits.c (`test_imaxabs`) |
| imaxabs | `imaxabs(INTMAX_MIN)` | deliberately not tested | DESCRIPTION: "If the result cannot be represented, the behavior is undefined" — `-INTMAX_MIN` overflows `intmax_t`, so any expected value the test could name would just be asserting the UB itself |
| imaxdiv | RETURN VALUE: `quot*denom + rem == numer` and truncation toward zero, for all four sign combinations plus a small table including near-INTMAX_MAX/MIN operands | covered | test/posix-limits.c (`test_imaxdiv`) |

### Bugs found (limits.h / float.h)

Both were found by deriving expected values from `sizeof()`/the
actual type's arithmetic rather than trusting the header's own
number — the LLP64 split. Both are header-value fixes (no ABI/public
API change), fixed and merged with commit `83cad79`.

1. **`SSIZE_MAX` used the 32-bit `LONG_MAX` on x86_64, where
   `ssize_t` is actually a 64-bit `long long`.** `include/limits.h`
   had `#define SSIZE_MAX LONG_MAX`. `ssize_t` is typedef'd from
   `_Addr` (`arch/*/bits/alltypes.h.in`): `int` (32-bit) on i386,
   `long long` (64-bit) on x86_64 — it tracks pointer width, not
   `long`, despite the name. `long` stays 32-bit on both arches under
   this target's LLP64 model, so on i386 `SSIZE_MAX == LONG_MAX`
   happened to be numerically correct, masking the bug there. On
   x86_64, `SSIZE_MAX` was silently capped at 2^31-1 despite
   `ssize_t` actually holding 64 bits — a real conformance defect
   against `limits.h.html`'s "SSIZE_MAX: Maximum value for an object
   of type ssize_t." **Fixed** (commit `83cad79`): moved `SSIZE_MAX`
   out of the generic `include/limits.h` into
   `arch/i386/bits/limits.h` (`0x7fffffff`, unchanged value) and
   `arch/x86_64/bits/limits.h` (new: `0x7fffffffffffffffLL`).
   Regression test: `test_limits_consistency`'s `SSIZE_MAX` check in
   test/posix-limits.c, which derives the expected value from
   `sizeof(ssize_t)` so it covers both arches from one assertion.

2. **`LDBL_MANT_DIG`/`LDBL_MAX`/`LDBL_EPSILON`/etc. unconditionally
   described the 80-bit x87 extended format, even for this tcc's PE
   targets where `long double` is really just an 8-byte `double`.**
   `arch/i386/bits/float.h` and `arch/x86_64/bits/float.h` both had a
   single, unconditional block: `LDBL_MANT_DIG 64`,
   `LDBL_MAX 1.1897...e+4932L`, etc. `src/math/x87.h`'s
   `NTLIBC_LDBL_EXTENDED` macro already documents that this exact tcc
   build gives `long double` no 80-bit range or precision at all
   (`sizeof(long double) == 8`, `__SIZEOF_LONG_DOUBLE__` undefined),
   while the mingw-w64/gcc fallback compiler, and the native
   gcc/clang used for `make asan`, give it the genuine 80-bit format.
   Before the fix, a tcc-built PE binary's `float.h` claimed
   `LDBL_MAX ~= 1.19e+4932L`, unrepresentable in an 8-byte `double`,
   and `LDBL_EPSILON` implying 64 bits of mantissa precision an
   8-byte object does not have. **Fixed** (commit `83cad79`): gated
   both arch headers' LDBL_* block on
   `defined(__SIZEOF_LONG_DOUBLE__) && __SIZEOF_LONG_DOUBLE__ > 8` —
   the identical test `NTLIBC_LDBL_EXTENDED` already uses — keeping
   the 80-bit values for that branch and adding a new branch that
   mirrors `DBL_*` exactly for the tcc/NT-target branch. Regression
   test: `test_float_ldbl` in test/posix-limits.c, written entirely
   in terms of `sizeof(long double)`/`DBL_MANT_DIG` so it holds under
   either branch rather than asserting one specific width.

No other divergence was found between i386 and x86_64 in these four
headers: `intptr_t`/`uintptr_t`/`ptrdiff_t`/`size_t`/`WCHAR_MIN`/
`WCHAR_MAX` are already correctly split per-arch (via `_Addr` in
`arch/*/bits/alltypes.h.in` and `arch/*/bits/stdint.h`), and
`inttypes.h`'s `__PRI64`/`__PRIPTR` selection (`include/inttypes.h`)
was independently checked against those same typedefs and found
correct as-is.

### Not reached (limits.h / float.h / stdint.h / inttypes.h)

`wcstoimax`/`wcstoumax` (implemented in `src/stdlib/wcstoimax.c`, out
of this audit's file scope); the non-representative subset of
LEAST/FAST PRI/SCN combinations beyond LEAST64/FAST64/FAST32.

## Memory allocation, process termination, and environment (priority 11)

Clause-by-clause POSIX.1-2017 audit of `src/malloc/`, `src/exit/` and
`src/env/`. New audit: `test/posix-alloc.c` (48 `CHECK()`
assertions). Pre-existing coverage checked against the spec rather
than duplicated: `test/malloc.c` (extensive alignment/aliasing/
ASan-ENOMEM coverage of malloc/calloc/realloc/free/posix_memalign/
aligned_alloc/memalign/valloc/reallocarray/malloc_usable_size) and
`test/misc.c` (getenv/setenv/unsetenv/putenv basics, `system()`, and
one atexit-LIFO + abort/assert/exit child-spawn sequence).

The atexit/abort/exit-vs-_Exit clauses need a child process observed
from the parent (fork() needs `RtlCloneUserProcess`, absent under
stock Wine); `test/posix-alloc.c` re-execs itself via `__spawn()`,
same pattern as `test/misc.c`'s `test_abort_child()`.

| function | clause checked | status | test |
|---|---|---|---|
| malloc | RETURN VALUE: size==0 -> NULL or a usable/freeable pointer (implementation-defined, either permitted) | covered | test/malloc.c (basic), test/posix-alloc.c (two simultaneously-live malloc(0) results must not alias if both non-null) |
| malloc | ERRORS: ENOMEM on failure | covered | test/malloc.c |
| calloc | DESCRIPTION: "the space shall be initialized to all bits 0" | covered | test/malloc.c (small), test/posix-alloc.c (4 MiB allocation, deliberately dirtied via a same-size malloc+memset+free beforehand to rule out "happened to start zero") |
| calloc | nelem\*elsize overflow must be detected (NULL+ENOMEM), not silently wrapped into an under-sized allocation | covered | test/malloc.c (SIZE_MAX/2\*4, SIZE_MAX\*SIZE_MAX), test/posix-alloc.c (exact wraparound boundaries: product wraps to 0, product wraps to 1, product==SIZE_MAX exactly via both m=SIZE_MAX,n=1 and m=1,n=SIZE_MAX, n==0 never treated as overflow, and a genuinely satisfiable 1000\*1000 must NOT be rejected) |
| realloc | RETURN VALUE: size==0 -> NULL or a usable/freeable pointer (implementation-defined, either permitted); this implementation's actual choice recorded as information, not asserted as a preference | covered | test/malloc.c, test/posix-alloc.c |
| realloc | DESCRIPTION: "contents ... shall remain unchanged up to the lesser of the new and old sizes", both growing and shrinking | covered | test/malloc.c (basic), test/posix-alloc.c (32->4096 grow preserves all 32 original bytes; 4096->10 shrink preserves the surviving 10-byte prefix) |
| realloc | RETURN VALUE: "the memory referenced by ptr shall not be changed" if realloc() fails with ENOMEM | covered (best-effort) | test/posix-alloc.c (near-SIZE_MAX request; original content re-verified when the call does report failure; when the huge address space actually satisfies it, the successful path's content is checked instead — ENOMEM is not reliably forceable under Wine) |
| realloc(NULL, n) | DESCRIPTION: "equivalent to malloc(size)" | covered elsewhere | test/malloc.c |
| free | DESCRIPTION / no return value | covered elsewhere | test/malloc.c |
| posix_memalign | ERRORS: EINVAL for a non-power-of-two-multiple-of-sizeof(void\*) alignment | covered elsewhere | test/malloc.c |
| posix_memalign | boundary case: alignment == sizeof(void\*) itself (the smallest legal value) succeeds | covered | test/posix-alloc.c |
| aligned_alloc / memalign / valloc / reallocarray / malloc_usable_size | out of scope beyond what's cited above; already exercised in `test/malloc.c` | covered elsewhere | test/malloc.c |
| exit | DESCRIPTION: calls every atexit()-registered function "in the reverse order of their registration" | covered | test/misc.c (h1/h2/h3, LIFO order of 3, in-process), test/posix-alloc.c (LIFO order of 40 handlers, also in-process — POSIX floor is 32, so 40 exercises past-the-minimum registration) |
| exit | atexit handlers run "at normal program termination" via exit() | covered | test/misc.c (exit(23) propagates through waitpid), test/posix-alloc.c (exit-code-only child check: the registered handler force-terminates with a distinctive code (99) if it runs at all, so the parent's wait status alone says whether it ran) |
| _Exit / _exit | "_exit() and _Exit() ... do not call functions registered with atexit()" | covered | test/posix-alloc.c (same exit-code-only check, contrasted directly against the exit() case in the same test) |
| atexit | DESCRIPTION: "at least 32 functions can be registered" | covered | test/posix-alloc.c (40 registrations in a child, every one required to return 0, all 40 confirmed to fire in exact reverse order via the log file) |
| atexit | RETURN VALUE: 0 on success / non-zero on failure | covered (success path only; ATEXIT_MAX==128 in `src/exit/exit.c`, so failing the call would need 128 registrations — not attempted, out of scope beyond the required-minimum-32 clause) | test/posix-alloc.c |
| abort | DESCRIPTION: "abnormal process termination", SIGABRT via raise() | covered | test/misc.c (child dies, exit code nonzero or signalled), test/posix-alloc.c (`check_died_abnormally()`: `WIFSIGNALED(status)` required, and `WTERMSIG(status)==SIGABRT` additionally required whenever the status did come back signalled; skipped rather than falsely failed under the native asan build, where the real host wait4() truncates the wait-status encoding before ntlibc's decoder sees it, same caveat as test/waitpid-overflow.c and test/posix-signal.c) |
| abort | DESCRIPTION: "shall override blocking or ignoring the SIGABRT signal" | covered | test/posix-alloc.c (child installs SIG_IGN for SIGABRT, still dies abnormally; separate child blocks SIGABRT via sigprocmask, still dies abnormally — both via `check_died_abnormally()`) |
| abort | not reachable through atexit — an atexit-registered handler must not run when abort() (rather than exit()) ends the process | covered | test/posix-alloc.c (the shared force-exit(99) handler's code absent from the wait status of both the SIG_IGN and the blocked-signal abort children) |
| assert | DESCRIPTION: false expression -> diagnostic to stderr, then abort() | covered | test/misc.c (child does not report success; noise expected on stderr), test/posix-alloc.c (`check_died_abnormally()` on the assert-triggering child, same contract as abort() itself) |
| getenv | RETURN VALUE: pointer to value, or NULL if not found | covered elsewhere | test/misc.c, test/posix-stdlib.c |
| setenv | ERRORS: EINVAL for empty name or a name containing '=' | covered elsewhere | test/misc.c, test/posix-stdlib.c |
| setenv | DESCRIPTION: overwrite==0 leaves an *existing* value unchanged, but must still create a variable that does not exist yet (the two cases are distinct; only "existing" was isolated elsewhere) | covered | test/posix-alloc.c |
| setenv | DESCRIPTION: "shall...copy" name and value (contrasted directly against putenv's aliasing, in the same test, on the same buffer) | covered | test/posix-alloc.c |
| unsetenv | RETURN VALUE 0 on success incl. a no-op removal of a missing name; ERRORS EINVAL for empty/'=' name | covered elsewhere | test/misc.c, test/posix-stdlib.c |
| putenv | DESCRIPTION: "the string ... shall become part of the environment, so altering the string shall change the environment" (aliased, not copied) | covered elsewhere (basic case) | test/misc.c; test/posix-alloc.c adds the direct copy-vs-alias contrast on one shared buffer |
| environ | reflects setenv() additions and unsetenv() removals when walked directly (not just through getenv()) | covered | test/posix-alloc.c |
| environ inheritance across a spawn | not a POSIX.1-2017 clause by itself (environment inheritance across exec is `exec.html`'s domain), but a real NT integration point: the environment block `__spawn` hands the child is UTF-16 and rebuilt at spawn time (src/process/spawn.c) | covered (information, not a spec clause) | test/posix-alloc.c — observed: a setenv()-modified environment IS inherited by a spawned child (child exits 44, meaning it saw the parent's `setenv()`ed variable) |

### Bugs found (malloc / exit / env)

None. Every clause checked against `malloc.html`, `calloc.html`,
`realloc.html`, `posix_memalign.html`, `exit.html`, `atexit.html`,
`abort.html`, `assert.html`, `getenv.html`, `setenv.html`,
`unsetenv.html`, and `putenv.html` matched ntlibc's `src/malloc/`,
`src/exit/`, and `src/env/` implementation, including the
security-sensitive `calloc` overflow check
(`if (n && m > (size_t)-1 / n)` in `src/malloc/malloc.c`), which is
correct at every wraparound boundary tested.

### Observed behaviour where POSIX permits latitude

- `malloc(0)`: returns a unique, non-NULL, freeable pointer (backed
  by `RtlAllocateHeap`, which is documented to do this for a 0-byte
  request). Two live `malloc(0)` results are distinct pointers.
- `realloc(p, 0)`: returns a non-NULL pointer (`RtlReAllocateHeap`
  with size 0 behaves the same way malloc(0) does on this heap),
  freeable. Neither result is asserted as *the* correct answer — both
  are within the POSIX-permitted set.
- `atexit` runs at least 40 handlers in exact reverse order (the
  POSIX floor is 32).
- `abort`/`assert` override both `SIG_IGN` and a blocked `SIGABRT`.
- A `setenv()`-modified environment **is** inherited by a spawned
  child.

### Not reached (malloc / exit / env)

`atexit()`'s own failure return (registering past `ATEXIT_MAX`==128
in `src/exit/exit.c` — the POSIX-mandated clause is only the
*minimum* of 32 successful registrations, which is covered);
`_SC_ATEXIT_MAX` via `sysconf()` (`src/unistd/sysconf.c` does not
implement it and falls through to `EINVAL`, which POSIX permits);
`realloc()`'s ENOMEM-leaves-ptr-untouched clause could not be forced
down the failure path reliably under Wine; `quick_exit`/
`at_quick_exit` (implemented in `src/exit/exit.c` but not exercised
by any test in the tree, same gap already noted in the stdlib.h
section above); the signal-catching form of `abort()`'s "unless
SIGABRT is being caught and the handler does not return" clause (a
real handler that `longjmp()`s out of `abort()`) — only the
ignore-and-block override paths were exercised.
