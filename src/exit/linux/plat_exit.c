/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_exit.h -- see src/mman/linux/
 * plat_mem.c's own banner for the general discipline this file follows
 * too (raw syscall(2), no host libc, -nostdinc against ntlibc's own
 * headers, aarch64 syscall numbers confirmed against this host's own
 * <sys/syscall.h>, since this file's build cannot include that header
 * itself without pulling in glibc's conflicting type system).
 *
 * exit_group(2), not exit(2): exit(2) ends only the calling thread,
 * which is only correct for a single-threaded process.  NT's
 * NtTerminateProcess(NtCurrentProcess(), code), the call this replaces,
 * always tears down every thread of the process at once (see
 * src/signal/nt/plat_signal.c's own comment on __plat_thread_start()),
 * so exit_group(2) -- the whole-process exit -- is the faithful match,
 * not the per-thread exit(2) syscall the same syscall number space also
 * offers.
 */
#include "plat_exit.h"

/* aarch64 Linux syscall number (confirmed via a throwaway host program
 * printing SYS_exit_group from <sys/syscall.h>, the same oracle
 * technique src/mman/linux/plat_mem.c's banner describes). */
#define SYS_exit_group 94

extern long syscall(long number, ...);

_Noreturn void __plat_terminate(int code)
{
	/* Retries forever on the vanishingly unlikely chance the first
	 * attempt does not immediately end the process -- the same
	 * defensive loop the NT backend keeps (src/exit/nt/plat_exit.c),
	 * and required here too: this function is _Noreturn, so the
	 * compiler must see a path that provably never falls off the end. */
	for (;;) syscall(SYS_exit_group, code);
}
