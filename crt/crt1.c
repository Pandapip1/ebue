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
static int split_cmdline(const WCHAR *p, size_t n, char ***argvp)
{
	WCHAR *buf;
	char **argv;
	int argc = 0;
	size_t i = 0, cap = 2, units, bytes;

	if (!__size_add_checked(n, 1, &units) ||
	    !__size_mul_checked(units, sizeof(WCHAR), &bytes)) return -1;
	buf = __malloc(bytes);
	argv = __malloc(sizeof(char *[2]));

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
			nv = __malloc(bytes);
			if (!nv) return -1;
			memcpy(nv, argv, sizeof *argv * (size_t)argc);
			__free(argv);
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
		ev = __malloc(sizeof(char *));
		if (ev) ev[0] = 0;
		return ev;
	}
	for (p = env; *p; p += wcslen_(p) + 1) count++;
	if (!__size_add_checked(count, 1, &slots) ||
	    !__size_mul_checked(slots, sizeof *ev, &bytes)) return 0;
	ev = __malloc(bytes);
	if (!ev) return 0;
	for (p = env, i = 0; *p; p += wcslen_(p) + 1)
		ev[i++] = __utf16_to_utf8(p, wcslen_(p));
	ev[i] = 0;
	return ev;
}

void __libc_start_main(void)
{
	PRTL_USER_PROCESS_PARAMETERS pp;
	int rc;

	/* The PEB comes out of the TEB, not out of an argument.
	 *
	 * This used to read a first stack argument, on the reasoning that
	 * ntdll's RtlUserThreadStart hands the initial thread's start
	 * routine the PEB.  That is true -- from Vista onwards.  It is a
	 * statement about one era of NT, not about entry points, and
	 * ntlibc's images are Subsystem 3 (Windows CUI), which is where the
	 * era matters:
	 *
	 *   - NT 4.0 through Server 2003 start the first thread at
	 *     kernel32!BaseProcessStart, reached through
	 *     BaseProcessStartThunk;
	 *   - Vista and later unified process and thread startup on
	 *     ntdll!RtlUserThreadStart, whose thread parameter *is* the
	 *     PEB.  ReactOS's ntdll.spec marks RtlUserThreadStart
	 *     `-stub -version=0x600+' for exactly that reason.
	 *
	 * On the pre-Vista path the PEB is put in EBX and then dropped.
	 * ReactOS's BaseThreadStartThunk and BaseProcessStartThunk differ
	 * by one instruction -- the thread thunk does `push ebx' for
	 * lpParameter, the process thunk does not
	 * (dll/win32/kernel32/client/i386/thread.S) -- and
	 * BaseProcessStartup duly calls `lpStartAddress()' with no
	 * arguments, PPROCESS_START_ROUTINE being DWORD(WINAPI *)(VOID)
	 * (dll/win32/kernel32/client/proc.c).  The initial context does set
	 * it: `Context->Eax = StartAddress; Context->Ebx = Parameter;'
	 * (dll/win32/kernel32/client/utils.c).  So the value exists and
	 * simply never reaches the callee, leaving whatever the preceding
	 * NtSetInformationThread call left in that slot -- in practice
	 * NtCurrentThread(), the pseudo-handle 0xFFFFFFFE, so that reading
	 * ->ProcessParameters off it faulted at address 0xE.
	 *
	 * This is not a ReactOS quirk.  NT 4.0's own thunk is
	 * instruction-for-instruction the same shape
	 * (xor ebp,ebp / push eax / push 0 / jmp BaseProcessStart),
	 * measured from an NTSD disassembly by the coordinator who reported
	 * this; ReactOS is reproducing NT 5.x faithfully rather than
	 * diverging from it.  Recorded gap: no XP or Server 2003 binary was
	 * available, so NT 5.x's BaseProcessStart bytes were never actually
	 * disassembled.  NT 4 is measured, and the XP thunk has the same
	 * shape, but that last step is inference, not measurement.
	 *
	 * Wine cannot testify about any of this: `BaseProcessStart' does
	 * not appear anywhere under its dlls/ or include/.  It implements
	 * only the Vista+ shape -- the initial thread is started as
	 * signal_start_thread(TransferAddress, peb, teb)
	 * (dlls/ntdll/unix/server.c) and BaseThreadInitThunk passes that
	 * arg straight to the entry point, pushed on i386 and in %rcx on
	 * x86_64 (dlls/kernel32/thread.c).  So the old assumption survived
	 * here precisely because Wine implements the era the assumption
	 * came from.  Independent corroboration that the eras really do
	 * differ, from neither ReactOS nor our own code: Wine's own
	 * kernel32 process test once asserted
	 *   ok( !((ctx.Esp + 0x10) & 0xfff) || broken( !((ctx.Esp + 4) & 0xfff) ),
	 * with the comment `winxp, w2k3' (dlls/kernel32/tests/process.c at
	 * wine f19a0fb6c, line 3445; since deleted upstream) -- XP/2003
	 * reserve one dword below StackBase where Vista+ reserves 0x10, and
	 * ReactOS's own `Context->Esp -= sizeof(PVOID)' (utils.c) matches
	 * the measured w2k3 number.
	 *
	 * A related hazard, checked and found NOT to apply here -- recorded
	 * because the check is worth keeping and the negative result is
	 * easy to get backwards.  /ENTRY is documented to require __stdcall
	 * ("The function must be defined to use the __stdcall calling
	 * convention" -- MSVC's /ENTRY reference, Remarks).  A __stdcall
	 * entry point taking one argument emits `ret 4' and would therefore
	 * over-pop BaseProcessStartup's frame on every pre-Vista system,
	 * corrupting the caller's stack quite apart from the bad read.  Our
	 * _start was plain C, which tcc compiles as __cdecl, so it never
	 * had that defect.  Settled by disassembly, not by reasoning from
	 * the signature:
	 *
	 *   i386 before:  mov 0x8(%ebp),%eax / push %eax / call
	 *                 / add $0x4,%esp / leave / ret   <-- bare `ret'
	 *   i386 after:   push %ebp / mov %esp,%ebp / call / leave / ret
	 *
	 * x86_64 has no callee-pop at all, so no difference is expected
	 * there and none is seen (`call / leave / ret' before and after).
	 * This change therefore fixes one defect, the read -- not two.  The
	 * old signature was still wrong under the documented convention and
	 * merely harmless under the one we happened to get; `void
	 * _start(void)' is the unique signature correct under both, since a
	 * zero-argument callee pops nothing either way.  (In practice the
	 * epilogue is unreachable anyway -- __libc_start_main ends in
	 * exit(), which is _Noreturn -- but the signature should not depend
	 * on that.)
	 *
	 * Hence: read the PEB from the TEB, which every thread has before
	 * any user code runs, on every NT version, whatever the subsystem.
	 * Note that no Microsoft documentation states what a Windows-
	 * subsystem entry point receives -- the /ENTRY page says only that
	 * "the parameters and return value depend on if the program is a
	 * console application, a windows application or a DLL" -- so there
	 * was never a documented argument to read in the first place.
	 *
	 * Deliberately NOT done: detecting at run time which convention we
	 * got.  There is no reliable way to tell a passed PEB from a stale
	 * stack slot that merely looks like one -- that is exactly how this
	 * bug hid, 0xFFFFFFFE being a plausible-looking pointer.  The short
	 * version of everything above: the entry-point argument is
	 * unspecified before Vista and must never be relied on; the TEB is
	 * the portable source.
	 *
	 * __teb() is a two-instruction read of fs:0x18 / gs:0x30 compiled
	 * into libc.a (src/internal/{i386,x86_64}/teb.c) -- not an ntdll
	 * import -- so this keeps the property the old comment was really
	 * defending: no ntdll call happens before __peb exists.  That
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
 * This exists to be *measured*, not used.  __libc_start_main above no
 * longer reads it -- it takes the PEB from the TEB, for the reasons set
 * out there -- and that separation is exactly what makes this a
 * measurement: the CRT no longer has a stake in the answer.  The old
 * evidence that real Windows passes the PEB here was that this CRT read
 * the slot and nothing crashed, which is not a reading of the value.  The
 * assumption is version-specific, not universal: Vista and later enter at
 * ntdll!RtlUserThreadStart, whose thread parameter is the PEB, but NT 4
 * through Server 2003 enter at kernel32!BaseProcessStart and forward
 * nothing -- which is why ReactOS, which targets that era, declares the
 * entry point as PPROCESS_START_ROUTINE, DWORD (WINAPI *)(VOID).
 * test/entry-arg.c prints this alongside the PEB read out of the TEB and
 * the PEB the kernel reports for the process, so that a log says which of
 * them the entry point was actually handed; its header comment carries the
 * full version axis.
 *
 * Nothing may make this the source of __peb: it is the quantity under
 * measurement, and a consumer would turn the measurement into a
 * tautology. */
void *__entry_arg0;

/* The second argument slot -- %rdx on x86_64, [%esp+8] on i386 -- captured
 * for exactly one reason: it is the control for __entry_arg0.
 *
 * Nobody claims the entry point takes two arguments.  That is the point.
 * If __entry_arg0 came back holding the PEB and there were no second
 * reading, "we captured the incoming argument" and "we reported the PEB
 * because that is what this code always reports" would produce identical
 * logs.  A second slot read the same way, through the same mechanism, in
 * the same call, cannot hold the PEB by construction -- so a log showing
 * arg0 == PEB and arg1 == something else is a log in which the capture
 * demonstrably reads real machine state rather than synthesising an
 * answer.  If both slots came back equal to the PEB, or both came back
 * zero, that would be a reason to distrust the measurement, and it is
 * only visible because this is here. */
void *__entry_arg1;

/* Both parameters are captured and neither is used.  The first is named
 * arg0, not peb, deliberately: calling it "peb" is what made the original
 * bug look reasonable for as long as it did. */
void _start(void *arg0, void *arg1)
{
	__entry_arg0 = arg0;
	__entry_arg1 = arg1;
	__libc_start_main();
}
