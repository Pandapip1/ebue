/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A real, correct __plat_thread_alertable_yield() (src/internal/
 * plat_thread.h's declared interface -- a plain sched_yield(2)),
 * standing in for the full src/thread/linux/plat_thread.c on the
 * x86_64/i386 cross pilots (tools/linux-build-dlfcn-cross.sh) -- NOT
 * because this one function is hard to port, but because the REST of
 * that file (futex-based mutexes, clone(2) thread creation, semaphores,
 * ...) is a much larger, genuinely separate porting job this pass's own
 * scope (crt/linux/$(ARCH)/start.S + src/dlfcn/linux/plat_dlfcn.c's
 * relocation handling) never asked for, and pulling in that whole file
 * just to satisfy the one symbol src/internal/plat_malloc_generic.h's
 * ntlibc_malloc_lock() references (only on its CAS-retry path -- never
 * actually reached by this pilot's own single-threaded, uncontended
 * test) would silently expand this pass's footprint well past what it
 * verifies. Kept in fuzz/ rather than src/thread/linux/, deliberately:
 * this is test-harness scaffolding standing in for an unported file,
 * not a (partial, therefore misleading) arch port of that file itself.
 *
 * Real code, not a stub: a genuine sched_yield(2) syscall, the exact
 * same job src/thread/linux/plat_thread.c's own aarch64 version does,
 * just this pass's own two new arches' syscall numbers (confirmed
 * against arch/x86/entry/syscalls/syscall_{64,32}.tbl, the same oracle
 * every other syscall number in this pass's own commits came from).
 */
#include "plat_thread.h"

#if defined(__x86_64__)
void __plat_thread_alertable_yield(void)
{
	__asm__ volatile("syscall" : : "a"(24) : "rcx", "r11", "memory");
}
#elif defined(__i386__)
void __plat_thread_alertable_yield(void)
{
	__asm__ volatile("int $0x80" : : "a"(158) : "memory");
}
#else
#error "linux_pilot_dlfcn_cross_yield.c: unsupported architecture"
#endif

/* __mq_fd_closed() (src/thread/mqueue.c's own declared interface,
 * called unconditionally from the public close() front door,
 * src/unistd/close.c, so any fd this test closes needs it to exist) --
 * a real, correct no-op here, not a stub standing in for unwritten
 * behavior: this curated cross build links no message-queue subsystem
 * at all (src/thread/mqueue.c's own dependencies reach straight into
 * src/thread/linux/plat_thread.c's futex/semaphore machinery -- the
 * same large, genuinely separate, not-yet-ported-for-this-arch file
 * this same rationale already applies to above), so there is no open
 * mqueue descriptor this fd could ever be associated with to clean up.
 * Exactly fdpos.c's own precedent (src/internal/linux/fdpos.c's own
 * banner: "a real, correct no-op for this platform, not a stub"),
 * just here because the mqueue subsystem is unported rather than
 * because the underlying kernel operation is unneeded. */
void __mq_fd_closed(int fd)
{
	(void)fd;
}

/* __raise_internal() (src/signal/signal.c's own declared interface,
 * reached from src/misc/resource.c's __fsize_exceeded() -- itself only
 * reached when write()/pwrite() sees RLIMIT_FSIZE actually exceeded,
 * which never happens in this pilot's own test: nothing here sets a
 * file-size limit, and the memory-only FILE vsnprintf()/dlerror() write
 * through never reaches write()'s own real-fd code path at all -- see
 * src/unistd/write.c's own write()). Real signal delivery (src/signal/
 * signal.c) is a large, genuinely separate subsystem this pass's own
 * scope never asked for (its own dependency chain reaches into
 * src/signal/linux/plat_signal.c, not yet ported for this arch, the
 * same class of gap this file's own __plat_thread_alertable_yield/
 * __mq_fd_closed already stand in for above) -- disclosed here rather
 * than silently deepened. Returning 0 (success, no error) is the
 * correct behavior for a code path this build can prove is never
 * actually exercised, not a claim about what a real port would do. */
int __raise_internal(int sig)
{
	(void)sig;
	return 0;
}
