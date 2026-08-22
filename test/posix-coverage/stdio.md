<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# stdio.h coverage fragment

Owner group: `stdio.h`, per `test/POSIX-COVERAGE.md`'s priority order
(#5). This file is this group's private ledger fragment; the top-level
`test/POSIX-COVERAGE.md` is not touched by this group.

`test/stdio.c` (pre-existing, ~430 checks) is broad sanity coverage for
nearly every function in scope, including heavy `printf`/`scanf`
conversion-table exercises, `fmemopen`/`open_memstream`, `tmpfile`/
`tmpnam` (in a spawned child, see its `test_tmpnam_child`), `fdopen`/
`freopen`/`rename`/`remove`, `setvbuf`/`setbuf`, and `fseek`/`ftell`/
`rewind`/`fgetpos`/`fsetpos`/`ungetc` round-tripping. It was left
untouched, per this group's instructions. New clause-cited audit:
`test/posix-stdio.c` (this session) -- adds the clauses `test/stdio.c`'s
checks do not exercise, cited against
`https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html`.

`test/posix-io.c` (owned by `unistd.h`/`fcntl.h` group) also happens to
exercise `fopen`/`fclose`/`fread`/`fwrite`/`fseek`/`ftell`/`fflush`/
`ungetc`/`fputc`/`fgetc` incidentally while testing `open`/`read`/
`write`/`dup2` interop; not duplicated here.

| function | clause checked | status | test |
|---|---|---|---|
| fopen / fdopen / freopen | mode parsing, `+`/update semantics, fd takeover | covered | test/stdio.c |
| fopen | update-stream rule (fopen.html DESCRIPTION: output not directly followed by input without an intervening fflush/fseek/fsetpos/rewind, and vice versa unless EOF) | covered -- ntlibc's `__toread`/`__towrite` (src/stdio/buf.c) apply the implicit fflush/seek on every read<->write direction switch automatically, a strict superset of the requirement | test/posix-stdio.c `test_update_stream_rule` |
| fclose | closes fd, frees FILE, flushes pending writes | covered | test/stdio.c |
| fread / fwrite | byte counts, partial reads, `size`/`nmemb` == 0 | covered | test/stdio.c |
| fgetc / getc / getchar | EOF at end, unsigned-char return | covered | test/stdio.c |
| ungetc | one-char guarantee, return value, discarded by fseek/fsetpos/rewind, clears EOF indicator (ungetc.html DESCRIPTION) | covered | test/stdio.c (fseek discard, EOF-clearing) |
| ungetc | returns EOF when the stream is not open for reading (ungetc.html RETURN VALUE) | covered | test/posix-stdio.c `test_ungetc_errors` |
| fputc / putc / putchar | write, buffering interaction | covered | test/stdio.c |
| fgets / fputs / puts | NUL termination, newline handling, NULL at EOF-with-nothing-read | covered | test/stdio.c |
| getline / getdelim | growth, `-1` at EOF, delimiter | covered | test/stdio.c |
| fseek / fseeko | `SEEK_SET/CUR/END`, return 0/-1, clears EOF, undoes ungetc (fseek.html DESCRIPTION) | covered | test/stdio.c |
| ftell / ftello | position accounts for buffered-ahead/behind bytes on update streams | covered (this is the "`ftell` on update streams" bug class the task brief warns about; no violation found this session -- `ftello`'s comment in src/stdio/seek.c explicitly reasons through the r+/w+ case) | test/stdio.c |
| rewind / fgetpos / fsetpos | round-trip, `rewind` clears error indicator too | covered | test/stdio.c |
| fflush | `fflush(NULL)` flushes every open stream | covered | test/stdio.c |
| fflush | on a stream open for reading with an underlying fd: discards not-yet-reread `ungetc()` bytes and resyncs the fd offset to the stream position (fflush.html DESCRIPTION) | **BUG (fenced)** | test/posix-stdio.c `test_fflush_read_stream` |
| setvbuf | valid before any other operation on the stream, returns 0 | covered | test/posix-stdio.c `test_setvbuf` |
| setvbuf | returns non-zero for an invalid `type` (setvbuf.html RETURN VALUE) | **BUG (fenced)** | test/posix-stdio.c `test_setvbuf` |
| setbuf / setbuffer / setlinebuf | equivalence to a specific `setvbuf` call | covered | test/posix-stdio.c `test_setvbuf` (setbuf(f,NULL) == _IONBF, observable without an explicit fflush) |
| feof / ferror / clearerr | independent indicators; `clearerr` clears both at once (clearerr.html DESCRIPTION) | covered | test/stdio.c (feof/ferror individually), test/posix-stdio.c `test_clearerr_both` (both set at once, both cleared by one `clearerr`) |
| printf family | conversion table, flags/width/precision, return value = bytes transmitted on success | covered | test/stdio.c (snprintf-based `FMT` macro, ~200 cases), test/posix-stdio.c `test_fprintf_return` (a real `FILE*`, not just the memory-buffer path) |
| printf family | `[EILSEQ]` on an invalid wide-character code (fprintf.html ERRORS) | N/A -- ntlibc's formatter is POSIX-locale-only (see src/stdio/printf.c's header comment); there is no wide-character encoding step that can fail, so this error condition cannot arise | -- |
| printf family | `%n$` positional arguments (fprintf.html DESCRIPTION) | N/A (documented divergence) -- src/stdio/printf.c's header comment says these are not implemented; ntlibc has no notion of `$` in a conversion spec at all, so e.g. `"%1$d"` parses as width `1` followed by an unrecognized conversion character `$`, which falls to the "unknown conversion: emit literally" path and emits `"%$"`, consuming no argument, with the trailing `d` then copied through as an ordinary character -- net output `"%$d"`. Documented (divergent) behavior asserted, not the standard's | test/posix-stdio.c `test_printf_positional_divergence` |
| scanf family | conversion table, field width, `%n`, assignment suppression | covered (heavy pre-existing coverage, ~250 lines) | test/stdio.c |
| remove / rename | success, `ENOENT` | covered | test/stdio.c |
| tmpfile / tmpnam | uniqueness, buffer sizing (`L_tmpnam`), removed on close semantics via the read-after-write check | covered | test/stdio.c |
| perror | not separately clause-audited this session (thin wrapper: `strerror(errno)` to stderr) | not yet reached | -- |
| fileno | returns the underlying fd | covered (used incidentally, e.g. test/posix-stdio.c's raw `read(fileno(f),...)` in `test_fflush_read_stream`) | test/stdio.c |
| fmemopen / open_memstream | buffer growth, NUL-termination, mode parsing | covered | test/stdio.c |
| popen / pclose | not audited this session; no reachable POSIX shell semantics to check beyond src/stdio/misc.c's documented cmd.exe substitution, which is intentionally non-POSIX (no `/bin/sh`) | N/A (platform has no POSIX shell; ntlibc documents the divergence in src/stdio/misc.c) | -- |

## Bugs found this session

1. **`fflush()` on a readable stream never discards pending `ungetc()`
   bytes or resyncs the underlying fd offset.**
   `__fflush_locked` (`src/stdio/buf.c`) is:
   ```c
   if (!f->writable || !f->wpos) { f->wpos = 0; return 0; }
   ```
   For any stream not open for writing, this returns immediately
   without touching `f->nunget`, `f->rpos`/`f->rend`, or the fd's
   offset. Per
   [fflush.html](https://pubs.opengroup.org/onlinepubs/9699919799/functions/fflush.html)
   DESCRIPTION: "For a stream open for reading with an underlying file
   description, if the file is not already at EOF, and the file is one
   capable of seeking, the file offset of the underlying open file
   description shall be set to the file position of the stream, and any
   characters pushed back onto the stream by `ungetc()`... that have not
   subsequently been read from the stream shall be discarded." Two
   observable consequences, both fenced with `#if 0 /* BUG: ... */` in
   `test/posix-stdio.c`'s `test_fflush_read_stream`: (a) a byte pushed
   back with `ungetc()` then "flushed" is still delivered by the next
   `fgetc()` instead of the byte that should follow it; (b) a raw
   `read()` on `fileno(f)` right after `fflush(f)` sees whatever the
   internal read-ahead buffering left the fd at (potentially EOF, if a
   large buffer was filled), not the stream's current position. Fix
   belongs in `src/stdio/buf.c`'s `__fflush_locked` (or `fflush()`
   itself): for a readable, non-EOF, seekable stream, discard
   `f->nunget` and seek the fd back by `-(rend-rpos)` the same way
   `__towrite` already does.

2. **`setvbuf()` never validates its `type` argument.**
   `setvbuf()` (`src/stdio/buf.c`) only special-cases `mode == _IONBF`;
   every other value, valid or not, falls straight through to
   `f->bufmode = (unsigned char)mode;` and the function unconditionally
   returns `0`. Per
   [setvbuf.html](https://pubs.opengroup.org/onlinepubs/9699919799/functions/setvbuf.html)
   RETURN VALUE: "Otherwise, it shall return a non-zero value if an
   invalid value is given for `type` or if the request cannot be
   honored." Fenced in `test/posix-stdio.c`'s `test_setvbuf` with
   `setvbuf(f, 0, 12345, 0)`, which the implementation happily accepts
   and returns 0 for. Fix: reject anything other than `_IOFBF`,
   `_IOLBF`, `_IONBF` with a non-zero return (and, per common practice,
   `errno = EINVAL`, though POSIX does not require setting `errno`
   here).

## Extractions

None needed this session -- every requirement in scope was directly
observable through the public API (a raw `read(fileno(f), ...)` after
`fflush()` was enough to observe bug 1's fd-desync half without
reaching into `struct _IO_FILE`).

## Where to resume

`perror` and `popen`/`pclose` were not clause-audited this session
(both are thin, low-risk wrappers -- see the table). Otherwise this
group's scope is covered: every function in the task brief's list has
either pre-existing broad coverage in `test/stdio.c`, new clause-cited
coverage in `test/posix-stdio.c`, or an explicit N/A with reasoning
above. The two fenced bugs are real spec violations in
`src/stdio/buf.c` (`__fflush_locked`, `setvbuf`) and were left
unfixed per the task brief ("never edit an assertion to match the
implementation" -- the fix belongs to whoever owns triaging
`src/stdio/` bugs found across groups).
