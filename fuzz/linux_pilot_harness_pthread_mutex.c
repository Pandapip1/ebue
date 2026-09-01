/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test-harness scaffolding for the pthread_mutex_t front-door pilot --
 * NOT part of ntlibc, same standing as every other fuzz/
 * linux_pilot_harness_*.c file.
 *
 * src/thread/pthread_mutex.c's mutex_acquire() calls __sig_drain_pending()
 * unconditionally at its very top, and src/thread/pthread.c's
 * __pthread_current() calls __sig_current_mask_copy() -- both real
 * functions in src/signal/{signal,sigdelivery}.c, both genuinely
 * NT-specific (sigdelivery.c's own cross-process signal transport is a
 * separately-scoped, deliberately-deferred redesign -- see the
 * misc/select/signal Linux backend's own commit for why: "genuinely
 * NT-object-manager-shaped RPC... a real redesign, not a syscall
 * swap"). Stubbed here, LOCAL TO THIS TEST HARNESS ONLY, exactly the
 * same shape as every other Linux pilot's stand-ins for a dependency
 * genuinely out of THIS task's scope: "no pending signals to drain,
 * ever" and "an empty blocked-signal mask" are both correct, honest
 * answers for a process with no real signal-delivery infrastructure
 * running at all on this backend yet -- not a shortcut around
 * something this test actually needs, since it raises no signals.
 */
#include <signal.h>
#include <sched.h>
#include <errno.h>
#include "libc.h"

void __sig_drain_pending(void)
{
}

void __sig_current_mask_copy(sigset_t *mask)
{
	int i;
	unsigned char *p = (unsigned char *)mask;
	for (i = 0; i < (int)sizeof *mask; i++) p[i] = 0;
}

/* calloc()/free(): src/thread/pthread.c's __pthread_current() needs
 * calloc() for its per-thread control block. ntlibc's own real
 * malloc.c is built entirely on NT's process heap
 * (RtlAllocateHeap/RtlFreeHeap via __peb) -- porting a real allocator
 * is an entire separate subsystem, genuinely out of scope for proving
 * the mutex works. A fixed-size static pool is enough for this test
 * (one real OS thread's __pthread_current() call each, NTHREADS+1
 * total, never freed -- this test does not exercise thread exit at
 * all): honest for what it is, not a general allocator standing in
 * for one. */
#define POOL_SLOTS 64
static unsigned char pool[POOL_SLOTS][256];
static int pool_next;

void *calloc(unsigned long nmemb, unsigned long size)
{
	unsigned long total = nmemb * size;
	int i, slot;
	if (total > sizeof pool[0]) return 0;
	/* Real concurrent callers: every worker thread's first
	 * mutex_acquire() calls __pthread_current(), which calls this.
	 * __atomic_fetch_add(), not a plain pool_next++, since this
	 * harness's own stub -- unlike the library code under test -- has
	 * no mutex of its own to protect it with yet. */
	slot = __atomic_fetch_add(&pool_next, 1, __ATOMIC_RELAXED);
	if (slot >= POOL_SLOTS) return 0;
	for (i = 0; i < (int)sizeof pool[0]; i++) pool[slot][i] = 0;
	return pool[slot];
}

void free(void *p)
{
	(void)p; /* never freed in this test, see the comment above */
}

/* sched_get_priority_min()/_max() -- src/thread/pthread_mutex.c's
 * mutexattr defaults call these. Real bodies, matching src/misc/
 * sched.c's own values exactly (SCHED_PRIORITY_MAX=31, priority_min()
 * is 0 for SCHED_OTHER and 1 for everything else) -- linking the real
 * sched.c file directly would additionally pull in process_exists()/
 * __child_find()/__plat_process_open_checked()/__plat_process_alive(),
 * a whole separate subsystem this test has no business standing up
 * just to reach two three-line functions. Duplicated here rather than
 * shared, matching the same judgment call every Linux backend file in
 * this tree already makes about its own syscall-number tables. */
int sched_get_priority_max(int policy)
{
	(void)policy;
	return 31;
}

int sched_get_priority_min(int policy)
{
	return policy == SCHED_OTHER ? 0 : 1;
}

/* The rest of src/thread/pthread.c beyond __pthread_current() --
 * pthread_join()/_detach()/_exit()/_getschedparam()/_setschedparam()/
 * _setschedprio() and finish()'s TSD-destructor-running path -- is
 * explicitly out of scope for this pass (see
 * __pthread_current()'s own updated comment in pthread.c). None of it
 * is called by this test, but pthread.c is linked here as a whole
 * object (not pulled from a .a archive by need), so GNU ld resolves
 * every symbol those functions reference regardless -- the same
 * "linker still needs a real symbol even for a runtime-dead branch"
 * situation every other Linux pilot's harness in this tree documents.
 * RtlAcquirePebLock()/RtlReleasePebLock() themselves are stubbed here
 * (not routed through __plat_fast_lock()) deliberately: they are the
 * literal NT API names these OTHER, unported functions call directly,
 * and giving them real bodies would be pretending those functions are
 * portable when they are not -- a no-op is the honest stand-in for
 * "this code path never actually runs in this backend yet."
 *
 * RtlAcquirePebLock()/RtlReleasePebLock() are MACROS in libc.h (Thread
 * Safety Analysis wrappers around the two real raw ntdll functions of
 * the same name, self-referencing during expansion -- see libc.h's own
 * comment), active in this file too since it includes libc.h. #undef
 * first: the stub bodies below need the plain, real ntdll symbol
 * names the macro's own expansion resolves to, the same shape of
 * stand-in fuzz/ntstubs.c already provides for every other raw ntdll
 * call this project's native (non-Windows) builds need. */
#undef RtlAcquirePebLock
#undef RtlReleasePebLock
void RtlAcquirePebLock(void) { }
void RtlReleasePebLock(void) { }
void __pthread_cancel_defer_enter(void) { }
void __pthread_cancel_defer_leave(void) { }
void __pthread_run_specific_destructors(void) { }

/* __sig_current_mask_install(): pthread_create()'s own path
 * (__pthread_adopt_current(), src/thread/pthread.c), out of scope for
 * the same reason __sig_current_mask_copy() above is -- unreached by
 * this test, which spawns workers via __plat_thread_spawn() directly,
 * never pthread_create(). */
void __sig_current_mask_install(const sigset_t *mask) { (void)mask; }

/* __plat_thread_resume(): declared in plat_thread.h but owned by the
 * process subsystem's backend (src/process/nt/plat_process.c on NT,
 * src/process/linux/plat_process.c on Linux -- see this project's own
 * cross-session ownership history for __plat_process_resume()/
 * __plat_thread_resume()/__plat_event_set(), each declared in two
 * headers with one canonical owner). Not linking the real process
 * backend here (it would pull in fork/exec/clone machinery this test
 * has no use for) -- pthread_create() itself is out of scope (see
 * this file's own banner), so this call site is never reached at
 * runtime; a real implementation would just be `return 0` regardless
 * (Linux's own __plat_process_linux.c reports the identical no-op:
 * nothing __plat_thread_spawn() creates is ever suspended). */
int __plat_thread_resume(__plat_handle_t th) { (void)th; return 0; }
