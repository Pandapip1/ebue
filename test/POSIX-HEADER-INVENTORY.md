<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# POSIX.1-2017 header inventory

**Status: draft, produced by an audit agent -- not yet folded into
`test/POSIX-COVERAGE.md` (owned by a sibling effort).** This file only
records *which headers exist or are missing*; `test/POSIX-COVERAGE.md`
tracks conformance *within* a header, function by function, and is out
of scope here.

Source list: every header named on
`https://pubs.opengroup.org/onlinepubs/9699919799/idx/headers.html`
(POSIX.1-2017's own index), 82 headers, compared against `include/`
(`find include -name '*.h'`, 41 headers) as of this audit.

This split matters and is deliberately not flattened into one
"missing" bucket: a header that is missing because NT has no such
concept at all is a design fact worth recording once and moving on; a
header that is missing only because nobody has written it yet is a
todo, and conflating the two hides real gaps behind legitimate N/As.
The `pwd.h` bug that motivated this audit was exactly the second kind:
implementable, just absent, and nothing in-tree noticed because nothing
in-tree includes a header ntlibc does not have.

## Present (41)

`assert.h`, `ctype.h`, `dirent.h`, `errno.h`, `fcntl.h`, `fenv.h`,
`float.h`, `inttypes.h`, `iso646.h`, `libgen.h`, `limits.h`, `locale.h`,
`math.h`, `setjmp.h`, `signal.h`, `stdarg.h`, `stdbool.h`, `stddef.h`,
`stdint.h`, `stdio.h`, `stdlib.h`, `string.h`, `strings.h`,
`sys/resource.h`, `sys/select.h`, `sys/stat.h`, `sys/time.h`,
`sys/types.h`, `sys/wait.h`, `time.h`, `unistd.h`, `utime.h`, `wchar.h`.

Also present but **not** POSIX headers (extensions/generated, not part
of the 82 above, listed for completeness of `include/`): `alloca.h`,
`endian.h`, `features.h`, `getopt.h`, `stdalign.h`, `stdnoreturn.h`,
`sys/param.h`, `ntlibc/delayload.h`, `ntlibc/rpath.h`.

## In flight

- **`pwd.h`** -- being added by a sibling agent concurrently with this
  audit. Was the header whose absence broke the gnulib/Make bootstrap
  (`glob.c` and Make's `src/read.c` both `#include <pwd.h>`
  unconditionally on any non-`_WIN32` target). Not counted as missing
  below.

## Missing: genuine gap (implementable, just absent)

Ordered roughly by how directly the existing NTDLL-only machinery in
`src/internal/` gets you there.

| Header | Why it's a real gap |
|---|---|
| `grp.h` | Same shape as `pwd.h`: NT has no `/etc/group`, but a minimal synthetic single-entry database (as `pwd.h` is presumably doing) is exactly as implementable here as `pwd.h` was. Needed alongside `pwd.h` by the same class of source (anything that does `getpwuid()`+`getgrgid()` together). |
| `glob.h` | The other half of the bug report that started this audit: gnulib's `glob.c` needs both `pwd.h` and its own `glob.h`. Pure algorithm over `dirent.h`/`fnmatch.h`, both either present or gap-not-N/A; no OS blocker. |
| `fnmatch.h` | Pure pattern-matching algorithm, zero OS dependency, and `glob.h` above needs it. |
| `ftw.h` | `ftw`/`nftw` are a pure algorithm over `dirent.h`+`sys/stat.h`, both already present; no OS dependency beyond what this tree already has. |
| `wctype.h` | Wide-character classification, the direct analogue of the `ctype.h` this tree already has; `wchar.h` (present) is the only prerequisite. |
| `spawn.h` | `posix_spawn` is explicitly the public name for the internal `__spawn` (`src/internal/libc.h:177`, `src/process/spawn.c`) that `execve`/`fork` already build on. The machinery exists; only the public `spawn.h` declarations and the `posix_spawn`/`posix_spawn_file_actions_*` wrapper surface are missing. |
| `sys/mman.h` | NT's `NtCreateSection`/`NtMapViewOfSectionEx`/`NtUnmapViewOfSection` are pure-NTDLL and map directly onto `mmap`/`munmap`/`mprotect`; nothing here needs `kernel32`. Not started (`grep` for `NtCreateSection`/`MapViewOfSection` across `src/` and `include/` finds nothing). |
| `sys/utsname.h` | `RtlGetVersion`/the PEB's OS version fields are pure-NTDLL; `uname()` is a thin struct-fill on top. |
| `sys/statvfs.h` | `NtQueryVolumeInformationFile` is pure-NTDLL and covers everything `statvfs`/`fstatvfs` need. |
| `sys/times.h` | `NtQueryInformationProcess`/`NtQuerySystemTime` (already used by `src/time/clock.c` and friends) cover `times()` directly. |
| `semaphore.h` | `NtCreateSemaphore`/`NtReleaseSemaphore`/`NtWaitForSingleObject` are native NT objects with POSIX-semaphore-compatible semantics; pure NTDLL. |
| `poll.h` | `NtWaitForMultipleObjects` over the handles already tracked in `src/internal/fd.c`'s fd table covers the wait side; the harder part is deciding what "readable"/"writable" mean for each handle type ntlibc supports, which is real but bounded work. |
| `search.h` | `hsearch`/`tsearch`/`lsearch`/`lfind` families are pure algorithms (musl's implementations are ~self-contained), no OS dependency at all. |
| `regex.h` | Pure algorithm (POSIX ERE/BRE), substantial implementation effort but zero OS dependency -- musl's or OpenBSD's `regex.c` is a known-portable reference. |
| `ndbm.h` | Pure userspace flat-file database format, no OS dependency. |
| `nl_types.h` | `catopen`/`catgets`/`catclose` message catalogs; trivial to stub against a fixed "no catalogs installed" behavior, which POSIX explicitly permits. |
| `tgmath.h` | Purely `_Generic`/preprocessor dispatch on top of `math.h` (present); no OS dependency. Can be real-only (no `complex.h`) initially. |
| `tar.h` | Pure archive-format constants (magic numbers, mode bits), no OS dependency, essentially a copy-paste from the standard. |
| `cpio.h` | Same as `tar.h`: pure archive-format constants. |
| `fmtmsg.h` | `fmtmsg()` is a formatted `fprintf` to `stderr`/syslog-equivalent; no OS dependency beyond `stdio.h` (present). |
| `monetary.h` | `strfmon()` is locale-driven formatting on top of the `locale.c` this tree already has (`src/misc/locale.c`); low value, easy. |
| `iconv.h` | `src/internal/utf.c` already implements UTF conversions internally; `iconv_open`/`iconv`/`iconv_close` is mostly a stable-name wrapper around that plus a small codeset table. |
| `langinfo.h` | `nl_langinfo()` reads out of the same locale data `locale.h` (present) already has; thin wrapper. |
| `sched.h` | `NtSetInformationThread`/`NtQueryInformationThread` give priority get/set directly; the `sched_setscheduler`-style policy calls have no real NT equivalent and would need to be stubbed, same as several `pthread.h` calls below. |
| `sys/uio.h` | No true NT scatter/gather for arbitrary regular-file I/O without page alignment constraints (`NtReadFileScatter`/`NtWriteFileGather` are page-granular); `readv`/`writev` are still implementable as a loop over `NtReadFile`/`NtWriteFile`, just not natively vectored. |
| `termios.h` | NT's console model (classic console API, or the newer ConDrv/pseudoconsole device) does not map onto termios line-discipline settings the way a Unix tty driver does; `unistd/isatty.c` already establishes some console awareness, so this is bounded, real work (translating raw/cooked mode, echo, and the `c_cc[]` special characters onto console mode flags) rather than a blocked concept -- unlike `sys/socket.h` below, nothing here requires a non-NTDLL DLL. |
| `wordexp.h` | Needs a shell to do the expansion; `src/process/exec.c`'s existing spawn/exec machinery could shell out to `cmd.exe` (or a bundled `sh`), but the semantics (`$IFS`-free tokenizing per POSIX) are real work layered on top. |
| `ulimit.h` | Formally obsolescent in POSIX (superseded by `getrlimit`/`setrlimit`, both already present via `sys/resource.h`); implementable as a one-line wrapper if source compatibility with old code is ever needed, but not a priority. |
| `syslog.h` | No NTDLL-only equivalent (Event Log is a `kernel32`/`advapi32` API, `ReportEventW`); could stub to writing formatted lines to `stderr` under `--enable-kernel32`-style opt-in, or unconditionally as a degraded local-only implementation. |
| `pthread.h` | NT threads are pure-NTDLL (`NtCreateThreadEx` even undocumented aside, `RtlCreateUserThread` is the documented one) and `libpthread.a` already exists as an intentionally *empty* stub archive (`Makefile`'s `EMPTY_LIB_NAMES`), i.e. the project has reserved the slot but not built it. Real, substantial gap, not a design N/A. |
| `sys/ipc.h`, `sys/msg.h`, `sys/sem.h`, `sys/shm.h` | System V IPC has no native NT concept, but is emulatable (named NT sections for shm, named objects keyed off `ftok`-style paths for the rest) the same way every other platform without native SysV IPC fakes it. Real gap, but low value relative to effort -- most modern code prefers POSIX IPC (`semaphore.h`, `mqueue.h`, `mmap`-based shm) over this legacy interface. |
| `mqueue.h` | No native NT message-queue primitive as clean as the semaphore/section mappings above; would need to be built on named pipes or NT sections plus manual synchronization. Real gap, substantial effort, not attempted. |
| `netdb.h`, `net/if.h`, `netinet/in.h`, `netinet/tcp.h`, `sys/un.h`, `arpa/inet.h` | All gated on `sys/socket.h` (below) existing first -- NT sockets go through `ws2_32.dll` (Winsock), which is a Win32 DLL, not an NTDLL/native-NT primitive; the raw `AFD.sys` device-IoControl protocol Winsock itself is built on is undocumented and a major undertaking. Real gap in the sense that networking is obviously possible on NT, but blocked on a project-level decision to either depend on `ws2_32.dll` (breaking the "pure NTDLL, `kernel32` only opt-in" design) or reverse-engineer `AFD.sys`. |
| `sys/socket.h` | Same blocker as immediately above: this is the header the whole networking family depends on. Listed separately because it's the one decision point, not a per-header gap. |

## Missing: genuinely N/A on NT (no such concept, or POSIX itself marks it obsolescent/optional and no implementation anywhere targets it)

| Header | Reason |
|---|---|
| `stropts.h` | STREAMS I/O. POSIX itself marks the whole STREAMS option obsolescent/optional (removed from later issues' base); no OS in this project's target set (NT) has a STREAMS subsystem, and no other libc worth comparing against (musl, for one) implements it either. |
| `trace.h` | POSIX Tracing option (`_POSIX_TRACE`), optional even for POSIX-conformant systems; essentially unimplemented across the whole ecosystem (glibc doesn't have it either). No NT tracing primitive maps onto it in any direct way (ETW is a wildly different model). |
| `utmpx.h` | Login-record accounting (`/var/run/utmp`-style). NT has no file-based login-session-accounting concept; session/logon tracking on NT goes through the LSA and Winlogon, nothing an NTDLL-only libc can or should shadow with a fake flat file. |
| `aio.h` | POSIX asynchronous I/O has a real, better-fitting equivalent already: NT I/O is asynchronous-by-default (`IO_STATUS_BLOCK`, APCs, completion ports) via the exact `NtReadFile`/`NtWriteFile` primitives this library's synchronous calls already wrap. Implementing the POSIX `aio_*` shape on top would be translating one async model into a narrower one just to match a header name, not filling a real capability gap -- the underlying facility is present, the POSIX-shaped header is not, and is deliberately not planned. Listed as N/A rather than "gap" because the *concept* `aio.h` names (POSIX AIO control blocks + `aio_error`/`aio_return`) has no NT counterpart to wrap, unlike, say, `semaphore.h` above. |
| `complex.h` | No complex-number support anywhere in `src/math/`; this is a "not attempted, and arguably out of scope for a libc whose math library is the minimal freestanding set" call rather than an NT-specific N/A, but it is listed here (not as a "gap") because nothing about NT blocks or unblocks it -- it is purely a scope decision independent of the platform, unlike every header in the gap table above. |
| `dlfcn.h` | Listed here with a caveat, not a clean N/A: `ntlibc/rpath.h`'s `ntlibc_rpath_load`/`ntlibc_rpath_sym` already implement exactly `dlopen`/`dlsym`'s job on top of pure NTDLL. The POSIX *names* (`dlopen`, `dlsym`, `dlclose`, `dlerror`) are absent because the project chose its own non-POSIX API surface for this rather than shadowing the POSIX one -- a naming/API-surface decision already made, not an unstarted gap. Worth revisiting as a thin `dlfcn.h` wrapper over the existing `rpath.h` functions if POSIX source compatibility is ever wanted here. |

## Not audited here (out of scope)

`pwd.h` -- see "In flight" above.

## How this was produced

`find include -name '*.h' | sort` against the POSIX.1-2017 header index
page. Each "missing" header was checked against `grep -rl` over
`src/`/`include/` for any existing internal machinery it could sit on
top of, and against whether the underlying OS primitive is NTDLL-only
or requires `kernel32`/`ws2_32`/other non-NTDLL DLLs, per this
project's stated preference (`configure --enable-kernel32` being
opt-in) for staying NTDLL-only where possible.
