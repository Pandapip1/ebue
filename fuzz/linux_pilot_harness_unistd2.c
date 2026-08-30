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
 */
#include <string.h>
#include <unistd.h>
#include "libc.h"

/* A REAL raw syscall trampoline, overriding whatever `syscall` symbol
 * this pilot's ordinary (non-freestanding, host-libc-linked) build would
 * otherwise pick up from the host's own libc.so.
 *
 * src/mman/linux/plat_mem.c, src/unistd/linux/plat_fd.c and this pilot's
 * own src/unistd/linux/plat_unistd.c all declare `extern long
 * syscall(long number, ...);` and document (see plat_mem.c's banner)
 * that a raw Linux syscall "returns the result on success, or -errno
 * ... on failure" -- the kernel's own ABI, as issued by the `svc #0`
 * instruction directly. That is true of a real freestanding backend's
 * own syscall trampoline, but this pilot links against the host's
 * ordinary libc.so (tools/linux-build*.sh use `clang` with no
 * -nostdlib/-static/custom _start), so without this override the
 * `syscall` symbol every plat_*.c file calls resolves to GLIBC'S OWN
 * `syscall(2)` WRAPPER instead -- which is POSIX-shaped, not raw: on
 * failure it returns bare -1 and stores the real error in glibc's own
 * errno, never handing back -errno to the caller at all.  Confirmed on
 * this host with chdir(2) against a nonexistent path: through glibc's
 * wrapper the call site sees `ret == -1` (so plat_unistd.c's `errno =
 * (int)-ret` computes EPERM, always, regardless of the real failure);
 * through this trampoline it correctly sees `ret == -2` (-ENOENT), the
 * real ABI value plat_unistd.c's is_sys_error()/errno logic is written
 * to expect.
 *
 * This is a property of how THIS PILOT links, not a bug in any plat_*.c
 * file's own logic -- a genuine freestanding Linux backend would ship
 * exactly this kind of trampoline as part of its own runtime startup
 * (mirroring musl's __syscall()), never glibc's.  Fixing it here, in
 * test scaffolding, rather than papering over it in plat_unistd.c's own
 * error handling, keeps that file's logic correct for the real target
 * it is actually written for. aarch64 SVC ABI: x8 = syscall number,
 * x0..x5 = up to six arguments, raw x0 is the return value -- the
 * variadic C call already places its first six arguments in x1..x6
 * (AAPCS64 does not special-case integer/pointer varargs), so this
 * only has to shift them down by one register and swap the syscall
 * number into x8.
 *
 * Declared with a fixed arity, NOT `(long number, ...)`, even though
 * every call site's own extern declaration (plat_mem.c/plat_fd.c/
 * plat_unistd.c, and this pilot's own test) is variadic: a variadic
 * `naked` definition measurably breaks on this host's clang 18 --
 * confirmed by reproducing it standalone -- because the compiler still
 * emits its normal variadic-argument-spilling prologue in front of a
 * naked body when the DEFINITION itself is variadic, clobbering x0..x6
 * before this function's own asm ever runs and sending `ret` to a
 * garbage address (observed as a SIGBUS/BUS_ADRALN crash on the very
 * first lseek() call after a working ftruncate() -- an illegal, non-4-
 * byte-aligned jump target).  The caller-side AAPCS64 register-passing
 * convention this trampoline actually depends on is identical whether
 * the callee's own prototype is variadic or not -- variadic-ness is a
 * callee-prologue concern, not a caller one -- so a fixed 7-`long`
 * signature receives the exact same registers a real call site (however
 * many arguments it actually passes) puts there, without asking the
 * compiler to generate any prologue for them at all.
 *
 * Defined under an internal name and bound to the linker symbol
 * `syscall` via `__asm__("syscall")` rather than being named `syscall`
 * directly in C: this translation unit also pulls in <unistd.h> (for
 * pid_t/getpid()'s prototype below), which itself declares `long
 * syscall(long, ...)` -- the ordinary variadic signature -- and a
 * second, conflicting C declaration of the same name with a different
 * (fixed-arity) signature does not compile.  The renamed definition
 * sidesteps that: nothing here declares `syscall` in C at all, so there
 * is nothing to conflict with, while the object file still exports
 * exactly the symbol every plat_*.c file's own `extern long
 * syscall(long, ...);` links against. */
__attribute__((naked)) long __raw_syscall_trampoline(long number, long a1, long a2, long a3, long a4, long a5, long a6) __asm__("syscall");
__attribute__((naked)) long __raw_syscall_trampoline(long number, long a1, long a2, long a3, long a4, long a5, long a6)
{
	__asm__ volatile (
		"mov x8, x0\n"
		"mov x0, x1\n"
		"mov x1, x2\n"
		"mov x2, x3\n"
		"mov x3, x4\n"
		"mov x4, x5\n"
		"mov x5, x6\n"
		"svc #0\n"
		"ret\n"
	);
}

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

int __fd_install_at(int fd, HANDLE h, unsigned flags, int type)
{
	struct __fd *f = &__fds[fd];
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
