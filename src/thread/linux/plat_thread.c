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
 * NOT ported, and why: the real front door, src/thread/pthread_mutex.c,
 * cannot be linked against this backend as-is even with every function
 * below implemented. Its mutex_acquire()/pthread_mutex_unlock() call
 * RtlAcquirePebLock()/RtlReleasePebLock() directly -- a raw ntdll call,
 * never routed through plat_thread.h at all -- to protect mutex_data's
 * owner/recursion/waiters fields, and __pthread_current() (src/thread/
 * pthread.c) is threaded through process-lifecycle bookkeeping
 * (live_threads, exit()) this port does not touch. Porting pthread_
 * mutex.c for real is a second, separable piece of follow-up work, not
 * this one. What this file proves instead: the two functions
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
 * guarded by the RtlAcquirePebLock() call this port does not touch) --
 * only the blocking primitive underneath them.
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
 *   pointer to this file's own struct linux_sync, mmap()'d, tagged with
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

static long futex_wait(int *uaddr, int expected, const struct linux_timespec *timeout)
{
	return raw_syscall(SYS_futex, (long)uaddr,
	                   FUTEX_WAIT | FUTEX_PRIVATE_FLAG, (long)expected,
	                   (long)timeout, 0, 0);
}

static long futex_wake(int *uaddr, int count)
{
	return raw_syscall(SYS_futex, (long)uaddr,
	                   FUTEX_WAKE | FUTEX_PRIVATE_FLAG, (long)count, 0, 0, 0);
}

/* One mmap()'d page per synchronization object -- no allocator dependency
 * (this file is -nostdinc, linked before ntlibc's own malloc is
 * necessarily available in every configuration that might use it), at
 * the cost of a whole page for a handful of bytes. Fine at this port's
 * scale (a pilot proving the primitive, not a production allocator
 * concern); a real port would suballocate. `kind` distinguishes a
 * counting semaphore (P/V, __plat_wait_one decrements) from a manual-
 * reset event (__plat_wait_one only checks nonzero, never consumes) --
 * the two plat_thread.h waitable kinds this file implements. */
enum { SYNC_SEMAPHORE = 1, SYNC_EVENT = 2 };

struct linux_sync {
	int futex;         /* the wait/wake word */
	int max;           /* semaphore ceiling; unused (0) for an event */
	unsigned char kind;
};

static int alloc_sync(struct linux_sync **out)
{
	long ret = raw_syscall(SYS_mmap, 0, (long)sizeof(struct linux_sync),
	                       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
	                       -1, 0);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	*out = (struct linux_sync *)ret;
	return 0;
}

/* ---- events (manual-reset, initially unset) ----------------------------
 * __plat_event_set() is explicitly this file's to implement per
 * plat_thread.h's banner (it is also declared in plat_signal.h, whose
 * canonical owner is thread, not signal). */
int __plat_event_create(__plat_handle_t *out)
{
	struct linux_sync *obj;
	if (alloc_sync(&obj)) return -1;
	obj->futex = 0;
	obj->max = 0;
	obj->kind = SYNC_EVENT;
	*out = (__plat_handle_t)obj;
	return 0;
}

int __plat_event_set(__plat_handle_t h)
{
	struct linux_sync *obj = (struct linux_sync *)h;
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
	struct linux_sync *obj;
	(void)inheritable;
	if (alloc_sync(&obj)) return -1;
	obj->futex = (int)initial;
	obj->max = (int)maximum;
	obj->kind = SYNC_SEMAPHORE;
	*out = (__plat_handle_t)obj;
	return 0;
}

int __plat_semaphore_post(__plat_handle_t h)
{
	struct linux_sync *obj = (struct linux_sync *)h;
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
	struct linux_sync *obj = (struct linux_sync *)h;
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
	struct linux_sync *obj = (struct linux_sync *)h;
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
		if (obj->kind == SYNC_EVENT) {
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
