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

1. `string.h` / `strings.h` — **in progress**, see below
2. `stdlib.h` conversions, `qsort`/`bsearch` — not yet reached
3. `time.h` calendar functions — not yet reached (partial ad-hoc coverage
   already exists in `test/time.c` and `test/posix-parse.c`)
4. `dirent.h` — not yet reached (ad-hoc coverage in `test/dirent.c`)
5. `stdio.h` streams — not yet reached (ad-hoc coverage in `test/stdio.c`)
6. `unistd.h` process/file ops — not yet reached (ad-hoc coverage in
   `test/unistd.c`, `test/posix-io.c` — **owned by another agent, avoid**)
7. `signal.h` — not yet reached
8. `wchar.h` — not yet reached (the `wcs*`/`wmem*` functions currently
   live in `test/string.c`'s sanity pass; re-audit them here when this
   pass reaches wchar.h rather than duplicating)

Also not yet reached at all: `ctype.h`, `math.h`, `stdlib.h` non-numeric
(getenv/setenv/random family), `fcntl.h` (owned by another agent),
`sys/stat.h`, `sys/wait.h`.

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
| strsignal | not POSIX.1-2017 base (XSI/GNU); maps signal number to message | N/A (XSI extension, not base) | test/string.c (sanity only) |
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
way to force allocation failure under Wine could close it). Otherwise,
move on to `stdlib.h` (priority 2) next.
