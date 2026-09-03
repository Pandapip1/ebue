/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __plat_thread_alertable_yield() and __raise_internal() USED TO be
 * defined here too, standing in for the full src/thread/linux/
 * plat_thread.c and src/signal/signal.c on the x86_64/i386 cross pilots
 * the same way __mq_fd_closed() below still does for its own
 * still-unported subsystem -- NOT because either function was hard to
 * port, but because the REST of each file (futex-based mutexes, clone(2)
 * thread creation, semaphores, ... / real signal dispatch, sigaction,
 * altstack, ...) used to be a much larger, genuinely separate porting
 * job. Both whole files are now really ported for x86_64/i386
 * (src/thread/linux/plat_thread.c and src/signal/signal.c +
 * src/signal/linux/sigdelivery.c + src/signal/linux/plat_signal.c +
 * src/signal/$arch/altstack.S) and are in tools/linux-build-dlfcn-
 * cross.sh's own FILES list -- so both stand-ins that used to live here
 * were removed, not left duplicating the real definitions (a real,
 * reproduced `duplicate symbol` otherwise). __mq_fd_closed() below is
 * NOT removed: mqueue.c's own much larger subsystem (not just its
 * plat_*.c backend) is still genuinely out of this pass's scope.
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

/* __raise_internal() USED TO be stubbed here too, for the identical
 * reason __plat_thread_alertable_yield() used to be (see this file's
 * own banner): src/signal/signal.c's real definition reached into
 * src/signal/linux/plat_signal.c, not yet ported for this arch. That is
 * no longer true -- plat_signal.c is genuinely ported for x86_64/i386
 * now (tools/linux-build-crt-cross.sh proves it) -- so src/signal/
 * signal.c itself, src/signal/linux/sigdelivery.c and src/signal/$arch/
 * altstack.S are in this script's own FILES list for real, and this
 * stub was removed rather than left duplicating the real definition (a
 * real, reproduced `duplicate symbol: __raise_internal` otherwise). */
