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
 * exec (see spawn.c).  So every open descriptor's handle is marked
 * inheritable here before cloning, and the close-on-exec ones are put
 * back afterwards, in both processes.
 *
 * Marking *every* descriptor, close-on-exec ones included, is the whole
 * point, and it is not what this used to do.  A close-on-exec descriptor
 * is deliberately given a handle without OBJ_INHERIT (open.c, pipe.c,
 * dup.c, fcntl.c) so that exec does not carry it into the new program --
 * but fork is not exec.  POSIX fork() hands the child every descriptor
 * the parent had, close-on-exec included; they are only closed if and
 * when the child actually execs.  Leaving them unmarked here left the
 * clone with an fd table -- ordinary memory, so copied whole -- naming
 * handles that were never copied with it, and a handle number NT is not
 * using is a handle number NT will hand straight back out.  The first
 * thing an exec'ing child does is RtlCreateUserProcess, whose new
 * process handle lands on exactly such a free number whenever it is the
 * lowest one; __fd_close_all_cloexec (exec.c) then closes it "as a
 * descriptor" and the waitpid immediately after fails with
 * STATUS_INVALID_HANDLE.  What the caller sees is execve() returning
 * EBADF for a program that in fact started and ran to completion -- GNU
 * make reporting "Bad file descriptor" and exit 127 for a compile whose
 * object file is sitting right there.  A number recycled for something
 * other than the child process gives EFAULT (STATUS_ACCESS_VIOLATION)
 * or an object that never signals, i.e. a waitpid that never returns,
 * which is the same bug wearing a different hat.  It is not a race: the
 * clone's handle table is deterministic, so the same build loses the
 * same files every time.
 *
 * The price is two NtDuplicateObject calls per open descriptor per fork
 * rather than one per non-close-on-exec descriptor, and the close-on-exec
 * handles are inheritable only for the length of the clone call itself --
 * no exec can happen in between, so nothing leaks into a spawned program.
 *
 * The same goes for the process handles in __children: RtlCreateUserProcess
 * in __spawn and RtlCloneUserProcess here both hand them back
 * non-inheritable, so without help the clone's copy of the table would be
 * full of handle *values* that mean nothing (or, once NT reuses the slot,
 * something else entirely) in the child, and a waitpid on a sibling
 * would fail -- or worse, wait on the wrong object.  They are marked
 * inheritable around the clone, and un-marked again in both processes
 * afterwards, rather than kept inheritable from __child_add on: __spawn
 * passes InheritHandles=TRUE to RtlCreateUserProcess, which copies
 * *every* inheritable handle, not just the fd table it describes in
 * RuntimeData, so a permanently-inheritable child handle would leak into
 * every exec'd program as a stray process handle it can neither see nor
 * close.  The price is two NtDuplicateObject calls per tracked child per
 * fork, which is cheap next to the clone itself.  And, as on every
 * fork(), only the calling thread is cloned; a multi-threaded caller's
 * other threads simply do not exist in the child, which is the same
 * contract POSIX fork() has always had.
 */
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"

/* Set (inherit != 0) or clear the OBJ_INHERIT attribute on one open
 * descriptor's handle, in place: NtDuplicateObject with the attribute
 * asked for, then close the old handle and keep the new one under the
 * same fd number.  DUPLICATE_SAME_ATTRIBUTES is deliberately not used --
 * it would copy the source handle's attributes over the ones being
 * asked for, which is exactly backwards when the point is to change
 * one -- for the same reason mark_children_inheritable does not use it. */
static void set_fd_inherit(int i, int inherit)
{
	HANDLE dup;
	if (!__fds[i].h) return;
	if (NT_SUCCESS(NtDuplicateObject(NtCurrentProcess(), __fds[i].h, NtCurrentProcess(), &dup,
	                                 0, inherit ? OBJ_INHERIT : 0, DUPLICATE_SAME_ACCESS))) {
		NtClose(__fds[i].h);
		__fds[i].h = dup;
		__mq_fd_replaced(i, dup);
	}
}

/* Mark every open descriptor's handle inheritable so
 * RtlCloneUserProcess's INHERIT_HANDLES flag carries it into the child --
 * close-on-exec descriptors included, since fork gives the child all of
 * them and only exec drops them (see this file's header comment). */
static void mark_fds_inheritable(void)
{
	int i;
	for (i = 0; i < FD_MAX; i++) set_fd_inherit(i, 1);
}

/* Undo that for the close-on-exec descriptors, once the clone has been
 * made.  Run in both processes, before either can reach an exec, so a
 * close-on-exec handle is never inheritable at the moment __spawn's
 * InheritHandles=TRUE would copy it into a new program. */
static void unmark_cloexec_fds(void)
{
	int i;
	for (i = 0; i < FD_MAX; i++)
		if (__fds[i].flags & O_CLOEXEC) set_fd_inherit(i, 0);
}

/* Set (inherit != 0) or clear the OBJ_INHERIT attribute on every tracked
 * child-process handle.  DUPLICATE_SAME_ATTRIBUTES is deliberately not
 * used: the attribute is being changed, not copied.  The table is
 * ordinary memory -- the static seed array or, once grown, a process-heap
 * allocation (children.c), both of which RtlCloneUserProcess duplicates
 * along with the rest of the address space -- so the new handle values,
 * and the __children pointer aiming at them, travel with the clone. */
static void mark_children_inheritable(int inherit)
{
	int i;
	for (i = 0; i < __child_cap; i++) {
		HANDLE dup;
		if (!__children[i].pid || !__children[i].h) continue;
		if (NT_SUCCESS(NtDuplicateObject(NtCurrentProcess(), __children[i].h, NtCurrentProcess(), &dup,
		                                 0, inherit ? OBJ_INHERIT : 0, DUPLICATE_SAME_ACCESS))) {
			NtClose(__children[i].h);
			__children[i].h = dup;
		}
	}
}

pid_t fork(void)
{
	RTL_USER_PROCESS_INFORMATION info;
	NTSTATUS st;
	int pid;

	mark_fds_inheritable();
	mark_children_inheritable(1);

	memset(&info, 0, sizeof info);
	info.Length = sizeof info;

	st = RtlCloneUserProcess(RTL_CLONE_PROCESS_FLAGS_CREATE_SUSPENDED | RTL_CLONE_PROCESS_FLAGS_INHERIT_HANDLES,
	                          0, 0, 0, &info);

	if (st == STATUS_PROCESS_CLONED) {
		/* The child: this call is returning for the second time, in a
		 * thread the kernel built by copying the one that called it, in
		 * a process that is a copy of this one.  Almost nothing needs
		 * setting up -- __peb, __teb(), the fd table, the heap are all
		 * just memory, and all of it is already here.  What does need
		 * setting up is exactly the state POSIX says the child must
		 * *not* inherit, since the address-space copy is indifferent
		 * to that distinction.  The sibling handles in
		 * __children made the trip; stop them travelling any further,
		 * and put the close-on-exec descriptors back to non-inheritable
		 * now that they have arrived. */
		mark_children_inheritable(0);
		unmark_cloexec_fds();
		/* ...except the reaped-children time accounting, which is
		 * memory and therefore did make the trip, and must not have.
		 * fork.html: the child's tms_cutime/tms_cstime "shall be set
		 * to 0" -- it has waited for no children of its own.  Nothing
		 * in the kernel holds this figure (see wait.c), so this call
		 * is the only thing that can. */
		__rusage_children_reset();
		/* Same shape, same reason: fork.html also says "The time left
		 * until an alarm clock signal shall be reset to zero, and the
		 * alarm, if any, shall be canceled", and the deadline
		 * src/unistd/sleep.c records for a pending alarm() is a static
		 * that the address-space copy brought along.  The timer object
		 * behind it did not make the trip -- its handle is deliberately
		 * not OBJ_INHERIT -- so this only has to forget the deadline,
		 * not cancel anything. */
		__alarm_reset_after_fork();
		/* Memory locks and MCL_FUTURE are not inherited across fork().
		 * RtlCloneUserProcess copied mman.c's bookkeeping bytes, so make
		 * the child-side state match that kernel-level rule. */
		__mman_reset_after_fork();
		/* And once more: the sibling entries that travelled with the
		 * clone carry the parent's job-control bookkeeping, and this
		 * process stopped none of them.  Left alone, the clone would
		 * report through waitpid(WUNTRACED) a stop it did not cause,
		 * and would resume a sibling out from under the parent on its
		 * own exit (src/process/children.c). */
		__child_forget_stops();
		/* Same family, one more member: src/signal/sigdelivery.c's
		 * per-process listener pipe, delivery thread and mutex event.
		 * RtlCloneUserProcess clones only the calling thread (this
		 * file's banner), so the delivery thread that clone thinks it
		 * has does not exist here at all, and the pipe/mutex handle
		 * values it carried over name nothing live in this process --
		 * possibly something NT has since recycled onto that slot,
		 * exactly the hazard this file's banner describes for
		 * descriptor and child-process handles. Unconditional, like the
		 * three calls above it: it does not check whether this process
		 * already has a listener (it never can, immediately after a
		 * clone) and does not NtClose()/wait on the stale handles,
		 * only forgets them and builds fresh ones under this process's
		 * own, correctly-cloned pid. Left undone, the child would be
		 * permanently deaf to cross-process signals for its entire
		 * life: not merely delayed, since nothing would ever retry
		 * this. */
		__sig_delivery_reinit_after_fork();
		return 0;
	}

	mark_children_inheritable(0);
	unmark_cloexec_fds();
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	/* The parent.  The child exists, suspended; track it like any other
	 * child and let it run. */
	pid = (int)(ULONG_PTR)info.ClientId.UniqueProcess;
	if (__child_add(pid, info.Process) < 0) {
		/* The table grows on demand, so this only happens when it could
		 * not be grown -- the heap is exhausted.  Degrade rather than
		 * fail the fork: the child still runs, but it is unwaitable --
		 * waitpid() only ever consults the table (src/process/wait.c) --
		 * the same tradeoff __spawn makes. */
		NtClose(info.Process);
	}
	/* Still suspended: repair the WOW64-specific clone damage, if any,
	 * before the child ever runs a single instruction of it. */
	if (__is_wow64()) __wow64_fixup_clone(info.Process, info.Thread);
	NtResumeThread(info.Thread, 0);
	NtClose(info.Thread);
	return pid;
}

/* _Fork(): kept deliberately, and no test references it -- so it will
 * keep surfacing on tools/lint-unreferenced.sh's list.  It is not an
 * unspecified extension.  POSIX.1-2024 specifies it
 * (`https://pubs.opengroup.org/onlinepubs/9799919799/functions/_Fork.html`,
 * CHANGE HISTORY: "Austin Group Defects 62, 1361, and 1383 are applied,
 * adding the _Fork() function and removing the requirement for fork() to
 * be async-signal-safe"), so this is an interface the project has early,
 * not one it invented; deleting it means re-adding it when the project
 * moves editions.  Same reasoning as posix_close().
 *
 * What POSIX asks of it beyond fork() is that it be async-signal-safe
 * and not run pthread_atfork() handlers.  Neither distinguishes it here:
 * there is no libpthread (see flockfile in src/stdio/file.c), so there
 * are no atfork handlers to skip, and fork() registers no handlers of
 * its own.  So it forwards, and will need revisiting only if this
 * library ever grows real threads. */
pid_t _Fork(void)
{
	return fork();
}
