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
 *   nodename -- read straight from the registry:
 *               HKLM\SYSTEM\CurrentControlSet\Control\ComputerName\
 *               ActiveComputerName, value "ComputerName" -- the same
 *               location GetComputerNameW() itself answers from, and
 *               the machine's real name regardless of what the calling
 *               process's own environment says (nt_registry_computername()
 *               below).  NOT src/unistd/gethostname.c's %COMPUTERNAME%
 *               lookup: that reads the *caller's* environment, which a
 *               caller controls (setenv()) and a hand-built envp can
 *               omit entirely -- precisely the defect this fence
 *               recorded ("a value read out of the caller's own
 *               environment is set by the caller, which is ... an
 *               implementation-defined ORACLE, and a wrong one").  Falls
 *               back to gethostname()'s own env-based answer only if the
 *               registry query itself fails (no such environment this
 *               tree has been run in has been found, but a Windows
 *               installation missing this standard key, or an ntdll
 *               stub that has not been taught NtOpenKey/NtQueryValueKey
 *               yet -- see fuzz/ntstubs.c -- both remain possible), so
 *               uname() degrades rather than starts failing outright.
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

/* This whole function is NT-only: HANDLE/NTSTATUS/UNICODE_STRING/
 * OBJECT_ATTRIBUTES and the Nt*Key calls below describe a real Windows
 * registry, which src/internal/nt.h declares unconditionally (so the
 * rest of that shared header still gets its layout assertions on every
 * platform that matters, see nt.h's own __linux__ section) but which
 * nothing on Linux can meaningfully open -- there is no registry to
 * query.  `L"..."` is the second reason this cannot be shared code
 * as-is even in principle: it is the COMPILER's native wchar_t (32-bit
 * on Linux/clang), not this tree's own 16-bit WCHAR (unsigned short,
 * matching the real Windows ABI), so the same literal would silently
 * build the wrong-width array on that platform even before the registry
 * question comes up.  uname() below falls back to the portable
 * gethostname()-based lookup on Linux, unconditionally -- the same
 * thing this function is itself only a fallback FROM on NT. */
#ifndef __linux__
/* HKLM\SYSTEM\CurrentControlSet\Control\ComputerName\ActiveComputerName,
 * value "ComputerName" -- the registry location this node's real name
 * lives at, independent of any process's own environment.  Returns 0
 * and fills `out` (NUL-terminated, up to outsz bytes) on success, -1 on
 * any failure (key missing, value missing, wrong type, NtOpenKey/
 * NtQueryValueKey not implemented by the ntdll underneath) -- every
 * failure is treated identically by the one caller, uname() below,
 * which falls back to gethostname()'s env-based answer rather than
 * failing outright. */
static int nt_registry_computername(char *out, size_t outsz)
{
	static const WCHAR keypath[] =
		L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\"
		L"ComputerName\\ActiveComputerName";
	static const WCHAR valuename[] = L"ComputerName";
	UNICODE_STRING key_us, value_us;
	OBJECT_ATTRIBUTES oa;
	HANDLE khandle;
	NTSTATUS st;
	/* KEY_VALUE_PARTIAL_INFORMATION's Data[1] is a placeholder for a
	 * variable-length trailer; this buffer holds the header plus up to
	 * 256 bytes of value data, generously past any real computer name
	 * (NetBIOS caps it at 15 characters; DNS-style names in this key
	 * have been measured no longer than 63). */
	unsigned char buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 256];
	PKEY_VALUE_PARTIAL_INFORMATION info = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
	ULONG result_len = 0;
	int n;

	RtlInitUnicodeString(&key_us, keypath);
	RtlInitUnicodeString(&value_us, valuename);
	InitializeObjectAttributes(&oa, &key_us, OBJ_CASE_INSENSITIVE, 0, 0);

	st = NtOpenKey(&khandle, KEY_QUERY_VALUE, &oa);
	if (!NT_SUCCESS(st)) return -1;

	st = NtQueryValueKey(khandle, &value_us, KeyValuePartialInformation,
	    info, sizeof buf, &result_len);
	NtClose(khandle);
	if (!NT_SUCCESS(st)) return -1;
	/* REG_SZ == 1: the type this value has always been measured to be
	 * (GetComputerNameW() itself expects the same); anything else is
	 * not this library's job to reinterpret. */
	if (info->Type != 1 || info->DataLength < sizeof(WCHAR)) return -1;

	n = __utf16_to_utf8_buf((const WCHAR *)info->Data,
	    info->DataLength / sizeof(WCHAR), out, outsz);
	if (n < 0) return -1;
	/* The registry value is not guaranteed NUL-terminated within
	 * DataLength (RtlInitUnicodeString-style APIs never require it);
	 * __utf16_to_utf8_buf converts exactly the code units named above
	 * and does not add a terminator of its own if the input carried a
	 * trailing NUL WCHAR already counted in DataLength -- strip it here
	 * so out is a clean C string either way. */
	if (n > 0 && out[n - 1] == '\0') n--;
	if ((size_t)n < outsz) out[n] = '\0';
	else if (outsz) out[outsz - 1] = '\0';
	return 0;
}
#endif /* !__linux__ */

int uname(struct utsname *u)
{
	RTL_OSVERSIONINFOW vi;
	int n;

	if (!u) { errno = EFAULT; return -1; }

	memset(&vi, 0, sizeof vi);
	vi.dwOSVersionInfoSize = sizeof vi;
	RtlGetVersion(&vi);   /* NTSTATUS return is documented always-success */

	strcpy(u->sysname, "Windows_NT");

#ifndef __linux__
	if (nt_registry_computername(u->nodename, sizeof u->nodename) < 0)
#endif
	{
		/* Degraded, not the primary path on NT: see
		 * nt_registry_computername()'s own banner for when this is
		 * reached there.  The only path at all on Linux, where nothing
		 * above this function even exists -- see that function's own
		 * banner for why. */
		if (gethostname(u->nodename, sizeof u->nodename) < 0)
			strcpy(u->nodename, "localhost");
	}

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
