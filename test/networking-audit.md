<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Networking design audit: BSD sockets, `AF_UNIX`, and FD passing

This is an investigation, not an implementation. It exists to answer the
questions that block starting the work, with citations to primary source
(Wine's tree at `~/Projects/wine`, Microsoft's own documentation, and
ntlibc's own code) rather than plausible-sounding guesses. Where a claim
could not be pinned to a source read during this audit, it is called out
explicitly in [What I could not establish](#what-i-could-not-establish)
rather than asserted.

No files under `src/`, `include/`, or `test/*.c` were touched to produce
this document. `test/POSIX-HEADER-INVENTORY.md`, referenced in the task
that produced this audit, does not exist in this worktree at the time of
writing (a sibling agent may be adding it elsewhere); this document lives
beside `test/POSIX-COVERAGE.md` instead, per the fallback instruction.

## 1. The transport decision

### What AFD actually is, and how thin Winsock is over it

Every Winsock socket on NT — real Windows or Wine — is a handle opened
against the device object `\Device\Afd`, operated on with
`NtDeviceIoControlFile`. This is directly visible in
`dlls/ws2_32/socket.c`'s `WSASocketW`:

```c
UNICODE_STRING string = RTL_CONSTANT_STRING(L"\\Device\\Afd");
...
NtOpenFile(&handle, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &attr, &io, 0, ...);
...
NtDeviceIoControlFile(handle, NULL, NULL, NULL, &io, IOCTL_AFD_WINE_CREATE, &create_params, ...);
```

and essentially every socket operation in that file — `bind`, `listen`,
`accept`, `connect`, `recv`/`send`, `getsockname`, `select`/`WSAPoll`,
`WSAEventSelect`, `getsockopt`/`setsockopt` — goes through
`NtDeviceIoControlFile` on that same handle with an `IOCTL_AFD_*` code
(`dlls/ws2_32/socket.c:860,910,981,1032,1120,1187,1323,1390,1452,1653,
1685,1711,1725,2446,2873,3023,3177,3306,3793,3824,3897,3977,4135,4337`).
Winsock is, in that sense, a thin layer: the wire protocol to the kernel
*is* AFD, and `ws2_32.dll` is mostly argument marshalling plus
`NTSTATUS`→`WSA*` error translation (`NtStatusToWSAError`,
`dlls/ws2_32/socket.c:593`, a `static` — i.e. not exported — function)
and buffering for the higher-level convenience functions (`gethostbyname`
and friends, which really do need a resolver stack `ws2_32` links in).

**But "AFD" is not one fixed, documented wire protocol you can drive from
scratch.** `include/wine/afd.h` defines the ioctl codes Wine's own
`ws2_32`/`ntdll` use. Some of these carry codes built from
`CTL_CODE(FILE_DEVICE_BEEP, ...)` — `IOCTL_AFD_BIND` (0x800),
`IOCTL_AFD_LISTEN` (0x802), `IOCTL_AFD_RECV` (0x805), `IOCTL_AFD_POLL`
(0x809), `IOCTL_AFD_GETSOCKNAME` (0x80b), `IOCTL_AFD_EVENT_SELECT`
(0x821), `IOCTL_AFD_GET_EVENTS` (0x822) — which is the known disguise
real Windows AFD uses (`FILE_DEVICE_BEEP` is not a beep driver here; this
is long-established reverse-engineering, and Wine's choice to reuse the
exact same numbers is deliberate, matching real Windows for those
operations). But `include/wine/afd.h:187-190` also defines a second,
non-overlapping range under `FILE_DEVICE_NETWORK`:

```c
#define WINE_AFD_IOC(x) CTL_CODE(FILE_DEVICE_NETWORK, x, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AFD_WINE_CREATE   WINE_AFD_IOC(200)
#define IOCTL_AFD_WINE_ACCEPT   WINE_AFD_IOC(201)
```

and further Wine-only codes exist for connect, message-select, deferred
completion, and async completion
(`IOCTL_AFD_WINE_CONNECT`, `IOCTL_AFD_WINE_MESSAGE_SELECT`,
`IOCTL_AFD_WINE_DEFER`, `IOCTL_AFD_WINE_COMPLETE_ASYNC`, used at
`dlls/ws2_32/socket.c:580,860,1390,3977,4337`). Socket *creation* itself —
the step that on real Windows is done via `NtCreateFile` with an
`EaBuffer` naming a transport-provider GUID (documented, at a shallow
level, only by third-party reverse engineering, not by Microsoft) — is
done in Wine via the invented `IOCTL_AFD_WINE_CREATE` after a plain
`NtOpenFile`. Wine's own socket implementation is not a passthrough to a
real `AFD.sys`; it is Wine's userspace reimplementation, backed 1:1 by
real Unix sockets in `server/sock.c`, that reuses real AFD's numbering
for the parts Wine's authors reverse-engineered and matched, but invents
its own numbering for everything else.

That has a direct, concrete consequence for this project: **the only
place ntlibc can test against is Wine, and Wine's AFD is provably not a
faithful clone of real Windows' AFD for at least socket creation and
connect.** Code written to drive `IOCTL_AFD_WINE_CREATE` directly would
pass every test that runs here and then not work — or work differently —
on real Windows, which is exactly the failure mode this audit is meant
to prevent. Code that used the real, non-`WINE_`-prefixed codes (bind,
listen, recv, poll, getsockname, event-select) would be more likely to
carry over, but that is only part of the surface, the undocumented parts
(the `EaBuffer`/EA-based create/connect path) can't be validated at all
without a real Windows box, and no ReactOS-independent Microsoft
documentation of the AFD wire format exists to check against — real AFD
is not a published interface.

### Recommendation: layer on `ws2_32.dll`, loaded the way kernel32 already is

ntlibc already has a precedent for "kernel32 is allowed where necessary":
`src/internal/kernel32.h`'s header comment, and `src/signal/signal.c`'s
`install_ctrl_handler()`, which reaches `SetConsoleCtrlHandler` via
`LdrLoadDll(L"kernel32.dll")` + `LdrGetProcedureAddress`, entirely
avoiding an import-library dependency, gated behind
`#ifdef NTLIBC_USE_KERNEL32` (itself driven by `configure
--enable-kernel32`/`--disable-kernel32`, `configure:27,117-118,349`, and
enforced by `make check-kernel32`, per `CONTRIBUTING.md:54-56`). The same
pattern extends cleanly to `ws2_32.dll`: `LdrLoadDll(L"ws2_32.dll")`,
`LdrGetProcedureAddress` for `WSAStartup`/`socket`/`bind`/... — no new
import library, no change to the default (sockets-disabled) build's
dependency graph, and a natural new configure flag
(`--enable-winsock`/`NTLIBC_USE_WINSOCK`, or folded into the existing
kernel32 flag since Winsock only makes sense with it) that keeps this
opt-in exactly like kernel32 is.

Trade-offs, concretely:

- **`WSAStartup`.** A one-time process-lifetime cost, callable lazily on
  first `socket()`/`connect()`/etc. call. Ordinary overhead, not an
  architectural problem.
- **Error-code translation.** Winsock's `WSAGetLastError()` values are a
  *re-encoding* of the same `NTSTATUS` space `NtStatusToWSAError`
  performs (`dlls/ws2_32/socket.c:593` — not exported, so ntlibc would
  have to keep its own `WSA*`→`errno` table either way). ntlibc already
  has `__set_errno_status()` for `NTSTATUS`→`errno`
  (`src/internal/libc.h`, used throughout `src/unistd/read.c` etc.); a
  `WSA*`→`errno` table is a comparable-sized, independent piece of work
  regardless of which transport is chosen, since Winsock never hands
  back a raw `NTSTATUS` to begin with — going the direct-AFD route
  avoids this table but then needs the *raw* `NTSTATUS`→errno mapping
  extended for AFD-specific statuses instead, which is no smaller.
- **Dependency weight.** `ws2_32.dll` pulls in more than sockets alone —
  a resolver stack for `gethostbyname`/`getaddrinfo` that a direct-AFD
  implementation would otherwise have to build from nothing (DNS
  resolution has no ntdll-level equivalent at all; it is inherently a
  kernel32-and-above facility). Given `getaddrinfo`/`netdb.h` are on any
  plausible feature list, this cost is being paid one way or another;
  `ws2_32` amortizes it instead of duplicating it.
- **How much work each choice avoids.** Direct AFD avoids the
  `LdrLoadDll` indirection and one `WSAStartup` call, at the cost of:
  reimplementing every ioctl's exact input struct layout (the real,
  non-`WINE_` ones are only known via third-party reverse engineering,
  not Microsoft docs); reimplementing the create/connect EA-blob format
  that isn't in Wine's source at all in the real-Windows shape; and
  losing the only local test environment's ability to validate the parts
  that matter most (creation, connect) because Wine's equivalents there
  are admittedly not real-AFD-shaped. `ws2_32.dll` avoids all of that by
  construction — Microsoft maintains the ioctl-to-behavior contract, not
  ntlibc.

**Recommendation: `ws2_32.dll` via `LdrLoadDll`, under a new
`NTLIBC_USE_WINSOCK` (or reuse `NTLIBC_USE_KERNEL32`) guard.** Driving
AFD directly is not "impossible," but it asks ntlibc to reverse-engineer
an undocumented, evolving kernel interface with no way to check the
result against real Windows locally, for a saving (skip one DLL load and
one `WSAStartup` call) that does not justify that risk. This keeps
ntlibc's "ntdll first" ethos intact in spirit — `ws2_32` becomes exactly
as optional and load-time-only as kernel32 already is — while accepting
that sockets are one of the "genuinely kernel32-or-above" cases
`CONTRIBUTING.md:10-13` already carves out room for.

## 2. Sockets as file descriptors

`src/internal/libc.h` already reserves a slot for this:

```c
enum {
	__FD_FILE = 1, __FD_DIR, __FD_CONSOLE, __FD_PIPE, __FD_CHAR,
	__FD_SOCKET,
	__FD_UNKNOWN
};
```

(`src/internal/libc.h:107-114`) — `__FD_SOCKET` exists in the enum today
and is not referenced anywhere else in `src/`, `include/`, or `test/`
(confirmed by grep across all three). It is a placeholder, not a
half-built feature.

`struct __fd` (`src/internal/libc.h:116-124`) is:

```c
struct __fd {
	HANDLE h;
	unsigned flags;
	unsigned char type;
	unsigned char eof;
	unsigned char dirflag;
	unsigned char pad;
	long long pos;
};
```

A Winsock `SOCKET` *is* a `HANDLE` — `WSASocketW` returns the value from
`NtOpenFile` cast to `SOCKET` (`dlls/ws2_32/socket.c:4116` region,
`ret = HANDLE2SOCKET(handle)` pattern) — so `f->h` needs no new field,
and no union: a socket's `h` is exactly as usable with `NtClose` as a
file's. `dirflag` and `pos` are meaningless for a socket and can stay
zero/`-1`, matching how they already sit unused for `__FD_PIPE`/
`__FD_CONSOLE`. `eof` (already reused for "pipe/console has reported end
of input") extends naturally to "peer sent FIN."

What must branch, concretely, citing the existing dispatch:

- **`read`/`write`** (`src/unistd/read.c:8-32`, `src/unistd/write.c:9-36`)
  call `NtReadFile`/`NtWriteFile` directly on `f->h` today. Whether that
  keeps working for a Winsock-created socket handle depends on how the
  socket was opened — `WSASocketW` requests `FILE_SYNCHRONOUS_IO_NONALERT`
  only when `WSA_FLAG_OVERLAPPED` is *not* set
  (`dlls/ws2_32/socket.c:4125-4126`), and `IOCTL_AFD_RECV`/`send`-family
  ioctls, not plain `NtReadFile`/`NtWriteFile`, are what `ws2_32`'s own
  `recv`/`send` use (`dlls/ws2_32/socket.c:1032,1120` for send/recv
  paths). This audit could not establish from source read here whether a
  bare `NtReadFile`/`NtWriteFile` against a `SOCK_STREAM` socket handle
  behaves correctly end-to-end (Microsoft's own documentation for
  `ReadFile`/`WriteFile` says stream sockets support them, but this was
  not verified against Wine's or real Windows' `AFD` handler directly in
  this audit — see [open items](#what-i-could-not-establish)). The safe,
  supportable design is for `read`/`write`/`close` to add an explicit
  `f->type == __FD_SOCKET` branch that calls `IOCTL_AFD_RECV`/
  `IOCTL_AFD_SEND` (or, more simply, calls into the loaded `ws2_32`
  `recv`/`send`/`closesocket` once that dependency is taken) rather than
  assuming the existing `NtReadFile`/`NtWriteFile` path transfers over
  unexamined.
- **`close`** (`src/unistd/close.c:8-17`) calls `NtClose(f->h)`
  unconditionally today. `ws2_32`'s `closesocket` is not just `NtClose`
  — it clears entries from an internal `socket_list`
  (`dlls/ws2_32/socket.c`, `socket_list_add`/remove pattern visible
  around the `WSADuplicateSocket` hack at `socket.c:4067-4074`) that
  tracks which handles are sockets for `ws2_32`'s own bookkeeping across
  `WSADuplicateSocket`. If ntlibc goes through `ws2_32` for socket I/O at
  all, `close()` needs a `__FD_SOCKET` branch that calls the loaded
  `closesocket()` instead of `NtClose` directly, mirroring the `type`
  dispatch already used elsewhere (e.g. `read()`'s `f->type == __FD_DIR`
  check, `write()`'s same).
- **`select`/`poll`** — see §3.
- **File-position, `dup`/`dup2`, `fcntl` `O_NONBLOCK`.** `pread`/`pwrite`
  already reject non-`__FD_FILE` types with `ESPIPE`
  (`src/unistd/read.c:44`, `src/unistd/write.c:48`); a socket falls into
  that existing rejection with zero new code needed. `dup`-family
  presumably already works on any `HANDLE` via `NtDuplicateObject`
  (`fd.c:__fd_runtime_data` already does exactly this for inheritance,
  `src/internal/fd.c:180-186`) and needs no socket-specific change.
  `O_NONBLOCK` for a socket is a `ws2_32` `ioctlsocket(FIONBIO)` call,
  not an NT handle flag — another `__FD_SOCKET` branch, this time in
  whatever `fcntl()` does today (not read as part of this audit).

Net: the fd-table shape survives untouched (`struct __fd`, `__fd_alloc`,
`__fd_install`, `__fd_get`, `__fd_runtime_data`/inheritance) because a
`SOCKET` is a `HANDLE`; what's new is a handful of `f->type ==
__FD_SOCKET` branches at the I/O call sites, matching the pattern those
call sites already use for `__FD_DIR`/`__FD_PIPE`/`__FD_CONSOLE`. Nothing
about the file/directory paths needs to change.

## 3. `select`/`poll`: the hard prerequisite

`include/sys/select.h`'s own comment (`include/sys/select.h:20-52`,
quoted in full because it is the existing design sketch this audit
extends rather than replaces) already lays out the three fd shapes
ntlibc has today and their readiness tests: files/directories (always
ready), console (a genuinely waitable NT object,
`NtWaitForMultipleObjects` works directly), and pipes (not signalled on
data arrival — needs a `NtQueryInformationFile(FilePipeLocalInformation)`
polling loop). `test/posix-sysmisc.c` fences `select`/`pselect` tests as
`UNIMPL` (`test/posix-sysmisc.c:46-54,402-540`) pending exactly this.

**Sockets add a fourth shape, and it turns out to be the *good* one for
mixing.** Winsock's own `select()`/`WSAPoll()` implementation
(`dlls/ws2_32/socket.c`, the block around `socket.c:2980-3060`) issues a
single `IOCTL_AFD_POLL` call listing every socket of interest, submitted
asynchronously with a caller-supplied event:

```c
status = NtDeviceIoControlFile((HANDLE)poll_socket, sync_event, NULL, NULL,
                                &io, IOCTL_AFD_POLL, params, params_size, params, params_size);
if (status == STATUS_PENDING)
    wait_event_alertable(sync_event);   /* = WaitForSingleObjectEx(event, INFINITE, TRUE), socket.c:717-723 */
```

`sync_event` is an ordinary NT event object — `wait_event_alertable`
bottoms out in `WaitForSingleObjectEx`, itself a thin wrapper over
`NtWaitForSingleObject`/alertable delay. That means the completion
signal for "one or more sockets in this set became ready" is *itself* a
waitable `HANDLE`, exactly like the console handle already is in
ntlibc's design. So the mixing problem — which the task rightly flags as
usually the hard part — has a concrete, sourced answer for sockets
specifically: submit one `IOCTL_AFD_POLL` asynchronously (covering all
sockets in the caller's `fd_set`s) against any one of the sockets
involved (Wine's implementation picks an arbitrary `poll_socket` from
the set to issue the ioctl against — see `socket.c:2993,3006,3016`),
collect its completion event, and add that event to the same
`NtWaitForMultipleObjects` call that already waits on the console handle
and drives the pipe-polling timeslice loop `select.h`'s comment
describes. `AFD_POLL_READ`/`WRITE`/`HUP`/`CLOSE`/`CONNECT`/`ACCEPT`
(`include/wine/afd.h:48-64,67-78`) map onto POSIX's read/write/except
bits with the same granularity `select()` needs, and `exceptfds` gets a
real (if partial) answer for sockets — `AFD_POLL_OOB` — where it
currently has none for pipes/consoles, which is *better* than the
existing three-shape design, not harder.

The cost this adds to the estimate in `select.h`'s comment (which
already sizes the file/console/pipe work at 1-2 days): one more branch
in the polling core that issues an async `IOCTL_AFD_POLL` per `select()`
call instead of a synchronous one, one conversion table between
`AFD_POLL_*` bits and the caller's three `fd_set`s, and — the same
caveat the existing comment already raises for pipes — real multi-
process test coverage (a listener and a connecting peer under Wine,
exercising actual readiness transitions), which is exactly the kind of
test `test/process-win.c`'s pattern already supports. This does not look
like a second hard problem stacked on the first; it is one more
readiness source feeding the same wait loop the existing pipe/console
design already needs. A rough estimate: 2-3 additional days on top of
the existing 1-2, mostly for the `AFD_POLL_*` conversion table and the
live two-process test, once the transport decision (§1) and the
`__FD_SOCKET` plumbing (§2) already exist to build on.

`poll()` (`include/poll.h`, which does not exist yet) is the same
problem restated: POSIX `poll()`'s `pollfd`/`revents` model maps onto
`IOCTL_AFD_POLL`'s `AFD_POLL_*` bits at least as directly as `select()`'s
`fd_set` bits do, arguably more so since both are already "list of
(handle, interest bits)" shapes. Implementing `select()` first and
`poll()` as a thin reformatting layer over the same core (as musl and
glibc both do internally) is the natural order.

## 4. Unix domain sockets: precise, sourced support matrix

**What Windows itself supports**, per Microsoft's own Windows Command
Line blog post announcing the feature (`AF_UNIX comes to Windows`,
<https://devblogs.microsoft.com/commandline/af_unix-comes-to-windows/>):

- Only **`SOCK_STREAM`**. Datagram (`SOCK_DGRAM`) and sequenced-packet
  (`SOCK_SEQPACKET`) `AF_UNIX` sockets are explicitly stated as not
  supported.
- **No ancillary data at all** — the post states in as many words: "There
  is no support for ancillary data in the Windows unix socket
  implementation," naming both `SCM_RIGHTS` (fd passing) and
  `SCM_CREDENTIALS` (peer credentials) as the Linux features this rules
  out.
- **No autobind** (automatic abstract-address generation on bind with no
  name given).
- **No `socketpair()`** equivalent exposed through Winsock.
- Available starting Windows 10 Insider Build 17063 (December 2017),
  i.e. the Windows 10 1803/"Redstone 4" timeframe the task description
  names.

That fully answers "which socket types" and "what's missing vs POSIX":
stream only, no ancillary data of any kind, no autobind, no
`socketpair()`. Address format itself (`sockaddr_un`/`SOCKADDR_UN`,
`afunix.h`) was not independently re-derived in this audit beyond
confirming the type name exists in Wine's headers (`SOCKADDR_UN` used
throughout `dlls/ws2_32/tests/sock.c:14769` onward); assume it matches
the POSIX `sun_family`/`sun_path[108]` shape since Wine's tests construct
it that way, but this specific byte layout was not checked field-by-field
against Microsoft's `afunix.h`.

**What Wine implements, distinctly:** as of this checkout
(`~/Projects/wine`, tip `ae50a1fe7`, dated 2026-08-22 — current), **Wine
does not implement `AF_UNIX` at all.** The definitive evidence is
`server/sock.c`'s `get_unix_family()` (`server/sock.c:1815-1832`):

```c
static int get_unix_family( int family )
{
    switch (family)
    {
        case WS_AF_INET: return AF_INET;
        case WS_AF_INET6: return AF_INET6;
        case WS_AF_IPX: return AF_IPX;         /* #ifdef HAS_IPX */
        case WS_AF_IRDA: return AF_IRDA;       /* #ifdef AF_IRDA */
        case WS_AF_BTH: return AF_BLUETOOTH;   /* #ifdef AF_BLUETOOTH */
        case WS_AF_UNSPEC: return AF_UNSPEC;
        default: return -1;
    }
}
```

There is no `WS_AF_UNIX` case; any socket creation request for
`AF_UNIX`/`PF_UNIX` falls to `default: return -1`, which fails socket
creation. This is corroborated by `dlls/ws2_32/socket.c`'s
`supported_protocols[]` table (the table `WSASocketW` consults when
family/type/protocol are left to be inferred): every entry's
`iAddressFamily` is `AF_INET`, `AF_INET6`, `AF_IPX`, or `AF_BTH` — no
`AF_UNIX` entry exists (`socket.c`, all `iAddressFamily = ` occurrences
grepped). And tellingly, Wine's *own* test suite already expects this:
a recent upstream commit (`41ed5dc8c`, "ws2_32/tests: Add some tests for
AF_UNIX sockets", 2025-10-30, based on a patch from MR !2786) added
`AF_UNIX` tests that explicitly handle the not-yet-implemented case:

```c
listener = socket(AF_UNIX, SOCK_STREAM, 0);
...
win_skip("AF_UNIX sockets not supported\n");
```

(`dlls/ws2_32/tests/sock.c:14913-14917`.) So Wine has *tests* for
`AF_UNIX` (presumably to make the eventual implementation easy to
verify) but not the implementation itself, and the tests are written to
skip gracefully rather than fail when it's absent — which is the state
of this checkout today.

**Precisely, per the task's distinction:**

- **Windows supports** stream-only `AF_UNIX`, no ancillary data,
  starting 1803 — established from Microsoft's own blog post.
- **Wine does not support `AF_UNIX` at all** — established from reading
  `server/sock.c`'s family-translation switch and corroborated by
  `ws2_32`'s own protocol table and Wine's own graceful-skip test code.
- **We could test: nothing, locally, today.** ntlibc's only local test
  environment is Wine (per the task's framing and `CONTRIBUTING.md`'s
  three-leg CI description), and Wine's `AF_UNIX` gap means any
  `AF_UNIX` code path ntlibc wrote could not be exercised here at all —
  only on the real-Windows CI leg `CONTRIBUTING.md` describes
  (`process-win.c`'s pattern, real-Windows-only). This is a materially
  different, and worse, situation than TCP/IP sockets over `AF_INET`,
  which Wine supports fully and which this audit did not find any
  Wine-side gap for.

This was not independently re-verified by building and running Wine in
this session — no built Wine binary was available in this worktree/
environment (see [open items](#what-i-could-not-establish)) — but the
finding rests on reading the exact dispatch code (`get_unix_family`'s
switch statement) that a live run would hit, which is as close to
"ran it" as source-reading gets for a negative result (there is no
`WS_AF_UNIX` case to reach, regardless of what value is passed in).

## 5. FD passing

**Verified, not assumed: NT's `AF_UNIX` does not support `SCM_RIGHTS`.**
Per the same Microsoft blog post cited in §4: "There is no support for
ancillary data in the Windows unix socket implementation" — this is
stated as a flat, current limitation, not a version caveat, and covers
`SCM_RIGHTS` by name. So the premise in the task ("I believe it does
not") is confirmed from Microsoft's own documentation, not inferred.

### What a faithful emulation would need

The task's own sketch is correct and matches how `DuplicateHandle`
actually works: `DuplicateHandle(hSourceProcess, hSource, hTargetProcess,
&hTarget, ...)` needs a process handle to *both* ends, not just the
handle being duplicated, and must be called by a party (or issue a
request to a party) that holds both — typically a duplicate-into-target
or duplicate-out-of-source split across the two processes, or a trusted
third party (a broker) holding both. Concretely, over an `AF_UNIX`
`SOCK_STREAM` connection between processes A and B, faithfully emulating
`sendmsg(..., SCM_RIGHTS)`/`recvmsg()` requires:

1. A's message carries, out-of-band (there being no ancillary-data
   channel to carry it in-band), the tuple **(A's PID, the `HANDLE`
   value in A's handle table)** — the same information
   `WSADuplicateSocket`'s `PROTOCOL_INFO` blob already carries for the
   one case Windows *does* support natively (duplicating a *socket*
   across processes, `dlls/ws2_32/socket.c`'s `dwServiceFlags3`/`4`
   hack at `socket.c:4067-4074` stashes exactly a duplicated handle
   value this way).
2. B must be able to open a process handle to A
   (`NtOpenProcess(&h, PROCESS_DUP_HANDLE, &attr, &clientId)` — both
   `NtOpenProcess` and `NtDuplicateObject` are already declared in
   `src/internal/nt.h:1040,1042`, so the ntdll-level primitives ntlibc
   would need already exist in its declared surface, even though
   `NtDuplicateObject` is only used same-process today, in
   `src/internal/fd.c`'s `__fd_runtime_data()` for `fork`/`exec`
   inheritance).
3. B then calls `NtDuplicateObject(hA, sourceHandleValue, NtCurrentProcess(), &hLocal, ...)`
   to pull the handle into its own table — this requires
   `PROCESS_DUP_HANDLE` access to A, which A's security descriptor must
   grant B (same-user, same-desktop processes typically have this by
   default; sandboxed or privilege-separated peers would not).

### How Cygwin approaches this

Established via source (`fhandler_socket_unix.cc`, the current
implementation backing Cygwin's `AF_UNIX`/`PF_UNIX` sockets): it
implements `SCM_CREDENTIALS` (peer-credential passing, `struct ucred`)
over its ancillary-data path, but grepping that file for `SCM_RIGHTS` or
any `DuplicateHandle`-based fd-passing logic in `sendmsg`/`recvmsg` found
none. Cygwin's own mailing list ("Duplicating Unix Domain Sockets")
discusses the `DuplicateHandle`-needs-both-process-handles problem in
the same terms as the task's sketch, as a known-hard, apparently
unsolved-in-Cygwin problem. So: **Cygwin, a far more mature POSIX layer
than ntlibc, has not shipped `SCM_RIGHTS` emulation either.** This is not
a case of "everyone else solved this and ntlibc just needs to catch up"
— it is closer to "no mainstream POSIX-on-Windows layer has shipped a
faithful answer," which should calibrate how much effort this deserves
relative to the rest of the roadmap.

### Security and lifetime consequences, honestly

- **Which process needs which rights.** The receiver needs
  `PROCESS_DUP_HANDLE` on the sender (or vice versa, depending on which
  side initiates the duplicate) — a right that, by default, a process
  has over children/same-token peers but not over arbitrary other users'
  processes. A faithful general-purpose implementation therefore either
  requires peers to already trust each other at the OS level (parent/
  child, same job object) — which overlaps heavily with what ntlibc's
  existing `fork()`/`children.c` already sets up — or needs elevated
  privilege on one side, which is a meaningfully different security
  posture than POSIX `SCM_RIGHTS`, where the kernel itself brokers the
  transfer and neither peer needs any special right over the other.
- **What happens if the peer dies mid-exchange.** If A sends the
  (PID, handle-value) tuple and dies before B calls
  `NtDuplicateObject`, B's `NtOpenProcess`/`NtDuplicateObject` calls fail
  cleanly (`STATUS_INVALID_CID`/similar) — no corruption, just a lost
  transfer, which is at least well-behaved. But there is a real window
  where the handle value A sent is momentarily valid, then invalid, with
  no atomicity guarantee tying "the message arrived" to "the handle is
  still duplicable" — POSIX `SCM_RIGHTS` has no such window because the
  kernel keeps the file object referenced (via the queued `SCM_RIGHTS`
  cmsg) independent of the sender's own fd until the receiver picks it
  up or the socket is destroyed. An NT emulation built on
  `DuplicateHandle` cannot replicate that atomicity without a broker
  process holding an extra reference for the lifetime of the in-flight
  message — which is exactly the design `fork()`'s child-handle-table
  approach already resembles (`src/process/children.c`'s header comment:
  "The handle is the only thing that keeps a child reapable").
- **Is the result genuinely `SCM_RIGHTS`-compatible, or merely similar?**
  Merely similar, and should be documented as such rather than presented
  as a drop-in. It can deliver the same practical effect (peer B ends up
  with a working, independent duplicate of a resource A had open) under
  the constrained case where both peers trust each other enough to grant
  `PROCESS_DUP_HANDLE`, but it cannot match POSIX's atomicity guarantee,
  cannot be done between mutually-untrusting processes the way a kernel-
  brokered transfer can, and depends on an out-of-band protocol
  (PID + handle value riding in the payload, not in ancillary data) that
  every peer would need to speak — meaning this only works between two
  ntlibc (or ntlibc-aware) processes, not as interop with an arbitrary
  Winsock `AF_UNIX` peer that doesn't know the convention. That last
  point matters: it is not "sockets gain `SCM_RIGHTS`," it is "two
  ntlibc processes that agree on a private protocol can approximate it."

### `fork()`'s child-handle table as an alternative mechanism

ntlibc's `RtlCloneUserProcess`-based `fork()` (working under the locally
patched Wine described in the project's own notes) plus
`src/process/children.c`'s real child-handle table changes the calculus
for the *specific* case of parent/child fd sharing: a parent that wants
a child to inherit a socket doesn't need `SCM_RIGHTS` emulation at all —
the existing `__fd_runtime_data()`/`OBJ_INHERIT` machinery
(`src/internal/fd.c:164-190`) already carries arbitrary inheritable
handles, sockets included once `__FD_SOCKET` exists, across `fork`/
`exec` exactly the way it does today for files, pipes, and consoles.
`SCM_RIGHTS`-over-`AF_UNIX` specifically matters for the case that
inheritance can't cover: passing a descriptor to an *already-running*,
unrelated process (the classic Unix pattern of a privileged daemon
handing off an accepted connection to a worker pool). That case is where
the `DuplicateHandle`-based approach above is actually needed; where the
relationship is parent/child, inheritance is simpler, already
architecturally present, and needs no new protocol.

## 6. Staged plan

Each stage lands something independently testable. Headers are listed
per stage; sizes are rough, assuming one engineer familiar with this
codebase, following the transport decision in §1.

**Stage 0 — `NTLIBC_USE_WINSOCK` plumbing.** `LdrLoadDll`/
`LdrGetProcedureAddress` resolution of `ws2_32.dll`'s entry points,
mirroring `src/signal/signal.c`'s `install_ctrl_handler()` pattern; a
new `configure` flag; `WSAStartup`/`WSACleanup` lifecycle. No new
headers yet — this is infrastructure only, testable by confirming the
DLL loads and `WSAStartup` succeeds under Wine. Half a day.

**Stage 1 — TCP/IP client sockets, `__FD_SOCKET` wired into
read/write/close.** `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`
(address structs, `htons`/`ntohs`/`inet_addr`-class functions —
byte-order conversion needs no OS help at all and can be pure C
independent of everything else here). `socket()`, `connect()`,
`send()`/`recv()` (or routed through `read()`/`write()` per §2's
branches), `close()`. This is the stage with the highest payoff: it's
the minimum for "ntlibc programs can talk to the network at all," it
exercises the `ws2_32` loading and the fd-table integration end-to-end,
and — unlike `AF_UNIX` — Wine fully supports `AF_INET`/`AF_INET6`
`SOCK_STREAM`, so it is fully testable here. 3-5 days.

**Stage 2 — Server sockets.** `bind()`, `listen()`, `accept()`. Same
headers, no new ones. Needs `accept()` to install a *new* fd for the
accepted connection (`__fd_install`, already generic) — small addition
once Stage 1 exists. 1-2 days.

**Stage 3 — `select()`/`poll()`.** `<poll.h>` (new), extending
`<sys/select.h>`'s existing declaration. This is a hard prerequisite for
*using* sockets in anything but a trivial blocking client/server (as the
task states), but is technically independent of Stages 1-2's existence —
it could in principle land in parallel once a `__FD_SOCKET` and a pipe
both exist to test the mixing case against. Per §3: 3-5 days total
(1-2 for the existing file/console/pipe core the `select.h` comment
already scopes, 2-3 more for the `AFD_POLL_*` integration and live
two-process tests).

**Stage 4 — `<netdb.h>` (`getaddrinfo`/`gethostbyname`).** Pulled in
"for free" by taking the `ws2_32` dependency in Stage 0 (§1), but the
POSIX-shaped wrapper (`getaddrinfo`, `freeaddrinfo`, `struct addrinfo`)
is still new surface. 2-3 days, mostly plumbing and error-code mapping
(`EAI_*` from `WSA*`).

**Stage 5 — `UDP`/`SOCK_DGRAM`.** No new headers beyond what Stage 1
already added (`sendto`/`recvfrom` live in `<sys/socket.h>`). Small
addition once Stage 1's plumbing exists — datagram sockets don't need
`connect()`/`accept()`, just `sendto`/`recvfrom` on top of the same
`__FD_SOCKET` machinery. 1-2 days.

**Stage 6 — `<sys/un.h>`, `AF_UNIX`.** Per §4, only stream sockets, no
ancillary data, and — critically — **untestable in this project's own
CI/dev environment today**, since Wine does not implement it. This
should either wait for Wine to land `AF_UNIX` support (the test
groundwork already exists upstream, per §4's citation of commit
`41ed5dc8c`, suggesting an implementation may follow) or be developed
against the real-Windows CI leg only, with a correspondingly higher risk
of undetected regressions between CI runs. 2-3 days of code, but
indeterminate calendar time given the test-environment gap — this is a
good candidate to defer until Wine catches up, rather than carry
unverifiable code.

**Stage 7 (optional, indefinitely deferrable) — `SCM_RIGHTS` emulation.**
Per §5: no mainstream prior art has shipped this, it depends on Stage 6
existing first, it needs a private wire protocol only ntlibc-aware peers
can speak, and its security/atomicity properties are strictly weaker
than real `SCM_RIGHTS`. Given `fork()`'s inheritance path already covers
the parent/child case (§5's last paragraph), this stage only matters for
unrelated-process handoff, which is a narrower need than the task's
framing might suggest. Recommend documenting it as explicitly
out-of-scope until a concrete consumer needs it, rather than building it
speculatively.

**Highest-value-first summary:** Stage 1 (TCP/IP client sockets) is the
one that turns "ntlibc has no networking" into "ntlibc can talk to the
network," is fully testable locally, and every later stage builds on its
`__FD_SOCKET`/`ws2_32`-loading groundwork. Stage 6/7 (`AF_UNIX`, FD
passing) are the parts explicitly worth deferring — not because they're
unimportant, but because §4 and §5 both show real, sourced reasons they
can't be built with the same confidence the rest of this plan can.

## What I could not establish

Listed explicitly, per this project's standing rule against
speculation, rather than folded into the analysis above as if settled:

- **Whether `NtReadFile`/`NtWriteFile` work correctly on a Winsock-opened
  `SOCK_STREAM` socket handle end-to-end.** Microsoft's own
  `ReadFile`/`WriteFile` documentation states stream sockets support
  them, and `ws2_32.dll`'s own `send`/`recv` go through
  `IOCTL_AFD_SEND`/`RECV`, not plain `NtReadFile`/`NtWriteFile` — this
  audit did not trace far enough to establish whether that's a Winsock
  implementation choice (both would work) or a functional requirement
  (only the ioctls work correctly, e.g. for framing or short-read
  semantics). §2 recommends the conservative choice (ioctl-based
  read/write for sockets) specifically because this wasn't settled.
- **The exact byte layout Windows expects for `SOCKADDR_UN`/`sun_path`
  length limits**, beyond confirming the type exists in Wine's test
  code. Not independently checked against Microsoft's `afunix.h`.
- **Whether the real (non-Wine-specific) `AFD` ioctl codes this audit
  found in `include/wine/afd.h` (bind, listen, recv, poll, getsockname,
  event-select, get-events) are accurate for *current* Windows** versus
  some earlier version Wine's authors reverse-engineered against — AFD
  is not a documented, versioned interface, and this audit has no way to
  check for drift across Windows releases.
- **No empirical (built-and-run) test was performed in this session.**
  No pre-built Wine binary was present in this worktree/environment
  (the project's own notes describe a locally patched Wine build used
  for `fork()` testing, at `~/Projects/wine/build-wow64/`, but that
  directory did not exist when checked during this audit), and building
  one from source was judged out of scope for a design audit at this
  effort level. Every AF_UNIX/AFD claim above rests on reading Wine's
  and Microsoft's source/documentation directly, not on running code.
  The clearest place a live test would add value beyond what source-
  reading already established is confirming empirically that `socket
  (AF_UNIX, SOCK_STREAM, 0)` fails under this project's actual test
  Wine build the same way the source predicts — this is very likely
  given the switch-statement evidence in §4, but "very likely" is not
  "verified by running it," and should be flagged as such.
- **`fcntl()`'s current implementation** (for `O_NONBLOCK` handling, §2's
  last bullet) was not read as part of this audit; the claim that it
  needs a new `__FD_SOCKET` branch is inferred from the same dispatch
  pattern `read`/`write`/`close` already use, not confirmed against that
  file's actual contents.
