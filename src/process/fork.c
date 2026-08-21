/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fork().
 *
 * Unlike execve, fork has no image to start from: it has to give the
 * child a copy of *this* process's own memory -- heap, globals, stack --
 * and have it resume inside fork() itself, not at the entry point crt1.c
 * runs for every other process.  Windows has exactly one primitive for
 * that, ntdll's RtlCloneUserProcess, and no combination of the others
 * gets there:
 *
 *   - NtCreateProcessEx and NtCreateProcess (the lower-level calls
 *     RtlCloneUserProcess itself is built on) create a process object
 *     that shares the calling process's address space but has no thread.
 *     The obvious next step, NtCreateThreadEx into it, does not work: the
 *     kernel considers a thread-less process "awaiting deletion" and
 *     unconditionally answers STATUS_PROCESS_IS_TERMINATING to any
 *     attempt to give it one.  There is no supported way to hand such a
 *     process its first thread other than what RtlCloneUserProcess itself
 *     does internally.
 *
 *   - NtCreateUserProcess does not clone anything: it is RtlCreateUserProcess's
 *     underlying call, and like it, it maps a *new* image into a *new*
 *     address space.  That is exec's primitive, not fork's; __spawn
 *     already uses the Rtl wrapper around it.
 *
 *   - RtlCloneUserProcess itself is documented as unreliable and is where
 *     the trouble it took to characterise the above two lives: measured
 *     on real Windows (M2libc's x86/windows/process.c has the notes),
 *     under WOW64 -- a 32-bit process running on a 64-bit kernel -- the
 *     cloned thread resumes past the 32-on-64 CPU-simulation bring-up
 *     that normally programs the FS segment base, so fs:0x18 (the TEB
 *     access every access to __teb() depends on) faults; separately, the
 *     clone can inherit an ntdll-internal SRW lock in a state only the
 *     WOW64 return path releases, deadlocking the child on its way out
 *     of the call.  Both are specific to that 32-on-64 transition: they
 *     live in the code path a thread only takes while switching between
 *     32-bit and 64-bit execution modes on the way back from the clone.
 *     A native x86_64 ntlibc process never runs that way -- there is no
 *     32-on-64 handoff to miss -- but an i386 ntlibc process can be
 *     running under WOW64, so fork() below checks __is_wow64() and, on
 *     that path only, calls __wow64_fixup_clone() (arch/i386/src/
 *     wow64_fixup.c) on the clone's still-suspended handles before ever
 *     resuming it: the same heaven's-gate context surgery and stuck-lock
 *     patch M2libc's notes describe, done here in ntlibc's own types.
 *
 * What RtlCloneUserProcess actually does is duplicate the calling
 * process's entire address space, at the same virtual addresses, into a
 * new process, and clone the calling thread's own register state into a
 * new thread in it -- so the new thread's copy of this very call resumes
 * with the same stack, the same locals, the same everything, and returns
 * STATUS_PROCESS_CLONED instead of STATUS_SUCCESS to tell it apart from
 * the original.  That is fork()'s 0-vs-child-pid split, for free: no
 * setjmp-style register capture, no manual memory copy, no address
 * matching to arrange, because the primitive already does all of that as
 * the definition of "clone".  It is also exactly why __fds (fd.c) and the
 * heap (malloc.c) both say in their own comments that they survive fork
 * automatically: they are just memory, and all of this process's memory
 * makes the trip.
 *
 * The one thing that does not make the trip on its own is *handle*
 * inheritance: NT copies into the clone only the handles marked
 * OBJ_INHERIT, the same rule __spawn relies on for redirected fds across
 * exec (see spawn.c).  open() and pipe() already mark every non-O_CLOEXEC
 * handle that way, but the three standard handles this process itself
 * inherited (and anything a future fcntl/dup path might leave otherwise)
 * are not guaranteed to be, so every open, non-close-on-exec descriptor
 * is (re-)marked inheritable here before cloning, the same way
 * __fd_runtime_data does for a spawned child.
 *
 * What does not carry over cleanly: any process handle already sitting
 * in __children for a grandchild spawned before this fork was not
 * necessarily created inheritable (RtlCreateUserProcess in __spawn
 * doesn't ask for that), so a child that calls fork() and then waitpid's
 * a process its parent started earlier may find that handle invalid in
 * its own process -- a real limitation of Windows handle inheritance,
 * not something this implementation papers over.  And, as on every
 * fork(), only the calling thread is cloned; a multi-threaded caller's
 * other threads simply do not exist in the child, which is the same
 * contract POSIX fork() has always had.
 */
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"

/* Mark every open, non-O_CLOEXEC descriptor's handle inheritable so
 * RtlCloneUserProcess's INHERIT_HANDLES flag actually carries it into
 * the child.  NtDuplicateObject onto the same handle just adds the flag
 * in place; the fd table's numeric value doesn't change. */
static void mark_fds_inheritable(void)
{
	int i;
	for (i = 0; i < FD_MAX; i++) {
		HANDLE dup;
		if (!__fds[i].h || (__fds[i].flags & O_CLOEXEC)) continue;
		if (NT_SUCCESS(NtDuplicateObject(NtCurrentProcess(), __fds[i].h, NtCurrentProcess(), &dup,
		                                 0, OBJ_INHERIT, DUPLICATE_SAME_ACCESS | DUPLICATE_SAME_ATTRIBUTES)))
			__fds[i].h = dup;
	}
}

pid_t fork(void)
{
	RTL_USER_PROCESS_INFORMATION info;
	NTSTATUS st;
	int pid;

	mark_fds_inheritable();

	memset(&info, 0, sizeof info);
	info.Length = sizeof info;

	st = RtlCloneUserProcess(RTL_CLONE_PROCESS_FLAGS_CREATE_SUSPENDED | RTL_CLONE_PROCESS_FLAGS_INHERIT_HANDLES,
	                          0, 0, 0, &info);

	if (st == STATUS_PROCESS_CLONED) {
		/* The child: this call is returning for the second time, in a
		 * thread the kernel built by copying the one that called it, in
		 * a process that is a copy of this one.  Nothing else to set up
		 * -- __peb, __teb(), the fd table, the heap are all just memory,
		 * and all of it is already here. */
		return 0;
	}

	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	/* The parent.  The child exists, suspended; track it like any other
	 * child and let it run. */
	pid = (int)(ULONG_PTR)info.ClientId.UniqueProcess;
	if (__child_add(pid, info.Process) < 0) {
		/* The table is full; the child still runs, but waitpid on it
		 * will have to reopen it (the same tradeoff __spawn makes). */
		NtClose(info.Process);
	}
	/* Still suspended: repair the WOW64-specific clone damage, if any,
	 * before the child ever runs a single instruction of it. */
	if (__is_wow64()) __wow64_fixup_clone(info.Process, info.Thread);
	NtResumeThread(info.Thread, 0);
	NtClose(info.Thread);
	return pid;
}

pid_t _Fork(void)
{
	return fork();
}
