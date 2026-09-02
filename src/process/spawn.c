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
 *     (NULL) or (HANDLE)(LONG_PTR)-1 (INVALID_HANDLE_VALUE) into the
 *     field: on real Windows 11, something between the write here and
 *     the child's first instruction silently replaces either one with a
 *     live, open descriptor (measured; neither kernel32 nor kernelbase
 *     is even mapped into these -nostdlib/-lntdll-only processes at that
 *     point, so it is not a kernel32-side console/std-handle bring-up
 *     doing it, and the exact replacing actor -- ntdll on either side of
 *     the call, or the kernel itself -- is not identified).  The
 *     replacement is value-blind, not keyed to either sentinel, so a
 *     third magic value would fare no better.  What works instead is
 *     handing the field a real, valid, non-NULL, non-pseudo HANDLE that
 *     is not a file, console or pipe -- closed_placeholder() below
 *     duplicates the current-process pseudohandle for this.  The
 *     receiving side still refuses to install it, because
 *     __handle_type() cannot identify a process handle (src/internal/
 *     fd.c install_std), so the descriptor comes up closed in the child
 *     either way.
 *
 *     This also has to survive Windows filling in NULL std-handle slots
 *     when it allocates a new console for the child -- which it does
 *     whenever the parent has none (a detached process, a CI runner, a
 *     service), as opposed to the child inheriting the parent's console,
 *     where a NULL slot is left alone.  A live placeholder handle is
 *     preserved unchanged on both routes; a NULL slot is not.  That is
 *     why passing NULL for "closed" is not equivalent to what this file
 *     does, and why the difference only shows up for a detached parent
 *     rather than an interactive one.
 *
 * The command line is built by the quoting rules CommandLineToArgvW and
 * every Windows C runtime agree on, so that an argument with spaces,
 * quotes or backslashes survives the round trip into the child's argv.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "libc.h"
#include "plat_fd.h"
#include "plat_process.h"

/* posix_spawn()'s POSIX_SPAWN_SETSCHEDPARAM/POSIX_SPAWN_SETSCHEDULER --
 * see this pair's own declaration in libc.h for the full story.  Lives
 * here (portable) because its one real consumer, NT's
 * __plat_process_spawn() (src/process/nt/plat_process.c), and its one
 * real producer, src/process/posix_spawn.c's spawn_common(), are both
 * already reaching this file's own __spawn() for everything else a
 * spawn needs. */
static int pending_priority;
static int pending_priority_set;

void __spawn_set_pending_priority(int nice_value)
{
	pending_priority = nice_value;
	pending_priority_set = 1;
}

void __spawn_clear_pending_priority(void)
{
	pending_priority_set = 0;
}

int __spawn_pending_priority(int *out)
{
	if (!pending_priority_set) return 0;
	*out = pending_priority;
	return 1;
}

/* posix_spawn_file_actions_adddup2() targets above 2, for Linux's own
 * __plat_process_spawn() (src/process/linux/plat_process.c) alone -- see
 * struct __spawn_dup2_target's own comment (libc.h) for why this is the
 * mirror image of pending_priority above: NT never reads it back, Linux
 * is the only backend that needs a channel here at all. */
static const struct __spawn_dup2_target *pending_dup2s;
static int pending_dup2s_n;

void __spawn_set_pending_dup2s(const struct __spawn_dup2_target *list, int n)
{
	pending_dup2s = list;
	pending_dup2s_n = n;
}

void __spawn_clear_pending_dup2s(void)
{
	pending_dup2s = 0;
	pending_dup2s_n = 0;
}

const struct __spawn_dup2_target *__spawn_pending_dup2s(int *out_n)
{
	*out_n = pending_dup2s_n;
	return pending_dup2s;
}

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
	__plat_handle_t job = __PLAT_HANDLE_NULL;
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

	pid = __plat_process_spawn(path, argv, envp ? envp : __environ, std, &process, &job);
	if (pid < 0) return -1;

	/* The process exists, running (the backend already resumed its
	 * initial thread); track it like any other child so waitpid() can
	 * find it. */
	if (__child_add(pid, process, job) < 0) {
		/* The table grows on demand (src/process/children.c), so this
		 * only happens when the heap is exhausted.  Degrade rather than
		 * fail the spawn: the process still runs, but it is unwaitable
		 * -- waitpid() only ever consults the table (src/process/wait.c). */
		__plat_close(process);
		if (job) __plat_close(job);
	}
	return pid;
}

// NOLINTEND(misc-include-cleaner)
