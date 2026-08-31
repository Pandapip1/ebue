/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * uname(): every field is something NT can genuinely answer, nothing
 * invented:
 *
 *   sysname  -- the literal string "Windows_NT".  This is not a guess
 *               dressed up as an OS name: it is the exact string
 *               Windows itself puts in %OS% on every NT-family system
 *               (`cmd /c echo %OS%`), and the same string MSYS2/Cygwin
 *               report from their own uname() for the same reason --
 *               it is the name NT gives itself, not a POSIX-flavoured
 *               fabrication.
 *   nodename -- src/unistd/gethostname.c's existing %COMPUTERNAME%
 *               lookup, reused rather than duplicated (see that file's
 *               own comment for its fallback to "localhost" when even
 *               that is unset).
 *   release  -- "%lu.%lu" of RtlGetVersion()'s dwMajorVersion and
 *               dwMinorVersion: NT's own idea of its release number
 *               (10.0 for every Windows 10/11 build; NT has reported
 *               major.minor this way since NT 3.1), read straight from
 *               the OS, not looked up in a table this library would
 *               have to keep current by hand.
 *   version  -- "Build %lu" of dwBuildNumber, the same convention
 *               `winver`/`ver` show a user on the box itself -- the
 *               finer-grained number release.html's DESCRIPTION
 *               distinguishes version from ("further defines... release
 *               of the operating system"), sourced from the same
 *               RtlGetVersion() call as release, not synthesized
 *               separately.
 *   machine  -- a compile-time check of which arch.h this library was
 *               built with (__x86_64__ / __i386__), i.e. the same
 *               binary-compatibility class the running program was
 *               itself just compiled and linked for -- exactly what
 *               uname.html's DESCRIPTION means by "the name of the
 *               hardware type". WOW64 (a 32-bit process on a 64-bit
 *               kernel) reports "i686", not "x86_64": the running
 *               *process* is what a caller checking machine actually
 *               cares about (can it dlopen() a same-arch library?
 *               does a size_t match?), and that is 32-bit regardless
 *               of the underlying kernel's own bitness.
 *
 * A single RtlGetVersion() call, not a cached/env-derived one: it is
 * pure-NTDLL (tools/ntdll.def already exports it; src/internal/nt.h
 * already declares RTL_OSVERSIONINFOW and the prototype -- neither
 * needed adding), always succeeds per its own documentation, and reading
 * it fresh means uname() can never go stale relative to the OS it is
 * actually running under.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/utsname.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "libc.h"

int uname(struct utsname *u)
{
	RTL_OSVERSIONINFOW vi;
	int n;

	if (!u) { errno = EFAULT; return -1; }

	memset(&vi, 0, sizeof vi);
	vi.dwOSVersionInfoSize = sizeof vi;
	RtlGetVersion(&vi);   /* NTSTATUS return is documented always-success */

	strcpy(u->sysname, "Windows_NT");

	if (gethostname(u->nodename, sizeof u->nodename) < 0)
		strcpy(u->nodename, "localhost");

	n = snprintf(u->release, sizeof u->release, "%lu.%lu",
	    (unsigned long)vi.dwMajorVersion, (unsigned long)vi.dwMinorVersion);
	if (n < 0) return -1;
	if ((size_t)n >= sizeof u->release) { errno = EOVERFLOW; return -1; }
	n = snprintf(u->version, sizeof u->version, "Build %lu",
	    (unsigned long)vi.dwBuildNumber);
	if (n < 0) return -1;
	if ((size_t)n >= sizeof u->version) { errno = EOVERFLOW; return -1; }

#if defined(__x86_64__)
	strcpy(u->machine, "x86_64");
#elif defined(__i386__)
	strcpy(u->machine, "i686");
#else
	strcpy(u->machine, "unknown");
#endif

	return 0;
}

// NOLINTEND(misc-include-cleaner)
