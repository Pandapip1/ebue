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

**Status as of the priority-13 pass: nothing in the priority list is
"not yet reached" any more.** Every header and function group it named
now has a clause-cited row, an explicit N/A with a stated reason, or a
fenced BUG. What is left is per-section "Not reached" lists of
individual *clauses* that cannot be exercised on this platform. The two
BUGs recorded under priority 12/13 have since been fixed in commit
`694a098` and their tests un-fenced; the one thing left open there is
`chdir()`'s own [ENOTDIR] path-prefix gap, which the shared-layer fix
does not reach because `chdir()` does not use the shared layer (see BUG
1 under priority 12/13). A successor's job is therefore no longer "pick
up the next group" but one of: close an open BUG, re-audit against real
Windows rather than Wine, or audit something newly implemented. Two habits from this pass are worth keeping: check
the tests before trusting a row (several groups were audited in the
tree long before this file recorded it), and remember that coverage of
one caller of a shared layer is not coverage of the layer — see BUG 2
under priority 12/13 for what that cost, and for why `chdir()`'s
private copy of the length check was nevertheless kept.

Scope note: this file tracks functions ntlibc **implements**. POSIX
functions it lacks entirely are tracked separately, in
`test/POSIX-GAP-ACCOUNTING.md`.

Status values:
- **covered** — every testable DESCRIPTION/RETURN VALUE/ERRORS clause has
  an assertion, either pre-existing or added.
- **N/A (reason)** — not a POSIX.1-2017 base function (GNU/BSD
  extension), or the LEGACY variant removed in Issue 7, or the clause
  cannot be triggered/observed under Wine or without real hardware
  failure (e.g. malloc-exhaustion ENOMEM paths). That last case
  includes the one the fence vocabulary kept getting wrong: a clause
  whose **code exists** but whose **test fixture cannot be built here**
  is N/A, not UNIMPL. UNIMPL's contract is "absent, but implementable,
  and the fence names the NT mechanism"; a fence with nothing absent
  and no mechanism to name is not making that claim. Such an N/A is
  usually conditional rather than permanent, so it carries an expiry
  condition naming what would make the fixture buildable — see
  `test/verification-coverage-accounting.md` section 6 for the
  permanent/conditional split, and section 5 for the three
  `test/posix-glob.c` fences this rule retagged.
- **BUG (fenced)** — a genuine spec violation found; test is in the file
  with `#if 0 /* BUG: ... */`, see "Bugs found" below.
- **not yet reached** — nobody has checked the spec page against the
  tests yet.

**Companion file:** `test/POSIX-GAP-ACCOUNTING.md` accounts for the
other half of the picture — every one of POSIX.1-2017's 1177 function
interfaces, including the 473 ntlibc does not have at all, and the 357
it has that this ledger has no row for. Read it for "what is missing";
read this file for "how conformant is what exists". It also names the
headers this file's priority order never reached at all (`termios.h`,
`search.h`, `fenv.h`, `pwd.h`/`grp.h`, `regex.h`, `dlfcn.h`, the
glob/fnmatch/wordexp group, `ftw.h`, `sys/uio.h`, `arpa/inet.h`).
**Eight of those have since been clause-audited here** — see the
"successor-queue item 2, group A"-through-"group G" sections at the end
of this file; `ftw.h`, `sys/uio.h` and `arpa/inet.h` remain unreached.
It also names the
four rows here whose second slash-joined name (`utimes`, `fpathconf`,
`readlink`, `unlinkat`) was called by no test. **Those four are now
closed**: each has been split onto a row of its own above and given a
first-ever assertion in `test/posix-unistd.c`, which turned up one
bug (`unlinkat()`'s `flag` validation), since fixed.

## Priority order (per the task brief)

Done (clause-by-clause audited, see the per-header sections below):

1. `string.h` / `strings.h`
2. `stdlib.h` conversions, `qsort`/`bsearch`, `getenv`/`setenv`/random family
3. `time.h` calendar and clock functions
4. `dirent.h`, plus the smaller headers audited alongside it this round:
   `ctype.h` (base `is*`/`to*` family; the XSI `isascii`/`toascii` pair
   is audited separately, see group 12), `locale.h`, `libgen.h`,
   `setjmp.h`, `getopt()`
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
12. `strings.h`, the XSI additions to `ctype.h`
    (`isascii`/`toascii`/`_tolower`/`_toupper`), `assert.h`, `utime.h`,
    and `endian.h` — `test/posix-strings.c`
13. `sys/resource.h` (`getrlimit`/`setrlimit`/`getrusage`/
    `getpriority`/`setpriority`), `sys/select.h` (`select`/`pselect` +
    the `fd_set` macro family), `poll.h`, `sys/param.h`, and the
    GNU `getopt_long`/`getopt_long_only` extensions —
    `test/posix-sysmisc.c`
14. `quick_exit`/`at_quick_exit` (`test/posix-stdlib.c`) and `perror`
    (`test/posix-stdio.c`) — the two loose ends groups 2 and 5 left
    open; both now closed, see those sections.

Not yet reached:

Nothing in the original priority list remains. Every header and
function group named in the task brief now has a clause-cited row
below, an explicit **N/A** with a stated reason, or a fenced **BUG**.

Beyond that list, the sections headed **"successor-queue item 2, group
A"** through **"group G"** at the end of this file audit eight headers
the priority order never named at all — `termios.h`, `search.h`,
`fenv.h`, `pwd.h`/`grp.h`, `regex.h`, `dlfcn.h` and the
`glob.h`/`fnmatch.h`/`wordexp.h` group — following item 2 of
`test/POSIX-GAP-ACCOUNTING.md`'s successor queue rather than the
brief's own numbering. `arpa/inet.h`, `ftw.h` and `sys/uio.h` are the
part of that queue item still open.

Beyond those, the sections headed **"group K"** and **"group L"** at the
end of this file audit the two rows `test/POSIX-GAP-ACCOUNTING.md`'s
"Implemented, not clause-audited" table names as `stdio.h` (16) and
`stdarg.h` (12) — 28 interfaces the priority order reached only in
passing. See that file's second "Changes since" note for how the count
moves (22, not 28: six of them already had a row here).

The residual gaps are the per-section "Not reached" lists, which record
individual *clauses* (not whole functions) that cannot be exercised on
this platform — malloc-exhaustion ENOMEM paths, a second security
principal to be denied as, a read-only file system, symbolic-link
loops, and the like.

For POSIX functions ntlibc does not implement **at all** (as opposed to
implements-but-unaudited, which is what this file tracks), see
`test/POSIX-GAP-ACCOUNTING.md`.

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
| wcs*/wmem* (declared via string.h/wchar.h) | full clause audit deferred to the wchar.h pass — **that pass covered them**: `wcslen`/`wcscpy`/`wcsncpy`/`wcscat`/`wcsncat`/`wcscmp`/`wcsncmp`/`wcschr`/`wcsrchr` and `wmemchr`/`wmemcmp`/`wmemcpy`/`wmemmove`/`wmemset` all have clause-cited assertions there; `wcsstr`, `wcspbrk`, `wcsspn`, `wcscspn`, `wcstok`, `wcsdup`, `wcsnlen`, `wcpcpy`, `wcpncpy`, `wcscasecmp`, `wcsncasecmp` are now implemented and clause-tested there too; the `wcstol` family, `wcscoll`, `wcsxfrm`, `wcsftime`, `mbsnrtowcs` and `wcsnrtombs` are implemented and clause-tested too; of the `wcs*` surface only `wcswidth`/`wcwidth` remain unimplemented (deliberately -- see the wcwidth row) by this libc at all | covered / N/A (not implemented) — see the wchar.h section (priority 8) | test/posix-wchar.c; test/string.c (sanity only) |

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
| atexit / exit / _Exit / abort / quick_exit / at_quick_exit | LIFO order, abort()-under-SIGABRT-ignored still terminates, exit()/_Exit() exit-code propagation | covered elsewhere | test/misc.c |
| quick_exit / at_quick_exit | N1570 7.22.4.7/7.22.4.3 (no POSIX.1-2017 page exists — `quick_exit.html`/`at_quick_exit.html` both 404; they are a C11 addition POSIX.1-2017 does not carry): at_quick_exit registers, quick_exit runs the registered functions in reverse order of registration and then passes control to `_Exit(status)`; at least 32 registrations must succeed; `atexit`-registered functions must **not** run | covered — child-process test now written, re-execing via `__spawn()` (the same pattern test/misc.c and test/posix-alloc.c use, since stock Wine has no `RtlCloneUserProcess` for `fork()`) | test/posix-stdlib.c `test_quick_exit` |
| mkstemp / mkstemps / mkdtemp | template validation (EINVAL for no XXXXXX), successful creation, uniqueness | covered | test/stdlib.c |
| mkstemp family | EINVAL for fewer than six trailing X's | covered | test/posix-stdlib.c |
| mkostemp / mkostemps | DESCRIPTION: created "as if" by open() with O_RDWR forced regardless of access-mode bits in flags | covered | test/posix-stdlib.c |
| mkstemp permission bits (S_IRUSR\|S_IWUSR) | not independently checked (no reliable NT permission-bit oracle under Wine) | N/A | — |
| realpath | NULL resolved_name allocates and returns a heap pointer; ENOENT for a missing path; drive-letter-absolute, forward-slash-normalized result | covered | test/stdlib.c |
| realpath | RETURN VALUE: with a caller-supplied buffer, returns that same pointer | covered | test/posix-stdlib.c |
| system | RETURN VALUE: system(NULL) always non-zero when a shell is available; wait-status decoded through WIFEXITED/WEXITSTATUS | covered | test/posix-stdlib.c |
| system | RETURN VALUE: "as if exit(127)" when the command interpreter itself cannot be executed | covered — was recorded here as "not independently triggerable"; it **is** triggerable, and was a BUG (`__spawn()` failing outright surfaced as `system()==-1`/`ENOEXEC` rather than a `WIFEXITED`/127 status). **Fixed in 182ec5e**: the `pid < 0` branch now synthesizes a `127<<8` status | test/posix-stdlib.c |
| a64l | RETURN VALUE 0L for empty string; digit mapping ('.'=0..'z'=63); first char is least-significant digit; only first six chars used | covered | test/stdlib.c, test/posix-stdlib.c |
| l64a | RETURN VALUE: pointer to an empty string for 0L | covered | test/stdlib.c, test/posix-stdlib.c |
| getsubopt | DESCRIPTION: does not modify keylistp; single-suboption case leaves \*optionp at the terminating NUL; return codes for not-matched/matched-no-value/matched-with-value | covered | test/stdlib.c, test/posix-stdlib.c |
| getloadavg | not POSIX.1-2017 (BSD/glibc); always -1 on NT | N/A (not POSIX.1-2017) | test/stdlib.c |
| ecvt / fcvt / gcvt | not POSIX.1-2017 base (SVID/XSI legacy) | N/A (XSI legacy, not base) | test/stdlib.c |
| mblen / mbtowc / wctomb / mbrtowc / wcrtomb / mbstowcs / wcstombs / btowc / wctob | UTF-8 <-> UTF-16 conversion | covered — see wchar.h section below | test/posix-wchar.c |

No bugs found in stdlib.h this session; every clause checked matched
`src/stdlib/` (and, for getenv/setenv/exit, `src/env/`/`src/exit/`).

### Not reached (stdlib.h)

`system()`'s exit(127)-on-exec-failure clause (originally listed here as
not independently triggerable; since **fixed and covered** — commit
182ec5e makes `system()` synthesize a `127<<8` status when the shell
cannot be executed); mkstemp/mkostemp permission bits (Wine oracle
risk). `quick_exit`/`at_quick_exit` are no longer a gap — see their row
above. The
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
| isascii | `isascii.html` DESCRIPTION: "defined on all integer values" — unlike the base `is*` family, whose argument is UB outside unsigned-char/EOF; RETURN VALUE: non-zero iff `0 <= c <= 0177` | covered | test/ctype.c (-1..255 sweep), test/posix-strings.c (`test_isascii_toascii_defined_for_all_ints`: 128, 255, 256, 65536, -129, -1000, INT_MAX, INT_MIN) |
| toascii | `toascii.html` RETURN VALUE: "shall return the value (c & 0x7f)", exactly, for every `int` including negative ones | covered | test/ctype.c, test/posix-strings.c |
| isascii / toascii | Standards Status: **OB XSI** — obsolescent XSI extension in Issue 7 ("may be removed in a future version"), not POSIX.1-2017 base; correctly gated behind `_XOPEN_SOURCE`/`_GNU_SOURCE`/`_BSD_SOURCE` in include/ctype.h | N/A as a *base* requirement (recorded, not skipped — the clauses above are asserted anyway) | test/posix-strings.c |
| _tolower / _toupper | `_toupper.html`: equivalent to `toupper()`/`tolower()` "except that the application shall ensure that the argument c is a lowercase [uppercase] letter" — only the in-contract inputs asserted, since behaviour outside them is unspecified by the contract itself, not by ntlibc | covered | test/posix-strings.c (`test_underscore_tolower_toupper`) |

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
| _setjmp / _longjmp (XSI, obsolescent) | same value contract as setjmp/longjmp, incl. `_longjmp(env,0)` yielding 1 | covered | test/posix-misc.c `test_setjmp` |
| _longjmp | "shall not manipulate the signal mask" | N/A — vacuous: nothing in `src/setjmp` saves or restores a mask on either arch, so there is no manipulation to be absent. The test still checks the mask across the pair as a regression net | test/posix-misc.c |

No bugs found. NT has no real signal mask, so `sigsetjmp`/`siglongjmp`
share the plain `setjmp`/`longjmp` assembly body — the "restore the
signal mask" half of the contract is vacuously satisfied.

**Superseded, and left standing rather than rewritten.** The
`_longjmp` "shall not manipulate the signal mask" row above is marked
N/A on the grounds that there is no mask machinery for it to be absent
from. Group J3 below asserts the same clause as **covered**, and its
reasoning is the one that holds today: `src/signal/signal.c` does keep a
real `blocked` set that `sigprocmask()` reads and writes, so the clause
is observable and is observed. The two rows contradict on status and on
reason; group J3's is later, evidenced against the tree, and asserted by
`test/posix-tail.c`. Both tests pass and neither assertion contradicts
the other — only the two rows' prose does. Flagged here rather than
edited, because whichever row is deleted the deletion is a hand-edit of
someone else's audit; successor-queue item 4 in
`test/POSIX-GAP-ACCOUNTING.md` is where it gets reconciled.

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
| setbuf / setlinebuf | equivalence to a specific setvbuf call | covered | test/posix-stdio.c |
| feof / ferror / clearerr | independent indicators; clearerr clears both at once | covered | test/stdio.c, test/posix-stdio.c |
| printf family | conversion table, flags/width/precision, return value = bytes transmitted | covered | test/stdio.c, test/posix-stdio.c |
| printf family | `[EILSEQ]` on invalid wide-character code | N/A — formatter is POSIX-locale-only, no wide-char encoding step to fail | -- |
| printf family | `%n$` positional arguments | N/A (documented divergence) — not implemented; `"%1$d"` parses as width 1 + unrecognized `$`, net output `"%$d"` | test/posix-stdio.c |
| scanf family | conversion table, field width, %n, assignment suppression | covered (pre-existing, ~250 lines) | test/stdio.c |
| remove / rename | success, ENOENT | covered | test/stdio.c |
| tmpfile / tmpnam | uniqueness, L_tmpnam buffer sizing, removed-on-close semantics | covered | test/stdio.c |
| tempnam (XSI, obsolescent) | honours `dir` and `pfx`, null `dir` falls back to P_tmpdir, null `pfx` accepted, the generated name does not already exist and is usable, two calls differ, the result is free()-able | covered | test/posix-stdio.c `test_tempnam` |
| tempnam | [ENOMEM] | N/A — needs allocator exhaustion, which this suite cannot induce | -- |
| getc_unlocked | functionally equivalent to getc() on the same stream inside a flockfile()/funlockfile() scope; unsigned-char return; EOF at end | covered | test/posix-stdio.c `test_getc_unlocked` |
| getc_unlocked family | the thread-safety distinction itself ("not required to be ... fully thread-safe" vs "thread-safe within a flockfile() scope") | N/A — no threading on this platform, so the two have no observable difference | -- |
| vprintf / vscanf | equivalent to printf()/scanf() with an argument list; bytes transmitted / items assigned; EOF from vscanf on exhausted input | covered — stdout redirected at fd level, stdin via freopen() | test/posix-stdio.c `test_vprintf_vscanf` |
| va_arg / va_copy (`<stdarg.h>`) | va_copy'd list yields the identical argument sequence from the same point, including after the original is exhausted | covered | test/posix-stdio.c `va_copy_sees_same` |
| perror | `perror.html` DESCRIPTION: writes `s` then ": " then the `strerror()` text then a newline to stderr, but writes no prefix when `s` is NULL or empty; messages identical to `strerror()`'s; must not change `errno` on success | covered | test/posix-stdio.c `test_perror` (stderr captured through a redirected fd: prefixed, NULL-`s`, and empty-`s` forms, plus the errno-preserved case) |
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

`popen`/`pclose` (N/A, no POSIX shell on this platform). `perror` is no
longer a gap — see its row above.

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
| mkdir / rmdir / unlink | covered | test/unistd.c, test/posix-io.c, test/posix-unistd.c |
| mkdirat (AT_FDCWD == mkdir, dirfd-relative, EEXIST on a dir and on a file, ENOENT, ENOTDIR for a file prefix component and for a non-directory `fd`, EBADF) | covered; the `mode` clause N/A — directory mode bits are implementation-defined on NTFS and `src/stat/mkdir.c` ignores `mode` by design | test/posix-unistd.c `test_mkdirat` |
| mkfifo / mkfifoat / mknod / mknodat | N/A (permanent stubs — see `test/POSIX-GAP-ACCOUNTING.md`'s degenerate-stub table); the one clause a stub can honour, "if -1 is returned, the new file shall not be created", **is** asserted, as is `mknod`'s POSIX-listed [EPERM] | test/posix-unistd.c `test_mkfifo_mknod_stubs` |
| unlinkat (AT_FDCWD == unlink/rmdir, AT_REMOVEDIR, ENOTDIR, ENOTEMPTY, ENOENT, EBADF, dirfd-relative) | covered; the [EINVAL] clause **fixed** (`src/unistd/unlink.c`'s `unlinkat()` now rejects `flags & ~AT_REMOVEDIR` instead of masking it off) and asserted unfenced | test/posix-unistd.c `test_unlinkat` |
| rename / renameat: success, ENOENT, same-file no-op | covered | test/unistd.c, test/posix-io.c, test/posix-unistd.c |
| rename EISDIR (new is a dir, old isn't) | **fixed**, commit 3c606a7 (`renameat()` in `src/stdio/misc.c` now disambiguates NT's `STATUS_ACCESS_DENIED` by querying old/new's types, giving EISDIR instead of EACCES) | test/posix-unistd.c `test_rename_new_dir_old_file_eisdir` |
| rename ENOTEMPTY/EEXIST (new is a non-empty dir) | **fixed**, commit 3c606a7 (same fix, gives ENOTEMPTY instead of EACCES) | test/posix-unistd.c `test_rename_onto_nonempty_dir` |
| link / symlink | covered | test/unistd.c |
| linkat (AT_FDCWD == link, dirfd-relative on both sides, st_nlink incremented, same st_ino, EEXIST, ENOENT, EBADF) | covered | test/posix-unistd.c `test_linkat` |
| linkat | AT_SYMLINK_FOLLOW | N/A — `src/unistd/link.c` always opens with FILE_OPEN_REPARSE_POINT and ignores `flag`, so it always implements the flag-clear branch; telling the two apart needs a symlink, which the suite's own environment cannot create — a Wine VERSION gap, not a privilege one (measured, `ff1327e`): stock apt Wine 9.0 answers `FSCTL_SET_REPARSE_POINT` with `STATUS_NOT_SUPPORTED`, that FSCTL having first shipped in wine-10.19, so nothing ever reaches a privilege check. On real NT the privilege (or Developer Mode) *is* the requirement, which is why the real-Windows legs are the ones that can close this | -- |
| execl / execle / execlp / fexecve | argv and envp delivered to the exec'd image; the exec'd image's exit status becomes the caller's; ENOENT for a missing file (direct and PATH-searching forms); EBADF for fexecve on a closed descriptor; a failed exec returns and leaves the process image running | covered | test/exec.c (`--exec-l`, `--exec-le`, `--exec-lp`, `--exec-f` roles) |
| confstr | `_CS_PATH` value and size, `len == 0`/`buf == NULL` sizing call, truncation to `len-1`, [EINVAL] for an invalid name | covered — the [EINVAL] half was a **BUG, FIXED**: `src/unistd/sysconf.c`'s `confstr()` now switches on `name` and rejects the `default`, where it used to start from an empty value and only replace it for `_CS_PATH`, so an unrecognized name returned 1 with errno untouched instead of 0 with EINVAL | test/posix-unistd.c `test_confstr` |
| swab | adjacent-byte exchange, nothing written past nbytes, nbytes 0 and negative do nothing, odd nbytes exchanges nbytes-1, double swab is identity | covered (pure byte shuffling — the one call in this group for which a Wine pass is strong evidence) | test/posix-unistd.c `test_swab` |
| sync | callable, returns no value, defines no error | covered as far as POSIX allows; the scheduling itself N/A — POSIX permits `sync()` to be undetectable by any conforming observation (`fsync()` is the call with a completion guarantee) | test/posix-unistd.c `test_sync` |
| getlogin / getlogin_r | same name from both; getlogin_r returns 0 (not a length, not -1) on success and the errno *value* ERANGE on a short buffer; exactly-fits is a success | covered | test/posix-unistd.c `test_getlogin` |
| fchown / fchownat / lchown / setregid / setpgrp / setsid / tcgetpgrp / tcsetpgrp | return values, agreement with the getters (`setpgrp() == getpgrp()`, `setsid() == getsid(0)`, `tcgetpgrp(0) == getpgrp()`), and `tcgetpgrp`/`tcsetpgrp`'s shall-fail [EBADF] | covered for the returns; the *effects* N/A — one user and one fixed session, per `src/unistd/ids.c`'s and `src/termios/termios.c`'s banners, so nothing could be observed to change. The [EBADF] half was a fenced BUG — both calls discarded `fildes` — and is now **fixed**: `src/unistd/ttyname.c`'s `tcgetpgrp()`/`tcsetpgrp()` run `fildes` through `__fd_get()` before answering, and the fixed answer is `getpgrp()` rather than a second hard-coded 1. [ENOTTY] is deliberately not part of that gate — see the note under the bug list below | test/posix-unistd.c `test_id_session_stubs` |
| pause | DESCRIPTION: suspend until a signal is delivered; RETURN VALUE -1 with EINTR | N/A — **not callable from this suite at all**: `src/unistd/sleep.c`'s `pause()` is `NtDelayExecution` with a maximal timeout, and no asynchronous delivery exists to end it, so a call hangs forever rather than returning. A test would deadlock the run, not fail it | -- |
| readlink / readlinkat (byte count, no NUL, bufsize truncation, EINVAL on a non-link, ENOENT, AT_FDCWD, dirfd-relative) | covered | test/unistd.c, test/posix-unistd.c `test_readlink` |
| access / faccessat (F_OK/R_OK/W_OK/X_OK, ENOENT, EACCES) | covered | test/unistd.c |
| access/stat/open/unlink/rename trailing-slash-on-non-directory ENOTDIR | **fixed**, commit 3c606a7 (`__ntpath()`/`__ntpath_at()` in `src/internal/path.c` now re-check the resolved object's type after stripping a trailing slash — a shared-path-layer fix, so it covers all of these, not just access()) | test/posix-unistd.c `test_access_trailing_slash_enotdir` |
| chdir / fchdir / getcwd (incl. ERANGE off-by-one boundary) | covered | test/unistd.c, test/posix-unistd.c |
| ftruncate / truncate | covered | test/unistd.c |
| fsync / fdatasync | covered | test/unistd.c, test/posix-io.c |
| isatty / ttyname / ttyname_r | covered, except ttyname_r ERANGE (only reachable from a real console fd; test detects and skips) | test/unistd.c, test/posix-unistd.c |
| getpid / getppid / sysconf / pathconf / umask | covered | test/unistd.c, test/posix-unistd.c |
| fpathconf (agrees with pathconf, errno untouched on success, not more restrictive than `<limits.h>` minimums, EINVAL on a bad name) | covered; optional [EBADF] N/A — `fpathconf()` ignores `fildes`, which POSIX permits because that error is "may fail" | test/posix-unistd.c `test_fpathconf` |
| utimensat / futimens / utime / futimesat | covered | test/unistd.c |
| utimes (XSI; tv_usec scaled to tv_nsec, null `times` == now, ENOENT incl. the empty string) | covered | test/unistd.c, test/posix-unistd.c `test_utimes` |
| nanosleep (`src/unistd/sleep.c`) | covered (sanity, via test/unistd.c; not separately clause-cited) | test/unistd.c |

All four bugs originally found here (umask, trailing-slash ENOTDIR,
rename EISDIR, rename ENOTEMPTY) were fixed in commit `3c606a7` ("Fix
four POSIX conformance bugs: umask, trailing-slash ENOTDIR, rename
EISDIR/ENOTEMPTY") and the corresponding fenced tests in
`test/posix-unistd.c` were un-fenced; none remain open.

### Bugs found (never-asserted sweep, unistd.h group)

Three, all found fenced in `test/posix-unistd.c`, all probed on this
tree. All three are now **fixed** and their assertions run unfenced;
none remain.

1. **`unlinkat()` masked off undefined `flag` bits instead of rejecting
   them** — **fixed**. `unlink.html` ERRORS lists as *shall fail*:
   "[EINVAL] (unlinkat() only) The value of the flag argument is not
   valid." `AT_REMOVEDIR` is the only flag `unlinkat()` defines, so
   every other bit is invalid.

   Mechanism: `src/unistd/unlink.c` was
   `int unlinkat(int dirfd, const char *path, int flags) { return
   __unlink_at(dirfd, path, flags & AT_REMOVEDIR); }` — it kept the one
   bit it understood and silently discarded the rest. Probed on this
   tree before the fix: `unlinkat(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW)`
   returned 0 and **deleted the file**, so a caller who reached for the
   wrong `AT_` constant got destruction rather than a diagnostic. The
   fix is the `if (flags & ~AT_REMOVEDIR) { errno = EINVAL; return -1; }`
   guard in front of `__unlink_at()`; the assertion pair (EINVAL, and
   the file still there afterwards) now runs unfenced in
   `test_unlinkat()`.

2. **`confstr()` reported success for an invalid name — FIXED.**
   `confstr.html` RETURN VALUE: "If the value of the name argument is
   invalid, confstr() shall return 0 and set errno to indicate the
   error"; ERRORS lists "[EINVAL] ... the name argument is invalid" as
   its only, shall-fail, entry.

   Mechanism of the defect: `src/unistd/sysconf.c`'s `confstr()` started
   from `const char *s = "";` and replaced it only when
   `name == _CS_PATH`. An unrecognized name fell through the same path a
   genuine empty value would — a lone NUL into the caller's buffer, and
   `i + 1` == 1 returned. Probed: `confstr(-1, buf, n)` and
   `confstr(12345, buf, n)` both returned 1 with errno untouched. A
   caller could not tell an invalid name from a valid one with an empty
   value, and neither of POSIX's two zero-returning cases was reachable
   for any input.

   The fix closes the name set instead of defaulting it: `confstr()`
   switches on `name`, `_CS_PATH` is the one case, and the `default` is
   `errno = EINVAL; return 0`. `<unistd.h>` defines exactly one `_CS_*`
   constant, so that is the whole recognized set — and a name added to
   the header must gain a case with it, which is stated at the function.
   POSIX's *other* zero, a valid name with no configuration-defined
   value returning 0 with errno unchanged, still has no name to reach it
   here.

3. **`tcgetpgrp()`/`tcsetpgrp()` never failed, not even on a descriptor
   that was not open** — **fixed**, and the fence removed.
   `tcgetpgrp.html`: "The tcgetpgrp() function *shall* fail if: [EBADF]
   The fildes argument is not a valid file descriptor", and
   `tcsetpgrp.html` carries the identical clause.

   Mechanism: `src/unistd/ttyname.c`'s two one-line definitions
   discarded `fd` without ever reaching `__fd_get()`, which is what
   every other fd-taking call in the library uses to produce EBADF.
   Probed at the time: `tcgetpgrp(4096)` returned 1 and
   `tcsetpgrp(4096, 1)` returned 0, both with errno untouched. That was
   separable from the deliberate single-session design
   `src/termios/termios.c`'s banner argues for: a fixed process group
   for a *valid* descriptor is that design; answering successfully for
   fd 4096 was an argument check that had never been written. Both now
   call `__fd_get()` first and return -1 with EBADF when it fails.

   **Still open, deliberately: [ENOTTY] on a valid non-terminal
   `fildes`.** Both pages also make that a shall-fail ("The calling
   process does not have a controlling terminal, or the file is not the
   controlling terminal"), and `src/termios/termios.c`'s `tcgetsid()` —
   the closest sibling — *does* gate on `__FD_CONSOLE`. It was left out
   of the fix above because it is a change to the model rather than the
   missing argument check: this library answers one process group for
   the whole process, not per descriptor, and the row above records
   `tcgetpgrp(0) == getpgrp()` as covered on a fd 0 that
   `tools/runtests.sh` redirects from `/dev/null` and that `isatty()`
   therefore rejects. Closing it means deciding whether a process that
   holds no console has a controlling terminal at all, and retiring that
   covered clause — its own change, with its own fence.

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
| sighold / sigrelse (XSI, obsolescent) | block/unblock exactly one signal, idempotent, visible through `sigprocmask()`; [EINVAL] for an illegal signal number | covered — [EINVAL] was a BUG (both wrappers discarded `sigaddset()`'s failure, so the bad bit was never set, `sigprocmask()` got an empty mask and returned 0); **fixed**: both now return -1 as soon as `sigaddset()` fails, leaving the process mask untouched | test/posix-signal.c `test_sighold_sigrelse` |
| sigset (XSI, obsolescent) | returns the previous disposition and installs the new one; SIG_ERR+EINVAL for an illegal signo, SIGKILL, SIGSTOP | covered; **BUG (fenced)** for the SIG_HOLD return and the "shall remove sig from the mask" clause — `<signal.h>` does not define SIG_HOLD at all and `sigset()` is a bare alias of `signal()` | test/posix-signal.c `test_sigset` |
| sigpause (XSI, obsolescent) | returns -1 with EINTR | covered | test/posix-signal.c `test_sigpause` |
| sigpause | DESCRIPTION: suspend until a signal is received | N/A — same reason as `sigsuspend()` above: no asynchronous delivery exists, so a call that genuinely suspended could only hang | -- |
| siginterrupt (XSI, obsolescent) | returns 0 for a valid signal, both flag values | covered; **BUG (fenced)** for [EINVAL] — `sig` is discarded without validation | test/posix-signal.c `test_siginterrupt` |
| siginterrupt | the SA_RESTART effect it names | N/A — same reason as `sigaction`'s SA_RESTART row above | -- |
| abort | never returns; overrides SIG_IGN and SIG_BLOCK; a caught SIGABRT whose handler returns normally still terminates | covered | test/misc.c, test/posix-signal.c |
| abort | "may" attempt fclose() on open streams | N/A — MAY, not SHALL | -- |
| strsignal | see the string.h table above (correction: base function, not XSI) | covered | test/string.c, test/posix-signal.c |
| psignal / psiginfo | not implemented anywhere in `src/`/`include/` | N/A (not implemented) | -- |
| wait | any child, blocks until one changes state | covered | test/misc.c, test/waitpid-overflow.c |
| waitpid | pid==-1/0 (any child — one implicit process group here), pid>0 (exactly that child), WNOHANG, ECHILD | covered | test/waitpid-overflow.c, test/posix-signal.c |
| waitpid | EINVAL for an invalid `options` value | covered — was a BUG (no validation of the other bits); **fixed in 99474ee**: rejects anything outside `WNOHANG\|WUNTRACED\|WCONTINUED`, uniformly for wait/waitpid/wait3/wait4 | test/posix-signal.c `test_waitpid_einval_options` |
| waitpid | EINTR (signal caught while waiting) | N/A — no asynchronous delivery exists to interrupt a blocking wait | -- |
| wait3 / wait4 | BSD/historical, not POSIX.1-2017 base | N/A (not POSIX.1-2017 base) | test/posix-signal.c (sanity only) |
| waitid | P_ALL / P_PID / P_PGID; the siginfo_t result (si_signo == SIGCHLD, si_pid, si_uid, si_code, si_status); WEXITED; WNOHANG | covered — new in this pass; the reaping itself is `do_waitpid()`, so the child-table walk and the exit-status decoding are shared with waitpid() and cannot disagree with it. P_PGID *is* P_PID here, not an approximation of it: every process is its own process group of one (`src/unistd/ids.c`, and kill()'s writeup in `src/signal/signal.c` argues the same point) | test/posix-sysmisc.c `test_waitid_exited`, `test_waitid_signalled` |
| waitid | si_code CLD_EXITED / CLD_KILLED / CLD_DUMPED, with si_status carrying the exit status in the first case and the signal number in the other two | covered | test/posix-sysmisc.c (a child exiting 7, one killed by SIGTERM → CLD_KILLED, one by SIGABRT → CLD_DUMPED) |
| waitid | WNOWAIT — "Keep the process whose status is returned in infop in a waitable state" | covered — expressible because reaping is two separable steps: the status is recorded in the child-table entry, and `__child_remove()` is what closes the handle and frees the slot. WNOWAIT does the first and skips the second, so the child is genuinely still waitable. This exposed a latent defect: the state `pid != 0 && done == 1` was previously unreachable, and `do_waitpid()`'s any-child scan skipped `done` entries, so a `wait()` after a WNOWAIT would have reported ECHILD instead of the status. **Fixed in the preceding commit**, separately from waitid itself | test/posix-sysmisc.c `test_waitid_wnowait` (verified to fail on both assertions with the scan fix reverted) |
| waitid | WSTOPPED / WCONTINUED | **N/A (fenced), platform impossibility.** No child here can be stopped or continued: `kill(pid, SIGSTOP)` is `NtTerminateProcess(h, __NT_SIGNAL_EXIT(SIGSTOP))` (src/signal/signal.c's kill()) — it ends the child rather than suspending it, NT having no job control to suspend into — and even a process suspended by other means could not be reported, since an NT process object transitions to signalled exactly once, on termination: there is no waitable stop or continue transition for `NtWaitForSingleObject` to return, and `NtSuspendProcess` is not declared by src/internal/nt.h. waitid() accepts both flags and simply never has such a status to report, which is correct on a system where children never stop | test/posix-sysmisc.c `test_waitid_stopped_continued`, fenced `#if 0 /* N/A: ... */` with the full assertions written out |
| waitid | ERRORS [EINVAL] (invalid options; none of WEXITED/WSTOPPED/WCONTINUED, which the DESCRIPTION requires applications to specify at least one of; an idtype that is not P_ALL/P_PID/P_PGID), [ECHILD] (no existing unwaited-for child) | covered | test/posix-sysmisc.c `test_waitid_errors` |
| waitid | ERRORS [EINTR] | N/A — same reason as waitpid's: no asynchronous delivery exists to interrupt a blocking wait | -- |
| CLD_EXITED / CLD_KILLED / CLD_DUMPED / CLD_TRAPPED / CLD_STOPPED / CLD_CONTINUED | basedefs/signal.h.html requires all six | all six defined; ntlibc produces only the first three. The other three are defined for source compatibility — a portable `switch (si_code)` covering all six must compile — and `include/signal.h` says plainly that they are never produced and why. Deliberately *unlike* the `ILL_*`/`BUS_*` fault subcodes that header omits: those are asked for by a handler already doing platform-specific fault analysis, whereas CLD_* is the ordinary vocabulary of wait-family status reporting | test/posix-sysmisc.c (the three produced ones); the other three appear only inside the fenced block |
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

### Bugs found (never-asserted sweep, signal.h group)

Three, originally all fenced in `test/posix-signal.c`, all found by the
first calls anything in this tree has ever made to these five XSI names.
All three were probed on this tree, not inferred from source. The first
has since been fixed and its assertions un-fenced unmodified; two remain
fenced.

1. **`sighold()`/`sigrelse()` reported success for an illegal signal
   number.** FIXED; kept here in past tense as the record.
   `sigset.html` ERRORS, shall-fail: "[EINVAL] The sig argument is an
   illegal signal number." Both wrappers build a one-signal set and hand
   it to `sigprocmask()`, but neither looked at `sigaddset()`'s return —
   and `sigaddset()` *does* validate. The invalid bit was simply never
   set, `sigprocmask()` got an empty mask and succeeded, and the caller
   was told the signal was held when nothing happened: the worst of the
   three possible outcomes. Probed then: `sighold(-1)` and
   `sighold(NSIG)` both returned 0. Note that the failure could not have
   degraded into a failing `sigprocmask()` instead — an empty mask is a
   legal argument that `sigprocmask()` is required to accept — so the
   check has nowhere else it could live. Both wrappers now return -1
   with `errno` as `sigaddset()` set it, before touching the process
   mask; `test_sighold_sigrelse` also pins that a rejected call leaves
   the mask empty and that a valid `sighold()`/`sigrelse()` pair still
   works afterwards, so the [EINVAL] arm cannot be satisfied by refusing
   everything.

2. **`sigset()` cannot report SIG_HOLD, and `<signal.h>` does not define
   it.** `sigset.html` RETURN VALUE: "shall return SIG_HOLD if the
   signal had been blocked and the signal's previous disposition if it
   had not been blocked", and the call "shall remove sig from the calling
   process' signal mask". `include/signal.h:260` declares `sigset()` but
   never defines `SIG_HOLD`, which `basedefs/signal.h.html` requires
   alongside it, so no conforming caller can even spell the comparison;
   and `src/signal/signal.c:311` is `{ return signal(sig, h); }`, which
   neither consults nor clears the mask. Probed: with SIGUSR1 held,
   `sigset()` returns SIG_DFL and SIGUSR1 stays blocked.

3. **`siginterrupt()` accepts any signal number.** `siginterrupt.html`
   ERRORS, shall-fail: "[EINVAL] The sig argument is not a valid signal
   number." `src/signal/signal.c:305` is
   `{ (void)sig; (void)flag; return 0; }` — `sig` never reaches this
   file's own `sig_valid()`, which `signal()`, `sigaction()`, `sighold()`
   and `sigrelse()` all use. Probed: `siginterrupt(-1, 1)` and
   `siginterrupt(NSIG, 1)` both return 0 with errno untouched. That the
   `flag` effect is a documented no-op here does not excuse the argument
   check: [EINVAL] is a clause about the argument, not the effect.

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
| wcsstr / wcspbrk / wcscspn / wcsspn / wcstok / wcsdup / wcsnlen / wcpcpy / wcpncpy / wcscasecmp / wcsncasecmp (+ `_l`) | search/span/tokenise/duplicate/bounded-length/copy-returning-end/case-insensitive-compare semantics, each read for wide characters off the corresponding byte-string page | covered | test/posix-wchar.c |
| wcstod / wcstof / wcstold | wcstod.html's equivalence clause asserted directly -- every case is run through both the wide and the byte form and required to agree bit for bit on the value, on the endptr offset and on errno -- over signs, whitespace, exponent and hexadecimal forms, `INF`/`NAN` spellings, no-conversion, `[ERANGE]` at both ends, the rounding-boundary values `src/stdlib/strtod.c`'s 800-digit proof is about, and subject sequences of 511/512/513/799/800/801/4096/60000 digits straddling `MAXDIG` | covered | test/posix-wchar.c |
|  | NOTE for anyone reading `POSIX-GAP-ACCOUNTING.md`'s wide-character section: its suggestion that these "can wrap the existing `src/stdlib/strtod.c` through a narrowing pass" is **wrong**, and that accounting document is a dated record that must not be retro-edited.  A fixed narrowing buffer truncates a subject sequence this library converts exactly today -- a regression against an existing capability, not a shortfall against the spec -- and a dynamic one puts an allocation in front of a function whose ERRORS list cannot report its failure.  The refutation and the design that replaced it are in the commit that added the stride cursor to `src/stdlib/strtod.c` | -- | -- |
| wcstol / wcstoll / wcstoul / wcstoull | subject-sequence grammar incl. base 0/16 prefixes, leading whitespace, endptr placement, sign, and the LLP64-specific split between 32-bit `long` and 64-bit `long long` ranges; `{LONG_MIN}`/`{LONG_MAX}`/`{ULONG_MAX}` clamping with `[ERANGE]`; unsigned negation wrap | covered | test/posix-wchar.c |
| wcscoll / wcscoll_l / wcsxfrm / wcsxfrm_l | C/POSIX-locale collation is code-unit order, so `wcscoll()` is `wcscmp()`'s ordering and `wcsxfrm()` is the identity transform; the defining `wcscmp(xfrm(a),xfrm(b)) == wcscoll(a,b)` clause, the full-length return under truncation, the `n == 0` size query, and errno left untouched | covered | test/posix-wchar.c |
| mbsnrtowcs / wcsnrtombs (Issue 8) | the `nmc`/`nwc` input bound; `*src` set to null at a terminating null and to just past the last unit processed otherwise; `dst == NULL` counting without modifying `*src`; an incomplete character at the end of the input buffer stopping rather than failing, and completing on the next call; never splitting a 4-byte supplementary character; `[EILSEQ]` | covered | test/posix-wchar.c |
| wcsftime | wide-character (not byte) `maxsize` accounting incl. the terminating null; ordinary characters copied unchanged, `%%`, unrecognised specifiers and a trailing `%` matching `strftime()`'s own grammar; a non-ASCII specifier letter; a specifier expansion holding a supplementary character (`%Z`) arriving as a surrogate pair and counted as two | covered | test/posix-wchar.c |
| wcwidth / wcswidth | not implemented, and deliberately so: an ASCII-only `wcwidth()` would report -1 for every code point from U+0080 up, and providing it would displace the gnulib replacement consumers use when a libc has none | **UNIMPL (fenced, declined)** | test/posix-wchar.c, fenced |
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
| fgetwc / getwc / getwchar | one wide character per call incl. a multibyte sequence and a supplementary character arriving as two `wchar_t` from one four-byte sequence; a null wide character distinguished from WEOF; `[EILSEQ]` for invalid bytes and for end-of-file mid-sequence, setting the error rather than the end-of-file indicator | covered | test/posix-wchar.c |
| fputwc / putwc / putwchar | `wc` returned on success, including for a lone high surrogate that writes nothing until its partner arrives; the whole multibyte sequence written; `[EILSEQ]` for an unpaired low surrogate | covered | test/posix-wchar.c |
| fgetws | the `n-1` bound counted in wide characters not bytes; `<newline>` retained; null-wide-character termination; a final unterminated line returned and end-of-file reported on the next call; a null pointer at end-of-file with nothing read | covered | test/posix-wchar.c |
| fputws | non-negative on success; the terminating null wide character not written; an empty string succeeds; a surrogate pair crossing one conversion state | covered | test/posix-wchar.c |
| ungetwc | one level of pushback, guaranteed and enforced; WEOF rejected leaving the stream unchanged; the end-of-file indicator cleared; a non-ASCII character returned unchanged (the slot holds a wide character, not its bytes) | covered | test/posix-wchar.c |
| fwide | no orientation on a newly opened stream; a query (mode 0) does not create one; an orientation once set is never changed in either direction; byte and wide I/O functions set it without fwide() being called | covered | test/posix-wchar.c |
| fwprintf / wprintf / swprintf / vfwprintf / vwprintf / vswprintf | the RETURN counted in wide characters, and the field width and precision likewise, over a `%s` whose bytes are a two-byte UTF-8 sequence and a supplementary character that becomes a surrogate pair; `%ls` copied through unchanged; `%c` converted "as if by calling btowc()", failing with `[EILSEQ]` for a byte that is not a complete character; `%lc` taking the whole `wint_t`; ordinary characters and an unknown conversion taken from the WIDE format; and swprintf()'s distinct truncation contract -- a negative return with errno set, not snprintf()'s would-have-been length | covered | test/posix-wchar.c |
| fprintf family, `%ls` / `%lc` | fprintf.html's `l` qualifier on `s` and `c`: the whole multibyte form of every wide character, the precision counting BYTES with "a partial character shall not be written", the field width computed from the converted length, and a supplementary character crossing one conversion state | covered | test/posix-stdio.c `test_printf_l_modifier` |
| fwscanf / wscanf / swscanf / vfwscanf / vwscanf / vswscanf | the equivalence clause asserted directly -- 19x12 integer and 13x12 string (format, input) pairs run through both the wide and the byte family and required to agree on the return value, the converted values and errno, most of them matching failures so that a failure must happen in the same place in both; plus what only a wide input can show: `%ls` storing wide characters unchanged, `%s` converting them "as if by repeated calls to wcrtomb()", the field width and `%n` counted in wide characters rather than the bytes they encode to, a scanset naming characters the 256-entry table cannot hold (members and ranges, including a range with one end either side of 0xff), a non-ASCII literal in the format, and the pushback ledger -- a look-ahead character given back by seeking, which is a byte offset a wide character does not carry | covered | test/posix-wchar.c |
| open_wmemstream | the buffer holds `wchar_t`, not their multibyte encoding, so a non-ASCII character occupies and is counted as one wide character and a supplementary character as the two it was written as; `*sizep` a wide-character count; wide orientation established at open, before any wide function is applied; a never-written stream reporting an empty terminated buffer; the realloc growth path over 5000 characters; `fclose()` flushing so the final size is visible; `[EINVAL]` for a null `bufp` or `sizep` | covered | test/posix-wchar.c |
| `<wctype.h>` (isw*/tow*/wctype/wctrans) | header does not exist in this library at all | N/A (whole header missing) | -- |
| wcstoimax / wcstoumax | equivalent to the wcstol/wcstoll/wcstoul/wcstoull family; overflow/EINVAL/base-0 auto-detection | covered | test/posix-wchar.c |

No bugs found this session (the two divergences above are deliberate
design choices documented in `src/stdlib/mbrtowc.c`, not spec
violations to fence).

**Superseded, and left standing rather than rewritten.** The
`<wctype.h>` row above — "header does not exist in this library at all",
N/A — was true when priority 8 was written and is not true now:
`include/wctype.h` landed on 2026-08-23, and group I below clause-audits
all sixteen of its `isw*`/`tow*` functions as covered, plus `wctype()`
and `wctrans()`. The row is the stalest kind of ledger entry — one made
false by a later commit rather than by a later audit — and it is the
one place in this file where an old N/A directly contradicts a new
"covered". Flagged here rather than deleted, for the same reason as the
`_longjmp` note under priority 4.

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
| exp2 / exp2f / exp2l | RETURN VALUE table (NaN, ±0->1, +Inf, -Inf->+0), both range errors, exactness of 2^n | covered | test/posix-math.c (`test_exp2`) |
| sin / cos / tan / atan / atan2 | special-value tables (atan2's full ~13-clause quadrant table) | covered | test/math.c, test/posix-math.c |
| pow | full ~20-clause special-value table | covered | test/math.c (~10 sampled), test/posix-math.c (remaining ~14) |
| fmax / fmin (+ fmaxf/fmaxl/fminf/fminl) | one-NaN-arg returns the other, both-NaN -> NaN; the f/l variants carry the identical clause | covered | test/math.c, test/posix-math.c (`test_fmaxmin`, `test_fmaxmin_variants`) |
| hypot | ±Inf wins even over a NaN co-argument; NaN with non-Inf co-arg -> NaN; overflow -> HUGE_VAL | **covered (bug fixed)** — `src/math/hypot.c` used to special-case only a NaN argument, so `hypot(Inf,Inf)` fell through to `Inf/Inf=NaN` and returned NaN instead of +Inf; fixed by moving the infinity check ahead of the NaN check (an infinity outranks a NaN per the spec's stated precedence), governing both cases with one unconditional check | test/math.c (Inf-beats-NaN case), test/posix-math.c (`test_hypot`, all cases including both-Inf, unfenced and passing) |
| nan | "a quiet NaN, if available" | covered | test/math.c, test/posix-math.c |
| math_errhandling / MATH_ERRNO / MATH_ERREXCEPT | required macro values; the conditional `<fenv.h>` requirement | **covered (bug fixed)** — `include/math.h` unconditionally defines `math_errhandling` as `MATH_ERREXCEPT` (2), which per basedefs/math.h.html obligated the implementation to provide `<fenv.h>`'s `FE_DIVBYZERO`/`FE_INVALID`/`FE_OVERFLOW`, but `include/fenv.h` did not exist. Fixed by adding a real `include/fenv.h` + `src/math/fenv.c`: the full C99 set (`feclearexcept`/`feraiseexcept`/`fetestexcept`/`fe{get,set}exceptflag`/`fe{get,set}round`/`fe{get,set}env`/`feholdexcept`/`feupdateenv`) against real hardware, aggregating the x87 status word and (on x86_64 only, `#ifndef __i386__`) MXCSR, since tcc/i386 compiles `double` arithmetic to x87 and tcc/x86_64 to SSE2 (confirmed by disassembling a trivial `double` function under both cross tcc targets) while `src/math/x87.h`'s helpers are x87 on both arches | test/posix-math.c `test_errhandling` (real hardware exceptions from both `src/math/*.c` helpers and plain compiler-emitted arithmetic, plus round-trips of every new function) |

### The `f`/`l` tail (never-asserted sweep)

POSIX gives `acos()`, `acosf()` and `acosl()` one page and one
RETURN VALUE/ERRORS table, the `f`/`l` entries differing only in argument
and return type, so each block below cites the same page as its `double`
counterpart above and asserts the same clauses at the other two widths.
What is new is not the clause but the *type*: a special-value table can
be right for `double` and wrong for `float` (a wrong-width constant, a
promotion that routes the `f`-form through the `double` body and back, a
`HUGE_VALF`/`HUGE_VAL` mixup), and nothing in the tree had looked.
70 names, all previously with no assertion anywhere in `test/*.c`.

| group | clause checked | status | test |
|---|---|---|---|
| acosf/acosl/asinf/asinl | NaN, ±0, acos(1)==+0, finite \|x\|>1 and ±Inf domain errors, asin(±1)==±pi/2 | covered | test/posix-math.c `test_asin_acos_variants` |
| sinhf/sinhl/coshf/coshl/tanhf/tanhl | NaN, ±0, ±Inf, and overflow **at each type's own width** (200 overflows a float and is nowhere near overflowing a double) | covered | `test_hyperbolic_variants` |
| asinhf/asinhl/acoshf/acoshl/atanhf/atanhl | NaN, ±0/±Inf, acosh(1)==+0 and x<1 domain error, atanh(±1) pole error, \|x\|>1 and ±Inf domain errors | covered | `test_inverse_hyperbolic_variants` |
| cbrtf/cbrtl | NaN, ±0, ±Inf, exact cube roots | covered | `test_cbrt_variants` |
| expm1f/expm1l/log1pf/log1pl | NaN, ±0, ∓Inf, per-width overflow, log1p(-1) pole error, x<-1 domain error | covered | `test_expm1_log1p_variants` |
| erff/erfl/erfcf/erfcl | NaN, ±0, ±Inf → ±1, erfc(+Inf)==+0, erfc(-Inf)==2 | covered; `erfc(0)==1` printed as **informational**, never a CHECK — POSIX mandates no accuracy and `src/math/erf.c` lands ~1e-9 short | `test_erf_erfc_variants` |
| lgammaf/lgammal/tgammaf/tgammal | NaN, lgamma pole errors, lgamma(1)/(2)==+0, tgamma negative-integer and -Inf domain errors, tgamma(±0) pole error with the sign of x | covered; the `tgamma(5)==24` factorial check uses a tolerance, for the same accuracy reason | `test_gamma_variants` |
| fmodl | NaN, ±0 with y≠0, y==±0 and x==±Inf domain errors, y==±Inf with finite x, round-toward-zero quotient and the sign of x | covered | `test_fmod_variants` |
| frexpf/frexpl/ldexpf/modff/modfl | result in [0.5,1) and `value == r * 2^*exp`; `value == 0` → 0 with `*exp` 0 **and the sign of the zero kept**; ±Inf/NaN returned unchanged; ldexp round-trip and per-width overflow; modf's signed fraction, ±0→±0 with ±0 stored, ±Inf→±0 with ±Inf stored, NaN→NaN with NaN stored | covered | `test_frexp_ldexp_modf_variants` |
| ceilf/floorl/roundf/truncf/truncl | NaN, ±0 (sign kept), ±Inf, direction of rounding, round()'s halfway-away-from-zero rule (vs rint/nearbyint's to-even), and a negative result rounding to **−0** | covered | `test_rounding_variants` |
| hypotf/hypotl | 3-4-5, "±Inf → +Inf **even if the other argument is NaN**", NaN otherwise, ±0, per-width overflow | covered | `test_hypot_variants` |
| ilogbf/ilogbl/logbf/logbl | logb pole error at ±0, NaN, ±Inf→+Inf, exact exponents; ilogb's three out-of-band results FP_ILOGB0 / FP_ILOGBNAN / INT_MAX, and equivalence to `(int)logb(x)` in range | covered | `test_ilogb_logb_variants` |
| nearbyintf/nearbyintl | NaN, ±0, ±Inf, and the clause that distinguishes it from rint(): **no FE_INEXACT**, checked through `<fenv.h>` | covered | `test_nearbyint_variants` |
| nextafterf/nextafterl/nexttowardf/nexttowardl | x==y→y in x's type, NaN, direction, step-and-back identity, per-width overflow to ±HUGE_VALF, underflow ("correct value or 0.0"), and `nexttowardf` agreeing with `nextafterf` — nexttoward's second argument is `long double` at every width, the shape a mechanical `f`-wrapper is likeliest to get wrong | covered | `test_nextafter_variants` |
| remainderf/remainderl/remquof/remquol | NaN, x==±Inf and y==±0 domain errors, round-to-nearest quotient (vs fmod's toward-zero), `*quo` congruent mod 2^n with the sign of x/y, and remquo's remainder agreeing with remainder's | covered | `test_remainder_remquo_variants` |
| fdimf/fdiml/fmaf/fmal | positive difference, x<y→+0, NaN, per-width overflow; fma's single rounding, the 0×Inf and Inf−Inf domain errors, NaN z, and ±0 results | covered | `test_fdim_fma_variants` |
| scalbnl/scalblnf/scalblnl | NaN, ±0, ±Inf, n==0, per-width overflow and underflow | covered | `test_scalb_variants` |
| isgreater / isgreaterequal / isless / islessequal / islessgreater / isunordered | the ordered results; **0 for a NaN operand in either position** (islessgreater is the one not to confuse with `!=`); isunordered's 1; ±Inf ordered against everything but NaN; and the whole reason the macros exist — **no FE_INVALID for unordered operands**, checked through `<fenv.h>` | covered | `test_compare_macros` |

No bugs found. One test-authoring trap worth recording: `include/math.h`
defines `NAN` as `(0.0f/0.0f)`, and on this toolchain that division is
*not* constant-folded — evaluating the macro inside an `feclearexcept()`
guarded region sets FE_INVALID itself and fails the measurement for a
reason unrelated to the code under test. `test_compare_macros` hoists
the NaN into a `volatile` before clearing. C99 wants `NAN` to be a
constant expression; that is a header/codegen matter rather than a POSIX
clause about these macros, so it is noted, not fenced.

### Not reached (math.h)

`lround`/`llround`/`lrint`/`llrint`/`rint` and their `f`/`l` forms
(implemented, basic behaviour covered by test/math.c, not independently
re-audited clause-by-clause); no fuzzing/property-based cross-check
against glibc beyond test/math.c's existing `NEAR()` spot checks.

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

## strings.h, ctype.h (XSI), assert.h, utime.h, endian.h (priority 12)

New clause-cited audit: `test/posix-strings.c`. `strings.h`'s
`strcasecmp`/`strncasecmp`/`ffs` rows live in the string.h table at the
top of this file; this section records the four small headers audited
alongside them. The XSI `ctype.h` rows are in the ctype.h table under
priority 4, next to the base `is*`/`to*` family they belong with.

### assert.h

| function / requirement | clause checked | status | test |
|---|---|---|---|
| assert | `assert.html` DESCRIPTION: a false expression writes information about the failing call to stderr and calls `abort()`; the information "shall include the text of the argument, the name of the source file, the source file line number, and the name of the enclosing function" | covered | test/posix-strings.c `test_assert_message_and_death` (child re-exec'd via `__spawn()` with its stderr redirected through a real pipe; the parent reads the pipe back and checks all four elements are present, and that the child died SIGABRT-shaped). The line number is cross-checked against an independent `__LINE__` expansion at the *same* source position, so no manually-counted offset can drift. |
| assert | `assert.html` DESCRIPTION: "shall expand to a void expression" | covered | test/posix-strings.c `test_assert_is_a_void_expression` (comma operand, explicit `(void)` cast, `for`-loop clause, `?:` operand) |
| assert / NDEBUG | `basedefs/assert.h.html` DESCRIPTION: with NDEBUG defined before inclusion, `assert()` is `((void)0)`; "the `assert()` macro shall be redefined according to the current state of NDEBUG **each time** `<assert.h>` is included" | covered | test/posix-strings.c (`test_assert_active_before_ndebug` / `test_assert_inactive_under_ndebug` / `test_assert_active_after_ndebug_undef`: one TU toggles NDEBUG and re-includes twice, proving active → inactive → active, via a side effect *inside* the asserted expression — a suppressed `assert()` must not evaluate its argument) |
| assert | `basedefs/assert.h.html` DESCRIPTION: "shall be implemented as a macro, not as a function" | covered (compile-time) | test/posix-strings.c (`#ifndef assert` / `#error`, placed after the NDEBUG toggling so it also pins that the final re-include restored the macro) |
| static_assert | **Not a POSIX.1-2017 requirement**: `basedefs/assert.h.html` names exactly one thing the header shall define, `assert()`. `static_assert` is ISO C11 (N1570 7.2p3). include/assert.h gates it on `__STDC_VERSION__ >= 201112L`, so under this project's `-std=c99` build it is correctly absent rather than leaking a non-reserved identifier | N/A (C11, not POSIX.1-2017) — asserted anyway, in both directions | test/posix-strings.c `test_assert_h_defines_only_assert` |
| __assert_fail | ntlibc-internal (the out-of-line target `assert()` expands to); no POSIX page. Its observable contract is `assert()`'s, and is covered by the row above | N/A (internal, not POSIX) | test/posix-strings.c (exercised through `assert()`) |

### utime.h

| function / requirement | clause checked | status | test |
|---|---|---|---|
| `struct utimbuf` | `basedefs/utime.h.html`: `time_t actime`, `time_t modtime`, "measured in seconds since the Epoch" | covered | test/unistd.c (both members set to distinct known values and read back through `stat()` as `st_atime`/`st_mtime`) |
| utime | DESCRIPTION: sets the access and modification times of the named file | covered | test/unistd.c |
| utime | DESCRIPTION: "If times is a null pointer, the access and modification times of the file shall be set to the current time" | covered | test/posix-strings.c `test_utime_null_sets_current_time` (both times first driven to 2001, then bracketed against `time(0)` readings taken either side of the call — checked through `utime()` itself, not inferred from `utimensat(..., NULL, 0)`, which is a separate entry point) |
| utime | DESCRIPTION: "shall mark the last file status change timestamp for update" | covered | test/posix-strings.c `test_utime_marks_ctime` (`st_ctim` must advance across the call at full timespec resolution, and must *not* follow the 2001 access/modification times backwards) |
| utime | RETURN VALUE: 0 on success, -1 with errno on failure | covered | test/posix-strings.c `test_utime_return_value` |
| utime | ERRORS [ENOENT]: "A component of path does not name an existing file **or path is an empty string**" | covered (both halves) | test/posix-strings.c |
| utime | ERRORS [ENOTDIR], trailing-slash half | covered | test/posix-strings.c (goes through `reject_if_not_dir()` in src/internal/path.c) |
| utime | ERRORS [ENOTDIR], path-prefix half | **fixed**, commit `694a098` (`reject_if_prefix_not_dir()` in `src/internal/path.c`; shared-layer fix, so it covers open/stat/access/unlink/mkdir/rename/opendir/utimensat too) | test/posix-strings.c `test_utime_enotdir_path_prefix` |
| utime | ERRORS [ENAMETOOLONG] (shall-fail, component longer than {NAME_MAX}) | **fixed**, commit `694a098` (`__US_MAX_WCHARS` check hoisted into `__ntpath()`, plus `STATUS_NAME_TOO_LONG` → ENAMETOOLONG; same shared-layer reach) | test/posix-strings.c `test_utime_enametoolong` |
| utime | ERRORS [EACCES], [EPERM] | N/A — both need a second security principal to be denied *as*; ntlibc models exactly one user (`geteuid()` is a hardcoded 1000) | -- |
| utime | ERRORS [EROFS] | N/A — needs a read-only file system the harness cannot mount | -- |
| utime | ERRORS [ELOOP] (both the shall-fail and may-fail forms) | N/A — needs a symbolic-link loop, and the suite's own environment cannot create a symlink at all. Measured (`ff1327e`), the blocker is a Wine version gap rather than the privilege this row used to name: stock apt Wine 9.0 returns `STATUS_NOT_SUPPORTED` from `FSCTL_SET_REPARSE_POINT` (first shipped in wine-10.19), so `src/unistd/link.c`'s `EPERM` arm never runs. `SeCreateSymbolicLinkPrivilege` or Developer Mode is the real-NT requirement, not the one observed here | -- |
| utime / `<utime.h>` | Standards Status: the function and the whole header are marked **obsolescent** in Issue 7 ("may be removed in a future version"); `utimensat()` is the replacement, and is audited under the unistd.h group | recorded, not a testable clause | -- |

### endian.h

**N/A as a whole — `endian.h` is not a POSIX header.** Verified rather
than assumed: it is absent from the POSIX.1-2017 base-definitions
header index (`idx/head.html`, 91 headers), and neither
`basedefs/endian.h.html` nor `functions/endian.h.html` exists. It is a
glibc/BSD extension ntlibc ships for source compatibility, correctly
gated behind `_GNU_SOURCE`/`_BSD_SOURCE` in include/endian.h. There is
therefore no clause to audit — a clause-by-clause pass here would be
inventing requirements, which is exactly the inflation this ledger is
supposed to avoid.

Tested for internal self-consistency instead, which is the only honest
thing to assert: `__BYTE_ORDER` names a real endianness and `BYTE_ORDER`
agrees with it; `htobe16`/`htobe32`/`htobe64` actually swap; the `htole*`
family is the identity on this little-endian-only target; and the
`htobe*`/`be*toh` pairs are inverses. (`test/posix-strings.c`
`test_endian_internal_consistency`.)

## sys/statvfs.h (priority 15)

New header (`include/sys/statvfs.h`), new implementation
(`src/stat/statvfs.c`), new audit rows in `test/posix-sysmisc.c`. All of
it is `NtQueryVolumeInformationFile`, which works on any open handle on
the volume, so `statvfs()` and `fstatvfs()` share one filler and differ
only in how they obtain a handle — the same split `stat()`/`fstat()`
already use next door.

| function / member | clause checked | status | test |
|---|---|---|---|
| statvfs / fstatvfs | RETURN VALUE 0 and a filled `struct statvfs`; a path and a descriptor naming the same file system agree on every non-live field | covered | test/posix-sysmisc.c `test_statvfs` |
| f_bsize / f_frsize | the cluster size (`SectorsPerAllocationUnit * BytesPerSector`). NT's allocation unit *is* its fundamental block — every volume size NT reports is counted in them — so the two are equal here, which is permitted, not required | covered | test/posix-sysmisc.c (nonzero, power of two, equal) |
| f_blocks / f_bfree / f_bavail | `FileFsFullSizeInformation`'s TotalAllocationUnits / ActualAvailableAllocationUnits / CallerAvailableAllocationUnits. That class exists to separate "free on the volume" from "free to this caller after quota", which is POSIX's f_bfree/f_bavail split exactly. Falls back to `FileFsSizeInformation` where unsupported, which reports only the caller-visible figure — f_bfree is then set *equal to* f_bavail rather than guessed at | covered | test/posix-sysmisc.c (the spec's own bounds: both ≤ f_blocks, f_bavail ≤ f_bfree) |
| f_files / f_ffree / f_favail | "Total number of file serial numbers" and the two free counts | **documented zero, not a stub.** NT exposes no file-serial-number pool: a POSIX file system allocates inodes from a fixed table and can say how many remain, NTFS grows its MFT on demand and no `FileFs*` class reports a record count. `fstatvfs.html` DESCRIPTION covers this — "It is unspecified whether all members of the **statvfs** structure have meaningful values on all file systems". Any nonzero value would be fabricated and would be believed by a caller doing capacity arithmetic | test/posix-sysmisc.c asserts the zeros, so a later change that starts inventing an inode count fails the suite rather than being trusted |
| f_fsid | `FileFsVolumeInformation`'s VolumeSerialNumber — the same value `src/stat/stat.c` puts in `st_dev`, so the two agree about what "the same file system" means, which is the only property POSIX gives f_fsid | covered | test/posix-sysmisc.c (asserted equal to `fstat()`'s `st_dev` for a real file) |
| f_namemax | `FileFsAttributeInformation`'s MaximumComponentNameLength (255 on NTFS) | covered | test/posix-sysmisc.c (asserts POSIX's `_POSIX_NAME_MAX` floor of 14, not 255 — an unexpected volume should report a real problem, not a surprise) |
| f_flag ST_RDONLY | `FILE_READ_ONLY_VOLUME` in FileSystemAttributes (a read-only mount) **or** `FILE_READ_ONLY_DEVICE` in `FileFsDeviceInformation`'s Characteristics (read-only media — a CD-ROM is read-only without the file system saying so). Both are checked; they are different conditions | covered (mapped) | not asserted: whether the CI volume is read-only is not the test's business, and the test writes its own temp file |
| f_flag ST_NOSUID | "does not support the semantics of the ST_ISUID and ST_ISGID file mode bits" — set unconditionally, which is a *real* mapping rather than a default: no NT file system supports those bits, `src/stat/stat.c`'s `mode_from_attrs` never produces them, and the exec family never honours them. Omitting the bit would be the inaccurate choice | covered | test/posix-sysmisc.c (asserted always set) |
| statvfs ERRORS | [ENOENT] (missing component, and the empty path), [ENOTDIR] (a regular file used as a path prefix — resolved by `src/internal/path.c`'s `reject_if_prefix_not_dir()`, since NT's object manager answers both cases with STATUS_OBJECT_PATH_NOT_FOUND) | covered | test/posix-sysmisc.c `test_statvfs_errors` |
| fstatvfs ERRORS | [EBADF] "The fildes argument is not an open file descriptor" | covered | test/posix-sysmisc.c `test_statvfs_errors` (negative and out-of-range fd) |
| statvfs / fstatvfs ERRORS | [EOVERFLOW] "One of the values to be returned cannot be represented correctly in the structure pointed to by buf" | covered (guarded) — the only field that can overflow is `f_bsize`/`f_frsize`: `unsigned long` is 32-bit under this target's LLP64 model while the cluster size is a product of two ULONGs. The block counts cannot: `fsblkcnt_t` is unsigned 64-bit and the NT counters are signed 64-bit LARGE_INTEGERs | not reachable from a test — no NT volume has a >4GB cluster; the guard is asserted by inspection only |

## sched.h (priority 14)

New header (`include/sched.h`) and new audit rows in
`test/posix-sysmisc.c`. Only `sched_yield()` is provided; the rest of
what `sched.h` is specified to declare is the `_POSIX_PRIORITY_SCHEDULING`
option group, which ntlibc does not claim and NT cannot honestly support
(NT thread scheduling has priorities but no policy distinction, so
`SCHED_FIFO` and `SCHED_RR` would be one thing under two names) — see
that header's banner.

| function | clause checked | status | test |
|---|---|---|---|
| sched_yield | RETURN VALUE — "shall return 0 if it completes successfully". ERRORS — "No errors are defined", so the `NtYieldExecution` status is deliberately not forwarded: `STATUS_NO_YIELD_PERFORMED` (0x40000024, what kernel32's `SwitchToThread()` turns into FALSE, and what Wine returns routinely) is an informational code meaning the scheduler had no other runnable thread, which is not a POSIX failure and has no errno that could describe it | covered | test/posix-sysmisc.c `test_sched_yield` (1000-iteration loop, i.e. the no-other-thread path, plus errno untouched) |
| sched_getparam / sched_setparam / sched_getscheduler / sched_setscheduler / sched_get_priority_max / sched_get_priority_min / sched_rr_get_interval | the `_POSIX_PRIORITY_SCHEDULING` option group | N/A — not declared, deliberately. `NtSetInformationThread`/`NtQueryInformationThread` would give priority get/set, but NT has no `SCHED_FIFO`/`SCHED_RR`/`SCHED_OTHER` distinction, so `sched_setscheduler()` could only ever report ENOTSUP; declaring it would make a configure probe conclude the option group is present. POSIX puts these declarations inside its own `[PS]` margin markers, which permits exactly this | — (nothing to test; the option is not claimed) |

## sys/resource.h, sys/select.h, poll.h, sys/param.h, getopt_long (priority 13)

New clause-cited audit: `test/posix-sysmisc.c` (1026 lines). This
section supersedes the stale "not yet reached" entries this file used to
carry for all five.

**The `sys/select.h` entry was stale.** It said `select()` was "declared
but not implemented ... nothing to audit until it exists". `select()`
and `pselect()` are both implemented (`src/select/select.c`, which
carries the design writeup), `poll()` too (`src/select/poll.c`), and
include/sys/select.h's own `undefined-ok` marker was removed when they
landed. All three are audited below.

### sys/resource.h

| function | clause checked | status | test |
|---|---|---|---|
| getrlimit | RETURN VALUE 0 + a filled `struct rlimit`; the resources NT can genuinely report (RLIMIT_NOFILE, RLIMIT_NPROC) report real values, the rest `RLIM_INFINITY` — which `getrlimit.html` explicitly permits for a resource with no limit | covered | test/posix-sysmisc.c `test_getrlimit` |
| getrlimit | ERRORS [EINVAL] for an unrecognized `resource` | covered | test/posix-sysmisc.c |
| setrlimit | DESCRIPTION/RETURN VALUE for the resources NT has a real enforcement primitive for — RLIMIT_NPROC, RLIMIT_CPU, RLIMIT_AS, RLIMIT_DATA, via a job object the process creates and assigns itself to. Round-trips through `getrlimit()` | covered — was a gap (declared in the header, **defined nowhere in `src/`**: calling it was a link error, not a runtime ENOSYS). Now defined, `src/misc/resource.c`, commit ec25e54 | test/posix-sysmisc.c `test_setrlimit_enforceable` |
| setrlimit | ERRORS [EINVAL] (`rlim_cur` exceeds `rlim_max`), [EPERM] (raising the hard limit without privilege) | covered | test/posix-sysmisc.c |
| setrlimit | DESCRIPTION for RLIMIT_NOFILE / RLIMIT_STACK / RLIMIT_FSIZE / RLIMIT_CORE / RLIMIT_RSS / RLIMIT_MEMLOCK — the new limit must actually *constrain* resource use | N/A (fenced, with the NT mechanism ruled out one resource at a time: FD_MAX is a compile-time array bound, NT fixes stack reservation at `NtCreateThreadEx()` time, there is no per-process max-file-size / core-dump / RSS / mlock-budget primitive at all). `setrlimit()` accordingly **rejects** a request to lower one of these with EINVAL rather than accepting it and silently not enforcing it | test/posix-sysmisc.c `test_setrlimit_unenforceable` (fenced `#if 0 /* N/A: ... */`) |
| getrusage | RETURN VALUE 0; `ru_utime`/`ru_stime` are real `struct timeval`s (`tv_usec` in [0,1e6)), read from `NtQueryInformationProcess(ProcessTimes)` | covered | test/posix-sysmisc.c `test_getrusage` |
| getrusage | ERRORS [EINVAL] for an invalid `who` | covered | test/posix-sysmisc.c |
| getrusage | RUSAGE_CHILDREN: "resources used by ... terminated and waited-for child processes" — a real accumulator, not a hardcoded zero | covered | test/posix-sysmisc.c (re-execs a short-lived child via `__spawn()`, reaps it, requires the running total to be non-decreasing across it); also test/exec.c, test/posix-grp.c, test/process-win.c |
| getrusage | RUSAGE_THREAD | N/A (Linux/BSD extension, not POSIX.1-2017) — treated as an alias for RUSAGE_SELF since NT gives ntlibc no per-thread accounting; asserted only not to error | test/posix-sysmisc.c |
| getpriority / setpriority | POSIX.1-2017 **base** functions (moved from XSI to BASE in Issue 5, `getpriority.html` "Standards Status"). Nice range `[-NZERO, NZERO-1]`; PRIO_PROCESS / PRIO_PGRP / PRIO_USER; the "set errno to 0 first, since -1 is a legal return" protocol; a `setpriority()` raise round-trips through `getpriority()` | covered — were entirely absent before commit 11426a7; now `src/misc/resource.c`, mapped onto `NtSetInformationProcess(ProcessPriorityClass)` | test/posix-sysmisc.c `test_getpriority_setpriority` |
| getpriority / setpriority | ERRORS [ESRCH] (no such process), [EINVAL] (unrecognized `which`), [EACCES] (unprivileged request to *lower* the nice value) | covered | test/posix-sysmisc.c |
| setpriority | ERRORS [EPERM] (a process the caller does not own) | N/A — needs a second security principal; ntlibc models one user and one process group (see include/sys/resource.h's PRIO_PGRP/PRIO_USER note) | test/posix-sysmisc.c (recorded in a comment, not asserted) |

The nice↔NT mapping is lossy (20 nice values onto 3 priority classes),
and `getpriority()` on a *foreign* process is explicitly approximate —
both documented at length in include/sys/resource.h rather than papered
over. Every `setpriority()` this process issues against itself is
remembered verbatim, so the self round-trip above is exact.

### sys/select.h and poll.h

| function | clause checked | status | test |
|---|---|---|---|
| select | RETURN VALUE: "the total number of bits set in the bit masks" | covered | test/posix-sysmisc.c `test_select_ready_count` |
| select | DESCRIPTION: a zero-valued timeout polls and returns immediately | covered | test/posix-sysmisc.c `test_select_zero_timeout_polls` |
| select | ERRORS [EBADF] (a set contains a descriptor that is not open), [EINVAL] (`nfds` negative or > FD_SETSIZE, invalid timeout) | covered | test/posix-sysmisc.c `test_select_errors` |
| pselect | DESCRIPTION: differs from `select()` only in the `struct timespec` timeout and the `sigmask` argument | covered | test/posix-sysmisc.c `test_pselect_timespec_and_mask` |
| pselect | the atomic sigmask swap's *observable* difference from a `sigprocmask()`+`select()` pair | N/A — there is no asynchronous signal delivery on this platform (every signal is delivered synchronously, `src/signal/signal.c`), so the atomicity the clause exists to guarantee is vacuous here | test/posix-sysmisc.c (comment) |
| FD_ZERO / FD_SET / FD_CLR / FD_ISSET / FD_SETSIZE | `basedefs/sys_select.h.html` macro semantics | covered | test/posix-sysmisc.c `test_fd_macros` (pre-dates `select()` existing) |
| poll | RETURN VALUE: "the number of pollfd structures that have selected events"; POLLIN/POLLOUT event bits | covered | test/posix-sysmisc.c `test_poll_ready_count` |
| poll | DESCRIPTION: zero timeout polls; a negative `fd` is ignored (and `revents` zeroed); POLLNVAL for an invalid open descriptor | covered | test/posix-sysmisc.c `test_poll_zero_timeout_polls`, `test_poll_negative_fd_ignored`, `test_poll_nval` |

`src/select/select.c`'s banner carries the design rationale — the
wait-vs-poll split across this library's three descriptor shapes
(pipe / console / regular file), the latency-vs-CPU trade-off of the
20ms pipe-poll interval, and exact timeout semantics. `poll()` shares
that file's readiness-probe and wait primitives.

### sys/param.h

**N/A — BSD macros only, no functions, and not a POSIX header.**
Exercised for internal consistency only (`test/posix-sysmisc.c`
`test_sys_param`), the same treatment `endian.h` gets above and for the
same reason.

### getopt_long / getopt_long_only

**N/A — GNU extensions, no POSIX page.** Recorded explicitly here so
nobody re-derives it: `getopt.html` specifies `getopt()`, `optarg`,
`opterr`, `optind` and `optopt` and nothing else; there is no
`functions/getopt_long.html`, and `<getopt.h>` is not in the
POSIX.1-2017 header index at all. The POSIX-conformance status of these
two is therefore settled and needs no further work.

They are nevertheless implemented (`src/misc/getopt_long.c`) and heavily
used, so `test/posix-sysmisc.c` audits them against the GNU
documentation cited inline instead of against POSIX: unambiguous
abbreviation matching (`test_getopt_long_abbrev`), all three argument
forms — `--opt=arg`, `--opt arg`, and an optional argument's
attached-only rule (`test_getopt_long_arg_forms`), the `flag`/`val`
indirection (`test_getopt_long_flag`), `getopt_long_only`'s single-dash
long-option acceptance (`test_getopt_long_only`), `longindex`
(`test_getopt_long_index`), and rescanning after an `optind` reset
(`test_getopt_long_reset`).

### Bugs found (priority 12/13 groups)

Two, both found while auditing `utime.html`'s ERRORS list, both fenced
in `test/posix-strings.c` at the time (a fix belongs in a change of its
own, not in an audit pass) and **both since fixed** in commit `694a098`,
where the two tests were un-fenced.

Neither was specific to `utime()`. Both were probed across the library
before being written up, and `open()`, `stat()`, `access()`, `unlink()`,
`mkdir()` and `utimensat()` reproduced both; so each was **one** defect
in the shared path layer (`src/internal/path.c`), not seven — and one
shared fix closed each of them everywhere at once.

1. **A path prefix component that names an existing regular file gives
   `ENOENT`, not `ENOTDIR`.** `utime.html` (and `open.html`,
   `stat.html`, `access.html`, `unlink.html`, `mkdir.html`,
   `chdir.html`, … — the clause is boilerplate across the whole
   file-system surface) lists as *shall fail*: "[ENOTDIR] A component of
   the path prefix names an existing file that is neither a directory
   nor a symbolic link to a directory."

   Mechanism: NT's object manager does not distinguish "a directory in
   the path prefix does not exist" from "a component of the path prefix
   exists but is a file" — it answers both with
   `STATUS_OBJECT_PATH_NOT_FOUND`, which `src/internal/errno.c` maps to
   `ENOENT` (correctly, for the first of the two). POSIX requires them
   told apart.

   Was implementable, not an NT limitation: `src/internal/path.c`'s
   `reject_if_not_dir()` already did exactly this kind of extra
   `NtQueryAttributesFile()` disambiguation for the *trailing-slash*
   half of the very same [ENOTDIR] clause (which is why that half
   passed).

   **Fixed** in commit `694a098`: `reject_if_prefix_not_dir()` next to
   it applies the same query to the path prefix, walking from the
   nearest ancestor outwards. A path whose parent is a directory costs
   one query and stops there (a directory's own parents cannot be
   anything but directories); a deeper ancestor is only looked at once a
   nearer one has come back missing, i.e. on a path that was going to
   fail regardless. ENOTDIR is reported only on a positive answer, so
   any query that cannot be answered still leaves the verdict to the
   real operation.

   One platform caveat, recorded because Wine is not the authority here:
   Wine's `NtQueryAttributesFile` resolves a `RootDirectory`-relative
   name against the *process* working directory rather than the root
   handle (`dlls/ntdll/unix/file.c`: the Unix name `lookup_unix_name()`
   built relative to the root fd is handed to `get_file_info()`), so
   under Wine the `openat(dirfd, "file/below", …)` form still reports
   ENOENT — as does the pre-existing trailing-slash check on the same
   form, which Wine has never rejected either. NT hands the whole
   `OBJECT_ATTRIBUTES` to `ObOpenObjectByName`, root handle included, so
   both are expected to hold there; the `windows-test` CI legs are the
   verdict.

   Test (un-fenced): `test_utime_enotdir_path_prefix`.

2. **An over-long pathname gives `ENOENT`, not `ENAMETOOLONG`.**
   `utime.html` lists as *shall fail*: "[ENAMETOOLONG] The length of a
   component of a pathname is longer than {NAME_MAX}." (The {PATH_MAX}
   form is only *may fail*; the 40000-byte single component the test
   uses exceeds both, so the shall-fail clause is the one that applies.)

   Mechanism: `src/internal/path.c`'s `__ntpath()` funnelled every
   `RtlDosPathNameToNtPathName_U_WithStatus()` failure other than
   `STATUS_NO_MEMORY` into a single `errno = ENOENT`. The correct check
   existed in the same file — but only in `__ntpath_at()`'s
   relative-to-a-dirfd branch (`n > __US_MAX_WCHARS`), which an
   `AT_FDCWD` or absolute path never reaches, because `__ntpath_at()`
   forwards both straight to `__ntpath()`.

   **Fixed** in commit `694a098`: that check is hoisted into
   `__ntpath()`, which fixes every caller of the layer at once, and
   `STATUS_NAME_TOO_LONG` — what the Rtl returns for a relative name
   that only overflows once resolved against the current directory — now
   maps to ENAMETOOLONG instead of falling into the catch-all.

   `chdir()` was the **one** caller that reported this correctly, and
   only because `src/unistd/chdir.c` carries its own copy of the length
   check. `test/unistd.c` pins `chdir()` and `symlink()` — which is
   precisely why the gap in every other caller went unnoticed for so
   long. A cautionary case for this ledger: coverage of one caller of a
   shared layer is not coverage of the layer.

   That private copy was reviewed as part of the fix and **kept**: it is
   not a redundant second implementation of the shared check, because
   `chdir()` never calls `__ntpath()` at all — it hand-builds a
   `UNICODE_STRING` for `RtlSetCurrentDirectory_U()`, and the check
   guards that narrowing. Deleting it would regress `chdir()` to ENOENT.
   The same asymmetry leaves `chdir()` with the [ENOTDIR] path-prefix
   gap the shared layer no longer has (`chdir("file/below")` still gives
   ENOENT), since the prefix check likewise lives in `__ntpath()`;
   recorded here as open, and deliberately not patched with a third
   private copy.

   Test (un-fenced): `test_utime_enametoolong`.

### Not reached (priority 12/13 groups)

`utime()`'s [EACCES]/[EPERM]/[EROFS]/[ELOOP] paths and
`setpriority()`'s [EPERM] path (all need a second security principal, a
read-only mount, or symlink-creation privilege — see the rows above);
`setrlimit()`'s six unenforceable resources (fenced N/A, with the NT
mechanism ruled out one at a time); `pselect()`'s sigmask atomicity
(vacuous without asynchronous signal delivery). Real (non-Wine) Windows
remains the authority for all of the above — the CI `windows-test` legs,
not a Wine run, are the verdict.

## termios.h (successor-queue item 2, group A)

The first of the twelve headers `test/POSIX-GAP-ACCOUNTING.md`'s
successor queue names as never having been given a single ledger row
here. `src/termios/termios.c`'s file banner argues, function by
function, which calls are spec-permitted no-ops; that argument had
never been checked against the spec pages by anyone but its author,
which is why this header was the one to start with.

New clause-cited audit: `test/posix-termios.c` (this session), which
already existed for `<sys/ioctl.h>`/`<sys/file.h>` and now carries the
`<termios.h>` rows below.

**Oracle: NT-behaviour territory, and Wine is weak evidence.** Every
function in this header is gated on `__FD_CONSOLE` and the three
load-bearing `c_lflag` bits go through kernel32's
`GetConsoleMode()`/`SetConsoleMode()`. A green Wine run proves the
gating, the header constants and the `cf*` struct accessors — all of
which are pure ntlibc code — and nothing about console mode. The
`windows-test` CI legs are the authority for the rest.

| function | clause checked | status | test |
|---|---|---|---|
| tcgetattr | "[EBADF] The fildes argument is not a valid file descriptor." | covered | test/posix-termios.c (`test_termios_gating`) |
| tcgetattr | "[ENOTTY] The file associated with fildes is not a terminal." — on a regular file, a pipe (both ends) and a directory | covered | test/posix-termios.c (`test_termios_gating`, `test_termios_gating_other_shapes`) |
| tcgetattr | tcgetattr.html DESCRIPTION "shall get the parameters associated with the terminal ... and store them in the termios structure" — c_iflag/c_oflag/c_cflag/c_cc[] retrieved as stored | covered *(console only)* | test/posix-termios.c (`test_termios_stored_roundtrip`) |
| tcgetattr | the same, for c_lflag's ISIG/ICANON/ECHO via `GetConsoleMode()` | covered *(console only, and real-NT-only for the console-mode half)* | test/posix-termios.c (`test_termios_lflag_roundtrip`) |
| tcsetattr | "[EBADF]" / "[ENOTTY]" | covered | test/posix-termios.c (`test_termios_gating`) |
| tcsetattr | "[EINVAL] The optional_actions argument is not a supported value" | covered *(console only — on a non-terminal the [ENOTTY] gate is reached first, and POSIX fixes no order between them)* | test/posix-termios.c (`test_termios_einval`) |
| tcsetattr | TCSANOW / TCSADRAIN / TCSAFLUSH are all supported values and none may be rejected | covered *(console only)* | test/posix-termios.c (`test_termios_stored_roundtrip`) |
| tcsetattr | "shall not change the values found in the termios structure" | covered *(console only)* | test/posix-termios.c (`test_termios_stored_roundtrip`) |
| tcsetattr | tcsetattr.html DESCRIPTION, TCSAFLUSH "all input so far received but not read shall be discarded" | N/A — needs type-ahead sitting unread in a real interactive console's input queue; ntlibc wraps no `WriteConsoleInput()`, so a process cannot inject into its own console input buffer to observe the discard | fenced, `test_tcflush_discards_input` |
| tcsetattr | "the modem control lines shall no longer be asserted" when the output baud rate is B0 | N/A — a console handle has no modem control lines; nothing on this platform reads `c_ospeed` back (src/termios/termios.c banner) | — |
| tcsetattr / tcflush / tcflow / tcdrain / tcsendbreak | "[EIO] The process group of the writing process is orphaned ..." and the background-process-group SIGTTOU clauses | N/A — this platform has exactly one, fixed session and one process group (src/unistd/ids.c's `getsid()`/`setsid()` always answer 1, src/unistd/ttyname.c's `tcgetpgrp()` likewise), so no process can ever be in a *background* process group of its controlling terminal, and the orphaned-group precondition is unconstructible | — |
| tcsetattr / tcdrain | "[EINTR] A signal interrupted ..." | N/A — neither call blocks on this platform (tcsetattr's console-mode write is synchronous, tcdrain returns immediately, see below), so there is no window for a signal to interrupt | — |
| cfgetispeed | "shall return exactly the value in the termios data structure, without interpretation" | covered | test/posix-termios.c (`test_cf_speed_no_interpretation`) |
| cfgetospeed | the same clause, output side | covered | test/posix-termios.c (`test_cf_speed_no_interpretation`) |
| cfsetispeed | "shall set the input baud rate stored in the structure pointed to by termios_p to speed" — all sixteen POSIX baud values, and no other member of the structure disturbed | covered | test/posix-termios.c (`test_cf_speed_all_rates_and_isolation`) |
| cfsetospeed | the same clause, output side | covered | test/posix-termios.c (`test_cf_speed_all_rates_and_isolation`) |
| cfsetispeed / cfsetospeed | "[EINVAL] The speed value is not a valid baud rate" | N/A — a *may fail*, not a *shall fail*; ntlibc has no serial line whose supported-rate set could make any value invalid, so it accepts every one, which the clause permits | — |
| tcflush | "[EBADF]" / "[ENOTTY]" | covered | test/posix-termios.c (`test_termios_gating`, `test_termios_gating_other_shapes`) |
| tcflush | "[EINVAL] The queue_selector argument is not a supported value" | covered *(console only)* | test/posix-termios.c (`test_termios_einval`) |
| tcflush | "shall discard data written ... but not transmitted, or data received but not read" | N/A — see the tcsetattr TCSAFLUSH row; the input half is genuinely implemented (`FlushConsoleInputBuffer()`) but unobservable from inside the process, the output half has no transmit queue to discard | fenced, `test_tcflush_discards_input` |
| tcdrain | "[EBADF]" / "[ENOTTY]" | covered | test/posix-termios.c (`test_termios_gating`, `test_termios_gating_other_shapes`) |
| tcdrain | "shall block until all output written to the object referred to by fildes is transmitted" | N/A — console output is already in the screen buffer when `WriteConsole()` returns, so no state exists in which tcdrain() could be seen to block, and a correct immediate return is indistinguishable from a stub | fenced, `test_tcdrain_blocks_until_transmitted` |
| tcflow | "[EBADF]" / "[ENOTTY]" | covered | test/posix-termios.c (`test_termios_gating`) |
| tcflow | "[EINVAL] The action argument is not a supported value" | covered *(console only)* | test/posix-termios.c (`test_termios_einval`) |
| tcflow | TCOOFF "output shall be suspended" / TCOON / TCIOFF / TCION STOP-START transmission | N/A — no console API suspends a screen-buffer write and no wire exists for a STOP/START character. **Recorded with a caveat**: unlike tcsendbreak.html, tcflow.html grants *no* implementation-defined escape for a terminal with no serial line, so the unconditional `return 0` rests on a platform argument rather than a spec permission. See "Observed behaviour where POSIX permits latitude" below. | fenced, `test_tcflow_suspends_output` |
| tcsendbreak | "[EBADF]" / "[ENOTTY]" | covered | test/posix-termios.c (`test_termios_gating`) |
| tcsendbreak | "If the terminal is not using asynchronous serial data transmission, it is implementation-defined whether tcsendbreak() sends data ... or returns without taking any action" | covered — the no-op *is* the specified behaviour here, so the return value is the whole testable content of the clause | test/posix-termios.c (`test_termios_gating`) |
| tcgetsid | "[EBADF]" / "[ENOTTY]" | covered | test/posix-termios.c (`test_termios_gating`, `test_termios_gating_other_shapes`) |
| tcgetsid | "shall return the process group ID of the session associated with the terminal" | covered *(console only)* — agrees with `getsid(0)` rather than inventing an answer, and is positive, `(pid_t)-1` being reserved for the error return | test/posix-termios.c (`test_tcgetsid`) |
| `<termios.h>` | termios.h.html "Subscript values shall be ... distinct", and the c_iflag/c_oflag/c_cflag/c_lflag names must be OR-able into one `tcflag_t` each, so within a group they must not overlap; CSIZE's CS5..CS8 must lie inside the CSIZE mask and not collide with any other control-mode bit | covered | test/posix-termios.c (`test_termios_header_constants`) |
| `<termios.h>` | the [XSI] Output Modes delay masks — NLDLY (NL0, NL1), CRDLY (CR0..CR3), TABDLY (TAB0..TAB3), BSDLY (BS0, BS1), VTDLY (VT0, VT1), FFDLY (FF0, FF1) | **UNIMPL** — see below | fenced, `test_termios_oflag_delay_masks` |

### UNIMPL found (termios.h)

1. **The [XSI] `c_oflag` delay masks are not defined.**
   `termios.h.html`'s Output Modes table marks `NLDLY`/`NL0`/`NL1`,
   `CRDLY`/`CR0`..`CR3`, `TABDLY`/`TAB0`..`TAB3`, `BSDLY`/`BS0`/`BS1`,
   `VTDLY`/`VT0`/`VT1` and `FFDLY`/`FF0`/`FF1` [XSI], in the same table
   and with the same marking as `ONLCR`, `OCRNL`, `ONOCR`, `ONLRET`,
   `OFILL` and `OFDEL`. ntlibc compiles `-D_XOPEN_SOURCE=700` and
   defines those six; it defines none of the delay names.

   Classified **UNIMPL, not N/A**, on the project's own rule that "I
   chose not to" is UNIMPL. The N/A argument would have to be "no
   console applies output delays" — but that is equally true of
   `ONLCR`, which *is* defined, and `c_oflag` is already accepted and
   stored wholesale by `src/termios/termios.c`'s shadow, so the delay
   bits would round-trip like every other output-mode bit at no cost.
   A header gap, not a platform impossibility.

   Test (fenced): `test_termios_oflag_delay_masks`.

### Observed behaviour where POSIX permits latitude (termios.h)

- `tcdrain()`, `tcflow()` and `tcflush()`'s output half return 0
  without acting. For `tcsendbreak()` this is explicitly sanctioned
  (`tcsendbreak.html`: "it is implementation-defined whether
  tcsendbreak() sends data to generate a break condition or returns
  without taking any action"); the other three pages carry no such
  clause, so their no-op status rests on the platform argument in
  `src/termios/termios.c`'s banner — a console write is complete when
  `WriteConsole()` returns, so there is no transmit queue to drain,
  suspend or discard. That argument is sound and the alternative
  (returning -1) would be worse: none of the three has an
  ENOTSUP-shaped error, so a failure return would be indistinguishable
  from the genuine [EBADF]/[EINVAL]/[ENOTTY] cases. Recorded here
  rather than as a BUG because the requested effect is *unobservable*
  on this platform, not because it is permitted — a fenced test names
  each clause so the distinction is not lost.

- `speed_t` values are the literal bps number rather than opaque
  constants, and `c_ispeed`/`c_ospeed` are struct members added the
  *BSD way. POSIX mandates neither the encoding nor the storage shape
  (`termios.h.html` requires only the `cf*` accessors), so both are
  latitude, and `test_cf_speed_no_interpretation` pins the one clause
  that does constrain them ("without interpretation").

### Not reached (termios.h)

Everything marked *(console only)* above needs a descriptor that
`isatty()` calls a terminal. `make check` has none: `tools/runtests.sh`
redirects stdin from `/dev/null` and captures stdout/stderr through a
pipe, so fds 0/1/2 are not consoles there. `test/posix-termios.c` now
looks for one in two places rather than one and notes what it found;
under an interactive run (a pty attached, so fds 0/1/2 *are*
`__FD_CONSOLE`) every one of those rows was verified green, which is
how they are recorded as covered rather than not reached.

Side finding, outside this header's clauses and therefore not a row
above: **`open("/dev/tty")` never succeeds on this platform, even when
a console is attached.** `src/internal/path.c:29` rewrites `/dev/tty`
to `CON`, which `RtlDosPathNameToNtPathName_U` turns into `\??\CON`, a
name `NtCreateFile` does not resolve here — measured EBADF with no
console attached and EINVAL with one. That was this file's *only*
console-detection path before this session, so the whole
console-dependent half of it skipped unconditionally in every
environment, not just under `make check`'s runner. The fix here is to
the test (fall back to whichever of fds 0/1/2 `isatty()` accepts); the
`/dev/tty` mapping itself is untouched and remains open. POSIX
specifies `/dev/tty` in XBD 10.1 Directory Structure and Devices, not
in any function page audited here.

## search.h (successor-queue item 2, group B)

Second of the twelve. `test/posix-glob.c` already carried a
`<search.h>` section (added when the header was implemented) covering
the common path of all eleven functions; this pass audited the spec
pages clause by clause against it and filled the gaps.

**Oracle: pure C library, so Wine is a sound oracle.** Not one of
these eleven functions makes an NT call — `hcreate`/`tsearch` reach
`malloc`/`free` and nothing else. A green Wine run is as good as a
green run anywhere.

Rows below are the clauses this pass *added*; the ones
`test_search_hsearch_roundtrip`, `test_search_tsearch_tfind`,
`test_search_tdelete`, `test_search_twalk`,
`test_search_lsearch_lfind` and `test_search_insque_remque` already
covered are not repeated.

| function | clause checked | status | test |
|---|---|---|---|
| hcreate | "shall return 0 if it cannot allocate sufficient space for the table; otherwise, it shall return non-zero" | covered — FIXED (b6656c1 capacity overflow); the defect is described in the narrative below, kept in past tense as the record | test/posix-glob.c (`test_search_hcreate_overflow`) |
| hcreate | "The nel argument is an estimate of the maximum number of entries ... This number may be adjusted upward" | covered — the capacity is not pinned, only that it is finite and non-degenerate | test/posix-glob.c (`test_search_hsearch_table_full`) |
| hsearch | "shall return a null pointer if ... the action is ENTER and the table is full" | covered | test/posix-glob.c (`test_search_hsearch_table_full`) |
| hsearch | "It shall return a pointer into a hash table indicating the location at which an entry can be found" — every entry accepted before the table filled is still findable | covered | test/posix-glob.c (`test_search_hsearch_table_full`) |
| hcreate / hsearch | "may fail if: [ENOMEM] Insufficient storage space is available" | N/A — a *may fail*; neither function sets errno, which the clause permits, and a genuine allocation failure is unforceable here (the same malloc-exhaustion limit this ledger records for every other header) | — |
| `<search.h>` | basedefs/search.h.html: `ENTRY` with `char *key` / `void *data`; `ACTION` with FIND and ENTER; `VISIT` with preorder, postorder, endorder, leaf — the enumerators steer `hsearch()` and `twalk()`, so within each enumeration they must be distinct | covered | test/posix-glob.c (`test_search_header_types`) |
| tsearch / tfind / tdelete | "A null pointer shall be returned by tdelete(), tfind(), and tsearch() if rootp is a null pointer on entry" — distinct from `*rootp` being null, which denotes an empty tree | covered | test/posix-glob.c (`test_search_null_rootp`) |
| tdelete | "shall return a pointer to the parent of the deleted node" — the only one of the three return cases whose *value* POSIX specifies; the root and not-found cases were already covered | covered | test/posix-glob.c (`test_search_tdelete_parent`) |
| tsearch / tfind / tdelete / twalk | "it shall be possible to cast a pointer-to-node into a pointer-to-pointer-to-element to access the element stored in the node" — applied to tdelete()'s parent return and to twalk()'s node argument, not just tsearch()/tfind() | covered | test/posix-glob.c (`test_search_tdelete_parent`, `test_search_twalk_order_and_levels`) |
| twalk | "preorder, postorder, endorder, or leaf depending on whether this is the first, second, or third time that the node is visited (during a depth-first, left-to-right traversal), or whether the node is a leaf", and "the level of the node in the tree, with the root being level 0" | covered — the whole visit sequence is recorded and compared against what the clause requires for a known tree shape, which a leaf-count plus a root-level check cannot distinguish from a wrong order | test/posix-glob.c (`test_search_twalk_order_and_levels`) |
| twalk | "If root is a null pointer, no operation shall be performed" | covered | test/posix-glob.c (`test_search_twalk_order_and_levels`) |
| lsearch / lfind | the comparison function is called with the key first and the array element second, and a hit returns a pointer *into the table* rather than the caller's key | covered | test/posix-glob.c (`test_search_lsearch_argument_order`) |
| remque | "shall remove the element pointed to by element from a queue" — the circular form, including removing the sole element of a one-element ring where both neighbour pointers name the element itself | covered | test/posix-glob.c (`test_search_remque_circular`) |
| insque / remque | "No errors are defined." / both return void | N/A — vacuous | — |

### Bugs found (search.h)

1. **`hcreate()` reports success for a table it could not size.**
   `hcreate.html` RETURN VALUE: "The hcreate() function shall return 0
   if it cannot allocate sufficient space for the table; otherwise, it
   shall return non-zero."

   Mechanism: `src/search/hsearch.c` computes the capacity as
   `cap = nel + nel / 2 + 8` in `size_t`, with no overflow check, so a
   large enough `nel` wraps to a tiny capacity that `calloc()` then
   satisfies trivially. `hcreate()` returns non-zero for a table that
   cannot come close to holding `nel` entries — which is exactly the
   case the RETURN VALUE clause exists to report.

   `nel = (SIZE_MAX / 3) * 2 + 2` is the wrapping value on both arches
   this library builds for: `SIZE_MAX` is `3q` for `q = SIZE_MAX/3`
   (2^32-1 and 2^64-1 are both divisible by 3), so `nel + nel/2` comes
   to `SIZE_MAX + 3`, i.e. 2 modulo the `size_t` width, and `cap` ends
   up 10 either way. Measured on x86_64: `hcreate()` returns 1 and the
   11th `ENTER` then returns NULL — ten slots reported as sufficient
   space for 1.2e19 entries.

   Fix shape (not applied, per the standing rule): a range check
   before the multiply-and-add, refusing any `nel` above
   `(SIZE_MAX - 8) / 3 * 2`.

   Test (fenced): `test_search_hcreate_overflow`.

### Observed behaviour where POSIX permits latitude (search.h)

- `src/search/hsearch.c`'s banner attributes the sentence *"Only one
  hash table may be active at a time."* to `hcreate.html`. **That
  sentence is not on the page** — the fetched POSIX.1-2017 text
  contains no clause limiting the number of active tables at all; the
  wording is from the Linux `hsearch(3)` man page. The *behaviour*
  (a second `hcreate()` silently destroys the first table) is
  therefore in unspecified territory rather than in violation, so no
  row above claims it either way. Recorded here because a fabricated
  spec citation is the kind of thing this ledger exists to catch, and
  because it is the only place in the header where the source's own
  reasoning does not survive checking.

- `hsearch()` returns `(ENTRY *)&table[i]`, casting a `struct slot *`
  to `ENTRY *`. The two share a prefix layout (`char *key`,
  `void *data`), so it works in practice, but it is a strict-aliasing
  violation rather than a guaranteed one. No POSIX clause is broken —
  the returned pointer does behave as the spec requires — so this is
  a latent-miscompile note, not a BUG row, and no assertion can
  distinguish it.

- On `ENTER` for a key already present, ntlibc leaves the existing
  entry's `data` alone. `hcreate.html` describes `action` only as
  "indicating the disposition of the entry **if it cannot be found**",
  so which of the two survives is unspecified; the existing test
  documents the choice.

### Not reached (search.h)

`hcreate()`/`hsearch()`'s optional `[ENOMEM]` and `tsearch()`'s "shall
return a null pointer if there is not enough space available to create
a new node" both need a real allocation failure, which this ledger
already records as unforceable for every other header. Nothing else in
this header is out of reach: it has no OS dependency at all.

## fenv.h (successor-queue item 2, group C)

Third of the twelve. `test/POSIX-GAP-ACCOUNTING.md` names `test/math.c`
as this header's test file; that is wrong — `test/math.c` asserts
nothing about `<fenv.h>` at all (its `fe` matches are substrings of
English prose in comments). The real coverage is in
`test/posix-math.c`'s `test_errhandling()`, which reaches most of the
header incidentally on its way to `math.h`'s `math_errhandling`
contract. This pass audited the eleven functions' own spec pages
against it.

**Oracle: pure C library, so Wine is a sound oracle** — with one
qualification worth stating, because it is the opposite of the usual
one. Nothing here makes an NT call, so Wine cannot diverge; but the
FPU *is* real hardware in both cases, and one row below turns on what
NT hands a thread at startup rather than on anything ntlibc or Wine
does. That row is written to compare against the environment the test
captures for itself rather than a hardcoded constant, so it holds on
real Windows too.

Rows below are what this pass *added* or *found*; `test_errhandling()`'s
existing coverage (macro values, the flags against real hardware
exceptions, one exceptflag round trip, one round round trip,
`feholdexcept`/`feupdateenv(&env)`) is not repeated.

| function | clause checked | status | test |
|---|---|---|---|
| feclearexcept | "shall return zero if the excepts argument is zero" — and shall clear nothing | covered | test/posix-math.c (`test_fenv_zero_argument`) |
| feraiseexcept | the same clause for a zero argument — and shall raise nothing | covered | test/posix-math.c (`test_fenv_zero_argument`) |
| feraiseexcept | "shall attempt to raise the supported floating-point exceptions represented by the excepts argument" — exactly those become set | covered | test/posix-math.c (`test_fenv_raise_exact_set`) |
| feraiseexcept | "Whether ... additionally raises the inexact ... whenever it raises the overflow or underflow ... is implementation-defined" | covered — this implementation's answer is "no", recorded rather than assumed | test/posix-math.c (`test_fenv_raise_exact_set`) |
| feraiseexcept | "the order in which these floating-point exceptions are raised is unspecified" | N/A — unspecified, and unobservable anyway while every exception is masked (which, per BUG 3 below, is not something this library actually guarantees) | — |
| fetestexcept | "the bitwise-inclusive OR of the ... macros corresponding to the currently set ... exceptions **included in excepts**" — the result is a subset of the argument, not the whole status word | covered | test/posix-math.c (`test_fenv_testexcept_subset`) |
| fegetexceptflag / fesetexceptflag | "shall return zero if excepts is zero" | covered | test/posix-math.c (`test_fenv_zero_argument`) |
| fesetexceptflag | "does not raise floating-point exceptions, but only sets the state of the flags" | N/A — `src/math/fenv.c` sets the sticky bits directly and never issues a floating-point operation, so no trap can be synthesised to observe; conforming by construction, and unobservable while BUG 3 leaves the mask state unmanaged | — |
| fegetexceptflag / fesetexceptflag | "shall have been set by a previous call to fegetexceptflag() whose second argument represented at least those exceptions" | N/A — a caller obligation; violating it is undefined, not a testable implementation requirement | — |
| fesetround | "If the argument is not equal to the value of a rounding direction macro, the rounding direction is not changed" | covered — all four macros, not just one; the existing test checked only that a bad value returns non-zero | test/posix-math.c (`test_fenv_round_modes`) |
| fegetround / fesetround | basedefs/fenv.h.html: the four rounding-direction macros have "distinct non-negative values" | covered | test/posix-math.c (`test_fenv_round_modes`) |
| fegetround / fesetround | the rounding direction is the one *arithmetic obeys* — a plain `double` division changes result across the modes | covered | test/posix-math.c (`test_fenv_round_affects_arithmetic`) |
| fegetround | "or a negative value if there is no such rounding direction macro or the current rounding direction is not determinable" | N/A — the x87/SSE rounding-control field is two bits and all four encodings name a defined macro, so no negative return is reachable | — |
| fegetenv / fesetenv | fesetenv "shall attempt to establish the floating-point environment represented by the object pointed to by envp" — a full round trip carrying both the rounding direction and the status flags | covered — nothing asserted `fesetenv()` directly before | test/posix-math.c (`test_fenv_env_roundtrip`) |
| fegetenv | "shall attempt to **store** the current floating-point environment in the object pointed to by envp" — a getter must not modify what it reads | covered — FIXED (6b0dbe2 fegetenv no longer masks); the defect is described in the narrative below, kept in past tense as the record | test/posix-math.c (`test_fenv_getenv_does_not_modify`) |
| feholdexcept | "install a non-stop (continue on floating-point exceptions) mode, if available, for all floating-point exceptions", and "shall return zero if and only if non-stop floating-point exception handling was successfully installed" | covered — FIXED (6b0dbe2 feholdexcept installs non-stop mode); the defect is described in the narrative below, kept in past tense as the record | test/posix-math.c (`test_fenv_holdexcept_installs_nonstop`) |
| feupdateenv | "save the currently raised floating-point exceptions ..., install the floating-point environment ..., and then attempt to **raise the saved** floating-point exceptions" | covered — against `FE_DFL_ENV`, which carries no status flags of its own, so anything set afterwards can only have come from the re-raise; the existing `feupdateenv(&env)` test cannot distinguish that from installing an environment that already had them | test/posix-math.c (`test_fenv_updateenv_reraises`) |
| `<fenv.h>` | "FE_DFL_ENV ... represents the default floating-point environment (that is, the one installed at program startup)" | covered — FIXED (a86082f FE_DFL_ENV captured at startup); the defect is described in the narrative below, kept in past tense as the record | test/posix-math.c (`test_fenv_dfl_env_is_startup_env`) |
| `<fenv.h>` | FE_ALL_EXCEPT is "the bitwise-inclusive OR of all floating-point exception macros defined by the implementation" | covered (pre-existing) | test/posix-math.c (`test_errhandling`) |

### Bugs found (fenv.h)

1. **`FE_DFL_ENV` is not the environment installed at program startup.**
   `basedefs/fenv.h.html` defines it as exactly that.
   `src/math/fenv.c` hardcodes an x87 control word of `0x037F`, taken
   from musl's Linux x86_64 fenv code where `0x037F` *is* the Linux
   startup value. NT starts a thread with `0x027F` — verified with a
   bare `-nostdlib` PE that does nothing but `fnstcw` at its entry
   point, so it is the kernel-supplied initial thread state and not
   something `crt/crt1.c` establishes (that file contains no FPU
   initialisation at all).

   The two differ in the precision-control field (bits 8-9): `0x027F`
   is 53-bit (double) precision, `0x037F` is 64-bit (extended). Every
   call to `fesetenv(FE_DFL_ENV)` — including the one inside
   `feupdateenv(FE_DFL_ENV)` — therefore silently widens x87
   precision, changing the double-rounding behaviour of every
   `src/math/x87.h` helper on both arches and of all plain `double`
   arithmetic on i386, where `include/fenv.h`'s own banner records
   that tcc emits x87 rather than SSE. The MXCSR half of `FE_DFL_ENV`
   is correct: `0x1F80` is measured to be the startup value.

   Test (fenced): `test_fenv_dfl_env_is_startup_env`. It compares
   against an environment `main()` captures for itself rather than a
   hardcoded word, so it is equally valid on real Windows.

2. **`fegetenv()` modifies the environment it is specified only to
   store.** `fegetenv.html`: "shall attempt to **store** the current
   floating-point environment in the object pointed to by envp."

   `src/math/fenv.c`'s `fegetenv()` is a bare `FNSTENV` with no
   restoring `FLDENV`. Per the Intel SDM's FSTENV/FNSTENV description
   the instruction, after saving, *masks all floating-point
   exceptions* — so `fegetenv()` silently masks every x87 exception as
   a side effect. glibc's x86 `fegetenv()` issues an `FLDENV` of the
   just-saved image for exactly this reason; musl's x86_64 version has
   the same defect ntlibc inherited. Measured: control word `0x027B`
   (divide-by-zero unmasked) before the call, `0x027F` after.

   Test (fenced): `test_fenv_getenv_does_not_modify`. It uses inline
   x86 asm, since POSIX provides no way to unmask an exception —
   safe, because fenced code is never compiled.

3. **`feholdexcept()` returns success without installing non-stop
   mode.** `feholdexcept.html` requires it to "install a non-stop
   (continue on floating-point exceptions) mode ... for all
   floating-point exceptions" and to "return zero **if and only if**
   non-stop floating-point exception handling was successfully
   installed."

   `src/math/fenv.c`'s implementation is `fegetenv()` plus
   `feclearexcept(FE_ALL_EXCEPT)` plus an unconditional `return 0`.
   Neither callee sets a mask bit. The x87 half is masked only by
   accident, via BUG 2's `FNSTENV` side effect — so fixing BUG 2 makes
   this one *worse*. MXCSR is never touched at all: `feclearexcept()`'s
   MXCSR path clears status bits 0-5 and never the mask bits at 7-12.
   On x86_64 that is the unit tcc emits every `double` operation into,
   so a caller who unmasked divide-by-zero and then called
   `feholdexcept()` expecting a non-stop region still takes a hardware
   exception on the first `1.0/0.0` — while `feholdexcept()` reported
   the success the RETURN VALUE clause makes conditional on precisely
   that not happening. Measured: x87 CW `0x027F` (masked, by accident)
   but MXCSR `0x1D80`, ZM still clear, return value 0.

   Test (fenced): `test_fenv_holdexcept_installs_nonstop`, inline asm
   for the same reason as BUG 2, and `#ifndef __i386__` because MXCSR
   is only part of `fenv_t` on the SSE arch.

### Observed behaviour where POSIX permits latitude (fenv.h)

- `FE_ALL_EXCEPT` is `0x3D`, the OR of exactly the five macros
  `include/fenv.h` defines. It therefore excludes the x86 denormal
  flag, which `feclearexcept(FE_ALL_EXCEPT)` consequently never
  clears. That is what the clause requires ("all floating-point
  exception macros **defined by the implementation**"), and is
  stricter than musl, which uses `63` and so names a bit it does not
  declare. Not a defect; recorded so it is not "fixed".

- `fegetround()` reads the x87 control word on both arches, while on
  x86_64 `double` arithmetic obeys MXCSR. Within this header's own API
  the two never diverge — `fesetround()` writes both and
  `fegetenv()`/`fesetenv()` round-trip both — so no clause is broken.
  They *can* be made to disagree by installing an `fenv_t` whose two
  rounding-control fields differ, which only foreign code touching one
  unit could produce. Recorded as a latitude note rather than a BUG
  row because no sequence of calls to this header alone can construct
  it.

### Not reached (fenv.h)

`feraiseexcept()`'s unspecified ordering, and the "does not raise, only
sets" guarantees of `fesetexceptflag()`/`fesetenv()`, all need an
*unmasked* exception to be observable — and BUG 3 means this library
does not manage mask state at all, so there is no supported way to
reach that state through the header. Every remaining clause is
covered.

## pwd.h / grp.h (successor-queue item 2, group D)

Fourth and fifth of the twelve. `test/pwd.c` and `test/posix-grp.c`
already covered these fairly thoroughly — the `_r` contract in
particular is implemented correctly and was already tested (0 with
`*result == NULL` for not-found, never -1, never via `errno`, `ERANGE`
for a short buffer). This pass read the ten spec pages against them and
found one defect plus a set of unasserted clauses.

**Oracle: NT-behaviour territory, but only weakly.** There is no POSIX
user database on this platform: both files synthesise a single record
from `%USERNAME%`/`%USERPROFILE%`/`%ComSpec%` and `getuid()`/
`getgid()`. So what Wine could diverge on is only what those
environment variables hold, which the tests do not depend on (every
assertion is gated on `have_user()`/`have_group()`, and both branches
are genuinely exercised — the native ASan harness starts with an empty
environ). The clause logic itself is pure ntlibc code.

Note from the fetched `pwd.h.html`: POSIX.1-2017 requires exactly
`pw_name`, `pw_uid`, `pw_gid`, `pw_dir`, `pw_shell`. `pw_passwd` and
`pw_gecos` are **not** required, so omitting them is conformant, not a
gap.

| function | clause checked | status | test |
|---|---|---|---|
| getpwuid / getpwnam / getgrgid / getgrnam | ERRORS lists exactly [EIO], [EINTR], [EMFILE], [ENFILE] for the non-`_r` forms; [ERANGE] is listed **only** for the `_r` forms | covered — FIXED (d79f011 grp [ERANGE]); the defect is described in the narrative below, kept in past tense as the record | test/pwd.c (`test_getpwuid_erange_not_in_its_errno_list`), test/posix-grp.c (`test_getgrgid_erange_not_in_its_errno_list`) |
| getpwuid / getgrgid / getpwnam / getgrnam | "A null pointer shall be returned if the requested entry is not found" — on an id that could not plausibly exist, not merely the adjacent one, so a fabricated-entry-for-any-argument failure would be caught | covered | test/pwd.c (`test_getpwuid_absurd_uid`), test/posix-grp.c (`test_getgrgid_absurd_gid`) |
| getpwuid_r / getgrgid_r / getpwnam_r / getgrnam_r | "shall return zero on success **or if the requested entry was not found and no error has occurred**" — on the same absurd id | covered | test/pwd.c (`test_getpwuid_r_absurd_uid`), test/posix-grp.c (`test_getgrgid_r_absurd_gid`) |
| getpwent / getgrent | "If the database is not already open, getpwent() shall open it and return ... the first entry" — so `endpwent()` then `getpwent()` must re-yield entry one rather than stay at end-of-file | covered — the existing tests call `end*ent()` only as their last statement and never read after it | test/pwd.c (`test_pwent_reopen_and_errno`), test/posix-grp.c (`test_grent_reopen_and_errno`) |
| setpwent / endpwent / setgrent / endgrent | "shall not change the setting of errno if successful" | covered | same two tests |
| getpwuid_r / getgrgid_r | "[ERANGE] Insufficient storage was supplied via buffer and bufsize" — at the *boundary*: one byte short must fail, exactly enough must succeed | covered — the existing tests use a one-byte buffer, which cannot tell a correct size computation from a blanket refusal | test/pwd.c (`test_getpwuid_r_erange_boundary`), test/posix-grp.c (`test_getgrgid_r_alignment_and_erange_boundary`) |
| getgrgid_r / getgrnam_r | grp.h.html: `gr_mem` is "a null-terminated array of character pointers to member names" — carved out of the *caller's* buffer, so it must be correctly aligned even when that buffer is not | covered — nothing had ever passed a deliberately misaligned buffer | test/posix-grp.c (`test_getgrgid_r_alignment_and_erange_boundary`) |
| getpwuid | `pw_name` agrees with `getlogin()` — `src/misc/pwd.c`'s `current_name()` is a private copy of `getlogin()`'s lookup, so the two can drift | covered | test/pwd.c (`test_pw_name_matches_getlogin`) |
| all fourteen | [EIO], [EINTR], [EMFILE], [ENFILE] | N/A — all four describe failures of *opening and reading a database file*; no file is ever opened, the record is built from environment variables, so no descriptor is consumed and no I/O can fail or be interrupted | — |
| getpwent / getgrent | the multi-entry enumeration a real database would have | N/A — NT has no POSIX user/group database; the degenerate one-entry enumeration is the honest whole of it, and the existing tests already pin that it terminates rather than looping | — |
| all fourteen | POSIX permits the returned pointer to be static storage a later call overwrites | covered (pre-existing) — the tests snapshot names rather than relying on it | test/pwd.c, test/posix-grp.c |

### Bugs found (pwd.h / grp.h)

1. **The non-`_r` lookups can set `[ERANGE]`, which POSIX lists only
   for the `_r` forms.** `getpwuid.html`/`getpwnam.html`/
   `getgrgid.html`/`getgrnam.html` list exactly [EIO], [EINTR],
   [EMFILE] and [ENFILE] for the non-`_r` forms; [ERANGE] appears only
   under the `_r` variants, where it means "insufficient storage was
   supplied via *buffer* and *bufsize*" — arguments the non-`_r` forms
   do not have.

   Mechanism: `src/misc/pwd.c`'s `getpwnam()`/`getpwuid()` and
   `src/misc/grp.c`'s `getgrnam()`/`getgrgid()` all pack into an
   *internal* static buffer and forward its overflow verbatim:
   `if (r == ERANGE) { errno = ERANGE; return 0; }`. `g_grbuf` is only
   256 + `sizeof g_grmem` = 272 bytes and `g_pwbuf` is 256 + 2*4096, so
   both are reachable by a program that sets its own `%USERNAME%` —
   no unusual NT configuration required. `getpwent()`/`getgrent()`
   inherit it, since they delegate to `getpwuid()`/`getgrgid()`, whose
   ERRORS lists are equally short.

   This is the "stub returning an errno that is not in its POSIX list"
   shape — the same class as `mkfifo()` answering `ENOSYS` — not a
   platform N/A. Fix shape (not applied): treat an internal-buffer
   overflow as "not found" (NULL, errno untouched), or size the static
   buffer so the case is unreachable and say so. One fix closes all
   six functions.

   Tests (fenced): `test_getpwuid_erange_not_in_its_errno_list`,
   `test_getgrgid_erange_not_in_its_errno_list`.

### Not reached (pwd.h / grp.h)

Nothing, beyond the four database-I/O errno values marked N/A above:
this platform has no user database to make them reachable, and no
second security principal to enumerate. Cross-binary agreement between
`src/misc/pwd.c`'s and `src/misc/grp.c`'s two independent private
copies of `current_name()` is likewise not asserted — the two tests are
separate binaries — though both now agree with `getlogin()`, which
constrains them jointly.

## regex.h (successor-queue item 2, group E)

Sixth of the twelve, and by a wide margin the one with the most
defects. `test/POSIX-GAP-ACCOUNTING.md` names `test/posix-parse.c` as
this header's test file; that is wrong — `test/posix-parse.c` is
`strtol`/`mktime`/`strftime` boundary tests and contains no regex at
all. The `<regex.h>` section lives in `test/posix-glob.c`, whose own
banner already listed back-references, collating symbols, interval
boundary counts and locale-dependent bracket expressions as unaudited.

**Oracle: pure C library, so Wine is a sound oracle.** `src/regex/`
makes no NT call; everything below was measured under Wine and would
measure identically anywhere.

Six BUGs and one UNIMPL, all fenced. The first is the serious one: a
`regexec()` that terminates the process.

| function | clause checked | status | test |
|---|---|---|---|
| regexec | "If regexec() finds a match, it shall return zero; otherwise, it shall return non-zero" — on a repeat whose body can match empty | **BUG (fenced)** — crashes the process | test/posix-glob.c (`test_regex_nullable_repeat_does_not_crash`) |
| regcomp | XBD 9.3.3: the asterisk is ordinary "As the first character of an entire BRE (after an initial `^`, if any)" | **BUG (fenced)** | test/posix-glob.c (`test_regex_bre_star_after_leading_circumflex`) |
| regcomp | REG_ICASE, "Ignore case in match" — inside a bracket expression's character classes | **BUG (fenced)** | test/posix-glob.c (`test_regex_icase_inside_character_class`) |
| regcomp | regex.h.html: REG_EESCAPE is "Trailing `<backslash>` character in pattern" | **BUG (fenced)** — a BRE gives REG_EPAREN | test/posix-glob.c (`test_regex_bre_trailing_backslash_code`) |
| regcomp | regex.h.html: REG_EBRACE is "`'\{\}'` imbalance", distinct from REG_BADBR's "Content of `'\{\}'` invalid" | **BUG (fenced)** — REG_EBRACE is never produced at all | test/posix-glob.c (`test_regex_ebrace_vs_badbr`) |
| regexec | XBD 9.1: "the search is for the longest of the leftmost matches" | **BUG (fenced)**, self-documented in `src/regex/regex.c`'s banner | test/posix-glob.c (`test_regex_leftmost_longest_alternation`) |
| regcomp | XBD 9.3.6 back-references `\1`..`\9` in a BRE | **UNIMPL (fenced)** — deliberately rejected, with a documented rationale | test/posix-glob.c (`test_regex_bre_backreference`) |
| regexec | pmatch[0] is the whole match; "Any unused elements of pmatch up to pmatch[nmatch-1] shall be filled with -1"; a non-participating subexpression gets -1, while one that participates and matches empty gets a real equal pair | covered | test/posix-glob.c (`test_regex_pmatch_fill_and_nonparticipating`) |
| regcomp / regexec | REG_NOSUB: "the nmatch and pmatch arguments to regexec() are ignored" — with an *oversized* nmatch, which `nmatch == 0` cannot distinguish from "nothing to fill" | covered | test/posix-glob.c (`test_regex_nosub_ignores_pmatch`) |
| regcomp | REG_NEWLINE's second and fourth requirements — "or by any form of a non-matching list", and the `$`-before-newline anchor — plus both anchors' "regardless of the setting of REG_NOTBOL/REG_NOTEOL" | covered — the existing test covered only the first and third | test/posix-glob.c (`test_regex_newline_full`) |
| regcomp | XBD 9.3.5 bracket-expression syntax: `]` first is literal, `-` first or last is literal, and the REG_ERANGE / REG_ECTYPE / REG_ECOLLATE / REG_EBRACK codes | covered | test/posix-glob.c (`test_regex_bracket_edges`) |
| regcomp | XBD 9.3.6/9.4.6 intervals in both grammars, including `{m}`, `{m,}`, `{m,n}`, a group with an interval, and BRE `{`/`}` being ordinary characters | covered | test/posix-glob.c (`test_regex_intervals`) |
| regcomp | XBD 9.3.3/9.3.8 vs 9.4.9: `^` and `$` are anchors only at the ends of a BRE but always special in an ERE | covered | test/posix-glob.c (`test_regex_bre_anchor_vs_literal`) |
| regerror | "shall return the size of the buffer needed to hold the entire generated string"; truncation must still null-terminate; "If errbuf_size is 0, regerror() ignores the errbuf argument" | covered — the existing test covers only the two-call size query, where an off-by-one would not show | test/posix-glob.c (`test_regex_regerror_truncation`) |
| regfree | after a *failed* regcomp | N/A — POSIX leaves `preg`'s state undefined after a regcomp failure, so this is an extension `src/regex/regex.c` provides rather than a requirement; asserted anyway, and recorded as an extension | test/posix-glob.c (`test_regex_regfree_after_failed_regcomp`) |
| regcomp | multi-character collating symbols `[.ch.]` and real equivalence classes `[=a=]` | N/A — `src/misc/locale.c` is C/POSIX-locale-only, and in the C locale every collating element is a single character, so there is no multi-character element for the syntax to name | — |
| regcomp / regexec | REG_ESPACE ("Out of memory") | N/A — needs a real allocation failure, unforceable here, as elsewhere in this ledger | — |

### Bugs found (regex.h)

1. **`regexec()` terminates the process on a repeat whose body can
   match the empty string.** `src/regex/regex.c`'s matcher recurses per
   alternation-split and per capture-save; a repeat body that consumes
   nothing produces a loop that makes no input progress, so the
   recursion is unbounded. The function's own `MAX_STEPS` guard counts
   *steps*, not depth, so the C stack is exhausted long before the
   counter trips. Measured under Wine: `(a*)*b` against a run of `a`s,
   `()*a` against `"a"`, and (via BUG 2) `^*a` against `"*a"` each kill
   the process. glibc, musl and the BSDs return normally for all three.

   This is reachable by any program that hands a user-supplied pattern
   to `regcomp()`, `src/sh/` included. Two independent defects — no
   depth bound in the matcher, and a progress-free loop from the
   compiler — and either fix alone stops the crash.

   The whole test is fenced, not merely its assertions: an unfenced
   crash would take `test/posix-glob.c`'s entire binary down and report
   with no indication of which clause was at fault.

   Test (fenced): `test_regex_nullable_repeat_does_not_crash`.

2. **BRE `*` immediately after a leading `^` is treated as a repeat
   operator.** XBD 9.3.3 makes the asterisk ordinary "As the first
   character of an entire BRE (after an initial `^`, if any)". The
   parenthetical is quoted verbatim in the comment directly above the
   line that gets it wrong: the suppression fires only when the first
   *atom* is itself the `*`, and in `^*a` the first atom is the anchor.
   Wrong parse (`^*a` must match a literal `"*a"` and must not match
   `"a"`), and — because starring a zero-width assertion builds a
   progress-free loop — the crash in BUG 1. The related `\(*a\)` case,
   `*` first inside a subexpression, is handled correctly.

   Test (fenced): `test_regex_bre_star_after_leading_circumflex`.

3. **`REG_ICASE` makes `[[:upper:]]` match nothing and `[^[:upper:]]`
   match everything.** The set-bit helper folds to lowercase under
   REG_ICASE and the test-bit helper always folds the subject byte, but
   the class emitter forces the fold *off* on the argument that
   "classes are their own fold". That holds for `alpha`, `alnum`,
   `print`, `graph` and `xdigit`, which contain both cases; it is false
   for `upper` and `lower`. `[[:upper:]]` therefore sets only the
   `'A'`..`'Z'` bits, which the always-folding test helper can never
   consult. `[:lower:]` survives by accident. Measured: `[[:upper:]]`
   under REG_ICASE gives REG_NOMATCH for both `"H"` and `"h"`, and its
   negation matches `"H"`. A silent wrong answer, which is the worst
   shape for this class of defect.

   Test (fenced): `test_regex_icase_inside_character_class`.

4. **A BRE ending in an unescaped backslash reports REG_EPAREN, not
   REG_EESCAPE.** The BRE branch parser treats a trailing backslash as
   end-of-branch without consuming it, and `regcomp()` then attributes
   the leftover input to parentheses. The ERE path is correct, so the
   two grammars disagree about the same malformed pattern. Measured:
   BRE `"a\"` → REG_EPAREN, ERE `"a\"` → REG_EESCAPE.

   Test (fenced): `test_regex_bre_trailing_backslash_code`.

5. **REG_EBRACE is unreachable; brace imbalance reports REG_BADBR.**
   `regex.h.html` distinguishes REG_EBRACE ("`'\{\}'` imbalance") from
   REG_BADBR ("Content of `'\{\}'` invalid: not a number, number too
   large, more than two numbers, first larger than second").
   `src/regex/regex.c` never assigns REG_EBRACE anywhere — the constant
   appears only in the error-message table — and both grammars' missing-
   closing-brace checks answer REG_BADBR. Measured: BRE `"a\{2"` and
   ERE `"a{2"` both give REG_BADBR, indistinguishable from `"a{3,2}"`,
   which is the one case where REG_BADBR is right.

   Test (fenced): `test_regex_ebrace_vs_badbr`.

6. **Alternation is leftmost-first, not leftmost-longest.** XBD 9.1:
   "the search is for the longest of the leftmost matches". The
   matcher returns on the first alternative that reaches a match.
   Measured: `"a|ab"` against `"ab"` reports 0,1 where POSIX requires
   0,2. Already documented in `src/regex/regex.c`'s own banner, which
   predicts this exact failure — so it is a known gap rather than a
   surprise, but it is a spec violation and gets a fenced test like the
   rest. Narrower than the banner implies, which is worth recording:
   greedy give-back, nested subexpression lengths and the classic
   `(wee|week)(knights|nights)` case were all measured correct; it is
   specifically the top-level alternation choice that does not back off
   to try a longer branch.

   Test (fenced): `test_regex_leftmost_longest_alternation`.

### UNIMPL found (regex.h)

1. **BRE back-references `\1`..`\9`.** XBD 9.3.6 specifies them;
   `src/regex/regex.c` rejects them outright with a documented
   rationale, which makes this UNIMPL rather than BUG on this
   project's own rule. Note the chosen error code is itself inapt:
   `regex.h.html` defines REG_ESUBREG as "Number in `'\digit'` invalid
   or in error", and `\1` after a real `\(...\)` is a valid
   back-reference, merely an unsupported one. EREs are unaffected —
   XBD 9.4 gives them no back-reference production, so treating ERE
   `"\1"` as a literal `1` is conforming.

   Test (fenced): `test_regex_bre_backreference`.

### Observed behaviour where POSIX permits latitude (regex.h)

- `src/regex/regex.c` accepts bare `+` and `?` as BRE repeat
  operators. XBD 9.3 makes them ordinary characters in a BRE, so this
  is a GNU-style leniency rather than conformance; it is documented
  in-code and one existing test depends on it. Not fenced, because
  writing the strict form as a failing test would also require
  rewriting that test's pattern — recorded here instead.
- `src/regex/regex.c`'s `MAX_STEPS` cap can in principle convert a
  legal match into REG_NOMATCH, for which no clause gives licence
  (`regexec()` may answer REG_ESPACE, but not a false REG_NOMATCH). No
  benign pattern reaching it was constructed, so there is no test; and
  per BUG 1 it does not in fact protect against the recursion blowup it
  was written to cap.

### Not reached (regex.h)

REG_ESPACE, and multi-character collating elements/equivalence classes
— both N/A above, for a malloc-exhaustion reason and a C-locale reason
respectively. Everything else in `regcomp.html` and XBD chapter 9 that
applies to this implementation now has a row.

## dlfcn.h (successor-queue item 2, group F)

Seventh of the twelve. `test/posix-dl.c` already covered `dlopen()`'s
two mode flags, the `dlclose()` refcount chain and `dlerror()`'s
one-shot contract — that last one is the design most likely to be got
wrong and it is implemented correctly. This pass read the five spec
pages against it.

**Oracle: NT-behaviour territory, and Wine is weak evidence.** Wine's
`LdrLoadDll`/`LdrGetProcedureAddress`/`LdrUnloadDll` are an independent
implementation of real NT's loader. Every assertion added below was
chosen so that it is decided by *ntlibc's own code* — the `__rpath`
search, the path-normalisation join, the error-sequence bookkeeping,
the NULL-handle guards — rather than by loader behaviour the two could
disagree about. Deliberately *not* asserted, and named here so the
omission is not mistaken for an oversight: the case-insensitive
module-identity variant of the single-copy clause, and anything
involving a synthetic non-NULL garbage handle. The `windows-test` CI
legs remain the authority for the pre-existing refcount rows.

| function | clause checked | status | test |
|---|---|---|---|
| dlerror | "If no dynamic linking errors have occurred since the last invocation of dlerror(), dlerror() shall return NULL" — after a *successful* `dlopen()` | **BUG (fenced)** — see below | test/posix-dl.c (`test_dlerror_null_after_successful_dlopen`) |
| dlopen / dlsym | "a global symbol table handle ... shall provide access to the symbols from an ordered set of executable object files consisting of the original program image file, any executable object files loaded at program start-up ..., and the set ... loaded ... with the RTLD_GLOBAL flag" | **BUG (fenced)** — see below | test/posix-dl.c (`test_dlopen_null_global_symbol_set`) |
| dlopen | "If file contains a `<slash>` character, the file argument is used as the pathname for the file" — for a *relative* slash-containing name | **BUG (fenced)**, knowing deviation — see below | test/posix-dl.c (`test_dlopen_relative_pathname_uses_cwd`) |
| dlopen | "If file is a null pointer, dlopen() shall return a global symbol table handle for the currently running process image" — the handle itself | covered | test/posix-dl.c (`test_dlopen_null_returns_a_handle`) |
| dlopen | "Otherwise, file is used in an implementation-defined manner to yield a pathname" — the bare-name case | covered (this half genuinely is implementation-defined, and ntlibc's `__rpath`-only policy is conforming) | test/posix-dl.c (existing `test_dl_underlying_mechanism`, plus the new `__rpath` shape) |
| dlopen | "Only a single copy ... shall be brought into the address space ... **even if different pathnames are used**" | covered — via forward-slash normalisation, which `src/internal/rpath.c` does itself | test/posix-dl.c (`test_dlopen_single_copy_different_pathnames`) |
| dlsym | "if the symbol named by name cannot be found ... dlsym() shall return a null pointer. More detailed diagnostic information shall be available through dlerror()" — through the `<dlfcn.h>` surface, not the internal rpath layer | covered | test/posix-dl.c (`test_dlsym_failure_through_dlfcn`) |
| dlsym / dlerror | the clear-then-call-then-check idiom the spec gives for disambiguating a NULL return from a symbol whose value is NULL | covered | test/posix-dl.c (`test_dlsym_failure_through_dlfcn`) |
| dlsym | "If handle does not refer to a valid symbol table handle ... shall return a null pointer" — the NULL handle only | covered | test/posix-dl.c (`test_dlsym_failure_through_dlfcn`) |
| dlclose | "If handle does not refer to an open symbol table handle ... dlclose() shall return a non-zero value. More detailed diagnostic information shall be available through dlerror()" | covered | test/posix-dl.c (`test_dlclose_invalid_handle`) |
| dlerror | "shall return a null-terminated character string (with no trailing `<newline>`) that describes the last error that occurred" | covered — length, no trailing newline, and that the message names the file that failed | test/posix-dl.c (`test_dlerror_message_shape`) |
| `<dlfcn.h>` | "shall define the following symbolic constants ...: RTLD_LAZY, RTLD_NOW, RTLD_GLOBAL, RTLD_LOCAL" — each independently representable, since the spec's own examples OR them together | covered | test/posix-dl.c (`test_dlfcn_header_constants`) |
| dlopen | RTLD_LOCAL: "symbols shall not be made available for relocation processing of any other executable object file" | N/A — no NT loader primitive narrows a mapped module's export directory out of another module's import resolution; already fenced in the file before this pass | test/posix-dl.c (`test_dlopen_rtld_local_scoping`) |
| dlopen | RTLD_GLOBAL's first sentence: "symbols shall be made available for relocation processing of any other executable object file" | N/A — unconditionally true on NT; there is no primitive to make it *not* so | — |
| dlopen | mode validation | N/A — recorded because it is easy to assume the opposite: `dlopen.html` defines no errors at all ("No errors are defined"), imposes no requirement that exactly one of RTLD_LAZY/RTLD_NOW be given, and specifies no [EINVAL]. glibc's rejection of an invalid mode is an extension, so ntlibc accepting any mode is conforming and no test asserts a rejection | — |
| dlsym | symbol-name decoration | N/A — PE export directories store undecorated names on both i386 and x86-64, unlike Mach-O's leading underscore, so no decoration layer is needed or possible to get wrong | — |
| dlsym | using a handle after `dlclose()` | N/A — the spec constrains the *application* here, not the implementation; asserting on it would be asserting on undefined behaviour, and on real NT on a possibly-unmapped base | — |
| dlerror | "It is implementation-defined whether or not the dlerror() function is thread-safe" | N/A — ntlibc has no threads | — |
| `<dlfcn.h>` | RTLD_NEXT / RTLD_DEFAULT | N/A — not POSIX.1-2017 base (glibc extensions); `include/dlfcn.h` deliberately omits them, which is conforming | — |

### Bugs found (dlfcn.h)

1. **A successful `dlopen()` can leave a pending `dlerror()`.**
   `src/internal/rpath.c` walks `__rpath` entry by entry and records an
   error on each miss, bumping the sequence counter
   `src/dlfcn/dlfcn.c`'s `dlerror()` uses to decide whether an error is
   outstanding. When an early entry misses and a later one hits,
   `dlopen()` returns a valid handle with the counter already bumped,
   and the next `dlerror()` reports a failure the caller never
   experienced. `src/dlfcn/dlfcn.c`'s own comment states the opposite
   as its correctness argument — "A successful
   dlopen()/dlsym()/dlclose() call never bumps that counter" — which is
   what made it worth checking.

   This also breaks the spec's recommended `dlsym()` disambiguation
   idiom for every later call, since the stale error is still
   outstanding when the caller clears-and-calls. Measured under Wine;
   not Wine-specific, since the failing `__rpath` entry is a directory
   that exists nowhere.

   Test (fenced): `test_dlerror_null_after_successful_dlopen`. Note
   this test is why `test/posix-dl.c`'s `__rpath` is now a two-entry
   array rather than empty — it is the only shape that reaches the
   per-entry error recording on a call that then succeeds, and it
   changes nothing for the path-qualified loads, which never consult
   `__rpath`.

2. **`dlopen(NULL, ...)` plus `dlsym()` searches only the executable,
   not the ordered set POSIX requires.** `src/dlfcn/dlfcn.c` returns
   the PEB's `ImageBaseAddress`, and `dlsym()` hands that to
   `LdrGetProcedureAddress()`, which answers only "does *this one
   module* export this name". The start-up-loaded modules and the
   RTLD_GLOBAL set are never searched. Measured: `dlsym(g,
   "RtlAllocateHeap")` is NULL, and ntdll.dll is unambiguously loaded
   at program start-up on both Wine and real NT.

   Classified BUG rather than N/A because the NT mechanism is present
   in this very tree: `PEB_LDR_DATA`, `InLoadOrderModuleList` and
   `LDR_DATA_TABLE_ENTRY` are already declared in `src/internal/nt.h`,
   and walking `InLoadOrderModuleList` trying `LdrGetProcedureAddress`
   on each `DllBase` is precisely POSIX's load-order search.
   `src/dlfcn/dlfcn.c`'s comments discuss only the narrower point that
   a `-nostdlib` tcc EXE has an empty export directory, and never
   address the "ordered set" requirement.

   Test (fenced): `test_dlopen_null_global_symbol_set`.

3. **A relative pathname containing a slash resolves against the image
   directory, not the working directory.** `dlopen.html`'s
   implementation-defined latitude covers only the *no-slash* case: "If
   file contains a `<slash>` character, the file argument is used as
   the pathname for the file." `src/internal/rpath.c` joins such a name
   onto the image directory instead, and `include/ntlibc/rpath.h` says
   so outright.

   **The behaviour is not what should change here.** The security
   rationale (a CWD-relative load is attacker-controllable in a way an
   `$ORIGIN`-relative one is not) is sound. What is wrong is the
   conformance claim: the surrounding comments cite the
   implementation-defined clause as licensing the deviation, and it
   does not — it licenses only the bare-name half of ntlibc's policy,
   which genuinely is fully conforming. Recorded as a knowing
   deviation so the ledger does not carry it as conformance.

   Test (fenced, with no runnable body on purpose):
   `test_dlopen_relative_pathname_uses_cwd`.

### Observed behaviour where POSIX permits latitude (dlfcn.h)

- RTLD_LAZY is honoured as RTLD_NOW. `dlopen.html` puts relocation
  timing under RTLD_LAZY at "an implementation-defined time, ranging
  from the time of the dlopen() call until the first reference", so
  eager resolution is inside the specified range. Conforming.
- `dlclose()` on the main-image handle always returns 0 without
  unloading. `dlclose.html` says the implementation "may unload", so
  this is permitted. Latent: a program that also `dlopen()`s its own
  image by path increments the loader's count and never decrements it,
  because the short-circuit fires first. Harmless (the main image is
  not unloadable anyway) and not usefully assertable, so it has no row.
- `dlclose(NULL)`'s `dlerror()` message renders with an empty string
  before the colon. The spec requires only a null-terminated non-NULL
  string, so this is conforming, just unhelpful.

### Not reached (dlfcn.h)

RTLD_LOCAL isolation and RTLD_GLOBAL's relocation half (both N/A on
the NT loader), and everything Wine cannot be trusted on — the
case-insensitive module-identity form of the single-copy clause, and
`dlclose()`/`dlsym()` with a synthetic non-NULL handle. Those last two
are unreached by choice, not by impossibility: they are real clauses
that only the `windows-test` CI legs could settle, and asserting them
here would pin Wine's answer rather than NT's.

## glob.h / fnmatch.h / wordexp.h (successor-queue item 2, group G)

Eighth, ninth and tenth of the twelve, audited together because
`glob()` is specified in terms of the same pattern-matching notation
`fnmatch()` is, and `wordexp()` is specified in terms of `glob()`.
`test/posix-glob.c` already covered the common path of all five
functions. This pass read `glob.html`, `fnmatch.html`, `wordexp.html`,
their three basedefs pages and XCU 2.6/2.13 against it.

**Oracle: pure C library, so Wine is a sound oracle for `fnmatch()`
and `wordexp()`.** `glob()` is the exception: it reads real
directories through `opendir()`/`readdir()`/`stat()`, so its rows carry
the same NT caveat as the rest of the filesystem surface — but the
defects found are in pattern handling and result assembly, not in the
directory walk, and were all reproduced against ordinary files and
directories that Wine and NT agree about.

Seven BUGs fenced, and three previously-fenced clauses unfenced
because they turned out to be implemented and passing.

| function | clause checked | status | test |
|---|---|---|---|
| fnmatch | XCU 2.13.1: "Otherwise, the `<left-square-bracket>` shall match the character itself" — an unterminated `[` | covered — FIXED (3edf110 unterminated '[' is literal) | test/posix-glob.c (`test_fnmatch_unmatched_bracket_is_literal`) |
| fnmatch | XCU 2.13.3's leading-`<period>` rule in the FNM_PERIOD-without-FNM_PATHNAME form, and against a non-matching list / range / character class rather than only `*` and `?` | covered | test/posix-glob.c (`test_fnmatch_period_forms`) |
| fnmatch | XBD 9.3.5: `]` first in the list is literal; `-` first or last is literal | covered | test/posix-glob.c (`test_fnmatch_bracket_edges`) |
| fnmatch | a `<newline>` in a bracket expression | covered — and recorded, because it is easy to misremember the opposite: neither XCU 2.13 nor `fnmatch.html` restricts `<newline>` at all. That rule belongs to `REG_NEWLINE`, a `<regex.h>` flag with no fnmatch counterpart | test/posix-glob.c (`test_fnmatch_bracket_edges`) |
| fnmatch | FNM_PATHNAME's bracket-and-slash interaction (XCU 2.13.3's "the open bracket shall be treated as an ordinary character") | N/A for fnmatch — `fnmatch.html` incorporates only XCU 2.13.1 and 2.13.2; that sentence is in 2.13.3, which `glob()` incorporates and `fnmatch()` does not. `fnmatch.html`'s own FNM_PATHNAME text says only that a `<slash>` "shall not be matched ... by a bracket expression", which ntlibc honours and the existing test covers | — |
| glob | APPLICATION USAGE: "The new pathnames generated by a subsequent call with GLOB_APPEND are not sorted together with the previous pathnames" | covered — FIXED (767ab77 GLOB_APPEND sorting) | test/posix-glob.c (`test_glob_append_does_not_resort`) |
| glob | "[GLOB_NOMATCH] The pattern does not match any existing pathname" — for an empty pattern | covered — FIXED (3d2a6b6 empty pattern) | test/posix-glob.c (`test_glob_empty_pattern`) |
| glob | "GLOB_MARK: Each pathname that is a directory that matches pattern shall have a `<slash>` appended" — for a pattern that itself ends in a slash | covered — FIXED (ba54185 trailing-slash pathname) | test/posix-glob.c (`test_glob_mark_trailing_slash_pattern`) |
| glob | XCU 2.14.3 rule 1 (2.13.3 in POSIX.1-2017/2004): "If a `<slash>` character is found following an unescaped `<left-square-bracket>` before a corresponding `<right-square-bracket>` ... the open bracket shall be treated as an ordinary character. ... It only matches a pathname of literally a[b/c]d" | covered — was fenced as a BUG asserting the OPPOSITE answer; the clause requires a MATCH and the fence was inverted, see below | test/posix-glob.c (`test_glob_bracket_containing_slash`) |
| glob | "If a filename begins with a `<period>`, the `<period>` shall be explicitly matched" | covered | test/posix-glob.c (`test_glob_leading_period`) |
| globfree | "shall free any space associated with pglob from a previous call to glob()" — and leave the structure safe for a second call | covered | test/posix-glob.c (`test_globfree_idempotent`) |
| glob | GLOB_NOCHECK returning the pattern verbatim | covered (pre-existing) — and recorded from the fetched text: the 2017 edition says nothing about removing backslash escapes from the returned pattern, so returning it verbatim is conforming | test/posix-glob.c (`test_glob_nocheck`) |
| glob | GLOB_ERR / `errfunc` / GLOB_ABORTED | N/A (pre-existing fence) — the unreadable-directory fixture cannot be built here; chmod 0 does not revoke owner access under this platform's permission model | test/posix-glob.c (`test_glob_err_callback`) |
| glob | GLOB_NOSPACE | N/A — needs a real allocation failure. See "Observed behaviour" below for a defect found by inspection on this path that no assertion can reach | — |
| glob | GLOB_NOSORT | N/A — the resulting order "is unspecified", so there is nothing to assert | — |
| wordexp | XCU 2.6 step 2: "Field splitting ... shall be performed on the portions of the fields generated by step 1" | **BUG (fenced)** | test/posix-glob.c (`test_wordexp_field_splits_expansion_result`) |
| wordexp | XCU 2.6.5: "the shell shall treat each character of the IFS as a delimiter", and "If the value of IFS is null, no field splitting shall be performed" | **BUG (fenced)** | test/posix-glob.c (`test_wordexp_honours_ifs`) |
| wordexp | XCU 2.6: "If the complete expansion appropriate for a word results in an empty field, that empty field shall be deleted from the list of fields ... unless the original word contained single-quote or double-quote characters" | **BUG (fenced)** — the exception is implemented; the rule it is an exception to is not | test/posix-glob.c (`test_wordexp_empty_field_deleted`) |
| wordexp | "wordexp() shall fail, and the number of expanded words shall be 0" on WRDE_BADCHAR without WRDE_APPEND | **BUG (fenced)** | test/posix-glob.c (`test_wordexp_wordc_zero_on_badchar`) |
| wordexp | WRDE_BADCHAR's full character list, WRDE_NOCMD → WRDE_CMDSUB (including the backquote form and the quoted-substitution non-case), and field splitting of literal input | covered — **unfenced this session**; all three were implemented and passing, and had been fenced only because they shared a function with the command substitution that genuinely needs a shell | test/posix-glob.c (`test_wordexp_badchar_nocmd_and_literal_splitting`) |
| wordexp | "In other error cases, if the WRDE_APPEND flag was specified, we_wordc and we_wordv shall not be modified" | covered — the complement of the WRDE_BADCHAR clause above; the two cover the two flag states and are not in tension | test/posix-glob.c (`test_wordexp_append_preserved_on_error`) |
| wordexp | "[WRDE_SYNTAX] Shell syntax error, such as unbalanced parentheses or unterminated string" — all five paths | covered | test/posix-glob.c (`test_wordexp_syntax_errors`) |
| wordexp | WRDE_REUSE: "The result shall be the same as if the application had called wordfree() and then called wordexp() without WRDE_REUSE"; WRDE_APPEND's "in the same order as before"; `we_wordv` NULL-termination | covered — the existing test checks the APPEND total but not the order, which is the half `glob()` gets wrong | test/posix-glob.c (`test_wordexp_reuse_and_append_order`) |
| wordexp | WRDE_UNDEF → WRDE_BADVAL inside an arithmetic expansion, and the complement (an undefined name is zero without the flag) | covered | test/posix-glob.c (`test_wordexp_undef_in_arithmetic`) |
| wordexp | XBD 2.6.4 Arithmetic Expansion → XBD 1.1.2 → ISO C 6.5.7p3: a shift whose count is negative or is not less than the width of the promoted left operand is *undefined*, so 2.6.4 specifies no result and an implementation may not simply perform the shift | **covered (bug fixed)** — `src/wordexp/arith.c`'s `apply_binop()` evaluated `cur << rhs` / `cur >> rhs` with no bound on `rhs` at all, for `<<`/`>>` and for the `<<=`/`>>=` compound forms, while the `/` and `%` cases beside it already guarded their own operand-dependent UB. Found by fuzz/fuzz_wordexp.c driving `__wordexp_arith()`; UBSan (`tools/asan-build.sh`, `-fno-sanitize-recover`) aborted the process on `$((1<<-1))`, `$((1>>-1))`, `$((1<<64))` and `$((1>>64))`, which is why the test had to be fenced whole rather than per-assertion. Fixed by `shift_count_ok()`, which refuses a count outside `[0, LONG_BIT)` with WRDE_SYNTAX — the code the sibling zero-divisor guard already uses — and by routing the in-range left shift through `unsigned long` so 6.5.7p4's overflowing shift wraps like every other overflow in that file. The ceiling is `LONG_BIT` (32 on both arches) rather than `sizeof(long) * CHAR_BIT`, so it is the same in the native sanitizer build, where the host compiler's `long` is 64 bits | test/posix-glob.c (`test_wordexp_arith_shift_bounds`) — both directions, both spellings, both ends of the accepted range, and a short-circuited branch that must not reach the guard at all |
| wordexp | performing command substitution | N/A (pre-existing fence, now narrowed to just this) — no shell on this platform parses `$(...)`; `src/stdio/misc.c` hands shell work to `cmd.exe` | test/posix-glob.c (`test_wordexp_cmdsub_needs_a_shell`) |
| wordexp | WRDE_SHOWERR: "Do not redirect stderr to /dev/null" | N/A — there is no subprocess whose stderr could be redirected, since command substitution always fails first | — |
| wordexp | an unquoted `<newline>` as a WRDE_BADCHAR character | N/A — **genuinely ambiguous in the spec**: the same character is required to be IFS whitespace by XCU 2.6.5 and is listed under WRDE_BADCHAR, and the qualifier is "in an *inappropriate* context". ntlibc treats it as a field separator. Deliberately not asserted either way rather than pinning one reading | — |

### Bugs found (glob.h / fnmatch.h / wordexp.h)

1. **`fnmatch()` treats an unterminated `[` as a bracket expression
   instead of a literal `[`.** XCU 2.13.1: "Otherwise, the
   `<left-square-bracket>` shall match the character itself."
   `src/fnmatch/fnmatch.c`'s bracket scanner walks to the end of the
   pattern looking for a `]` and, not finding one, returns the
   accumulated state anyway. Measured: `"[abc"` vs `"[abc"`, `"a[b"`
   vs `"a[b"` and `"["` vs `"["` all give FNM_NOMATCH where POSIX
   requires 0. Note the deliberate contrast with the
   regular-expression grammar, where the same input is an error
   (REG_EBRACK — see the `regex.h` section): the two pattern languages
   part company here.

2. **`glob()` re-sorts a `GLOB_APPEND` call's results together with the
   previous call's.** `src/glob/glob.c` sorts the whole vector at the
   end of every call. Measured over `a.txt`/`b.txt`/`d.log`:
   `glob("*.log", 0)` then `glob("*.txt", GLOB_APPEND)` yields
   `a.txt b.txt d.log` where POSIX requires `d.log a.txt b.txt`. The
   existing test checks only `gl_pathc`. `wordexp()`'s WRDE_APPEND, by
   contrast, gets this right.

3. **`glob("")` returns 0 with `"."` as a match.** An empty pattern
   names no pathname, so `[GLOB_NOMATCH]` is required.
   `src/glob/glob.c`'s pattern-exhausted branch assumes it was reached
   mid-recursion after a directory prefix was confirmed and synthesises
   `"."` for an empty prefix — handing back a pathname the caller never
   asked about, as a success.

4. **`GLOB_MARK` drops the slash when the pattern itself ends in one.**
   The clause is about what the pathname *is*, not how the pattern
   named it. A trailing-slash pattern exits through the branch that
   strips the slash and never consults the flag. Measured:
   `glob("subdir/", GLOB_MARK)` returns `"subdir"`. The existing test
   covers only the wildcard path, which goes through a different branch
   and is correct.

5. **`glob()` and a bracket expression spanning a `<slash>` — NOT a
   bug; this entry recorded the wrong expected answer and is kept as
   the correction.** XCU 2.14.3 rule 1 (numbered 2.13.3 in
   POSIX.1-2017/2018 and POSIX.1-2004; normative text unchanged)
   requires an unescaped `[` whose bracket expression would span a
   `<slash>` to be demoted to an ordinary character, and says what
   follows from that: "the pattern `a[b/c]d` does not match such
   pathnames as `abd` or `a/d`. **It only matches a pathname of
   literally `a[b/c]d`.**" The Rationale (C.2.14.3) makes the XSI
   behaviour — matching that pathname — *required*.

   So the correct answer is a MATCH: the pathname `a[b/c]d` is the
   directory `a[b` containing the file `c]d`. `src/glob/glob.c` splits
   on every `/` and, with the brackets ordinary, arrives there
   correctly. Confirmed against glibc.

   The entry previously read as a latent BUG whose expected answer was
   `GLOB_NOMATCH`, reasoning that "no POSIX filename may contain a
   `<slash>`". That conflates **filename** with **pathname**: no
   filename may contain a slash, which is precisely why the slash
   separates the two components the pathname is made of. `glob()`'s
   DESCRIPTION makes only rule 3 optional, so rule 1 is mandatory and
   there was never an out on that ground either.

   The test is now live and asserts the match, plus the two non-matches
   the standard names by hand (`abd`, `a/d`) — a bracket-stripping
   implementation would wrongly match one of them. It remains the
   regression guard for the fnmatch unmatched-bracket fix: reverting
   that one alone makes this file's fnmatch assertions fail.

6. **`wordexp()` never field-splits the *result* of an expansion.**
   XCU 2.6 step 2 requires splitting "on the portions of the fields
   generated by step 1"; `src/wordexp/wordexp.c` splits the input text
   instead, and a parameter's value is appended at a point the scanner
   has already passed. Measured with `V="a b"`: `wordexp("$V")` gives
   one word `"a b"` where POSIX requires two. The double-quoted form
   correctly gives one word, so the quoting half of the rule is right
   and only the splitting half is missing. `include/wordexp.h`
   describes IFS field splitting as implemented, which is what made
   this worth checking.

7. **`IFS` is ignored.** `src/wordexp/wordexp.c` hardcodes space, tab
   and newline and never reads `IFS`, so neither half of XCU 2.6.5
   holds: a custom `IFS` does not split, and a null `IFS` does not
   suppress splitting. Recorded as BUG rather than UNIMPL because
   `include/wordexp.h` presents this as implemented; leaving it out is
   a legitimate choice, but then the header has to say so. Strictly
   harder than BUG 6 and subsuming it — fixing `IFS` handling without
   fixing *where* splitting is applied would still get `$V` wrong.

8. **An empty expansion produces a spurious empty word.** XCU 2.6: an
   empty field "shall be deleted from the list of fields ... unless the
   original word contained single-quote or double-quote characters".
   `src/wordexp/wordexp.c` marks the word active before expanding.
   Measured: `wordexp("$UNSET")` gives one empty word where POSIX
   requires none, and `wordexp("x $UNSET y")` gives three words where
   POSIX requires two. The quoted exception is implemented correctly —
   so the exception exists and the rule it is an exception to does not.

9. **`we_wordc` is not zeroed on a non-`WRDE_APPEND` failure.**
   `wordexp.html`: on WRDE_BADCHAR "wordexp() shall fail, and the
   number of expanded words shall be 0." The failure path leaves the
   caller's structure untouched for every error but WRDE_NOSPACE. The
   "shall not be modified" caveat elsewhere on the page is explicitly
   conditioned on WRDE_APPEND having been given, and that case is
   separately covered and passing.

### Observed behaviour where POSIX permits latitude (glob/fnmatch/wordexp)

- **`glob()` can report a false `GLOB_ABORTED` under `GLOB_ERR`.**
  `src/glob/glob.c` clears `errno` around its `readdir()` loop, but
  every `continue` in the loop body skips the clear — in particular the
  one taken when a per-entry `stat()` fails — so a leftover `ENOENT`
  from a dangling entry can be misread as a `readdir()` failure once
  the loop ends. Order-dependent, hence intermittent. Recorded here
  rather than as a BUG row because the fixture that makes it
  deterministic is a directory full of dangling links, which is the
  same fixture `test_glob_err_callback` is already fenced for being
  unable to build on this platform. Fix shape: save `errno` around the
  `readdir()` call itself rather than sampling it after the loop.
- **`glob()` discards `finish()`'s `GLOB_NOSPACE` return** at all
  three call sites; on the `GLOB_APPEND` path that returns 0 with a
  `gl_pathv` pointing at memory `finish()` already freed. Found by
  inspection; not assertable without an allocation-failure injection
  hook, which is the same limit `test_glob_nospace_and_free` already
  documents. Recorded so it is not lost.
- **`test/posix-glob.c` declares its own `FNM_*`/`GLOB_*`/`WRDE_*`
  macros and `glob_t`/`wordexp_t` rather than including the shipped
  headers** — an artifact of having been written before those headers
  existed. Every value was compared against `include/fnmatch.h`,
  `include/glob.h` and `include/wordexp.h` this session and all match,
  so nothing here is testing the wrong flag today. But the shipped
  headers are not exercised by this file at all, and a change to one
  of them would desync silently. Not restructured here (the local
  `typedef`s would collide with the real ones); recorded as a
  hygiene gap.

### Not reached (glob.h / fnmatch.h / wordexp.h)

`GLOB_ERR`/`errfunc`/`GLOB_ABORTED` and `GLOB_NOESCAPE` (both
pre-existing fences, for fixtures this platform's permission model and
filename rules cannot build), `GLOB_NOSPACE`/`WRDE_NOSPACE` (real
allocation failure), `GLOB_NOSORT` (unspecified order), command
substitution (no shell), and the unquoted-`<newline>` question, which
is left unasserted because the spec itself is ambiguous rather than
because this platform cannot reach it.

## ctype.h, the twelve `is*` pages (successor-queue item 2, group H)

`test/POSIX-GAP-ACCOUNTING.md`'s "Implemented, not clause-audited"
table names twelve `<ctype.h>` functions — `isalnum isalpha isblank
iscntrl isdigit isgraph islower isprint ispunct isspace isupper
isxdigit` — with the note that priority 4 above "audited the `is*`
family as a group and cites `isascii`/`toascii`/`tolower`/`toupper`/
`_tolower`/`_toupper` by name; these twelve are the individual pages it
does not". That is exactly right, and worth spelling out, because it is
the shape of gap this ledger keeps rediscovering.

**What the earlier group audit actually covered.** `test/ctype.c` (the
file priority 4 leans on) is a *consistency* test: it builds a single
oracle out of C range expressions — `c >= 'A' && c <= 'Z'`,
`c >= 0x20 && c < 0x7f`, `(unsigned)c-'\t' < 5`-shaped reasoning — and
checks all sixteen `<ctype.h>` entry points against it in one loop over
`-1..255`. It is a good test and it passes. But its oracle is written
in the same idiom as `src/ctype/*.c`'s implementations, so a
misremembered range would have to be misremembered *identically* in two
places to be caught, and it never opens the twelve individual spec
pages: nothing in it cites a clause, and nothing in it asserts the
DESCRIPTION domain sentence, the "non-zero, not 1" wording of RETURN
VALUE, or the ERRORS section.

**What this pass adds** (`test/posix-ctype.c`, new file):

- Every oracle is an *enumeration* of the characters XBD 7.3.1
  `LC_CTYPE` puts in that class in the POSIX locale, written out
  character by character as a string literal, sharing no arithmetic with
  `src/ctype/*.c`. The one class that cannot be written as a C string
  literal — `cntrl`, because it contains NUL — gets an explicit closed
  numeric range instead.
- The domain sentence is asserted as a domain: all 257 values of
  `{EOF} U [0, UCHAR_MAX]` are swept through each function, with `EOF`
  and both `unsigned char` edges also called out individually.
- RETURN VALUE's "non-zero" is respected literally: every true-side
  assertion is `!!f(c) == 1`-shaped, never `f(c) == 1`, so an
  implementation returning any other true value still passes. The
  false side is asserted exactly, because 0 is specified exactly.
- ERRORS ("No errors are defined." on all twelve pages) gets its own
  assertion: a sentinel `errno`, the whole domain swept through all
  twelve functions, and the sentinel must survive.
- An explicitly non-asserting out-of-domain probe, for ASan. See below.

No BUGs. All twelve are conformant over the whole domain.

| function | clause checked | status | test |
|---|---|---|---|
| isalpha | DESCRIPTION "class alpha in the current locale" vs the XBD 7.3.1 POSIX-locale enumeration; RETURN VALUE non-zero/0; domain `{EOF} U [0,UCHAR_MAX]` | covered | test/posix-ctype.c (`test_isalpha`) |
| isupper | DESCRIPTION "class upper"; same three clauses | covered | test/posix-ctype.c (`test_isupper`) |
| islower | DESCRIPTION "class lower"; same three clauses | covered | test/posix-ctype.c (`test_islower`) |
| isdigit | DESCRIPTION "class digit" — the one class XBD 7.3.1 fixes in *every* locale as exactly `0`-`9`; `'0'-1` and `'9'+1` asserted as the adjacent non-members | covered | test/posix-ctype.c (`test_isdigit`) |
| isalnum | DESCRIPTION "class alpha **or** digit" — asserted both as an enumeration and as that stated union, over the whole domain | covered | test/posix-ctype.c (`test_isalnum`) |
| isxdigit | DESCRIPTION "a hexadecimal digit"; `'g'`/`'G'` asserted as the adjacent non-members | covered | test/posix-ctype.c (`test_isxdigit`) |
| isspace | DESCRIPTION "class space" — XBD 7.3.1's six characters, each asserted by name, plus the two control characters bracketing the `\t`-`\r` run | covered | test/posix-ctype.c (`test_isspace`) |
| isblank | DESCRIPTION "class blank" — `<space>` and `<tab>` only; the strict-subset relation to `space` asserted via `\n` | covered | test/posix-ctype.c (`test_isblank`) |
| iscntrl | DESCRIPTION "class cntrl"; NUL asserted separately (no string literal can carry it); XBD 7.3.1's cntrl/print disjointness asserted over the whole domain | covered | test/posix-ctype.c (`test_iscntrl`) |
| isprint | DESCRIPTION "class print" — `alnum + punct + <space>`, i.e. `0x20`-`0x7e`; `0x7f` and `0x1f` asserted as the adjacent non-members | covered | test/posix-ctype.c (`test_isprint`) |
| isgraph | DESCRIPTION "class graph" — asserted as an enumeration *and* as "print minus `<space>`" over the whole domain | covered | test/posix-ctype.c (`test_isgraph`) |
| ispunct | DESCRIPTION "class punct" — enumerated from XBD 7.3.1 rather than derived, so a defect in `isgraph()`/`isalnum()` cannot cancel against a matching one here; the derived relation asserted separately as a cross-check | covered | test/posix-ctype.c (`test_ispunct`) |
| all twelve | ERRORS: "No errors are defined." | covered | test/posix-ctype.c (`test_no_errors_defined`) |
| all twelve | DESCRIPTION: "If the argument has any other value, the behavior is undefined" | N/A — undefined by the spec, so nothing may be asserted about the *result*. Repurposed as an ASan assertion instead; see below | test/posix-ctype.c (`test_out_of_domain_probe`) |

### Observed behaviour where POSIX permits latitude (ctype.h)

- **Every byte in `0x80`-`0xff` is in every function's domain and in no
  class.** These values are "representable as an `unsigned char`", so
  the domain sentence puts them squarely *inside* the defined domain —
  they are not the undefined case. XBD 7.3.1 defines the POSIX locale's
  classes only over the portable character set, so the required answer
  is 0 for all twelve functions across all 128 of them. `src/ctype/*.c`
  answers 0, and the sweep asserts it for each. Worth recording because
  it is the half of the domain most easily mistaken for "undefined".
- **`src/ctype/*.c` are pure arithmetic on `(unsigned)c`, with no
  lookup table anywhere.** That is why the out-of-domain probe passes
  today and why it is worth keeping: the classic implementation of this
  family is a 257-entry table indexed by `c + 1`, and the classic bug
  is an out-of-domain argument indexing outside it — a silent
  buffer overflow no return-value assertion can see, because the
  return value is undefined for exactly those arguments.
  `test_out_of_domain_probe()` calls all twelve across `INT_MIN`,
  `INT_MAX`, `EOF - 1` and `UCHAR_MAX + 1` and **deliberately asserts
  nothing about the results**; under `make asan` (tools/asan-build.sh
  runs every test that links natively) it is ASan, not the assertion
  count, that does the checking. Verified out-of-band that ASan does
  catch the shape: a stand-in `static const unsigned char tbl[257]`
  indexed by `c + 1` and handed this same probe's argument list dies
  with `AddressSanitizer: SEGV` on the first out-of-domain value.
- `isalnum()`, `ispunct()` and `isxdigit()` are the three that are
  *implemented* in terms of their siblings (`isalpha||isdigit`,
  `isgraph&&!isalnum`, `isdigit||...`). POSIX permits this — the
  APPLICATION USAGE tables even state the equivalences — but it is
  precisely why their oracles here are independent enumerations rather
  than the same composition.

### Not reached (ctype.h)

Nothing. All three clause sections of all twelve pages are asserted
over the entire defined domain; the only unasserted clause is the
undefined-behaviour sentence, which cannot be asserted by construction
and is covered as an ASan probe instead.

The `_l` variants (`isalnum_l` and the other eleven, all `CX`) are
declared by neither `include/ctype.h` nor any `src/` file, so they are
a `POSIX-GAP-ACCOUNTING.md` matter (missing interfaces), not a row
here.

## wctype.h, the sixteen wide-character pages (successor-queue item 2, group I)

`test/POSIX-GAP-ACCOUNTING.md` lists sixteen `<wctype.h>` functions as
implemented but never clause-audited: `iswalnum iswalpha iswblank
iswcntrl iswctype iswdigit iswgraph iswlower iswprint iswpunct iswspace
iswupper iswxdigit towctrans towlower towupper`.

**What existed before.** `test/posix-wchar.c` gained a `<wctype.h>`
block when that header landed (2026-08-23) and calls fourteen of the
sixteen. It is a smoke test — a dozen single-character spot checks, plus
`WEOF` and a lone surrogate — not a clause audit; it cites no clause per
assertion, sweeps no domain, and its only `iswctype()` checks use one
class name. It is left exactly as it is; the new `test/posix-wctype.c`
does the pages. The two overlap the way a smoke test and an audit are
meant to.

**The domain, which is the clause most easily got wrong.** All thirteen
`isw*` pages carry the same sentence: the argument "shall ... [be] a
wide-character code corresponding to a valid character in the locale
used by the function, or equal to the value of the macro WEOF. If the
argument has any other value, the behavior is undefined." The locale is
always the POSIX locale here, and XBD 7.3.1 defines that locale's
character set as the portable character set — so the *defined* domain is
exactly U+0000..U+007F plus `WEOF`, 129 values, and that is what the
sweeps cover. Everything from 0x80 up, surrogate halves and values above
`WCHAR_MAX` included, is undefined by the spec; this file asserts
nothing about it *as a POSIX requirement*.

It does assert it as an **ntlibc promise**, separately and labelled
(`test_documented_extension`): `include/wctype.h`'s banner commits in
writing to 0 from every classification function and the argument
unchanged from every conversion function across that whole undefined
region, with no special-casing of surrogate halves. A libc that promises
its callers a defined answer for the undefined region owes them a test
of it; what it does not owe is a claim POSIX demanded it.

No BUGs. All sixteen are conformant over the defined domain, and the
documented extension holds across every probe value.

| function | clause checked | status | test |
|---|---|---|---|
| iswalpha | DESCRIPTION "class alpha in the current locale" vs XBD 7.3.1's POSIX-locale enumeration; domain U+0000..U+007F + `WEOF`; RETURN VALUE non-zero/0 | covered | test/posix-wctype.c (`test_iswalpha`) |
| iswupper | DESCRIPTION "class upper"; same three clauses | covered | test/posix-wctype.c (`test_iswupper`) |
| iswlower | DESCRIPTION "class lower"; same three clauses | covered | test/posix-wctype.c (`test_iswlower`) |
| iswdigit | DESCRIPTION "class digit" — fixed by XBD 7.3.1 as exactly `0`-`9` in every locale; U+002F and U+003A asserted as the adjacent non-members | covered | test/posix-wctype.c (`test_iswdigit`) |
| iswalnum | DESCRIPTION "class alpha **or** digit" — asserted as an enumeration and as that stated union, whole domain | covered | test/posix-wctype.c (`test_iswalnum`) |
| iswxdigit | DESCRIPTION "a hexadecimal digit"; `g`/`G` asserted as the adjacent non-members | covered | test/posix-wctype.c (`test_iswxdigit`) |
| iswspace | DESCRIPTION "class space" — XBD 7.3.1's six characters, each by name | covered | test/posix-wctype.c (`test_iswspace`) |
| iswblank | DESCRIPTION "class blank" — `<space>`/`<tab>` only; strict-subset relation to `space` asserted | covered | test/posix-wctype.c (`test_iswblank`) |
| iswcntrl | DESCRIPTION "class cntrl"; U+0000 asserted separately; XBD 7.3.1's cntrl/print disjointness asserted over the whole domain | covered | test/posix-wctype.c (`test_iswcntrl`) |
| iswprint | DESCRIPTION "class print"; U+007F and U+001F asserted as the adjacent non-members | covered | test/posix-wctype.c (`test_iswprint`) |
| iswgraph | DESCRIPTION "class graph" — asserted as an enumeration *and* as "print minus `<space>`" over the whole domain | covered | test/posix-wctype.c (`test_iswgraph`) |
| iswpunct | DESCRIPTION "class punct" — enumerated from XBD 7.3.1, with the derived relation asserted separately as a cross-check | covered | test/posix-wctype.c (`test_iswpunct`) |
| wctype | RETURN VALUE: a usable non-zero value for each of the twelve reserved class names, `(wctype_t)0` for an invalid name; distinct handles for distinct classes; stable across calls. Case-sensitivity and a trailing-space name asserted as invalid | covered | test/posix-wctype.c (`test_wctype`) |
| iswctype | RETURN VALUE "non-zero (true) if and only if wc has the property described by charclass"; CX "If charclass is `(wctype_t)0`, these functions shall return 0" — asserted for a `wc` that is in every other class, so a "return the class regardless" bug cannot pass | covered | test/posix-wctype.c (`test_iswctype`) |
| iswctype | APPLICATION USAGE's twelve-row equivalence table (`iswalnum(wc)` ≡ `iswctype(wc, wctype("alnum"))`, …) — **all twelve rows, over the whole defined domain**, against both the sibling function and XBD 7.3.1 directly | covered | test/posix-wctype.c (`test_iswctype_equivalence_table`) |
| iswctype | "If the value of charclass is invalid … the result is unspecified" | N/A — unspecified, so no result may be asserted. The one adjacent thing that *is* specified, the `(wctype_t)0` case, is asserted above; `wctype()` returns exactly that for an invalid name | test/posix-wctype.c (`test_iswctype`) |
| towlower | DESCRIPTION/RETURN VALUE: the corresponding lowercase code for an uppercase argument, "All other arguments in the domain are returned unchanged" — asserted for all 128 defined characters plus `WEOF`, against XBD 7.3.1's `tolower` mapping | covered | test/posix-wctype.c (`test_towlower`) |
| towupper | the mirror image, plus round-trip inverseness on the cased range | covered | test/posix-wctype.c (`test_towupper`) |
| wctrans | "the following character mapping names are defined in all locales: tolower toupper"; "shall return 0 … if the given character mapping name is not valid"; distinct, stable handles | covered | test/posix-wctype.c (`test_wctrans`) |
| towctrans | RETURN VALUE "shall return the mapped value of wc using the mapping described by desc. Otherwise, they shall return wc unchanged"; APPLICATION USAGE's two equivalences, whole domain | covered | test/posix-wctype.c (`test_towctrans`) |
| towctrans | "If the value of desc is invalid … the result is unspecified", and the *may fail* `[EINVAL]` | N/A — unspecified result, optional error. `src/ctype/towctrans.c` documents choosing "return `wc` unchanged", which is also the page's own stated fallback, so that choice is asserted; the absent `[EINVAL]` is not a defect because the error is *may fail*, not *shall fail* | test/posix-wctype.c (`test_towctrans`) |
| all thirteen isw*, towlower, towupper | ERRORS: "No errors are defined." | covered | test/posix-wctype.c (`test_no_errors_defined`) |
| all sixteen | the whole undefined region outside U+0000..U+007F ∪ {`WEOF`} | N/A as a POSIX clause (undefined). Asserted instead against `include/wctype.h`'s own written commitment, in its own labelled section | test/posix-wctype.c (`test_documented_extension`) |

### Observed behaviour where POSIX permits latitude (wctype.h)

- **The equivalence table is the assertion that matters most in this
  header, and it had a real failure mode.** `src/ctype/wctype.c` hands
  out a 1-based index into a twelve-entry `classes[]` array;
  `src/ctype/iswctype.c` consumes it with a hand-written twelve-case
  switch. Two lists, kept in step by nothing but sitting near each other
  in the tree. Transposing any two entries in either leaves both files
  compiling and every pre-existing assertion passing — `test/posix-
  wchar.c`'s only `iswctype()` checks use `"digit"` — while
  `iswctype(wc, wctype("alpha"))` quietly answers `iswblank()`.
  Confirmed by doing it: transposing `"lower"`/`"print"` in `classes[]`,
  and separately rewiring one case of the switch, are each caught by
  `test_iswctype_equivalence_table()` and by nothing else in the tree.
- **The `isw*` family is implemented by delegation to the `is*`
  family** (`src/ctype/iswalpha.c` is `return isalpha((int)wc);`, and so
  on for eleven others). That is sound here and worth recording as
  deliberate: in a locale whose character set is the portable character
  set, the two families are required to agree, and the cast is safe for
  the whole `wint_t` range because `src/ctype/*.c` are total arithmetic
  functions rather than table lookups. It does mean the two families
  share a fate; that is why `test/posix-ctype.c` and this file assert
  against independent XBD 7.3.1 enumerations rather than against each
  other.
- **`WEOF` is `0xffffffffU`, `wint_t` is `unsigned`, `wchar_t` is 16-bit
  `unsigned short`.** `WEOF` is therefore not representable as a
  `wchar_t`, which is what `towlower.html`'s domain sentence requires,
  and `(int)WEOF` is `-1`, which is `EOF` — so the delegation above
  lands `WEOF` on the one `is*` domain member that is also required to
  answer 0. Correct, but by a coincidence of two independent choices
  rather than by construction, so it is recorded here.

### Not reached (wctype.h)

Nothing in the defined domain. The three unasserted clauses are all
spec-unspecified or spec-optional and are listed as N/A rows above:
`iswctype()` with an invalid `charclass`, `towctrans()` with an invalid
`desc`, and `towctrans()`'s *may fail* `[EINVAL]`.

The `_l` variants (`iswalnum_l` and the other fifteen, all `CX`) are
declared by neither `include/wctype.h` nor any `src/` file, so they are
a `POSIX-GAP-ACCOUNTING.md` matter, not a row here.

## The long tail of small headers (successor-queue item 2, group J)

`test/POSIX-GAP-ACCOUNTING.md`'s "Implemented, not clause-audited"
table has a tail of one- and two-function headers that no priority in
the list above ever reached: `locale.h`'s locale-object API,
`sys/uio.h`, `ftw.h`, two `fcntl.h` advisory functions, `setjmp.h`'s
`OB XSI` pair, `string.h`'s `strlen`/`strnlen`, `sys/times.h`,
`sys/utsname.h`, `sys/time.h`'s `gettimeofday`, `stdlib.h`'s `srand48`,
and `stropts.h`'s `ioctl`. Nineteen functions across eleven headers.
They are audited here as one group, in three subsections, because they
share nothing but their size — and two of them carry a judgement call
big enough to want its own heading.

### J1: locale.h -- the locale-object API

`newlocale`, `duplocale`, `freelocale`, `uselocale`
(`test/posix-locale.c`, new file). `setlocale`/`localeconv`, the other
two `<locale.h>` names, were audited in priority 4 and are untouched.

`src/misc/locale.c`'s whole locale-object half is one immutable,
stateless, file-scope `struct __locale_struct` handed out for
everything, with `newlocale()` ignoring `category_mask` and `base`,
`uselocale()` ignoring its argument, `duplocale()` returning that same
static, and `freelocale()` a no-op. ntlibc is C/POSIX-locale-only
(`setlocale()` in the same file accepts no other name), so that shape
is not obviously wrong — and the whole point of this subsection is that
**the four functions do not get the same verdict.** Two are correct for
such a libc by a real mechanism; two are unimplemented contract, and
one of those actively misleads a caller.

**Two BUGs fenced, both verified to pass once fixed** (see "Bugs found"
below for the proof procedure).

| function | clause checked | status | test |
|---|---|---|---|
| newlocale | DESCRIPTION: `"C"`, `"POSIX"` and `""` "are defined for all settings of category_mask"; RETURN VALUE: a handle usable "on subsequent calls to duplocale(), freelocale()" | covered — asserted for `LC_ALL_MASK`, a single category mask, a two-category mask and mask 0 | test/posix-locale.c (`test_newlocale_accepts_the_preset_names`) |
| newlocale | ERRORS, *shall fail*: "[ENOENT] For any of the categories in category_mask, the locale data is not available" — including that the name comparison is exact (`"c"` is not `"C"`) | covered | test/posix-locale.c (`test_newlocale_enoent`) |
| newlocale | DESCRIPTION: "If the function call fails and the base argument is not (locale_t)0, the contents of base shall remain valid and unchanged" | covered — vacuous in the implementation (`base` is never read), but the observable half, that `base` is still a usable handle after a failed call, is asserted | test/posix-locale.c (`test_newlocale_base_unchanged_on_failure`) |
| newlocale | ERRORS, *shall fail*: "[EINVAL] The category_mask contains a bit that does not correspond to a valid category" | covered — FIXED (99fc6f8 category_mask validation) | test/posix-locale.c (`test_newlocale_einval_on_invalid_mask`) |
| newlocale | ERRORS, *may fail*: "[EINVAL] The locale argument is not a valid string pointer" | N/A — *may* fail, so not implementing it conforms. ntlibc accepts a null `name` and treats it as `"C"`; not asserted either way | — |
| duplocale | DESCRIPTION "shall create a duplicate copy of the locale object"; RETURN VALUE "a handle for a new locale object", usable wherever a `newlocale()` handle is | **N/A (single immutable stateless locale object)** — see below | test/posix-locale.c (`test_duplocale`) |
| duplocale | DESCRIPTION: "If the locobj argument is LC_GLOBAL_LOCALE, duplocale() shall create a new locale object containing a copy of the global locale determined by the setlocale() function" | covered — and separately correct: the global locale is unconditionally `"C"` here, asserted through `setlocale(LC_ALL, NULL)` | test/posix-locale.c (`test_duplocale`) |
| freelocale | DESCRIPTION "shall cause the resources allocated for a locale object ... to be released"; RETURN VALUE "None"; ERRORS "None" | **N/A (nothing is ever allocated)** — see below | test/posix-locale.c (`test_freelocale`) |
| freelocale | "Any use of a locale object that has been freed results in undefined behavior" | N/A — undefined, so nothing may be required. It is also what makes `duplocale()`'s aliasing harmless rather than dangerous here | — |
| uselocale | DESCRIPTION: "If the newloc argument is (locale_t)0, the current locale shall not be changed; this value can be used to query the current locale setting" | covered | test/posix-locale.c (`test_uselocale_query_does_not_change`) |
| uselocale | DESCRIPTION: installing a locale object as the thread-local locale, and `LC_GLOBAL_LOCALE` uninstalling it | covered | test/posix-locale.c (`test_uselocale_install_and_uninstall`) |
| uselocale | RETURN VALUE: "... or LC_GLOBAL_LOCALE if no thread-local locale was in use" | **BUG (fenced)** | test/posix-locale.c (`test_uselocale_reports_lc_global_locale`) |
| uselocale | ERRORS, *may fail*: "[EINVAL] newloc is not a valid locale object and is not (locale_t)0" | N/A — *may* fail. Recorded explicitly so its absence is not mistaken for the *shall*-fail gap in `newlocale()` above | — |
| newlocale, duplocale | ERRORS, *shall fail*: "[ENOMEM]" | N/A — needs a real allocation failure, and neither function allocates anything to begin with. Same limit `glob()`'s `GLOB_NOSPACE` row records | — |

### Bugs found (locale.h)

1. **`newlocale()` never validates `category_mask`.**
   `newlocale.html` ERRORS, *shall fail* (not *may*): "[EINVAL] The
   category_mask contains a bit that does not correspond to a valid
   category." DESCRIPTION defines the valid bits as the six named
   `LC_*_MASK` constants "or any of the implementation-defined mask
   values defined in `<locale.h>`"; `include/locale.h` defines those
   six plus `LC_ALL_MASK` (`0x7fffffff`), so bit 31 corresponds to no
   category under any reading. `src/misc/locale.c`'s `newlocale()`
   opens with `(void)mask;` and never looks at it again. Measured:
   `newlocale(0x40000000|(1<<31), "C", 0)` returns a non-null handle
   with `errno` untouched. **Not excused by being C-locale-only** —
   validating a bitmask needs no locale data, and a *shall fail* clause
   is what a caller relies on to detect its own bad argument. Same
   defect class as the six unimplemented shall-fail argument checks the
   never-asserted-name sweep found.

2. **`uselocale()` cannot report "no thread-local locale is in use",
   so the one question the interface exists to answer cannot be
   answered.** `uselocale.html` RETURN VALUE: "shall return a handle
   for the thread-local locale that was in use ... **or
   LC_GLOBAL_LOCALE if no thread-local locale was in use**." ntlibc
   never installs a thread-local locale and stores nothing, so that
   condition is true on entry to every call ever made and
   `LC_GLOBAL_LOCALE` is the required answer every time; it returns
   `&__c_locale` instead. Measured: `uselocale((locale_t)0)` returns
   `0x41d7c8` while `LC_GLOBAL_LOCALE` is `(locale_t)-1`.

   **Why this is a BUG where `freelocale()`'s no-op is N/A.** The
   failure is not "an unused constant came back wrong".
   `uselocale(0) == LC_GLOBAL_LOCALE` is the documented way for a
   program to ask *am I on the global locale?*, and here that question
   always answers "no" when the truth is always "yes". The standard
   save/restore idiom — `old = uselocale(loc); ...; uselocale(old);` —
   therefore cannot distinguish "put me back on the global locale"
   from "put me back on that locale object", and silently does the
   wrong one rather than failing. A caller is misled; that is the line
   this audit draws between BUG and N/A for a C-locale-only libc.

   A correct fix is small but is **not** "return `LC_GLOBAL_LOCALE`
   unconditionally": once `uselocale(loc)` has been called a
   thread-local locale *is* in use and a later query must report it,
   which the live `test_uselocale_install_and_uninstall` already
   asserts. One word of state (`static locale_t current =
   LC_GLOBAL_LOCALE;`, assigned when the argument is non-zero)
   satisfies both.

**Both fences were verified to pass once fixed**, not merely asserted
to. The two fixes above were applied to `src/misc/locale.c`, both
blocks un-fenced, and the file rebuilt and rerun: green. Restoring the
original implementation with the tests still un-fenced turns exactly
the seven expected assertions red (four `newlocale`, three
`uselocale`), after which both were re-fenced and `src/misc/locale.c`
reverted. Nothing in `src/` is modified by this commit.

### Observed behaviour where POSIX permits latitude (locale.h)

- **The two N/A verdicts are mechanism arguments, not convenience
  ones,** which is the bar this ledger sets. `freelocale()`: no
  resource is ever allocated for a locale object — both constructors
  return the address of one file-scope static — so there is nothing to
  release and a no-op is the complete implementation of the clause
  rather than a placeholder for one. `duplocale()`: the object is
  immutable and carries no per-object state (`struct
  __locale_struct { int dummy; };`), so a "duplicate copy" is
  indistinguishable from the original by every means POSIX defines —
  no field to read, none to change on one copy and observe on the
  other — and freeing the copy cannot damage the original because
  freeing releases nothing. POSIX never promises two handles compare
  unequal, so returning the same pointer is the same object, correctly.
- **`LC_ALL_MASK` is `0x7fffffff`,** not the union of the six defined
  category masks (`0x3f`). That is `<locale.h>`'s own choice and POSIX
  permits "implementation-defined mask values defined in `<locale.h>`",
  so it is conforming — but it does mean bits 6-30 are nominally valid
  categories with nothing behind them, and it narrows the [EINVAL]
  clause above to bit 31 alone. Recorded because a stricter
  `LC_ALL_MASK` would widen that clause considerably, and whoever fixes
  BUG 1 should decide deliberately which of the two they are
  implementing.

### J2: stropts.h -- ioctl(), and a ledger row that should not exist

`test/POSIX-GAP-ACCOUNTING.md` lists `ioctl` (`OB XSR`) under
"Implemented, not clause-audited", with the note "`src/ioctl/ioctl.c`
implements the name, not the STREAMS semantics POSIX attaches to it".
**Reading `ioctl.html` shows the note is right and the row it sits in
is wrong.**

POSIX's `ioctl()` is not implemented here at all. A different function
that shares its name is:

| | POSIX `<stropts.h>` | ntlibc `<sys/ioctl.h>` |
|---|---|---|
| header | `stropts.h` — absent here | `sys/ioctl.h` — not a POSIX header |
| signature | `int ioctl(int, int, ...)` | `int ioctl(int, unsigned long, ...)` |
| specified over | STREAMS devices | NT file/pipe/console handles |
| command set | `I_PUSH I_POP I_LOOK I_FLUSH I_SETSIG I_FIND I_PEEK …` | `FIONREAD TIOCGWINSZ FIONBIO` |

They share a name, an `fd` parameter, and nothing else — disjoint
headers, disjoint command sets, and POSIX's own text says of everything
ntlibc's version does that "for non-STREAMS devices, the functions
performed by this call are unspecified". `include/sys/ioctl.h`'s banner
opens by saying so itself: "ioctl(): NOT a POSIX interface — POSIX
deliberately specifies termios(3) … instead of a general ioctl(2)".

**Verdict: reclassify, do not fence.** `ioctl` belongs in
`POSIX-GAP-ACCOUNTING.md`'s *absent* accounting, next to the other
headers ntlibc does not have, rather than in "Implemented, not
clause-audited". Fencing an UNIMPL inside a row that should not exist
would have recorded the symptom and preserved the miscategorisation.
This was checked against the alternative framing (that ntlibc
implements POSIX's `ioctl` with the wrong header and parameter type)
and rejected: nothing in `src/ioctl/ioctl.c` is an attempt at the
STREAMS interface, so there is no partial implementation to grade.

| function | clause checked | status | test |
|---|---|---|---|
| ioctl | ERRORS, general conditions: "[EBADF] The fildes argument is not a valid open file descriptor" — the one clause on the page **not** conditioned on `fildes` referring to a STREAMS device | covered — asserted for `-1`, an out-of-range descriptor, and a descriptor that was open and has been closed | test/posix-stropts.c (`test_ebadf`) |
| ioctl | SYNOPSIS: `#include <stropts.h>`, `int ioctl(int fildes, int request, ...)` — and the whole of `basedefs/stropts.h.html` (the eight structures, the `I_*`/`S_*`/`FLUSH*` constants, `FMNAMESZ`, and `isastream`/`getmsg`/`getpmsg`/`putmsg`/`putpmsg`) | **UNIMPL (fenced)** — `grep` over `include/` and `src/` finds no `stropts.h`, no `I_PUSH`, no `FMNAMESZ`, no `isastream`. A strictly conforming application does not compile. Deliberately UNIMPL, not N/A: nothing about NT prevents shipping a header of constants and structures | test/posix-stropts.c (`test_stropts_header_exists`) |
| ioctl | the STREAMS command set (`I_PUSH` … `I_PUNLINK`) and every per-command `[EINVAL]`/`[ENXIO]`/`[EAGAIN]`/`[ENOSR]`, plus the general `[EINTR]`, `[EIO]`, `[ENOTTY]`, `[ENXIO]`, `[ENODEV]` and the "linked downstream from a multiplexer" `[EINVAL]` | **N/A: NT has no STREAMS subsystem, so `fildes` can never refer to a STREAMS device** — see below | — |

### Observed behaviour where POSIX permits latitude (stropts.h)

- **The STREAMS N/A is a scope that cannot be entered, not "NT is
  different".** `ioctl.html` DESCRIPTION opens "The ioctl() function
  shall perform a variety of control functions on STREAMS devices" and
  immediately adds "For non-STREAMS devices, the functions performed by
  this call are unspecified." Every clause below that sentence is
  conditioned on `fildes` referring to a STREAMS device, or on a STREAM
  linked downstream from a multiplexer. NT has no STREAMS driver, no
  way to open a device as a STREAM, and no module to push onto one — so
  those clauses are **vacuous rather than violated**, and everything
  ntlibc's `ioctl()` does (`FIONREAD`/`TIOCGWINSZ`/`FIONBIO` on
  ordinary NT handles) falls squarely in the region POSIX explicitly
  leaves unspecified. Emulating STREAMS in user space would not make
  the clauses apply either: they are about the STREAM a *device driver*
  provides, and a userspace shim over NT handles would be one more
  non-STREAMS device.
- **The BSD `ioctl()` ntlibc does ship is deliberately not audited
  against `ioctl.html` beyond `[EBADF]`.** It is not the function the
  page specifies, and asserting whatever the code happens to do would
  be exactly the "audit the implementation instead of the spec" failure
  this ledger exists to avoid. Its own behaviour — which three requests
  are real, what `FIONREAD` answers for a pipe versus a regular file,
  and that an unrecognised request fails `EINVAL` rather than silently
  succeeding — is documented in `src/ioctl/ioctl.c`'s banner. Giving
  *that* function real tests of its own is a separate, non-POSIX job
  and is not done here.
- The STREAMS N/A is recorded as a comment in `test/posix-stropts.c`
  rather than as a fenced test, because there is no assertion to write:
  a test that cannot construct a STREAMS device cannot assert anything
  about one, even fenced.

### J3: sys/uio.h, ftw.h, fcntl.h advisory, setjmp.h OB XSI, string.h, sys/times.h, sys/utsname.h, sys/time.h, stdlib.h srand48

Fourteen functions across nine headers, in `test/posix-tail.c` (new
file). **Five BUGs fenced** — one in `nftw()` that is severe, one more
in `nftw()` found by inspection, and three more *shall fail* error
clauses that are simply absent. **One assertion group comes out
`rc=77` unverified** here.

| function | clause checked | status | test |
|---|---|---|---|
| readv | "shall place the input data into the iovcnt buffers … shall always fill an area completely before proceeding to the next" — asserted on the exact iov boundaries, not just the total | covered | test/posix-tail.c (`test_readv`) |
| readv | "Refer to read" — 0 at end-of-file; a short transfer reporting the bytes that really moved and leaving later areas untouched; zero-length areas skipped rather than read as EOF | covered | test/posix-tail.c (`test_readv`) |
| readv, writev | *may fail* "[EINVAL] The iovcnt argument was less than or equal to 0, or greater than {IOV_MAX}" — and the *upper edge accepted*: `iovcnt == IOV_MAX` must succeed, not be rejected off by one | covered | test/posix-tail.c (`test_readv_writev_iovcnt`) |
| readv, writev | *shall fail* "[EINVAL] The sum of the iov_len values … overflowed an ssize_t", and writev's "the operation shall fail and no data shall be transferred" (asserted through `st_size`) | covered | test/posix-tail.c (`test_writev`) |
| writev | "shall gather output data from the iovcnt buffers … iov[0], iov[1], …" in order; "If fildes refers to a regular file and all of the iov_len members … are 0, writev() shall return 0 and have no other effect" | covered | test/posix-tail.c (`test_writev`) |
| readv, writev | "Refer to read"/"Refer to write" `[EBADF]` | covered | test/posix-tail.c (`test_writev`) |
| readv, writev | XBD 2.9.7's atomicity requirement against other `read`/`write`/`readv`/`writev` on a regular file | N/A — `src/misc/uio.c`'s banner documents this as a deliberate, argued divergence (NT's `NtReadFileScatter`/`NtWriteFileGather` are page-granular and cannot take arbitrary iovecs). No test: ntlibc has no threads to race with | — |
| ftw | "shall recursively descend the directory hierarchy rooted in path. For each object … shall call the function pointed to by fn"; FTW_D/FTW_F; "shall visit a directory before visiting any of its descendants"; a real `stat` buffer (checked through `st_size`) | covered | test/posix-tail.c (`test_ftw`) |
| ftw | "If the function pointed to by fn returns a non-zero value, ftw() shall stop its tree traversal and return whatever value was returned" — asserted for a positive and a negative value, with the entry count | covered | test/posix-tail.c (`test_ftw`) |
| ftw | "The ndirs argument shall specify the maximum number of directory streams or file descriptors … available" — a two-level walk with `ndirs == 1` must still complete (exercises `src/ftw/ftw.c`'s LRU close/`telldir`/`seekdir` replay) | covered | test/posix-tail.c (`test_ftw`) |
| ftw, nftw | *shall fail* "[ENOENT] A component of path does not name an existing file or path is an empty string"; "[ENOTDIR] A component of path names an existing file that is neither a directory nor a symbolic link to a directory" | covered | test/posix-tail.c (`test_ftw`, `test_nftw`) |
| nftw | `struct FTW`: "base is the offset of the object's filename in the pathname passed as the first argument to fn"; "level indicates depth relative to the root of the walk, where the root level is 0" — every entry, exact values, and `path + base` compared against the filename | covered | test/posix-tail.c (`test_nftw`) |
| nftw | "FTW_DEPTH: If set, nftw() shall report all files in a directory before reporting the directory itself", and FTW_DP "shall only occur if the FTW_DEPTH flag is included in flags" — both directions asserted | covered | test/posix-tail.c (`test_nftw`) |
| nftw | "FTW_MOUNT: If set, nftw() shall only report files in the same file system as path" — a real test here, since `src/stat/stat.c` fills `st_dev` from the NT volume serial number | covered | test/posix-tail.c (`test_nftw`) |
| nftw | "FTW_CHDIR: … If clear, nftw() shall not change the current working directory" | covered | test/posix-tail.c (`test_nftw`) |
| nftw | "FTW_CHDIR: If set, nftw() shall change the current working directory to each directory as it reports files in that directory" — together with "shall recursively descend the directory hierarchy rooted in path" | **BUG (fenced)** — the severe one; see below | test/posix-tail.c (`test_nftw_chdir`) |
| nftw | "If FTW_PHYS is clear … nftw() shall follow links instead of reporting them, but shall not report the contents of any directory that would be a descendant of itself" (and the FTW_DEPTH variant) | **BUG (fenced)** | test/posix-tail.c (`test_nftw_symlink_loop`) |
| nftw, ftw | FTW_PHYS's FTW_SL, FTW_SLN, and link-following | **unverified (rc=77)** — needs a real symbolic link; `symlink()` fails `ENOSYS` under Wine and `EPERM` on real Windows without `SeCreateSymbolicLinkPrivilege`. The `ENOSYS` is the correct rendering of `STATUS_NOT_SUPPORTED` (0xc00000bb), which stock Wine below 10.19 answers `FSCTL_SET_REPARSE_POINT` with — `src/internal/errno.c:82-84` maps that status onto `ENOSYS` together with `STATUS_NOT_IMPLEMENTED` and `STATUS_INVALID_DEVICE_REQUEST`, so the errno is not evidence of `NOT_IMPLEMENTED` in particular, and on that leg the privilege is never consulted. Probed at run time, one SKIP line naming the mechanism and errno | test/posix-tail.c (`test_nftw_symlinks`) |
| ftw, nftw | FTW_DNR (an unreadable directory), FTW_NS (an unstattable object), `[EACCES]`, `[ELOOP]`, `[ENAMETOOLONG]`, `[EOVERFLOW]`, `[EMFILE]`/`[ENFILE]` | N/A — the same permission-model limit `glob()`'s `GLOB_ERR` row already records: `chmod 0` does not revoke owner access on this platform, so a directory that cannot be read or an object that cannot be stat'd cannot be built | — |
| ftw | *may fail* "[EINVAL] The value of the ndirs argument is invalid" | N/A — *may* fail; ntlibc does not validate `ndirs` and is conforming not to | — |
| posix_fadvise | DESCRIPTION: every advice value is advisory and "shall have no effect on the semantics of other operations" (asserted by reading the file back after `POSIX_FADV_DONTNEED`); "The specified range need not currently exist in the file"; "If len is zero, all data following offset is specified"; RETURN VALUE returns the error number, not `-1`/`errno` | covered — all six `POSIX_FADV_*` values | test/posix-tail.c (`test_posix_fadvise`) |
| posix_fadvise | *shall fail* "[EBADF] The fd argument is not a valid file descriptor"; the advice half of "[EINVAL] The value of advice is invalid" | covered | test/posix-tail.c (`test_posix_fadvise`) |
| posix_fadvise | the other half of the same clause: "…or the value of len is less than zero" | covered — **FIXED**; `src/fcntl/fadvise.c` guards `len < 0` ahead of the advice switch | test/posix-tail.c (`test_posix_fadvise_einval_negative_len`) |
| posix_fadvise | *shall fail* "[ESPIPE] The fd argument is associated with a pipe or FIFO" | covered — **FIXED**; `src/fcntl/fadvise.c` now ends with `if (f->type == __FD_PIPE) return ESPIPE;`, the same predicate `posix_fallocate()` uses for its identically worded clause. Asserted on both ends of a `pipe()`; a FIFO is not separately testable (`mkfifo()` is `ENOSYS` here) and would not widen the test — NT has one `FILE_DEVICE_NAMED_PIPE` for both, so `__handle_type()` maps them onto the same `__FD_PIPE` | test/posix-tail.c (`test_posix_fadvise_espipe`) |
| posix_fallocate | "If the offset+len is beyond the current file size, then posix_fallocate() shall adjust the file size to offset+len. Otherwise, the file size shall not be changed"; the range is really writable and readable back; "Space allocated … shall be freed by a successful call to creat() or open() that truncates the size of the file" | covered | test/posix-tail.c (`test_posix_fallocate`) |
| posix_fallocate | *shall fail* `[EBADF]` (invalid fd), `[EINVAL]` (negative `offset` or `len`), `[ESPIPE]` (pipe/FIFO); *may fail* `[EINVAL]` for `len == 0` (asserted permissively, since it is optional) | covered | test/posix-tail.c (`test_posix_fallocate`) |
| posix_fallocate | *shall fail* "[EFBIG] The value of offset+len is greater than the maximum file size" | covered — FIXED (5222c97 defined-behaviour [EFBIG]); the fenced defect was: the only way the implementation can produce it is by signed-integer overflow, which is undefined behaviour; see below | test/posix-tail.c (`test_posix_fallocate_efbig`) |
| posix_fallocate | the DESCRIPTION file-size and storage-reservation clauses | covered on x86_64; **unverified (rc=77) on i386** — see below | test/posix-tail.c (`test_posix_fallocate`) |
| posix_fallocate | *shall fail* "[ENODEV] The fd argument does not refer to a regular file" | covered — FIXED (6db513e [ENODEV] for a non-regular file); the fenced defect was: fenced on a *writable character device*, not a directory; see below | test/posix-tail.c (`test_posix_fallocate_enodev`) |
| posix_fallocate | *shall fail* "[EBADF] The fd argument references a file that was opened without write permission" | covered — FIXED (0fb77fc write-permission check) | test/posix-tail.c (`test_posix_fallocate_ebadf_readonly`) |
| _setjmp, _longjmp | "shall be equivalent to longjmp() and setjmp()" — `_setjmp()` returns 0 directly, `_longjmp(env, v)` makes it return `v`, and `_longjmp(env, 0)` makes it return 1 (a negative `v` is returned as given: only 0 is special) | covered | test/posix-tail.c (`test__setjmp_return_values`) |
| _setjmp, _longjmp | **the entire difference from `setjmp`/`longjmp`**: "with the additional restriction that _longjmp() and _setjmp() shall not manipulate the signal mask" | covered — and genuinely, not vacuously: `src/signal/signal.c` keeps a real `blocked` set that `sigprocmask()` reads and writes, so the test blocks `SIGUSR1`, `_setjmp()`s, unblocks it, `_longjmp()`s, and asserts the mask is still the *unblocked* one rather than the one in effect at the `_setjmp()` | test/posix-tail.c (`test__longjmp_does_not_manipulate_the_signal_mask`) |
| _longjmp | "If _longjmp() is called even though env was never initialized … or when the last such call was in a function that has since returned, the results are undefined" | N/A — undefined, so nothing may be asserted | — |
| strlen | "shall compute the number of bytes in the string … not including the terminating NUL character"; "no return value shall be reserved to indicate an error" | covered — including a sweep of every (start offset, length) pair below three machine words, which is what actually exercises `src/string/strlen.c`'s three phases (byte prologue to alignment, word-at-a-time SWAR, byte epilogue) | test/posix-tail.c (`test_strlen`) |
| strnlen | "the number of bytes preceding the first null byte … if s contains a null byte within the first maxlen bytes; otherwise … maxlen" | covered | test/posix-tail.c (`test_strnlen`) |
| strnlen | "shall never examine more than maxlen bytes of the array pointed to by s" | covered — as an **ASan assertion**, not a return-value one: heap blocks of exactly `n` bytes with no NUL, for `n` in 1…40. Both a conforming and an over-reading implementation return `n`, so only ASan can tell them apart. Verified out-of-band that ASan catches it (see below) | test/posix-tail.c (`test_strnlen`) |
| times | "shall fill the tms structure … All times are measured in terms of the number of clock ticks used"; RETURN VALUE "the elapsed real time, in clock ticks, since an arbitrary point in the past … This point does not change from one invocation … to another" (asserted as non-decreasing across two calls with CPU burned between); cumulative CPU totals non-decreasing; `sysconf(_SC_CLK_TCK) > 0` | covered | test/posix-tail.c (`test_times`) |
| times | *shall fail* "[EOVERFLOW] The return value would overflow the range of clock_t" | N/A — needs an uptime long enough to overflow `clock_t`, which no test can arrange | — |
| times | the *magnitude* of `tms_utime`, or that it grows across a busy loop | N/A — deliberately not asserted. NT's `ProcessTimes` is quantised (coarser still under Wine) and the tick here is 1/100 s, so a loop short enough to keep the suite fast is not guaranteed to cross a tick boundary. Asserting growth would be a flake, not a conformance test | — |
| uname | "shall store information identifying the current system"; "Upon successful completion, a non-negative value shall be returned" (non-negative, not specifically 0); every member NUL-terminated within its array and non-empty; two calls in one process agree; `nodename` matches `gethostname()` | covered | test/posix-tail.c (`test_uname`) |
| uname | the content of each member | N/A — "The format of each member is implementation-defined", so nothing may be asserted about the values. `src/misc/uname.c`'s banner documents where each comes from | — |
| gettimeofday | "shall obtain the current time, expressed as seconds and microseconds since the Epoch"; RETURN VALUE "shall return 0 and no value shall be reserved to indicate an error"; ERRORS "No errors are defined"; `tv_usec` in [0, 1000000); `tv_sec` agrees with `time()` and is past a sanity floor; two consecutive calls do not go backwards | covered | test/posix-tail.c (`test_gettimeofday`) |
| gettimeofday | "If tzp is not a null pointer, the behavior is unspecified"; "The resolution of the system clock is unspecified" | N/A — unspecified. Passing a `struct timezone` is exercised for survivability only, with nothing asserted about the result | test/posix-tail.c (`test_gettimeofday`) |
| srand48 | "sets the high-order 32 bits of Xi to the low-order 32 bits contained in its argument. The low-order 16 bits of Xi are set to the arbitrary value 330E" — asserted for six seeds against an **arithmetic oracle**, not a golden vector: X₁ and X₂ are recomputed here from `drand48.html`'s own `Xn+1 = (aXn + c) mod 2^48` with a = 0x5DEECE66D, c = 0xB, and compared against `drand48()` (exactly `Xi / 2^48`, exact in a double), `lrand48()` (`Xi >> 17`) and `mrand48()` (`Xi >> 16` as signed 32-bit) | covered | test/posix-tail.c (`test_srand48`) |
| srand48 | "After lcong48() is called, a subsequent call to either srand48() or seed48() shall restore the standard multiplier and addend values, a and c" | covered — `lcong48()` with a = 3, c = 7, then `srand48(42)` must reproduce the pre-`lcong48()` sequence exactly | test/posix-tail.c (`test_srand48`) |
| srand48 | "the sequence of numbers in each stream shall not depend upon how many times the routines are called to generate numbers for the other streams" | covered — `srand48()` must not disturb an `erand48()` caller's own `xsubi` | test/posix-tail.c (`test_srand48`) |

### Bugs found (group J3)

1. **`nftw()` with `FTW_CHDIR` walks nothing below the root.** Every
   entry of every directory is reported as `FTW_NS`, no directory below
   the root is ever descended into, and the walk returns 0 as though
   the tree had been exhausted. This is the most severe defect in the
   group: a caller gets a *successful* return and a silently truncated
   walk.

   `nftw.html`: "shall recursively descend the directory hierarchy
   rooted in path", and "FTW_CHDIR: If set, nftw() shall change the
   current working directory to each directory as it reports files in
   that directory." `FTW_NS` is specified as "The stat() function
   failed on the object because of lack of appropriate permission" —
   not "the implementation looked in the wrong place".

   Mechanism: `src/ftw/ftw.c`'s `walk()` opens the directory, calls
   `chdir_absolute(ws, path)`, and then builds each child path by
   appending `"/name"` to `path` — which is relative to the walk's
   *original* working directory. `chdir_absolute()` is careful to
   resolve its own argument against the cwd captured before the first
   `chdir()` (its comment diagnoses exactly this hazard), but nothing
   does the same for the child paths handed to the recursive
   `walk()`'s `lstat()`/`stat()`/`opendir()`. Once the process has
   `chdir`'d into `tailtree`, looking up `tailtree/f1` resolves to
   `tailtree/tailtree/f1`.

   Measured under Wine on this file's own fixture: `t4` → `FTW_D`;
   `t4/f1` → `FTW_NS`; `t4/sub` → `FTW_NS`; `t4/sub/f2` never reported
   at all; `rc = 0`. Without `FTW_CHDIR` the identical walk reports all
   four objects with the right types, so this is `FTW_CHDIR` alone. Not
   the "results are unspecified if the application-supplied fn function
   does not preserve the current working directory" escape clause: the
   test's callback changes nothing.

2. **`nftw()` has no protection against a directory that is a
   descendant of itself.** `nftw.html` requires, when `FTW_PHYS` is
   clear, that it "shall not report any directory that would be a
   descendant of itself" (with `FTW_DEPTH` set) or "shall not report
   the contents of any directory that would be a descendant of itself"
   (with it clear). `struct walkstate` carries `nopenfd`,
   `open_count`, `flags`, `legacy`, `root_dev` and the two callback
   pointers and nothing else — there is no state in which "would be a
   descendant of itself" could be computed, so a symbolic link back up
   the tree recurses until the stack or the path length gives out.
   Found by inspection; the fenced assertions need a symbolic link, so
   they stay fenced until both the fix lands and a platform that can
   create one runs them. Fix shape: keep the `(st_dev, st_ino)` of
   every ancestor on the recursion path — `src/stat/stat.c` fills both
   with real values here, and `FTW_MOUNT` already relies on `st_dev`.

3. **`posix_fadvise()` never looked at `len` — FIXED.**
   `posix_fadvise.html` *shall fail*: "[EINVAL] The value of advice is
   invalid, **or the value of len is less than zero**."
   `src/fcntl/fadvise.c` opened `(void)offset; (void)len;` and switched
   on `advice` alone, so `posix_fadvise(fd, 0, -1, POSIX_FADV_NORMAL)`
   returned 0. It now returns `EINVAL` for `len < 0`, ahead of the
   advice switch; `len == 0` keeps its own documented meaning ("all
   data following offset is specified") and `offset` stays unchecked,
   because this page's `[EINVAL]` names advice and len only.
   `test_posix_fadvise_einval_negative_len` is un-fenced and runs.

4. **`posix_fadvise()` returned 0 for a pipe or FIFO instead of
   `[ESPIPE]` — FIXED.** *Shall fail*: "[ESPIPE] The fd argument is
   associated with a pipe or FIFO." `src/fcntl/fadvise.c` validated
   `fd`, `len` and `advice` and then returned 0 without ever looking at
   *what* the descriptor was, so
   `posix_fadvise(pipefd, 0, 0, POSIX_FADV_NORMAL)` returned 0. This
   page's "shall have no effect on the semantics of other operations"
   latitude does not reach the clause: it says a conforming
   implementation may do nothing, not that it may *succeed* on a
   descriptor the ERRORS section requires it to refuse. The mechanism
   was already present one function away in the same file —
   `posix_fallocate()` does `if (f->type == __FD_PIPE) return ESPIPE;`
   — and that is now the last check in `posix_fadvise()` too.

   **Ordering, since POSIX orders none of the three against each
   other.** `[EBADF]` stays first because it is forced, not chosen:
   `f->type` cannot be read until `__fd_get()` has produced an `f`.
   `[ESPIPE]` is placed *after* both halves of `[EINVAL]`, so a pipe
   given a bogus advice reports `EINVAL`. The rule is the one
   `posix_fallocate()` already follows — validate the arguments the
   caller passed, then the object they name — chosen so the two
   functions in this file cannot be caught disagreeing about a
   descriptor that fails two clauses at once. `test_posix_fadvise_
   espipe` asserts that case permissively (`EINVAL || ESPIPE`), the way
   `test_posix_fadvise()` already does for `[EBADF]` against an invalid
   advice, so the *choice* is documented here rather than frozen into
   an assertion POSIX does not license.

5. **`posix_fallocate()` reports `[EBADF]` where `[ENODEV]` is
   required, and never checks write permission.** Two clauses, one
   fence each:
   - "[ENODEV] The fd argument does not refer to a regular file."
     `src/fcntl/fadvise.c` has `if (si.Directory) return EBADF;
     /* not a regular file */` — the comment names the right condition
     and the code returns the wrong errno for it. **The fence uses a
     writable character device (`/dev/null`, NT's `NUL`), not a
     directory, and that choice is the point:** a directory descriptor
     is simultaneously "not a regular file" *and* "opened without write
     permission", so both `[ENODEV]` and `[EBADF]` conform for one and
     it cannot distinguish the clauses. The directory case is asserted
     *live*, permissively, as `ENODEV || EBADF` — never success.
     Measured: `open("NUL", O_WRONLY)` gives a valid, writable,
     `S_ISCHR` descriptor and `posix_fallocate()` on it returns 9
     (`EBADF`) where 19 (`ENODEV`) is required.
   - "[EBADF] The fd argument references a file that was opened without
     write permission." Never checked. Measured: with an 11-byte file
     open `O_RDONLY`, `posix_fallocate(fd, 0, 5)` — a range already
     inside the file, so no NT call is reached at all — returns 0,
     reporting success for an allocation the caller can never write
     into. (`posix_fallocate(fd, 0, 100)` on the same descriptor
     happens to come back 9 too, but only as a side effect of how the
     failing `NtSetInformationFile` status maps; that is not the
     required check and is environment-dependent. The within-EOF call
     is the decisive one.) Fix shape: `f->flags` already records the
     open flags, so `(f->flags & O_ACCMODE) == O_RDONLY` is the whole
     test.

6. **`posix_fallocate()`'s `[EFBIG]` check is signed-integer overflow,
   and that is the only way it can fire.** `src/fcntl/fadvise.c`
   computes `want = (long long)offset + (long long)len;` and then tests
   `if (want < 0) return EFBIG;` — which relies on the sum having
   already wrapped. Signed overflow is undefined in C, so a compiler is
   entitled to delete the `want < 0` test as unreachable, and any
   argument pair whose sum *does* fit never sets it: there is no
   argument pair that reaches `[EFBIG]` defined-ly. Found by
   UndefinedBehaviorSanitizer under `make asan`, from this file's own
   first draft, which asserted `[EFBIG]` live: `src/fcntl/fadvise.c:57:
   27: runtime error: signed integer overflow: 4611686018427387904 +
   4611686018427387904 cannot be represented in type 'long long'`. The
   live assertion was **removed rather than kept** — a test that
   provokes undefined behaviour in the library is not a conformance
   test — and the assertion moved into a fence, written against
   arguments that do not overflow `off_t`. Fix shape: compare before
   adding (`if (offset > MAXFILESIZE - len) return EFBIG;`), with the
   platform's real limit rather than `LLONG_MAX`, since the clause is
   about "the maximum file size", not about what fits in an `off_t`.

**All five*** *(now six)* **fences were verified to pass once fixed.** Fixes for all
five were applied to `src/fcntl/fadvise.c` and `src/ftw/ftw.c`, all
five blocks un-fenced, and the file rebuilt and rerun: green (rc=77,
from the symlink group, with zero failures). Restoring the original
implementations with the tests still un-fenced turns exactly twelve
assertions red across the five groups. Everything was then re-fenced
and both files reverted; nothing in `src/` is modified by this commit.

**Since then the `src/fcntl/fadvise.c` half has landed for real**, one
clause per commit — items 3, 4, 5 and 6 above are all marked FIXED and
their tests run live. The two `nftw()` fences (items 1 and 2) are the
only ones this file still carries.

### Observed behaviour where POSIX permits latitude (group J3)

- **Two mutations that this file deliberately does *not* claim to
  catch,** recorded so a successor does not read the coverage as wider
  than it is. (a) `src/misc/uio.c`'s `break` when one area comes back
  short is unobservable for a regular file — the next `read()` at
  end-of-file returns 0 and the total is identical either way — and for
  a pipe ntlibc's `read()` answers `EAGAIN` on empty, which the loop
  also treats as "stop and report what moved". Removing the `break`
  outright changes no result this file can see. (b) `ftw()`/`nftw()`
  each carry an explicit empty-path check, but removing it changes
  nothing observable: `lstat("")` fails `ENOENT` and the walk root's
  failure is returned as `-1` with that errno anyway. Both assertions
  are written against the *clause*, which holds either way — not
  against the redundant check.
- **`strnlen()`'s "shall never examine more than maxlen bytes" is an
  ASan assertion, like `test/posix-ctype.c`'s out-of-domain probe.**
  Both a conforming implementation and one that scans past `maxlen`
  return the same number, so no return-value check can separate them.
  Verified out-of-band that ASan catches the shape given exactly this
  test's fixture (heap blocks of 1…40 bytes, no NUL): a stand-in
  `memchr(s, 0, n + 1)` dies with `AddressSanitizer:
  heap-buffer-overflow`.
- **`posix_fallocate()` fails `[EINVAL]` on i386 (WOW64) for a
  zero-length file, and that is a conforming answer.**
  `posix_fallocate.html`'s *shall fail* `[EINVAL]` reads "The len
  argument is less than zero, or the offset argument is less than zero,
  **or the underlying file system does not support this operation**",
  so an implementation whose storage layer cannot reserve blocks is
  entitled to return it. Measured with the same source built both ways:
  every form of the call succeeds on x86_64, while on i386
  `NtSetInformationFile(FileAllocationInformation)` comes back
  `STATUS_INVALID_PARAMETER` for a zero-length file.
  `src/fcntl/fadvise.c`'s own comment records that Wine answers
  `STATUS_NOT_IMPLEMENTED` for this class and that it falls through on
  `ENOSYS` *specifically*; the WOW64 path returns a different status,
  which that fallback does not cover. A non-empty file skips the call
  entirely (its `AllocationSize` is already a whole cluster), which is
  why `test/unistd.c`'s pre-existing `posix_fallocate(fd, 0, 4096)`,
  made on a five-byte file, passes on both arches and this file's
  zero-length one does not. The test therefore **probes once and
  reports the allocation group `rc=77` unverified** rather than
  asserting an environment: asserting success unconditionally would be
  asserting the platform, not the clause. Worth reporting upstream
  under this project's Wine-divergence rule — real NT implements
  `FileAllocationInformation` on both arches — but that is a Wine
  change, not an ntlibc one, and nothing here was adjusted to hide it.
- **`posix_fadvise()` doing nothing is conforming.** The page says the
  function "shall have no effect on the semantics of other operations
  on the specified data, although it may affect the performance", so a
  validate-and-no-op implementation is correct and `src/fcntl/
  fadvise.c`'s banner argues that case honestly. The two BUGs above
  (3 and 4, both now fixed) were never about its inaction: one was its
  *argument* validation (`len < 0`) and the other its *descriptor*
  validation (`[ESPIPE]`). The latitude is over what the call may
  **do**, and it does not extend to succeeding where the ERRORS
  section says the call shall fail.
- **`readv()`/`writev()` are not atomic with respect to other
  `read`/`write` calls (XBD 2.9.7).** `src/misc/uio.c`'s banner
  documents this as a deliberate, argued divergence rather than an
  oversight: NT's `NtReadFileScatter`/`NtWriteFileGather` are
  page-granular and cannot take arbitrary iovecs, and restricting the
  interface to page-aligned buffers would reject every real caller. No
  assertion here, since ntlibc has no threads to race with — recorded
  so the divergence is visible from the ledger and not only from the
  source.
- **`times()`'s `tms_utime` magnitude and `gettimeofday()`'s resolution
  are both deliberately unasserted**, for the reasons in their rows
  above. Both would be flakes rather than conformance tests.

### Not reached (group J)

`FTW_DNR`, `FTW_NS`, `[EACCES]`, `[ELOOP]`, `[ENAMETOOLONG]`,
`[EOVERFLOW]` and `[EMFILE]`/`[ENFILE]` for `ftw`/`nftw` (this
platform's permission model cannot build an unreadable directory or an
unstattable object — the same limit `glob()`'s `GLOB_ERR` row records);
`times()`'s `[EOVERFLOW]`; `posix_fallocate()`'s allocation clauses on
i386 (unverified, not unreachable — see above); `[ENOMEM]` for `newlocale`/`duplocale` (no
allocation-failure injection hook, and neither allocates); the STREAMS
half of `ioctl.html` (no STREAMS subsystem exists to enter); and the
`FTW_PHYS`/`FTW_SL`/`FTW_SLN` group, which is the one thing in this
group that is **unverified rather than unreachable** — it needs only a
platform that can create a symbolic link, and reports itself as
`rc=77` with a SKIP line naming the mechanism and errno when it cannot
get one.

## stdio.h, the "implemented, not clause-audited" row (group K)

The two rows `test/POSIX-GAP-ACCOUNTING.md`'s "Implemented, not
clause-audited (357)" table names as `stdio.h` (16) and `stdarg.h` (12).
Distinct from the priority-5 "stdio.h streams" section above, which
audited the stream machinery (`fopen`/`fread`/`fseek`/`setvbuf`/...) and
reached only four of these 28 names in passing: `tempnam`,
`getc_unlocked`, `vprintf`/`vscanf` and the `va_arg`/`va_copy` pair each
already have a first-column row up there and are **not** re-rowed here.

**What the earlier session's eight `<stdarg.h>` "first assertions"
actually covered, checked before assuming anything.** The ledger's
`stdarg.h` row says "eight of the twelve gained their first assertion in
this session's `test/posix-stdio.c` additions". The eight are
`vfprintf`, `vsnprintf`, `vsprintf`, `vsscanf`, `vfscanf`, `vdprintf`
(the six v-forms `test_v_forms()` reaches through its `via_*` wrappers)
and `va_start`/`va_end`, which those wrappers use. Read against the
pages, that is one happy-path call each and nothing more:
`via_vsnprintf` is the only one with a second case (the truncation
return, `n == 3` on a 4-byte result), `via_vsscanf` scans one integer,
`via_vdprintf` writes five bytes to an already-open fd, and
`va_start`/`va_end` are never the subject of an assertion at all —
they are used, incidentally, six times. No ERRORS clause, no boundary,
no zero/one-byte case, no error-indicator check anywhere in the eight.
First assertion, exactly as advertised; not a clause audit. Everything
in the `<stdarg.h>` block below is therefore new coverage, not a
re-statement.

**Oracle.** The formatted-output and formatted-input families are pure C
over memory or over an already-open fd, so Wine is a sound oracle for
almost all of it. Three rows are not: the `[EPIPE]` output-error row
goes through NT named pipes, `dprintf`'s file-offset row through the
real fd layer, and `tempnam`'s existing row through the temp directory —
real-Windows CI is the authority for those three.

Three defects fenced (two BUG, one UNIMPL), and one N/A-by-mechanism
verdict recorded with its evidence (`flockfile`).


| function | clause checked | status | test |
|---|---|---|---|
| snprintf | `fprintf.html` DESCRIPTION: "output bytes beyond the n-1st shall be discarded ... and a null byte is written at the end of the bytes actually written"; RETURN VALUE: "the number of bytes that would be written to s had n been sufficiently large excluding the terminating null byte" — exact fit, one short, `n == 1`, and a sentinel one byte past the buffer in each case | covered | test/posix-stdio.c `test_snprintf_boundaries` |
| snprintf | RETURN VALUE: "If the value of n is zero ... nothing shall be written, the number of bytes that would have been written ... shall be returned, and s may be a null pointer" — both the non-null-`s` and null-`s` forms | covered | test/posix-stdio.c `test_snprintf_boundaries` |
| snprintf / vsnprintf | ERRORS: "[EOVERFLOW] The value of n is greater than {INT_MAX}" — a **shall fail**, not a may-fail | covered — FIXED (this commit); the fenced defect was: no bound was checked at all, `n` went straight to the throwaway memory `FILE`'s `mem_size` and the call formatted normally. See below | test/posix-stdio.c `test_snprintf_eoverflow` |
| sprintf | DESCRIPTION: "shall place output followed by the null byte"; RETURN VALUE: "the number of bytes written to s, excluding the terminating null byte" | covered | test/posix-stdio.c `test_snprintf_boundaries` |
| fprintf / printf / dprintf | DESCRIPTION: "If the format is exhausted while arguments remain, the excess arguments shall be evaluated but are otherwise ignored" | covered | test/posix-stdio.c `test_snprintf_boundaries` |
| fprintf family | the flag table's `'` (`<apostrophe>`) entry: "The integer portion of the result of a decimal conversion ... shall be formatted with thousands' grouping characters" — a `[CX]` flag, i.e. base POSIX, and in the POSIX locale a no-op that must still be *accepted* | covered — was a fenced BUG (`"%'d"` emitted literally, and no argument consumed); FIXED, see below | test/posix-stdio.c `test_printf_apostrophe_flag` |
| fprintf family | DESCRIPTION: "A negative field width is taken as a `'-'` flag followed by a positive field width. A negative precision is taken as if the precision were omitted" | covered | test/posix-stdio.c `test_printf_width_precision` |
| fprintf family | the flag table's `#`, `+` and `<space>` entries, and the precision rules for `d`, `f`, `e`, `g` and `s`, including "The result of converting a zero value with a precision of 0 shall be no characters" | covered | test/posix-stdio.c `test_printf_width_precision` |
| fprintf family | the precision clause out to the widest a `double` has: `%.1074f` of the smallest subnormal, whose exact expansion's last non-zero fractional digit is the 1074th | covered — and deliberately deep: a formatter that clamped its internal expansion short would still get the length and the leading digits right, so nothing shallower catches it | test/posix-stdio.c `test_printf_width_precision` |
| fprintf family | field widths and precisions far past any internal buffer (`%5000d`, `%.5000d`, `%.5000f`, `%.5000e`, `%.5000g`, `%.5000s`) | covered — and run under `tools/asan-build.sh`'s LeakSanitizer/AddressSanitizer, which is the net that matters here: a fixed-size body buffer has been a real defect in this tree before | test/posix-stdio.c `test_printf_width_precision` |
| fprintf | RETURN VALUE: "If an output error was encountered, these functions shall return a negative value and set errno"; ERRORS "refer to fputc" → `fputc.html` [EBADF] and [EPIPE], plus fputc's "shall set the error indicator for the stream" | covered — [EBADF] by closing the fd under the stream, [EPIPE] by writing to a pipe whose read end is closed (with SIGPIPE ignored first; without the ignore the process dies of the signal, which is the clause's other half and is what this measured before the ignore was added) | test/posix-stdio.c `test_printf_output_error` |
| fprintf family | ERRORS: "[EOVERFLOW] The value to be returned is greater than {INT_MAX}" | N/A — needs a single call to transmit more than 2 GiB, which this suite cannot afford to do | — |
| fprintf family | ERRORS: "[EILSEQ] A wide-character code that does not correspond to a valid character" | N/A (already recorded under priority 5) — the formatter is POSIX-locale-only, with no wide-character encoding step to fail | — |
| fprintf family | DESCRIPTION, numbered argument conversions — "Conversions can be applied to the *n*th argument after the format in the argument list ... the conversion specifier character `%` is replaced by the sequence `"%n$"`, where *n* is a decimal integer in the range `[1,{NL_ARGMAX}]`" | **UNIMPL (fenced)** — positional arguments are not implemented at all: `src/stdio/printf.c` has no notion of an argument index and no `$` anywhere, so `"%1$s"` parses as a width of 1 followed by an unknown conversion. `<limits.h>` does define `NL_ARGMAX` (9) — deliberately, see the note at that definition — so the header is complete and this is where the gap is recorded. Implementing it needs a two-pass format scan: `va_arg` cannot be rewound, so arguments must be collected into an indexable table, which needs each one's type, which is only known from its own specifier | test/posix-stdio.c `test_printf_positional_arguments` |
| fprintf family | ERRORS: "[ENOMEM] Insufficient storage space is available" (a *may fail*) | N/A — allocator exhaustion, which this suite has no way to induce | — |
| dprintf / vdprintf | DESCRIPTION: "shall write output to the file associated with the file descriptor specified by the fildes argument rather than place output on a stream" — i.e. to the open file *description*, offset and all: a raw `write()` interleaved between two `dprintf()`s must land between them, and the fd must be left advanced | covered — the half a `FILE*`-shaped implementation can get wrong, and `src/stdio/printf.c`'s `vdprintf()` does wrap the raw fd in a stack `FILE` | test/posix-stdio.c `test_dprintf_fd_path` |
| dprintf / vdprintf | ERRORS: "[EBADF] The fildes argument is not a valid file descriptor" (a *may fail* for `dprintf`, but the write is an output error either way, so RETURN VALUE's negative-return-and-errno is required) | covered | test/posix-stdio.c `test_printf_output_error` |
| sscanf | `fscanf.html` RETURN VALUE: "this number can be zero in the event of an early matching failure"; "If the input ends before the first conversion (if any) has completed, and without a matching failure having occurred, EOF shall be returned" — and the complement, input ending *after* a completed conversion | covered | test/posix-stdio.c `test_sscanf_clauses` |
| sscanf | DESCRIPTION: the white-space directive ("reading input until ... the first byte which is not a white-space character"), the ordinary-character directive, `%%`, `%*` assignment suppression, the maximum field width, and the `%n` rule that its count is not counted toward the return value | covered — including the two worked examples from `fscanf.html` EXAMPLES verbatim | test/posix-stdio.c `test_sscanf_clauses` |
| sscanf | DESCRIPTION: "An input item shall be defined as the longest sequence of input bytes ... which is an initial subsequence of a matching sequence" — for a 400-digit fraction with an exponent, far past any staging buffer | covered — a fixed-size numeric staging buffer has been a real defect here before; `src/stdio/scanf.c`'s `struct nbuf` moves to the heap when a field outgrows its 128-byte `init[]`, and this exercises that | test/posix-stdio.c `test_sscanf_clauses` |
| fscanf | DESCRIPTION: "if the comparison shows that they are not equivalent, the directive shall fail, and the differing and subsequent bytes shall remain unread" | covered — only a real stream can show this; `sscanf()` has no observable read position afterwards | test/posix-stdio.c `test_fscanf_stream_clauses` |
| fscanf | RETURN VALUE: "If an error occurs before the first conversion (if any) has completed ... EOF shall be returned and errno shall be set to indicate the error. If a read error occurs, the error indicator for the stream shall be set" | covered — read error manufactured by scanning a stream not open for reading (`fgetc.html` [EBADF]) | test/posix-stdio.c `test_fscanf_stream_clauses` |
| fscanf family | DESCRIPTION, `[CX]`: "The %c, %s, and %[ conversion specifiers shall accept an optional assignment-allocation character 'm', which shall cause a memory buffer to be allocated" | **UNIMPL (fenced)** — the directive parser does not recognise `'m'` at all; see below | test/posix-stdio.c `test_scanf_m_modifier` |
| fscanf family | ERRORS: "[ENOMEM] Insufficient storage space is available" — a **shall fail** | N/A to assert (allocator exhaustion), but a defect on this path is visible by inspection; see "Observed behaviour" below | — |
| fscanf family | ERRORS: "[EINVAL] There are insufficient arguments" (a *may fail*) | N/A — a may-fail, and there is no conforming way for a variadic callee to detect the condition | — |
| fscanf family | ERRORS: "[EILSEQ] Input byte sequence does not form a valid character" | N/A — POSIX-locale-only parser, no encoding step to fail | — |
| gets | `gets.html` DESCRIPTION: "shall read bytes from ... stdin ... until a `<newline>` is read or an end-of-file condition is encountered. Any `<newline>` shall be discarded and a null byte shall be placed immediately after the last byte read into the array" | covered — both the newline-terminated and the EOF-terminated line | test/posix-stdio.c `test_gets` |
| gets | RETURN VALUE: "Upon successful completion, gets() shall return s. If the end-of-file indicator for the stream is set, or if the stream is at end-of-file, the end-of-file indicator for the stream shall be set and gets() shall return a null pointer" — both halves, including a second call with the indicator already set | covered | test/posix-stdio.c `test_gets` |
| gets | RETURN VALUE: "If a read error occurs, the error indicator for the stream shall be set, gets() shall return a null pointer, and set errno to indicate the error" — for an error with **no** bytes read yet | covered | test/posix-stdio.c `test_gets` |
| gets | the same clause for an error **after** some bytes have been read into the array | not reached — see "Not reached" below; `src/stdio/rw.c` returns `s` rather than a null pointer on that path, found by inspection, but no fixture here can make a stream fail part-way through a line | — |
| gets | APPLICATION USAGE: "Reading a line that overflows the array pointed to by s results in undefined behavior" | N/A — undefined behaviour, with no conforming way to exercise it. Status, stated rather than editorialised: `gets()` was removed from the C standard by C11, and POSIX.1-2017 marks it `[OB]` with a FUTURE DIRECTIONS saying it "may be removed in a future version" — but it is still normatively specified in the edition this audit is against, so it is audited. ntlibc does implement it: `src/stdio/rw.c` guards the definition with `#if __STDC_VERSION__ < 201112L` and this tree builds at `-std=c99`, so the guard is satisfied; `include/stdio.h` declares it unconditionally | — |
| ctermid | `ctermid.html` RETURN VALUE: "The symbolic constant L_ctermid ... shall have a value greater than 0"; "If s is not a null pointer ... the string is placed in this array and the value of s shall be returned"; "If s is a null pointer, the string shall be generated in an area that may be static, the address of which shall be returned" | covered — including that the string fits in the `L_ctermid` bytes the caller was told to supply, since a too-small `L_ctermid` behind `src/stdio/misc.c`'s `strcpy()` would be an overflow of the *caller's* array | test/posix-stdio.c `test_ctermid` |
| ctermid | RETURN VALUE: "shall return an empty string if the pathname that would refer to the controlling terminal cannot be determined, or if the function is unsuccessful" | covered (the complement) — ntlibc always determines one (`"/dev/tty"`), so the empty-string branch is not taken; the measured behaviour is pinned instead. Deliberately **not** asserted: that the pathname can be opened. The DESCRIPTION says outright "If ctermid() returns a pathname, access to the file is not guaranteed", so a fixed `"/dev/tty"` on a platform with no such path is conforming | test/posix-stdio.c `test_ctermid` |
| ctermid | ERRORS: "No errors are defined" | covered trivially — nothing to assert, recorded so the page is not left half-read | — |
| ctermid | DESCRIPTION: "need not be thread-safe if called with a NULL parameter" | N/A — same mechanism as `flockfile` below: no threads on this platform | — |
| flockfile / funlockfile | `flockfile.html` DESCRIPTION, the lock count: "if the count is zero or if the count is positive and the caller owns the (FILE *) object, the count shall be incremented ... Each call to funlockfile() shall decrement the count. This allows matching calls ... to be nested"; and "All functions that reference (FILE *) objects, except those with names ending in `_unlocked`, shall behave as if they use flockfile() and funlockfile() internally" | covered — nesting, and that ordinary (locking) stdio calls still serve the owner inside a held lock, which is where a no-op that had accidentally become a self-deadlocking real lock would hang | test/posix-stdio.c `test_flockfile_nesting` |
| ftrylockfile | RETURN VALUE: "shall return zero for success" — on an unlocked stream and on a nested acquisition by the owner | covered | test/posix-stdio.c `test_flockfile_nesting` |
| flockfile family | every clause that distinguishes a real lock from a no-op: "Otherwise, the calling thread shall be suspended, waiting for the count to return to zero"; "When the count is positive, a single thread owns the (FILE *) object"; ftrylockfile's "non-zero to indicate that the lock cannot be acquired"; "The behavior is undefined if a thread other than the current owner calls funlockfile()" | **N/A (conditional — see the expiry condition below) — mechanism: this libc has exactly one thread of control.** There is no `<pthread.h>` in `include/` (`test/POSIX-GAP-ACCOUNTING.md` lists all 102 pthread interfaces under Absent) and `lib/libpthread.a` is an 8-byte empty archive — the `!<arch>\n` magic and nothing else — built only so that `-lpthread` links. With one thread the count can only ever be incremented by its owner, so the suspension branch is unreachable and `ftrylockfile()` can never fail; `src/stdio/file.c`'s no-ops are indistinguishable from a correct recursive mutex by any conforming program. N/A rather than UNIMPL because nothing was declined: the distinguishing observation does not exist here. See the caveat in "Observed behaviour" below | — |
| ftrylockfile / flockfile / funlockfile | ERRORS: "No errors are defined" | covered trivially | — |
| getc_unlocked / getchar_unlocked / putc_unlocked / putchar_unlocked | `getc_unlocked.html`: "functionally equivalent to the original versions", used inside a `flockfile()`/`funlockfile()` scope — which `flockfile.html` RATIONALE calls the only case where the locking functions are *required* | covered (`getc_unlocked` and `putc_unlocked`/`putchar_unlocked`/`getchar_unlocked` rows already exist under priority 5; what is new here is exercising them inside the scope the RATIONALE names) | test/posix-stdio.c `test_flockfile_nesting`, `test_putc_family`, `test_getchar` |

## stdarg.h, the "implemented, not clause-audited" row (group L)

Audited alongside group K and sharing its test file, because eleven of
the twelve names are the v-form of a `<stdio.h>` function and are
specified only by reference to it (`vfprintf.html`: "shall be equivalent
to ... except that instead of being called with a variable number of
arguments, they are called with an argument list"). The boundary and
ERRORS coverage the v-forms gained is in group K's rows above, where the
clause text lives; what follows is the `<stdarg.h>` machinery itself,
from XBD `<stdarg.h>`, which `va_arg.html` defers to in full.

| function | clause checked | status | test |
|---|---|---|---|
| va_start / va_arg / va_end | XBD `<stdarg.h>` DESCRIPTION: "va_start() ... is invoked to initialize ap to the beginning of the list before any calls to va_arg()"; "The va_arg() macro shall return the next argument in the list ... Each invocation of va_arg() modifies ap so that the values of successive arguments are returned in turn"; "Different types can be mixed" — a six-argument walk over `int`, `double`, `char *`, `long long`, `unsigned` and `void *` | covered — the mixed-width walk is the point: a same-width walk never leaves the x86-64 register save area, so it cannot show a hand-off bug | test/posix-stdio.c `stdarg_mixed` |
| va_arg | XBD `<stdarg.h>`, the two explicitly-permitted type mismatches: "One type is a signed integer type, the other type is the corresponding unsigned integer type, and the value is representable in both types", and "One type is a pointer to void and the other is a pointer to a character type" | covered — both, in the same walk | test/posix-stdio.c `stdarg_mixed` |
| va_start / va_end | XBD `<stdarg.h>`: "Multiple traversals, each bracketed by va_start() ... va_end(), are possible", and "Each invocation of the va_start() and va_copy() macros shall be matched by a corresponding invocation of the va_end() macro in the same function" | covered — a second traversal after the first is ended | test/posix-stdio.c `stdarg_mixed` |
| va_end | XBD `<stdarg.h>`: "it invalidates ap for use (unless va_start() or va_copy() is invoked again)" | N/A — the post-`va_end` state is by definition not observable; the only conforming use is the re-initialisation the row above covers | — |
| va_copy | XBD `<stdarg.h>`: "initializes dest as a copy of src, as if the va_start() macro had been applied to dest followed by the same sequence of uses of the va_arg() macro as had previously been used to reach the present state of src" | covered (pre-existing row under priority 5) | test/posix-stdio.c `va_copy_sees_same` |
| va_copy | XBD `<stdarg.h>`: "Neither the va_copy() nor va_start() macro shall be invoked to reinitialize dest without an intervening invocation of the va_end() macro for the same dest" | N/A — a constraint on the caller, whose violation is undefined behaviour | — |
| vfprintf / vprintf / vsnprintf / vsprintf / vdprintf / vfscanf / vscanf / vsscanf | XBD `<stdarg.h>`: "The object ap may be passed as an argument to another function; if that function invokes the va_arg() macro with parameter ap, the value of ap in the calling function is unspecified", and `vfprintf.html`/`vfscanf.html`: "These functions shall not invoke the va_end macro. As these functions invoke the va_arg macro, the value of ap after the return is unspecified" — the testable half being that a **partially consumed** `ap` handed to a v-form continues from where the caller left off, not from the beginning | covered — new; the pre-existing eight all handed over a freshly-`va_start`ed list, which cannot tell the two apart | test/posix-stdio.c `stdarg_handoff` |
| vsnprintf / vsprintf | `vfprintf.html`: "shall be equivalent to ... snprintf() and sprintf() ... respectively"; RETURN VALUE "Refer to fprintf" — the same exact-fit / one-short / `n == 0` / null-`s` boundary set as the non-`v` forms | covered — new; the pre-existing coverage was one exact case and one truncation case for `vsnprintf`, none for `vsprintf` | test/posix-stdio.c `test_snprintf_boundaries` |
| vdprintf | `vfprintf.html` equivalence to `dprintf()`: the fd path, the file offset, and the [EBADF] output error | covered — new | test/posix-stdio.c `test_dprintf_fd_path`, `test_printf_output_error` |
| vfscanf / vsscanf | `vfscanf.html` equivalence to `fscanf()`/`sscanf()`; RETURN VALUE "Refer to fscanf" — the unread-byte clause and the long-field item | covered — new | test/posix-stdio.c `test_fscanf_stream_clauses`, `test_sscanf_clauses` |
| vprintf / vscanf | `vfprintf.html`/`vfscanf.html` equivalence to `printf()`/`scanf()` | covered (pre-existing row under priority 5) | test/posix-stdio.c `test_vprintf_vscanf` |
| all v-forms | the `%n$` positional form, which `vfprintf.html` inherits from `fprintf.html` | N/A (documented divergence, already recorded under priority 5) — not implemented; `"%1$d"` parses as width 1 plus an unrecognised `$` | test/posix-stdio.c `test_printf_positional_divergence` |

### Bugs found (groups K/L)

1. **`snprintf()` did not fail with `[EOVERFLOW]` when `n` is greater
   than `{INT_MAX}`.** FIXED — `src/stdio/printf.c`'s `vsnprintf()` now
   refuses such a call before it formats anything, which covers
   `snprintf()` too because that is its only variadic wrapper. The
   description below is kept in the past tense as the record; the fence
   is gone and `test_snprintf_eoverflow` asserts the clause for real,
   for both entry points, with a sentinel byte proving `s` is untouched
   and an `n == {INT_MAX}` case proving the boundary itself still
   formats.

   `fprintf.html` ERRORS: "The snprintf() function shall fail if:
   [EOVERFLOW] The value of n is greater than {INT_MAX}." That is a
   *shall fail*, not a may-fail, so the call has
   to return a negative value and set `errno` (RETURN VALUE: "If an
   output error was encountered, these functions shall return a
   negative value and set errno") rather than format.
   `src/stdio/printf.c`'s `vxprintf_mem()` took `n` straight to the
   throwaway memory `FILE`'s `mem_size` and never compared it to
   anything. Measured before the fix: `snprintf(b, (size_t)INT_MAX + 1,
   "z")` returned 1, left `errno` at 0, and wrote `"z"`. The clause
   exists because the return type is `int`: the return value is defined
   as the number of bytes that *would* have been written had `n` been
   sufficiently large, and for `n > INT_MAX` that value is not
   representable in the return type at all — so the standard makes the
   call fail up front instead of returning something that cannot be
   right.

   Worth naming the class rather than just the instance: this is an
   ERRORS "shall fail" argument check that was simply never written,
   which is by now the most common defect shape found in this tree.
   The never-asserted sweep found six of it; the concurrent
   `ctype`/`wctype` pass found three more the same day (`newlocale`
   ignoring `category_mask`, `posix_fadvise` ignoring `len`/`offset`,
   `posix_fallocate`'s wrong errno for a directory). Enumerating a
   page's "shall fail" list explicitly, and checking each entry, finds
   more here than auditing return values or happy paths does.

2. **The `[CX]` `<apostrophe>` flag was not recognised; `"%'d"` was
   emitted literally.** FIXED — `src/stdio/printf.c`'s flag loop now
   accepts `'`. The description below is kept in the past tense as the
   record of what was wrong and why it mattered; the fence is gone and
   `test_printf_apostrophe_flag` asserts the clause for real, including
   the argument-stream alignment that the original one-conversion test
   could not have detected. `fprintf.html`'s flag table lists `'` as a
   `[CX]` flag — base POSIX, not XSI — requiring the integer portion of
   a decimal conversion to be "formatted with thousands' grouping
   characters", using "the non-monetary grouping character". In the
   POSIX locale there is no grouping, so the *output* a conforming
   implementation produces for `"%'d"` is byte-for-byte what `"%d"`
   produces; what it may not do is fail to recognise the flag.
   `src/stdio/printf.c`'s flag loop accepts only `-`, `+`, `<space>`,
   `0` and `#`, so a `'` terminates the flag scan and then falls out of
   the conversion switch's default arm, which emits the two bytes
   verbatim. Measured: `snprintf(b, n, "%'d", 1234567)` yields `"%'d"`,
   3 bytes.

   **Severity: this is a silent argument-stream desync, not a
   formatting defect.** The default arm emits the two bytes and does
   *not* consume an argument, so every conversion after the `%'` in the
   same format string reads the argument meant for the one before it.
   In `printf("%'d %s\n", total, name)` the `%s` is handed `total` and
   dereferences an integer as a `char *` — a crash, or a garbage
   string, in code that reads as obviously correct.

   The POSIX locale makes this worse rather than better. There is no
   thousands grouping there, so the *correct* output of `%'d` is
   byte-for-byte the output of `%d`; an author who tries `%'d`, sees no
   separators and concludes "not supported here, harmless" has no
   reason to suspect the rest of the format is now misaligned. The one
   visible symptom is the one that looks least alarming.

### UNIMPL found (groups K/L)

1. **The `[CX]` assignment-allocation character `'m'` is not
   implemented for `%c`, `%s` or `%[`.** `fscanf.html` DESCRIPTION:
   "The %c, %s, and %[ conversion specifiers shall accept an optional
   assignment-allocation character 'm', which shall cause a memory
   buffer to be allocated to hold the string converted including a
   terminating null character ... The application shall be responsible
   for freeing the memory after usage." `src/stdio/scanf.c`'s directive
   parser recognises `'*'`, a width and the length modifiers, and
   nothing else; an `'m'` falls through the conversion switch's default
   arm, so no argument is consumed and every remaining directive is
   then matched against the wrong input. Measured:
   `sscanf("abc", "%ms", &p)` returns 0 and leaves `p` untouched.
   UNIMPL rather than BUG: the feature is absent, not wrong.

### Observed behaviour where POSIX permits latitude (groups K/L)

- **`%n` inside a truncated `snprintf()` reports the untruncated
  count.** `fprintf.html`'s `n` conversion writes "the number of bytes
  written to the output so far by this call", and for `snprintf()` the
  standard does not say whether "the output" means the notional full
  output or the bytes that reached the array. Measured:
  `snprintf(b, 3, "abcdef%n", &nn)` sets `nn` to 6, matching the
  function's own return value. Recorded rather than asserted, and
  deliberately not fenced: the reading is genuinely ambiguous and
  pinning either answer would be inventing a requirement.

- **`fscanf()` has no channel for `[ENOMEM]`, which the page makes a
  *shall fail*.** `fscanf.html` ERRORS: "In addition, the fscanf()
  function shall fail if: ... [ENOMEM] Insufficient storage space is
  available." `src/stdio/scanf.c` says so itself, in the banner over
  `scandrain()`: "scanf has no channel for ENOMEM, so this becomes a
  matching failure". A conforming caller therefore cannot distinguish
  a malformed field from an exhausted allocator. Found by inspection
  and recorded here rather than fenced, for the same reason the
  `glob.h` section records its `GLOB_NOSPACE` finding this way: no
  assertion this suite can write reaches the path, so there is nothing
  to un-fence when it is fixed.

- **`gets()` returns `s`, not a null pointer, if a read error strikes
  after some bytes are already in the array.** `gets.html` RETURN
  VALUE requires a null pointer on any read error. `src/stdio/rw.c`'s
  loop breaks out on `EOF` and only returns 0 when `i == 0`, so the
  error and the ordinary end-of-line-at-EOF case are indistinguishable
  once a byte has been read. Same disposition as the `[ENOMEM]` item:
  inspection only, no fixture here can make a stream fail part-way
  through a line.

### Expiry condition on the `flockfile` N/A (groups K/L)

Recorded under its own heading rather than as a bullet, because it is
an **N/A that can stop being one** and nothing else in this ledger
carries that property.

The `flockfile`/`ftrylockfile`/`funlockfile` clauses marked N/A above
are N/A **only for as long as `lib/libpthread.a` stays an empty archive
and `include/` has no `<pthread.h>`**. A no-op lock is *correct* in a
libc with one thread of control and *wrong* the day a second one
exists. If `<pthread.h>` ever lands, every one of those clauses becomes
reachable at once and `src/stdio/file.c`'s three functions become BUGs
without a single line of them changing — a regression no diff
introduces and nobody goes looking for. Whoever adds threading to this
libc has to come back to that row. The same condition is stated in
`test/posix-stdio.c`'s `test_flockfile_nesting` banner, so it is not
carried by this file alone.

### Not reached (groups K/L)

`[EOVERFLOW]` on a return value greater than `{INT_MAX}` (needs a
single call transmitting more than 2 GiB). `[ENOMEM]` on the
printf and scanf families (allocator exhaustion). `[EILSEQ]` on both
families (POSIX-locale-only, no encoding step). `fscanf`'s `[EINVAL]`
may-fail. `gets()`'s read-error-after-partial-line path, and
`fscanf()`'s `[ENOMEM]` path — both recorded under "Observed
behaviour" above as inspection findings that no assertion here can
reach. Every clause of `flockfile.html` that needs a second thread.

## unistd.h identity, process group, session, scheduling (successor-queue item 2, group M)

The first of four groups clause-auditing `test/POSIX-GAP-ACCOUNTING.md`'s
**"Implemented, not clause-audited (357)"** table's *first* row —
`unistd.h`, 43 functions, which that table explicitly orders first "by
how much a clause audit would plausibly find". This group takes the
`src/unistd/ids.c` family plus `alarm`/`pause`/`nice`/`gethostname`;
groups N, O and P take the `*at()` link calls, the `exec` family and
`fork` respectively.

New clause-cited audit: `test/posix-unistd-ids.c` (this session).

**These 23 names were not unvisited** — `test/POSIX-GAP-ACCOUNTING.md`'s
never-asserted sweep gave every one of them (except `pause`) a *first
assertion* in `test/posix-unistd.c`'s `test_id_session_stubs()` and
`test/unistd.c`. What that sweep asserted was the **return values** and
the **cross-getter consistency**, and it recorded the *effects* as N/A
on the ground that this library models one user and one session. This
audit accepts that N/A for the effects and rejects it for everything
else, on one distinction the sweep did not draw:

> A degenerate identity model makes an **effect** unobservable. It does
> not make an **argument check** unobservable.

Every page below carries shall-fail `[EINVAL]`/`[EPERM]`/`[EBADF]`/
`[ESRCH]` clauses that constrain what the call may do with a bad
argument no matter how many users the platform has. Six of them are
answered "success". That is the same defect shape the never-asserted
sweep's own six finds had — *every one a shall-fail error clause that
survived because no caller was ever in a position to notice* — and it
is why this row was worth working before the other 42.

**Oracle: Wine is sound for nearly all of this.** Not one function in
`src/unistd/ids.c` makes an NT call; they are 30 lines of one-line C
returning constants. `gethostname()` reads `%COMPUTERNAME%` and
`pause()` calls `NtDelayExecution`. What is being measured is ntlibc's
own C, so a green Wine run is strong evidence here in a way it is not
for the filesystem or console groups.

| function | clause checked | status | test |
|---|---|---|---|
| getuid / geteuid / getgid / getegid | getuid.html RETURN VALUE "shall always be successful and no return value is reserved to indicate the error"; ERRORS "No errors are defined." | covered | test/posix-unistd-ids.c (`test_getid_always_successful`) |
| getuid / geteuid / getgid / getegid | the returned id is a real id, not the `(uid_t)-1` chown.html reserves, and is stable across calls | covered | test/posix-unistd-ids.c (`test_getid_always_successful`) |
| geteuid / getegid | geteuid.html's real-vs-effective distinction | N/A — NT has no set-user-ID bit and no saved set-user-ID; a process token is not switched by executing a file, so nothing on this platform can make the effective id differ from the real one. The agreement *is* asserted (test/posix-unistd.c's `test_access_real_effective_uid_identical` already leans on it) | — |
| getgroups | "If gidsetsize is 0, getgroups() shall return the number of group IDs that it would otherwise return without modifying the array" | covered | test/posix-unistd-ids.c (`test_getgroups`) |
| getgroups | "the value returned shall always be greater than or equal to one and less than or equal to the value of {NGROUPS_MAX}+1"; "The actual number of group IDs stored in the array shall be returned" — same count with room for all of them, and the first *n* entries actually written | covered | test/posix-unistd-ids.c (`test_getgroups`) |
| getgroups | "[EINVAL] The gidsetsize argument is non-zero and less than the number of group IDs that would have been returned" — for a **negative** gidsetsize | covered — was a fenced BUG (`gidsetsize` decided only whether to *store*, never whether to *fail*); FIXED, see below | test/posix-unistd-ids.c (`test_getgroups`) |
| getgroups | the same [EINVAL] for a *positive* gidsetsize | N/A — unconstructible rather than unimplemented: the count is 1 and the smallest positive gidsetsize is 1, so no positive value is ever less than it. A one-group process cannot exhibit that error on any implementation. `src/unistd/ids.c` compares against the count rather than against zero, so the check stays right if the count ever grows | — |
| setuid / seteuid / setgid / setegid / setreuid / setregid | setuid.html RETURN VALUE "Upon successful completion, 0 shall be returned" for the request this platform can honestly grant (the id already in force), and setreuid.html's `(uid_t)-1` "left unchanged" form | covered | test/posix-unistd-ids.c (`test_setid_family`) |
| setuid / setgid | "The setuid() function shall not affect the supplementary group list in any way" / "Any supplementary group IDs of the calling process shall remain unchanged" — observed through getgroups() | covered | test/posix-unistd-ids.c (`test_setid_family`) |
| setuid / seteuid / setgid / setegid / setreuid / setregid | "[EPERM] The process does not have appropriate privileges and uid does not match the real user ID or the saved set-user-ID" (shall-fail, all six pages) | **BUG** — see below | fenced, `test_setid_family` |
| setuid / setgid | "[EINVAL] The value of the uid argument is invalid and not supported by the implementation" (shall-fail) | **BUG** — same fence group | fenced, `test_setid_family` |
| setuid / setgid | the *effect* ("shall set the real user ID, effective user ID, and the saved set-user-ID") | N/A — one fixed identity (src/unistd/ids.c's banner), and `sysconf(_SC_SAVED_IDS)` is -1, so there is no second id to move to | — |
| getpgrp | getpgrp.html RETURN VALUE "shall always be successful and no return value is reserved to indicate an error"; ERRORS "No errors are defined" | covered | test/posix-unistd-ids.c (`test_process_group_and_session`) |
| getpgid | "If pid is equal to 0, getpgid() shall return the process group ID of the calling process" — agrees with getpgrp() and with getpgid(getpid()) | covered | test/posix-unistd-ids.c (`test_process_group_and_session`) |
| getsid | "If pid is (pid_t)0, it specifies the calling process"; `(pid_t)-1` reserved for the error return | covered | test/posix-unistd-ids.c (`test_process_group_and_session`) |
| getpgid / getsid | "[ESRCH] There is no process with a process ID equal to pid" (shall-fail, both pages) | covered — was a fenced BUG (`pid` was discarded by both, so no pid could fail); FIXED, see below. Asserted for an unallocated pid and for a negative one, with the pids that *do* name a process (0, `getpid()`) pinned as still answering | test/posix-unistd-ids.c (`test_process_group_and_session`) |
| getpgid / getsid | "[EPERM] ... not in the same session as the calling process" | N/A — one fixed session for every process, and NT has no session or process-group object for src/process/spawn.c to put a child in a different one of (a Job object has no leader, no session and no controlling-terminal relationship) | — |
| setpgid | "if pid is 0, the process ID of the calling process shall be used. Also, if pgid is 0, the process ID of the indicated process shall be used"; RETURN VALUE 0 on success | covered | test/posix-unistd-ids.c (`test_process_group_and_session`) |
| setpgid | "[EINVAL] The value of the pgid argument is less than 0" and "[ESRCH] The value of the pid argument does not match the process ID of the calling process or of a child process" (both shall-fail) | **BUG** — see below | fenced, `test_process_group_and_session` |
| setpgid | its [EACCES] and remaining two [EPERM] clauses | N/A — each presupposes a second process in a different session or process group; see the getpgid [EPERM] row | — |
| setpgrp | setpgrp.html RETURN VALUE "Upon completion, setpgrp() shall return the process group ID"; ERRORS "No errors are defined" — self-consistent with getpgrp() | covered | test/posix-unistd-ids.c (`test_process_group_and_session`) |
| setpgrp | "If the calling process is not already a session leader, setpgrp() sets the process group ID of the calling process to the process ID of the calling process" | **UNIMPL** — see below | fenced, `test_process_group_and_session` |
| setsid | RETURN VALUE "the value of the new process group ID of the calling process" — agrees with getpgrp() and getsid(0) | covered | test/posix-unistd-ids.c (`test_process_group_and_session`) |
| setsid | the DESCRIPTION/[EPERM] state machine: the first call leaves the process a group leader, so the second "shall fail ... [EPERM] The calling process is already a process group leader" | **UNIMPL** — see below | fenced, `test_process_group_and_session` |
| chown / fchown / lchown / fchownat | chown.html RETURN VALUE "these functions shall return 0"; "If owner or group is specified as (uid_t)-1 ... the corresponding ID of the file shall not be changed" — st_uid/st_gid unmoved across all four spellings | covered | test/posix-unistd-ids.c (`test_chown_family`) |
| chown | "the set-user-ID (S_ISUID) and set-group-ID (S_ISGID) bits of the file mode shall be cleared upon successful return" | N/A — NTFS has no set-user-ID or set-group-ID bit and src/stat/chmod.c stores no shadow for one; st_mode can never come back with S_ISUID set, so there is nothing to clear. Asserted in the only observable direction (absent before and after) | test/posix-unistd-ids.c (`test_chown_family`) |
| chown / lchown | "[ENOENT] A component of path does not name an existing file or path is an empty string" and "[ENOTDIR] A component of the path prefix names an existing file that is neither a directory nor a symbolic link to a directory" (both shall-fail) | **BUG** — see below | fenced, `test_chown_family` |
| fchown | "[EBADF] The fildes argument is not an open file descriptor" (shall-fail) | **BUG** — same fence group | fenced, `test_chown_family` |
| fchownat | "[EBADF] The path argument does not specify an absolute path and the fd argument is neither AT_FDCWD nor a valid file descriptor" and its [ENOENT] (both shall-fail) | **BUG** — same fence group | fenced, `test_chown_family` |
| fchownat | "[EINVAL] The value of the flag argument is not valid" | *may*-fail, so accepting an undefined flag bit is permitted; not fenced, unlike unlinkat()'s masking, which was a bug and is fixed (unlink.html makes its [EINVAL] shall-fail, and there the accepted bit changed what the call did — fchownat() does nothing either way) | test/posix-unistd-ids.c (`test_chown_family`) |
| chown family | [EACCES], [EPERM], [EROFS], [ELOOP], [EIO], [EINTR] | N/A — each needs a second security principal, a read-only mount, a symlink cycle handed to NT's own resolver, or is a may-fail. Unreachable *even if the four functions were fully implemented*, which is what separates them from the fenced rows above | — |
| alarm | alarm.html ERRORS "The alarm() function is always successful, and no return value is reserved to indicate an error"; RETURN VALUE "Otherwise, alarm() shall return 0" with no request outstanding | covered | test/posix-unistd-ids.c (`test_alarm`) |
| alarm | "shall cause the system to generate a SIGALRM signal ... after the number of realtime seconds specified"; "If there is a previous alarm() request with time remaining, alarm() shall return a non-zero value that is the number of seconds until the previous request would have generated a SIGALRM" | **UNIMPL** — see below | fenced, `test_alarm` |
| pause | every clause on pause.html (the suspend, the -1 return, "[EINTR] A signal is caught by the calling process") | N/A — **not callable from this suite at all**: src/unistd/sleep.c implements it as an alertable `NtDelayExecution` with a maximal timeout and this platform has no asynchronous signal delivery to end it, so a call deadlocks the run rather than failing it. The one name in test/POSIX-GAP-ACCOUNTING.md's original never-asserted 112 that cannot be given any assertion | fenced, `test_alarm` |
| nice | nice.html RETURN VALUE, checked in the errno-0 form the page's own APPLICATION USAGE prescribes ("As -1 is a permissible return value in a successful situation ..."); "A maximum nice value of 2*{NZERO}-1 and a minimum nice value of 0 shall be imposed by the system" | covered | test/posix-unistd-ids.c (`test_nice`) |
| nice | "shall add the value of incr to the nice value of the calling process"; "shall return the new nice value -{NZERO}"; and XBD `<limits.h>`'s `{NZERO}` | **UNIMPL** — see below | fenced, `test_nice` |
| nice | "[EPERM] The incr argument is negative and the calling process does not have appropriate privileges" | folded into the UNIMPL above — with `incr` ignored there is no negative-incr path to reject, so the clause and the DESCRIPTION gap are one defect | fenced, `test_nice` |
| gethostname | "shall return the standard host name for the current machine"; "The returned name shall be null-terminated"; "Host names are limited to {HOST_NAME_MAX} bytes"; RETURN VALUE 0; ERRORS "No errors are defined"; and the exactly-fits boundary (namelen == strlen+1) | covered | test/posix-unistd-ids.c (`test_gethostname`) |
| gethostname | "if namelen is an insufficient length to hold the host name, then the returned name shall be truncated" — the truncation itself | covered (the bytes are there) | test/posix-unistd-ids.c (`test_gethostname`) |
| gethostname | ... and is a *successful completion*, so "Upon successful completion, 0 shall be returned" applies to it | **BUG** — see below | fenced, `test_gethostname` |

### Bugs found (unistd.h identity group)

Six, all shall-fail error clauses, all probed on this tree rather than
inferred, none fixed by the audit commit itself. An entry marked FIXED
has been corrected since, and its description is kept in the past tense
as the record; the rest still stand as found.

1. **`getgroups()` succeeded for a negative `gidsetsize`.** FIXED —
   `src/unistd/ids.c`'s `getgroups()` now compares `gidsetsize` against
   the count it would return, and `test_getgroups` asserts the clause
   unfenced. The description is kept in the past tense as the record of
   what was wrong. `getgroups.html` ERRORS: "[EINVAL] The gidsetsize
   argument is non-zero and less than the number of group IDs that
   would have been returned." -1 is non-zero and less than the 1 this
   implementation returns. `src/unistd/ids.c:20` used `n` only to
   decide whether to *store*, never whether to *fail*, so
   `getgroups(-1, list)` reported "1 group ID stored" into an array it
   did not write and a caller that trusted the return read
   uninitialised memory.

2. **The whole `set*id` family reports success for requests it did not
   carry out, including ones POSIX requires it to refuse.**
   `setuid.html` ERRORS: "[EPERM] The process does not have appropriate
   privileges and uid does not match the real user ID or the saved
   set-user-ID"; `sysconf(_SC_SAVED_IDS)` is -1 here, so there is no
   saved id to match either and uid 0 is not the real uid (1000) — the
   precondition holds exactly. `src/unistd/ids.c:12-19` are six
   `(void)u; return 0;` stubs. Also `[EINVAL]` for an unsupported id.

   This is the one classification in the group worth arguing, and the
   argument is: *"one user, so the effect is unobservable"* is a sound
   N/A for the **success** path and this ledger keeps it there. It is
   not an argument for answering 0 to `setuid(0)`. That answer is a
   claim the caller acts on — every privilege-dropping idiom in Unix
   software is `if (setuid(pw->pw_uid) != 0) abort();`, and a stub that
   says 0 turns "refuse to run unprivileged" into "run believing the
   drop happened". Returning -1/`[EPERM]` for any id that is not the
   current one is both what the page requires and what the
   single-identity model actually means.

3. **`getpgid()`/`getsid()` answered for a process that does not
   exist.** FIXED — both now resolve `pid` through `pid_exists()` in
   `src/unistd/ids.c` before answering, and `test_process_group_and_session`
   asserts the clause unfenced. The description is kept in the past
   tense as the record of what was wrong. Both pages: "[ESRCH] There is
   no process with a process ID equal to pid", shall-fail.
   `src/unistd/ids.c:21,25` were `{ (void)p; return 1; }`, discarding
   `pid`, so there was no pid for which either could fail. This needed
   no session model to get right: `src/process/children.c` already
   tracks every process this one created and `src/process/wait.c`
   already distinguishes a live child from an unknown pid. The fix
   takes that lookup and the one `kill()`/`getpriority()` already use:
   pid 0 and the caller's own pid are the caller; a pid in the child
   table is a child, live or exited-but-unreaped (which is still a
   process POSIX-wise, and is the case an `NtOpenProcess` probe alone
   could get wrong, Wine and Windows disagreeing about whether an
   exited pid is still openable — see `src/process/wait.c`);
   anything else is put to the object manager by CLIENT_ID, where
   STATUS_INVALID_CID means [ESRCH] and STATUS_ACCESS_DENIED means the
   process exists and is merely not ours to open. A negative pid names
   no process and is refused without an NT call. The answer for a pid
   that *does* exist is unchanged — the fixed 1 of the one process
   group and one session this platform has.

4. **`setpgid()` accepts a negative `pgid` and an unrelated `pid`.**
   `setpgid.html`: "[EINVAL] The value of the pgid argument is less
   than 0" and "[ESRCH] The value of the pid argument does not match
   the process ID of the calling process or of a child process", both
   shall-fail. `src/unistd/ids.c:22` looks at neither argument. The
   `[EINVAL]` half is a pure range check on a signed value.

5. **The `chown` family reports success for a path that does not exist,
   for the empty string, for a prefix that is a regular file, and for a
   descriptor that was never opened.** `chown.html`: "[ENOENT] ... or
   path is an empty string", "[ENOTDIR] A component of the path prefix
   names an existing file that is neither a directory ...";
   `fchown.html`: "[EBADF] The fildes argument is not an open file
   descriptor"; `chown.html`'s `fchownat()` section repeats both. All
   shall-fail. `src/unistd/ids.c:26-29` are four stubs that touch
   neither `__ntpath_at()` nor `__fd_get()`.

   Same distinction as (2): the degenerate-stub argument is about
   *ownership*, and this is about *path resolution*.
   `chown("does-not-exist", ...)` returning 0 is not a statement about
   ownership, it is a statement that the file exists, and it is false.
   An installer that chowns a list of files it has just laid down loses
   its only report that one of them is missing.

6. **`gethostname()` reports a failure POSIX does not define.**
   `gethostname.html` DESCRIPTION makes truncation the *specified*
   behaviour for a short `namelen` ("the returned name shall be
   truncated and it is unspecified whether the returned name is
   null-terminated") and ERRORS says "No errors are defined", so a
   short buffer is a successful completion and "Upon successful
   completion, 0 shall be returned" applies. `src/unistd/gethostname.c:15`
   performs the required truncation and then reports it as
   -1/`ENAMETOOLONG`. **Whoever fixes this must change
   `test/unistd.c:723` in the same commit** — that assertion currently
   pins the present behaviour. Several other libcs return
   -1/`ENAMETOOLONG` here too; that is historical divergence, not a
   licence in this page.

### UNIMPL found (unistd.h identity group)

1. **`alarm()` never schedules anything.** `alarm.html`: "shall cause
   the system to generate a SIGALRM signal for the process after the
   number of realtime seconds specified", and RETURN VALUE requires the
   remaining time of a previous request. `src/unistd/sleep.c:41` is
   `unsigned alarm(unsigned s) { (void)s; return 0; }`.
   `test/POSIX-GAP-ACCOUNTING.md`'s degenerate-stub table already calls
   this "a genuine gap, and the root of the
   `getitimer`/`setitimer`/`ualarm` `undefined-ok:` chain". UNIMPL, not
   N/A: NT has the mechanism (a waitable timer plus the APC delivery
   `src/signal/signal.c` would need anyway).

2. **`nice()` ignores `incr`, and `<limits.h>` defines no `{NZERO}`.**
   Two gaps, one fence because neither is testable without the other:
   `src/unistd/ids.c:30` discards `incr`, so two calls asking for
   different priorities report the same answer; and XBD `<limits.h>`
   lists `{NZERO}` ("Default process priority. Minimum Acceptable
   Value: 20") while `include/limits.h` does not define it, leaving
   `nice()`'s return — specified purely in terms of `{NZERO}` — with
   nothing to interpret it against. UNIMPL, not N/A: NT has process
   priority (`NtSetInformationProcess(ProcessBasePriority)`) and
   `src/misc/resource.c` already wraps the query side for
   `getpriority()`.

3. **`setsid()`/`setpgrp()` never enter the state their pages
   describe.** `setsid.html`'s DESCRIPTION and `[EPERM]` together make
   a testable transition — the first call leaves the process a group
   leader, so the second must fail — and `src/unistd/ids.c:23-24`
   always answer 1, never setting the process group ID to `getpid()`.
   `setpgrp.html` is starker: "No errors are defined", so there is not
   even a failure return to hide behind; the call has exactly one
   specified effect and it does not happen. UNIMPL rather than N/A
   because the one-fixed-session model is a *chosen* fiction
   (`src/unistd/ids.c`'s and `src/termios/termios.c`'s banners), and
   "I chose not to" is UNIMPL by this project's rule. UNIMPL rather
   than BUG because, unlike the `set*id` fences, no single call's
   answer is a lie in isolation — only a transition that never happens.

### Not reached (unistd.h identity group)

`pause()` in its entirety (fenced N/A above — enabling it hangs
`make check` until the job timeout); every clause needing a second
security principal, a second session or process group, a read-only
mount, or a symbolic-link cycle. `exec.html`'s `[EINVAL]`
("recognized executable binary format, but the system does not support
execution of a file with this format") belongs to group J, not here.

## unistd.h: the `*at()` link calls (successor-queue item 2, group N)

Second of four groups working the `unistd.h` row of
`test/POSIX-GAP-ACCOUNTING.md`'s "Implemented, not clause-audited
(357)" table (group M took the identity family). `linkat`,
`readlinkat` and `symlinkat`, against `link.html`, `readlink.html` and
`symlink.html`.

New clause-cited audit: `test/posix-unistd-links.c` (this session).
`test/posix-unistd.c`'s `test_linkat()` and `test_readlink()` already
covered part of this ground and are cited below where they do; this
file adds what they do not reach, and gives `symlinkat()` its first
assertions of any kind — `test/posix-glob.c` merely *calls* it while
building a fixture.

**Environment gate, and the third outcome.** A symbolic link on NT is
a reparse point, and one cannot be created in every environment — but
the blocker differs by leg, and this paragraph used to name only the
privilege. On real NT it is `SeCreateSymbolicLinkPrivilege` or
Developer Mode (`src/unistd/link.c`'s banner); under stock Wine below
10.19 the privilege is never consulted, because
`FSCTL_SET_REPARSE_POINT` is answered with `STATUS_NOT_SUPPORTED`.
The canonical account is `test/posix-unreferenced.c`'s
`test_fchmodat_eloop()` fence. Every clause needing
a link to exist first sits behind one trial `symlinkat()`; if that
fails, those groups print a `SKIP` line naming the mechanism and the
observed errno and the process exits **77 (unverified)** —
`test/posix-socket.c`'s model, honoured by `tools/runtests.sh`,
`tools/asan-build.sh` and CI's PowerShell loop. Never a silent skip and
never a reported pass for something that did not run. Under the locally
patched Wine used to develop this, reparse points *are* creatable
without the privilege, which is how the privileged half was exercised
at all.

**Oracle: NT filesystem behaviour, so the `windows-test` legs are the
authority.** Both findings below are readable straight out of
`src/unistd/link.c` and do not depend on which of Wine or NTFS is
underneath.

| function | clause checked | status | test |
|---|---|---|---|
| symlinkat | "[EEXIST] The path2 argument names an existing file" — for a regular file and a directory — plus "If the symlink() function fails for any reason other than [EIO], any file named by path2 shall be unaffected" | covered | test/posix-unistd-links.c (`test_symlinkat_errors`) |
| symlinkat | "[ENOENT] A component of the path prefix of path2 does not name an existing file or path2 is an empty string" | covered | test/posix-unistd-links.c (`test_symlinkat_errors`) |
| symlinkat | "[ENOTDIR] A component of the path prefix of path2 names an existing file that is neither a directory nor a symbolic link to a directory" | covered | test/posix-unistd-links.c (`test_symlinkat_errors`) |
| symlinkat | "[EBADF] The path2 argument does not specify an absolute path and the fd argument is neither AT_FDCWD nor a valid file descriptor open for reading or searching" — and nothing created | covered | test/posix-unistd-links.c (`test_symlinkat_errors`) |
| symlinkat | "[ENOTDIR] The path2 argument is not an absolute path and fd is a file descriptor associated with a non-directory file" | covered | test/posix-unistd-links.c (`test_symlinkat_errors`) |
| symlinkat | "[ENAMETOOLONG] ... the length of the path1 argument is longer than {SYMLINK_MAX}" — the failure, and that it leaves no debris | covered — `src/unistd/link.c` bounds path1 by what a `REPARSE_DATA_BUFFER`'s `USHORT` lengths can describe, a *larger* limit than the page's `_POSIX_SYMLINK_MAX` minimum, which still conforms; `test/unistd.c:375` already pins the case itself | test/posix-unistd-links.c (`test_symlinkat_errors`) |
| symlinkat | "shall create a symbolic link called path2 that contains the string pointed to by path1"; "The string pointed to by path1 shall be treated only as a string and shall not be validated as a pathname" — asserted with a target that does not exist and never will | covered *(needs the privilege)* | test/posix-unistd-links.c (`test_symlinkat_creates`) |
| symlinkat | "All interfaces ... shall behave as if the contents of symbolic links can always be read, except that the value of the file mode bits ... is unspecified" — `S_ISLNK()` required, permission bits not | covered *(needs the privilege)* | test/posix-unistd-links.c (`test_symlinkat_creates`) |
| symlinkat | both target shapes `src/unistd/link.c` builds differently — relative (`SYMLINK_FLAG_RELATIVE`) and absolute (the `\??\` prefix) — round-trip losslessly through `readlink()` | covered *(needs the privilege)* | test/posix-unistd-links.c (`test_symlinkat_creates`) |
| symlinkat | "the symbolic link is created relative to the directory associated with the file descriptor fd" — created *there* and not in the current directory | covered *(needs the privilege)* | test/posix-unistd-links.c (`test_symlinkat_creates`) |
| symlinkat | "If symlinkat() is passed the special value AT_FDCWD ... the behavior shall be identical to a call to symlink()" | covered *(needs the privilege)* | test/posix-unistd-links.c (`test_symlinkat_creates`) |
| symlinkat | "If path2 names a symbolic link, symlink() shall fail and set errno to [EEXIST]" — the DESCRIPTION's separate statement, which must hold even for a *dangling* link that names nothing existing | covered *(needs the privilege)* | test/posix-unistd-links.c (`test_symlinkat_creates`) |
| symlinkat | "The symbolic link's user ID shall be set to the process' effective user ID. The symbolic link's group ID shall be set to the group ID of the parent directory or to the effective group ID" | N/A — `src/stat/stat.c` reports one fixed uid/gid for every file (the single identity of `src/unistd/ids.c`), so both halves are true by construction and neither can be observed otherwise | — |
| symlinkat | "shall mark for update the last data access, last data modification, and last file status change timestamps of the symbolic link" | N/A — a reparse point's own timestamps are not reachable through this library: `lstat()` reports the link, but there is no second call that would update them for a comparison | — |
| symlinkat | [EACCES], [EROFS], [ENOSPC], [EIO], [ELOOP] | N/A — a second security principal, a read-only mount, a full filesystem, a hardware error, or a symlink cycle handed to NT's own resolver. Unreachable even with a fully general `symlinkat()` | — |
| readlinkat | "[EBADF] The path argument does not specify an absolute path and the fd argument is neither AT_FDCWD nor a valid file descriptor" | covered | test/posix-unistd-links.c (`test_readlinkat_dirfd`) |
| readlinkat | "[ENOTDIR] The path argument is not an absolute path and fd is a file descriptor associated with a non-directory file" | covered | test/posix-unistd-links.c (`test_readlinkat_dirfd`) |
| readlinkat | "[ENOTDIR] A component of the path prefix names an existing file that is neither a directory ..." | covered | test/posix-unistd-links.c (`test_readlinkat_dirfd`) |
| readlinkat | an **absolute** path is resolved without consulting `fd` at all — the precondition of both clauses above is "does not specify an absolute path", so `readlinkat(4096, "/abs/path", ...)` must not report [EBADF] | covered | test/posix-unistd-links.c (`test_readlinkat_dirfd`) |
| readlinkat | "[EINVAL] The path argument names a file that is not a symbolic link" — for a **directory**, the other shape `FSCTL_GET_REPARSE_POINT` can be handed (`test/posix-unistd.c` covers the regular-file case) | covered | test/posix-unistd-links.c (`test_readlinkat_dirfd`) |
| readlinkat | RETURN VALUE "Otherwise, these functions shall return a value of -1, **leave the buffer unchanged**, and set errno" — checked on every failure above | covered | test/posix-unistd-links.c (`test_readlinkat_dirfd`) |
| readlink / readlinkat | "shall place the contents of the symbolic link ... in the buffer"; "If the buf argument is not large enough ... the first bufsize bytes shall be placed in buf"; RETURN VALUE "the count of bytes placed in the buffer"; [EINVAL] on a regular file; [ENOENT]; AT_FDCWD equivalence; dirfd-relative resolution | covered — pre-existing | test/posix-unistd.c (`test_readlink`) |
| readlink | "If the value of bufsize is greater than {SSIZE_MAX}, the result is implementation-defined" | N/A — explicitly latitude, and nothing here can allocate a buffer that large | — |
| linkat | "[ENOTDIR] A component of either path prefix names an existing file that is neither a directory ..." — for **both** path1 and path2 | covered | test/posix-unistd-links.c (`test_linkat_remaining`) |
| linkat | "[ENOTDIR] The path1 or path2 argument is not an absolute path and fd1 or fd2, respectively, is a file descriptor associated with a non-directory file" — both sides | covered | test/posix-unistd-links.c (`test_linkat_remaining`) |
| linkat | "[ENOENT] A component of either path prefix does not exist ... or path1 or path2 points to an empty string" — the **path2** side (`test/posix-unistd.c` covers path1) | covered | test/posix-unistd-links.c (`test_linkat_remaining`) |
| linkat | "[EEXIST] The path2 argument resolves to an existing directory entry" — when path2 is path1, and when it is a directory | covered | test/posix-unistd-links.c (`test_linkat_remaining`) |
| linkat | "[EPERM] The file named by path1 is a directory and either the calling process does not have appropriate privileges or the implementation prohibits using link() on directories" | covered — was a BUG (a directory path1 reported `EISDIR`, which `link.html`'s ERRORS list does not contain); **fixed in the commit that unfenced it**: `src/unistd/link.c`'s `linkat()` reads path1's attributes off the handle it already holds and returns `EPERM` before path2 is resolved, with `STATUS_FILE_IS_A_DIRECTORY` from `NtSetInformationFile` mapped to `EPERM` at that call site as the fallback | test/posix-unistd-links.c (`test_linkat_remaining`) — the errno, the absence of debris, and a regular-file positive control (both `linkat()` and `link()` still make a real hard link) |
| linkat | "If path1 names a symbolic link ... [if] the AT_SYMLINK_FOLLOW flag is clear ... a new link is created for the symbolic link path1 and not its target" | covered *(needs the privilege)* | test/posix-unistd-links.c (`test_linkat_remaining`) |
| linkat | ... "[if] the AT_SYMLINK_FOLLOW flag is set ... a new link is created for the file referred to by path1" | **BUG** — see below; supersedes this ledger's earlier N/A for the clause | fenced, `test_linkat_remaining` |
| linkat | "shall atomically create a new link for the existing file and the link count of the file shall be incremented by one"; [EEXIST]; [ENOENT] for path1 and the empty string; [EBADF] on either side; dirfd-relative creation | covered — pre-existing | test/posix-unistd.c (`test_linkat`) |
| linkat | "[EINVAL] The value of the flag argument is not valid" | *may*-fail, and linkat() likewise ignores its flag argument outright; unasserted (unlike unlinkat()'s [EINVAL], which is a shall-fail and is now enforced) | — |
| linkat | [EMLINK], [EXDEV], [ENOSPC], [EROFS], [EACCES], [ELOOP] | N/A — {LINK_MAX} is 1023 here so [EMLINK] means creating a thousand entries per run for a limit the platform rather than this code enforces; [EXDEV] needs two filesystems a CI image is not guaranteed to have; the rest as for symlinkat | — |

### Bugs found (unistd.h `*at()` link group)

1. **`linkat()` on a directory reported `EISDIR`, an errno
   `link.html` does not list — fixed.** The page's shall-fail list has
   "[EPERM] The file named by path1 is a directory and either the
   calling process does not have appropriate privileges or the
   implementation prohibits using link() on directories" — NTFS does
   prohibit it, so that clause applied exactly. `EISDIR` appears
   nowhere in `link.html`'s ERRORS. `src/unistd/link.c`'s `linkat()`
   had no directory case at all: it let `NtSetInformationFile` fail
   with `STATUS_FILE_IS_A_DIRECTORY` and handed that to
   `__set_errno_status()`, whose table maps it to `EISDIR`. That
   mapping is right for `open()` and `rename()`, where `EISDIR` *is* a
   specified errno; it was this call site that had to translate.

   **Fixed in the commit that unfenced it.** `linkat()` now asks the path1
   handle it has already opened for `FileAttributeTagInformation` and
   returns `EPERM` for a directory before path2 is resolved, so the
   errno does not depend on which status a particular filesystem
   chooses; a `STATUS_FILE_IS_A_DIRECTORY` that still reaches the
   `NtSetInformationFile` call is mapped to `EPERM` there as well, for
   the volume whose driver cannot answer the attribute query. The
   directory predicate is `src/stdio/misc.c`'s `isdir_attrs()`, so a
   symbolic link to a directory stays a non-directory file the way
   POSIX, `renameat()` and `lstat()` already have it. A regular-file
   path1 is untouched, and `test_linkat_remaining()` now pins that
   with a positive control beside the `EPERM` assertion.

2. **`linkat()` ignores `flags`, so `AT_SYMLINK_FOLLOW` does nothing.**
   `link.html` distinguishes two behaviours by that flag; `src/unistd/
   link.c:27` is `(void)flags;` and line 31 opens with
   `FILE_OPEN_REPARSE_POINT` unconditionally, which is precisely the
   flag-*clear* branch. Measured: the entry created with
   `AT_SYMLINK_FOLLOW` is itself a symbolic link, where the clause
   requires a hard link to the target.

   **This supersedes an earlier N/A.**
   `test/POSIX-GAP-ACCOUNTING.md`'s successor-session notes record this
   clause as N/A because distinguishing the branches "needs a symbolic
   link, which needs `SeCreateSymbolicLinkPrivilege` and is not
   available on the CI images this suite is the authority on". That is
   an accurate statement about the *test environment* — though the
   privilege half of it holds only for the real-Windows leg; on the
   Wine leg the blocker is an unimplemented `FSCTL_SET_REPARSE_POINT`
   — and it is why
   the fence sits behind the symlink probe, but it is not a reason
   to call the clause inapplicable. The defect is visible in the source
   without running anything, and it is reachable in any environment
   that can create a symbolic link at all. N/A is for "a real NT
   mechanism makes the clause inapplicable", not for "this CI image
   cannot reach it".

### Not reached (unistd.h `*at()` link group)

Everything marked *(needs the privilege)* above, in any environment
that cannot create a symbolic link — on real Windows for want of
`SeCreateSymbolicLinkPrivilege` or Developer Mode, on stock Wine below
10.19 because `FSCTL_SET_REPARSE_POINT` is unimplemented there and the
privilege is never reached. Reported as `SKIP` plus rc=77, not as a
pass. Also: `[EMLINK]`, `[EXDEV]`, and
every clause needing a second security principal, a read-only mount, a
full filesystem or a symbolic-link cycle.

One thing a mutation could **not** reach, recorded so the coverage is
not overstated: forcing `symlinkat()` to set `SYMLINK_FLAG_RELATIVE`
on an absolute target is *not* caught by this file. `readlinkat()`
strips a `\??\` prefix only when one is present, so the
create/read pair stays lossless either way, and POSIX specifies
nothing about the NT flag. The round trip is the clause; the flag is
not.

## unistd.h: the exec family's ERRORS (successor-queue item 2, group O)

Third of four groups working the `unistd.h` row. `execl`, `execle`,
`execlp`, `execv`, `execve`, `execvp` and `fexecve` — all seven share
one page,
`https://pubs.opengroup.org/onlinepubs/9699919799/functions/exec.html`.

New clause-cited audit: `test/posix-unistd-exec.c` (this session).

**Division of labour with `test/exec.c`, which is not duplicated.**
That file already covers the *success* path — argv/envp round trips
through `src/process/spawn.c`'s command-line builder and back out of
`crt1.c`, the exec'd image's exit status becoming the caller's,
`[E2BIG]`, `[ENOENT]` for a missing program, `[EBADF]` for `fexecve()`
on a closed descriptor — and needs a spawn/role harness to do it,
because a successful exec never returns. What was left was the rest of
the ERRORS list, and that half needs no harness at all: every call is
one POSIX requires to **fail**, so `exec.html`'s "If execution fails,
the calling process image remains unchanged" is precisely what makes an
in-process test possible, and is itself asserted by the file continuing
to run. A counter pins the number of calls that returned, so an exec
which started *succeeding* — and therefore never returned — cannot be
mistaken for a shorter run that passed.

**No `fork()` anywhere in the file, deliberately**, so it runs under
`make check`'s Wine leg like any ordinary test and needs no `-win`
suffix. Under `tools/asan-build.sh`'s native build it exits **77
(unverified)** with a `SKIP` line: `fuzz/ntstubs.c`'s
`RtlCreateUserProcess` is a host `execve(2)`, so there is no NT process
creation for these clauses to be about and a green run there would be
evidence about glibc.

**Oracle: mixed.** The empty-string and directory cases are decided
inside ntlibc (`src/process/find_program.c`, `src/process/spawn.c`), so
Wine is sound for them; the `[ENOEXEC]` answer comes from
`RtlCreateUserProcess` refusing a non-PE image, which the
`windows-test` legs are the authority on.

| function | clause checked | status | test |
|---|---|---|---|
| execv / execve / execl / execle / fexecve | "[ENOEXEC] The new process image file has the appropriate access permission but has an unrecognized format" — a plain-text file with the executable bit set | covered | test/posix-unistd-exec.c (`test_enoexec`) |
| execvp / execlp | the *inverse* of that clause: "In the cases where the other members of the exec family of functions would fail and set errno to [ENOEXEC], the execlp() and execvp() functions shall execute a command interpreter ... `execl(<shell path>, arg0, file, arg1, ..., (char *)0)`" — which is why [ENOEXEC]'s entry reads "except for execlp() and execvp()" | **UNIMPL** — see below | fenced, `test_enoexec` |
| all seven | "[EINVAL] The new process image file has appropriate privileges and has a recognized executable binary format, but the system does not support execution of a file with this format" | N/A — the [ENOEXEC]/[EINVAL] split is "unrecognized" vs "recognized but unsupported", so reaching it means a PE image for a machine type this host cannot run. `src/internal/pe.c` can parse one but nothing here can build one at test time, and this suite carries no checked-in foreign binary | — |
| execv / execve | "[ENOENT] A component of path or file does not name an existing file or path or file is an empty string" — a missing program and the empty string | covered | test/posix-unistd-exec.c (`test_path_errors`) |
| execl / execle | the same [ENOENT], through the l-forms' `va_list` argument builder | covered | test/posix-unistd-exec.c (`test_path_errors`) |
| execvp / execlp | the same [ENOENT], for a name in no PATH directory ("Otherwise, the path prefix for this file is obtained by a search of the directories passed as the environment variable PATH") | covered | test/posix-unistd-exec.c (`test_path_errors`) |
| execvp / execlp | ... and for the **empty string** | **BUG** — see below | fenced, `test_path_errors` |
| execv / execl | "[ENOTDIR] A component of the new process image file's path prefix names an existing file that is neither a directory nor a symbolic link to a directory ..." | covered | test/posix-unistd-exec.c (`test_path_errors`) |
| execv / execve | "The new image shall be constructed from a regular, executable file" — a directory is not one, so the call must fail and leave the caller running | covered | test/posix-unistd-exec.c (`test_not_a_regular_file`) |
| execv / execve | "[EACCES] The new process image file is not a regular file and the implementation does not support execution of files of its type" — the *errno* for that case | **BUG** — see below | fenced, `test_not_a_regular_file` |
| fexecve | "[EBADF] The fd argument is not a valid file descriptor open for executing" — for a descriptor open on a **directory**, which is the one place on this page where EBADF is the right answer. Asserted rather than fenced, to pin the distinction the [EACCES] fence draws | covered | test/posix-unistd-exec.c (`test_not_a_regular_file`) |
| all seven | "[EACCES] Search permission is denied for a directory listed in the new process image file's path prefix, or the new process image file denies execution permission" | N/A — `src/unistd/access.c`'s `X_OK` is satisfied by the file merely existing (NTFS has no execute bit this library maps a mode onto — `src/stat/chmod.c`'s banner), and one fixed identity cannot construct an unsearchable directory. Neither branch is reachable | — |
| all seven | RETURN VALUE "If one of the exec functions returns to the calling process image, an error has occurred; the return value shall be -1, and errno shall be set" — every call in the file, 19 of them | covered | test/posix-unistd-exec.c (all four functions, counted in `main`) |
| execve | "If execution fails, the calling process image remains unchanged" — in the form that once bit this tree: an open **FD_CLOEXEC** descriptor must survive a failed exec and still be readable at its old offset. `src/process/exec.c`'s banner records the regression (`__fd_close_all_cloexec()` used to run *before* the spawn, so a failed `execv()` handed back a process whose cloexec fds were already shut) | covered | test/posix-unistd-exec.c (`test_failed_exec_leaves_image_unchanged`) |
| execve | ... and the environment a *failed* `execve()` was asked to install does not take effect on the caller: "the environment for the new process image shall be taken from the external variable environ in the calling process" | covered | test/posix-unistd-exec.c (`test_failed_exec_leaves_image_unchanged`) |
| all seven | "[ELOOP]", "[ENAMETOOLONG]", "[ETXTBSY]", "[ENOMEM]" | N/A — a symlink cycle handed to NT's own resolver; `[ETXTBSY]`/`[ENOMEM]` are may-fail. `[ENAMETOOLONG]` is a shall-fail that this tree answers `ENOENT` to, but that is a library-wide path-resolution property already fenced against `utime()` in `test/posix-strings.c`, not an exec defect, and is not re-opened here | — |
| execl / execle / execlp / execv / execve / execvp / fexecve | argv/envp round trip, exit-status propagation, `[E2BIG]`, `[ENOENT]`, `fexecve` `[EBADF]` on a closed fd, "the calling process image remains unchanged" | covered — pre-existing | test/exec.c |

### Bugs found (unistd.h exec group)

1. **`execvp()`/`execlp()` report `[EBADF]` for an empty `file`
   argument, where `exec.html` requires `[ENOENT]`.** The v/l forms get
   this right; the p-forms do not. `src/process/exec.c:62` computes
   `use_path = !strchr(file, '/') && !strchr(file, '\\')`, which is
   true for `""`, so `__find_program("", 1)` runs the PATH search with
   an empty name. `try_dir()` then builds `<PATH entry>\` — a directory
   name with nothing appended — and **accepts it**, because
   `access(p, X_OK)` succeeds on a directory. `execvp("")` therefore
   resolves to the first directory in `PATH` and tries to execute it.
   The empty string is a case `__find_program()` has to reject before
   the loop.

2. **Executing a directory reports `[EBADF]`, which is not an errno
   `exec.html` allows the path-taking forms to produce.** The page's
   shall-fail list gives `[EACCES]` for "not a regular file and the
   implementation does not support execution of files of its type".
   `[EBADF]` appears on the page only under "The fexecve() function
   shall fail if", about the *descriptor* argument — so a caller
   distinguishing "I passed a bad fd" from "that path is not
   executable" is misled. `src/process/spawn.c` hands the path to
   `RtlCreateUserProcess` without checking `S_ISREG` first;
   `src/stat/stat.c` already provides the check the clause asks for.

### UNIMPL found (unistd.h exec group)

1. **`execvp()`/`execlp()` do not fall back to a command interpreter.**
   `exec.html` DESCRIPTION requires that where the other members would
   fail with `[ENOEXEC]`, these two "shall execute a command
   interpreter", as if by
   `execl(<shell path>, arg0, file, arg1, ..., (char *)0)`. That is
   why the `[ENOEXEC]` ERRORS entry is scoped "except for execlp() and
   execvp()". `src/process/exec.c` has no `ENOEXEC` branch anywhere:
   `execvpe()` resolves the name and hands it straight to `execve()`,
   so `RtlCreateUserProcess`'s status for a non-PE image reaches the
   caller unaltered.

   UNIMPL, not N/A: this tree now *has* a shell (`src/sh/` and the `sh`
   binary), so `<shell path>` exists and the fallback is a re-exec of
   it. Re-enabling the fenced assertion needs `test/exec.c`'s role
   harness rather than an in-process call, since a working fallback
   does not return; what is fenced here is the observation that
   identifies the gap.

### Not reached (unistd.h exec group)

`[EINVAL]` (needs a foreign-architecture PE), `[EACCES]` in either of
its two forms (no execute bit, one identity), `[ELOOP]`, `[ETXTBSY]`,
`[ENOMEM]`. Under `tools/asan-build.sh`'s native build the whole file
is rc=77 unverified, for the reason its banner gives.

## unistd.h: the seven already-audited names (successor-queue item 2, group Q)

Bookkeeping, and deliberately **not** a table.

Seven of the `unistd.h` row's 43 — `confstr`, `getlogin`,
`getlogin_r`, `swab`, `sync`, `tcgetpgrp`, `tcsetpgrp` — were already
audited clause by clause by the never-asserted sweep, which cited each
page in `test/posix-unistd.c` **and** added first-column rows for them
to the priority-6 section ("unistd.h, fcntl.h, sys/stat.h") above:

- `confstr` — one row, plus the fenced `[EINVAL]` BUG
- `swab` — one row
- `sync` — one row
- `getlogin / getlogin_r` — one row
- `fchown / fchownat / lchown / setregid / setpgrp / setsid /
  tcgetpgrp / tcsetpgrp` — one row, which also carries the `[EBADF]`
  BUG the last two used to have (fixed, un-fenced)

So this ledger already carries them and **restating them here as rows
of their own would double-count every one**. What is stale is
`test/POSIX-GAP-ACCOUNTING.md`, whose `unistd.h` row of 43 is a
mechanical snapshot of `04edec2` and predates those rows; its own
"Changes since" notes are where that is corrected, and this section
exists only to name the seven so the correction can be checked.

The same caution applies to the other three groups, and is stated once
here rather than three times: **groups M, N and O also overlap the
priority-6 section's first columns** — M shares `fchown`, `fchownat`,
`lchown`, `setregid`, `setpgrp`, `setsid` and `pause`; N shares
`linkat` and `readlinkat`; O shares `execl`, `execle`, `execlp` and
`fexecve`. In every case the priority-6 row records the never-asserted
sweep's *first assertion* (a return value, a stub's consistency) and
the group M/N/O row records the *page's clause list*, which is what
this session added. A first-column tokeniser cannot tell those apart
and will count each name twice. That is the double-count a concurrent
auditor hit on six names the same day, and it is why the arithmetic in
`test/POSIX-GAP-ACCOUNTING.md`'s notes is stated in terms of **pages
audited** rather than in terms of what a tokeniser would report.

## unistd.h: fork() (successor-queue item 2, group P)

Last of the four groups working the `unistd.h` row, and the last of
its 43 names. `fork()` against
`https://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html`.

New clause-cited audit: `test/posix-fork-clauses-win.c` (this session).

**The `-win` suffix is load-bearing.** `fork()` here is
`RtlCloneUserProcess` (`src/process/fork.c`'s banner explains why no
other NT primitive gets there). Stock apt Wine does not implement it:
a call does not fail, it **hangs**, into `winedbg --auto` forever, and
a hang costs a CI job its whole timeout. The Makefile's
`TEST_RUN = $(filter-out %-win.exe,$(TEST_EXES))` keeps such a test out
of the Wine leg while still building it, as `test/fork-win.c`,
`test/fork-handles-win.c` and `test/fork-cloexec-exec-win.c` all do.
Anything added to this file that forks must keep the suffix.

**Division of labour with the three existing fork tests**, none of
which cites the page: `test/fork-win.c` checks the 0-vs-pid split and
that the child's writes to globals do not leak back;
`test/fork-handles-win.c` pins what happens to *pre-existing sibling*
process handles; `test/fork-cloexec-exec-win.c` reproduces one specific
handle-reuse bug. This file takes the page's own DESCRIPTION list — the
enumerated ways the child is and is not an exact copy — plus RETURN
VALUE and ERRORS.

**Oracle: real Windows CI.** Under the locally patched Wine that does
have `RtlCloneUserProcess` this file runs and passes, but Wine's clone
is an emulation of the very primitive under test — see the caveat under
"Not reached" for a measured instance of that mattering.

| function | clause checked | status | test |
|---|---|---|---|
| fork | RETURN VALUE "shall return 0 to the child process and shall return the process ID of the child process to the parent process. Both processes shall continue to execute from the fork() function" — the pid the parent got is the pid the child answers to, reported back over a pipe | covered | test/posix-fork-clauses-win.c (`test_identity`) |
| fork | "The child process shall have a unique process ID" | covered | test/posix-fork-clauses-win.c (`test_identity`) |
| fork | "The child process shall have a different parent process ID, which shall be the process ID of the calling process" | covered | test/posix-fork-clauses-win.c (`test_identity`) |
| fork | "The child process shall have its own copy of the parent's file descriptors" — `close()` in the child does not close the parent's | covered | test/posix-fork-clauses-win.c (`test_shared_open_file_description`) |
| fork | "Each of the child's file descriptors shall refer to the **same open file description** with the corresponding file descriptor of the parent" — `lseek()` in the child moves the parent's offset. The two halves pull in opposite directions, which is the point: an implementation that copied the file too deeply would pass the first and fail this, and one that shared the descriptor table would do the reverse | covered | test/posix-fork-clauses-win.c (`test_shared_open_file_description`) |
| fork | "The set of signals pending for the child process shall be initialized to the empty set" | covered | test/posix-fork-clauses-win.c (`test_child_state_reset`) |
| fork | "The child process values of tms_utime, tms_stime, tms_cutime, and tms_cstime shall be set to 0" — `tms_cutime`/`tms_cstime` exactly (a fresh child has reaped nothing, so 0 is required at any timing resolution); `tms_utime`/`tms_stime` only for being non-negative, since any nonzero value could be time the child has since spent | covered | test/posix-fork-clauses-win.c (`test_child_state_reset`) |
| fork | "After fork(), both the parent and the child processes shall be capable of executing independently before either one terminates" — a two-pipe handshake that can only complete if both are running at once | covered | test/posix-fork-clauses-win.c (`test_independent_execution`) |
| fork | "File locks set by the parent process shall not be inherited by the child process" | N/A — `src/fcntl/fcntl.c`'s `F_SETLK`/`F_GETLK` are advisory no-ops (`test/posix-unistd.c`'s `test_fcntl_locks_are_noops` pins that; `src/file/flock.c` is the call that reaches real NT byte-range locks). With nothing that can ever be *denied*, "the child did not inherit the lock" and "there was no lock" are the same observation. The regression net — the child must not be able to release a lock it never took — runs unfenced | fenced, `test_locks_not_inherited` |
| fork | "The time left until an alarm clock signal shall be reset to zero, and the alarm, if any, shall be canceled" | **UNIMPL** — see below | fenced, `test_alarm_cleared_in_child` |
| fork | "[EAGAIN] The system lacked the necessary resources to create another process, or the system-imposed limit ... {CHILD_MAX} would be exceeded"; may-fail "[ENOMEM]" | N/A — reaching either means exhausting NT's process table or the heap from inside a test whose own failure mode would then be indistinguishable from the condition under test, with this suite's runner the first casualty. `src/process/fork.c` does route a failed `RtlCloneUserProcess` through `__set_errno_status()`, so the -1 path exists; the *trigger* is unconstructible | — |
| fork | "The child process shall have its own copy of the parent's open directory streams. Each open directory stream in the child process **may** share directory stream positioning with the corresponding directory stream of the parent" | N/A — `src/dirent/opendir.c` builds a `DIR` on the heap around a descriptor and both are ordinary memory the clone carries; the clause explicitly permits either behaviour, so there is nothing to assert | — |
| fork | message catalogs, semaphores, `semadj`, interval timers, per-process timers, message queues, asynchronous I/O, memory locks, MAP_PRIVATE mappings, SCHED_FIFO/SCHED_RR inheritance, trace streams, CPU-time clocks | N/A — every one names a facility this library does not have at all (`test/POSIX-GAP-ACCOUNTING.md`'s "absent" table), so the clause has no object | — |
| fork | "A process shall be created with a single thread" | N/A — true by construction: `RtlCloneUserProcess` clones only the calling thread (`src/process/fork.c`'s banner), and nothing in this library creates a second one to test it with | — |
| fork | the 0-vs-pid split; the child's writes to globals not leaking back; pre-existing sibling process handles; the cloexec handle-reuse regression | covered — pre-existing | test/fork-win.c, test/fork-handles-win.c, test/fork-cloexec-exec-win.c |

### UNIMPL found (unistd.h fork group)

1. **The child's pending alarm cannot be observed to be cleared,
   because there are no alarms.** `fork.html` requires the child's
   alarm to be cancelled, and `alarm.html`'s RETURN VALUE is the only
   way to see whether one is pending. `src/unistd/sleep.c:41` is
   `unsigned alarm(unsigned s) { (void)s; return 0; }`, so a correct
   implementation of this clause and a complete absence of alarms are
   indistinguishable.

   Recorded against *this* page as well as against `alarm.html` (group
   M) because the fork side would still need writing once `alarm()` is
   real: `RtlCloneUserProcess` copies the address space, so a timer
   recorded in a global would travel into the child and have to be
   explicitly cancelled there. The fence is the note for whoever does
   that.

### Not reached (unistd.h fork group)

`[EAGAIN]`/`[ENOMEM]`, the file-lock distinction, and every clause
naming an absent facility, as above.

Under `tools/asan-build.sh`'s native build the whole file is **rc=77
unverified**, with a `SKIP` line. `fuzz/ntstubs.c`'s
`RtlCloneUserProcess` is a real host `fork(2)` — which is why fork
tests are no longer on that script's `not_native()` list — but its
pipes are host pipes, and measured there with a standalone probe
against the same objects: a child writing to the pipe is killed by
SIGPIPE (wait status 13) while the parent still holds the read end
open, and the parent's `read()` sees EOF. The identical sequence works
under the PE build on both Wine and real Windows, and
`test/fork-handles-win.c` has carried a child-to-parent pipe across a
fork for as long as it has existed — so that is a property of the
host-fork/host-pipe stand-in, not of `src/process/fork.c` or
`src/unistd/pipe.c`, and asserting into it would be measuring
`fuzz/ntstubs.c`. Flagged here rather than silently skipped, for
whoever owns that file.

**One measured Wine divergence, recorded so the local green is not
mistaken for proof.** Making `src/process/fork.c`'s `set_fd_inherit()`
a no-op — so no descriptor's handle is ever marked `OBJ_INHERIT` before
the clone — does **not** fail this file under the patched Wine: the
child's descriptors still work. On real NT that marking is exactly what
carries a handle into the clone, and `src/process/fork.c`'s banner
records the downstream damage when it is missing (a handle number NT
recycles, `execve()` returning `EBADF` for a program that ran to
completion). So the "own copy of the parent's file descriptors" rows
above are verified *as clauses* here and are only verified *as a test
of the marking step* on the `windows-test` legs. Two other mutations —
`getppid()` answering `getpid()`, and `times()` reporting a nonzero
`tms_cutime` — were both caught.

## puts / scanf / renameat / fchmodat / sigwait / psignal / roundl / strxfrm_l (unreferenced-function sweep, group R)

`tools/lint-unreferenced.sh` landed at `d36b07c` and reported **56**
declared-and-implemented functions that no natively compiled `test/*.c`
carries an undefined-symbol relocation for — no test so much as calls
them. Eight of those 56 are POSIX interfaces with a specification page
to hold them to, and this group audits all of them, clause by clause,
against
`https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html`.
Every entry of every one of those pages' `ERRORS` lists gets an
assertion or a fence; `puts` and `psignal` inherit `fputc.html`'s list
and `scanf` inherits `fgetc.html`'s, so those are audited too.

**An eighth turned out to be POSIX after all: `strxfrm_l()`.** It was
filed with the glibc-only `*_l` names (`strtod_l`, `strtof_l`,
`strtold_l`, which genuinely have no POSIX page), but POSIX.1-2017
specifies it on `strxfrm.html` alongside `strxfrm()`. It is audited
here rather than deferred. `test/posix-string.c` already cites
`strxfrm.html` and names `strxfrm_l` in a comment while never calling
it — exactly the "mentioned is not referenced" distinction
`tools/lint-unreferenced.sh` was built to draw.

**The baseline this group lands on is 43, not 48.** The 56 above is the
count at `d36b07c`, where this group was written; on the merged tree it
is **43**, measured by re-running `tools/lint-unreferenced.sh` rather
than carried forward. This group's eight account for eight of the
thirteen, and the remaining five went to the `unistd.h` clause audit
that landed alongside it: groups M-Q's new tests reference `alarm`,
`setpgid`, `setreuid` and `symlinkat`, and one more name left the set
between `d36b07c` and `origin/main`. `tools/unreferenced-baseline.txt`
is set to 43 for that reason; the ratchet is one-way, so a stale 48
would have silently permitted five regressions.

Two of them are the striking ones: **`puts()` and `scanf()` are
core C interfaces this library has always implemented and that no test
had ever called.** Both turned out to be correct on the happy path.
`scanf()`'s `ERRORS` list is where its gaps are, which is this
codebase's dominant defect shape.

**Oracle: mixed.** `puts`, `scanf`, `psignal`, `roundl` and `strxfrm_l` are pure C
library over redirected descriptors, so Wine is a sound oracle for
them. `renameat` and `fchmodat` go through `NtSetInformationFile` /
`NtOpenFile` on real paths; every defect fenced below was reasoned from
the NT semantics in `src/stdio/misc.c` and `src/internal/path.c` as
well as observed under Wine, and the CI leg that runs this suite on
Server 2025 is the real-NT check on them.

**Eight BUGs fenced, one UNIMPL fenced, and the whole of `sigwait()`
recorded as the degenerate stub it is.** All in
`test/posix-unreferenced.c`. Three of the eight are not ordinary
per-function defects and are called out first, because severity is what
decides whether anyone acts:

1. **`renameat()` of a directory over an existing regular file succeeds
   and destroys the file — silent data loss from a conforming call.**
   `rename.html` requires `[ENOTDIR]` *and* that neither file is
   changed. The call returns 0, the regular file is gone, and the name
   now refers to the directory. Nothing tells the caller.
2. **`__ntpath_at()` does not resolve a `dot` component in a
   `RootDirectory`-relative name.** One defect in shared path machinery,
   reaching **every** `*at()` entry point this library ships: `openat`
   (via `__open_handle`), `faccessat` (via `fstatat`), `fstatat`,
   `unlinkat` and `rmdir` (via `__unlink_at`), `linkat`, `symlinkat`,
   `readlinkat`, `mkdirat`, `renameat`, `fchmodat`, `utimensat`, and
   `nftw()`'s directory walk. Fenced once, in `fchmodat`'s section of
   `test/posix-unreferenced.c`, to avoid a dozen identical copies —
   this list is repeated here and in the fence itself so it is
   discoverable from either end.
3. **`renameat()` ignores `newfd` entirely** — half of what
   distinguishes `renameat()` from `rename()` does not work at all. The
   one-line fix is recorded in the fence: `ri->RootDirectory =
   np.oa.RootDirectory` in `src/stdio/misc.c`. Not applied; a fix needs
   its own review.

| function | clause checked | status | test |
|---|---|---|---|
| puts | DESCRIPTION: "shall write the string pointed to by `s`, followed by a `<newline>` ... The terminating null byte shall not be written"; RETURN VALUE "a non-negative number" | covered | `test_puts_success` |
| puts | RETURN VALUE: "Otherwise, it shall return EOF, shall set an error indicator for the stream, and `errno` shall be set" — with `fputc.html`'s `[EBADF]` | covered | `test_puts_ebadf` |
| puts | `fputc.html` `[EPIPE]` "An attempt is made to write to a pipe or FIFO that is not open for reading by any process" | covered | `test_puts_epipe` |
| puts | `fputc.html` `[EAGAIN]`, `[EINTR]`, `[EIO]`, `[ENOSPC]`, may-fail `[ENOMEM]`/`[ENXIO]` | N/A (fenced), one mechanism given per error — no `O_NONBLOCK` on a file descriptor, no signal that can interrupt a non-alertable `NtWriteFile`, no controlling terminal, no fillable volume | `test_puts_eagain` etc. |
| puts | `fputc.html` `[EFBIG]` | **UNIMPL (fenced)** — `src/unistd/write.c` has no offset-maximum check and nothing in `__set_errno_status` produces `EFBIG` | `test_puts_efbig` |
| scanf | DESCRIPTION: "equivalent to `fscanf()` with the argument `stdin` interposed"; `%n` consuming no input and never counting toward the return value; the assignment-suppressing `*` | covered | `test_scanf_basics`, `test_scanf_returns` |
| scanf | RETURN VALUE: "the number of successfully matched and assigned input items", "can be zero in the event of an early matching failure", and "If the input ends before the first conversion ... has completed ... EOF shall be returned" — all three, plus the case where a *later* conversion hits EOF and the result is therefore not EOF | covered | `test_scanf_returns` |
| scanf | `fgetc.html` `[EBADF]` — `scanf()` on a `stdin` reopened write-only | covered | `test_scanf_ebadf` |
| scanf | `ERRORS`, shall fail: `[ENOMEM]` "Insufficient storage space is available" | **BUG (fenced)** — the `m` assignment-allocation character is not implemented at all; `%ms` falls through `switch(*p)` to `default: break`, assigning nothing and reporting neither a matching failure nor an error. With no allocating conversion there is no situation in which `[ENOMEM]` can arise | `test_scanf_enomem` |
|  | POINTER, 2026-08-24: `test/external-suites.md`'s `regression/printf-fmt-n | %n mismatch | needs triage` row was RESOLVED by **c200c7f** ("printf: read %z and %t as size_t/ptrdiff_t, not as long").  That document is a dated measurement against a named base revision and is deliberately not edited; the resolution is recorded here, in the living one, so a reader of either finds it | -- | -- |
| scanf | `ERRORS`, shall fail: `[EILSEQ]` "Input byte sequence does not form a valid character" | covered — raised by the `l` length modifier's conversion, and the conversion itself is asserted separately, because detecting the failure and performing the conversion are two different things | `test_scanf_eilseq`, `test_scanf_l_modifier` (both in test/posix-unreferenced.c, both unfenced) |
|  | CORRECTION, 2026-08-24: this row previously read "**BUG (fenced)** — the `l` length modifier is parsed and then ignored by the `s`, `c` and `[` conversions, so `%ls` stores raw bytes into a `wchar_t` buffer".  That stopped being true at **6029595** ("scanf: implement the l (ell) length modifier for %s, %c and %["), which added `wide_put()` to `src/stdio/scanf.c` and unfenced both tests; the row was not updated with it.  Recorded rather than silently overwritten so the change is legible.  Note this is a living document and is corrected in place; `POSIX-GAP-ACCOUNTING.md` is a dated record and is never retro-edited | -- | -- |
| scanf | `ERRORS`, may fail: `[EINVAL]` "There are insufficient arguments" | N/A (fenced) — a variadic callee cannot count its arguments; undefined behaviour at the call site | `test_scanf_einval` |
| scanf | `fgetc.html` `[EOVERFLOW]`, `[EAGAIN]`, `[EINTR]`, `[EIO]`, `[ENOMEM]`, `[ENXIO]` | N/A / UNIMPL (fenced), same mechanisms as the `puts` row | `test_scanf_stream_errors` |
| renameat | DESCRIPTION: relative `old` resolved against `oldfd`, `AT_FDCWD` meaning the cwd, and "If the link named by the `new` argument exists, it shall be removed and `old` renamed to `new`" | covered | `test_renameat_success` |
| renameat | DESCRIPTION: "If `new` is a relative path, the file is located relative to the directory associated with the file descriptor `newfd`" | covered — FIXED (ef17f3a newfd honoured); the fenced defect was: `src/stdio/misc.c` sets `ri->RootDirectory = 0` unconditionally while `__ntpath_at(newdirfd, …)` returns a name that is relative to that descriptor's handle, so *every* `renameat()` with a relative `new` and a real `newfd` fails `ENOENT`. Only the `old` side honours its descriptor today. One-line fix: `ri->RootDirectory = np.oa.RootDirectory` | `test_renameat_new_relative_to_dirfd` |
| renameat | DESCRIPTION, directory case: "If the directory named by the `new` argument exists, it shall be removed and `old` renamed to `new`" for an **empty** directory | **BUG (fenced)**, on two counts — NT will not replace a directory, and the `EISDIR`/`ENOTEMPTY` disambiguation in `src/stdio/misc.c` then reports `ENOTEMPTY` without ever asking whether `new` is empty. Observed: `-1`/`ENOTEMPTY` against a freshly created empty directory | `test_renameat_dir_over_empty_dir` |
| renameat | `[ENOTDIR]` "the `old` argument names a directory and the `new` argument names a non-directory file" | **FIXED in `c96657c`**, asserted unfenced. Was destructive: the rename succeeded, the regular file was gone, and the name referred to the directory, violating RETURN VALUE's "neither the file named by `old` nor the file named by `new` shall be changed or created" as well as the error. `renameat()` now classifies both operands *before* `NtSetInformationFile` — it has to be a precondition test, because `FILE_RENAME_REPLACE_IF_EXISTS` reports success once the victim is already unlinked. The mirror case (`[EISDIR]`, non-directory onto a directory) was always handled | `test_renameat_enotdir_dir_over_file` |
| renameat | DESCRIPTION: "If either the `old` or `new` argument names a symbolic link, `rename()` shall operate on the symbolic link itself, and shall not resolve the last component", and "If the `new` argument points to a pathname of a symbolic link, the symbolic link shall be removed" — i.e. a symbolic link is a NON-directory file for both the `[ENOTDIR]` and `[EISDIR]` clauses, whatever it points at | **open, measured rather than assumed** — `renameat()`'s type test above reads `FILE_ATTRIBUTE_DIRECTORY` alone, and NT sets that bit on a *directory* symlink (`src/unistd/link.c`'s `symlinkat()` creates one with `FILE_DIRECTORY_FILE`); the correct predicate needs the reparse TAG, which `NtQueryAttributesFile` cannot return. NOT a traversal problem — `NtQueryAttributesFile` does **not** follow reparse points (measured; `GetFileAttributesW` Remarks; ReactOS `IopQueryAttributesFile()` sets `FILE_OPEN_REPARSE_POINT` unconditionally), so `FILE_OPEN_REPARSE_POINT` is not the fix. Wine cannot reproduce it — it stores every reparse point as a plain Unix file and never sets the DIRECTORY bit on one — so the real-Windows legs are the oracle and the test asserts POSIX and lets them answer | test/posix-rename-symlink.c |
| renameat | `[EBADF]`, `[ENOTDIR]` (descriptor for a non-directory file), `[ENOENT]` (non-existent `old`, missing prefix of `new`, and the empty string under `AT_FDCWD`), `[ENOTDIR]` (prefix component not a directory), `[EISDIR]`, `[EEXIST]`/`[ENOTEMPTY]` | covered — both descriptor positions checked for `[EBADF]` and `[ENOTDIR]` | `test_renameat_errors` |
| renameat | `[ENOENT]` "either `old` or `new` points to an empty string" — with a real directory descriptor | covered — FIXED (527ef09 empty relative name); the fenced defect was: `__ntpath_at()` treats an empty relative name as naming the descriptor's directory itself; observed `EINVAL`, not `ENOENT` | `test_renameat_empty_at_dirfd` |
| renameat | `[EINVAL]` "The `old` pathname names an ancestor directory of the `new` pathname, or either pathname argument contains a final component that is dot or dot-dot" | **BUG (fenced)** — neither clause is checked anywhere; observed, a rename of `dir/.` **succeeded** | `test_renameat_einval` |
| renameat | `[EACCES]`, `[EPERM]` (S_ISVTX), `[EROFS]`, `[EBUSY]`, `[EIO]`, `[EMLINK]`, `[ENOSPC]`, `[ELOOP]`, `[EXDEV]`, `[ENAMETOOLONG]` | N/A (fenced) with one mechanism each — no POSIX permission model, no `S_ISVTX`, no second writable volume, and no symbolic link to build a loop with. That last is a Wine VERSION gap, not a privilege one (measured, `ff1327e`): stock apt Wine 9.0 answers `FSCTL_SET_REPARSE_POINT` with `STATUS_NOT_SUPPORTED` (that FSCTL first shipped in wine-10.19), so `src/unistd/link.c`'s `EPERM` arm never runs and no privilege check is reached at all. `[ENAMETOOLONG]` is no longer UNIMPL: the per-component `{NAME_MAX}` check now exists, in `src/internal/path.c`'s `__name_too_long()`, and applies to `renameat` like every other path-taking interface — see the `fchmodat` row below, which is where it is pinned. It stays inside this fence only because the fence is a MIX and the rest of it is still N/A | `test_renameat_eacces`, `test_renameat_ebusy`, `test_renameat_misc_errors` |
| fchmodat | DESCRIPTION: "equivalent to `chmod()` ... If `fchmodat()` is passed the special value `AT_FDCWD` in the `fd` parameter ... and, if `flag` is zero, the behavior shall be identical to a call to `chmod()`"; relative resolution against a real descriptor; `AT_SYMLINK_NOFOLLOW` on a non-link; a directory as the target | covered — the observable contract on NTFS is the write bits (`src/stat/chmod.c`: "chmod can only express one thing on NTFS") | `test_fchmodat_success` |
| fchmodat | `[EBADF]`, `[ENOTDIR]` (descriptor for a non-directory), `[ENOENT]` (absent component **and** the empty string under `AT_FDCWD`), `[ENOTDIR]` (prefix component not a directory), and RETURN VALUE's "If -1 is returned, no change to the file mode occurs" | covered | `test_fchmodat_errors`, `test_fchmodat_empty` |
| fchmodat | `[ENOENT]` for the empty string — with a real directory descriptor | covered — FIXED (527ef09 empty relative name); the fenced defect was: `fchmodat(dfd, "", mode, 0)` silently changes the mode of the descriptor's own directory and returns 0 | `test_fchmodat_empty_at_dirfd` |
| fchmodat | XBD 4.13 Pathname Resolution, which `chmod.html` invokes for `path`: a `dot` component "refers to the directory specified by its predecessor" | covered — FIXED (3edf110/527ef09 path normalisation); the fenced defect was: `__ntpath_at()` special-cases a path that is exactly `"."` and hands everything else to the NT object manager, which does not implement dot components in a `RootDirectory`-relative name. `fchmodat(dfd, "./f", …)` fails `ENOENT` while `fchmodat(dfd, "f", …)` on the same file succeeds. **This affects every `*at()` function that goes through `__ntpath_at()`**, not just this one; recorded here because this is the audit that found it | `test_fchmodat_dot_component` |
| fchmodat | `[EPERM]`, `[EACCES]`, `[EROFS]`, `[ELOOP]` (both forms) | N/A / UNIMPL (fenced) — no ownership model to violate, and no symbolic link to build a loop with — a Wine version gap rather than a privilege one, see `renameat`'s row above | `test_fchmodat_eperm`, `test_fchmodat_eloop` |
| fchmodat | `[ENAMETOOLONG]` "The length of a component of a pathname is longer than `{NAME_MAX}`" (**shall fail** — not the separate *may-fail* clause about the whole pathname's length) | covered — FIXED; the defect was systemic rather than an `fchmodat` quirk: `{NAME_MAX}` appeared nowhere in `src/` except `sysconf.c`, where it was only *reported as a value*, so **no path-taking function performed a per-component length check at all**. Measured before the fix, a 300-byte component came back `-1`/`[ENOENT]`. The check now lives in `src/internal/path.c`'s `__name_too_long()`, called from the builder `__ntpath()` and `__ntpath_at()` share, and directly by `chdir()`, which hand-builds its own `UNICODE_STRING`. **This affects every path-taking interface in the library**, not just this one; recorded here because this is the audit that found it. Note the deliberate behaviour change it carries: `{NAME_MAX}` is 255 *bytes* while NTFS bounds a component at 255 *UTF-16 code units*, so a 300-byte / 100-character UTF-8 name that used to be created successfully is now refused — which is what POSIX asks for and what glibc does on ext4 | `test_fchmodat_enametoolong` |
| fchmodat | may fail: `[EINTR]`, `[EINVAL]` (invalid `mode`), `[EINVAL]` (invalid `flag`) | **UNIMPL (fenced)**, and conforming — all three are "may fail", so the current behaviour is legal. Worth naming anyway: `src/stat/chmod.c` tests `flags & AT_SYMLINK_NOFOLLOW` and ignores every other bit, so `fchmodat(fd, path, mode, 0x4000)` silently **succeeds** where glibc reports `EINVAL`. Observed | `test_fchmodat_einval` |
| sigwait | DESCRIPTION: "shall select a pending signal from `set`, atomically clear it from the system's set of pending signals, and return that signal number in the location referenced by `sig`"; "If no signal in `set` is pending ... the thread shall be suspended"; RETURN VALUE "shall ... return zero"; `[EINVAL]` | **UNIMPL (fenced)** — `src/signal/signal.c`'s `sigwait()` is `{ errno = EINVAL; return EINVAL; }`, already recorded in `test/POSIX-GAP-ACCOUNTING.md` under "Permanent degenerate stubs". A genuine gap, not a platform limitation: `sigpending()` already reports the pending set and `sigprocmask()` already delivers on unblock, both in the same file, so a real `sigwait()` is writable here | `test_sigwait_spec` |
| sigwait | the behaviour that is actually there, pinned unfenced so a change to it is visible — including that it sets `errno` as well as returning the number, which RETURN VALUE does not provide for (`sigwait()` reports through its return value alone) | covered | `test_sigwait_stub` |
| psignal | DESCRIPTION: message, then `<colon>` and `<space>`, then the signal description, then a `<newline>`, on **stderr**; "If the argument `message` is a null pointer or points to the null string, the ... message shall consist only of" the description and the newline; "shall not change the setting of `errno`" on success; RETURN VALUE "shall not return a value" | covered, for a non-null message, a null message, an empty-string message, and an out-of-range signal number; agreement with `psiginfo()` checked too | `test_psignal` |
| psignal | `ERRORS`: "Refer to `fputc()`" | N/A (fenced) — `[EBADF]` is the one arrangeable member and doing it here would only re-prove what `test_puts_ebadf` proves about the shared `__fwrite()` path; the rest have the same unavailable mechanisms as the `puts` row | `test_psignal_ebadf` |
| roundl | DESCRIPTION: "round their argument to the nearest integer value in floating-point format, rounding halfway cases away from zero, **regardless of the current rounding direction**" — the last clause checked under `FE_DOWNWARD`, `FE_UPWARD` and `FE_TOWARDZERO` as well as the default | covered | `test_roundl` |
| roundl | RETURN VALUE: NaN → NaN; ±0 → x, sign included (`signbit`, which `==` cannot see); ±Inf → x; and the sign of a value that rounds *to* zero | covered | `test_roundl` |
| roundl | `ERRORS`: "No errors are defined" — `errno` untouched for every argument including the special ones | covered | `test_roundl` |
| strxfrm_l | DESCRIPTION: the transform, "No more than `n` bytes ... including the terminating NUL character", "If `n` is 0, `s1` is permitted to be a null pointer", "shall not change the setting of `errno` if successful", and "equivalent to `strxfrm()`, except that the locale data used is from the locale represented by `locale`" — checked with a null `locale_t`, a real one from `newlocale()`, and `LC_GLOBAL_LOCALE` | covered | `test_strxfrm_l` |
| strxfrm_l | DESCRIPTION: "if `strcmp()` is applied to two transformed strings, it shall return a value greater than, equal to, or less than 0, corresponding to the result of `strcoll()` applied to the same two original strings" — all three directions | covered | `test_strxfrm_l` |
| strxfrm_l | RETURN VALUE: "the length of the transformed string (not including the terminating NUL character)"; "If the value returned is `n` or more, the contents of the array ... are unspecified" — so the return value is asserted and the truncated contents deliberately are not, only that nothing past `n` was touched | covered | `test_strxfrm_l` |
| strxfrm_l | `ERRORS`, may fail: `[EINVAL]` "The string pointed to by the `s2` argument contains characters outside the domain of the collating sequence" | N/A (fenced) — the C locale's collating sequence covers every value a `char` can hold, so no input is outside its domain. No reachable case, rather than an unimplemented one | `test_strxfrm_l_einval` |

### Mutation proofs (group R)

Every unfenced assertion group in `test/posix-unreferenced.c` was shown
capable of failing, by deliberately breaking the implementation,
confirming the assertion caught it, and restoring. Fifteen mutations,
fourteen caught, **one honest miss**, plus two negative controls.

| # | mutation | result |
|---|---|---|
| M1 | `puts()`: drop the trailing `<newline>` | caught — byte count and content |
| M2 | `fputs()`: write the terminating null byte too | caught — byte count and content |
| M3 | `__fputc`/`__fwrite`: report `EINVAL` instead of `EBADF` on a non-writable stream | caught |
| M4 | `__fputc`: stop setting the stream error indicator | **SURVIVED** — see below |
| M4b | `__fwrite`: stop setting the stream error indicator | caught — `ferror(stdout)` |
| M5 | `scanf`: count a `%n` toward the return value | caught |
| M6 | `scanf`: return `0` instead of `EOF` when input ends before the first conversion | caught, twice |
| M7 | `roundl`: round halfway cases to even instead of away from zero | caught, three assertions |
| M8 | `roundl`: lose the sign of a negative-zero result | caught — via `signbit`, which `==` cannot see |
| M9 | `psignal`: emit the `<colon>` even for a null/empty message | caught, three assertions |
| M10 | `sigwait`: return `0` instead of `EINVAL` | caught — the degenerate-stub pin works |
| M11 | `fchmodat`: derive the read-only attribute from the read bits instead of the write bits | caught, three assertions |
| M12 | `renameat`: report `EACCES` instead of `EISDIR` for new-names-a-directory | caught |
| M13 | `__ntpath_at`: drop the `ENOTDIR` check on a non-directory descriptor | caught, three assertions — two `renameat` positions and `fchmodat` |
| M14 | `strxfrm`: return the truncated length instead of the source length | caught, twice |
| M15 | `strxfrm`: write `n` bytes without room for the terminating NUL (overrun by one) | caught — the "nothing past `n` was touched" assertion |
| NC1 | `cbrt`: return the argument unchanged | **survived, as required** — `test/posix-unreferenced.c` stayed green. Verified to be a real break: `test/posix-math.c` catches it (`cbrt(27.0) == 3.0`) |
| NC2 | `a64l`: always return 0 | **survived, as required** — stayed green |

**The honest miss, M4.** Deleting `f->err = 1` from `__fputc`'s
non-writable-stream branch did *not* fail the suite, and the reason is
worth recording rather than papering over: `puts()` reaches the error
through `fputs()` → `__fwrite()`, not through `__fputc()`, so
`__fputc`'s copy of that line is never on the path
`test_puts_ebadf` exercises — the first character never gets that far
because `fputs()` has already returned `EOF`. The mutation was
mis-aimed, not the assertion weak; M4b, aimed at `__fwrite`'s copy of
the same line, is caught immediately. The assertion `ferror(stdout) != 0`
is real and does its job. What M4 actually demonstrates is that
`__fputc`'s `f->err = 1` on that branch has **no test anywhere** — a
separate, smaller gap, recorded here rather than fixed by widening this
file's scope.

**The negative controls are the part that makes the rest
trustworthy.** Both broke functions this file does not touch; both left
`test/posix-unreferenced.c` green. NC1 was additionally confirmed to be
a genuine, detectable break by another test in the same suite, so
"stayed green" means "correctly indifferent", not "does not run".

### Observed behaviour worth recording (group R)

- **The `__ntpath_at()` dot-component defect is not `fchmodat`'s.** It
  is in `src/internal/path.c` and reaches every caller of
  `__ntpath_at()` in `src/`, checked by grep: `openat` (via
  `__open_handle`), `faccessat` (via `fstatat`), `fstatat`, `unlinkat`
  and `rmdir` (via `__unlink_at`), `linkat`, `symlinkat`, `readlinkat`,
  `mkdirat`, `renameat`, `fchmodat`, `utimensat`, and `nftw()`'s
  directory walk. `fchownat`, `mknodat` and `mkfifoat` are stubs that
  never build a path and are not affected. Only one fence was written
  for it, in `test_fchmodat_dot_component`. **A reader arriving at any
  of those twelve functions should be pointed here**; none of them is
  unaudited on this clause merely because the fence lives elsewhere.
  Note the asymmetry that lets it survive: an `AT_FDCWD` or absolute
  path goes through `__ntpath()`, which handles dot components
  correctly, so the common case works.
- **`renameat`'s `new`-side descriptor is ignored entirely.** The same
  `RootDirectory = 0` line makes the `[EBADF]`/`[ENOTDIR]` assertions
  for `newfd` pass for the wrong reason: `__ntpath_at()` rejects the
  bad descriptor before the rename is ever attempted, which is
  correct, but a *good* descriptor is then thrown away.
- **`puts()` and `scanf()` had no test at all, and both happy paths
  were already right.** That is the pattern this project's earlier
  audits found and this one confirms: the defects are in the `ERRORS`
  lists, not in the common case.

## spawn.h — the `_POSIX_SPAWN` option (group S)

New header (`include/spawn.h`), new sources (`src/process/posix_spawn.c`,
`spawn_file_actions.c`, `spawnattr.c`, `spawn_internal.h`) and a new
audit file (`test/posix-spawn.c`). All 21 interfaces
`basedefs/spawn.h.html` lists are declared and defined; the header
landed in two instalments, the first being the eight GNU make's
`USE_POSIX_SPAWN` path calls (`src/job.c` `child_execute_job`).

The mechanism is `__spawn()` (`src/process/spawn.c`), which `execve()`,
`fork()` and `system()` already share. There is no child to replay file
actions *in* — NT starts a process from an image file — so they are
replayed on the parent's own descriptor table immediately before
`__spawn()` reads it and undone immediately after, which is safe here
specifically because ntlibc has no threads. `test/posix-dl.c`'s
file-actions `UNIMPL` fence proposed exactly this design and is what
this closes.

| function | clause checked | status | test |
|---|---|---|---|
| posix_spawn | DESCRIPTION steps 1–4, RETURN VALUE ("shall return the process ID ... in the variable pointed to by a non-NULL *pid* ... and shall return zero as the function return value"; on failure "an error number shall be returned as the function value" — **not** errno), ERRORS `[EINVAL]` on *attrp*, and the `close()`/`dup2()`/`open()`, `fork()`/exec pass-through clauses | covered | test/posix-spawn.c `test_adddup2_stdout`, `test_adddup2_stderr`, `test_order_two_targets`, `test_order_chained`, `test_adddup2_self`, `test_adddup2_badfd`, `test_parent_table_restored`, `test_enoent_and_errno`, `test_null_actions_and_argv` |
| posix_spawn_file_actions_init / _destroy | RETURN VALUE, and reuse after destroy | covered | `test_file_actions_object` |
| posix_spawnp | DESCRIPTION — "shall be equivalent to `posix_spawn()` except that ... the *file* parameter shall be used to construct a pathname ... using the `PATH` environment variable"; a *file* with a directory part is not searched for | covered — over `__find_program()` (`src/process/find_program.c`), the same resolver `execvp()` uses, including its `;` PATH separator and `.exe` suffix. The discriminating assertion is that the *same* bare name resolves through `posix_spawnp()` and fails `ENOENT` through `posix_spawn()` | `test_spawnp_path_search` |
| posix_spawn_file_actions_adddup2 | DESCRIPTION ("as if `dup2(fildes, newfildes)` had been called"), ERRORS `[EBADF]` ("negative or greater than or equal to {OPEN_MAX}"), and posix_spawn.html's "performed in the order in which they were added" | covered | `test_file_actions_object`, `test_order_two_targets`, `test_order_chained`, `test_adddup2_self` |
| posix_spawn_file_actions_addclose | DESCRIPTION ("as if `close(fildes)` had been called"), ERRORS `[EBADF]` | covered. Closing a descriptor that is already closed is a success here, not `EBADF`: the action's postcondition already holds, and glibc agrees. The fd 0 case exercises `src/process/spawn.c`'s `closed_placeholder()` end to end — a closed standard descriptor cannot be handed over as NULL or -1 (both measured on real Windows to arrive open), so a rejected-but-real handle is passed and the child's `install_std()` refuses it | `test_addclose` |
| posix_spawn_file_actions_addopen | DESCRIPTION ("as if `open()` had been called ... and the returned file descriptor, if not *fildes*, had been changed to *fildes*"; "The string described by *path* shall be copied"), ERRORS `[EBADF]`, and posix_spawn.html's `open()` pass-through | covered, both directions, including the path copy (the caller's buffer is clobbered after the add and the right file still opens) and a failing open failing the whole call with `ENOENT` and no child created | `test_addopen`, `test_addopen_copies_path` |
| posix_spawnattr_init / _destroy | DESCRIPTION — "the resulting spawn attributes object ... contains ... the default values", i.e. no flag set | covered | `test_attr_flags_acted_on` (a default-initialised object spawns) |
| posix_spawnattr_getflags / _setflags | DESCRIPTION, and each flag's own clause in posix_spawn.html | covered | `test_attr_roundtrip`, `test_attr_flags_acted_on` |
| posix_spawnattr_getsigmask / _setsigmask | DESCRIPTION ("get/set the spawn-sigmask attribute") | covered as storage; `posix_spawn()` acts on it only for an empty mask (see below) | `test_attr_roundtrip`, `test_attr_flags_acted_on` |
| posix_spawnattr_getsigdefault / _setsigdefault | DESCRIPTION ("get/set the spawn-sigdefault attribute") | covered as storage | `test_attr_roundtrip` |
| posix_spawnattr_getpgroup / _setpgroup | DESCRIPTION ("get/set the spawn-pgroup attribute") | covered as storage; `posix_spawn()` accepts only the one process group this platform has (see below) | `test_attr_roundtrip`, `test_attr_flags_acted_on` |
| posix_spawnattr_getschedparam / _setschedparam / getschedpolicy / _setschedpolicy | DESCRIPTION ("get/set the spawn-schedparam / spawn-schedpolicy attribute") | covered as storage, deliberately. These four are pure attribute storage and that is a promise this platform *can* keep, so it keeps it; POSIX gives the setters no error to refuse a value with, and refusing would break a caller that only reads the value back. `posix_spawn()` acting on the corresponding flags is the part that cannot be done, and it fails loudly rather than dropping them (see below). `struct sched_param` comes from `bits/alltypes.h` under `__NEED_struct_sched_param` rather than from `<sched.h>`, which still claims no `_POSIX_PRIORITY_SCHEDULING` interface | `test_attr_roundtrip` |
| `POSIX_SPAWN_SETSIGDEF` | "the signals ... shall be set to their default actions in the child" | covered — satisfied by construction. A fresh NT process runs its own crt1 before `main()`, and `src/signal/signal.c`'s `handlers[]` is a static, so every signal in every child is already `SIG_DFL` | `test_attr_flags_acted_on` |
| `POSIX_SPAWN_SETSIGMASK`, empty mask | "the child process shall have the signal mask specified" | covered — satisfied by construction (`blocked` in signal.c is a static). This is the case GNU make uses: `sigemptyset()` then `posix_spawnattr_setsigmask()` | `test_attr_flags_acted_on` |
| `POSIX_SPAWN_SETSIGMASK`, non-empty mask | as above | **UNIMPL**, and `posix_spawn()` fails with `[EINVAL]` rather than accepting the flag and dropping it. The mechanism to carry state to a not-yet-running child *does* exist — `RTL_USER_PROCESS_PARAMETERS`' `RuntimeData`, which already carries the descriptor table and is exercised by `test/spawn-runtimedata-stress.c` — so `test/posix-dl.c`'s "no channel to hand a chosen initial mask ... to a child" is **expired**. What is missing is a format and a reader: the block's layout is msvcrt's inherited-descriptor table on purpose, so a mask would be an ntlibc-only trailer, reaching an ntlibc-built child and silently nothing else. Fenced with that mechanism named | `test/posix-spawn.c` fence `test_setsigmask_nonempty_is_delivered` |
| `POSIX_SPAWN_RESETIDS` | "reset the effective user ID ... to the real user ID" | **N/A** — an NT access token has no real/effective/saved-set-id triple, so the postcondition is unconditionally true. Already fenced on this mechanism in `test/posix-dl.c`; accepted by `posix_spawn()` because there is nothing to do, not because it is ignored | `test_attr_flags_acted_on` (accepted), fence `test_resetids` |
| `POSIX_SPAWN_SETPGROUP` | "set the process group ID of the new process ... as if by `setpgid()`" | **N/A** on the mechanism (no NT process-group object; a job object groups for resource limits, not job-control signal delivery, and `src/unistd/ids.c` answers `getpgrp()`/`getpgid()` with a fixed 1 for every process). A spawn-pgroup naming that one group is accepted, because it is already true of the child; anything else — 0 included, which asks for a *new* group — is refused with `[EINVAL]`, which is what ERRORS routes here via `setpgid()`'s "not a value supported by the implementation" | `test_attr_flags_acted_on`, fence `test_setpgroup_other_group` |
| `POSIX_SPAWN_SETSCHEDPARAM` / `POSIX_SPAWN_SETSCHEDULER` | "the child ... shall be as specified in the spawn-schedparam / spawn-schedpolicy attribute" | **UNIMPL**, refused with `[EINVAL]` — ERRORS routes these to `sched_setparam()`/`sched_setscheduler()`, whose "[EINVAL] The value of the policy parameter is invalid" is accurate where no POSIX policy exists (Issue 6 removed `[ENOSYS]` from `sched_setscheduler()` precisely because stubs need not be provided). The unused hook is real — `__spawn()` already creates the process suspended — but the POSIX *shape* does not survive: `<sched.h>` deliberately does not claim `_POSIX_PRIORITY_SCHEDULING` | `test_attr_flags_acted_on` |
| `POSIX_SPAWN_USEVFORK` | not POSIX; a GNU extension GNU make sets whenever the macro exists | covered — satisfied by construction: `__spawn()` never copies the parent's address space | `test_attr_flags_acted_on` |

### Not reached (group S)

`[ENOMEM]` on `posix_spawn_file_actions_adddup2()` and on
`posix_spawn_file_actions_addopen()`, and on
`posix_spawn()`'s own save array (allocator exhaustion, unforceable
here as elsewhere in this ledger). `[EINVAL]` "the value specified by
*file_actions* ... is invalid" is a *may fail* this implementation does
not take up: an object that was never `_init()`ed is undefined
behaviour, not a detectable state.

## The vacuous zero-assertion sweep (group T)

Not a header audit. A sweep of the whole of `test/` for assertions of
the shape "this field is 0", triaged by one question: **would this
assertion pass identically if the code under test had never run, never
written the field, or handed back a struct that arrived zeroed?** A zero
that is also the value of "nothing happened" is not a measurement.

Method: every `CHECK(...== 0)` in `test/*.c` on a counter, time, size,
offset, error field, list count, or returned-struct member was read in
context and classified. Companion assertions and preceding non-zero
states are what make a zero meaningful, so each DISCRIMINATING verdict
below names the specific line that supplies one. Verdicts marked
**VERIFIED** were mutation-tested: the field's producer was broken so it
is never populated, and the test was required to go from green to red.

### Tally

| verdict | count |
|---|---|
| DISCRIMINATING | 34 |
| VACUOUS (fixed here) | 9 |
| UNCERTAIN | 1 |

The headline result is that this codebase's zero-assertions are mostly
already discriminating — the prevailing house idiom is to poison the
destination (`memset(&x, 0xff, sizeof x)`, or `pfd.revents = -1`) or to
pair the zero with a non-zero companion in the same function. The nine
exceptions clustered in two places: child CPU-time accounting, and
helpers that seeded the very value their callers then asserted.

### VACUOUS — fixed

| site | why it could not fail | fix | mutation evidence |
|---|---|---|---|
| `test/exec.c` `test_wait_rusage`, `test/process-win.c` `test_wait_rusage` (2 sites) | the "running total grew" check was a four-clause **disjunction**, one clause of which was `ru_after.ru_utime.tv_usec >= ru_before.ru_utime.tv_usec` — satisfied by the two structs merely being *equal*, which they are at zero. It passed with `getrusage(RUSAGE_CHILDREN)` writing nothing at all. (The disjunction is itself a symptom: comparing `tv_sec` and `tv_usec` independently is wrong because `tv_usec` wraps each second) | compare whole timevals folded to microseconds; poison `ru_after` before the call; `test/exec.c` additionally requires the accumulated total to be non-zero, not merely non-decreasing | **VERIFIED.** Deleting the two `children_*time100ns +=` lines in `src/process/wait.c` `fill_child_rusage()` leaves the *previous* test green ("exec: all tests passed") and fails the new one. **Re-verified after the floors were made deterministic:** the same mutation now fails `test/exec.c` at both `ru_stime > 0` and `ru_utime > 0`, and `test/posix-grp.c` at both `tms_cutime > 0` and the `RUSAGE_CHILDREN` tick floor. Note the earlier justification for this row's `> 0` floor — "measured under stock apt Wine: 0.17s by this point" — was measuring `exec.exe`'s **own** kernel time, not any child's: stock Wine's `NtQueryInformationProcess(ProcessTimes)` ignored the handle and returned the *calling* process's times (`dlls/ntdll/unix/process.c`, `case ProcessTimes:`, "FIXME: user/kernel times only work for current process"; fixed upstream in our fork by `aa1c505c2`/`94e2d180c`). Both files now reap a child that has confirmed its own CPU against its own `times()`, and probe `wait4()`'s per-child `struct rusage` against that confirmed floor to decide at run time whether the platform reports child times at all — exiting 77 (UNVERIFIED) rather than green where it does not |
| `test/posix-grp.c` `test_times_children` (2 sites) | `t.tms_cutime == timeval_to_clockticks(&ru_children.ru_utime)` is the right *shape* — two readers of one accumulator (`children_utime100ns` in `src/process/wait.c`) — but `0 == 0` agrees whether or not either reader works. The `--times-child` role already burned CPU to avoid this; nothing checked that the burn showed up | assert the total is non-zero before comparing the two readers; poison both destinations. The burn is **measured, not fixed-size**: `burn_user_ticks()` loops until this process's own `tms_utime` has advanced by `BURN_TICKS`, and the child exits 3 if it never does, so a zero in the parent cannot be blamed on a fast machine | **VERIFIED.** Same mutation (deleting the two `children_*time100ns +=` lines): previous test green, new one fails at `test/posix-grp.c:737` and `:738`, printing `tms_cutime=0 ticks … (child confirmed >= 20)` |
| `test/posix-grp.c` `test_times_self` (2 sites) | `t.tms_utime >= utime_ticks` where a test process that has done nothing reports `0 >= 0`, and the "not off by an order of magnitude" bounds were `0 - 0 < 500`. Passed with `times()` and `getrusage()` both writing nothing | burn measurable user CPU first, then require `t.tms_utime > 0` as well as the cross-check; the burn is `burn_user_ticks()`, measured rather than a fixed iteration count | **VERIFIED** by the same mutation round |
| `test/posix-select-socket.c` `poll_one()` — 5 caller sites | the helper seeded `p.revents = 0` and then handed the value back to five callers each asserting `revents == 0`. They read back what the helper had just written, so a `poll()` that ignored `revents` entirely satisfied all five | seed `-1`, the idiom `test/posix-sysmisc.c` already uses | **VERIFIED indirectly.** Deleting `p->revents = 0` in `src/select/poll.c` fails `test/posix-sysmisc.c:573` and `:587` — the `-1` idiom catching exactly this. It cannot be shown on `posix-select-socket.exe` itself, which reports rc=77 unverified under stock Wine (`bind()` → `errno=5`) |
| `test/posix-glob.c` `test_globfree_idempotent` | asserted `gl_pathc == 0` and `gl_pathv == NULL` after `globfree()` without ever establishing the count had been non-zero | assert `gl_pathc >= 1` and `gl_pathv != NULL` between the `glob()` and the frees | **VERIFIED.** Forcing `pglob->gl_pathc = 0` in `src/glob/glob.c` fails the new `test/posix-glob.c:1153` |
| `test/posix-glob.c` wordexp free-idempotence | same shape for `we_wordc == 0` after a double `wordfree()` | assert `we_wordc == 2` and `we_wordv != NULL` first | **VERIFIED.** Forcing `pwordexp->we_wordc = 0` in `src/wordexp/wordexp.c` fails the new `test/posix-glob.c:1383` |
| `test/posix-glob.c` `nftw_cb` | `f->level == 0` was checked only for the walk root, where 0 is the correct value — so it could not distinguish a `level` `nftw()` computes from a `struct FTW` it never writes | assert `f->level > 0` and the `base` offset for every non-root path | **VERIFIED, and the sharpest of the set.** Forcing `f.level = 0` in `src/ftw/ftw.c`'s `report()` leaves the previous `test/posix-glob.c` passing outright ("posix-glob: all ok", rc=0) and fails the new `:2852` three times. `test/posix-tail.c` does check non-zero levels, but it is rc=77 unverified under stock Wine, so *nothing in a green `make check`* caught this before |

### UNCERTAIN

`test/posix-fork-clauses-win.c:340` — the forked child reports `RC_TIMES`
if `t.tms_cutime != 0 || t.tms_cstime != 0`, checking fork.html's "The
child process values of tms_utime, tms_stime, tms_cutime, and tms_cstime
shall be set to 0." This is the exact clause `a8a3016` fixed
(`__rusage_children_reset()`), and it is discriminating **only if the
parent's own accumulated child time is non-zero when the fork happens** —
which nothing in the file establishes. Three earlier tests do reap
children first, so on the real-Windows legs it is very likely non-zero,
but "likely" is not a measurement and this file is filtered out of the
Wine leg (`TEST_RUN = $(filter-out %-win.exe,...)`), so it could not be
run here to find out. Left untouched rather than guessed at: adding an
unverifiable assertion to a test only the `windows-test` legs execute
would risk turning that leg red for a reason nobody could reproduce
locally.

### DISCRIMINATING — the ones that already hold, and what makes them hold

Recorded because the *reason* is the reusable part; each names the line
that supplies the non-zero pre-state or companion.

| site | what makes the zero meaningful |
|---|---|
| `test/exec.c:534`, `test/process-win.c:207` `ru_child.ru_*.tv_sec >= 0` | `memset(&ru_child, 0xff, ...)` immediately before the `wait4()` — an untouched struct reads back −1 and fails |
| `test/posix-sysmisc.c:573`, `:587` `pfd.revents == 0` | `pfd.revents = -1` on the preceding line |
| `test/posix-select-socket.c:384`, `:385`, `:402` `pfd[i].revents == 0` | `pfd[i].revents = -1` at `:381`–`:382` and `:399` (these three were already correct; only the `poll_one()` helper was not) |
| `test/posix-sysmisc.c:1085` `f_files == f_ffree == f_favail == 0` | documented zeros, but the same struct is proved populated by `f_frsize > 0 && f_bsize > 0` at `:1071` and `f_blocks > 0` at `:1077` |
| `test/misc.c:218` `sig_calls == 0` while blocked | `CHECK(sig_calls == 1)` at `:222` after the unblock — the counter is proved to be genuinely tracked |
| `test/posix-glob.c:1777` `m[1].rm_so == 0 && rm_eo == 0` | the preceding `regexec()` into the *same* array left `−1/−1` and is asserted so at `:1769` |
| `test/posix-tail.c:461` `level == 0 && base == 0` | `:462`–`:464` assert levels 1, 1, 2 and bases 9, 9, 13 for the other entries |
| `test/posix-tail.c:432`, `:436` `nent == 0` | `reset_walk()` plus `CHECK(nent == 4)` at `:459` |
| `test/posix-tail.c:932` `st.st_size == 0` after `O_TRUNC` | `st.st_size == 4096` at `:921` on the same struct |
| `test/posix-unistd.c:348` `st.st_size == 0` after `creat()` | `st.st_size == 10` at `:345` on the same struct |
| `test/posix-unistd.c:552`, `:557` directory `st_size == 0` | `S_ISDIR(st.st_mode)` at `:551` proves `stat()` populated the struct |
| `test/posix-limits.c:736` `imaxdiv(0,5)` quot/rem zero | `:732`–`:735` assert non-zero quot/rem from the same function |
| `test/time.c:53` `tm_isdst == 0` | `check_tm()` asserts seven other fields against known non-zero values at `:45`–`:52` |
| `test/posix-time.c:528` `tm_wday == 0` | `:525`–`:526` assert non-zero year/mday/hour/min/sec on the same struct |
| `test/posix-io.c:327` `ftell(f) == 0` | `CHECK(ftell(f) == 1)` at `:331` |
| `test/sh-engine.c:265` `pl->bang == 0` | `CHECK(pl->bang == 1)` at `:278` |
| `test/sh-engine.c` list terminators (`->next == 0`, 11 sites) | each is the tail of an inline non-NULL chain in the same expression, and `only_command()` asserts `l->items != 0` at `:91` before any of them |
| `test/sh-engine.c:111`/`:145`/`:160` `assigns == 0` / `words == 0` | complementary pair: `test_simple_command_words` asserts `words != 0` while `assigns == 0`, `test_assignment_only_command` asserts `assigns != 0` while `words == 0` |
| `test/posix-ctype.c`, `test/posix-wctype.c` `is*(x) == 0` | every one is adjacent to an `is*(y) != 0` for the complementary character |

### A separate category, deliberately not counted

`CHECK(errno == 0)` after a successful call (`test/posix-io.c:95`,
`:101`, `:109`, `:251`, `test/posix-misc.c:79`) *looks* like this family
but is not. There the absence of a write is the requirement itself —
XSH 2.3 (`basedefs/V1_chap02.html`) permits a function to set `errno` on
success only where its page says so, so "nothing was written" is the
positive result being asserted, not a proxy for one. The explicit
`errno = 0` before each call is what makes it well-formed.

### One platform where these clauses have no object

The three non-zero child-CPU-time assertions are compiled only for the
PE build (`#ifdef _WIN32`), and the native run prints a note saying so.
`tools/asan-build.sh` builds this suite against `fuzz/ntstubs.c`, whose
`NtQueryInformationProcess` answers `ProcessTimes` for the calling
process only and returns `STATUS_NOT_IMPLEMENTED` for any child handle;
`fill_child_rusage()` bails on that status, so the accumulator is
*legitimately* zero there and no assertion could tell that apart from a
real accounting failure. Guarding it and saying so is the point of the
whole exercise: a run that did not test a clause must not read like one
that did. `make asan` after this change: 53/60 passed, 7 unverified, 7
not applicable natively, **0 unlinkable**.

### Negative control

Making `strverscmp()` return 0 unconditionally
(`src/string/strverscmp.c`) turns `test/string.c` red at `:107`–`:109`
and leaves every test touched by this group green (`exec`, `posix-grp`,
`posix-glob`, `posix-sysmisc`, `posix-select-socket`). A pass from them
therefore means "correctly indifferent", not "never ran".

## XBD header contents — the macros no ledger counts (group U)

Every other section of this file, and all of
`test/POSIX-GAP-ACCOUNTING.md`, is **function-granular**. That file
buckets all 1177 POSIX.1-2017 System Interfaces function interfaces, so
"function *X* is absent" is recorded somewhere for every *X*. The gap
this group audits is one level down: the **symbolic constants, macros
and limits XBD requires a header to define**, which are not function
interfaces and therefore appear in no ledger row, no test, and no header
banner. A 1177-row accounting looks exhaustive, and that is precisely
what made this surface invisible.

`<tar.h>` and `<cpio.h>` are the clearest proof of the shape of the
blind spot: they declare **no functions at all**, so they are outside
that accounting's 1177 by construction rather than by oversight.

Method, reproducible end to end: fetch the XBD `basedefs/<hdr>.h.html`
page for every header in `include/`, extract every macro name from each
page's DESCRIPTION, then probe each name against the real include tree
with the **target preprocessor** —
`x86_64-win32-tcc -E -std=c99 -nostdinc -I arch/$(ARCH) -I arch/generic
-I obj/include -I include` — rather than by reading the headers. A macro
can be present in a header and unreachable behind a feature-test guard;
only the preprocessor settles which. Every "missing" below is a
preprocessor result, not a grep result.

### Audited and clean (group U)

These are as much a result as the gaps: an unaudited surface and an
audited-and-clean surface look identical from outside.

| surface | scope of the check | result |
|---|---|---|
| cross-header exposure requirements | all **68** "the `<X>` header shall define *T* as described in `<Y>`" sentences extracted from the XBD pages; every one applying to a header we have was **compile**-probed (`<sys/wait.h>`/`siginfo_t`+`id_t`+`sigval`, `<sys/stat.h>`/`struct timespec`+`blkcnt_t`+`blksize_t`, `<sys/select.h>`/`sigset_t`+`suseconds_t`, `<sys/time.h>`/`fd_set`, `<time.h>`/`clockid_t`+`timer_t`+`locale_t`+`pid_t`, `<dirent.h>`/`ino_t`, `<termios.h>`/`pid_t`, `<unistd.h>`/`intptr_t`+`size_t`+`ssize_t`+`off_t`+`uid_t`+`gid_t`+`pid_t`+`SEEK_*`) | **clean.** The single failure is `<signal.h>`/`pthread_t`, which is the already-recorded absent `pthread.h` family — not a new finding |
| `<inttypes.h>` `PRI*`/`SCN*` | the full matrix: 6 conversions × {plain, `LEAST`, `FAST`} × {8,16,32,64}, plus `MAX` and `PTR` | **complete, zero missing** |
| `<errno.h>` | all 81 mandated error macros | 76 present, 5 missing — see below |
| headers with **no** missing mandatory macro | `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<strings.h>`, `<wchar.h>`, `<wctype.h>`, `<locale.h>`, `<poll.h>`, `<glob.h>`, `<fnmatch.h>`, `<wordexp.h>`, `<regex.h>`, `<ftw.h>`, `<search.h>`, `<sys/stat.h>`, `<sys/statvfs.h>`, `<sys/uio.h>`, `<sys/times.h>`, `<sys/utsname.h>`, `<sys/socket.h>`, `<time.h>` | **clean** (15+ headers) |

Deliberately **not** fenced by this group, because they are already
recorded elsewhere and so are not silence: `<netinet/in.h>`'s IPv6 set
and `<sys/socket.h>`'s `struct msghdr`/`cmsghdr`/`CMSG_*` (both headers'
own banner scope notes), `<sched.h>`'s `SCHED_*` (that header's banner,
and `POSIX-GAP-ACCOUNTING.md`), and `<termios.h>`'s output-delay masks
(`test/posix-termios.c`, already fenced).

### Where a group U fence lives (group U)

A header-content fence must **fail the way a consumer fails**. Where
an XBD clause is about what *one* header supplies on its own, the test
needs a translation unit that includes only that header, and it lives
in `test/posix-headers.c` — a file whose top is a sequence of small
TU-shaped "islands", each including exactly the header its clause is
about, each carrying its test immediately after that `#include`, and
ordered so no earlier island can supply a name a later one probes.
`<fcntl.h>`'s `SEEK_*` clause is the worked example: `<stdio.h>` and
`<unistd.h>` both define `SEEK_SET`, so in any ordinary test file that
clause degrades into a runtime assertion about a recorded flag, which
tests a different thing than the one that breaks people — a consumer
meets it as a compile error in a single-header TU.

Where a clause is only about a name existing **somewhere**, an existing
test file is the right home and `posix-headers.c` is the wrong one.
That file adds one binary to `make check` (57 -> 58 passed); every
clause in it is currently fenced, and it passes because its harness
self-check is the only thing that runs.

### Findings (group U)

| header | clause | triage | status | test |
|---|---|---|---|---|
| `errno.h` | `errno.h.html` DESCRIPTION — "The `<errno.h>` header shall define the following macros ... distinct positive values ... suitable for use in `#if` preprocessing directives", 81 names, unconditional (no option marker on any of them) | **ABSENT** | **UNIMPL (fenced)** — `EBADMSG`, `EMULTIHOP`, `ENETRESET`, `ENOLINK`, `EPROTO` are in no header in `include/`. The other 76 are present. Consumer impact: gnulib's `errno`/`strerror-override` modules name four of the five directly. Observed: fails to **compile**, `'EBADMSG' undeclared` | `test/posix-errno.c` fence `test_errno_mandatory_macros` |
| `fcntl.h` | `fcntl.h.html` DESCRIPTION — "The `<fcntl.h>` header shall define the values used for `l_whence`, `SEEK_SET`, `SEEK_CUR`, and `SEEK_END` as described in `<stdio.h>`" | **ABSENT** | covered — FIXED (9587e7e); the fenced gap was: defined in `<stdio.h>` and `<unistd.h>`, not in `<fcntl.h>`. The sentence exists so a TU doing record locking (which needs `<fcntl.h>` for `struct flock` and `F_SETLK`) can fill in `l_whence` without `<stdio.h>`; such a TU compiles on glibc and musl and fails here. Acceptance criterion is the three definitions and nothing more — `lseek()`/`fcntl()` already honour the three values (priority 6). Observed: fails to **compile**, `'SEEK_SET' undeclared`, in an isolated single-header TU — which is why it lives in `test/posix-headers.c` rather than in a file that includes `<stdio.h>` for the rest of its audit | `test/posix-headers.c` fence `test_fcntl_h_defines_seek_whence` |
| `fcntl.h` | `fcntl.h.html` DESCRIPTION — the file-access-mode list ("The values shall be unique, except that `O_EXEC` and `O_SEARCH` may have equal values"), plus "`O_TTY_INIT` ... can have the value zero and in this case it need not be bitwise-distinct" | **ABSENT** | **UNIMPL (fenced)** — `O_EXEC`, `O_SEARCH`, `O_TTY_INIT` are in no header in `include/`; every other `O_*` POSIX lists, including `O_DSYNC`, `O_RSYNC`, `O_DIRECTORY` and `O_NOFOLLOW`, is present. None of the three is optional, and `O_TTY_INIT`'s permission to be zero is the standard's own way of saying an implementation with nothing to do for it still defines it. The fence covers the **header constants only** — whether `open()` would then have to give `O_SEARCH` a traverse-only directory handle is a separate, larger gap it does not claim. Observed: fails to **compile**, `'O_EXEC' undeclared` | `test/posix-io.c` fence `test_fcntl_h_access_mode_constants` |
| `unistd.h` | `unistd.h.html` DESCRIPTION — "The `<unistd.h>` header shall define the following symbolic constants for `sysconf()`:", 125 `_SC_*` names, unconditional | **ABSENT** | **UNIMPL (fenced)** — 15 of 125 defined, **110 absent**. Acceptance criterion is **both** halves, not just the `#define`: `sysconf.html` specifies `[EINVAL]` only for an *invalid* name, and every name on the list is valid by being on it, so a definition alone would leave `src/unistd/sysconf.c`'s `default: errno = EINVAL` answering "no such name" for a name `<unistd.h>` mandates — the declared-but-unimplemented trap exactly. The truthful answer for an unsupported option is `-1` with **errno unchanged**, which the test accepts. Consumer impact: autoconf/gnulib probe `_SC_SYMLOOP_MAX`, `_SC_IOV_MAX`, `_SC_GETPW_R_SIZE_MAX` routinely. Observed: fails to **compile**, `'_SC_2_CHAR_TERM' undeclared` | `test/posix-unistd.c` fence `test_unistd_sysconf_names` |
| `unistd.h` | `unistd.h.html` DESCRIPTION — "...the following symbolic constants for `pathconf()`:", 21 `_PC_*` names | **ABSENT** | **UNIMPL (fenced)** — 9 of 21 defined, **12 absent** (`_PC_2_SYMLINKS`, `_PC_ALLOC_SIZE_MIN`, `_PC_ASYNC_IO`, `_PC_FILESIZEBITS`, `_PC_PRIO_IO`, the four `_PC_REC_*`, `_PC_SYMLINK_MAX`, `_PC_SYNC_IO`, `_PC_TIMESTAMP_RESOLUTION`). Unlike the `_SC_` list this does **not** require every name to be answerable — `fpathconf.html` makes "[EINVAL] The implementation does not support an association of the variable *name* with the specified file" a *may fail* — so the test asserts only that both entry points decide the *same* thing, extending `test_fpathconf`'s existing shape. Observed: fails to **compile**, `'_PC_2_SYMLINKS' undeclared` | `test/posix-unistd.c` fence `test_unistd_pathconf_names` |
| `unistd.h` | `unistd.h.html` DESCRIPTION — "...the following symbolic constants for the `confstr()` function:", 31 `_CS_*` names | **ABSENT** | **UNIMPL (fenced)** — only `_CS_PATH` defined; **30 absent** (the `_CS_POSIX_V6_`/`_CS_POSIX_V7_` programming-model `CFLAGS`/`LDFLAGS`/`LIBS` triples, `_CS_POSIX_V7_THREADS_*`, both `_WIDTH_RESTRICTED_ENVS`, `_CS_V6_ENV`/`_CS_V7_ENV`). These are what a `getconf`-driven build system asks for — the bootstrap situation this libc is a target of. Acceptance criterion deliberately **the definitions only**: when this was written `confstr()`'s answers were entangled with the then-open `confstr()` `[EINVAL]` BUG, so no assertion could tell "recognized, empty value" from "unrecognized". That BUG is now fixed, and the fix names the follow-on precisely — `confstr()`'s recognized set is a closed `switch` over the `_CS_*` names `<unistd.h>` defines, so each of the 30 must gain a `case` in `src/unistd/sysconf.c` as it gains its `#define`, or it will be reported `[EINVAL]`. Observed: fails to **compile**, `'_CS_POSIX_V6_ILP32_OFF32_CFLAGS' undeclared` | `test/posix-unistd.c` fence `test_unistd_confstr_names` |
| `unistd.h` | `unistd.h.html` "Constants for Options and Option Groups" — the thirteen constants whose text reads "This symbol shall **always** be set to the value 200809L", as against the section's general "The following symbolic constants, **if defined** in `<unistd.h>`, shall have a value of -1, 0, or greater" | **ABSENT** | **UNIMPL (fenced)** — none of the thirteen is defined, and no `_POSIX_*`/`_XOPEN_*` option constant at all beyond `_POSIX_VERSION`/`_POSIX2_VERSION`. **The acceptance criterion here is not "add a `#define`"**, which is why it is a clause of its own: `200809L` is a compile-time promise the application may test with `#if` and cannot re-check at runtime. Seven of the thirteen are thread-related and the pthread family is a recorded absence — defining `_POSIX_THREADS` as `200809L` with no threads would be a false claim and strictly worse than the omission. The gap is the **option**, not the constant. What the omission costs today: `#ifdef _POSIX_TIMERS` gets the same silence from a libc that *has* `clock_gettime()`/`clock_nanosleep()` as from one that has nothing. Observed: fails to **compile**, `'_POSIX_ASYNCHRONOUS_IO' undeclared` | `test/posix-unistd.c` fence `test_unistd_mandatory_option_constants` |
| `unistd.h` | `unistd.h.html` "Constants for Functions" — "`_POSIX_VDISABLE` This symbol shall be defined to be the value of a character that shall disable terminal special character handling... This symbol shall always be set to a value other than -1" | **ABSENT** | **UNIMPL (fenced)** — not defined anywhere in `include/`, although `pathconf(_PC_VDISABLE)` already answers `0` (`src/unistd/sysconf.c`) and `<termios.h>` is implemented and audited (group A): the value already exists inside the library, only the constant naming it is missing. Mandatory in its own right — it is not in the options section and carries no option marker. Consumer impact: coreutils' `stty` reads it at **compile** time to print and set `undef`, so its absence is a build failure, not a degraded answer. Acceptance criterion: the definition, agreeing with what `pathconf(_PC_VDISABLE)` already reports. Observed: fails to **compile**, `'_POSIX_VDISABLE' undeclared` | `test/posix-unistd.c` fence `test_unistd_posix_vdisable` |
| `limits.h` | `limits.h.html` "Minimum Values" — "The `<limits.h>` header shall define the following symbolic constants with the values shown", for the three entries carrying **no** option-group marker | present | covered — FIXED: the three unmarked Minimum Values are defined with the standard's exact values (4/128/64). They are portable floors an application may rely on, not a claim about ntlibc, which is why defining them is safe while threads are absent | `test/posix-limits.c` fence `test_limits_minimum_values_unmarked` |
| `limits.h` | `limits.h.html` "Minimum Values", same sentence, for its three `[XSI]` entries | present | covered — FIXED: `_XOPEN_IOV_MAX`/`_XOPEN_NAME_MAX`/`_XOPEN_PATH_MAX` defined as 16/255/1024. `test_limits_pathname`'s `CHECK(IOV_MAX >= 16)` literal is replaced by the macro, which is what the fence asked for | `test/posix-limits.c` fence `test_limits_minimum_values_xsi` |
| `limits.h` | `limits.h.html` "Runtime Increasable Values" — "The magnitude limitations in the following list shall be fixed by specific implementations. An application should assume that the value of the symbolic constant defined by `<limits.h>` ... is the minimum that pertains" | present | covered — FIXED: all nine defined. Seven at their `_POSIX2_` floors (no larger capability to claim: bc is not ours, collation is C-locale only); `LINE_MAX` 4096 to match `sysconf(_SC_LINE_MAX)`, and `RE_DUP_MAX` 32767 to match `regex.c`'s `DUP_MAX` — a Runtime Increasable value states what THIS implementation supports, so the floor would understate both | `test/posix-limits.c` fence `test_limits_runtime_increasable` |
| `limits.h` | `limits.h.html` "Other Invariant Values" — "The `<limits.h>` header shall define the following symbolic constants:" | present | covered — FIXED: all six defined at the printed minima. `NL_ARGMAX` = 9 deliberately although `%n$` is unimplemented — omitting it breaks a consumer that merely references the constant, while defining it can only mislead one already broken by printf; the gap is fenced against printf as `test_printf_positional_arguments`. `NZERO` coexists with `<sys/resource.h>`'s identical definition | `test/posix-limits.c` fence `test_limits_other_invariant` |
| `signal.h` | `signal.h.html` DESCRIPTION — "`[CX]` The `<signal.h>` header shall define the symbolic constants in the Code column of the following table for use as values of `si_code`". `CX` marks an extension to ISO C that POSIX requires, not an option group, so the whole table is mandatory | **ABSENT** | **UNIMPL (fenced)** — 31 of 40 defined, **9 absent**: `ILL_ILLOPN`, `ILL_ILLADR`, `ILL_ILLTRP`, `ILL_PRVREG`, `ILL_COPROC`, `ILL_BADSTK`, `FPE_FLTSUB`, `BUS_ADRERR`, `BUS_OBJERR`. A ragged edge, not a missing family — `ILL_ILLOPC` and `ILL_PRVOPC` are present while their six siblings are not, `BUS_ADRALN` is present while the other two are not — which is the shape only a spec-inward sweep finds: nothing looks incomplete from inside the tree, and the existing `si_code` tests happen to provoke only codes that exist. The fence claims **nothing** about ntlibc ever *delivering* the nine; a portable handler switches on them regardless, and one that cannot name a code cannot have a default branch for it. Observed: fails to **compile**, `'ILL_ILLOPN' undeclared` | `test/posix-signal.c` fence `test_signal_si_code_constants` |
| `tar.h` | `tar.h.html` DESCRIPTION — "The `<tar.h>` header shall define the following symbolic constants with the indicated values" (`TMAGIC`/`TMAGLEN`/`TVERSION`/`TVERSLEN`, nine typeflags, twelve octal mode bits) | **ABSENT** (whole header) | covered — FIXED (4e4782e); the fenced gap was: `include/tar.h` does not exist. POSIX **base**: the SYNOPSIS carries no option marker; the only `[XSI]` on the page is the single constant `TSVTX`. **This is the clearest proof of the shape of the blind spot group U audits**: `<tar.h>` declares no functions at all, so it is outside `POSIX-GAP-ACCOUNTING.md`'s 1177 function interfaces *by construction* — no function-granular accounting, however exhaustive, can record its absence, and nothing in the tree did. Pure constants header; nothing in `src/` would change. Consumer: GNU tar and pax. Observed: fails to **compile**, `include file 'tar.h' not found` | `test/posix-headers.c` fence `test_tar_h_constants` |
| `cpio.h` | `cpio.h.html` DESCRIPTION — "shall define the symbolic constants needed by the `c_mode` field of the cpio archive format" (20 `C_*` octal constants) and "shall define the following symbolic constant as a string: `MAGIC "070707"`" | **ABSENT** (whole header) | covered — FIXED (this commit); the fenced gap was: `include/cpio.h` does not exist. **POSIX base, not XSI**, correcting the obvious assumption: the page's own CHANGE HISTORY says "Issue 7 The `<cpio.h>` header is moved from the XSI option to the Base." So its absence is a base-conformance hole, not a missing option group. Function-free like `<tar.h>` and invisible to the 1177 accounting for the same reason. Observed: fails to **compile**, `include file 'cpio.h' not found` | `test/posix-headers.c` fence `test_cpio_h_constants` |
| `sched.h` | `sched.h.html` DESCRIPTION — "The `<sched.h>` header shall define the `sched_param` structure ... This structure shall include at least the following member: `int sched_priority`" | present | covered — FIXED: `<sched.h>` now exposes the struct via `__NEED_struct_sched_param`, the way `<spawn.h>` already did. The clause carries NO option margin marker (the sentences either side do — `[PS]` on `pid_t`, `[SS|TSP]` on `time_t`), so the struct is POSIX base even though every policy and function that uses it is `[PS]` and remains declined. `include/sched.h`'s banner previously grouped the struct with those optional members and is corrected |TPS]`, `SCHED_RR [PS\|TPS]`, `SCHED_SPORADIC [SS\|TSP]`, `SCHED_OTHER [PS\|TPS]` and puts every function inside a `[PS]`/`[PS\|TPS]` region, but the `sched_param` sentence carries **no marker and sits outside every option region** — verified mechanically by counting the page's own `opt-start`/`opt-end` delimiters (they balance to zero before it) and by reading the sentences either side, which do carry theirs. So the struct is base and unconditional while the things the banner groups it with are genuinely optional. Acceptance criterion: one `__NEED_struct_sched_param` include, the way `<spawn.h>` already does it. The fence claims **nothing** about `_POSIX_PRIORITY_SCHEDULING` — the banner's reasoning for declining the policies and functions stands untouched. Observed: fails to **compile**, incomplete type | `test/posix-headers.c` fence `test_sched_h_defines_sched_param` |

### Open, not decided by this group (group U)

Seven entries of `limits.h.html`'s "Minimum Values" table are absent
from ntlibc and are **not** fenced: `_POSIX_MQ_OPEN_MAX` and
`_POSIX_MQ_PRIO_MAX` `[MSG]`, `_POSIX_SS_REPL_MAX` `[SS|TSP]`, and
`_POSIX_TRACE_EVENT_NAME_MAX`, `_POSIX_TRACE_NAME_MAX`,
`_POSIX_TRACE_SYS_MAX`, `_POSIX_TRACE_USER_EVENT_MAX` `[OB TRC]`. The
section's sentence is "shall define ... with the values shown", but each
of these seven carries an option-group margin marker for an option
ntlibc does not claim, and whether that sentence survives the marker is
a question this audit does not answer on its own authority. Left open
deliberately rather than fenced on a guess or dismissed as N/A.

### Not fenced on purpose (group U)

`<limits.h>`'s "Runtime Invariant Values (Possibly Indeterminate)" and
"Pathname Variable Values" sections both say a definition "**shall be
omitted** from `<limits.h>` on specific implementations where the
corresponding value is equal to or greater than the stated minimum, but
is unspecified/can vary". Every name in those two sections is therefore
legally absent, and none is fenced. Do not "complete the set" later:
fencing a legally-omittable constant would be manufacturing a finding.
The three sections that do say "shall define ... with the values shown"
— Minimum Values, Runtime Increasable Values, Other Invariant Values —
are the ones this group fences.

## The shell engine's IO_NUMBER lexer (XCU 2.7, 2.10.1)

This ledger is organised by header and function, and the shell engine
(`src/sh/`, tested by `test/sh-engine.c`) is neither: it implements XCU
chapter 2, the Shell Command Language, which has no function page of
its own. There is therefore no chapter-2 section above, and this one
does not try to become the missing audit of the whole chapter — it
records the one clause whose fence was lifted, so that
`tools/lint-ledger.sh`'s two directions have something to agree with
and a successor auditing the rest of chapter 2 has a place to start.

| clause | requirement | status | test |
| --- | --- | --- | --- |
| XCU 2.7 Redirection, 2.10.1 Shell Grammar Lexical Conventions | "the number shall be a file descriptor number"; IO_NUMBER is a token "made up solely of digits" immediately followed by `<` or `>` — with no length limit stated on the digit string | covered — **was a fenced BUG, now FIXED**: `src/sh/parse.c` accumulated the digits into an `int` with an unguarded `v = v * 10 + (w[i] - '0')`, so fifteen digits was signed overflow (undefined behaviour, and where it did not trap the redirection kept whatever the wrap produced — possibly a negative fd) | test/sh-engine.c (`test_ionum_overflow`) |

The clause puts no bound on the digit string, but the value has to
become a redirection's `fd`, which is an `int`, so a digit string that
does not fit one names no file descriptor that could ever be
redirected. The lexer now guards the multiply and diagnoses such a
string (`file descriptor number too large: ...`) instead of wrapping
it. Demoting it to an ordinary WORD was the other candidate and was
rejected: it would silently turn `2147483648<x` into a command *named*
`2147483648` — a different program, accepted without a word — which is
exactly the "subtly wrong is worse than no shell" failure
`test/sh-design.md` says matters most here.

The boundary is asserted from both sides, so the guard cannot be
satisfied by refusing every IO number: `2147483647<x` (INT_MAX, in
range by one) still lexes as an IO_NUMBER and reaches the redirection
carrying that value, and `cmd 2>out 10<in` still parses with `fd` 2 and
10.

The defect was found by `fuzz/fuzz_shparse.c` under UBSan; that
harness's `ionum_fence()` — which kept the fuzzer off any input with
ten or more consecutive digits — has been deleted along with the fence.

**Four other fences remain in `test/sh-engine.c`** (a redirection-list
leak, a here-document queue leak, and two printer round-trip bugs).
None of them is recorded here, and none is this section's business;
they are listed in `tools/ledger-baseline.txt` as class-B entries —
fenced, with no row in this file — which is where the rest of the
chapter-2 audit will have to start.
