<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# POSIX conformance coverage: dirent.h, ctype.h, locale.h, libgen.h,
# setjmp.h, getopt()

Fragment of the clause-by-clause POSIX.1-2017 audit for the "dirent.h +
smaller headers" group. See `test/POSIX-COVERAGE.md` for the overall
ledger, method and status vocabulary (not edited by this session -- that
file is owned by the session that started the audit). New clause-cited
assertions are in `test/posix-misc.c`. Existing ad-hoc coverage lives in
`test/dirent.c`, `test/ctype.c`, `test/getopt.c` and `test/misc.c`
(not modified this session).

## dirent.h

| function | clause checked | status | test |
|---|---|---|---|
| opendir | positioned at first entry; ENOTDIR on a non-directory path component; ENOENT on missing/empty dirname | covered | test/dirent.c (basic), test/posix-misc.c (ENOTDIR, ENOENT, empty string) |
| fdopendir | ENOTDIR if fd does not reference a directory | covered | test/posix-misc.c |
| readdir | errno unchanged on success and at end-of-directory (must be pre-set to 0 by the caller) | covered | test/posix-misc.c |
| readdir_r | *result == entry on success, NULL at end; return value is an error number, not errno | covered | test/posix-misc.c |
| rewinddir | resets to the beginning of the stream (re-readable after running dry); void return | covered | test/dirent.c (rewind + recount), test/posix-misc.c (dry-then-rewind, from "." again) |
| rewinddir / readdir | whether a file added *after* opendir()/rewinddir() is visible | explicitly unspecified by readdir.html, not asserted | see "Bugs found / observations" below |
| telldir / seekdir | seekdir(dp, telldir(dp)) is a no-op; seeking to an earlier telldir() value reproduces that position | covered | test/dirent.c, test/posix-misc.c |
| dirfd | returns a valid, usable fd for the stream | covered | test/dirent.c (>=0), test/posix-misc.c (fcntl(fd, F_GETFD) succeeds) |
| closedir | returns 0 on success | covered | test/posix-misc.c |
| scandir | returns entries sorted by the comparator; "." and ".." included like readdir() | covered (pre-existing) | test/dirent.c |
| alphasort | sorts by name (ntlibc uses strcmp, equivalent to strcoll in the only locale ntlibc supports) | covered (pre-existing) | test/dirent.c |

### Observation, not a bug: rewinddir() and a file added mid-stream

`rewinddir.html` DESCRIPTION says rewinddir "shall also cause the
directory stream to refer to the current state of the corresponding
directory, as a call to opendir() would have done." Taken alone this
reads as "you should see files added after opendir()". But
`readdir.html` DESCRIPTION is the more specific clause and explicitly
overrides that reading: "If a file is removed from or added to the
directory after the most recent call to opendir() or rewinddir(),
whether a subsequent call to readdir() returns an entry for that file is
unspecified."

Empirically (via a throwaway native/Wine probe, not committed): opening
a directory, creating a new file in it, then calling `rewinddir()` +
`readdir()` on the *same* handle does **not** show the new file, even
though `RestartScan = TRUE` is passed to `NtQueryDirectoryFile()`
(`src/dirent/rewinddir.c`). A fresh `opendir()` on the same path *does*
see it. This looks like an NT directory-handle enumeration cache (the
FCB keeps its own notion of the listing until the handle is reopened),
which matches real Windows behavior in other implementations' bug
reports, not a Wine-only quirk -- and either way POSIX leaves the
outcome unspecified, so `test/posix-misc.c` does not assert either
direction. Recorded here rather than silently dropped in case a
successor wants to chase whether real hardware differs from Wine here.

## ctype.h

| function | clause checked | status | test |
|---|---|---|---|
| is*/to* family | argument must be representable as unsigned char or equal EOF; UB otherwise | covered | test/ctype.c (full 0..255 + EOF table, pre-existing), test/posix-misc.c (confirms plain `char` is signed on this target, so the trap is real; explicit EOF-returns-false-for-every-classifier and EOF-unchanged-by-to*() checks) |
| toupper / tolower | value with no case mapping (incl. EOF) is returned unchanged | covered | test/ctype.c, test/posix-misc.c |

No bugs found. ntlibc's ctype tables are branch-computed (`c >= 'A' &&
c <= 'Z'`-style), not a lookup table indexed by the raw (possibly
negative) `int`, so there is no out-of-bounds read for a bare negative
`char` promoted to `int` -- unlike a classic lookup-table libc, calling
these with a negative `char` here happens not to crash, but is still UB
per the clause and not something calling code should rely on.

## locale.h

| function | clause checked | status | test |
|---|---|---|---|
| setlocale | "C"/"POSIX" recognized for every category; NULL queries without changing; unsupported name returns NULL and leaves the global locale unchanged | covered | test/misc.c, test/getopt.c (pre-existing), test/posix-misc.c (explicitly re-queries after a rejected setlocale() to confirm the global state, not just the return value, is unchanged) |
| localeconv | struct lconv char members use CHAR_MAX (not a hardcoded 127) to mean "not available"; decimal_point is the one string member that must stay non-empty | covered | test/misc.c (values), test/posix-misc.c (CHAR_MAX from <limits.h>, decimal_point non-empty) |

No bugs found. `src/misc/locale.c`'s `__posix_lconv.frac_digits = 127`
etc. matches `CHAR_MAX` on this target (signed char) exactly.

## libgen.h

| function | clause checked | status | test |
|---|---|---|---|
| basename / dirname | full basename.html EXAMPLES table (POSIX-defined inputs) | covered | test/misc.c, test/getopt.c (pre-existing, overlapping coverage), test/posix-misc.c (table transcribed directly from the spec page, both functions checked per input with a writable copy) |
| basename / dirname | Windows drive-letter prefixes (`C:\`, `C:/foo`, `C:foo`) | ntlibc extension, not POSIX -- asserted separately | test/posix-misc.c |

No bugs found. "//" -> "/" or "//" is documented as implementation-defined
by POSIX itself; not asserted either way.

## setjmp.h

| function | clause checked | status | test |
|---|---|---|---|
| setjmp / longjmp | 0 direct return, nonzero via longjmp for several distinct values, longjmp(env,0) yields 1 | covered | test/misc.c (spot checks), test/posix-misc.c (loop over 5 distinct values + the 0 case) |
| longjmp | volatile automatic locals changed between setjmp/longjmp are preserved | covered | test/misc.c, test/posix-misc.c |
| sigsetjmp / siglongjmp | same value contract as setjmp/longjmp | covered | test/posix-misc.c (not exercised anywhere else in the suite) |

No bugs found. ntlibc has no real signal mask to save (NT has none in
the POSIX sense), so `sigsetjmp`/`siglongjmp` share the plain
`setjmp`/`longjmp` assembly body (see `src/setjmp/{i386,x86_64}/setjmp.S`
block comment) -- correct given the platform, but means the "restore the
signal mask" half of `sigsetjmp`'s contract is vacuously satisfied
rather than actually exercised.

**Native `make asan` note**: `test/posix-misc.c` is unlinkable under
`tools/asan-build.sh` (`asan: ... 1 unlinkable`, not a failure -- see
that script's pass/skip/nolink accounting) because `sigsetjmp` only
exists in ntlibc's arch-specific `.S` files, which the asan build never
compiles (it only picks up `src/*.c`, per its own comment). `make check`
under Wine on **both** i386 and x86_64 (`./configure --host=i386-win32`
/ `--host=x86_64-win32`, `CC` set to the bare `*-win32-tcc` name) is
20/20 on each -- that is the coverage that actually exercises the
assembly. Not fixed here: `tools/asan-build.sh` is shared test
infrastructure outside this session's `src/` scope
(`src/dirent/`, `src/ctype/`, `src/misc/`, `src/setjmp/`).

## getopt() (unistd.h / getopt.h)

| function | clause checked | status | test |
|---|---|---|---|
| getopt | "--" discarded, -1 returned, optind left at the first operand | covered | test/getopt.c (pre-existing permutation/termination coverage), test/posix-misc.c (explicit optind position + argv contents check right after "--") |
| getopt | leading ':' suppresses error messages / changes '?' to ':'; unknown option -> '?' with optopt set, independent of opterr | covered | test/getopt.c (':' cases), test/posix-misc.c (opterr=0 still yields the same return/optopt contract) |

No bugs found. `opterr`'s effect on whether a message is *printed* to
stderr is not captured here (stderr may be redirected by
`tools/runtests.sh`); only the opterr-independent return-value contract
is asserted, which is the part POSIX actually specifies as observable
by the caller.

## Extractions made

None needed. Every clause in scope was reachable and testable through
the public API; no internal decision function had to be pulled out
`__errno_from_status`-style.

## Not reached

- `getopt_long`/`getopt_long_only` (GNU extensions, not POSIX.1-2017) --
  already has ad-hoc coverage in `test/getopt.c`; out of scope for a
  POSIX clause audit since there is no spec page for them.
- `d_type`/`DT_*` as a _GNU_SOURCE/BSD extension to `struct dirent` --
  not a POSIX.1-2017 base member; `test/dirent.c` already exercises it.
- `EOVERFLOW`/`ENOENT` "may fail" error paths for `readdir`/`readdir_r`
  (stream position corruption, serial-number overflow) -- not
  triggerable without corrupting NT-internal state.
- Real (non-Wine) Windows behavior for the rewinddir-cache observation
  above -- flagged, not chased further.
