<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# time.h -- POSIX.1-2017 coverage fragment

Priority group 3 (`time.h` calendar and clock functions). Method and
status vocabulary: see `test/POSIX-COVERAGE.md`. This fragment is
merged into that ledger by the coordinator; do not edit the top-level
file directly from here.

Existing ad-hoc coverage: `test/time.c` (broad, known-epoch table
covering `gmtime`/`localtime`/`mktime`/`timegm` round-trips across
leap years/centuries/negative time_t, `strftime` conversion-by-
conversion, `strptime` round-trips, `asctime`/`ctime` literal strings,
`difftime`, TZ parsing including named/quoted/DST-suffix forms,
`getdate`, and `clock_gettime`/`clock`/`clock_getres`/`stime`/
`gettimeofday`/`settimeofday`/`getrlimit`/`getrusage` sanity). New
clause-cited audit: `test/posix-time.c` (this session), covering what
`test/time.c` and `test/posix-parse.c` (which already clause-cites
`mktime.html` normalization and `strftime.html`'s short-buffer
RETURN VALUE) did not reach.

`src/time/` reviewed in full: `time.c`, `gettimeofday.c`, `stime.c`,
`clock.c`, `clock_gettime.c` (also `clock_settime`/`clock_getres`/
`clock_getcpuclockid`), `clock_nanosleep.c`, `timespec_get.c`,
`gmtime.c`, `localtime.c`, `mktime.c`, `timegm.c`, `asctime.c`,
`ctime.c`, `difftime.c`, `strftime.c`, `strptime.c`, `tzset.c`,
`names.c`, `getdate.c`, `time_impl.h` (the shared Hinnant civil-date
conversion, day/month name tables, `__num_digits`). `nanosleep()` is
**not** in `src/time/` -- it lives in `src/unistd/sleep.c` (owned by
whoever has `unistd.h`/process group); out of this session's scope,
not audited here.

| function | clause checked | status | test |
|---|---|---|---|
| time | RETURN VALUE: returns value, stores into *tloc if non-null | covered | test/time.c |
| time | ERRORS: EOVERFLOW if seconds since Epoch don't fit time_t | N/A (untriggerable: time_t is 64-bit here, real wall time never overflows it) | -- |
| difftime | RETURN VALUE: type is `double`, computes time_1 - time_0 | covered | test/time.c (numeric value across signs/magnitudes), test/posix-time.c (`sizeof(...)==sizeof(double)`, and a value only exactly representable as a double) |
| clock | DESCRIPTION: CPU time used by the process, not wall time; CLOCKS_PER_SEC==1000000 | covered | test/time.c (non-decreasing, CLOCKS_PER_SEC value), test/posix-time.c (CPU time barely advances across a wall-clock sleep, unlike busy work) |
| clock | RETURN VALUE: (clock_t)-1 on failure | N/A (no way to force ProcessTimes query failure under Wine) | -- |
| mktime | DESCRIPTION: out-of-range fields normalized; original tm_wday/tm_yday ignored on input, set on output; calls tzset() | covered | test/time.c + test/posix-parse.c (normalization across month/day/hour/sec overflow, both directions), test/posix-time.c (garbage tm_wday/tm_yday input provably ignored) |
| mktime | RETURN VALUE/ERRORS: (time_t)-1 + EOVERFLOW if the result can't be represented | **BUG (fenced)** | test/posix-time.c `test_mktime_overflow_returns_minus_one` (disabled) |
| timegm | not POSIX.1-2017 (BSD/glibc extension; not in the functions/ index) | N/A (not POSIX.1-2017) | test/time.c (extensive sanity + normalization, mirrors mktime's) |
| gmtime / gmtime_r | DESCRIPTION: seconds-since-Epoch -> broken-down UTC; RETURN VALUE: NULL on error | covered | test/time.c (known epochs), test/posix-time.c (EOVERFLOW path: `gmtime_r`/`gmtime` return NULL, errno set, *result left untouched on failure -- verified against src/time/gmtime.c's early-return-before-any-write structure) |
| localtime / localtime_r | (documented together with gmtime on the same page); local = UTC shifted by tzset()'s fixed offset | covered | test/time.c (known epochs + multiple TZ forms), test/posix-time.c (EOVERFLOW propagates through the `*tp - timezone` -> gmtime_r chain) |
| asctime / asctime_r | DESCRIPTION: exact 26-byte "Www Mmm dd hh:mm:ss yyyy\n\0" format; asctime_r needs >=26-byte buf; obsolescent in Issue 7 | covered | test/time.c (literal strings, strlen==25), test/posix-time.c (exactly-26-byte buffer boundary) |
| ctime / ctime_r | DESCRIPTION: equivalent to `asctime(localtime(clock))`; ERRORS: none defined | covered | test/time.c (literal strings under TZ=UTC0), test/posix-time.c (direct equivalence check under a non-UTC TZ so localtime's offset is actually exercised; NULL-propagation from localtime_r's EOVERFLOW) |
| strftime / strftime_l | RETURN VALUE: byte count excl. NUL, or 0 (contents unspecified) if it doesn't fit; DESCRIPTION: LC_TIME-driven (POSIX-locale only here) | covered | test/time.c (every implemented conversion), test/posix-parse.c (short-buffer 0-return boundary, exact-fit boundary, maxsize==0) |
| strftime | which conversions POSIX requires: %a %A %b %B %c %C %d %D %e %F %g %G %h %H %I %j %m %M %n %p %r %R %S %t %T %u %U %V %w %W %x %X %y %Y %z %Z %% | **gap, not fixed** | src/time/strftime.c's header comment documents `%U %W %V %G %g %s` as unimplemented (an unrecognized `%<letter>` passes through literally rather than erroring); test/time.c pins that pass-through behavior with a loose `OR` check. Implementing the ISO-8601 week-number family was judged out of scope for a from-scratch libc with no locale support beyond "C" (per the existing top-of-file rationale) -- flagged here rather than silently dropped. |
| strptime | RETURN VALUE: pointer past last character parsed, or NULL; DESCRIPTION: %y century pivot [69,99]->19xx, [00,68]->20xx | covered | test/time.c (round-trips through every implemented conversion incl. composites, explicit %y pivot values both sides of the boundary, garbage-rejected cases, leftover-input-not-an-error, %z, %p), test/posix-time.c (%% literal mid-format, multi-char whitespace run) |
| strptime | conversion table includes %C ("all but the last two digits of the year") | **BUG (fenced)** | test/posix-time.c `test_strptime_century` (disabled) -- no `case 'C'` in src/time/strptime.c's switch, so any format using %C returns NULL outright |
| strptime | conversion table also includes %U/%W (week numbers) | **gap, not fixed, not separately tested** | same missing-case shape as %C; not fenced separately to avoid duplicate bug reports for the same root cause (strptime.c implements a fixed subset and rejects everything else via `default: return NULL`) |
| tzset / tzname / daylight / timezone | DESCRIPTION: TZ parsing (name[+-]offset), tzname[0]/[1], daylight 0 iff no DST ever applies, timezone = UTC-minus-local-standard seconds | covered | test/time.c (unset/empty->UTC, EST5, IST-5:30, `<+03>-3`, DST-rule-suffix-ignored-but-base-offset-parsed), test/posix-time.c (plain name+DSTname suffix with no rule, still daylight==0) |
| nanosleep | in `src/unistd/sleep.c`, not `src/time/` | out of scope | not audited this session (belongs to unistd.h/process owner) |
| clock_nanosleep | ERRORS: EINVAL for bad nsec / unknown clock; DESCRIPTION: relative vs. TIMER_ABSTIME semantics, immediate return if target <= now | covered (relative, EINVAL, CLOCK_REALTIME abstime) | test/posix-time.c |
| clock_nanosleep | DESCRIPTION: TIMER_ABSTIME's absolute time is measured against clock_id's *own* reading | **BUG (fenced)** | test/posix-time.c `test_clock_nanosleep_monotonic_abstime` (disabled) -- src/time/clock_nanosleep.c's TIMER_ABSTIME branch always runs `req` through `__unix_to_nt()` (a unix-epoch-seconds -> NT-FILETIME conversion), correct for CLOCK_REALTIME but wrong for CLOCK_MONOTONIC (an arbitrary, non-1970 epoch): confirmed live, a 1-2s monotonic abstime request returns in microseconds instead of waiting |
| clock_gettime | RETURN VALUE/ERRORS: 0 or -1/EINVAL for unknown clock; EOVERFLOW if seconds don't fit | covered (EINVAL); EOVERFLOW N/A (64-bit time_t) | test/time.c (all clock IDs, EINVAL for unknown id, monotonic non-decreasing, cputime bounded) |
| clock_settime | ERRORS: EINVAL for unknown/unsettable clock id, EINVAL for tv_nsec outside [0,999999999], EPERM possible | partially covered | test/time.c (CLOCK_MONOTONIC -> EINVAL; CLOCK_REALTIME "set to now" accepting either success or a privilege-driven failure) |
| clock_settime | ERRORS: EINVAL for out-of-range tv_nsec | **BUG (reported, fenced, not exercised live)** | test/posix-time.c `test_clock_settime_bad_nsec` (disabled) -- src/time/clock_gettime.c's `clock_settime()` never validates `ts->tv_nsec`'s range before calling `NtSetSystemTime`; not run live because CLOCK_REALTIME's real syscall path depends on unpredictable process privilege under Wine (same caveat test/time.c already documents for stime()/clock_settime()) and would either perturb the host clock or mask the bug behind EPERM either way |
| clock_getres | DESCRIPTION: "If res is NULL, the clock resolution is not returned" (i.e. legal no-op, not an error); ERRORS: EINVAL for unknown clock | partially covered | test/time.c (REALTIME/MONOTONIC resolution, EINVAL for unknown id), test/posix-time.c (adds PROCESS_CPUTIME_ID/THREAD_CPUTIME_ID resolution) |
| clock_getres | NULL res must be accepted | **BUG (fenced, not exercised live)** | test/posix-time.c `test_clock_getres_null` (disabled) -- src/time/clock_gettime.c's `clock_getres()` dereferences `res` unconditionally in every branch; confirmed live that `clock_getres(CLOCK_REALTIME, NULL)` crashes the process (SIGSEGV, wine exit 11) rather than returning 0. Not run as a normal CHECK because a segfault takes down the whole test binary instead of failing one assertion. |
| clock_getcpuclockid | RETURN VALUE/ERRORS: 0 + clock id, or ESRCH if pid unknown | covered | test/time.c |
| timespec_get | C11 / POSIX.1-2024 (not POSIX.1-2017 base, per https://man7.org/linux/man-pages/man3/timespec_get.3.html); RETURN VALUE: nonzero base on success, 0 otherwise | covered | test/time.c (TIME_UTC success, unsupported base -> 0), test/posix-time.c (agrees with CLOCK_REALTIME within a few seconds) |
| getdate | DESCRIPTION: real getdate() reads $DATEMSK template file; RETURN VALUE: struct tm* or NULL + getdate_err | N/A (documented reimplementation, not the POSIX algorithm) | src/time/getdate.c's own header comment explains this is a pragmatic stand-in (fixed template list, ignores $DATEMSK, only sets getdate_err to 1 or 7 of the 8 POSIX values); test/time.c exercises the templates it does implement and both of those two error codes. Not re-litigated here since the deviation is already fully documented in the source and by design, not a bug. |

## Bugs found this session

All four below are fenced `#if 0 /* BUG: ... */` in `test/posix-time.c`,
confirmed live (compiled+run under Wine, not just read from source)
before fencing:

1. **`mktime()` swallows `EOVERFLOW`** (mktime.html RETURN VALUE/
   ERRORS). `src/time/mktime.c:38` calls `localtime_r(&t, tm)` and
   discards its return value; when the computed year overflows `int`
   (`tm_year` is `int`; `tm_mon/12` can push the effective year further
   than `tm_year` alone), `localtime_r`->`gmtime_r` correctly returns
   NULL with `errno=EOVERFLOW`, but `mktime()` still returns the raw,
   unrepresentable `time_t` instead of `(time_t)-1`. Confirmed live:
   `tm_year=INT_MAX, tm_mon=100` -> `mktime()` returns
   `67768036422969600` with `errno` left at `EOVERFLOW` (75) rather
   than returning `(time_t)-1`.
2. **`clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)` uses the
   wrong epoch** (clock_nanosleep.html DESCRIPTION). `src/time/
   clock_nanosleep.c`'s `TIMER_ABSTIME` branch always builds the
   absolute wait via `__unix_to_nt(req->tv_sec, req->tv_nsec)`, which
   assumes `req` is unix-epoch (1970) seconds -- true for
   `CLOCK_REALTIME` but not `CLOCK_MONOTONIC`, whose readings have an
   arbitrary (near-boot) epoch. Confirmed live: requesting a
   `CLOCK_MONOTONIC` abstime ~1-2s in the future returns in a few
   microseconds instead of sleeping.
3. **`strptime()` has no `%C` conversion** (strptime.html conversion
   table: "%C - All but the last two digits of the year {2}").
   `src/time/strptime.c`'s switch has no `case 'C'` and falls through
   to `default: return NULL`, so any format string using `%C` fails
   outright. (`%U`/`%W`, also in the table, are missing for the same
   reason -- not separately fenced, same root cause.)
4. **`clock_getres()` crashes on `res == NULL`** (clock_getres.html
   DESCRIPTION: "If res is NULL, the clock resolution is not
   returned" -- a legal no-op, not an error).
   `src/time/clock_gettime.c`'s `clock_getres()` writes through `res`
   unconditionally in every clock-id branch. Confirmed live:
   `clock_getres(CLOCK_REALTIME, NULL)` segfaults (wine exit code 11).
5. **`clock_settime()` doesn't validate `tv_nsec`'s range**
   (clock_settime.html ERRORS: EINVAL "if ... the nanosecond field is
   negative or greater than or equal to 1000 million"). `src/time/
   clock_gettime.c`'s `clock_settime()` only checks the clock id, never
   `ts->tv_nsec`, before calling `NtSetSystemTime`. Reported but *not*
   exercised live or even fenced as a runnable test, since triggering
   the `CLOCK_REALTIME` path depends on unpredictable process privilege
   under Wine and risks perturbing the host's real clock either way
   (same caveat as the pre-existing stime()/clock_settime() tests in
   test/time.c).

No `src/time/` changes were made: every gap found here is either a
genuine missing check (fenced per the "never edit an assertion to
match the implementation" rule) or a documented, intentional scope
limitation (week-number family, getdate's template-file
reimplementation). Nothing in `time_impl.h` needed extraction to
become testable -- everything checked this session was already
reachable through the public API.

## Where to resume in time.h

Essentially complete for the functions in scope. Loose ends for a
successor:
- The five fenced bugs above are unfixed; fixing `clock_settime`'s
  nsec validation and `clock_getres`'s NULL check are both
  low-risk/high-value (simple guard clauses, no ABI change).
  `clock_nanosleep`'s TIMER_ABSTIME/CLOCK_MONOTONIC epoch bug needs
  more care (it would need to convert via a monotonic-clock delta
  rather than `__unix_to_nt`). `mktime`'s EOVERFLOW propagation needs
  `mktime()` to check `localtime_r()`'s return.
- `nanosleep()` (src/unistd/sleep.c) was not audited -- it's outside
  `src/time/` and this session's ownership boundary.
- `getdate()`'s reimplementation gap (no $DATEMSK, no default-to-
  "today" for missing fields, only 2 of 8 getdate_err codes ever set)
  is fully intentional per its own header comment; not revisited.
- Move on to priority group 4 (`dirent.h`) next.
