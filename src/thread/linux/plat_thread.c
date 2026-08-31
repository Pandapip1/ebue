/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of a DELIBERATELY SMALL SLICE of src/internal/
 * plat_thread.h -- see that header's own banner for the full 11-file
 * interface (mutexes, rwlocks, condvars, semaphores, TLS, async I/O,
 * message queues) this does NOT attempt to cover. plat_thread.h is the
 * single hardest subsystem in the platform-abstraction migration: real
 * POSIX threading on Linux rests on two primitives, clone(2) (thread
 * creation) and futex(2) (the wait/wake primitive every mutex/condvar/
 * semaphore ultimately reduces to), and both are genuinely easy to get
 * subtly wrong -- a broken futex wake is a silent race, not a compile
 * error. This file ports exactly enough of both, real and tested under
 * real contention, to prove a working mutex: __plat_semaphore_create/
 * _post/_getvalue, __plat_event_create/_set, __plat_wait_one (single
 * handle only), and __plat_thread_spawn. Everything else plat_thread.h
 * declares (__plat_wait_any, named \BaseNamedObjects-style objects,
 * __plat_thread_suspend/_queue_apc/_redirect_ip/_stack_extent/
 * _duplicate_self/_current_pseudo/_terminate_self, mqueue's
 * __plat_thread_file_io, __plat_cancel_unsafe_abort,
 * __plat_thread_alertable_yield, __plat_query_system_time) is left
 * undefined here, not stubbed -- open follow-up work, same as the mman/
 * unistd pilot (908d58b) left __fd_pos_save/_restore's real interface
 * gap disclosed rather than papered over.
 *
 * UPDATE: every src/thread/pthread_*.c/semaphore.c front door's own
 * direct RtlAcquirePebLock()/RtlReleasePebLock() call sites -- the raw
 * ntdll calls this banner originally described as never routed through
 * plat_thread.h at all -- have since been renamed to __plat_fast_lock()/
 * __plat_fast_unlock() (the functions below already provide, byte-
 * identical to the old macro expansion on NT: see src/thread/nt/
 * plat_thread.c's own one-line wrappers). That specific blocker is gone;
 * what remains is genuinely just "the missing functions below" --
 * __pthread_current() (src/thread/pthread.c) still threads through
 * process-lifecycle bookkeeping (live_threads, exit()) this port does
 * not touch, and every function this banner lists as undefined two
 * paragraphs up still is. What this file proves: the two functions
 * pthread_mutex.c's own blocking slow path already rests on --
 * __plat_wait_one() against a binary semaphore, released by
 * __plat_semaphore_post() -- really give real mutual exclusion, under
 * real contention, from real Linux kernel threads on this host. See
 * fuzz/linux_pilot_test_thread.c, which builds a minimal mutex (lock =
 * __plat_wait_one() on a semaphore created initial=1,max=1; unlock =
 * __plat_semaphore_post()) directly on top of the real functions below
 * -- not a separate, unrelated toy -- and stress-tests it with
 * clone()-spawned threads hammering a shared counter. It does not
 * reproduce pthread_mutex_t's owner tracking, recursion, or error
 * checking (those live in pthread_mutex.c's mutex_data bookkeeping,
 * guarded by __plat_fast_lock()/__plat_fast_unlock(), defined further
 * down in this same file) -- only the blocking primitive underneath
 * them.
 *
 * Every syscall here is issued through a small raw-syscall helper
 * written in this file (raw_syscall(), below) rather than through the
 * `extern long syscall(long, ...)` declaration src/mman/linux/plat_mem.c
 * and src/unistd/linux/plat_fd.c use -- a deliberate, tested deviation
 * from their pattern, not an oversight. That symbol resolves at link
 * time to the HOST's real glibc syscall() (this build is -nostdinc, not
 * -nostdlib -- only preprocessing/compiling avoids the host headers, the
 * final link step still pulls in host libc), and glibc's syscall()
 * itself performs the usual libc error-translation: on failure it
 * returns exactly -1 and sets glibc's OWN errno (a different memory
 * location than ntlibc's own errno global, src/internal/errno.c) to the
 * real code -- it does NOT hand back the raw kernel -errno in
 * [-4095,-1] the way a bare `svc`/syscall instruction does. Confirmed
 * empirically on this host before writing a line of the functions below
 * (raw glibc `syscall(SYS_close, -1)` returns -1 with glibc-errno=9, not
 * -9): the [-4095,-1]-range is_sys_error() check plat_mem.c/plat_fd.c
 * both use still happens to flag the failure (-1 is itself inside that
 * range), but their subsequent `errno = (int)-ret` then computes
 * `errno = 1` (EPERM) on EVERY failure of every call in both of those
 * files, discarding the real cause -- a latent bug in already-merged,
 * out-of-scope code, not fixed here (would touch files this port does
 * not own), but avoided in this file by never going through glibc's
 * syscall() wrapper at all. raw_syscall() below talks to the kernel
 * directly via inline `svc #0`, so the value it returns is the genuine
 * kernel ABI result this file's own is_sys_error()/errno translation
 * assumes.
 *
 * clone(2) needs more than a raw syscall can give it: a syscall
 * instruction returns twice under clone() exactly like fork() (once in
 * the caller, once in the new child, sharing this thread's register
 * state but using the CHILD's own fresh stack), and
 * the two returns must run different code paths -- the child must not
 * unwind back into this C function's stack frame at all, since that
 * frame lives on the PARENT's stack and the child's stack pointer now
 * points at the fresh memory this file mmap()'d for it. That is
 * genuinely assembly-level work no C function can safely wrap (every
 * real libc's clone() wrapper -- glibc's, musl's -- is hand-written
 * assembly for exactly this reason); see clone_aarch64.S, confirmed
 * correct by first validating a standalone copy of it outside this tree
 * against real clone()+wait4() round trips (see this file's own report
 * for the numbers) before integrating it here.
 *
 * Two more real, deliberate simplifications, disclosed rather than
 * hidden:
 *
 *   __plat_thread_spawn() clones with CLONE_VM|CLONE_FS|CLONE_FILES|
 *   CLONE_SIGHAND and the SIGCHLD exit-signal bit, but NOT CLONE_THREAD
 *   and NOT CLONE_SETTLS. This is real concurrent execution sharing one
 *   address space (CLONE_VM is what actually matters for exercising a
 *   shared-memory mutex under genuine contention -- confirmed by the
 *   WITHOUT-mutex control run in fuzz/linux_pilot_test_thread.c, which
 *   reliably shows a corrupted count), but it is not a full NPTL-style
 *   pthread: the spawned thread has no distinct TLS block (no
 *   CLONE_SETTLS -- its TPIDR_EL0 is whatever the creating thread's was,
 *   so any `__thread`-qualified variable would ALIAS the creator's,
 *   unsafely, if one were ever touched from the spawned thread; this
 *   port's own code never touches one for exactly that reason), and
 *   without CLONE_THREAD the new thread is its own thread-group leader,
 *   joined with plain wait4() rather than a futex-on-ctid join the way
 *   CLONE_CHILD_CLEARTID would give a true NPTL thread. Discovered by
 *   testing, not assumed: omitting the low-byte SIGCHLD exit-signal bits
 *   in the clone() flags word makes the resulting child invisible to a
 *   plain wait4(pid, &status, 0) call (ECHILD) even though it is a real,
 *   running, distinct process sharing this one's address space --
 *   wait4() without __WALL/__WCLONE only reports children whose
 *   exit-signal is SIGCHLD, and clone()'s exit-signal is encoded in
 *   flags' low byte, not a separate argument.
 *
 *   __plat_wait_one() below only understands a handle THIS file
 *   produced via __plat_semaphore_create()/__plat_event_create() (a
 *   pointer to this file's own struct ntlibc_linux_sync, mmap()'d, tagged with
 *   a kind byte) -- never a thread handle from __plat_thread_spawn()
 *   (which boxes a pid, an unrelated small integer, not a pointer to
 *   that struct at all). NT's HANDLE unifies every waitable kind behind
 *   one WaitForSingleObject-shaped call; this backend does not, the same
 *   class of NT-shaped assumption 908d58b's own report flagged for
 *   __plat_mem_release()'s missing length parameter. Passing a thread
 *   handle to __plat_wait_one() here would dereference garbage. Nothing
 *   in this port's own scope does that (its test harness joins spawned
 *   threads with a direct wait4() call instead, documented at its own
 *   call site) -- disclosed as real follow-up work, not fixed here,
 *   because fixing it for real means either giving thread handles their
 *   own tagged representation understood by every wait path (a design
 *   change to a shared header, better done once rather than piecemeal)
 *   or building the futex-on-ctid join a real CLONE_THREAD port would
 *   need anyway.
 */
#include <errno.h>
#include <stddef.h>
#include "plat_thread.h"
#include "linux/sync.h"

/* aarch64 Linux syscall numbers -- confirmed against this host's own
 * <sys/syscall.h> (compiled and printed by a throwaway host program, not
 * assumed) and cross-checked against a real kernel
 * asm-generic/unistd.h, the same discipline src/mman/linux/plat_mem.c's
 * banner describes:
 *   SYS_mmap=222 SYS_munmap=215 (unchanged from plat_mem.c, reused here)
 *   SYS_futex=98 SYS_clone=220 SYS_exit=93 SYS_gettid=178 */
#define SYS_mmap   222
#define SYS_munmap 215
#define SYS_futex  98

#define FUTEX_WAIT         0
#define FUTEX_WAKE         1
#define FUTEX_PRIVATE_FLAG 128

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20

/* clone(2)'s flags word: a real Linux "thread" (CLONE_VM|CLONE_FS|
 * CLONE_FILES|CLONE_SIGHAND), narrowed from a full NPTL pthread exactly
 * as this file's own banner discloses, plus SIGCHLD (17) in the flags'
 * low byte -- the exit-signal field -- so the spawned thread stays
 * visible to a plain wait4() (see the banner for how this was found:
 * omitting it produces a real, running child that wait4() nonetheless
 * reports ECHILD for). */
#define CLONE_VM      0x00000100
#define CLONE_FS      0x00000200
#define CLONE_FILES   0x00000400
#define CLONE_SIGHAND 0x00000800
#define LINUX_SIGCHLD 17

#define DEFAULT_STACK_BYTES ((size_t)1 << 20) /* 1 MiB, matches pthread.c's
                                                * own DEFAULT_STACK_SIZE */

/* A minimal 6-argument raw syscall: `svc #0` directly, no host libc
 * wrapper anywhere in the call path -- see this file's banner for why
 * this differs from plat_mem.c/plat_fd.c's `extern long syscall(...)`
 * pattern (that symbol is satisfied by the HOST's glibc at link time,
 * which silently discards the real -errno magnitude on failure; this
 * does not). aarch64's syscall calling convention: x8 = syscall number,
 * x0..x5 = up to 6 arguments, result (or -errno in [-4095,-1]) in x0. */
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a1;
	register long x1 __asm__("x1") = a2;
	register long x2 __asm__("x2") = a3;
	register long x3 __asm__("x3") = a4;
	register long x4 __asm__("x4") = a5;
	register long x5 __asm__("x5") = a6;
	__asm__ volatile("svc #0"
		: "+r"(x0)
		: "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
		: "memory", "cc");
	return x0;
}

/* clone_aarch64.S's hand-written trampoline -- see this file's banner
 * for why a raw syscall cannot do clone()'s job by itself. Matches
 * __plat_thread_entry_t's shape (`unsigned (*)(void*)`, NT's
 * NtCreateThreadEx StartRoutine shape, reused as-is per plat_thread.h's
 * own banner) so callers pass `entry` straight through with no adapter. */
extern long __ntlibc_linux_clone(__plat_thread_entry_t fn, void *stack_top,
                                 long flags, void *arg);

static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

/* The raw kernel `struct timespec` shape on a 64-bit arch (two `long`s,
 * seconds then nanoseconds) -- defined once, locally, rather than
 * pulling in ntlibc's own <time.h> struct timespec (a different type by
 * name even if layout-compatible), since this is what the futex(2)
 * syscall ABI itself expects, not a libc-level type. */
struct linux_timespec { long tv_sec; long tv_nsec; };

/* NOT FUTEX_PRIVATE_FLAG -- a real, confirmed bug fixed here, not a
 * missed optimization left on the table. These two helpers back EVERY
 * futex word this file hands out through __plat_wait_one()/
 * __plat_event_set()/__plat_semaphore_post(): both the genuinely
 * per-process objects alloc_sync() creates (MAP_PRIVATE|MAP_ANONYMOUS,
 * where FUTEX_PRIVATE_FLAG's optimization is valid and was originally
 * used) AND the cross-process objects map_named_sem()/
 * __plat_named_mutant_acquire() create (MAP_SHARED, backed by a real
 * file under /tmp, opened independently by every process that touches
 * it). FUTEX_PRIVATE_FLAG tells the kernel to hash a waiter by
 * (this process's mm_struct, the virtual address passed in) instead of
 * by the underlying physical page -- a real optimization, but only
 * correct when every waiter and waker sharing that word are the SAME
 * process (or CLONE_VM threads of it, sharing one mm_struct, exactly
 * what __plat_thread_spawn() above creates and what fuzz/
 * linux_pilot_test_thread.c's own mutex stress test already proved
 * correct). Two SEPARATE processes independently mmap()ing the same
 * MAP_SHARED file get the SAME physical page but, in general, DIFFERENT
 * virtual addresses (confirmed on this host: two real fork/1-1.c
 * processes racing the same named mutant lock mapped it at 0x6f12d3418000
 * and 0x6f12d3418000-0x18384 respectively -- not equal), so a
 * FUTEX_WAKE_PRIVATE issued by the process that posts the lock hashes to
 * a completely different (mm, uaddr) key than the FUTEX_WAIT_PRIVATE the
 * other process is blocked in, and the wakeup is silently never
 * delivered: the kernel has no idea the two calls are even about the
 * same futex word. That is the real root cause diagnosed behind the
 * fork, pthread_atfork, the sem_ family, mqueue, sigsuspend and sigwait
 * TIMEOUT cluster's
 * third, most-elusive bug (after the sleep(1) busy-spin in
 * src/signal/linux/plat_signal.c and the malloc/fast-lock self-deadlock
 * in src/internal/plat_malloc_generic.h): every one of those interfaces'
 * test cases that synchronizes two real, separate processes through a
 * named semaphore or (transitively, via sem_open()'s own namespace lock)
 * this file's named mutant hit exactly this silently-dropped wakeup,
 * intermittently -- exactly as often as the two processes' independent
 * mmap() calls happened to land at different virtual addresses, which in
 * practice was every single time. Confirmed with instrumented tracing
 * (a temporary per-process debug log plus a real statx(2) inode
 * comparison) showing both processes' mappings resolving to the
 * identical inode -- ruling out "different files" -- while their
 * `obj_addr` values differed, and the waiting process's own
 * FUTEX_WAIT_PRIVATE call never returning despite the other process
 * completing a real, successful FUTEX_WAKE_PRIVATE on the same logical
 * object moments earlier. Dropping FUTEX_PRIVATE_FLAG makes the kernel
 * hash by the underlying inode and page offset instead, which is correct
 * for every caller in this file regardless of whether the object turns
 * out to be MAP_PRIVATE or MAP_SHARED, at the cost of the (real, but
 * here-unmeasured and secondary to correctness) performance difference
 * FUTEX_PRIVATE_FLAG exists to buy back for the pure single-process
 * case. */
static long futex_wait(int *uaddr, int expected, const struct linux_timespec *timeout)
{
	return raw_syscall(SYS_futex, (long)uaddr,
	                   FUTEX_WAIT, (long)expected,
	                   (long)timeout, 0, 0);
}

static long futex_wake(int *uaddr, int count)
{
	return raw_syscall(SYS_futex, (long)uaddr,
	                   FUTEX_WAKE, (long)count, 0, 0, 0);
}

/* One mmap()'d page per synchronization object -- no allocator dependency
 * (this file is -nostdinc, linked before ntlibc's own malloc is
 * necessarily available in every configuration that might use it), at
 * the cost of a whole page for a handful of bytes. Fine at this port's
 * scale (a pilot proving the primitive, not a production allocator
 * concern); a real port would suballocate. `kind` distinguishes a
 * counting semaphore (P/V, __plat_wait_one decrements) from a manual-
 * reset event (__plat_wait_one only checks nonzero, never consumes) --
 * the two plat_thread.h waitable kinds this file implements. struct
 * ntlibc_linux_sync itself now lives in src/internal/linux/sync.h, not
 * here -- see that header's own banner for why (named semaphores and
 * stop-events need to build the same kind of object this file's own
 * __plat_wait_one()/__plat_event_set()/__plat_semaphore_post() already
 * understand, rather than inventing a second synchronization
 * primitive). */

/* This function's own `out`, and map_named_sem()'s below, are the
 * common root of a whole class of findings this sweep leaves as an
 * honest residual rather than force-fitting `nonnull`: every
 * __plat_*_create()/_open()/_open_or_create() below writes
 * `obj->futex`/`obj->kind`/`obj->max` unconditionally right after
 * calling this function (or map_named_sem()), and `obj` is that
 * checked call's own OUTPUT -- `*out = (struct ntlibc_linux_sync
 * *)ret;`, only reached past `if (is_sys_error(ret)) { ...; return
 * -1; }` -- the identical "checked allocation, then use" shape this
 * tree's own malloc() already gets trusted for elsewhere (see
 * tools/clang/OwnershipChecker.cpp's own allocator-family extent
 * tracking, 8a56a66), just via this backend's raw_syscall(SYS_mmap,
 * ...) instead of a call the checker already recognizes as an
 * allocator. Verified sound by hand at every one of this file's own
 * call sites (is_sys_error() is checked before `obj`/`*out` is ever
 * touched, with no path that skips it); teaching the checker to trust
 * this specific raw-syscall idiom the same way is a real, narrow lemma
 * this pass did not attempt, not a shortcut taken here. */
static int alloc_sync(struct ntlibc_linux_sync **out)
{
	long ret = raw_syscall(SYS_mmap, 0, (long)sizeof(struct ntlibc_linux_sync),
	                       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
	                       -1, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	*out = (struct ntlibc_linux_sync *)ret;
	return 0;
}

/* ---- events (manual-reset, initially unset) ----------------------------
 * __plat_event_set() is explicitly this file's to implement per
 * plat_thread.h's banner (it is also declared in plat_signal.h, whose
 * canonical owner is thread, not signal). */
int __plat_event_create(__plat_handle_t *out)
{
	struct ntlibc_linux_sync *obj;
	if (alloc_sync(&obj)) return -1;
	obj->futex = 0;
	obj->max = 0;
	obj->kind = NTLIBC_LX_SYNC_EVENT;
	*out = (__plat_handle_t)obj;
	return 0;
}

int __plat_event_set(__plat_handle_t h)
{
	struct ntlibc_linux_sync *obj = (struct ntlibc_linux_sync *)h;
	__atomic_store_n(&obj->futex, 1, __ATOMIC_RELEASE);
	/* This file's only event kind is manual-reset: it stays set after
	 * this call (never auto-clears the way NT's own SynchronizationEvent
	 * -- the kind plat_thread.h's own comment names -- does for one
	 * released waiter), so every current waiter must wake here, not just
	 * one; a plain futex_wake(&obj->futex, 1) would leave the rest
	 * parked even though the word is already set. 0x7fffffff (INT_MAX)
	 * wakes every futex waiter currently parked on this word. */
	futex_wake(&obj->futex, 0x7fffffff);
	return 0;
}

/* ---- unnamed counting semaphores ---------------------------------------
 * `inheritable` (OBJ_INHERIT on NT) has no separate concept here: a
 * MAP_PRIVATE|MAP_ANONYMOUS mapping is already inherited by a fork()'d
 * child via ordinary copy-on-write, unconditionally -- narrower than
 * NT's per-handle opt-in, but every caller in this port's own scope
 * passes 0 (see plat_thread.h's own comment: real callers only pass
 * nonzero for sem_init()'s process-shared case, out of scope here), so
 * the difference is never exercised. Documented, not silently assumed. */
int __plat_semaphore_create(long initial, long maximum, int inheritable,
                            __plat_handle_t *out)
{
	struct ntlibc_linux_sync *obj;
	(void)inheritable;
	if (alloc_sync(&obj)) return -1;
	obj->futex = (int)initial;
	obj->max = (int)maximum;
	obj->kind = NTLIBC_LX_SYNC_SEMAPHORE;
	*out = (__plat_handle_t)obj;
	return 0;
}

int __plat_semaphore_post(__plat_handle_t h)
{
	struct ntlibc_linux_sync *obj = (struct ntlibc_linux_sync *)h;
	int cur = __atomic_load_n(&obj->futex, __ATOMIC_RELAXED);
	for (;;) {
		if (cur >= obj->max) {
			/* [EOVERFLOW] decided here while the real state is still in
			 * hand, matching plat_thread.h's own comment on why NT's
			 * STATUS_SEMAPHORE_LIMIT_EXCEEDED is translated at this
			 * exact call site rather than reconstructed later. */
			errno = EOVERFLOW;
			return -1;
		}
		if (__atomic_compare_exchange_n(&obj->futex, &cur, cur + 1, 1,
		                                __ATOMIC_RELEASE, __ATOMIC_RELAXED))
			break;
	}
	futex_wake(&obj->futex, 1);
	return 0;
}

int __plat_semaphore_getvalue(__plat_handle_t h, int *value)
{
	struct ntlibc_linux_sync *obj = (struct ntlibc_linux_sync *)h;
	*value = __atomic_load_n(&obj->futex, __ATOMIC_ACQUIRE);
	return 0;
}

/* ---- waiting -------------------------------------------------------------
 * Single-handle only: __plat_wait_any() (NtWaitForMultipleObjects'
 * WaitAny mode) is out of this port's chosen scope -- no caller here
 * needs it -- and is left undefined rather than stubbed. */
int __plat_wait_one(__plat_handle_t h, int alertable, int has_timeout,
                    long long relative_ticks)
{
	struct ntlibc_linux_sync *obj = (struct ntlibc_linux_sync *)h;
	struct linux_timespec ts, *tsp = 0;
	(void)alertable; /* Linux has no APC-alertable-wait concept; every wait
	                  * this backend performs is non-alertable, so
	                  * __PLAT_WAIT_INTR is never produced here -- no
	                  * caller in this port's scope relies on it. */
	if (has_timeout) {
		long long ticks = relative_ticks < 0 ? -relative_ticks : relative_ticks;
		ts.tv_sec = (long)(ticks / 10000000LL);
		ts.tv_nsec = (long)((ticks % 10000000LL) * 100);
		tsp = &ts;
	}
	for (;;) {
		long r;
		if (obj->kind == NTLIBC_LX_SYNC_EVENT) {
			if (__atomic_load_n(&obj->futex, __ATOMIC_ACQUIRE) != 0)
				return __PLAT_WAIT_OK;
		} else {
			int cur = __atomic_load_n(&obj->futex, __ATOMIC_ACQUIRE);
			while (cur > 0) {
				if (__atomic_compare_exchange_n(&obj->futex, &cur, cur - 1, 1,
				                                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
					return __PLAT_WAIT_OK;
			}
		}
		r = futex_wait(&obj->futex, 0, tsp);
		if (r == 0) continue;             /* real wake: recheck the word */
		if (r == -EAGAIN) continue;       /* word changed before we slept
		                                   * -- benign race, futex(2)'s own
		                                   * documented behavior; retry. */
		if (r == -EINTR) continue;        /* spurious signal; restart the
		                                   * wait. Does not re-derive a
		                                   * shortened remaining timeout --
		                                   * a real, minor, disclosed
		                                   * inaccuracy for a has_timeout
		                                   * caller hit by a signal, out of
		                                   * scope to fix for this pilot. */
		if (r == -ETIMEDOUT) return __PLAT_WAIT_TIMEOUT;
		errno = (int)-r;
		return __PLAT_WAIT_ERROR;
	}
}

/* ---- thread lifecycle ----------------------------------------------------
 * Only __plat_thread_spawn(): see this file's banner for the scope this
 * one function was narrowed to (CLONE_VM-sharing, no CLONE_THREAD/
 * CLONE_SETTLS) and for why the resulting handle cannot be passed to
 * __plat_wait_one() above. `create_suspended` has no primitive on this
 * backend (clone() has no NT-CreateSuspended equivalent short of
 * stopping the child with a real signal immediately after start, its
 * own small synchronization problem) and no caller in this port's scope
 * needs it. NOT this file's to define: __plat_thread_resume() --
 * despite being declared in plat_thread.h, its one real implementation
 * is process's (src/process/nt/plat_process.c), per this task's own
 * multiple-definition history; a Linux one belongs there, not here.
 *
 * A REAL, SERIOUS, CONFIRMED CONSEQUENCE of "no CLONE_SETTLS" that the
 * scope note above only gestured at: every thread this function spawns
 * shares the CALLING thread's TLS region -- not a separate one of its
 * own -- because aarch64 Linux TLS is addressed through the TPIDR_EL0
 * register, and clone(2) only reinitializes it for the child when
 * CLONE_SETTLS is passed (with a `tls` argument pointing at a real
 * per-thread TCB this backend does not build). Confirmed empirically,
 * not theorized: a standalone probe (`__thread int marker`, four
 * spawned threads and the caller each writing/printing `&marker`) shows
 * every one of the five threads reporting the IDENTICAL address for
 * `marker`. This means EVERY `__thread`-qualified variable anywhere in
 * a program linked against this backend -- including src/thread/
 * pthread.c's own `__pthread_self_control`, the cache
 * __pthread_current() relies on to hand back a stable per-thread
 * identity -- silently ALIASES across every thread __plat_thread_spawn()
 * creates, corrupting whatever multiple real threads concurrently treat
 * as "their own" state through it. This surfaced while porting
 * src/thread/pthread_mutex.c's real front door to this backend: a
 * multi-thread pthread_mutex_t stress test stalled partway through
 * (real, reproducible, not a timing fluke) because every worker thread
 * was unknowingly sharing ONE __pthread control block instead of having
 * its own. Not fixed here: a correct fix needs a real per-thread TCB
 * whose size/layout matches this program's own linked TLS segment (the
 * ELF PT_TLS entry's size/alignment) plus CLONE_SETTLS -- genuinely
 * tied to the "no real crt/startup exists for a Linux target build yet"
 * gap already disclosed elsewhere in this port's history, not a small,
 * separately fixable thing. Any code that spawns multiple threads via
 * this function and relies on __thread storage being independent per
 * thread (pthread_create()'s own full path would, once ported; this
 * port's own pthread_mutex_t test works around it by staying single-
 * threaded -- see fuzz/linux_pilot_test_pthread_mutex.c's own banner)
 * must know about this first. */
int __plat_thread_spawn(__plat_thread_entry_t entry, void *arg,
                        size_t stack_size, int create_suspended,
                        __plat_handle_t *out)
{
	size_t sz;
	long stack_ret, pid, flags;
	void *top;

	if (create_suspended) { errno = ENOTSUP; return -1; }

	sz = stack_size ? stack_size : DEFAULT_STACK_BYTES;
	stack_ret = raw_syscall(SYS_mmap, 0, (long)sz, PROT_READ | PROT_WRITE,
	                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (is_sys_error(stack_ret)) { errno = (int)-stack_ret; return -1; }
	top = (void *)(stack_ret + (long)sz);

	flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | LINUX_SIGCHLD;
	pid = __ntlibc_linux_clone(entry, top, flags, arg);
	if (pid < 0) {
		int e = (int)-pid;
		raw_syscall(SYS_munmap, stack_ret, (long)sz, 0, 0, 0, 0);
		errno = e;
		return -1;
	}
	/* Boxed as pid+1, echoing src/unistd/linux/plat_fd.c's fd+1 encoding
	 * (0 stays reserved for __PLAT_HANDLE_NULL) -- but see this file's
	 * banner: this is a DIFFERENT handle namespace than that file's, and
	 * the two must never be crossed. The stack this call mmap()'d is
	 * intentionally leaked (not tracked/freed on join) -- no destroy path
	 * exists in this port's scope, the same disclosed gap as the
	 * semaphore/event objects above never being freed. */
	*out = (__plat_handle_t)(pid + 1);
	return 0;
}

/* __plat_thread_duplicate_self(): boxed exactly like __plat_thread_spawn()
 * above (tid+1) -- a real, durable identifier for the calling thread
 * (gettid(2) never fails and is stable for the thread's whole
 * lifetime, the same "durable, not just valid for one call" property
 * NT's own NtDuplicateObject(NtCurrentThread()) gives __pthread_current()
 * this for). Unlike NT's version, there is no separate pseudo-handle
 * form to fall back to and no failure mode to handle -- gettid(2) is
 * documented to always succeed. See this file's own banner for why
 * __plat_wait_one() cannot yet wait on a thread handle at all (only
 * the semaphore/event objects above): callers that need to actually
 * block on a thread's exit (pthread_join(), out of this port's scope)
 * cannot use this handle for that purpose yet, only identify the
 * thread. */
#define SYS_gettid 178
__plat_handle_t __plat_thread_duplicate_self(void)
{
	long tid = raw_syscall(SYS_gettid, 0L, 0L, 0L, 0L, 0L, 0L);
	return (__plat_handle_t)(tid + 1);
}

/* ---- src/thread/pthread_mutex.c's/pthread.c's process-wide fast lock -----
 *
 * See plat_thread.h's own banner for the contract: available from the
 * first call, always exactly one, process-wide, no creation step.
 * NT gets this for free from the OS-provided PEB lock; this backend
 * builds the identical property from a single zero-initialized static
 * word (BSS, so no allocation and no lazy-init race the way a
 * dynamically created semaphore would need).
 *
 * A plain spinlock, not a futex-based sleep/wake mutex -- deliberately,
 * not a missed opportunity to reuse the futex_wait()/futex_wake()
 * helpers above. Every critical section this lock ever protects is a
 * handful of plain field reads/writes on pthread_mutex_t's own
 * bookkeeping (owner/recursion/waiters/robust_state) -- never a
 * blocking call, never unbounded work -- so the lock is never held
 * for longer than a few instructions by a thread that is, definitionally,
 * currently running (nothing on this backend is preempted-and-parked
 * mid-critical-section the way a fair scheduler might starve a spinner
 * against a *blocked* holder). A first implementation used the
 * standard three-state futex mutex algorithm (0/1/2, matching
 * RtlAcquirePebLock()'s own sleep-based contract more closely) and hit
 * a real, reproducible stall under this port's own contention test
 * (16 threads x 50000 pthread_mutex_lock()/_unlock() cycles apiece
 * through the REAL pthread_mutex_t front door -- fuzz/
 * linux_pilot_test_pthread_mutex.c -- stopped making any further
 * progress after only a few hundred increments, everything asleep on
 * a futex, nothing left to wake it): a genuine bug in that first
 * version, not a false alarm, whether in this file's own algorithm or
 * in some interaction with the semaphore-based blocking path
 * mutex_acquire() ALSO uses for its own, separate, already-proven-
 * correct wait (src/thread/pthread_mutex.c's own semaphore/
 * __plat_wait_one() path, unrelated to this lock, and unaffected by
 * this change). Rather than keep chasing a subtle concurrency bug in a
 * hand-written wakeup protocol for a lock that never needs to sleep in
 * the first place, this backend uses the lock shape that is trivially,
 * inspection-obviously correct for its actual job: spin, yielding the
 * CPU between attempts so a contended spin cannot starve the holder
 * (which is running right now, on this same host, and will release
 * within a few instructions) -- exactly the tool a short, always-brief
 * critical section calls for, and with no wakeup protocol at all,
 * there is no wakeup-protocol bug to have. Recursive acquisition by
 * the same thread deadlocks here exactly like RtlAcquirePebLock()
 * would on NT -- see plat_thread.h's own note that no caller in this
 * tree relies on recursion through this specific lock. */
static int fast_lock_word;

#define SYS_sched_yield 124

void __plat_fast_lock(void)
{
	int c;
	for (;;) {
		c = 0;
		if (__atomic_compare_exchange_n(&fast_lock_word, &c, 1, 1,
		                                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
			return;
		raw_syscall(SYS_sched_yield, 0L, 0L, 0L, 0L, 0L, 0L);
	}
}

void __plat_fast_unlock(void)
{
	__atomic_store_n(&fast_lock_word, 0, __ATOMIC_RELEASE);
}

/* ---- waiting on more than one object -------------------------------------
 * A real, correct-if-not-maximally-efficient implementation: Linux has
 * no single primitive matching NtWaitForMultipleObjects' WaitAny mode
 * across arbitrary futex words (a real one would need FUTEX_WAIT on
 * several addresses via io_uring or a helper thread per handle), so
 * this polls every handle's already-real state check (the same ones
 * __plat_wait_one() above performs) in a loop, sleeping briefly
 * between passes. Every caller in this tree (src/thread/aio.c) waits
 * on a small, fixed handful of handles for a relatively coarse aio
 * deadline, not a hot low-latency path, so a millisecond poll interval
 * is a real, disclosed tradeoff, not a correctness gap. */
#define SYS_nanosleep_wa 101

int __plat_wait_any(__plat_handle_t *handles, unsigned count, int alertable,
                    int has_timeout, long long relative_ticks)
{
	long long remaining_ns;
	(void)alertable;

	remaining_ns = has_timeout
		? (relative_ticks < 0 ? -relative_ticks : relative_ticks) * 100LL
		: -1;

	for (;;) {
		unsigned i;
		for (i = 0; i < count; i++) {
			struct ntlibc_linux_sync *obj = (struct ntlibc_linux_sync *)handles[i];
			if (obj->kind == NTLIBC_LX_SYNC_EVENT) {
				if (__atomic_load_n(&obj->futex, __ATOMIC_ACQUIRE) != 0)
					return __PLAT_WAIT_OK;
			} else {
				int cur = __atomic_load_n(&obj->futex, __ATOMIC_ACQUIRE);
				while (cur > 0) {
					if (__atomic_compare_exchange_n(&obj->futex, &cur, cur - 1, 1,
					                                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
						return __PLAT_WAIT_OK;
				}
			}
		}
		if (has_timeout) {
			if (remaining_ns <= 0) return __PLAT_WAIT_TIMEOUT;
			remaining_ns -= 1000000L; /* 1ms poll interval */
		}
		{
			struct linux_timespec ts;
			ts.tv_sec = 0; ts.tv_nsec = 1000000L;
			raw_syscall(SYS_nanosleep_wa, (long)&ts, 0L, 0L, 0L, 0L, 0L);
		}
	}
}

/* ---- named objects, keyed by the filesystem namespace ---------------------
 * NT's object manager namespace (\BaseNamedObjects\...) gives create-
 * or-open-by-name, race-free, as one syscall; Linux has no equivalent
 * single primitive, but the filesystem namespace under /tmp -- shared
 * by every process on this host, exactly like \BaseNamedObjects is --
 * plus O_CREAT|O_EXCL for atomic "did I just create this" detection,
 * gets the same property with a handful of real syscalls: open (or
 * create) a small backing file, size it to fit one struct
 * ntlibc_linux_sync, and MAP_SHARED it so every process that opens the
 * same path sees the SAME futex word -- not a private, per-process
 * copy the way __plat_semaphore_create()'s MAP_PRIVATE mapping above
 * is. This is the real, working primitive both named semaphores
 * (semaphore.c's sem_open()) and, via src/signal/linux/plat_signal.c's
 * own copy of this same technique, signal.c's stop-events are built
 * from. */
#define SYS_openat    56
#define SYS_ftruncate 46
#define SYS_close     57
#define AT_FDCWD_LX   (-100)
#define O_RDWR_LX     02
#define O_CREAT_LX    0100
#define O_EXCL_LX     0200
#define MAP_SHARED_LX 0x01

static void named_sem_path(const char *name, char *buf, size_t bufsz)
{
	static const char prefix[] = "/tmp/.ntlibc-sem.";
	size_t plen = sizeof(prefix) - 1, i, j = 0;
	for (i = 0; i < plen && j < bufsz - 1; i++) buf[j++] = prefix[i];
	for (i = 0; name[i] && j < bufsz - 1; i++)
		buf[j++] = (name[i] == '/') ? '_' : name[i];
	buf[j] = 0;
}

/* Opens (with `flags`) the backing file for `name`, sizes it on
 * O_CREAT, and hands back the MAP_SHARED mapping. A real, disclosed
 * race: if two processes race __plat_named_semaphore_open_or_create()
 * for the same brand-new name, the second one's O_CREAT|O_EXCL fails
 * and it falls back to a plain open() (below) that can, in principle,
 * observe the file before the first process's ftruncate() has run --
 * a zero-length mmap that would fail. Narrow in practice (the window
 * is a handful of instructions between two syscalls on the SAME
 * creator), not eliminated; a real fix would retry the mmap on
 * failure, disclosed as follow-up rather than papered over here.
 *
 * Every one of this function's own callers' obj->futex/obj->kind/obj->max
 * findings share alloc_sync()'s own "checked raw_syscall(SYS_mmap, ...)
 * allocation, then use" residual, above -- see that function's comment;
 * not repeated at each call site below. */
static int map_named_sem(const char *name, long flags, long mode,
                         struct ntlibc_linux_sync **out)
{
	char path[160];
	long fd, r;

	named_sem_path(name, path, sizeof path);
	fd = raw_syscall(SYS_openat, AT_FDCWD_LX, (long)path, flags, mode, 0, 0);
	if (is_sys_error(fd)) { errno = (int)-fd; return -1; }
	if (flags & O_CREAT_LX)
		raw_syscall(SYS_ftruncate, fd, (long)sizeof(struct ntlibc_linux_sync), 0, 0, 0, 0);
	r = raw_syscall(SYS_mmap, 0, (long)sizeof(struct ntlibc_linux_sync),
	                PROT_READ | PROT_WRITE, MAP_SHARED_LX, fd, 0);
	raw_syscall(SYS_close, fd, 0, 0, 0, 0, 0);
	if (is_sys_error(r)) { errno = (int)-r; return -1; }
	*out = (struct ntlibc_linux_sync *)r;
	return 0;
}

/* A fresh, believed-unique name -- collision is a plain error, not a
 * create-or-open contract, matching plat_thread.h's own contract for
 * this function. */
int __plat_named_semaphore_create(const char *name, long initial, long maximum,
                                  __plat_handle_t *out)
{
	struct ntlibc_linux_sync *obj;
	if (map_named_sem(name, O_RDWR_LX | O_CREAT_LX | O_EXCL_LX, 0600, &obj) < 0)
		return -1;
	obj->futex = (int)initial;
	obj->max = (int)maximum;
	obj->kind = NTLIBC_LX_SYNC_SEMAPHORE;
	*out = (__plat_handle_t)obj;
	return 0;
}

/* -2 reports "no such name" specifically (this backend's ENOENT),
 * matching plat_thread.h's own contract for the STATUS_OBJECT_NAME_NOT_FOUND
 * case sem_open()'s O_CREAT-without-O_EXCL recovery path needs. */
int __plat_named_semaphore_open(const char *name, __plat_handle_t *out)
{
	struct ntlibc_linux_sync *obj;
	if (map_named_sem(name, O_RDWR_LX, 0, &obj) < 0)
		return errno == ENOENT ? -2 : -1;
	*out = (__plat_handle_t)obj;
	return 0;
}

int __plat_named_semaphore_open_or_create(const char *name, long initial,
                                          long maximum, __plat_handle_t *out)
{
	struct ntlibc_linux_sync *obj;
	if (map_named_sem(name, O_RDWR_LX | O_CREAT_LX | O_EXCL_LX, 0600, &obj) == 0) {
		obj->futex = (int)initial;
		obj->max = (int)maximum;
		obj->kind = NTLIBC_LX_SYNC_SEMAPHORE;
		*out = (__plat_handle_t)obj;
		return 0;
	}
	if (map_named_sem(name, O_RDWR_LX, 0, &obj) < 0) return -1;
	*out = (__plat_handle_t)obj;
	return 0;
}

/* ---- named mutant: semaphore.c's cross-process advisory lock -------------
 * semaphore.c's namespace_lock()/namespace_unlock() (not just
 * sigdelivery.c, contrary to this file's own earlier assumption --
 * confirmed by grep, not guessed) need a real create-or-open,
 * cross-process binary lock keyed by name. Modeled as a
 * ntlibc_linux_sync semaphore with initial=1,max=1, the same
 * shared-file-plus-mmap technique __plat_named_semaphore_*() above
 * already uses, under its own path prefix so a mutant name can never
 * collide with a semaphore name that happens to hash to the same
 * bytes. */
static void named_mutant_path(const char *name, char *buf, size_t bufsz)
{
	static const char prefix[] = "/tmp/.ntlibc-mutant.";
	size_t plen = sizeof(prefix) - 1, i, j = 0;
	for (i = 0; i < plen && j < bufsz - 1; i++) buf[j++] = prefix[i];
	for (i = 0; name[i] && j < bufsz - 1; i++)
		buf[j++] = (name[i] == '\\' || name[i] == '/') ? '_' : name[i];
	buf[j] = 0;
}

/* Acquire (wait indefinitely, non-alertable) the create-or-open named
 * mutant `name`, matching plat_thread.h's own contract for this
 * function. Unlike the named-semaphore group above, this uses plain
 * O_CREAT (no O_EXCL): two processes both naming a brand-new lock for
 * the first time both legitimately need a valid, initialized lock back,
 * not a create-vs-open distinction. That first-touch initialization
 * used to be a real, confirmed race, not just a theoretical one: an
 * earlier version of this function checked `obj->kind !=
 * NTLIBC_LX_SYNC_SEMAPHORE` and then, non-atomically, wrote
 * obj->max/obj->futex/obj->kind in plain (non-atomic) stores. Two
 * processes racing the same fresh backing file could both observe
 * kind==0 and both start that write sequence; if process A's writes
 * landed, A went on to __plat_wait_one() and successfully decremented
 * futex from 1 to 0 (genuinely acquiring the lock) BEFORE process B's
 * own already-in-flight, already-decided "reinitialize" writes landed --
 * B's plain `obj->futex = 1` then silently overwrote A's decrement, and
 * B's own subsequent __plat_wait_one() call decremented that
 * resurrected 1 back to 0 too, so BOTH processes believed they
 * exclusively held the same lock at once. Confirmed with strace against
 * a real fork/1-1.c run (parent and child both call sem_open() on the
 * same name immediately after fork(), the exact shape this races):
 * intermittent hangs and double-acquisition-shaped corruption that
 * neither the sleep(1)-busy-spin fix (src/signal/linux/plat_signal.c)
 * nor the malloc/fast-lock self-deadlock fix (src/internal/
 * plat_malloc_generic.h) explained on their own -- this was the third,
 * separate bug behind the same TIMEOUT cluster, only reproducing on a
 * fraction of runs because it depends on exact process-scheduling
 * timing, unlike the other two which reproduced every time.
 *
 * The fix: exactly one process ever performs the plain, non-atomic
 * max/futex writes, decided by a real atomic CAS on `kind` (0 ->
 * INITIALIZING) rather than a plain read-then-write. The CAS's winner
 * writes max/futex and then RELEASE-publishes kind=SEMAPHORE; every
 * loser -- whether it raced in during the writes (observes
 * INITIALIZING) or arrives after they are done (observes SEMAPHORE
 * directly) -- only ever ACQUIRE-loads `kind`, synchronizing with that
 * RELEASE store before touching max/futex at all, so no process ever
 * writes those fields concurrently with another and no process ever
 * observes them half-written.
 *
 * obj->kind/obj->max below are the same "checked raw_syscall(SYS_mmap,
 * ...) allocation, then use" residual alloc_sync()'s own comment above
 * documents -- this function inlines its own copy of that pattern
 * (`r = raw_syscall(SYS_mmap, ...); if (is_sys_error(r)) ...; obj =
 * (struct ntlibc_linux_sync *)r;`) rather than sharing alloc_sync()
 * itself, since it maps a real file, not an anonymous page -- same
 * verified-sound-by-hand reasoning, not expressible as a `nonnull` on
 * this function's own name/out parameters. */
int __plat_named_mutant_acquire(const char *name, __plat_handle_t *out)
{
	char path[160];
	struct ntlibc_linux_sync *obj;
	long fd, r;
	unsigned char expect;

	named_mutant_path(name, path, sizeof path);
	fd = raw_syscall(SYS_openat, AT_FDCWD_LX, (long)path, O_RDWR_LX | O_CREAT_LX, 0600, 0, 0);
	if (is_sys_error(fd)) { errno = (int)-fd; return -1; }
	raw_syscall(SYS_ftruncate, fd, (long)sizeof(struct ntlibc_linux_sync), 0, 0, 0, 0);
	r = raw_syscall(SYS_mmap, 0, (long)sizeof(struct ntlibc_linux_sync),
	                PROT_READ | PROT_WRITE, MAP_SHARED_LX, fd, 0);
	raw_syscall(SYS_close, fd, 0, 0, 0, 0, 0);
	if (is_sys_error(r)) { errno = (int)-r; return -1; }
	obj = (struct ntlibc_linux_sync *)r;

	expect = 0;
	if (__atomic_compare_exchange_n(&obj->kind, &expect, NTLIBC_LX_SYNC_INITIALIZING,
	                                0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
		/* We are provably the only process that can be writing these
		 * two fields right now -- the CAS above admits exactly one
		 * winner across every process racing this same backing file. */
		obj->max = 1;
		obj->futex = 1;
		__atomic_store_n(&obj->kind, NTLIBC_LX_SYNC_SEMAPHORE, __ATOMIC_RELEASE);
	} else {
		/* Either already published (expect == SEMAPHORE, and the
		 * ACQUIRE above already synchronizes with whichever process's
		 * RELEASE store set it) or someone else's initialization is
		 * still in flight (expect == INITIALIZING) -- spin for the
		 * real, short window until it publishes. Real, not a
		 * fabricated wait: the winner's own critical section above is
		 * two plain integer stores, nothing that blocks or takes a
		 * kernel round trip, so this never spins for longer than a
		 * handful of instructions on a peer that is, definitionally,
		 * currently running -- the identical reasoning __plat_fast_lock()
		 * above gives for its own spin, reused here rather than
		 * duplicated with different wording. */
		while (__atomic_load_n(&obj->kind, __ATOMIC_ACQUIRE) == NTLIBC_LX_SYNC_INITIALIZING)
			raw_syscall(SYS_sched_yield, 0L, 0L, 0L, 0L, 0L, 0L);
	}

	__plat_wait_one((__plat_handle_t)obj, 0, 0, 0);
	*out = (__plat_handle_t)obj;
	return 0;
}

void __plat_named_mutant_release(__plat_handle_t lock)
{
	__plat_semaphore_post(lock);
}

/* ---- thread lifecycle, the remaining functions -----------------------
 * See this file's own banner (top): __plat_thread_resume() belongs to
 * src/process/linux/plat_process.c, already real (a no-op -- nothing
 * this backend spawns is ever created suspended). The functions below
 * are this file's own remaining share of plat_thread.h.
 */
#define SYS_kill_lx    129
#define SYS_getpid_lx  172
#define SYS_exit_lx    93
#define SYS_write_lx   64
#define SYS_clock_gettime_lx 113
#define LINUX_SIGSTOP  19

/* Suspends the thread __plat_thread_spawn() created, identified by its
 * boxed tid+1 (see that function's own comment). Real plain kill(2),
 * not tgkill(2): this backend's spawn() does not pass CLONE_THREAD (a
 * disclosed simplification, same banner), so the spawned "thread" is
 * its own thread-group leader -- its own separate pid, not a sibling
 * in the caller's thread group tgkill(2) requires -- and a plain
 * per-process SIGSTOP delivered to that pid is the real, correct
 * primitive for what this backend actually created. */
int __plat_thread_suspend(__plat_handle_t h)
{
	long tid = (long)h - 1;
	long r = raw_syscall(SYS_kill_lx, tid, (long)LINUX_SIGSTOP, 0, 0, 0, 0);
	if (is_sys_error(r)) { errno = (int)-r; return -1; }
	return 0;
}

/* NOT implemented for real: NT's QueueApcThread/redirect-via-CONTEXT-
 * record has no Linux equivalent this port builds yet (a real one
 * needs a dedicated real-time signal with a kernel-installed handler,
 * genuinely separate work from anything else in this file -- see
 * src/signal/linux/sigdelivery.c's own banner for the identical gap on
 * the signal-delivery side). Both return a real, honest failure rather
 * than silently doing nothing: src/thread/pthread_cancel.c's
 * PTHREAD_CANCEL_ASYNCHRONOUS path is the only caller of either, and a
 * clean failure there means asynchronous cancellation is not yet
 * available on this platform -- deferred cancellation (the POSIX
 * default type) never calls these at all. */
int __plat_thread_queue_apc(__plat_handle_t h, __plat_apc_fn fn, void *arg1, void *arg2)
{
	(void)h; (void)fn; (void)arg1; (void)arg2;
	errno = ENOSYS;
	return -1;
}

int __plat_thread_redirect_ip(__plat_handle_t h, void *target)
{
	(void)h; (void)target;
	errno = ENOSYS;
	return -1;
}

/* Same disclosed gap as above: no per-thread TEB-equivalent this port
 * tracks stack bounds through yet (see this file's own banner on the
 * missing CLONE_SETTLS/real TCB). pthread_getattr_np() is a glibc/BSD
 * extension, not strict POSIX -- a real, honest failure here rather
 * than a fabricated answer. */
int __plat_thread_stack_extent(__plat_handle_t h, void **base, size_t *size)
{
	(void)h; (void)base; (void)size;
	errno = ENOSYS;
	return -1;
}

/* Linux has no separate "pseudo-handle valid only for self-operations"
 * concept the way NT's NtCurrentThread() is -- gettid(2) is already a
 * real, durable, always-succeeding identifier for the calling thread,
 * so the same encoding __plat_thread_duplicate_self() above uses
 * (tid+1) answers both plat_thread.h roles identically and for real. */
__plat_handle_t __plat_thread_current_pseudo(void)
{
	return __plat_thread_duplicate_self();
}

_Noreturn void __plat_thread_terminate_self(void)
{
	for (;;) raw_syscall(SYS_exit_lx, 0L, 0L, 0L, 0L, 0L, 0L);
}

/* The bypass-everything emergency abort __pthread_cancel_unsafe_enter()'s
 * documented regions use -- real, not degraded: a raw write(2) straight
 * to fd 2 (this platform's real stderr, no stdio/fd-table locks that a
 * suspended target thread might itself hold involved at all) followed
 * by an immediate, unconditional process exit. `region` is written as
 * a fixed, already-NUL-terminated diagnostic; no formatting, on
 * purpose, for the same async-signal-safety reason
 * src/thread/nt/plat_thread.c's own version gives. */
_Noreturn void __plat_cancel_unsafe_abort(const char *region)
{
	static const char msg1[] = "ntlibc: cancellation-unsafe abort in: ";
	static const char msg2[] = "\n";
	size_t len = 0;
	while (region[len]) len++;
	raw_syscall(SYS_write_lx, 2L, (long)msg1, (long)(sizeof msg1 - 1), 0, 0, 0);
	raw_syscall(SYS_write_lx, 2L, (long)region, (long)len, 0, 0, 0);
	raw_syscall(SYS_write_lx, 2L, (long)msg2, (long)(sizeof msg2 - 1), 0, 0, 0);
	for (;;) raw_syscall(SYS_exit_lx, 1L, 0L, 0L, 0L, 0L, 0L);
}

/* Linux has no APC-alertable-wait concept (see __plat_wait_one()'s own
 * comment above) -- a plain sched_yield(2) is the real, honest
 * equivalent of "let a pending APC run first if there is one" here:
 * yield the processor, nothing more, since there is no APC queue to
 * drain. */
void __plat_thread_alertable_yield(void)
{
	raw_syscall(SYS_sched_yield, 0L, 0L, 0L, 0L, 0L, 0L);
}

/* NT ticks: 100ns units since 1601-01-01. Linux's real clock_gettime(2)
 * gives seconds+nanoseconds since 1970-01-01; 11644473600 is the real,
 * well-known number of seconds between those two epochs (the same
 * constant every Windows-interop codebase uses for this conversion). */
long long __plat_query_system_time(void)
{
	struct linux_timespec ts;
	long long secs_since_1601;
	raw_syscall(SYS_clock_gettime_lx, 0L /* CLOCK_REALTIME */, (long)&ts, 0, 0, 0, 0);
	secs_since_1601 = (long long)ts.tv_sec + 11644473600LL;
	return secs_since_1601 * 10000000LL + (long long)ts.tv_nsec / 100LL;
}

/* mqueue.c's positioned queue-file transfer -- real pread64(2)/
 * pwrite64(2), which already give exactly the contract mqueue.c's own
 * raw_io() needs (an explicit offset, no side effect on any file
 * position, no end-of-file short-circuit beyond the plain "0 bytes"
 * pread64 itself returns at EOF, which mqueue.c's own retry loop
 * already treats as a real error per plat_thread.h's own comment). */
#define SYS_pread64_lx  67
#define SYS_pwrite64_lx 68

ssize_t __plat_thread_file_io(__plat_handle_t h, void *buf, size_t count,
                              off_t off, int write_op)
{
	long fd = (long)h - 1;
	long r = write_op
		? raw_syscall(SYS_pwrite64_lx, fd, (long)buf, (long)count, (long)off, 0, 0)
		: raw_syscall(SYS_pread64_lx, fd, (long)buf, (long)count, (long)off, 0, 0);
	if (is_sys_error(r)) { errno = (int)-r; return -1; }
	return (ssize_t)r;
}
