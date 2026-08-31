<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# ebue (Easy Bootstrap Unix Environment)

A from-scratch C standard library for Windows NT that talks to the system
directly through ntdll, rather than kernel32 or any other DLL layered on
top of it.

[![CI](https://github.com/Pandapip1/ntlibc/actions/workflows/ci.yml/badge.svg)](https://github.com/Pandapip1/ntlibc/actions/workflows/ci.yml)

## Supported Windows versions

<!-- ntlibc-min-ntdll: 6.1 -->
**Windows 7 / Server 2008 R2 (NTDLL 6.1) or newer, both x86_64 and i386.**

This is a floor, not a preference. ntlibc imports from `ntdll.dll` and
nothing else, and a static import of a name the running ntdll does not
export is not a call that fails at runtime — the loader refuses the whole
image before any of its code runs. So the minimum supported version is
the maximum, over every import, of the version that first exported it.

Today two imports set it, both from `src/internal/utf.c`, the UTF-8 ↔
UTF-16 conversion that every path-taking and string-taking function goes
through:

| Import | First exported by NTDLL | Reference |
| --- | --- | --- |
| `RtlUTF8ToUnicodeN` | 6.1 (Windows 7) | [names61.htm](https://www.geoffchappell.com/studies/windows/win32/ntdll/history/names61.htm) |
| `RtlUnicodeToUTF8N` | 6.1 (Windows 7) | [names61.htm](https://www.geoffchappell.com/studies/windows/win32/ntdll/history/names61.htm) |

Because `utf.c` is reachable from nearly everything, this is not a floor
that only some programs hit: 71 of the 72 PEs this tree builds import
those two names directly.

Below Windows 7 the next floors down are Vista (`RtlCloneUserProcess`,
`RtlCreateProcessParametersEx` — `fork`/`exec`), then Server 2003 SP1
(`RtlDosPathNameToNtPathName_U_WithStatus`, and on i386 the `NtWow64*`
pair), then XP. Replacing the two UTF-8 conversions with in-tree code
would therefore lower the floor to Vista and no further — one release of
an OS that is itself long out of support — which is why the library
carries the Windows 7 floor rather than a fallback.

Every export in [`tools/ntdll.def`](tools/ntdll.def) carries the NTDLL
version it was first exported from, with the sources cited in that
file's header. `tools/lint-minver.sh` (`make minver`) fails if any export
is unannotated, or if the highest annotation stops matching the version
in the marker above — so this section cannot go stale with respect to
what the library actually imports.

Note on NT *behaviour* versus NT *exports*: a few call sites do ask for
things newer than 6.1 (`FileDispositionInformationEx`,
`FileRenameInformationEx`; both Windows 10). Those are not load-time
failures — NT rejects the request and the call site falls back to the
pre-`Ex` information class — so they do not raise the floor.

See [CONTRIBUTING.md](CONTRIBUTING.md) for build/test instructions and
project conventions.
