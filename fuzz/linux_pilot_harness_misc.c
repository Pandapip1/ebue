/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test-harness scaffolding for the Linux platform pilot's exit/misc/
 * select/signal extension -- NOT part of ntlibc, exactly like
 * fuzz/linux_pilot_harness.c is "not part of ntlibc" for the mman/
 * unistd pilot it extends.  See that file's own banner for the general
 * shape of this kind of scaffolding; this one stubs the handful of
 * cross-subsystem symbols src/misc/sched.c and src/misc/resource.c
 * reference that belong to subsystems this session does not own and
 * that this test never actually exercises, PLUS real (not stubbed)
 * one-syscall standins for getpid()/getppid()/getuid()/geteuid()/
 * getpgrp() -- src/unistd/getpid.c and src/unistd/ids.c's real
 * implementations are NT-only (__teb(), __plat_getppid(),
 * __plat_detect_uid() -- none built for Linux by this session, out of
 * scope, part of the rest of src/unistd a parallel session owns), so
 * sched.c/resource.c cannot link against them; these direct-syscall
 * standins let this test still exercise the REAL src/misc/sched.c and
 * src/misc/resource.c front doors end to end rather than calling this
 * session's plat_misc.c backend functions in isolation.
 *
 *   __child_find()       -- src/process/'s pid table (src/internal/
 *                        libc.h). sched.c's process_exists() and
 *                        resource.c's getpriority()/setpriority()
 *                        consult it before falling back to a foreign-
 *                        pid syscall path; this test only ever asks
 *                        about a real, but untracked-by-ntlibc, foreign
 *                        pid (its own parent), so "never a known
 *                        child" is the honest answer here.
 *   __rusage_children()  -- src/process/wait.c's accumulated child CPU
 *                        time; referenced by resource.c's
 *                        getrusage(RUSAGE_CHILDREN) arm, which this
 *                        test does not exercise (RUSAGE_SELF only).
 *   __raise_internal()   -- src/signal/signal.c's synchronous signal
 *                        delivery core; referenced (only on the
 *                        RLIMIT_FSIZE-exceeded path, __fsize_exceeded())
 *                        by resource.c but never reachable from any
 *                        setrlimit()/getrlimit()/getpriority()/
 *                        setpriority()/getrusage() call this test
 *                        makes -- needed only so the linker has a body
 *                        for a call site the compiler cannot prove
 *                        dead.
 *   __mq_fd_closed()     -- mqueue bookkeeping (src/internal/libc.h);
 *                        referenced unconditionally by the REAL
 *                        src/unistd/close.c this pilot now links (see
 *                        tools/linux-build-misc.sh's own note on why
 *                        close() joined the FILES list). This test
 *                        never opens an mqd_t, so there is nothing for
 *                        this hook to release -- the same no-op stand-
 *                        in fuzz/linux_pilot_harness_fs.c already uses
 *                        for the identical reason.
 */
#include <sys/types.h>
#include <sys/resource.h>
#include "libc.h"

/* aarch64 Linux syscall numbers (confirmed via a throwaway host program
 * printing the SYS_* macros from <sys/syscall.h>, the same oracle
 * technique src/mman/linux/plat_mem.c's banner describes). */
#define SYS_getpid  172
#define SYS_getppid 173
#define SYS_getuid  174

extern long syscall(long number, ...);

struct __child *__child_find(int pid) { (void)pid; return 0; }

void __rusage_children(struct rusage *ru) { if (ru) __builtin_memset(ru, 0, sizeof *ru); }

int __raise_internal(int sig) { (void)sig; return 0; }

void __mq_fd_closed(int fd) { (void)fd; }

pid_t getpid(void) { return (pid_t)syscall(SYS_getpid); }
pid_t getppid(void) { return (pid_t)syscall(SYS_getppid); }
uid_t getuid(void) { return (uid_t)syscall(SYS_getuid); }
uid_t geteuid(void) { return getuid(); }
/* Never actually reached by this test (always which==PRIO_PROCESS),
 * but resource.c's getpriority()/setpriority() switch statements
 * reference it regardless -- see this file's banner. this process is
 * its own process group of one for every purpose ntlibc's own
 * src/unistd/ids.c documents, so its own pid is as honest an answer as
 * any. */
pid_t getpgrp(void) { return getpid(); }
