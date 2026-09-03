/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __plat_thread_alertable_yield() USED TO be defined here too, standing
 * in for the full src/thread/linux/plat_thread.c on the x86_64/i386
 * cross pilots the same way __mq_fd_closed()/__raise_internal() below
 * still do for their own still-unported subsystems -- NOT because that
 * one function was hard to port, but because the REST of that file
 * (futex-based mutexes, clone(2) thread creation, semaphores, ...) used
 * to be a much larger, genuinely separate porting job. That whole file
 * is now really ported for x86_64/i386 (src/thread/linux/plat_thread.c
 * itself, including a real __plat_thread_alertable_yield()) and is in
 * tools/linux-build-dlfcn-cross.sh's own FILES list -- so the stand-in
 * that used to live here was removed, not left duplicating the real
 * definition (a real, reproduced `duplicate symbol:
 * __plat_thread_alertable_yield` otherwise, the identical class of
 * collision this file's own __raise_internal() comment below already
 * warns signal.c itself would cause if it were ever added alongside
 * this file). __mq_fd_closed() and __raise_internal() below are NOT
 * removed: mqueue.c's/signal.c's own much larger subsystems (not just
 * their plat_*.c backends) are still genuinely out of this pass's scope,
 * unlike plat_thread.c which this pass's own FILES list now links for
 * real in full.
 */
#include "plat_thread.h"

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
