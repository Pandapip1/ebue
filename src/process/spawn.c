/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Starting another program.
 *
 * Windows starts a process from an image file, not by copying a running
 * one, so __spawn is what execve and posix_spawn are built on: it builds
 * the child's process parameters (command line, environment, current
 * directory, and the inherited descriptor table), creates the process
 * suspended with RtlCreateUserProcess, and resumes it.
 *
 * Two things about handing a child its file descriptors are not obvious
 * and are done here because otherwise redirection fails silently:
 *
 *   - Only handles marked OBJ_INHERIT are copied into the child, so the
 *     ones the child should keep are duplicated inheritable first (that
 *     is what __fd_runtime_data does), and the numbers are written into
 *     the RuntimeData block the child's crt1 reads back.
 *
 *   - STARTF_USESTDHANDLES has to be set in WindowFlags or the standard
 *     handles written below do not reach the child intact -- measured to
 *     be necessary on Windows 11.  RtlCreateUserProcess offers no way to
 *     set it, so it is written into the parameter block directly.
 *     (ReactOS's kernel32 SetUpHandles sets the same flag, which is why
 *     it was picked; but that is a *parent-side kernel32* function this
 *     code never calls, so treat it as precedent for the value, not as
 *     an account of what consumes the flag here.  See the next bullet:
 *     kernel32 is not in the picture on this path at all.)
 *
 *   - A *closed* standard descriptor cannot be represented by writing 0
 *     (NULL) *or* (HANDLE)(LONG_PTR)-1 (INVALID_HANDLE_VALUE) into the
 *     field.  Something between the write here and the child's first
 *     instruction replaces it.  What is known about that something,
 *     sorted by how well it is known:
 *
 *     Measured.  NULL fails on real Windows.  -1 fails on real Windows
 *     as well (CI run 32703368816, the first real-Windows run to include
 *     the -1 attempt): in both cases the child came up with a live, open
 *     descriptor on all three windows-test legs.  A real-but-rejected
 *     handle works (run 32725836191, all three legs green).  Two
 *     unrelated sentinels failing identically is the useful part: the
 *     fix-up is *value-blind*.  It replaces whatever the caller wrote
 *     without inspecting it, which is why looking for a third magic
 *     value would not have worked either.
 *
 *     Ruled out.  Every kernel32/kernelbase mechanism, on the flat
 *     ground that neither DLL is in these processes.  tools/
 *     linkcheck.sh:525 and the Makefile's test rule both link
 *     `-nostdlib ... -lc -lntdll`, ntdll only; a built artifact agrees
 *     (the test executables import ntdll.dll and nothing else,
 *     Subsystem 3 = CUI); configure defaults kernel32=no, and the one
 *     CI leg built --enable-kernel32 still reaches kernel32 by
 *     LdrLoadDll at signal-setup time (src/signal/signal.c), not by import.  So at the
 *     moment __fd_init (src/internal/fd.c) reads StandardInput,
 *     kernelbase has never been mapped into the child.  That disposes of
 *     ConDllInitialize, BasepInitConsole, SetUpHandles, the CSRSS/ConSrv
 *     handoff, AllocConsole's NULL-backfill and lazy GetStdHandle alike
 *     -- and of the earlier revisions of this comment, which blamed "the
 *     child's own kernelbase.dll console/std-handle bring-up, which
 *     every CUI-subsystem process still goes through".  That code never
 *     ran here.  (The rprichard/win32-console-docs "CreateProcess
 *     (modern)" and "AllocConsole, AttachConsole (modern)" rule sets
 *     that earlier revisions leaned on are accurate; they are just rules
 *     about kernel32 functions this file does not call.)
 *
 *     Unknown.  Which actor actually does it.  Three remain: ntdll on
 *     the parent side, ntdll on the child side (LdrpInitializeProcess),
 *     or the kernel inside NtCreateUserProcess.  Nothing measured so far
 *     separates them, and this comment deliberately does not guess.
 *     test/spawn-stdhandle-attr.c is the probe built to separate them;
 *     it prints the raw values rather than asserting anything about
 *     them.
 *
 *     Leading candidate -- inference, not a finding.
 *     PS_ATTRIBUTE_STD_HANDLE_INFO / PsAttributeStdHandleInfo (phnt,
 *     ntpsapi.h:3232, structure and states at :3364-3390).  Its
 *     PsAlwaysDuplicate state duplicates the standard handles
 *     unconditionally -- value-blind -- and it is consumed during
 *     process creation, before the child runs an instruction.  Both
 *     properties match the measurement.  The tension against it: a
 *     zeroed attribute means PsNeverDuplicate, which is enumerator 0,
 *     and that predicts *no* fix-up, contradicting what was measured.
 *     For the candidate to survive, either ntdll supplies the attribute
 *     explicitly with a non-zero state, or the kernel's default in the
 *     attribute's absence is not the zero enumerator.  Neither has been
 *     shown.
 *
 *     What does work, and is what this file does: hand the field a real,
 *     valid, non-NULL, non-pseudo HANDLE that is not a file, console or
 *     pipe, so that a value-blind fix-up has nothing to fix up and a
 *     value-*sensitive* one has no sentinel to recognise.
 *     closed_placeholder() below duplicates the current-process
 *     pseudohandle for this.  The receiving side still refuses to
 *     install it, because __handle_type() cannot identify it
 *     (src/internal/fd.c install_std; NtQueryVolumeInformationFile
 *     answers STATUS_OBJECT_TYPE_MISMATCH for a process handle --
 *     verified directly with a standalone probe, not assumed), so the
 *     descriptor comes up closed in the child either way.
 *
 *     That this survives is measured, not inferred, and the measurement
 *     matters because the OS does rewrite std handle slots in one case.
 *     The rule (Windows 11 Pro 22621): if the child *inherits* the
 *     parent's console, a NULL std slot stays NULL; if a console is
 *     *allocated* for the child -- because the parent had none, or
 *     CREATE_NEW_CONSOLE, or CREATE_NO_WINDOW -- each NULL slot is
 *     filled per-slot, and non-NULL slots are left exactly as passed.
 *     A process handle in a std slot was then measured on both routes
 *     specifically, since every earlier cell had used a file handle:
 *     it is preserved unchanged either way.
 *
 *     So this file is immune to the fill path by construction, and not
 *     by luck: it never passes NULL, and non-NULL is never touched.
 *     The reason to state it as a rule rather than a result is that a
 *     bootstrap build, a CI runner or any detached process has no
 *     console, so its children take the *allocating* route -- the one
 *     that rewrites slots -- rather than the interactive inheriting
 *     one.  Code that reasons "we passed NULL, so the child sees NULL"
 *     would be correct only in the interactive case.  Representing a
 *     closed descriptor as something the child will reject, rather than
 *     as absence, is what makes the two routes agree: absence is what
 *     the OS feels entitled to fill in.
 *
 *     Not measured: a session-0 / service-context parent.  A detached
 *     interactive-user parent is not the same thing, and CI cannot
 *     close the gap -- GitHub's runner is an interactive user, so its
 *     legs re-measure the detached shape rather than a service.  If a
 *     service parent turns out not to get a console allocated for its
 *     children, the fill would not happen there.
 *
 *     Wine not reproducing any of this is consistent with the above
 *     rather than evidence about it.  Nothing in the Wine tree reads
 *     PsAttributeStdHandleInfo -- include/winternl.h:4131 and :4167
 *     define the enumerator and the PS_ATTRIBUTE_STD_HANDLE_INFO macro
 *     and that is the entirety of the hits -- and
 *     dlls/ntdll/unix/env.c's create_startup_info copies hStdInput/
 *     hStdOutput/hStdError through by value for a process created with
 *     PROCESS_CREATE_FLAGS_INHERIT_HANDLES, which is exactly what
 *     RtlCreateUserProcess(inherit=TRUE) asks for.  So under Wine the
 *     caller's value survives verbatim, whatever it is.
 *
 * The command line is built by the quoting rules CommandLineToArgvW and
 * every Windows C runtime agree on, so that an argument with spaces,
 * quotes or backslashes survives the round trip into the child's argv.
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "libc.h"
#include "plat_fd.h"
#include "plat_process.h"

/* Everything this file used to do inline -- building the UTF-16 command
 * line and environment block (see this file's own banner, above, for the
 * quoting rules and the measurements behind the standard-handle
 * fix-up), resolving the image path, and the whole
 * RtlCreateProcessParametersEx -> RtlCreateUserProcess -> resume sequence
 * -- now lives in src/process/nt/plat_process.c's __plat_process_spawn(),
 * one coarse call for the reason src/internal/plat_process.h's banner
 * gives.  What is left here is POSIX-level bookkeeping only: resolving
 * the three standard descriptors from this process's OWN fd table (the
 * table itself, and its close-on-exec bit, is this library's POSIX
 * bookkeeping, not part of the platform interface -- see plat_fd.h), and
 * adding the new pid to the child table so waitpid() can find it. */
int __spawn(const char *path, char *const argv[], char *const envp[])
{
	struct __fd *f0 = __fd_get(0), *f1 = __fd_get(1), *f2 = __fd_get(2);
	__plat_handle_t std[3];
	__plat_handle_t process = __PLAT_HANDLE_NULL;
	int pid;

	/* A close-on-exec standard descriptor is not the child's to have,
	 * and its handle is not inheritable anyway, so it is represented to
	 * the backend exactly like a fully closed one -- __PLAT_HANDLE_NULL,
	 * which __plat_process_spawn() turns into a real, value-blind
	 * placeholder rather than a sentinel a Windows fix-up could
	 * mistake for a live handle (see plat_process.h/plat_process.c). */
	if (f0 && (f0->flags & O_CLOEXEC)) f0 = 0;
	if (f1 && (f1->flags & O_CLOEXEC)) f1 = 0;
	if (f2 && (f2->flags & O_CLOEXEC)) f2 = 0;
	std[0] = f0 ? f0->h : __PLAT_HANDLE_NULL;
	std[1] = f1 ? f1->h : __PLAT_HANDLE_NULL;
	std[2] = f2 ? f2->h : __PLAT_HANDLE_NULL;

	pid = __plat_process_spawn(path, argv, envp ? envp : __environ, std, &process);
	if (pid < 0) return -1;

	/* The process exists, running (the backend already resumed its
	 * initial thread); track it like any other child so waitpid() can
	 * find it. */
	if (__child_add(pid, process) < 0) {
		/* The table grows on demand (src/process/children.c), so this
		 * only happens when the heap is exhausted.  Degrade rather than
		 * fail the spawn: the process still runs, but it is unwaitable
		 * -- waitpid() only ever consults the table (src/process/wait.c
		 * used to reopen the pid instead, which was wrong; see the
		 * comment there). */
		__plat_close(process);
	}
	return pid;
}
