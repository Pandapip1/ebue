/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Program startup.
 *
 * The kernel enters a new process's first thread at ntdll's
 * RtlUserThreadStart, which reaches the image's entry point -- this
 * file's _start.  tcc's PE linker names _start as the entry when linking
 * with -nostdlib, so nothing has to be said on the command line.
 *
 * _start declares two parameters, and uses neither.  They are captured
 * into __entry_arg0/__entry_arg1 purely so test/entry-arg.c can report
 * what the OS actually handed the entry point; the PEB itself comes from
 * the TEB.  See __libc_start_main below for why -- a Windows-subsystem
 * image's entry point is not reliably passed anything.
 *
 * What a C program expects to have been done by the time main runs, and
 * is done here: argv split out of the one command line string Windows
 * hands a process, the environment block turned into environ, the three
 * standard handles turned into descriptors 0, 1 and 2, and exit arranged
 * so that main's return value becomes the process's exit status.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <stdlib.h>
#include <string.h>
#include "libc.h"
#include "rtlib.h"

/* main is always called below with the full three arguments, whichever of
 * the three standard forms -- int main(void), int main(int, char **),
 * int main(int, char **, char **) -- the program actually defined.
 *
 * The traditional way to write this, and what musl's crt1.c still does, is
 * the unprototyped `int main();', which promises nothing about the
 * parameter list and so is compatible with all three definitions.  C23
 * removes that construct: a `()' parameter list now means `(void)', so the
 * old declaration would silently turn into a promise that main takes no
 * arguments at all and the call below would stop compiling.
 *
 * So declare the widest form instead, the way mingw-w64's own startup code
 * does (mingw-w64-crt/include/internal.h: `int __CRTDECL main(int _Argc,
 * char **_Argv, char **_Env);').  This is a proper prototype, valid in
 * every version of C including C23.
 *
 * When the program's main is one of the narrower forms this declaration
 * does not match its definition -- but the two are in different translation
 * units, so no compiler can see the mismatch, and passing the extra
 * arguments is harmless under both calling conventions ntlibc targets:
 *
 *   - i386 __cdecl: arguments are pushed on the stack and *the caller*
 *     removes them, so a callee that reads fewer than were pushed leaves
 *     the stack correctly balanced;
 *   - x86_64 Microsoft x64: the first four arguments travel in RCX, RDX,
 *     R8 and R9, and the caller both allocates and reclaims the 32-byte
 *     shadow space, so again a callee reading fewer registers costs
 *     nothing.
 *
 * In both cases a narrower main simply ignores the trailing arguments.
 * This is the same bargain every hosted C implementation makes -- the
 * standard itself only requires main to be *callable*, and leaves the
 * mechanism to the implementation (C99 5.1.2.2.1). */
int main(int, char **, char **);

PPEB __peb;
char **environ;
char **__argv;
int __argc;
char *__progname;
char *__progname_full;

/* Split a Windows command line into arguments by the rules every Windows
 * C runtime uses (the ones CommandLineToArgvW implements):
 *
 *   - arguments are separated by spaces or tabs outside quotes;
 *   - a double quote toggles "in quotes", where whitespace is literal;
 *   - 2n backslashes followed by a quote are n backslashes and the quote
 *     is special; 2n+1 backslashes followed by a quote are n backslashes
 *     and a literal quote; backslashes not followed by a quote are literal;
 *   - inside quotes, "" is a literal quote (the post-2008 rule).
 *
 * The first argument (the program name) is special: backslashes are never
 * escapes, and only quotes delimit it.  Done on the UTF-16 string so that
 * the quoting rules see the same code units the shell produced; each
 * argument is then converted to UTF-8. */
/* p is required: every dereference of it below is guarded by `i < n`
 * (so p itself is never read when n == 0), but this function's one
 * real call site -- __libc_start_main's own
 * `split_cmdline(pp->CommandLine.Buffer, pp->CommandLine.Length /
 * sizeof(WCHAR), &__argv)` -- passes a UNICODE_STRING's own Buffer/
 * Length pair, and a valid UNICODE_STRING's own invariant (NT itself,
 * not this tree) is that Buffer is non-NULL whenever Length > 0 -- the
 * same "genuine invariant established by the real caller, not
 * derivable from a bound check alone" class as this project's own
 * fixed-capacity array precedent. argvp is required unconditionally:
 * `*argvp = argv;` runs on every success path with no guard of argvp
 * itself, and the same one real call site always passes `&__argv`, a
 * real global's address, never NULL. */
static int split_cmdline(const WCHAR *p, size_t n, char ***argvp)
    __attribute__((nonnull(1, 3)));
static int split_cmdline(const WCHAR *p, size_t n, char ***argvp)
{
	WCHAR *buf;
	char **argv;
	int argc = 0;
	size_t i = 0, cap = 2, units, bytes;

	if (!__size_add_checked(n, 1, &units) ||
	    !__size_mul_checked(units, sizeof(WCHAR), &bytes)) return -1;
	buf = __malloc(bytes);
	argv = (char **)__malloc(sizeof(char *[2]));

	if (!buf || !argv) return -1;

	/* program name */
	{
		size_t o = 0;
		int inq = 0;
		while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
		while (i < n) {
			if (p[i] == '"') { inq = !inq; i++; continue; }
			if (!inq && (p[i] == ' ' || p[i] == '\t')) break;
			buf[o++] = p[i++];
		}
		argv[argc++] = __utf16_to_utf8(buf, o);
	}

	for (;;) {
		size_t o = 0;
		int inq = 0;
		while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
		if (i >= n) break;
		for (;;) {
			if (i >= n) break;
			if (p[i] == '\\') {
				size_t nb = 0;
				while (i < n && p[i] == '\\') { nb++; i++; }
				if (i < n && p[i] == '"') {
					size_t k;
					for (k = 0; k < nb / 2; k++) buf[o++] = '\\';
					if (nb & 1) { buf[o++] = '"'; i++; }
					/* even: the quote is handled by the loop */
				} else {
					size_t k;
					for (k = 0; k < nb; k++) buf[o++] = '\\';
				}
				continue;
			}
			if (p[i] == '"') {
				if (inq && i + 1 < n && p[i+1] == '"') { buf[o++] = '"'; i += 2; continue; }
				inq = !inq; i++;
				continue;
			}
			if (!inq && (p[i] == ' ' || p[i] == '\t')) break;
			buf[o++] = p[i++];
		}
		if ((size_t)argc + 1 >= cap) {
			size_t next;
			char **nv;
			if (!__array_next_capacity(cap, (size_t)argc, 2, 2,
			    sizeof *argv, &next) ||
			    !__size_mul_checked(next, sizeof *argv, &bytes)) return -1;
				nv = (char **)__malloc(bytes);
				if (!nv) return -1;
				memcpy((void *)nv, (const void *)argv, sizeof *argv * (size_t)argc);
				__free((void *)argv);
			argv = nv;
			cap = next;
		}
		argv[argc++] = __utf16_to_utf8(buf, o);
	}
	argv[argc] = 0;
	__free(buf);
	*argvp = argv;
	return argc;
}

/* The environment block is a sequence of NUL-terminated UTF-16 strings
 * ended by an empty one.  Windows keeps some "=C:=C:\dir" entries for
 * per-drive current directories; those are kept too, the way msvcrt and
 * Cygwin keep them, since a child may need them. */
static char **build_environ(const WCHAR *env)
{
	size_t count = 0, i, slots, bytes;
	const WCHAR *p;
	char **ev;

	if (!env) {
		ev = (char **)__malloc(sizeof(char *));
		if (ev) ev[0] = 0;
		return ev;
	}
	for (p = env; *p; p += wcslen_(p) + 1) count++;
	if (!__size_add_checked(count, 1, &slots) ||
	    !__size_mul_checked(slots, sizeof *ev, &bytes)) return 0;
	ev = (char **)__malloc(bytes);
	if (!ev) return 0;
	for (p = env, i = 0; *p; p += wcslen_(p) + 1)
		ev[i++] = __utf16_to_utf8(p, wcslen_(p));
	ev[i] = 0;
	return ev;
}

/* pp's own two dereferences below (pp->StandardError, pp->CommandLine)
 * are a disclosed, deliberately unmarked residual, not an oversight:
 * __libc_start_main takes no parameters at all, so there is no
 * function signature for `nonnull` to describe this on. pp is a plain
 * local, `__peb->ProcessParameters` -- a struct FIELD's own value,
 * distinct from __peb itself (which this checker already trusts
 * structurally, see OwnershipChecker.cpp's own isAlwaysNonNullGlobal)
 * -- the same "struct-field-value fact, not expressible via nonnull on
 * any signature" class this tree's own signal.c exception_handler()
 * comment and plat_signal.c open_shared_stop_event() comment already
 * established for two different subsystems. Verified sound by hand
 * regardless: RTL_USER_PROCESS_PARAMETERS is allocated and populated
 * by the NT loader before any user-mode instruction of a process runs
 * (ReactOS's own BasePushProcessParameters/RtlCreateProcessParameters),
 * so PEB->ProcessParameters is exactly as OS-guaranteed non-NULL as
 * __peb itself -- just one field access further than this checker's
 * global-identity mechanism reaches. */
void __libc_start_main(void)
{
	PRTL_USER_PROCESS_PARAMETERS pp;
	int rc;

	/* The PEB comes out of the TEB, not out of an argument.
	 *
	 * Whether the image entry point's own argument holds the PEB
	 * depends on which era of NT runs it, and ntlibc's images are
	 * Subsystem 3 (Windows CUI) binaries, which is where the era
	 * matters:
	 *
	 *   - NT 4.0 through Server 2003 start the first thread at
	 *     kernel32!BaseProcessStart, reached through
	 *     BaseProcessStartThunk, which calls the entry point with NO
	 *     arguments at all (PPROCESS_START_ROUTINE is DWORD(WINAPI
	 *     *)(VOID) -- see ReactOS's dll/win32/kernel32/client/proc.c
	 *     and i386/thread.S). The PEB is placed in EBX by the initial
	 *     thread context but never pushed for the callee, so a
	 *     callee that reads "the first argument" anyway gets
	 *     whatever the preceding NtSetInformationThread call left in
	 *     that stack slot -- in practice the pseudo-handle
	 *     0xFFFFFFFE, so dereferencing it as a PEB faults at address
	 *     0xE. NT 4.0's own thunk is instruction-for-instruction the
	 *     same shape.
	 *   - Vista and later unified process and thread startup on
	 *     ntdll!RtlUserThreadStart, whose thread parameter *is* the
	 *     PEB.  ReactOS's ntdll.spec marks RtlUserThreadStart
	 *     `-stub -version=0x600+' for exactly that reason.
	 *
	 * No Microsoft documentation states what a Windows-subsystem entry
	 * point receives at all -- the /ENTRY page says only that "the
	 * parameters and return value depend on if the program is a
	 * console application, a windows application or a DLL" -- so
	 * there is no documented argument to read in the first place, on
	 * any NT version. There is also no reliable way to detect at run
	 * time which convention a given process got: a stale stack slot
	 * can look exactly like a valid PEB pointer, the way 0xFFFFFFFE
	 * does above.
	 *
	 * Hence: read the PEB from the TEB, which every thread has before
	 * any user code runs, on every NT version, whatever the subsystem.
	 *
	 * __teb() is a two-instruction read of fs:0x18 / gs:0x30 compiled
	 * into libc.a (src/internal/{i386,x86_64}/teb.c) -- not an ntdll
	 * import -- so no ntdll call happens before __peb exists.  That
	 * matters because RtlGetCurrentPeb(), the obvious alternative, is
	 * an ntdll import, and under -Wl,--delay-all it would be a
	 * delay-load stub whose very first resolution needs __peb already
	 * set (delayload2.c's __delayLoadHelper2 computes every RVA off
	 * __peb->ImageBaseAddress) -- a chicken-and-egg deadlock.
	 *
	 * TEB.ProcessEnvironmentBlock is at +0x30 on i386 and +0x60 on
	 * x86_64.  nt.h asserts exactly that (NT_LAYOUT_OFFSET(TEB,
	 * ProcessEnvironmentBlock, 12*NT_PTR)), and ReactOS's own
	 * against-real-Windows layout tests pin the same two numbers for
	 * both Windows Server 2003 and Windows 10
	 * (sdk/include/ndk/tests/win2003_x86.c and win10_x86.c: 0x030;
	 * win2003_x64.c and win10_x64.c: 0x060). */
	__peb = __teb()->ProcessEnvironmentBlock;
	pp = __peb->ProcessParameters;

	/* Checked as early as this function CAN check it while still being
	 * able to report why: see src/internal/ldbl_layout_check.c's own
	 * banner for what this proves and why it matters before anything
	 * below (__fenv_init() included) touches a long double. It cannot
	 * run any earlier than this -- reporting a real diagnostic needs
	 * pp->StandardError, which does not exist before the two lines
	 * above resolve it from the TEB -- and nothing between process
	 * entry and here touches a long double, so this loses nothing by
	 * not being literally the first statement in the function.
	 *
	 * The write is a direct NtWriteFile to the raw handle, not stdio:
	 * __plat_cancel_unsafe_abort's own diagnostic path (src/thread/nt/
	 * plat_thread.c) uses the identical shape for the identical reason
	 * -- this runs before stdio is initialized at all, and the
	 * message is a single static string, so nothing here needs to
	 * allocate. STATUS_INVALID_IMAGE_FORMAT is NT's own status for
	 * "this image does not match the ABI the loader expected" -- the
	 * same real category of failure this is, just discovered one layer
	 * up (a compiler's own ABI promise not matching what it actually
	 * generated) rather than by the PE loader itself. */
	if (!__verify_ldbl_layout()) {
		static const char msg[] =
			"ntlibc: long double bit-layout assumption failed at startup\r\n";
		IO_STATUS_BLOCK io;
		if (pp->StandardError)
			NtWriteFile(pp->StandardError, 0, 0, 0, &io, msg,
				sizeof msg - 1, 0, 0);
		NtTerminateProcess(NtCurrentProcess(), STATUS_INVALID_IMAGE_FORMAT);
	}

	__argc = split_cmdline(pp->CommandLine.Buffer, pp->CommandLine.Length / sizeof(WCHAR), &__argv);
	if (__argc < 0) NtTerminateProcess(NtCurrentProcess(), STATUS_NO_MEMORY);
	__progname = __argv[0];
	__progname_full = __utf16_to_utf8(pp->ImagePathName.Buffer, pp->ImagePathName.Length / sizeof(WCHAR));
	__environ = build_environ(pp->Environment);
	if (!__environ) NtTerminateProcess(NtCurrentProcess(), STATUS_NO_MEMORY);

	__fd_init();
	__signal_init();
	__fenv_init();

	rc = main(__argc, __argv, __environ);
	exit(rc);
}

/* The raw value of the image entry point's first argument, captured
 * before anything in this file can overwrite or reinterpret it.
 *
 * This exists to be *measured*, not used.  __libc_start_main above never
 * reads it -- it takes the PEB from the TEB instead, for the reasons set
 * out there, on every NT version.  test/entry-arg.c prints this alongside
 * the PEB read out of the TEB and the PEB the kernel reports for the
 * process, so that a log says which of them the entry point was actually
 * handed on the NT version it is run against; its header comment carries
 * the full version axis. Nothing may make this the source of __peb: it is
 * the quantity under measurement, and a consumer would turn the
 * measurement into a tautology. */
void *__entry_arg0;

/* The second argument slot -- %rdx on x86_64, [%esp+8] on i386 -- captured
 * alongside __entry_arg0 as a control: nobody claims the entry point
 * takes two arguments, so a log showing this slot holding something
 * other than the PEB confirms the capture reads real incoming machine
 * state rather than a hardcoded answer. */
void *__entry_arg1;

/* Both parameters are captured and neither is used.  Named arg0/arg1, not
 * peb, since whether either slot holds a PEB is exactly what is under
 * measurement (see above) -- it is not known to be one. */
void _start(void *arg0, void *arg1)
{
	__entry_arg0 = arg0;
	__entry_arg1 = arg1;
	__libc_start_main();
}

// NOLINTEND(misc-include-cleaner)
