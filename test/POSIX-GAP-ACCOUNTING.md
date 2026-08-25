<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# POSIX.1-2017 gap accounting

Companion to `test/POSIX-COVERAGE.md`. That file is a *conformance*
ledger: for the functions ntlibc **has**, how far the clause-by-clause
audit against
`https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html`
has gotten. It says nothing about what POSIX specifies that ntlibc does
not have at all, which is the other half of "where are we".

`test/POSIX-COVERAGE.md` finished its priority list at `5a17605`/
`04edec2`: 285 **covered**, 68 **N/A**, and no remaining **not yet
reached** rows. Its column is therefore complete for what it covers, and
this file cross-references it rather than duplicating it.

This file is that other half. It enumerates **every function interface
in the POSIX.1-2017 System Interfaces volume** — all 1177 of them, taken
mechanically from POSIX's own index at
`https://pubs.opengroup.org/onlinepubs/9699919799/idx/functions.html` —
and puts each one in exactly one bucket. It deliberately does *not*
restate the coverage ledger's per-clause findings; where a function is
audited, this file points at the ledger's section and stops.

`test/POSIX-HEADER-INVENTORY.md` covers the same ground one level up, at
header granularity. **That file is now materially stale** and this one
supersedes it for anything function-level: it lists `ftw.h`, `grp.h`,
`pwd.h`, `search.h`, `regex.h`, `poll.h`, `dlfcn.h`, `termios.h`,
`sys/uio.h`, `sys/times.h`, `sys/utsname.h`, `wctype.h`, `sys/socket.h`
and the `netinet`/`arpa` pair as missing or blocked; every one of them
exists in `include/` and has a body in `src/` as of `04edec2`. Its
*reasoning* about what each would take is still worth reading; its
present/missing split is not.

## Status vocabulary

Reused verbatim from `test/POSIX-COVERAGE.md` where it applies, with two
statuses this file needs and that one does not:

- **implemented + clause-audited** — a body exists in `src/` (or the
  interface is a macro in `include/`), `tools/linkcheck.sh` links a real
  call to it, *and* `test/POSIX-COVERAGE.md` carries a row for it. The
  ledger, not this file, is authoritative for what "audited" bought.
- **implemented, not clause-audited** — exists and links, but no
  clause-by-clause audit yet. This is the ledger's **not yet reached**
  applied to a function the ledger never even listed.
- **declared but deliberately unimplemented** — declared in `include/`
  with a `tools/lint-undefined.sh` `undefined-ok:` marker, which
  `tools/linkcheck.sh` reuses as its own exception list. Reason quoted
  from the header.
- **implemented but non-functional** — a body exists and links, but the
  call cannot do its job on the target platform: either it is a
  permanent degenerate stub, or (the `sys/socket.h` case) the subsystem
  is real code that no available platform has been shown to run. Split
  out of "implemented" on purpose; see that section for why.
- **absent** — not declared anywhere in `include/`, no body in `src/`.

**XSI vs base** is tracked throughout and is not cosmetic: an absent XSI
interface is a missing *option group*, which a POSIX-conformant
implementation is permitted not to have; an absent base interface is a
conformance hole. The ledger already makes this call for `isascii`/
`toascii` (`OB XSI`), and this file extends it to every entry. The
option marker recorded here is POSIX's own margin code, scraped from
each function page's SYNOPSIS box (`XSI`, `CX`, `OB` obsolescent, `SPN`
spawn, `TRC`/`TRL`/`TEF`/`TRI` tracing, `MSG` message passing, `PS`
process scheduling, `SHM`/`TYM`/`ML`/`MLR`/`ADV` memory, `TSH`/`TPS`/
`TSA`/`TSS` threads, `XSR` STREAMS, `RPP` robust mutexes, `FSC`
synchronised I/O, `SIO`, `CPT`, `MC1`, `TCT`). An empty marker means
POSIX base, mandatory for conformance.

## Headline counts (as of `04edec2`, x86_64)

| bucket | count | of which XSI | of which base (no option marker) |
|---|---|---|---|
| implemented + clause-audited | 333 | 36 | 236 |
| implemented, not clause-audited | 357 | 45 | 293 |
| declared but deliberately unimplemented (`undefined-ok:`) | 14 | 11 | 0 |
| absent | 473 | 39 | 256 |
| **total POSIX.1-2017 function interfaces** | **1177** | | |

Read that as: **690 of 1177 (59%) exist and link**, of which **333 (48%
of what exists) have been clause-audited**. Of the 473 absent, only 256
are POSIX base — and 216 of those 256 sit in three families
(`pthread.h`, `complex.h`, wide-character stdio/`wchar.h`/`wctype.h`),
so the base-conformance hole is far more concentrated than the raw
number suggests.

Every count in this file, and every table below it, is a **dated
snapshot of `04edec2`**, derived mechanically by the pipeline in "How
this was produced" and deliberately not hand-edited afterwards: the
whole value of the arithmetic is that no human retyped any of it.
Functions that have since moved between buckets are therefore recorded
in "Changes since `04edec2`" immediately below, not by editing the
frozen numbers. Re-run the pipeline to fold them in.

## Changes since `04edec2`

Each row is a function interface that has moved out of **absent** since
the snapshot, with the commit that moved it. Subtract these from the
absent counts and add them to *implemented + clause-audited* when
reading the tables below.

| interface(s) | header | option | moved to | test |
|---|---|---|---|---|
| `fmaxf` `fmaxl` `fminf` `fminl` | `math.h` | base | implemented + clause-audited | `test/posix-math.c` (`test_fmaxmin_variants`) |
| `exp2` `exp2f` `exp2l` | `math.h` | base | implemented + clause-audited | `test/posix-math.c` (`test_exp2`) |
| `sched_yield` | `sched.h` (new) | base | implemented + clause-audited | `test/posix-sysmisc.c` (`test_sched_yield`) |
| `statvfs` `fstatvfs` | `sys/statvfs.h` (new) | base | implemented + clause-audited | `test/posix-sysmisc.c` (`test_statvfs`, `test_statvfs_errors`) |
| `waitid` | `sys/wait.h` | base | implemented + clause-audited | `test/posix-sysmisc.c` (`test_waitid_*`) |

Running adjustment: **11 base interfaces closed**; absent 473 -> 462
(base 256 -> 245), implemented + clause-audited 333 -> 344.  `math.h`'s
7 base absences, `sched.h`'s 1, `sys/statvfs.h`'s 2 and `sys/wait.h`'s 1
are now 0.  The other 7 `sched.h` entries stay absent and stay
`PS`-optional -- `include/sched.h`'s banner records why declaring them
would be worse than not having them.

That closes every row of "Small individually-actionable gaps" below
except `sigignore` (`OB XSI`), and closes the last base absence in four
headers.

The 1177 excludes the 14 entries in POSIX's function index that are
external *variables*, not functions (`environ`, `errno`, `optarg`,
`opterr`, `optind`, `optopt`, `stdin`, `stdout`, `stderr`, `daylight`,
`timezone`, `tzname`, `signgam`, `getdate_err`). They have index pages
but no declarator; they are not classified here.

## How this was produced

Reproducible end to end, no hand-listing anywhere in the classification:

1. **The POSIX side.** Fetch
   `https://pubs.opengroup.org/onlinepubs/9699919799/idx/functions.html`
   and take every `../functions/<name>.html` link — 1191 entries, one
   per named interface. Fetch all 1191 pages. Parse each page's
   `<blockquote class="synopsis">` box, tracking the `[XXX]` margin
   markers against the `opt-start.gif`/`opt-end.gif` region delimiters,
   and record (a) the option markers in force on the line that declares
   that page's own function and (b) the `#include <...>` in force. That
   gives, per interface, its owning header and its exact option-group
   membership straight from the standard rather than from recollection.
2. **The ntlibc side.**
   - *links*: `./configure --host=x86_64-win32 CC=x86_64-win32-tcc && make && make linkcheck`. `tools/linkcheck.sh` compiles and links one
     real call per declared symbol; the TUs it leaves in `obj/linkcheck/`
     are exactly the set of "declared and linkable" names (766 checked,
     51 excepted, 0 unlinkable, out of 817 declared).
   - *defined*: `nm lib/libc.a`, filtered to `T`/`D`/`B`/`R`.
   - *`undefined-ok:`*: parsed out of `make linkcheck`'s own "excepted"
     report, so it can never drift from the markers `lint-undefined.sh`
     enforces.
   - *macros*: the interfaces POSIX specifies that may be macros
     (`assert`, `FD_*`, `va_*`, the `math.h` classification macros,
     `_tolower`/`_toupper`) have no `RET NAME(...);` line for
     `linkcheck.sh` to see — by that script's own documented design —
     and were confirmed by hand as `#define`s in `include/`.
3. **The audit side.** Every identifier in the first column of every
   table row in `test/POSIX-COVERAGE.md`, intersected with the POSIX
   name list: 358 names, 333 of which are also implemented. (The other
   25 are names the ledger mentions in order to mark them N/A — LEGACY
   removals, GNU/BSD extensions — which are not POSIX.1-2017 interfaces
   and so are not in this file's 1177 either.)

Three cross-checks were run and are worth recording, because two of them
found something:

- `grep`ing `include/` for each nominally-absent name turned up nine
  false hits, all of them **mentions in comments, not declarations**:
  `mlock` (in `include/sys/resource.h`'s explanation of why NT has no
  mlock budget) and `getpeername`, `getsockname`, `recvfrom`, `recvmsg`,
  `sendmsg`, `sendto`, `sockatmark`, `socketpair` (in
  `include/sys/socket.h`'s own scope note saying they are deliberately
  not declared). All nine are classified **absent**, correctly.
- Six names carry an `undefined-ok:` marker but *are* defined in
  `lib/libc.a`: `dlopen`/`dlsym`/`dlclose`/`dlerror` (their marker says
  so explicitly — they resolve `__rpath`, which the calling program
  supplies, so no standalone TU can link them; that is a limitation of
  `linkcheck.sh`'s generated call site, not of the functions) and
  `hsearch`/`inet_ntoa` (both take a struct by value, which
  `linkcheck.sh`'s literal-`0` argument generator cannot express; both
  markers say exactly this). All six are classified **implemented**, not
  "deliberately unimplemented" — classifying them off the marker alone
  would have been wrong six times.
- Every implemented name was checked for a mention anywhere in
  `test/*.c`. 134 had none at all when this audit began; the successor
  session closed all of them but `pause()`,
  and they are listed below.

## Implemented + clause-audited (333)

Not restated here — `test/POSIX-COVERAGE.md` is authoritative. Pointer
table only, by owning header, so this file's count reconciles:

| header | audited | ledger section |
|---|---|---|
| `stdlib.h` | 60 | "stdlib.h" (priority 2) + "Memory allocation, process termination, and environment" (priority 11) |
| `stdio.h` | 46 | "stdio.h streams" (priority 5) |
| `unistd.h` | 34 | "unistd.h, fcntl.h, sys/stat.h" (priority 6) |
| `string.h` | 32 | "string.h / strings.h" (priority 1) |
| `math.h` | 32 | "math.h" (priority 9) |
| `time.h` | 23 | "time.h calendar and clock functions" (priority 3) |
| `wchar.h` | 20 | "wchar.h / multibyte conversions" (priority 8) |
| `signal.h` | 17 | "signal.h, sys/wait.h" (priority 7) |
| `sys/stat.h` | 11 | "unistd.h, fcntl.h, sys/stat.h" (priority 6) |
| `dirent.h` | 11 | "dirent.h, ctype.h, locale.h, libgen.h, setjmp.h, getopt()" (priority 4) |
| `sys/select.h` | 6 | the closing pass (`5a17605`) — `select`/`pselect`/`FD_*`; this file's earlier draft, and the ledger's own pre-`5a17605` text, both said `select()` was declared but unimplemented, which stopped being true at `bd9982c` |
| `ctype.h` | 6 | priority 4, plus the XSI `isascii`/`toascii` pair closed in `5a17605` |
| `sys/resource.h` | 5 | the closing pass (`5a17605`) |
| `strings.h` | 5 | "string.h / strings.h" (priority 1) |
| `setjmp.h` | 4 | priority 4 |
| `inttypes.h` | 4 | "limits.h / float.h / stdint.h / inttypes.h" (priority 10) |
| `fcntl.h` | 4 | priority 6 |
| `wctype.h` | 2 | priority 8 |
| `sys/wait.h` | 2 | priority 7 |
| `stddef.h` | 2 | scattered |
| `locale.h` | 2 | priority 4 |
| `libgen.h` | 2 | priority 4 |
| `utime.h` | 1 | the closing pass (`5a17605`) |
| `poll.h` | 1 | the closing pass (`5a17605`) |
| `assert.h` | 1 | the closing pass (`5a17605`) |

## Implemented, not clause-audited (357)

Exists in `src/`, links, no ledger row. The ledger has now closed every
row it opened (0 **not yet reached** as of `04edec2`) — but its priority
order never enumerated these headers at all, so "no remaining not-yet-
reached rows" and "audited" are not the same statement. These 357 are
the difference.

Ordered by how much a clause audit would plausibly find, which is not
the same as size:

| header | count | functions |
|---|---|---|
| `unistd.h` | 43 | `alarm chown confstr execl execle execlp execv execve execvp fchown fchownat fexecve fork getegid geteuid getgid getgroups gethostname getlogin getlogin_r getpgid getpgrp getsid getuid lchown linkat nice pause readlinkat setegid seteuid setgid setpgid setpgrp setregid setreuid setsid setuid swab symlinkat sync tcgetpgrp tcsetpgrp` |
| `termios.h` | 11 | `cfgetispeed cfgetospeed cfsetispeed cfsetospeed tcdrain tcflow tcflush tcgetattr tcgetsid tcsendbreak tcsetattr` — `src/termios/termios.c` has a long banner arguing which of these are spec-permitted no-ops on a console; `test/posix-termios.c` exists but the ledger has no section for it |
| `search.h` | 11 | `hcreate hdestroy hsearch insque lfind lsearch remque tdelete tfind tsearch twalk` (all XSI) |
| `fenv.h` | 11 | `feclearexcept fegetenv fegetexceptflag fegetround feholdexcept feraiseexcept fesetenv fesetexceptflag fesetround fetestexcept feupdateenv` |
| `sys/socket.h` | 10 | `accept bind connect getsockopt listen recv send setsockopt shutdown socket` — **read "implemented but non-functional" below before trusting this row**; this subsystem is actively moving |
| `arpa/inet.h` | 8 | `htonl htons inet_addr inet_ntoa inet_ntop inet_pton ntohl ntohs` — unlike the row above these need no OS support and `test/posix-socket.c` does exercise them unconditionally |
| `pwd.h` | 7 | `endpwent getpwent getpwnam getpwnam_r getpwuid getpwuid_r setpwent` |
| `grp.h` | 7 | `endgrent getgrent getgrgid getgrgid_r getgrnam getgrnam_r setgrent` |
| `sys/stat.h` | 6 | `fchmodat mkdirat mkfifo mkfifoat mknod mknodat` — the last four are permanent stubs, see below |
| `signal.h` | 5 | `sighold siginterrupt sigpause sigrelse sigset` (all XSI, all degenerate stubs — see below) |
| `regex.h` | 4 | `regcomp regerror regexec regfree` |
| `locale.h` | 4 | `duplocale freelocale newlocale uselocale` |
| `dlfcn.h` | 4 | `dlclose dlerror dlopen dlsym` (`test/posix-dl.c` exists) |
| `stdio.h` | 16 | `ctermid dprintf flockfile fprintf fscanf ftrylockfile funlockfile getc_unlocked getchar_unlocked gets putc_unlocked putchar_unlocked snprintf sprintf sscanf tempnam` |
| `stdarg.h` | 12 | `va_arg va_copy va_end va_start vdprintf vfprintf vfscanf vprintf vscanf vsnprintf vsprintf vsscanf` — eight of the twelve gained their first assertion in this session's `test/posix-stdio.c` additions |
| `ctype.h` | 12 | `isalnum isalpha isblank iscntrl isdigit isgraph islower isprint ispunct isspace isupper isxdigit` — the ledger audited the `is*` family as a group and cites `isascii`/`toascii`/`tolower`/`toupper`/`_tolower`/`_toupper` by name; these twelve are the individual pages it does not |
| `wctype.h` | 16 | `iswalnum iswalpha iswblank iswcntrl iswctype iswdigit iswgraph iswlower iswprint iswpunct iswspace iswupper iswxdigit towctrans towlower towupper` |
| `sys/uio.h` | 2 | `readv writev` (XSI) |
| `ftw.h` | 2 | `ftw nftw` |
| `glob.h` | 2 | `glob globfree` (`test/posix-glob.c` exists, no ledger section) |
| `wordexp.h` | 2 | `wordexp wordfree` |
| `fcntl.h` | 2 | `posix_fadvise posix_fallocate` |
| `setjmp.h` | 2 | `_setjmp _longjmp` (`OB XSI`) |
| `string.h` | 2 | `strlen strnlen` |
| `sys/times.h` | 1 | `times` (XSI) |
| `sys/utsname.h` | 1 | `uname` |
| `sys/time.h` | 1 | `gettimeofday` (`OB`) |
| `fnmatch.h` | 1 | `fnmatch` |
| `stdlib.h` | 1 | `srand48` (XSI) |
| `stropts.h` | 1 | `ioctl` (`OB XSR`) — `src/ioctl/ioctl.c` implements the name, not the STREAMS semantics POSIX attaches to it |
| `math.h` | 150 | the `f`/`l` suffixed variants and the long tail; the ledger's priority-9 section audited 32 double-precision entry points |

### Implemented, but no assertion anywhere in `test/*.c` (0)

Every implemented name was grepped against the concatenation of all
`test/*.c`. 134 were never named when this audit started; 22 gained
their first assertion in this session (see "Tests added" below), and
**the successor session closed the remaining 112** — see "Successor
session" at the end of this file, which also records how the list was
re-derived and verified still accurate before any of it was worked. The
per-header lines below are kept as the record of what the list *was*:

- `math.h` (0): all 70 (the `f`/`l` variants plus the six comparison macros) closed, see "Successor session" below
- `unistd.h` (0): all 23 closed, see "Successor session" below (`pause` is the one that could not be *called* — see there for why)
- `sys/stat.h` (0): all six (`mkdirat mkfifo mkfifoat mknod mknodat utimes`) closed, see "Successor session" below
- `signal.h` (0): all five (`sighold siginterrupt sigpause sigrelse sigset`) closed, see "Successor session" below
- `stdarg.h` (0): all four (`va_arg va_copy vprintf vscanf`) closed, see "Successor session" below
- `setjmp.h` (0): both (`_setjmp _longjmp`) closed, see "Successor session" below
- `stdio.h` (0): both (`getc_unlocked tempnam`) closed, see "Successor session" below

#### One bug, found by closing part of that list

Adding assertions for the never-asserted `<stdio.h>` names (see "Tests
added" below) turned up a real defect immediately: **`vdprintf()` — and
therefore `dprintf()` — leaked its stream buffer on every call.**

`src/stdio/printf.c`'s `vdprintf()` wraps the raw fd in a stack `FILE`
with `bufmode = _IONBF`; `__ensure_buf()` then `malloc()`s that FILE's
buffer (one byte, for `_IONBF`) on the first write, and nothing ever
freed it — the FILE never reaches `fclose()`, and `fflush()` drains a
buffer without releasing it. One byte per call, unbounded.

Nothing in the tree had ever called `dprintf()` or `vdprintf()`, which
is exactly why it survived: `tools/asan-build.sh` runs every test under
LeakSanitizer and would have failed on the first call. Fixed in the same
commit as the test (`if (f.buf && !f.user_buf) free(f.buf);`, guarded
the same way `__fclose_locked()` and `setvbuf()` guard buffer
ownership), not fenced as a `BUG`, because it is a leak rather than a
spec-violating observable behaviour and the fix is two lines.

The general lesson for whoever works the rest of the 112: "exists and
links" is a much weaker statement than it looks, and the cheapest
assertion is often enough to find out.

Twelve of the original 134 *do* appear in a `test/POSIX-COVERAGE.md`
table row — always as the second name in a slash-joined pair whose
assertion covers only the first (`kill / killpg`, `read / readlink`, and
so on). That is not a false claim by the ledger, but it does mean the
row's "covered" applies to a name that is never called. Seven were
closed this session (`killpg`, `sigaltstack`, `fseeko`, `ftello`,
`getchar`, `putc`, `putchar`, `strcoll_l`); the remaining four —
`utimes`, `fpathconf`, `readlink`, `unlinkat` — **were closed by the
successor session**, see below.

## Declared but deliberately unimplemented (14)

Every name carrying an `undefined-ok:` marker that is *also* genuinely
undefined. Reasons are the headers' own, as reported by
`make linkcheck`; the full text is in the declaring header.

| function | header | option | stated reason |
|---|---|---|---|
| `crypt` | `unistd.h` | XSI | DES password hashing |
| `encrypt` | `unistd.h` | XSI | same DES machinery as `crypt()` |
| `setkey` | `stdlib.h` | XSI | DES-based, like `crypt()`/`encrypt()` |
| `gethostid` | `unistd.h` | XSI | BSD host-id concept, no NT analogue |
| `lockf` | `unistd.h` | XSI | `F_SETLK`/`F_SETLKW` are themselves the underlying primitive |
| `posix_openpt` | `stdlib.h` | XSI | Unix98 pseudo-terminal allocation |
| `grantpt` | `stdlib.h` | XSI | see `posix_openpt` |
| `ptsname` | `stdlib.h` | XSI | see `posix_openpt` |
| `unlockpt` | `stdlib.h` | XSI | see `posix_openpt` |
| `getitimer` | `sys/time.h` | `OB XSI` | `ITIMER_REAL` needs the same machinery `alarm()` lacks |
| `setitimer` | `sys/time.h` | `OB XSI` | see `getitimer` |
| `sigwaitinfo` | `signal.h` | CX | `src/signal/signal.c`'s `sigwait()` is already a permanent stub |
| `sigtimedwait` | `signal.h` | CX | see `sigwaitinfo` |
| `sigqueue` | `signal.h` | CX | needs the same machinery |

Eleven of the fourteen are XSI (two of those also `OB`), the other
three are CX; none are POSIX base. The `undefined-ok:`
mechanism is, on this evidence, being used exactly as intended.

The other 37 `undefined-ok:` markers in the tree are on non-POSIX names
(`brk`/`sbrk`, `daemon`, `eaccess`, `euidaccess`, `getdomainname`,
`getentropy`, `getpass`, `getresuid`/`setresuid` and friends,
`getusershell` and friends, `syncfs`, `syscall`, `ualarm`, `vhangup`,
`acct`, `adjtime`, `sethostname`) or are the six generator-limitation
exceptions discussed above. They are outside this file's scope.

## Implemented but non-functional

Classified by *actual working state*, not by file presence — the point
the `sys/socket.h` case forces.

### `sys/socket.h` / `netinet/in.h` / `arpa/inet.h`: unverified, and moving

**This section is a snapshot of a subsystem under active repair; check
`git log -- src/socket/` before trusting it.**

`src/socket/` (10 `.c` files) compiles, links, and implements
`socket`/`bind`/`listen`/`connect`/`accept`/`send`/`recv`/`shutdown`/
`getsockopt`/`setsockopt` for `AF_INET`+`SOCK_STREAM` over the raw
`\Device\Afd` ioctl protocol. State as of `04edec2`:

- **On real Windows** (CI's `windows-test` legs, the only authority for
  real-NT behaviour): `socket()` **now works**, as of `1f2e40e` — the
  `AfdOpenPacketXX` extended-attribute buffer had carried ReactOS's
  12-byte `AFD_CREATE_PACKET` header where real Windows' `afd.sys` has
  wanted a 24-byte `AFD_OPEN_PACKET` since Vista, so the driver read the
  UTF-16 transport name text as a length (`0x00630069`) and walked ~6 MB
  past a 67-byte buffer, hence the `EFAULT`. `bind()` still fails, so
  the subsystem as a whole is still not working end to end there.
- **Under Wine**: `NtCreateFile` on `\Device\Afd\Endpoint` succeeds,
  but the first real ioctl (`IOCTL_AFD_BIND`) fails
  `STATUS_BAD_DEVICE_TYPE` (`0xC00000CB`) — Wine only wires up handles
  opened through its own invented `IOCTL_AFD_WINE_CREATE`, so a handle
  opened the portable way is never routed to its AFD implementation.
  Confirmed against both stock Wine and this project's locally patched
  build; neither carries an AFD patch. This is a Wine limitation, and
  following ReactOS/real-Windows rather than Wine's private control code
  is deliberate.

So the state is: `bind()` is the current frontier on real Windows, and
nothing past `socket()` is verifiable under Wine at all.

`test/posix-socket.c` handles this correctly and is the model for the
category: byte-order and address-text conversion (`src/socket/inet.c`)
and `socket()`'s own argument validation are exercised unconditionally,
everything downstream of a working AFD endpoint is gated behind a
runtime capability probe, and a failed probe exits **77** — "ran, but
verified nothing new", which `tools/runtests.sh` reports in its own
bucket, separate from PASS and FAIL. The gate's baseline of "41 passed
+ 1 unverified per arch" is that 77.

`test/posix-socket-ea.c` (added at `1f2e40e`) is the second half of the
answer and worth copying wherever this shape recurs: it opens nothing.
It asks `__afd_open_ea_size()`/`__afd_build_open_ea()` for the bytes and
re-parses them **by offset**, against constants taken from ReactOS and
phnt rather than from ntlibc's own headers, so a layout bug is caught
with no device involved — on a host with no `\Device\Afd`, under Wine,
under `make asan`, and on real Windows alike. That is how the 24-byte
packet bug became testable at all: `socket()` reported only `EFAULT` on
real Windows and *succeeded* under Wine, so nothing else in the suite
could have seen it.

Accounting consequence: the ten `sys/socket.h` and eight `arpa/inet.h`
names are counted as implemented above, because they are; but only the
eight `arpa/inet.h` ones, `socket()`'s validation path, `socket()`
itself on real Windows, and the EA-layout invariants are counted as
*working*. The remaining eleven `sys/socket.h` interfaces POSIX
specifies (`getpeername`, `getsockname`, `recvfrom`, `recvmsg`,
`sendmsg`, `sendto`, `sockatmark`, `socketpair`, plus `netdb.h`'s
`getaddrinfo`/`freeaddrinfo`/`getnameinfo`) are **absent** and
deliberately so — see `include/sys/socket.h`'s scope note and
`test/networking-audit.md` sec 6, stages 4-6.

### Permanent degenerate stubs

Found by scanning `src/` for bodies that discard every argument. These
link, never fail to link, and will never do the thing POSIX describes.
Whether that is a bug or a correct platform answer differs per line, and
this file does not adjudicate — it records that the body is degenerate
so that a "covered" row against one of them is read with the right
expectation.

| function(s) | file | behaviour | reading |
|---|---|---|---|
| `mkfifo`, `mkfifoat` | `src/stat/chmod.c:76` | `errno = ENOSYS; return -1` | genuine gap: NT named pipes exist and are pure NTDLL (`NtCreateNamedPipeFile`); nobody has mapped FIFO semantics onto them |
| `mknod`, `mknodat` | `src/stat/chmod.c:78` | `errno = EPERM; return -1` | POSIX-legal for a non-privileged caller; effectively N/A on NT |
| `alarm` | `src/unistd/sleep.c:41` | `return 0` | genuine gap, and the root of the `getitimer`/`setitimer`/`ualarm` `undefined-ok:` chain: needs a per-process timer thread delivering `SIGALRM` |
| `sigwait` | `src/signal/signal.c:304` | `errno = EINVAL; return EINVAL` | genuine gap; the reason `sigwaitinfo`/`sigtimedwait`/`sigqueue` are `undefined-ok:` |
| `sigsuspend`, `sigpause` | `src/signal/signal.c:303,312` | `errno = EINTR; return -1` | genuine gap, same family |
| `siginterrupt` | `src/signal/signal.c:305` | `return 0` | ntlibc has no restartable syscalls to interrupt; arguably correct |
| `sigaltstack` | `src/signal/signal.c:306` | reports `SS_DISABLE`, ignores `ss` | XSI; honest "no alternate stack" answer |
| `setuid`, `seteuid`, `setgid`, `setegid`, `setreuid`, `setregid` | `src/unistd/ids.c:12-17` | `return 0` | single-identity model; `src/misc/pwd.c` and `src/misc/grp.c` are built on this being true |
| `chown`, `fchown`, `lchown`, `fchownat` | `src/unistd/ids.c:26-29` | `return 0` | same |
| `getpgid`, `getsid` | `src/unistd/ids.c:21,25` | `return 1` | NT has no process groups or sessions |
| `setpgid`, `nice` | `src/unistd/ids.c:22,30` | `return 0` | same |
| `chroot` | `src/unistd/ids.c:31` | `errno = EPERM; return -1` | not POSIX.1-2017 anyway |
| `tcgetpgrp`, `tcsetpgrp` | `src/unistd/ttyname.c:23` | `return 1` / `return 0` | no controlling-terminal process groups on NT |
| `flockfile`, `ftrylockfile`, `funlockfile` | `src/stdio/file.c:182` | no-ops | correct while ntlibc is single-threaded; becomes a bug the moment `pthread.h` lands |
| `nan`, `nanf`, `nanl` | `src/math/nan.c:5` | ignore the tag string | POSIX permits ignoring an unrecognised n-char sequence |
| `fpathconf` | `src/unistd/sysconf.c:61` | forwards to `pathconf("")` | ignores the fd entirely |
| several `termios.h` calls | `src/termios/termios.c` | spec-permitted no-ops | the file's own banner argues each one; not re-litigated here |

## Absent (473), by subsystem

The `note` column answers "why absent, and what would it take". Clusters
are ordered by how much of the base-conformance hole they close.

Cluster sizes reconcile to the total: 102 + 66 + 84 + 50 + 34 + 14 + 21
+ 32 + 13 + 8 + 11 + 27 + 11 = 473. A cluster is named for the header
that owns most of it; where POSIX declares a function in a *different*
header from its family's (`pthread_kill` in `signal.h`, the four
`posix_spawnattr_*sig*` in `signal.h`, `getaddrinfo` in both
`sys/socket.h` and `netdb.h`) it is counted with its family, not its
header, and the discrepancy is called out in that cluster's note.

### Threads: `pthread.h` (102, all base except as marked)

100 in `pthread.h` itself plus `pthread_kill` and `pthread_sigmask`,
which POSIX declares in `signal.h` (both CX).

| what | note |
|---|---|
| whole header | Nothing exists. `libpthread.a` is built as an intentionally *empty* archive (`Makefile`'s `EMPTY_LIB_NAMES`), so the slot is reserved and unfilled. NT threads are pure NTDLL (`RtlCreateUserThread`), as are the synchronisation primitives (`NtCreateEvent`, `RtlSRWLock*`, `RtlConditionVariable*`, `RtlInitializeCriticalSection`), and TLS is a PEB/TEB slot `src/internal/*/teb.c` already reaches. This is the single largest base-POSIX gap in the tree and the one with the fewest platform blockers. |
| `pthread_create` `pthread_join` `pthread_detach` `pthread_equal` `pthread_exit` `pthread_self` `pthread_once` `pthread_atfork` | core lifecycle; `RtlCreateUserThread` + a TEB-slot thread struct |
| `pthread_attr_*` (18) | `pthread_attr_getstack`/`setstacksize`/`getguardsize` map onto `RtlCreateUserThread`'s stack parameters; the `inheritsched`/`schedpolicy`/`scope` half has no NT analogue and would be `ENOTSUP` stubs, same shape as `sched.h` below |
| `pthread_mutex_*` (10) + `pthread_mutexattr_*` (11) | `RtlInitializeCriticalSection`/`RtlEnterCriticalSection`; `prioceiling`/`protocol` (`TPP`/`TPI`) and `robust` (`RPP`) have no NT equivalent |
| `pthread_cond_*` (6) + `pthread_condattr_*` (5) | `RtlInitializeConditionVariable`/`RtlSleepConditionVariableCS` map almost exactly |
| `pthread_rwlock_*` (9) + `pthread_rwlockattr_*` (4) | `RtlAcquireSRWLockShared`/`Exclusive`; the `timed*` variants have no NT primitive and need a poll loop |
| `pthread_key_create` `pthread_key_delete` `pthread_getspecific` `pthread_setspecific` | TLS slots; `NtCurrentTeb()->TlsSlots` is already reachable |
| `pthread_barrier_*` (3) + `pthread_barrierattr_*` (4) | pure algorithm over a mutex+condvar |
| `pthread_spin_*` (5) | pure algorithm, `RtlInterlocked*` |
| `pthread_cancel` `pthread_setcancelstate` `pthread_setcanceltype` `pthread_testcancel` `pthread_cleanup_push` `pthread_cleanup_pop` | NT has no cancellation model; deferred cancellation is implementable as a flag checked at cancellation points, asynchronous cancellation is not |
| `pthread_getschedparam` `pthread_setschedparam` `pthread_setschedprio` `pthread_getconcurrency` `pthread_setconcurrency` `pthread_getcpuclockid` `pthread_kill` (CX) `pthread_sigmask` (CX) `pthread_mutex_consistent` (RPP) | scheduling/signalling surface; `NtSetInformationThread` covers priority, the rest are stubs |

Knock-on: `flockfile`/`ftrylockfile`/`funlockfile` and the `*_unlocked`
stdio family are currently correct only because nothing is threaded.

### Complex arithmetic: `complex.h` (66, all base)

| what | note |
|---|---|
| all 66 (`cabs*` `cacos*` `cacosh*` `carg*` `casin*` `casinh*` `catan*` `catanh*` `ccos*` `ccosh*` `cexp*` `cimag*` `clog*` `conj*` `cpow*` `cproj*` `creal*` `csin*` `csinh*` `csqrt*` `ctan*` `ctanh*`, each in `double`/`float`/`long double`) | No `complex.h`, no `_Complex` support anywhere in `src/math/`. Pure algorithm, zero OS dependency — this is a scope decision, not a platform one, exactly as `POSIX-HEADER-INVENTORY.md` argued. The blocker worth checking before starting is toolchain, not NT: `tcc` supports `_Complex` only partially, and `arch/*/src/fpconv.S` would need extending. Musl's `src/complex/` is ~40 small files and is the obvious reference. |

### Wide-character I/O and locale variants (84, all base or CX)

The single most *tractable* base gap: no OS support is needed for any of
it, and the multibyte core it sits on (`mbrtowc`/`wcrtomb`,
`src/stdlib/mbrtowc.c`, `src/internal/utf.c`) already exists and is
clause-audited (ledger priority 8).

| group | count | functions | note |
|---|---|---|---|
| wide stdio | 7 remaining of 17 | still missing: `fwprintf fwscanf open_wmemstream swprintf swscanf wprintf wscanf` (`open_wmemstream` is CX).  **Done**: `fgetwc fgetws fputwc fputws fwide getwc getwchar putwc putwchar ungetwc` (src/stdio/wide.c) | the orientation flag now exists on `struct _IO_FILE`, together with the two conversion states and the wide pushback slot the character-at-a-time functions need, so what is left is exactly the wide front end onto `src/stdio/printf.c`/`scanf.c`; `src/stdio/mem.c` already has `open_memstream`, so `open_wmemstream` is a variant of existing code |
| wide `stdarg` forms | 6 | `vfwprintf vfwscanf vswprintf vswscanf vwprintf vwscanf` | falls out of the row above for free |
| `wchar.h` string/convert | 3 remaining of 27 | still missing: `wcstod wcstof wcstold`.  **Done**: `mbsnrtowcs wcpcpy wcpncpy wcscasecmp wcscasecmp_l wcscoll wcscoll_l wcscspn wcsdup wcsftime wcsncasecmp wcsncasecmp_l wcsnlen wcsnrtombs wcspbrk wcsspn wcsstr wcstok wcstol wcstoll wcstoul wcstoull wcsxfrm wcsxfrm_l` | pure algorithm, and it was: `src/string/wcs*.c` gained the mirrors of the byte-string files beside them, and `src/stdlib/wcstoimax.c` became `src/stdlib/wcstol.c` when the four `wcstol` forms joined the parser it already had.  The note here used to say the float forms "can wrap the existing `src/stdlib/strtod.c` through a narrowing pass" -- **that turned out to be wrong** and is the reason the three are still open: a conforming subject sequence is unbounded, `strtod()` takes a contiguous buffer, and narrowing into any fixed buffer truncates a value this library currently converts exactly.  See the fence in test/posix-wchar.c |
| `wcswidth` `wcwidth` | 2 | XSI | needs an East-Asian-width table; the only entry here with real data behind it |
| `wctype.h` `_l` variants | 18 | `iswalnum_l iswalpha_l iswblank_l iswcntrl_l iswctype_l iswdigit_l iswgraph_l iswlower_l iswprint_l iswpunct_l iswspace_l iswupper_l iswxdigit_l towctrans_l towlower_l towupper_l wctrans_l wctype_l` (CX) | one-line forwards to the non-`_l` forms; ntlibc supports only the POSIX locale, and `src/misc/locale.c` already has `newlocale`/`uselocale` for the handle type |
| `ctype.h` `_l` variants | 14 | `isalnum_l isalpha_l isblank_l iscntrl_l isdigit_l isgraph_l islower_l isprint_l ispunct_l isspace_l isupper_l isxdigit_l tolower_l toupper_l` (CX) | identical: one-line forwards, same as `strcoll_l`/`strxfrm_l`/`strerror_l` already are |

### Tracing: `trace.h` (50, all optional, none base)

| what | note |
|---|---|
| all 50 `posix_trace_*` | The `_POSIX_TRACE` option and its `TRC`/`TRL`/`TEF`/`TRI` sub-options. Twenty-three are additionally marked `OB` (obsolescent). glibc does not implement this either. ETW is a wholly different model and would not be wrapped by these. **N/A, will not be done** — recorded once here so nobody re-derives it. `POSIX-HEADER-INVENTORY.md` already reached the same conclusion at header level. |

### Networking beyond the AFD stub (34)

| group | count | functions | note |
|---|---|---|---|
| `netdb.h` name resolution | 3 base | `getaddrinfo freeaddrinfo getnameinfo` | needs a DNS resolver. No NTDLL primitive; `dnsapi.dll`/`ws2_32.dll` are Win32. Implementable NTDLL-only as a UDP DNS client *on top of a working AFD socket layer* — i.e. gated on the block above being real |
| `netdb.h` `/etc/*` databases | 19 base | `endhostent endnetent endprotoent endservent gai_strerror gethostent getnetbyaddr getnetbyname getnetent getprotobyname getprotobynumber getprotoent getservbyname getservbyport getservent sethostent setnetent setprotoent setservent` | pure flat-file parsers over `%SystemRoot%\System32\drivers\etc\{hosts,services,protocol,networks}`, which NT does ship. No OS dependency at all beyond `stdio.h`. `gai_strerror` is a bare string table. Genuinely cheap; blocked only on nobody wanting them before `getaddrinfo` exists |
| `sys/socket.h` remainder | 8 base | `getpeername getsockname recvfrom recvmsg sendmsg sendto sockatmark socketpair` | explicitly deferred by `include/sys/socket.h`'s scope note (UDP, `AF_UNIX`, address introspection); staged as `test/networking-audit.md` sec 6 stages 4-6. Not startable in any meaningful sense until the AFD layer works on *some* platform |
| `net/if.h` | 4 base | `if_freenameindex if_indextoname if_nameindex if_nametoindex` | interface enumeration; `\Device\Tcp`-family ioctls or the `IP Helper` API. Same blocker |

### Memory mapping: `sys/mman.h` (14)

| function | option | note |
|---|---|---|
| `mmap` `munmap` `mprotect` | base | `NtCreateSection`/`NtMapViewOfSection(Ex)`/`NtUnmapViewOfSection`/`NtProtectVirtualMemory` are all pure NTDLL and map directly. The awkward parts are NT's 64 KiB allocation granularity vs POSIX's page granularity, and `MAP_FIXED` over an existing mapping (NT needs an explicit unmap first). Real work, no blocker. Nothing in `src/` references `NtCreateSection` today |
| `msync` | XSI | `NtFlushVirtualMemory` |
| `shm_open` `shm_unlink` | SHM | named sections under `\BaseNamedObjects`; falls out of `mmap` |
| `mlock` `munlock` | MLR | `NtLockVirtualMemory`; `include/sys/resource.h` already notes NT has no mlock *budget*, which is why `RLIMIT_MEMLOCK` is not honoured |
| `mlockall` `munlockall` | ML | same primitive, whole-address-space form |
| `posix_madvise` | ADV | advisory; a legal no-op returning 0 |
| `posix_typed_mem_open` `posix_typed_mem_get_info` `posix_mem_offset` | TYM | typed memory objects; no NT concept, and essentially no implementation anywhere. N/A |

### Process spawning: `spawn.h` (21, all `SPN`)

| what | note |
|---|---|
| `posix_spawn` `posix_spawnp` `posix_spawn_file_actions_*` (5) `posix_spawnattr_*` (10, four of them in `signal.h`, four more `PS`) | **closed** -- `include/spawn.h` plus `src/process/posix_spawn.c`/`spawn_file_actions.c`/`spawnattr.c`, over the existing `__spawn()`. All 21 are declared and defined. The assessment below was right about where the work was: no new NT primitive was needed, only the object types, the accessors, and a replay of the file actions onto the parent's own descriptor table immediately before `__spawn()` reads it (undone immediately after -- safe because there are no threads). What it did not anticipate is that most of the *spawn-attributes* have no NT meaning: `POSIX_SPAWN_SETSIGDEF`/`RESETIDS`/`USEVFORK` are satisfied by construction, `SETSIGMASK` only for an empty mask, and `SETPGROUP`/`SETSCHEDPARAM`/`SETSCHEDULER` not at all -- so `posix_spawn()` *fails* with the errno POSIX's ERRORS section routes each flag to rather than accepting and dropping it. See `test/POSIX-COVERAGE.md` group S. Original assessment: The machinery already exists: `src/process/spawn.c`'s internal `__spawn()` (declared `src/internal/libc.h`) is what `execve`/`fork`/`system()` and several tests are built on, and `test/spawn-cmdline-manyargs.c`/`test/spawn-runtimedata-stress.c` exercise it hard. What is missing is only the public POSIX surface: the `posix_spawn_file_actions_t`/`posix_spawnattr_t` types, the accessor pairs, and a translation layer from file actions onto the handle table `__spawn()` already builds. Highest value-per-effort item in this whole section, and the `SPN` option is one real programs actually use (it is how portable code avoids `fork()`, which on this platform needs `RtlCloneUserProcess`). |

### POSIX IPC and synchronisation (32)

| group | count | option | note |
|---|---|---|---|
| `semaphore.h` | 10 (`sem_close sem_destroy sem_getvalue sem_init sem_open sem_post sem_timedwait sem_trywait sem_unlink sem_wait`) | base | `NtCreateSemaphore`/`NtReleaseSemaphore`/`NtWaitForSingleObject` are native NT objects with semantics that match POSIX counting semaphores closely; named semaphores go under `\BaseNamedObjects`. Pure NTDLL, no blocker. Base POSIX, so this is a conformance hole, not an option |
| `mqueue.h` | 10 (`mq_close mq_getattr mq_notify mq_open mq_receive mq_send mq_setattr mq_timedreceive mq_timedsend mq_unlink`) | MSG | no NT primitive with priority-ordered messages; would be a named-pipe or section-backed ring buffer plus manual synchronisation, and `mq_notify` needs threads. Substantial, and gated on `pthread.h` |
| `sys/ipc.h` `sys/msg.h` `sys/sem.h` `sys/shm.h` | 12 (`ftok msgctl msgget msgrcv msgsnd semctl semget semop shmat shmctl shmdt shmget`) | XSI | System V IPC. Emulatable the way every non-SysV platform fakes it (named sections for shm keyed off an `ftok`-style path hash), but low value: XSI-optional, legacy, and superseded by the two rows above |

### Scheduling and timers (13)

| group | count | option | note |
|---|---|---|---|
| `sched_yield` | 1 | base | **closed** (see "Changes since `04edec2`") -- `NtYieldExecution` in new `include/sched.h` + `src/misc/sched.c`. Was the only base-POSIX interface in this cluster |
| `sched_get_priority_max` `sched_get_priority_min` `sched_getparam` `sched_getscheduler` `sched_rr_get_interval` `sched_setparam` `sched_setscheduler` | 7 | PS | `NtSetInformationThread`/`NtQueryInformationThread` give priority get/set; NT has no `SCHED_FIFO`/`SCHED_RR`/`SCHED_OTHER` policy distinction, so the policy calls would be `ENOTSUP` stubs. Also mostly meaningless before `pthread.h` |
| `timer_create` `timer_delete` `timer_getoverrun` `timer_gettime` `timer_settime` | 5 | CX | per-process interval timers. Same missing machinery as `alarm()`'s stub and the `getitimer`/`setitimer` `undefined-ok:` pair: needs a timer thread (`NtCreateTimer` + `NtSetTimer` exist and are pure NTDLL) delivering to the signal layer. Gated on threads, or on a dedicated helper thread |

### Asynchronous I/O: `aio.h` (8)

| what | option | note |
|---|---|---|
| `aio_read` `aio_write` `aio_cancel` `aio_error` `aio_return` `aio_suspend` `lio_listio` | base | NT I/O is asynchronous by default — the `IO_STATUS_BLOCK`/APC/completion-port machinery behind the very `NtReadFile`/`NtWriteFile` calls `src/unistd/read.c` and `write.c` already use synchronously. So the *facility* is present and the POSIX-shaped control-block API is not. `POSIX-HEADER-INVENTORY.md` calls this N/A on that basis; recorded here as a **base-POSIX absence** regardless, because these seven are not optional in POSIX.1-2017 and the argument "we have something better" does not change the conformance arithmetic. Whoever picks this up should decide deliberately rather than inherit the N/A |
| `aio_fsync` | FSC | as above, plus `NtFlushBuffersFile` |

### Locale and message catalogues (11)

| group | count | option | note |
|---|---|---|---|
| `nl_langinfo` `nl_langinfo_l` | 2 | base | reads out of the same locale data `src/misc/locale.c` already holds. Thin wrapper; the `LC_TIME` items overlap what `src/time/names.c` already has for `strftime`. Cheap, base-POSIX |
| `iconv` `iconv_open` `iconv_close` | 3 | base | `src/internal/utf.c` already implements the UTF-8/UTF-16 conversions; this is a stable-name wrapper plus a small codeset alias table. NT also exposes `RtlUnicodeToMultiByteN`/`RtlMultiByteToUnicodeN` in NTDLL for the ANSI codepage. Cheap, base-POSIX |
| `catopen` `catgets` `catclose` | 3 | base | XPG message catalogues. POSIX explicitly permits `catopen` to fail with no catalogues installed, so a conforming minimal implementation is a few dozen lines. Cheap, base-POSIX |
| `strfmon` `strfmon_l` | 2 | base | monetary formatting driven off `localeconv()`, which `src/misc/locale.c` has. Low value (the POSIX locale's monetary fields are all empty), but base |
| `fmtmsg` | 1 | XSI | formatted diagnostic to stderr and/or a console; no OS dependency beyond `stdio.h` |

Four of these five rows are POSIX **base** and none has an OS blocker.
Together they are 10 base interfaces closeable with no NT work at all —
the cheapest conformance points on the board after the `_l` forwards.

### Legacy, obsolescent, and unmapped XSI (27)

| group | count | option | note |
|---|---|---|---|
| `ndbm.h`: `dbm_clearerr dbm_close dbm_delete dbm_error dbm_fetch dbm_firstkey dbm_nextkey dbm_open dbm_store` | 9 | XSI | pure userspace flat-file database. No OS dependency, no modern caller |
| `utmpx.h`: `endutxent getutxent getutxid getutxline pututxline setutxent` | 6 | XSI | login-record accounting. NT tracks logon sessions through LSA/Winlogon; a fake flat file would be worse than nothing. **N/A** |
| `stropts.h`: `fattach fdetach getmsg getpmsg isastream putmsg putpmsg` | 7 | `OB XSR` | STREAMS. Obsolescent in POSIX itself, absent from every target platform and from musl. **N/A** |
| `ulimit` | 1 | `OB XSI` | superseded by `getrlimit`/`setrlimit`, both present. One-line wrapper if source compatibility is ever wanted |
| `syslog.h`: `closelog openlog setlogmask syslog` | 4 | XSI | no NTDLL-only equivalent — the NT Event Log is `advapi32`'s `ReportEventW`, outside this project's NTDLL-only rule. Implementable as a degraded local-only version writing formatted lines to `stderr`, or gated behind the existing `--enable-kernel32` opt-in. Not started |

### Small individually-actionable gaps (11)

Everything that did not cluster. These are the entries most likely to be
closed by accident in an afternoon.

| function | header | option | note |
|---|---|---|---|
| `exp2` `exp2f` `exp2l` | `math.h` | base | **closed** (see "Changes since `04edec2`") -- added to `src/math/exp.c` over the `__x87_exp2` helper `exp()`/`pow()` already use, which computes 2^x directly, so exp2 needs no argument reduction at all |
| `fmaxf` `fmaxl` `fminf` `fminl` | `math.h` | base | **closed** (see "Changes since `04edec2`") -- `src/math/fmax.c` defined only the `double` forms; the `f`/`l` suffixes exist for essentially every other `math.h` entry in the tree, so this was an oversight rather than a decision |
| `waitid` | `sys/wait.h` | base | **closed** (see "Changes since `04edec2`") -- built on `do_waitpid()` rather than beside it, so the child-table walk and exit-status decoding are shared with `waitpid`. `WNOWAIT` turned out to be expressible (record the status, skip `__child_remove`) but reachable only through a child-table state that `do_waitpid`'s any-child scan mishandled, which was fixed first and separately. `WSTOPPED`/`WCONTINUED` are a genuine platform impossibility and are fenced `N/A` with the NT mechanism named |
| `statvfs` `fstatvfs` | `sys/statvfs.h` | base | **closed** (see "Changes since `04edec2`") -- `src/stat/statvfs.c`, over `FileFsFullSizeInformation`/`FileFsSizeInformation`/`FileFsAttributeInformation`/`FileFsDeviceInformation`/`FileFsVolumeInformation`. The claim that these classes "cover every field" turned out to be wrong: `f_files`/`f_ffree`/`f_favail` have no NT source at all and are documented zeros, not fabricated counts |
| `sigignore` | `signal.h` | `OB XSI` | one line over `sigaction`; obsolescent, and its four siblings (`sighold`/`sigrelse`/`sigset`/`sigpause`) are already present |
| `getdate` | — | XSI | *not* absent — `src/time/getdate.c` exists; listed here only because `POSIX-HEADER-INVENTORY.md` implies otherwise |

## XSI vs base, restated

Of the 473 absent interfaces:

**256 are POSIX base** (no option marker at all), i.e. real conformance
holes. Exactly, by header:

| header | base absences | | header | base absences |
|---|---|---|---|---|
| `pthread.h` | 70 | | `sys/mman.h` | 3 |
| `complex.h` | 66 | | `nl_types.h` | 3 |
| `netdb.h` | 22 | | `iconv.h` | 3 |
| `wchar.h` | 17 | | `sys/statvfs.h` | 2 |
| `stdio.h` (wide) | 14 | | `monetary.h` | 2 |
| `semaphore.h` | 10 | | `langinfo.h` | 2 |
| `sys/socket.h` | 8 | | `sys/wait.h` | 1 |
| `trace.h` | 8 | | `sched.h` (`sched_yield`) | 1 |
| `math.h` | 7 | | `stdarg.h` (wide) | 6 |
| `aio.h` | 7 | | `net/if.h` | 4 |

(The eight `trace.h` rows counted as base are a scraping artefact — a
handful of `posix_trace_attr_*` pages put the declarator outside the
`opt-start`/`opt-end` region their siblings sit inside. The whole header
is the optional `_POSIX_TRACE` group; treat base-absent as 248, not 256,
if that matters to you.)

**39 are XSI**: `ndbm.h` (9), `utmpx.h` (6), `syslog.h` (4),
`sys/msg.h` (4), `sys/shm.h` (4), `sys/sem.h` (3), `wchar.h`'s
`wcwidth`/`wcswidth` (2), `pthread.h`'s `TPP`/`TPI` prioceiling pair
(2), and one each of `fmtmsg`, `ftok`, `msync`, `sigignore`, `ulimit`.
A further 7 are `OB XSR` (STREAMS). These are option-group gaps, not
conformance holes.

**178 are other optional groups**: tracing (42 more beyond the 8 above),
spawn (21 across `spawn.h` and `signal.h`), thread sub-options
(`TSH`/`TPS`/`TSA`/`TSS`, 12), CX (52 — mostly the `_l` locale variants
and the `timer_*`/`pthread_kill`/`pthread_sigmask` entries), message
passing (10), process scheduling (11), memory options
(`ML`/`MLR`/`SHM`/`TYM`/`ADV`, 10).

The practical reading: ntlibc's *base* POSIX conformance is materially
better than the headline 59% suggests, once threads and complex
arithmetic are set aside as declared scope decisions. Excluding
`pthread.h` and `complex.h`, base absences drop from 256 to **120**, and
37 of those 120 are wide-character stdio with no platform blocker at
all, another 22 are `netdb.h`'s flat-file databases (also no blocker),
and 10 more are the locale/message-catalogue group. That is 69 of 120
closeable without touching NT.

## Tests added alongside this accounting

Small and deliberately not a clause audit — these close part of the
"implemented, but no assertion anywhere" list rather than starting a new
ledger section. All three files run on **real Windows in CI** as well as
under Wine, and nothing added here depends on Wine-specific behaviour.

- `test/posix-stdio.c`: `test_putc_family` (`putc`, `putc_unlocked`),
  `test_putchar_return` (`putchar`, `putchar_unlocked`), `test_getchar`
  (`getchar`, `getchar_unlocked`, against a `freopen`'d stdin rather
  than whatever stdin happens to be), `test_fseeko_ftello`,
  `test_flockfile` (`flockfile`, `ftrylockfile`, `funlockfile`), and
  `test_v_forms` (`vfprintf`, `vsnprintf`, `vsprintf`, `vsscanf`,
  `vfscanf`, `dprintf`, `vdprintf`, each reached through a real
  `va_list`, so `va_start`/`va_end` are reached too). 19 previously-
  unasserted names.
- `test/posix-string.c`: `test_strcoll_l`.
- `test/posix-signal.c`: `test_killpg`, `test_sigaltstack_disabled`.

That is 22 of the original 134 (the 18 named above, plus `vsscanf`,
`va_start`, `va_end` and `strcoll_l` reached along the way). The
remaining 112 — 70 of them the `math.h` `f`/`l` tail — are left for the
successor; see below.

## Where this stopped, and what is next

**Rebased onto `04edec2`** and every count re-derived against that tree,
not carried forward from the earlier `bd9982c` pass — the ledger closing
out moved 14 functions from *implemented, not clause-audited* into
*implemented + clause-audited* (`sys/select.h`, `poll.h`,
`sys/resource.h`, `utime.h`, `assert.h`, `isascii`/`toascii`), and the
absent set did not move at all.

**Reached and individually noted:** every one of the 1177 interfaces is
classified — the bucket assignment is complete and mechanical, and the
`sys/socket.h` shape was specifically hunted for elsewhere in the tree
(the "permanent degenerate stubs" table is the result).

**Individually noted in the absent section:** all clusters, at the
granularity shown. Four families are covered by a *family* note plus a
complete name list rather than a per-function note, because the note
would have been identical 50-100 times: `pthread.h` (noted per
sub-family instead, 12 rows for 100 functions), `complex.h` (one row for
66), `trace.h` (one row for 50), `ndbm.h`/`utmpx.h`/`stropts.h` (one row
each). Nothing is unlisted; nothing is unexplained; nothing here is
finer-grained than the note it would carry.

**Not yet reached — the next successor's queue, in order:**

1. **The remaining implemented-but-unasserted functions.** Not an
   accounting gap but a test gap, and the cheapest work in this file —
   and it already paid for itself once (the `vdprintf` leak above).
   The four a ledger row claimed by name while nothing called them
   (`utimes`, `fpathconf`, `readlink`, `unlinkat`) are **done**; see
   "Successor session" at the end of this file for what they cost and
   what they found.
2. **A clause audit of the headers the ledger's priority order never
   named.** The ledger closed every row it opened at `04edec2`, but its
   eleven priority groups never covered these at all — each has a real
   implementation, most have a real test file, and none has a single
   ledger row:

   | header | functions | test file today |
   |---|---|---|
   | `termios.h` | 11 | `test/posix-termios.c` |
   | `search.h` | 11 | `test/posix-glob.c` (partly) |
   | `fenv.h` | 11 | `test/posix-math.c` (partly) |
   | `arpa/inet.h` | 8 | `test/posix-socket.c` |
   | `pwd.h` | 7 | `test/pwd.c` |
   | `grp.h` | 7 | `test/posix-grp.c` |
   | `regex.h` | 4 | `test/posix-glob.c` |
   | `dlfcn.h` | 4 | `test/posix-dl.c` |
   | `glob.h`/`fnmatch.h`/`wordexp.h` | 5 | `test/posix-glob.c` |
   | `ftw.h` | 2 | `test/posix-io.c` |
   | `sys/uio.h` | 2 | `test/posix-io.c` |
   | `sys/utsname.h`/`sys/times.h`/`sys/time.h`/`stropts.h` | 4 | `test/posix-sysmisc.c` |

   **Status:** all of these except `arpa/inet.h`, `ftw.h`,
   `sys/uio.h` and the `sys/utsname.h`/`sys/times.h`/`sys/time.h`/
   `stropts.h` row have since been clause-audited; see
   `test/POSIX-COVERAGE.md`'s "successor-queue item 2" sections
   (groups A-G) for the rows, the bugs found, and the per-header
   statement of which oracle applies. `arpa/inet.h` was deliberately
   left alone in that pass because the socket subsystem was under
   concurrent modification. Two of the "test file today" entries above
   were wrong and are corrected in place: `fenv.h`'s coverage is in
   `test/posix-math.c`, not `test/math.c` (which asserts nothing about
   that header at all), and `regex.h`'s is in `test/posix-glob.c`, not
   `test/posix-parse.c` (which contains no regex).

   That is the ledger's priority order continued; *this* file audits
   no clause of any of them and does not claim to — the clauses live in
   `test/POSIX-COVERAGE.md`, which now carries groups A-G. The four
   rows the Status note lists as still open are what remains of this
   item; `ftw.h` and `sys/uio.h` are the cheapest of them, and
   `arpa/inet.h` should wait for the socket subsystem to settle.
3. **The `math.h` `f`/`l` tail** (150 implemented, 70 with no assertion
   at all). Bulk, low risk, mechanical. **Done** — see "Closed: the
   `math.h` `f`/`l` tail (70)" under "Successor session" below.
4. **Re-derive this file's `impl` vs `audited` split with a method that
   does not have these failure modes**, after (2). The split here was
   derived by tokenising the ledger's first column; it is exact for a
   ledger row that names a function, and conservative for one that
   describes a group in prose. That is not the whole of it: four
   later audits each hit the method independently, in both directions
   and in three ways that are not counting errors at all. See "The
   counting method is what item 4 has to replace" at the end of this
   file for the five failure modes, the evidence, and the
   pages-audited counting convention that follows from them. Re-running
   the existing pipeline unchanged reproduces all five, so this item is
   a re-derivation, not a re-run.

**Explicitly unverified, and why:**

- **The `sys/socket.h` cluster's working state.** Classified from
  `test/posix-socket.c` exiting 77 under Wine and from the CI results
  quoted in `1f2e40e` (`socket()` fixed on real Windows, `bind()` still
  failing). This file did not itself run the real-Windows leg — CI is
  the only authority for real-NT behaviour, and Wine diverges materially
  and repeatedly. The subsystem is under active repair, so that section
  is a dated snapshot by construction: when `bind()` lands, it needs
  rewriting, not editing.
- **The `undefined-ok:` reasons** are quoted from `make linkcheck`'s
  report, which truncates each header comment at the first line break.
  The full reasoning is in the declaring header and was not
  independently re-derived.
- **i386.** Every count in this file is from an x86_64 configure/build.
  `arch/` is not shared, so `lib/libc.a` differs per arch and
  `tools/linkcheck.sh` can only see one at a time (its own comment says
  why). The declared set comes from `include/`, which *is* shared, so
  the classification should be arch-independent; that was not verified
  by re-running the whole pipeline against i386.

## Successor session: closing the never-asserted list

Item 1 of the queue above, worked in order. The list was re-derived
mechanically rather than taken on trust: every name in the "Implemented,
but no assertion anywhere" section was re-grepped (`grep -lw` for each,
against `test/*.c`) at `0e3aefa`, and all 112 were confirmed still
unnamed. Excluding the 70 `math.h` `f`/`l` variants, which item 3 covers
separately, that leaves **42** names in six headers.

### Closed: the four a ledger row claimed by name (`test/posix-unistd.c`)

`test_utimes`, `test_fpathconf`, `test_readlink`, `test_unlinkat` — the
first assertions any of these four have ever had. Each block cites its
`pubs.opengroup.org` page in a comment, as the rest of the file does.
Both `readlinkat` and `readlink` are reached, so five names in total
leave the list.

**One bug, immediately** — the pattern the `vdprintf` leak established
holds: **`unlinkat()` masks undefined `flag` bits off instead of
rejecting them with `EINVAL`**, so `unlinkat(fd, path, AT_SYMLINK_NOFOLLOW)`
silently *deletes the file* instead of failing. Fenced in
`test_unlinkat()` and written up in `test/POSIX-COVERAGE.md`'s "Bugs
found (never-asserted sweep, unistd.h group)"; not fixed here, because a
fix belongs in a change of its own.

**One clause marked N/A, with the reason:** `fpathconf()`'s `[EBADF]`.
`src/unistd/sysconf.c` ignores `fildes` entirely and forwards to
`pathconf()`, so a closed descriptor still answers — which POSIX permits,
since `fpathconf.html` lists `[EBADF]` under *may fail*, not *shall
fail*. Asserting it in either direction would be asserting a choice the
spec deliberately leaves open.

### Closed: the `sys/stat.h` five (`test/posix-unistd.c`)

`test_mkdirat` and `test_mkfifo_mknod_stubs`, checked against
`mkdir.html`, `mkfifo.html` and `mknod.html`. `mkdirat()` turned out to
be in better shape than its never-called status suggested: [EEXIST] on
both a directory and a plain file, [ENOENT] for a missing prefix and for
the empty string, [ENOTDIR] both for a regular-file prefix component and
for an `fd` open on a non-directory, [EBADF] for an unopened descriptor,
and dirfd-relative creation all behave as the page requires. No defect.

**N/A, with the reason:** `mkfifo`/`mkfifoat`/`mknod`/`mknodat` are
permanent stubs (the degenerate-stub table above), so every clause on
those two pages that presupposes a call can succeed even once has
nothing to observe. What *is* asserted is the clause a stub can still
honour and that both pages state in identical words — "if -1 is
returned, no FIFO shall be created" / "the new file shall not be
created" — plus `mknod()`'s [EPERM], which is POSIX's own answer for an
unprivileged caller and therefore exactly right. `mkfifo()`'s `ENOSYS`
is not in `mkfifo.html`'s ERRORS list, but no errno that page *does*
list would be truthful either; that deviation stays recorded here as a
known stub rather than being re-opened as a new fenced bug, so the test
pins the -1 and the absence of debris and leaves the errno to this note.

Also N/A: `mkdirat()`'s `mode`. This ledger already records directory
mode bits as implementation-defined, and `src/stat/mkdir.c` discards
`mode` by design, so the "permission bits ... initialized from mode"
clause has nothing observable behind it.

### Closed: `signal.h`, `stdarg.h`, `setjmp.h`, `stdio.h` (11 names)

The rest of the non-`math.h` list, in `test/posix-signal.c`,
`test/posix-misc.c` and `test/posix-stdio.c`, each block citing its page.

- **`signal.h` (5)** — `test_sighold_sigrelse`, `test_sigset`,
  `test_sigpause`, `test_siginterrupt`, against `sigset.html` and
  `siginterrupt.html`. **Three bugs**, all fenced, all probed rather than
  inferred: `sighold`/`sigrelse` report success for an illegal signal
  number (they discard `sigaddset()`'s failure); `sigset()` cannot report
  `SIG_HOLD` and `<signal.h>` does not define the constant at all; and
  `siginterrupt()` never validates `sig`. Written up in
  `test/POSIX-COVERAGE.md`'s "Bugs found (never-asserted sweep, signal.h
  group)". **N/A:** `sigpause()`'s "suspend until a signal is received"
  half and `siginterrupt()`'s SA_RESTART effect — both for the reason
  that file's banner already gives for `sigsuspend()`/`sigwait()`.
- **`stdarg.h` (4)** — `vprintf` and `vscanf` reached with stdout
  redirected at fd level and stdin through `freopen()`; `va_arg`/`va_copy`
  through a helper that walks a list, then walks a `va_copy` of it from
  the same point and checks the sequences match. No defect.
- **`setjmp.h` (2)** — `_setjmp`/`_longjmp` folded into
  `test/posix-misc.c`'s existing `test_setjmp`. No defect. **N/A:** the
  "shall not manipulate the signal mask" clause is vacuous here, since
  nothing in `src/setjmp` touches a mask on either arch; the test checks
  it across the pair anyway as a regression net.
- **`stdio.h` (2)** — `getc_unlocked` against `getc()` on the same
  stream inside a `flockfile()` scope, and `tempnam` for `dir`/`pfx`
  handling, non-collision, usability and `free()`-ability. No defect.
  **N/A:** `tempnam`'s `[ENOMEM]` (needs allocator exhaustion) and the
  `_unlocked` family's thread-safety distinction (no threading here).

**A caveat on `make asan` that this session had to establish.** The
`vdprintf` leak above was caught by `tools/asan-build.sh`'s
LeakSanitizer, so `tempnam()`'s "suitable for use in a subsequent call
to `free()`" was written expecting the same net. It does not exist in
this environment: `tools/asan-build.sh` reports **`0/0 tests passed, 47
unlinkable`** here, every test failing to link with `undefined reference
to 'NtYieldExecution'` (absent from `fuzz/ntstubs.c`). That was verified
on an unmodified `0e3aefa` checkout as well, so it is pre-existing and
not caused by anything in this sweep — but it means the asan stage of
`tools/gate.sh` currently passes **vacuously**, and any leak these new
tests would have caught is unverified locally. Worth fixing before the
next never-asserted batch, since leak-on-first-call is precisely the
defect shape this queue keeps turning up.

### Closed: the remaining `unistd.h` 18

`test/exec.c` for the four exec names, `test/posix-unistd.c` for the
rest.

- **`execl`, `execle`, `execlp`, `fexecve`** (`test/exec.c`, new
  `--exec-l`/`--exec-le`/`--exec-lp`/`--exec-f` roles). That file's
  existing harness is exactly what these need and needs no `fork()`: the
  parent `__spawn()`s itself in a role, that child execs itself in an
  `--argvl` role, and the exec'd image checks what it received. Also
  covered: the exec'd image's exit status becoming the caller's,
  `[ENOENT]` for both the direct and the PATH-searching l-form,
  `[EBADF]` for `fexecve()` on a closed descriptor, and the "if
  execution fails, the calling process image remains unchanged" clause
  (the failing roles keep running and report an errno afterwards). No
  defect.
- **`confstr`, `swab`, `sync`, `getlogin`, `getlogin_r`, `linkat`, and
  the identity/session stubs `fchown`/`fchownat`/`lchown`/`setregid`/
  `setpgrp`/`setsid`/`tcgetpgrp`/`tcsetpgrp`** (`test/posix-unistd.c`).

**Two more bugs**, both fenced, both probed:

- **`confstr()` reports success for an invalid name** — returns 1 with
  errno untouched where `confstr.html` requires 0 with `[EINVAL]`, and
  neither of POSIX's two zero-returning cases is reachable for any
  input.
- **`tcgetpgrp()`/`tcsetpgrp()` never produce the shall-fail `[EBADF]`**
  — `fd` is discarded without reaching `__fd_get()`. Separable from the
  deliberate single-session design: a fixed process group for a *valid*
  descriptor is that design; succeeding for fd 4096 is a missing
  argument check.

Both are written up in `test/POSIX-COVERAGE.md`'s "Bugs found
(never-asserted sweep, unistd.h group)", now three entries.

**N/A, with reasons:**

- **`pause()` — not callable from this suite at all.**
  `src/unistd/sleep.c` implements it as `NtDelayExecution` with a
  maximal timeout, and this platform has no asynchronous delivery to end
  it. A call hangs forever; a test would *deadlock the run* rather than
  fail it, which is worse than no test. This is the one name in the
  original 112 that cannot be given an assertion at all, and the reason
  is a platform impossibility rather than an omission.
- **The identity/session stubs' effects.** One user, one fixed session
  (`src/unistd/ids.c` and `src/termios/termios.c` both say so), so "the
  file's user ID shall be set", "shall become a session leader" and
  "shall set the foreground process group" have nothing observable
  behind them. The return values and cross-getter consistency
  (`setpgrp() == getpgrp()`, `setsid() == getsid(0)`,
  `tcgetpgrp(0) == getpgrp()`) *are* asserted.
- **`linkat()`'s `AT_SYMLINK_FOLLOW`.** `src/unistd/link.c` opens with
  `FILE_OPEN_REPARSE_POINT` unconditionally and ignores `flag`, so it
  always implements the flag-clear branch; distinguishing the two needs
  a symbolic link, which needs `SeCreateSymbolicLinkPrivilege` and is
  not available on the CI images this suite is the authority on.
- **`sync()`'s scheduling.** POSIX permits `sync()` to be undetectable
  by any conforming observation; `fsync()` is the call with a completion
  guarantee, and `test/unistd.c` already covers it.
- **`swab()`'s odd-`nbytes` last byte.** "The disposition of the last
  byte is unspecified", so the test asserts the swapped prefix and that
  nothing past `nbytes` was touched, and deliberately does not pin
  `src/unistd/swab.c`'s own documented choice.

### Closed: the `math.h` `f`/`l` tail (70)

Item 3 of the queue above, taken after the rest rather than left for a
successor. All 70 are in `test/posix-math.c`, in eighteen new
`*_variants` functions plus `test_compare_macros`; the ledger's
math.h section carries the per-group table.

POSIX gives `acos()`, `acosf()` and `acosl()` **one page and one
RETURN VALUE/ERRORS table**, the `f`/`l` entries differing only in
argument and return type, so each block cites the same page as its
`double` counterpart and asserts the same clauses at the other two
widths. That makes this batch mechanical, as the queue predicted — but
not empty: what is new is the *type*, and a special-value table can be
right for `double` and wrong for `float` (a wrong-width constant, a
promotion that routes the `f`-form through the `double` body and back, a
`HUGE_VALF`/`HUGE_VAL` mixup). The per-width overflow thresholds are
where that would show, so they are asserted at each type's own width
rather than at the `double` one: `sinhf(200.0f)`, `ldexpf(1.0f, 200)`,
`nextafterf(FLT_MAX, HUGE_VALF)`, `scalblnf(1.0f, 200L)`.

Six of the 70 are not `f`/`l` variants of anything: `isgreater`,
`isgreaterequal`, `isless`, `islessequal`, `islessgreater` and
`isunordered` are macros in `include/math.h`, and their whole reason for
existing — "shall not raise the invalid floating-point exception when x
and y are unordered" — is directly checkable through `<fenv.h>` and is
checked.

**No bugs found**, which is the honest result for a batch the queue
itself called low-risk. Two things are worth recording anyway:

- **Two accuracy assertions were written and then withdrawn**, not
  weakened silently: `erfc(0) == 1` and `tgamma(5) == 24` are exact
  mathematically but land ~1e-9 short in `src/math/erf.c` and
  `src/math/gamma.c`. POSIX mandates no accuracy for these (this file's
  and `test/posix-math.c`'s banners both say so), so asserting `==`
  would have been asserting an accuracy coincidence rather than a
  clause. The first is now printed as informational, the second checked
  with a tolerance loose enough to be about *routing* rather than about
  the last bit.
- **A test-authoring trap:** `include/math.h` defines `NAN` as
  `(0.0f/0.0f)`, and on this toolchain that division is *not*
  constant-folded — evaluating the macro inside an `feclearexcept()`
  guarded region sets FE_INVALID itself and fails the measurement for a
  reason unrelated to the code under test. `test_compare_macros` hoists
  the NaN into a `volatile` first. C99 wants `NAN` to be a constant
  expression; that is a header/codegen matter rather than a POSIX clause
  about these macros, so it is noted rather than fenced. Anyone adding
  `<fenv.h>` assertions elsewhere will hit it.

### Remaining after this session

**Nothing.** The "Implemented, but no assertion anywhere" list is
closed: all 112 names now have a first-ever assertion, with the single
exception of `pause()`, which cannot be *called* from a test at all
without deadlocking the run (see above) and is recorded as N/A with that
reason. Item 1 and item 3 of the successor queue are both done.

(Item 2 was untouched when this sweep was written. It no longer is: a
concurrent session clause-audited eight of its twelve header rows —
`termios.h`, `search.h`, `fenv.h`, `pwd.h`/`grp.h`, `regex.h`,
`dlfcn.h` and the `glob.h`/`fnmatch.h`/`wordexp.h` group — landing as
groups A-G of `test/POSIX-COVERAGE.md`. See the Status note under item
2 above, and "Changes since the clause audit of groups A-G" below.
`arpa/inet.h`, `ftw.h`, `sys/uio.h` and the
`sys/utsname.h`/`sys/times.h`/`sys/time.h`/`stropts.h` row are what is
left of item 2; item 4 is still untouched and, now that most of item 2
has landed, is the next thing due.)

**Score for the sweep: 111 of 112 names given a first-ever assertion,
and six bugs found** — `unlinkat()` masking undefined `flag` bits,
`confstr()` never reporting `[EINVAL]`, `tcgetpgrp`/`tcsetpgrp` never
reporting `[EBADF]`, `sighold`/`sigrelse` never reporting `[EINVAL]`,
`sigset()` unable to report `SIG_HOLD` (which `<signal.h>` does not even
define), and `siginterrupt()` never validating `sig`. The predecessor's
lesson holds and then some: **every one of the six is a *shall-fail*
error clause**, and every one survived because no caller had ever been
in a position to notice. The defects clustered entirely in the
platform-facing headers — four of the six non-`math.h` headers had one,
and `math.h`'s 70 names had none — which is worth remembering when
prioritising the next sweep. "Exists and links" remains a much weaker
statement than it looks.

## Changes since the clause audit of groups A-G

Dated note, 2026-08-24, added rather than folded into the headline
table for the reason the "Headline counts" section states: those
numbers are a mechanical snapshot of `04edec2` and are deliberately not
hand-edited. Item 4 of the successor queue — re-verify the `impl` vs
`audited` split after item 2 — is the thing that folds this in, and it
has not been run. Re-running it means re-running the whole pipeline in
"How this was produced", including re-fetching the 1191
`pubs.opengroup.org` index pages, which is not something a hand edit
can substitute for.

What moved: groups A-G of `test/POSIX-COVERAGE.md` audit eight of the
twelve headers in item 2's table — `termios.h` (11), `search.h` (11),
`fenv.h` (11), `pwd.h`/`grp.h` (7+7), `regex.h` (4), `dlfcn.h` (4) and
`glob.h`/`fnmatch.h`/`wordexp.h` (5): **60 interfaces**, every one of
which this file currently classifies under "Implemented, not
clause-audited (357)". Reading the frozen tables, subtract those from
357 and add them to *implemented + clause-audited*.

Two cautions on that 60, both instances of the caveat item 4 already
records — the split is derived by tokenising the ledger's **first
column**, which is exact for a row that names a function and
conservative for a row that describes a group in prose:

- Step 3 of "How this was produced", run against the new sections, sees
  **58** names, not 60. `hdestroy` and `wordfree` are audited (see
  `test/posix-glob.c`, which calls both and cites their pages) but are
  named only in the surrounding prose and in a sibling's clause column,
  never in a first column of their own. The pipeline would undercount
  them by exactly the mechanism item 4 warns about.
- Nothing else in this batch collides with an existing row. Every
  first-column identifier in groups A-G was checked against every
  first-column identifier earlier in the ledger; the intersection is
  empty, so no interface is claimed twice and no status contradicts
  another.
- Three parentheticals in the "Implemented, not clause-audited (357)"
  table are superseded by this note and were left standing rather than
  hand-edited, for the same frozen-snapshot reason: `termios.h`'s
  "`test/posix-termios.c` exists but the ledger has no section for it",
  `glob.h`'s "no ledger section", and `dlfcn.h`'s "(`test/posix-dl.c`
  exists)". All three headers now have a ledger section.

The bug counts elsewhere in this file are unaffected: the never-asserted
sweep's six bugs and groups A-G's finds are disjoint sets of functions,
in disjoint "Bugs found" sections of the ledger.

## Changes since the clause audit of groups H-J

Dated note, 2026-08-24. Same route as the "Changes since the clause
audit of groups A-G" note above, and for the same stated reason: the
headline counts are a mechanical snapshot of `04edec2` and are
**deliberately not hand-edited**. Item 4 of the successor queue is what
folds these in; it has not been run, and re-running it means re-running
the whole pipeline in "How this was produced", including re-fetching
the 1191 `pubs.opengroup.org` index pages. A hand edit cannot
substitute for that.

What moved: groups H-J of `test/POSIX-COVERAGE.md` audit three more
rows of the "Implemented, not clause-audited (357)" table.

**Group H — `ctype.h` (12).** `isalnum isalpha isblank iscntrl isdigit
isgraph islower isprint ispunct isspace isupper isxdigit`, in the new
`test/posix-ctype.c`. All twelve conformant; no BUGs. The row's own
note was accurate: priority 4's `is*` audit was a group consistency
test whose oracle shared its idiom with the implementation, and it
never opened the twelve individual pages. Subtract 12 from 357.

Checked against every earlier row, by the same first-column
tokenisation caution the A-G note records:

- All twelve names appear as first-column identifiers of their own in
  group H's table, so step 3 of "How this was produced" would see all
  twelve — no undercount of the `hdestroy`/`wordfree` kind here.
- No collision with any earlier first-column identifier. Priority 4's
  `ctype.h` row cites `isascii`/`toascii`/`tolower`/`toupper`/
  `_tolower`/`_toupper`; group H cites the disjoint twelve. Nothing is
  claimed twice and no status contradicts another.
- The `_l` variants of all twelve (`isalnum_l` and friends, `CX`) are
  declared nowhere in `include/` and defined nowhere in `src/`. They
  stay in this file's missing-interface accounting; group H does not
  touch them.

**Group I — `wctype.h` (16).** `iswalnum iswalpha iswblank iswcntrl
iswctype iswdigit iswgraph iswlower iswprint iswpunct iswspace iswupper
iswxdigit towctrans towlower towupper`, in the new
`test/posix-wctype.c`. All sixteen conformant; no BUGs. Subtract 16
from 357.

Note that this file's *other* table — "Audited (per
`test/POSIX-COVERAGE.md`)" — already credits `wctype.h` with 2
interfaces from priority 8. Those two are `wctype()` and `wctrans()`,
the constructors, which are not among the sixteen; group I audits them
again anyway (they are inseparable from `iswctype()`/`towctrans()`) but
claims no new count for them. The sixteen and the two are disjoint, so
neither table double-counts.

Checked against every earlier row:

- All sixteen appear as first-column identifiers of their own in group
  I's table, so step 3 of "How this was produced" would see all sixteen.
  `wctype` and `wctrans` also appear in a first column there and would
  be seen — hence the paragraph above, which is the one place in this
  batch where the tokeniser would *over*count rather than under.
- No collision otherwise. `test/posix-wchar.c` calls fourteen of the
  sixteen but is not cited as an audit row for any of them anywhere in
  `POSIX-COVERAGE.md`; group I is their first row.
- The `_l` variants of all sixteen stay in the missing-interface
  accounting.

**Group J — the long tail (19), in three subsections.** J1 covers
`locale.h`'s locale-object API: `newlocale duplocale freelocale
uselocale`, in the new `test/posix-locale.c`. **Two BUGs fenced**
(`newlocale()` never validates `category_mask`, a *shall fail*
[EINVAL]; `uselocale()` never returns `LC_GLOBAL_LOCALE`, so
`uselocale(0) == LC_GLOBAL_LOCALE` — the documented way to ask "am I on
the global locale?" — is always false when the truth is always true).
Two N/A by mechanism (`freelocale()` has nothing allocated to release;
`duplocale()`'s aliasing of a single immutable stateless object is
unobservable). Subtract 4 from 357.

Checked against every earlier row: all four appear as first-column
identifiers of their own in J1's table. `setlocale`/`localeconv`, the
`locale.h` row priority 4 already audited, are deliberately not
re-cited, so the "Audited" table's existing `locale.h` count of 2 and
J1's 4 are disjoint and neither double-counts.

**J2 — `stropts.h` (1): a correction to this file, not a subtraction
from it.** The `stropts.h` row's own parenthetical — "`src/ioctl/
ioctl.c` implements the name, not the STREAMS semantics POSIX attaches
to it" — turns out to be the accurate statement and the row it sits in
the inaccurate one. Reading `ioctl.html` against the tree:

- POSIX's `ioctl()` is declared in `<stropts.h>` as `int ioctl(int
  fildes, int request, ...)`. ntlibc has no `<stropts.h>` (nor any of
  the eight structures, the `I_*`/`S_*` constants, `FMNAMESZ`, or
  `isastream`/`getmsg`/`getpmsg`/`putmsg`/`putpmsg` that
  `basedefs/stropts.h.html` requires), and declares `int ioctl(int,
  unsigned long, ...)` in `<sys/ioctl.h>`, which is not a POSIX header.
- The two functions share a name, an `fd` parameter and nothing else:
  disjoint headers, disjoint signatures, disjoint command sets. POSIX
  says of everything ntlibc's version does that "for non-STREAMS
  devices, the functions performed by this call are unspecified", and
  `include/sys/ioctl.h`'s own banner opens "ioctl(): NOT a POSIX
  interface".

**So `ioctl` should be counted as absent, not as implemented-but-
unaudited.** That is a move *between* this file's two tables rather
than a subtraction from 357 — and, like every other count here, it is
recorded in this dated note rather than hand-edited into the frozen
`04edec2` snapshot. Item 4 of the successor queue is what folds it in;
whoever runs it should note that the pipeline derives "implemented"
from what links, and `ioctl` does link — the name is real, the
interface is not — so this is one row the mechanical pipeline cannot
get right on its own and that item 4 must special-case.

Group J2's ledger section records the clause detail: one covered row
(`[EBADF]`, the only general-condition clause not predicated on a
STREAMS device, which ntlibc does honour), one fenced UNIMPL (the
absent header), and the STREAMS command set as N/A — vacuous rather
than violated, because NT has no STREAMS subsystem and `fildes` can
never refer to a STREAMS device.

**J3 — the remaining fourteen (14).** `readv writev` (`sys/uio.h`),
`ftw nftw` (`ftw.h`), `posix_fadvise posix_fallocate` (`fcntl.h`),
`_setjmp _longjmp` (`setjmp.h`), `strlen strnlen` (`string.h`),
`times` (`sys/times.h`), `uname` (`sys/utsname.h`), `gettimeofday`
(`sys/time.h`) and `srand48` (`stdlib.h`), in the new
`test/posix-tail.c`. **Five BUGs fenced**, all five verified to pass
once fixed: `nftw()` with `FTW_CHDIR` reporting every entry below the
root as `FTW_NS` and returning 0; `nftw()` with no
descendant-of-itself protection; `posix_fadvise()` missing the
negative-`len` half of its `[EINVAL]` and the whole of its `[ESPIPE]`;
`posix_fallocate()` returning `[EBADF]` where `[ENODEV]` is required
and never checking write permission. **A sixth was found by the gate
itself**: `posix_fallocate()`'s `[EFBIG]` check is signed-integer
overflow — UndefinedBehaviorSanitizer caught it under `make asan` when
the first draft asserted that error live — and it is the only way that
error can be produced, so a compiler may delete the check. Subtract 14
from 357.

**This is where the count stops reconciling cleanly, and it is said
rather than forced.** Three separate reasons, all instances of the
caveat item 4 already records:

- `strlen` and `strnlen` are in the "Implemented, not clause-audited"
  table's `string.h` row, but `test/POSIX-COVERAGE.md`'s priority-1
  section ("string.h / strings.h") audited that header as a group. Its
  rows do not name `strlen` in a first column, so the tokeniser has
  never counted it either way — it is neither cleanly in the 357 nor
  cleanly in the audited set today. J3 gives both names first-column
  rows of their own, which the pipeline *will* count; whether that
  creates a double-count depends on whether item 4's re-run reads
  priority 1 as having covered them. Flagged rather than decided.
- `drand48`/`erand48`/`lrand48`/`mrand48`/`seed48`/`lcong48` are
  audited under priority 2 ("the random family"); only `srand48` is in
  the 357. J3's `srand48` rows necessarily *call* the other six as the
  observation channel, and their names appear in J3's clause column.
  That is exactly the sibling-column shape that undercounted
  `hdestroy`/`wordfree` before — except here the direction is the
  other one, since those six are already audited. Neither table should
  move for them.
- `ioctl` (J2) belongs in the absent accounting, not the 357 — a move
  between tables rather than a subtraction, and one the mechanical
  pipeline cannot infer, since `ioctl` does link.

Nothing above has been hand-fitted to make 357 come out even, and the
frozen `04edec2` headline counts are untouched. These four cases —
`strlen`/`strnlen`, the `drand48` family, `ioctl`, and `wctype`/
`wctrans` under group I — are collected with the other audits' as
evidence in "The counting method is what item 4 has to replace" below,
which is where the conclusion they point at is drawn.

Group totals for this session: H = 12, I = 16, J1 = 4, J2 = 1
(reclassified, not subtracted), J3 = 14. **46 interfaces given a
first-ever clause-cited row; 10 BUGs fenced; 2 assertion groups left
`rc=77` unverified** (`nftw()`'s symbolic-link clauses where
`symlink()` is unavailable, and `posix_fallocate()`'s allocation
clauses on i386, where WOW64 answers the documented "underlying file
system does not support this operation").

## Changes since the clause audit of the stdio.h and stdarg.h rows

Dated note, 2026-08-24, added rather than folded into the headline
table for the same reason the note above states and the "Headline
counts" section states before it: those numbers are a mechanical
snapshot of `04edec2` and are deliberately not hand-edited. Item 4 of
the successor queue is what folds this in, and it still has not been
run.

What moved: groups **K** and **L** of `test/POSIX-COVERAGE.md`
clause-audit the two rows this file's "Implemented, not clause-audited
(357)" table names as `stdio.h` (16) and `stdarg.h` (12) — **28
interfaces**, all 28 of which that table currently classifies as
implemented-but-unaudited.

**But the honest movement is 22, not 28, and the reason is worth
recording** — it is the same tokeniser hazard the note above warns
about, running in the opposite direction. Six of the 28 already had a
clause-cited first-column row in `test/POSIX-COVERAGE.md`'s priority-5
"stdio.h streams" section at `04edec2`: `tempnam`, `getc_unlocked`,
`vprintf`, `vscanf`, `va_arg` and `va_copy`. They were audited in the
very session that produced the snapshot — this file's "Implemented, but
no assertion anywhere" section says so in as many words ("`stdio.h`
(0): both (`getc_unlocked tempnam`) closed"; "`stdarg.h` (0): all four
(`va_arg va_copy vprintf vscanf`) closed") — but the 16/12 rows of the
frozen table were never updated to match, so those six are currently
counted in **both** places. Reading the frozen tables, subtract 28 from
357 and add **22** to *implemented + clause-audited*; the other six
were already there.

These six are the *overcount* case of the counting problem the section
"The counting method is what item 4 has to replace" below sets out. That
section is where the general conclusion is drawn, once, for all four
audits; this note records only what groups K/L contribute to it, and
its 22-of-28 is a count of pages audited, not of first-column tokens.

Two further cautions, both checked rather than assumed:

- Step 3 of "How this was produced", run against groups K and L, sees
  **27** first-column names, not 28. The one it does not see is
  `tempnam`, which groups K/L deliberately do not re-row (its
  priority-5 row already covers it and re-rowing it would be the
  double-count above, made worse). The remaining 27 each have a
  first-column row of their own, so no name in this batch depends on
  being mentioned only in prose or in a sibling's clause column — the
  failure mode that cost `hdestroy` and `wordfree` a count last time.
- Every first-column identifier in groups K and L was checked against
  every first-column identifier earlier in the ledger. The intersection
  is exactly the five re-rowed names named above (`getc_unlocked`,
  `va_arg`, `va_copy`, `vprintf`, `vscanf`), each of which is marked in
  its group K/L row as "pre-existing row under priority 5" so that a
  reader and a re-run of the pipeline reach the same total. Nothing
  else collides, and no status contradicts another: the group K/L rows
  extend the priority-5 ones (boundary and ERRORS clauses) rather than
  restating or overturning them.

Bug counts: groups K and L add **two BUGs** (`snprintf`'s missing
`[EOVERFLOW]` for `n > INT_MAX`, and the unrecognised `[CX]`
`<apostrophe>` flag in the `fprintf` family) and **one UNIMPL** (the
`[CX]` `'m'` assignment-allocation character in the `fscanf` family),
in their own "Bugs found (groups K/L)" and "UNIMPL found (groups K/L)"
sections. Disjoint from the never-asserted sweep's six and from groups
A-G's finds. Three further defects are recorded there under "Observed
behaviour" rather than fenced, because no assertion this suite can
write reaches them: `fscanf`'s `[ENOMEM]` (which `src/stdio/scanf.c`'s
own banner admits has "no channel"), `gets()`'s read-error-after-partial
-line return, and the standing caveat that `flockfile`'s no-ops are
N/A only for as long as `lib/libpthread.a` stays empty.

## Changes since the clause audit of the `unistd.h` row (group M)

Dated note, 2026-08-24, added rather than folded into the headline
table for the reason the "Headline counts" section states: those
numbers are a mechanical snapshot of `04edec2` and are deliberately not
hand-edited. This follows the precedent of "Changes since the clause
audit of groups A-G" immediately above; item 4 of the successor queue
is still the thing that folds both in, and it has not been run.

What moved: `test/POSIX-COVERAGE.md`'s new section **"unistd.h
identity, process group, session, scheduling (successor-queue item 2,
group M)"** clause-audits **25** of the 43 interfaces in the
"Implemented, not clause-audited (357)" table's `unistd.h` row:

`alarm chown fchown fchownat getegid geteuid getgid getgroups
gethostname getpgid getpgrp getsid getuid lchown nice pause setegid
seteuid setgid setpgid setpgrp setregid setreuid setsid setuid`

Reading the frozen tables, subtract those 25 from 357 and add them to
*implemented + clause-audited*; the `unistd.h` row of that table drops
from 43 to 18.

**Six bugs and three UNIMPLs**, every bug a *shall-fail* error clause:
`getgroups()` accepting a negative `gidsetsize`; the six `set*id()`
stubs reporting success for `setuid(0)` and for an unsupported id;
`getpgid()`/`getsid()` answering for a nonexistent pid; `setpgid()`
accepting a negative `pgid` and an unrelated `pid`; the four `chown`
stubs reporting success for a nonexistent path, the empty string, a
non-directory prefix and an unopened descriptor; and `gethostname()`
reporting `-1`/`ENAMETOOLONG` for a truncation `gethostname.html`
specifies as a successful completion. The UNIMPLs are `alarm()` (no
timer at all), `nice()` (ignores `incr`, and `<limits.h>` defines no
`{NZERO}`) and `setsid()`/`setpgrp()` (never enter the state their
pages describe).

That is the same shape as the never-asserted sweep's six and as the
three headers a concurrent auditor reported the same day (`newlocale`
ignoring `category_mask`, `posix_fadvise` ignoring `offset`/`len`,
`posix_fallocate` returning the wrong errno for a directory):
**unvalidated arguments in functions no caller had ever been in a
position to check.** This file's degenerate-stub table
("Permanent degenerate stubs") is where these 25 were previously
recorded, and it stays accurate about the *effects*; what group M adds
is that "the effect is unobservable" was being used to excuse
"the argument is unread", which is a separate and fixable defect.

**Two cautions on the 25, both instances of the caveat item 4 already
records.** The split is derived by tokenising the ledger's first
column, which is exact for a row naming one function and conservative
for a row describing several:

- Several rows above name four functions at once
  (`chown / fchown / lchown / fchownat`,
  `setuid / seteuid / setgid / setegid / setreuid / setregid`), so a
  tokeniser that splits on the first column will see them; but
  `pause` appears in a first column of its own only once and is
  otherwise discussed in prose, which is the undercount mechanism
  `hdestroy`/`wordfree` already demonstrated.
- Every first-column identifier in group M was checked against every
  first-column identifier earlier in the ledger. **The intersection is
  not empty**: `test/POSIX-COVERAGE.md`'s priority-6 section
  ("unistd.h, fcntl.h, sys/stat.h") already has first-column rows for
  `fchown`, `fchownat`, `lchown`, `setregid`, `setpgrp`, `setsid` and
  `pause` — added by the never-asserted sweep, recording that sweep's
  *first assertion* rather than an audit of those pages, where group
  M's rows record the pages' clause lists. Both rows are true and a
  tokeniser counts each name twice. Do not reconcile them by editing
  either. This is failure mode 2 of "The counting method is what item 4
  has to replace" at the end of this file, which is where the general
  conclusion is drawn for all four audits; the arithmetic above
  (43 -> 18) is in *pages audited*, per the convention that section
  sets out. `test/POSIX-COVERAGE.md`'s group Q section lists the same
  overlap for groups N, O and Q.

## Changes since the clause audit of the `unistd.h` row (group N)

Dated note, 2026-08-24, same route and same reason as group M's note
above: appended, not folded into the frozen `04edec2` headline table.

`test/POSIX-COVERAGE.md`'s new section **"unistd.h: the `*at()` link
calls (successor-queue item 2, group N)"** clause-audits **3** more of
the `unistd.h` row's 43: `linkat readlinkat symlinkat`. That row drops
from 18 (after group M) to 15.

`symlinkat` is the one of the three with no prior assertion of any
kind — `test/posix-glob.c` calls it to build a fixture and checks
nothing about it.

**Two bugs, both in `linkat()`**: a directory `path1` reports `EISDIR`,
which `link.html`'s ERRORS list does not contain (it requires
`[EPERM]`); and `flags` is discarded outright (`src/unistd/link.c:27`
is `(void)flags;`), so `AT_SYMLINK_FOLLOW` silently does nothing.

**One N/A in this file is superseded.** The "Closed: the remaining
`unistd.h` 18" section above records `linkat()`'s `AT_SYMLINK_FOLLOW`
as N/A on the grounds that distinguishing the two branches needs a
symbolic link and therefore `SeCreateSymbolicLinkPrivilege`. That is
true of the CI images and is why group N's fence sits behind a
privilege probe — but the defect is readable in the source without
running anything, so the clause is *unreached in some environments*,
not *inapplicable*. Group N reclassifies it as a fenced BUG. The
earlier text is left standing rather than edited, per this file's
frozen-snapshot rule; this note is the correction.

`symlinkat()`'s creation clauses are the first thing in this suite to
use the **rc=77 "unverified"** route for a *privilege* rather than a
network: without `SeCreateSymbolicLinkPrivilege` the dependent groups
print a `SKIP` naming the mechanism and the run exits 77 rather than
reporting a pass.

## Changes since the clause audit of the `unistd.h` row (groups O and Q)

Dated note, 2026-08-24, same route and reason as the group M and N
notes above.

**Group O** — `test/POSIX-COVERAGE.md`'s "unistd.h: the exec family's
ERRORS" section — clause-audits **7** more of the `unistd.h` row's 43:
`execl execle execlp execv execve execvp fexecve`.

**Group Q** — "unistd.h: the seven already-audited names" — is
bookkeeping rather than new work, and deliberately adds **no rows**.
`confstr getlogin getlogin_r swab sync tcgetpgrp tcsetpgrp` were
already audited clause by clause by the never-asserted sweep recorded
further up this file, with each page cited in `test/posix-unistd.c`
*and* first-column rows added to `test/POSIX-COVERAGE.md`'s priority-6
section. Restating them as rows of their own would double-count every
one. What is stale is **this file**: its `unistd.h` row of 43 is a
mechanical snapshot of `04edec2` and predates those rows. Group Q
names the seven so the correction can be checked; the `confstr()`
`[EINVAL]` and `tcgetpgrp()`/`tcsetpgrp()` `[EBADF]` fences are the
sweep's own.

Together with groups M (25) and N (3), that is **42 of the row's 43**;
`fork` is the remainder and is group P. The `unistd.h` row of the
frozen "Implemented, not clause-audited (357)" table therefore drops
43 -> 1 -> 0 once group P lands, and 42 (then 43) move to
*implemented + clause-audited*.

**Two bugs and one UNIMPL in group O**, all in
`src/process/exec.c`/`find_program.c`, none fixed:

- `execvp("")`/`execlp("")` report `[EBADF]` where `exec.html` requires
  `[ENOENT]` for an empty `file`. `__find_program("", 1)` runs the PATH
  search with an empty name and `try_dir()` accepts `<PATH entry>\`
  because `access(dir, X_OK)` succeeds on a directory — so `execvp("")`
  resolves to the first PATH directory and tries to execute it.
- executing a directory reports `[EBADF]`, an errno `exec.html` allows
  only `fexecve()` to produce and only about its descriptor; the page
  requires `[EACCES]` for a non-regular process image file.
- **UNIMPL:** `execvp()`/`execlp()` do not fall back to a command
  interpreter for a file that would otherwise be `[ENOEXEC]`, which
  `exec.html` DESCRIPTION requires and which is why the `[ENOEXEC]`
  entry is scoped "except for execlp() and execvp()". Now that this
  tree has `src/sh/` and an `sh` binary, `<shell path>` exists.

**Counting caution, continued.** Group Q's seven are the sharpest case
in the file of the count and the work disagreeing: they were audited at
`0e3aefa` and are cited page-by-page in `test/posix-unistd.c`, yet were
still carried here as unaudited, purely because no first-column row
named them — and they now carry priority-6 rows that a tokeniser would
add to group M/N/O's, counting them twice in the other direction. Both
halves are failure mode 2 below; see "The counting method is what item 4
has to replace" at the end of this file. Trust these notes' prose about
*which pages have been audited* over any tokeniser's total.

## Changes since the clause audit of the `unistd.h` row (group P) — row closed

Dated note, 2026-08-24, same route and reason as the M, N and O/Q notes
above.

`test/POSIX-COVERAGE.md`'s new section **"unistd.h: fork()
(successor-queue item 2, group P)"** clause-audits the **last** of the
43 interfaces in the "Implemented, not clause-audited (357)" table's
`unistd.h` row.

**That row is now closed: 43 -> 0.** Reading the frozen table,
subtract all 43 from 357 and add them to *implemented +
clause-audited*; `unistd.h`'s "not clause-audited" count is 0, and its
"implemented + clause-audited" pointer row (34) rises by 43 to 77.

Split across the four groups: M 25, N 3, O 7, P 1 — 36 pages
clause-audited this session — plus Q's 7, which needed no work at all
because the never-asserted sweep had already audited them and already
rowed them in `test/POSIX-COVERAGE.md`'s priority-6 section. 36 + 7 =
43.

Group P's finding is a single **UNIMPL**, and it is the same gap group
M records against `alarm.html`, recorded twice on purpose: `fork.html`
requires the child's pending alarm to be cancelled, and with
`src/unistd/sleep.c:41` returning 0 unconditionally there is no alarm
to cancel and no way to observe the clause. The fork side will still
need writing when `alarm()` becomes real, because
`RtlCloneUserProcess` copies the address space and a timer in a global
would travel into the child.

**`test/posix-fork-clauses-win.c` carries the `-win` suffix and must
keep it.** Stock apt Wine has no `RtlCloneUserProcess`, so a fork does
not fail there, it hangs; `TEST_RUN = $(filter-out %-win.exe,...)` is
what keeps the Wine leg from paying a full job timeout for it.

**A Wine divergence worth knowing before trusting a local green run.**
Making `src/process/fork.c`'s `set_fd_inherit()` a no-op — so nothing
is ever marked `OBJ_INHERIT` before the clone — does not fail that file
under the patched Wine; the child's descriptors keep working. On real
NT the marking is what carries a handle into the clone, and this file's
own `src/process/fork.c` notes record what breaks without it. The
`windows-test` legs are the only authority for that step.

### Running total for the `unistd.h` row

| group | interfaces | bugs | UNIMPL |
|---|---|---|---|
| M — identity, process group, session, `alarm`/`pause`/`nice`/`gethostname` | 25 | 6 | 3 |
| N — `linkat`/`readlinkat`/`symlinkat` | 3 | 2 | 0 |
| O — the `exec` family's ERRORS | 7 | 2 | 1 |
| P — `fork` | 1 | 0 | 1 |
| Q — already audited by the never-asserted sweep; no new rows | 7 | (2 pre-existing) | 0 |
| **total** | **43** | **10** | **5** |

**Every one of the ten is a shall-fail error clause or a specified
errno value**, which is now the fourth independent confirmation of that
pattern in this codebase — the never-asserted sweep's six, a concurrent
auditor's `newlocale`/`posix_fadvise`/`posix_fallocate`/`snprintf`
finds, and these ten. "Exists and links" remains a much weaker
statement than it looks, and so, this row shows, does "has a first
assertion": all but one of these 43 already had one.

## The counting method is what item 4 has to replace

Written once, here, because **four** separate audits reached it
independently and this file should not read as four people discovering
the same thing in four places. Groups A-G, groups H-J, groups K/L and
groups M-Q were assigned disjoint headers by letter and did not collide
on any interface; they collided only on this. The conclusion is not that
any of them miscounted:

**The `impl` vs `audited` split in this file is derived by tokenising
the first column of `test/POSIX-COVERAGE.md`. That method fails in five
distinct ways, in both directions, and no amount of care by an
individual auditor fixes any of them.** Item 4 of the successor queue is
therefore "re-derive the split with a method that does not have these
failure modes", not "re-run the same pipeline and trust the answer".
Re-running the pipeline unchanged reproduces every one of them.

**The convention that follows from this, adopted from groups M-Q and
applied to all four notes: count in *pages audited*, not in what a
tokeniser would report.** A page is audited when its clause list has
been read against the tree and rowed; that is a fact about the work and
does not change when a name happens to carry two rows or none. The
tokeniser's answer is a proxy for it, and the five failures below are
all places where the proxy and the fact disagree. Where a note states
both — group M's `unistd.h` row going 43 -> 18, groups K/L's 22 of 28 —
the pages-audited number is the one that is true.

**The four audits' movement, in pages audited, collected here so the
total is in one place.** None of it is folded into the frozen headline
counts; this is what item 4 has to fold in.

| batch | pages audited | effect on the "Implemented, not clause-audited (357)" table |
|---|---|---|
| groups A-G | 60 | eight headers' rows cleared (`termios.h` 11, `search.h` 11, `fenv.h` 11, `pwd.h` 7, `grp.h` 7, `regex.h` 4, `dlfcn.h` 4, `glob.h`/`fnmatch.h`/`wordexp.h` 5) |
| groups H-J | 46 | `ctype.h` 12, `wctype.h` 16, `locale.h` 4, `stropts.h` 1 (reclassified to *absent*, not subtracted), and J3's long tail of 14 |
| groups K/L | 22 (of 28 named) | `stdio.h` 16 and `stdarg.h` 12, less the six already carrying priority-5 rows at `04edec2` |
| groups M-Q | 43 | the `unistd.h` row closed outright, 43 -> 0: M 25, N 3, O 7, P 1 newly audited, plus Q's 7 the never-asserted sweep had already audited and already rowed |
| group R | 8 | `sys/stat.h` 6 -> 5 (`fchmodat`); the other seven — `puts scanf renameat sigwait psignal roundl strxfrm_l` — are in **no** row of that table, because it enumerates headers the priority order never reached and these sit under headers it did. They are movement the 357 cannot show: see failure mode 3 |

A tokeniser run over the same sections will not reproduce these
numbers, and that is the point: it double-counts the six of K/L, the
21 priority-6 names M/N/O/Q re-row, and `wctype`/`wctrans`; it misses
`hdestroy`, `wordfree`, `pause` and the `drand48` family; it has no
answer for `strlen`/`strnlen`; and it puts `ioctl` in the wrong table.
Group R lands after the other four and reports no new *direction* of
error — its eight pages are ordinary movement — but it is the batch that
supplies the sharpest evidence for two of the five modes below, because
`tools/lint-unreferenced.sh` gives this file, for the first time, a
mechanical answer to "does any test even call this name?" that is
independent of what the ledger says about it.

The evidence, each case recorded in the note that found it:

1. **Undercount — audited, but never in a first column.** `hdestroy`
   and `wordfree` are audited by `test/posix-glob.c`, which calls both
   and cites their pages, but they are named only in surrounding prose
   and in a sibling row's clause column. Step 3 sees 58 names where
   groups A-G audited 60. The `drand48`/`erand48`/`lrand48`/`mrand48`/
   `seed48`/`lcong48` family is the same shape from group J3: audited
   under priority 2, appearing in J3 only in a clause column. So is
   `pause` in group M, which carries one first-column row and is
   otherwise discussed in prose. (Groups A-G note; groups H-J note,
   third bullet of the J3 cautions; group M note, first caution.)
2. **Overcount — audited once, rowed twice.** `tempnam`,
   `getc_unlocked`, `vprintf`, `vscanf`, `va_arg` and `va_copy` already
   had clause-cited priority-5 rows at `04edec2`, and the frozen
   `stdio.h` (16) / `stdarg.h` (12) rows were never updated to match, so
   groups K/L move 22 pages, not 28. Groups M-Q hit the same shape at
   larger scale and from the other direction in time: the never-asserted
   sweep had already given priority-6 first-column rows to `fchown
   fchownat lchown setregid setpgrp setsid pause linkat readlink
   readlinkat execl execle execlp fexecve confstr swab sync getlogin
   getlogin_r tcgetpgrp tcsetpgrp`, recording that sweep's *first
   assertion*; groups M, N, O and Q row the same names again for their
   *pages' clause lists*. Both rows are true and neither supersedes the
   other, and a tokeniser counts every one of those names twice. Group I
   is a third instance: `wctype` and `wctrans` carry first-column rows
   there while the "Audited" table already credits them from priority 8.
   (Groups K/L note; group M note, second caution, and
   `test/POSIX-COVERAGE.md`'s group Q section, which states the overlap
   for M, N, O and Q in one place rather than four; groups H-J note,
   group I cautions.)
3. **Neither in nor out — covered by a group audit with no rows.**
   `strlen` and `strnlen` sit in the 357's `string.h` row, but
   priority 1 audited that header as a group and names neither in a
   first column, so the tokeniser has never counted them on either
   side. Group J3 gives them rows, which the pipeline *will* count;
   whether that is a new count or a double count depends on how item 4
   reads priority 1. Flagged, deliberately not decided here. (Groups
   H-J note, first bullet of the J3 cautions.) **Group R is the
   measured version of this mode**, and it is worse than `strlen`
   suggested. Five of group R's eight names were already inside a
   *grouped* first column and were therefore credited as covered:
   `fgets / fputs / puts`, `scanf family`, `rename / renameat` and
   `strxfrm / strxfrm_l`. `tools/lint-unreferenced.sh` then showed that
   **no test carried a relocation against any of the five** — the
   grouped rows are true about their other members and say nothing
   whatever about `puts`, `scanf`, `renameat` or `strxfrm_l`. A
   tokeniser cannot see this in either direction: split on `/` it
   over-credits four names it has no evidence for, and left whole it
   counts a row that is neither one interface nor the 357's. Group R
   gives all eight their own rows and their first assertions; the eight
   pages are new audit, not a re-row of anything, and only `fchmodat`
   is a subtraction from the 357.
4. **Wrong table entirely — a row that should not exist.** `ioctl`
   belongs in the absent accounting, not in "implemented, not
   clause-audited": POSIX's is `<stropts.h>`'s `int ioctl(int, int,
   ...)` over STREAMS devices, ntlibc's is `<sys/ioctl.h>`'s `int
   ioctl(int, unsigned long, ...)` over NT handles — two functions
   sharing a name. This is a move *between* tables rather than a
   subtraction from 357, and the mechanical pipeline cannot infer it,
   because it derives "implemented" from what links and `ioctl` does
   link: the name is real, the interface is not. Item 4 has to
   special-case it. (Groups H-J note, group J2.)
5. **A row made false by a later commit rather than by a later audit,
   with nothing to re-check it.** Merging these audits surfaced three
   in `test/POSIX-COVERAGE.md`, all flagged in place there and none
   hand-deleted. Priority 8's `<wctype.h>` row says the header "does not
   exist in this library at all" (N/A) — `include/wctype.h` landed
   2026-08-23 and group I audits all sixteen functions as covered.
   Priority 4's `_longjmp` row calls "shall not manipulate the signal
   mask" vacuous for want of any mask machinery — group J3 asserts it
   as covered against the real `blocked` set in `src/signal/signal.c`.
   Priority 6's `linkat` row calls `AT_SYMLINK_FOLLOW` N/A because
   telling the flag's two branches apart needs a symbolic link and so
   `SeCreateSymbolicLinkPrivilege` — group N reclassifies it a **BUG**,
   correctly: `src/unistd/link.c:27` is `(void)flags;`, which is
   readable without running anything, so the clause is *unreached in
   some environments*, not *inapplicable*. In all three cases the
   *tests* agree and all pass; it is the rows' prose that contradicts,
   and a tokeniser reading them counts one interface as both N/A and
   audited. (Group N's note states its own; the other two are noted at
   the rows.) **Group R adds a fourth, and it is the cleanest instance
   yet**: priority 7's `psignal / psiginfo` row reads "not implemented
   anywhere in `src/`/`include/`" and is marked N/A on that ground.
   `src/signal/signal.c:526` defines `psignal()` and `include/signal.h`
   declares it; group R audits the page and rows it. Unlike the other
   three this one was not found by reading two audits against each
   other — `tools/lint-unreferenced.sh` reported `psignal` in the set of
   *implemented* functions no test references, which is a statement the
   row asserts is impossible. That is the shape item 4 wants: a check
   that contradicts a stale row mechanically instead of waiting for the
   next auditor to read it.

Cases 1 and 2 are the two directions of the same defect: first-column
tokenisation cannot tell "audited twice" from "audited once", and cannot
see an audit a row describes in prose. Cases 3, 4 and 5 are not counting
errors at all — they are questions the method has no way to ask: what a
group audit covers, whether a linkable symbol is the interface POSIX
specifies, and whether a row is still true.

None of the five has been folded into the headline counts. Those numbers
are a mechanically-derived snapshot of `04edec2` and are deliberately
not hand-edited; each note above records its movement in prose for item
4 to fold in, and every one of them respects that rule.
