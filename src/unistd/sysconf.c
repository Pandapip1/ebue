/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * sysconf()/pathconf()/fpathconf()/confstr().
 *
 * The shape of sysconf() below is set by one sentence in sysconf.html
 * and one in unistd.h.html, read together.  <unistd.h> "shall define
 * the following symbolic constants for sysconf()" and then lists 125
 * _SC_ names unconditionally, so every one of them is a VALID name by
 * definition; sysconf.html gives [EINVAL] for an *invalid* name only.
 * A `default: errno = EINVAL` that catches mandated names therefore
 * tells the caller "no such variable" about a variable the header it
 * just included promises exists, and the caller cannot tell that from a
 * genuine rejection.
 *
 * The truthful answer for a variable this library has no number for is
 * the one sysconf.html spells out: "If the variable corresponding to
 * name has no limit ... sysconf() shall return -1 without changing
 * errno."  So the switch answers all 125: a real value where there is
 * one, and -1 with errno untouched where the option is absent.  errno
 * is left for names that are not on the list at all, which is the only
 * case [EINVAL] describes.
 *
 * An option answered -1 here is answered by SILENCE in <unistd.h> --
 * no _POSIX_* constant at all, rather than one set to -1 -- for the
 * reasons that header's own comment gives.  The two must not drift:
 * a header claiming an option sysconf() denies, or the reverse, is
 * worse than either answer alone.
 */
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include "libc.h"

long sysconf(int name)
{
	switch (name) {
	/* ---- limits with a real value ---------------------------- */
	case _SC_ARG_MAX: return ARG_MAX;
	case _SC_CHILD_MAX: return CHILD_CAP_LIMIT_;
	case _SC_CLK_TCK: return 100;
	case _SC_NGROUPS_MAX: return NGROUPS_MAX;
	case _SC_OPEN_MAX: return FD_MAX;
	case _SC_STREAM_MAX: return FD_MAX;
	case _SC_TZNAME_MAX: return TZNAME_MAX;
	case _SC_VERSION: return _POSIX_VERSION;
	case _SC_2_VERSION: return _POSIX2_VERSION;
	case _SC_XOPEN_VERSION: return _XOPEN_VERSION;
	/* Both names, one number: limits.h.html has {PAGESIZE}
	 * "equivalent to {PAGE_SIZE}", and see include/unistd.h on why
	 * the two selectors are nonetheless distinct here. */
	case _SC_PAGESIZE:
	case _SC_PAGE_SIZE: return 4096;
	case _SC_HOST_NAME_MAX: return HOST_NAME_MAX;
	case _SC_TTY_NAME_MAX: return TTY_NAME_MAX;
	case _SC_SYMLOOP_MAX: return SYMLOOP_MAX;
	case _SC_IOV_MAX: return IOV_MAX;
	/* The <limits.h> Runtime Increasable Values, which that header
	 * documents as the compile-time minimum this library promises;
	 * reporting anything smaller here would break the promise. */
	case _SC_LINE_MAX: return LINE_MAX;
	case _SC_RE_DUP_MAX: return RE_DUP_MAX;
	case _SC_BC_BASE_MAX: return BC_BASE_MAX;
	case _SC_BC_DIM_MAX: return BC_DIM_MAX;
	case _SC_BC_SCALE_MAX: return BC_SCALE_MAX;
	case _SC_BC_STRING_MAX: return BC_STRING_MAX;
	case _SC_COLL_WEIGHTS_MAX: return COLL_WEIGHTS_MAX;
	case _SC_EXPR_NEST_MAX: return EXPR_NEST_MAX;
	/* The atexit() table is a fixed array, so this is exact rather
	 * than a floor; shared with src/exit/exit.c through libc.h so
	 * the two cannot drift. */
	case _SC_ATEXIT_MAX: return ATEXIT_CAP_;

	/* ---- options this library does support -------------------- */
	/* Each of these names an option whose whole interface set is
	 * present and implemented here, so a value greater than zero is
	 * the honest answer -- an application that asks in order to
	 * decide whether to use fsync(), regcomp() or posix_spawn()
	 * should be told yes.  <unistd.h> defines no matching _POSIX_*
	 * constant for any of them, which is exactly the case
	 * sysconf.html exists for. */
	case _SC_FSYNC: return _POSIX_VERSION;           /* src/unistd/fsync.c */
	case _SC_REGEXP: return _POSIX_VERSION;          /* src/regex/regex.c */
	case _SC_SPAWN: return _POSIX_VERSION;           /* src/process/posix_spawn.c */
	case _SC_MONOTONIC_CLOCK: return _POSIX_VERSION; /* src/time/clock_gettime.c */
	case _SC_SHARED_MEMORY_OBJECTS: return _POSIX_VERSION; /* src/mman/shm.c */
	case _SC_XOPEN_UNIX: return _XOPEN_UNIX;
	case _SC_XOPEN_ENH_I18N: return _XOPEN_ENH_I18N;

	/* ---- ntlibc extensions ------------------------------------ */
	case _SC_NPROCESSORS_CONF:
	case _SC_NPROCESSORS_ONLN: {
		SYSTEM_BASIC_INFORMATION sbi;
		if (NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof sbi, 0)))
			return sbi.NumberOfProcessors;
		return 1;
	}
	case _SC_PHYS_PAGES: {
		SYSTEM_BASIC_INFORMATION sbi;
		if (NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof sbi, 0)))
			return (long)((unsigned long long)sbi.NumberOfPhysicalPages * sbi.PageSize / 4096);
		return -1;
	}

	/* ---- the rest of the mandated list: -1, errno untouched ---- */
	/*
	 * Absent option groups, by what is missing:
	 *
	 *   no <pthread.h>          -- _SC_THREADS and the whole _SC_THREAD_*
	 *                              block, _SC_BARRIERS,
	 *                              _SC_READER_WRITER_LOCKS,
	 *                              _SC_SPIN_LOCKS, _SC_TIMEOUTS,
	 *                              _SC_CLOCK_SELECTION (which needs
	 *                              pthread_condattr_setclock() as well as
	 *                              the clock_nanosleep() we have)
	 *   no <aio.h>              -- _SC_ASYNCHRONOUS_IO, _SC_AIO_*,
	 *                              _SC_PRIORITIZED_IO
	 *   no <semaphore.h>        -- _SC_SEMAPHORES, _SC_SEM_*
	 *   no <mqueue.h>           -- _SC_MESSAGE_PASSING, _SC_MQ_*
	 *   incomplete <sys/mman.h> -- _SC_MAPPED_FILES, _SC_MEMLOCK*,
	 *                              _SC_MEMORY_PROTECTION,
	 *                              _SC_TYPED_MEMORY_OBJECTS, _SC_XOPEN_SHM
	 *   no timer_create()       -- _SC_TIMERS, _SC_TIMER_MAX,
	 *                              _SC_DELAYTIMER_MAX
	 *   stub queued signals     -- _SC_REALTIME_SIGNALS, _SC_RTSIG_MAX,
	 *                              _SC_SIGQUEUE_MAX (<signal.h> marks
	 *                              sigqueue()/sigwaitinfo()/
	 *                              sigtimedwait() undefined-ok)
	 *   no utilities            -- the _SC_2_* development-utility set
	 *                              (c99, fort77, localedef, PBS, ...);
	 *                              sh/ implements a documented subset of
	 *                              sh, and system()/popen() run %ComSpec%
	 *                              rather than a POSIX shell, so
	 *                              _SC_SHELL goes with them
	 *   declined outright       -- _SC_TRACE*, _SC_SPORADIC_SERVER,
	 *                              _SC_PRIORITY_SCHEDULING and
	 *                              _SC_CPUTIME (<sched.h>'s banner),
	 *                              _SC_XOPEN_CRYPT (crypt() is
	 *                              undefined-ok), _SC_ADVISORY_INFO
	 *                              (posix_madvise() needs mman),
	 *                              _SC_IPV6/_SC_RAW_SOCKETS
	 *                              (<netinet/in.h>'s banner)
	 *
	 * _SC_JOB_CONTROL and _SC_SAVED_IDS answered -1 before this list
	 * existed and keep doing so: NT has no process groups to stop and
	 * resume, and src/unistd/ids.c has one fixed identity with no
	 * saved-set to switch between.
	 *
	 * The eight programming-model names are a deliberate decline
	 * rather than an oversight.  x86_64-win32 is LLP64, which is none
	 * of ILP32_OFF32/ILP32_OFFBIG/LP64_OFF64/LPBIG_OFFBIG, and even
	 * where i386-win32 would fit ILP32_OFFBIG a positive answer is
	 * only actionable together with the _CS_POSIX_V7_ILP32_OFFBIG_*
	 * flags, which confstr() cannot supply (see this file's confstr()
	 * and the defect recorded against it).  Claiming a model whose
	 * build flags are unobtainable is worse than declining it.
	 *
	 * _SC_LOGIN_NAME_MAX, _SC_GETPW_R_SIZE_MAX and
	 * _SC_GETGR_R_SIZE_MAX are the "no limit" case rather than the
	 * "no option" one, and answer -1 for that reason: the record
	 * src/misc/pwd.c hands back is built out of %USERNAME%,
	 * %USERPROFILE% and %ComSpec%, environment strings with no bound
	 * this library imposes, so any fixed maximum printed here would
	 * be a guess.  Callers grow the buffer on [ERANGE], which is the
	 * contract those functions already have.
	 */
	case _SC_JOB_CONTROL:
	case _SC_SAVED_IDS:
	case _SC_REALTIME_SIGNALS:
	case _SC_PRIORITY_SCHEDULING:
	case _SC_TIMERS:
	case _SC_ASYNCHRONOUS_IO:
	case _SC_PRIORITIZED_IO:
	case _SC_SYNCHRONIZED_IO:
	case _SC_MAPPED_FILES:
	case _SC_MEMLOCK:
	case _SC_MEMLOCK_RANGE:
	case _SC_MEMORY_PROTECTION:
	case _SC_MESSAGE_PASSING:
	case _SC_SEMAPHORES:
	case _SC_AIO_LISTIO_MAX:
	case _SC_AIO_MAX:
	case _SC_AIO_PRIO_DELTA_MAX:
	case _SC_DELAYTIMER_MAX:
	case _SC_MQ_OPEN_MAX:
	case _SC_MQ_PRIO_MAX:
	case _SC_RTSIG_MAX:
	case _SC_SEM_NSEMS_MAX:
	case _SC_SEM_VALUE_MAX:
	case _SC_SIGQUEUE_MAX:
	case _SC_TIMER_MAX:
	case _SC_2_C_BIND:
	case _SC_2_C_DEV:
	case _SC_2_FORT_DEV:
	case _SC_2_FORT_RUN:
	case _SC_2_SW_DEV:
	case _SC_2_LOCALEDEF:
	case _SC_2_CHAR_TERM:
	case _SC_2_UPE:
	case _SC_2_PBS:
	case _SC_2_PBS_ACCOUNTING:
	case _SC_2_PBS_CHECKPOINT:
	case _SC_2_PBS_LOCATE:
	case _SC_2_PBS_MESSAGE:
	case _SC_2_PBS_TRACK:
	case _SC_THREADS:
	case _SC_THREAD_SAFE_FUNCTIONS:
	case _SC_THREAD_DESTRUCTOR_ITERATIONS:
	case _SC_THREAD_KEYS_MAX:
	case _SC_THREAD_STACK_MIN:
	case _SC_THREAD_THREADS_MAX:
	case _SC_THREAD_ATTR_STACKADDR:
	case _SC_THREAD_ATTR_STACKSIZE:
	case _SC_THREAD_PRIORITY_SCHEDULING:
	case _SC_THREAD_PRIO_INHERIT:
	case _SC_THREAD_PRIO_PROTECT:
	case _SC_THREAD_PROCESS_SHARED:
	case _SC_THREAD_CPUTIME:
	case _SC_THREAD_SPORADIC_SERVER:
	case _SC_THREAD_ROBUST_PRIO_INHERIT:
	case _SC_THREAD_ROBUST_PRIO_PROTECT:
	case _SC_GETGR_R_SIZE_MAX:
	case _SC_GETPW_R_SIZE_MAX:
	case _SC_LOGIN_NAME_MAX:
	case _SC_XOPEN_CRYPT:
	case _SC_XOPEN_SHM:
	case _SC_XOPEN_REALTIME:
	case _SC_XOPEN_REALTIME_THREADS:
	case _SC_XOPEN_STREAMS:
	case _SC_XOPEN_UUCP:
	case _SC_ADVISORY_INFO:
	case _SC_BARRIERS:
	case _SC_CLOCK_SELECTION:
	case _SC_CPUTIME:
	case _SC_READER_WRITER_LOCKS:
	case _SC_SPIN_LOCKS:
	case _SC_SHELL:
	case _SC_SPORADIC_SERVER:
	case _SC_SS_REPL_MAX:
	case _SC_TIMEOUTS:
	case _SC_TYPED_MEMORY_OBJECTS:
	case _SC_TRACE:
	case _SC_TRACE_EVENT_FILTER:
	case _SC_TRACE_EVENT_NAME_MAX:
	case _SC_TRACE_INHERIT:
	case _SC_TRACE_LOG:
	case _SC_TRACE_NAME_MAX:
	case _SC_TRACE_SYS_MAX:
	case _SC_TRACE_USER_EVENT_MAX:
	case _SC_IPV6:
	case _SC_RAW_SOCKETS:
	case _SC_V6_ILP32_OFF32:
	case _SC_V6_ILP32_OFFBIG:
	case _SC_V6_LP64_OFF64:
	case _SC_V6_LPBIG_OFFBIG:
	case _SC_V7_ILP32_OFF32:
	case _SC_V7_ILP32_OFFBIG:
	case _SC_V7_LP64_OFF64:
	case _SC_V7_LPBIG_OFFBIG:
		return -1;

	default:
		errno = EINVAL;
		return -1;
	}
}

/* pathconf()/fpathconf().  Unlike sysconf() this one is NOT obliged to
 * answer every mandated name: fpathconf.html lists "[EINVAL] The
 * implementation does not support an association of the variable name
 * with the specified file" as a *may fail*, so refusing a variable that
 * means nothing for the file in hand is conforming.  What is not
 * conforming is the name failing to exist, which is why <unistd.h>
 * defines all 21 either way.
 *
 * Nothing here looks at path or fd yet -- every answer below is a
 * property of this library or of NT, not of the individual file -- so
 * fpathconf() is implemented by forwarding, which also makes the two
 * entry points agree by construction rather than by parallel
 * maintenance. */
long pathconf(const char *path, int name)
{
	(void)path;
	switch (name) {
	case _PC_LINK_MAX: return 1023;
	case _PC_MAX_CANON: return 255;
	case _PC_MAX_INPUT: return 255;
	case _PC_NAME_MAX: return NAME_MAX;
	case _PC_PATH_MAX: return PATH_MAX;
	case _PC_PIPE_BUF: return PIPE_BUF;
	case _PC_CHOWN_RESTRICTED: return 1;
	case _PC_NO_TRUNC: return 1;
	/* Same character _POSIX_VDISABLE names; keep the two equal. */
	case _PC_VDISABLE: return 0;
	case _PC_FILESIZEBITS: return FILESIZEBITS;
	/* NTFS reparse points carry symlinks, and this library's own path
	 * handling is bounded by PATH_MAX, so that is the length of link
	 * content readlink() can actually produce. */
	case _PC_SYMLINK_MAX: return PATH_MAX;
	case _PC_2_SYMLINKS: return 1;
	/* NT keeps every file time as a FILETIME, a count of 100ns ticks,
	 * and that is the resolution utimensat()/futimens() can preserve
	 * (src/stat/utimensat.c) -- not the nanosecond struct timespec can
	 * express. */
	case _PC_TIMESTAMP_RESOLUTION: return 100;

	/* The remaining seven are the "may fail" case the header comment
	 * describes, answered as "no limit / not supported" -- -1 with
	 * errno untouched -- rather than [EINVAL]:
	 *
	 *   _PC_ASYNC_IO, _PC_PRIO_IO, _PC_SYNC_IO name the aio and
	 *   prioritized/synchronized-I/O options, which sysconf() above
	 *   also declines;
	 *   _PC_ALLOC_SIZE_MIN and the three _PC_REC_* are the XSI
	 *   recommended-transfer-size set, which would have to come from
	 *   the volume's cluster geometry per file.  This function does
	 *   not open path, so any number printed here would be invented,
	 *   and pathconf.html's own APPLICATION USAGE treats these as
	 *   advisory rather than required. */
	case _PC_ASYNC_IO:
	case _PC_PRIO_IO:
	case _PC_SYNC_IO:
	case _PC_ALLOC_SIZE_MIN:
	case _PC_REC_INCR_XFER_SIZE:
	case _PC_REC_MAX_XFER_SIZE:
	case _PC_REC_MIN_XFER_SIZE:
	case _PC_REC_XFER_ALIGN:
		return -1;

	default: errno = EINVAL; return -1;
	}
}

long fpathconf(int fd, int name) { (void)fd; return pathconf("", name); }
int getpagesize(void) { return 4096; }
int getdtablesize(void) { return FD_MAX; }
/* confstr.html RETURN VALUE: an invalid name is 0 with [EINVAL], not the
 * 1 that a lone terminating null would account for and not (size_t)-1.
 * The name set is closed here rather than defaulted, which is what makes
 * that reachable: <unistd.h> defines exactly one _CS_* constant, so
 * every name but _CS_PATH is invalid, and a name added to the header has
 * to gain a case below with it.
 *
 * POSIX's other zero -- a valid name with no configuration-defined
 * value, which returns 0 with errno UNCHANGED -- has no name to reach it
 * in this tree.  A case wanting it cannot just set s to "": the tail
 * below counts the null it writes and returns 1.
 */
size_t confstr(int name, char *buf, size_t len)
{
	const char *s;
	size_t i;

	switch (name) {
	case _CS_PATH: s = "/bin:/usr/bin"; break;
	default: errno = EINVAL; return 0;
	}
	for (i = 0; s[i] && i + 1 < len; i++) buf[i] = s[i];
	if (len) buf[i] = 0;
	while (s[i]) i++;
	return i + 1;
}
