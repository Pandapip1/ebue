/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test-harness scaffolding for the "rest of src/unistd" Linux pilot --
 * NOT part of ntlibc, same standing as fuzz/linux_pilot_harness.c (the
 * mman/fd-ops pilot's own scaffolding, which this file deliberately
 * mirrors rather than duplicates logic from where the two overlap:
 * the fd table and the __fd_pos_save/restore, __mq_fd_closed and
 * __fsize_* stubs below are copied from it verbatim, because
 * src/unistd/{close,read,write,lseek,dup}.c are linked again here too,
 * to round-trip real data through the pipe()s src/unistd/pipe.c's
 * front door creates).
 *
 * Stands in for a handful of internal helpers the front-door files this
 * pilot links for real (src/unistd/{close,read,write,lseek,dup,fsync,
 * pipe,ftruncate,sysconf,unlink,ids}.c) reference but that this pilot
 * deliberately does not port, each for a different reason:
 *
 *   The fd table (__fds[], __fd_limit, __fd_alloc, __fd_install(_at),
 *   __fd_get) -- reimplemented here exactly as fuzz/linux_pilot_
 *   harness.c's own banner explains (src/internal/fd.c's real
 *   __fd_install_at() unconditionally calls __handle_type(), an NT-only
 *   device-type classifier, whenever type==0 -- a branch the compiler
 *   cannot prove dead even though this pilot never takes it).
 *
 *   __fd_pos_save/__fd_pos_restore, __mq_fd_closed, the __fsize_*
 *   quartet -- see fuzz/linux_pilot_harness.c's own definitions of the
 *   same four; write.c calls all of them unconditionally and this pilot
 *   needs no more from any of them than that file already established.
 *
 *   __environ -- normally filled in by crt1.c's NT-only process-
 *   parameter parsing before main() runs; src/unistd/ids.c's getlogin()
 *   reads it via getenv().  An empty environment is a faithful stand-in
 *   for "no USERNAME/USER set", which getlogin() already has a defined
 *   answer for (NULL).
 *
 *   getpid() -- NOT a __plat_* seam function at all, and so NOT another
 *   parallel session's territory to collide with: src/unistd/getpid.c's
 *   real getpid() reads NT's TEB directly
 *   (__teb()->ClientId.UniqueProcess), a construct that does not exist
 *   in a plain Linux ELF process, so the real front door cannot be
 *   linked into this pilot at all.  This is the same shape of gap
 *   src/internal/plat_fcntl.h's own banner documents for open() --
 *   "a non-NT backend will need an entirely different front door" --
 *   except here it applies to getpid()/gettid() themselves rather than
 *   to any src/internal/plat_unistd.h function, since neither is behind
 *   that interface.  This stub answers with the real Linux pid (a raw
 *   getpid(2) syscall) so that src/unistd/ids.c's own pid_exists()/
 *   setpgrp()/setsid()/getpgid() -- which all call getpid() as ordinary
 *   POSIX front-door code, the same way they would on any platform --
 *   can be exercised for real through that real front door rather than
 *   skipped.
 *
 *   __child_find() -- src/process/children.c's real definition is a
 *   plain array walk (portable on its own), but that file also defines
 *   __child_add(), which calls __sigchld_nocldwait() (src/signal/,
 *   another parallel session's subsystem), and its own clear_stops()
 *   helper, which calls __plat_process_resume() -- a
 *   src/internal/plat_process.h function, again another parallel
 *   session's Linux backend to write, not this one's.  Linking the real
 *   file to get one portable function would risk exactly the duplicate-
 *   __plat_* collision the project's NT-migration history already hit
 *   twice (see the task brief); a fixed "no known child" answer is
 *   enough for src/unistd/ids.c's pid_exists()/setpgid(), which only
 *   ask whether a pid is a child of this process, never false in a
 *   pilot that never fork()s.
 *
 *   __fsize_allow() -- src/misc/resource.c's real definition is fine on
 *   its own, but that file also #includes plat_misc.h and calls
 *   __plat_process_times_self()/__plat_process_open(), both someone
 *   else's Linux backend for the same reason as __child_find() above.
 *   "No limit" (0) is __fsize_allow()'s own answer when RLIMIT_FSIZE was
 *   never lowered, which is always true in this pilot.
 *
 * Added once src/unistd/{chdir,link}.c joined the real front doors this
 * pilot links (see tools/linux-build-unistd2.sh's own updated banner):
 *
 *   __vfs_cwd_set() -- src/internal/vfs.c's real definition is a single
 *   `static int cwd_kind; void __vfs_cwd_set(int kind) { cwd_kind =
 *   kind; }`, genuinely portable process-wide bookkeeping (chdir()'s own
 *   front door calls it, not __plat_chdir()) -- but that whole
 *   translation unit also defines __vfs_resolve_at(), which references
 *   real NT types (HANDLE, NTSTATUS, OBJECT_ATTRIBUTES,
 *   FILE_BASIC_INFORMATION) unconditionally, so linking the file at all
 *   would need NT headers this freestanding-Linux pilot has none of.
 *   Nothing this pilot calls (chdir() with this backend's __plat_chdir(),
 *   which always reports __VFS_NONE) ever reads cwd_kind back through
 *   __vfs_cwd_get() -- that only happens inside __vfs_resolve_at()
 *   itself, never linked here -- so a bare store with no reader is a
 *   faithful stand-in for what this pilot can actually observe.
 *
 *   __handle_path() -- pulled in by chdir.c's fchdir(), on the non-
 *   overlay branch (an fd whose f->vfs is not __VFS_ROOT/__VFS_DEV).
 *   This pilot's fd table never sets vfs at all (see __fd_install_at()
 *   above), so every fd it hands fchdir() takes that branch -- but the
 *   compiler cannot prove it statically, so the symbol is still needed;
 *   same reasoning, and same NULL "no reopenable name" stand-in, as
 *   fuzz/linux_pilot_harness_fs.c's own __handle_path() stub.
 *
 *   __free() -- pulled in by that same fchdir() branch (freeing the
 *   path __handle_path() returned). Never actually reached here since
 *   __handle_path() always returns NULL first, so a no-op body is
 *   enough to satisfy the linker.
 */
#include <string.h>
#include <unistd.h>
#include "libc.h"

struct __fd __fds[FD_MAX];
int __fd_limit = FD_MAX;

int __fd_alloc(int lowest)
{
	int i;
	if (lowest < 0) lowest = 0;
	for (i = lowest; i < __fd_limit; i++)
		if (!__fds[i].h) return i;
	errno = EMFILE;
	return -1;
}

/* Real fd.c's own version (src/internal/fd.c): frees getdents()'s
 * lazily-allocated continuation buffer before a slot is wiped and
 * reused -- src/unistd/close.c calls it unconditionally. __free()
 * below is already a no-op stand-in for this pilot, so this just
 * forwards to it like the real implementation does. */
void __fd_release_dynamic(struct __fd *f)
{
	if (f->dbuf) { __free(f->dbuf); f->dbuf = 0; }
}

int __fd_install_at(int fd, HANDLE h, unsigned flags, int type)
{
	struct __fd *f = &__fds[fd];
	__fd_release_dynamic(f);
	memset(f, 0, sizeof *f);
	f->h = h;
	f->flags = flags;
	f->type = (unsigned char)type; /* always nonzero in this pilot */
	f->pos = -1;
	return fd;
}

int __fd_install(HANDLE h, unsigned flags, int type)
{
	int fd = __fd_alloc(0);
	if (fd < 0) return -1;
	return __fd_install_at(fd, h, flags, type);
}

struct __fd *__fd_get(int fd)
{
	if (fd < 0 || fd >= FD_MAX || !__fds[fd].h) { errno = EBADF; return 0; }
	return &__fds[fd];
}

int __fd_pos_save(HANDLE h, long long *pos)
{
	(void)h;
	*pos = 0;
	return 0;
}

void __fd_pos_restore(HANDLE h, long long pos)
{
	(void)h;
	(void)pos;
}

void __mq_fd_closed(int fd)
{
	(void)fd;
}

/* write.c calls __fsize_limited() unconditionally to decide whether to
 * enforce a size limit at all; reporting "no limit" here means the
 * other three are unreachable at runtime (no rlimit is ever set), but
 * the linker still needs real bodies for them since the call site is
 * not statically dead code. */
int __fsize_limited(void) { return 0; }
long long __fsize_clamp(__plat_handle_t h, int append, size_t count)
{
	(void)h; (void)append;
	return (long long)count;
}
long long __fsize_room_at(long long off) { (void)off; return 0x7fffffffffffffffLL; }
int __fsize_exceeded(void) { return -1; }

/* src/unistd/ftruncate.c's own RLIMIT_FSIZE check; see this file's
 * banner for why the real src/misc/resource.c is not linked to provide
 * it. */
int __fsize_allow(long long size) { (void)size; return 0; }

/* src/unistd/ids.c's pid_exists()/setpgid(); see this file's banner for
 * why the real src/process/children.c is not linked to provide it. */
struct __child *__child_find(int pid) { (void)pid; return 0; }

/* See this file's own banner: not a __plat_* interface function, and
 * therefore not another session's Linux backend to collide with -- a
 * real getpid(2) syscall standing in for the front door
 * (src/unistd/getpid.c) that cannot be linked here at all. */
extern long syscall(long number, ...);
#define SYS_getpid_LX 172 /* aarch64; confirmed the same way every other
                           * syscall number in this pilot was (see
                           * src/mman/linux/plat_mem.c's banner) */
pid_t getpid(void)
{
	return (pid_t)syscall(SYS_getpid_LX);
}

/* crt1.c normally fills this in from NT's process parameters before
 * main() runs; an empty environment is a faithful stand-in here (see
 * this file's banner).  libc.h's own `#define __environ environ`
 * (matching glibc's naming) means the symbol to define is `environ`
 * itself, declared in <unistd.h>. */
char **environ;

/* See this file's own banner: chdir.c's real front door, portable
 * process-wide cwd-kind bookkeeping with no NT dependency of its own,
 * just not linkable here because it lives in the same translation unit
 * (src/internal/vfs.c) as genuinely NT-only __vfs_resolve_at(). */
void __vfs_cwd_set(int kind) { (void)kind; }

char *__handle_path(HANDLE h)
{
	(void)h;
	return 0;
}

void __free(void *p)
{
	(void)p;
}
