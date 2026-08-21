# Contributing to ntlibc

## NTDLL first, kernel32 only as a last resort

ntlibc's whole reason to exist is to be a C library that talks to Windows NT
directly through ntdll's `Nt*`/`Rtl*` routines, rather than going through
kernel32 (or any other DLL layered on top of it). Two rules follow from that:

1. **Use ntdll wherever a pure-ntdll path exists.** Before reaching for a
   kernel32 API, check whether the same effect is reachable through `Nt*`,
   `Rtl*`, or an `NtDll`-exported helper. It almost always is — kernel32
   itself is usually a thin wrapper over one of these. kernel32 may only be
   used for the few things that are genuinely kernel32 (or csrss/conhost)
   territory with no ntdll equivalent — e.g. registering a console
   Ctrl-C/Ctrl-Break handler, which has no `Nt*` counterpart.

2. **Every kernel32 reference must be compiled out unless explicitly
   requested.** Wrap it in `#ifdef NTLIBC_USE_KERNEL32` (see
   `src/internal/nt.h`/wherever the kernel32 prototypes for that call live)
   and provide a reasonable fallback behaviour for the `#else` path — not a
   build failure. `NTLIBC_USE_KERNEL32` is defined by `configure
   --enable-kernel32`/undefined by the default `--disable-kernel32` (see
   `configure`'s `KERNEL32` var and the `Makefile`'s `CFLAGS_ALL` rule for
   how the define is threaded through). This keeps a default build of
   ntlibc free of any kernel32 dependency at all, and makes every place
   that *isn't* ntdll-only easy to find by grepping for the macro.

When you add code that touches Windows and you're not sure whether it
belongs behind `NTLIBC_USE_KERNEL32`: if it's an `Nt*`/`Rtl*`/other-ntdll
call, it doesn't need the guard. If it's anything else (kernel32, advapi32,
etc.), it does, and it needs a real ntdll-only fallback path too.

## Other conventions

- SPDX header on every new file:
  ```c
  /* SPDX-FileCopyrightText: (C) 2026 Gavin John
   * SPDX-License-Identifier: GPL-3.0-or-later */
  ```
- Comments explain non-obvious *why* (an NT quirk, a measured behavior
  difference, a rejected alternative and why it didn't work) — not what the
  code already says by being well-named. See `src/process/spawn.c` and
  `src/process/fork.c` for the tone this codebase uses.
- Build/test loop:
  ```
  ./configure --host=x86_64-win32 CC=x86_64-win32-tcc
  make -j4
  make -j4 check   # builds test/*.c and runs them under wine
  ```
