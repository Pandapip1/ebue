/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef	_UNISTD_H
#define	_UNISTD_H

#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#ifdef __cplusplus
#define NULL 0L
#else
#define NULL ((void*)0)
#endif

#define __NEED_size_t
#define __NEED_ssize_t
#define __NEED_uid_t
#define __NEED_gid_t
#define __NEED_off_t
#define __NEED_pid_t
#define __NEED_intptr_t
#define __NEED_useconds_t

#include <bits/alltypes.h>

int pipe(int [2]);
int pipe2(int [2], int);
int close(int);
int posix_close(int, int);
int dup(int);
int dup2(int, int);
int dup3(int, int, int);
off_t lseek(int, off_t, int);
int fsync(int);
int fdatasync(int);

ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
ssize_t pread(int, void *, size_t, off_t);
ssize_t pwrite(int, const void *, size_t, off_t);

int chown(const char *, uid_t, gid_t);
int fchown(int, uid_t, gid_t);
int lchown(const char *, uid_t, gid_t);
int fchownat(int, const char *, uid_t, gid_t, int);

int link(const char *, const char *);
int linkat(int, const char *, int, const char *, int);
int symlink(const char *, const char *);
int symlinkat(const char *, int, const char *);
ssize_t readlink(const char *__restrict, char *__restrict, size_t);
ssize_t readlinkat(int, const char *__restrict, char *__restrict, size_t);
int unlink(const char *);
int unlinkat(int, const char *, int);
int rmdir(const char *);
int truncate(const char *, off_t);
int ftruncate(int, off_t);

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

int access(const char *, int);
int faccessat(int, const char *, int, int);

int chdir(const char *);
int fchdir(int);
char *getcwd(char *, size_t);

unsigned alarm(unsigned);
unsigned sleep(unsigned);
int pause(void);

pid_t fork(void);
pid_t _Fork(void);
int execve(const char *, char *const [], char *const []);
int execv(const char *, char *const []);
int execle(const char *, const char *, ...);
int execl(const char *, const char *, ...);
int execvp(const char *, char *const []);
int execlp(const char *, const char *, ...);
int fexecve(int, char *const [], char *const []);
_Noreturn void _exit(int);

pid_t getpid(void);
pid_t getppid(void);
pid_t getpgrp(void);
pid_t getpgid(pid_t);
int setpgid(pid_t, pid_t);
pid_t setsid(void);
pid_t getsid(pid_t);
char *ttyname(int);
int ttyname_r(int, char *, size_t);
int isatty(int);
pid_t tcgetpgrp(int);
int tcsetpgrp(int, pid_t);

uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int getgroups(int, gid_t []);
int setuid(uid_t);
int seteuid(uid_t);
int setgid(gid_t);
int setegid(gid_t);

char *getlogin(void);
int getlogin_r(char *, size_t);
int gethostname(char *, size_t);

int getopt(int, char * const [], const char *);
extern char *optarg;
extern int optind, opterr, optopt;

long pathconf(const char *, int);
long fpathconf(int, int);
long sysconf(int);
size_t confstr(int, char *, size_t);

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#define F_ULOCK 0
#define F_LOCK  1
#define F_TLOCK 2
#define F_TEST  3
int setreuid(uid_t, uid_t);
int setregid(gid_t, gid_t);
int lockf(int, int, off_t);  /* undefined-ok: F_SETLK/F_SETLKW are themselves
	permanent no-op stubs (src/fcntl/fcntl.c: "Advisory locks are not
	implemented; report success"), so a lockf() built on them would only
	look like real locking without providing any -- worse than absent */
long gethostid(void);  /* undefined-ok: BSD host-id concept, no NT analogue */
int nice(int);
void sync(void);
pid_t setpgrp(void);
char *crypt(const char *, const char *);  /* undefined-ok: DES password
	hashing is not something this library implements from scratch */
void encrypt(char *, int);  /* undefined-ok: same DES machinery as crypt() */
void swab(const void *__restrict, void *__restrict, ssize_t);
#endif

#if (defined(_XOPEN_SOURCE) && _XOPEN_SOURCE+0 < 700) \
 || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
int usleep(unsigned);
unsigned ualarm(unsigned, unsigned);  /* undefined-ok: alarm()'s NT timer
	(src/unistd/sleep.c) would carry a microsecond deadline happily
	enough, but ualarm()'s second argument makes it a repeating timer,
	and repeating is the part that cannot be honoured here: SIGALRM is
	delivered by an APC that only runs while the thread is in an
	alertable wait, so every expiry a computing thread missed would
	arrive as one delivery rather than as a series */
#endif

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#define L_SET 0
#define L_INCR 1
#define L_XTND 2
int brk(void *);  /* undefined-ok: this library's allocator is NT's private
	heap (RtlAllocateHeap, src/malloc/malloc.c), not a single growable
	brk-style arena; there is no NT primitive shaped like brk() */
void *sbrk(intptr_t);  /* undefined-ok: same brk-heap mismatch as brk() */
pid_t vfork(void);
int vhangup(void);  /* undefined-ok: hangs up a Unix controlling terminal,
	a session/tty concept this library does not model */
int chroot(const char *);
int getpagesize(void);
int getdtablesize(void);
int sethostname(const char *, size_t);  /* undefined-ok: setting the
	computer name is a privileged, persistent OS-configuration change
	with no ntdll-level equivalent */
int getdomainname(char *, size_t);  /* undefined-ok: NIS/YP domain name,
	not an NT concept */
int setdomainname(const char *, size_t);  /* undefined-ok: see getdomainname */
char *getpass(const char *);  /* undefined-ok: needs echo-off terminal
	input; this library has no termios-style tty control */
int daemon(int, int);  /* undefined-ok: the fork()+setsid() BSD idiom, on
	top of a fork() that already needs a patched Wine to run at all
	(see CONTRIBUTING.md); not a foundation to build another function on */
void setusershell(void);  /* undefined-ok: /etc/shells enumeration, no
	such file or concept on NT */
void endusershell(void);  /* undefined-ok: see setusershell */
char *getusershell(void);  /* undefined-ok: see setusershell */
int acct(const char *);  /* undefined-ok: Unix process accounting is a
	kernel facility NT has no equivalent of */
long syscall(long, ...);  /* undefined-ok: NT has no stable, numbered
	raw-syscall ABI exposed to user mode the way this presumes; the Nt*
	entry points this library calls directly are the closest analogue */
int execvpe(const char *, char *const [], char *const []);
int issetugid(void);
int getentropy(void *, size_t);  /* undefined-ok: no entropy source is
	exported by ntdll; a real source (BCryptGenRandom, RtlGenRandom)
	lives in bcrypt.dll/advapi32, which this library treats as an
	exception to load, not a routine dependency (see
	src/signal/signal.c's header comment on NTLIBC_USE_KERNEL32) */
extern int optreset;
#endif

#ifdef _GNU_SOURCE
extern char **environ;
int setresuid(uid_t, uid_t, uid_t);  /* undefined-ok: real/effective/saved
	IDs are a Linux-specific refinement of Unix credentials; this
	library's getuid()/geteuid() (src/unistd/ids.c) already report a
	single fixed identity, so there is nothing for the triple to select
	between */
int setresgid(gid_t, gid_t, gid_t);  /* undefined-ok: see setresuid */
int getresuid(uid_t *, uid_t *, uid_t *);  /* undefined-ok: see setresuid */
int getresgid(gid_t *, gid_t *, gid_t *);  /* undefined-ok: see setresuid */
char *get_current_dir_name(void);
int syncfs(int);  /* undefined-ok: syncs an entire filesystem by fd; NT has
	no per-volume sync primitive this library wires up, and fsync()
	(src/unistd/fsync.c) already covers the per-descriptor case */
int euidaccess(const char *, int);  /* undefined-ok: distinguishes real
	from effective uid the same way access() does not need to here --
	see setresuid on why this library's uid/euid are not distinct */
int eaccess(const char *, int);  /* undefined-ok: glibc alias of
	euidaccess(); see euidaccess */
pid_t gettid(void);
#endif

#define POSIX_CLOSE_RESTART     0

#define _XOPEN_VERSION          700
#define _XOPEN_UNIX             1
#define _XOPEN_ENH_I18N         1

#define _POSIX_VERSION  200809L
#define _POSIX2_VERSION _POSIX_VERSION

/* The Monotonic Clock option. clock_gettime()/clock_getres()/
 * clock_nanosleep() all accept CLOCK_MONOTONIC (see src/time/
 * clock_gettime.c, which backs it with NtQueryPerformanceCounter), so
 * POSIX requires <unistd.h> to say so. Portable code selects between
 * CLOCK_MONOTONIC and CLOCK_REALTIME on this macro alone, and without
 * it every such caller silently falls back to CLOCK_REALTIME -- which
 * on this target is NtQuerySystemTime, a clock Wine deliberately reads
 * from CLOCK_REALTIME_COARSE and whose granularity is therefore 1 ms
 * rather than the 100 ns clock_getres() advertises. */
#define _POSIX_MONOTONIC_CLOCK  200809L

/* Process CPU-time clocks and clock_getcpuclockid(). */
#define _POSIX_CPUTIME 200809L

/* shm_open()/shm_unlink() and file-backed MAP_SHARED mappings together
 * provide the Shared Memory Objects option. */
#define _POSIX_SHARED_MEMORY_OBJECTS 200809L

/* Whole-address-space and range memory locking. */
#define _POSIX_MEMLOCK       200809L
#define _POSIX_MEMLOCK_RANGE 200809L

/* Queued real-time signals, including payload-preserving cross-process
 * sigqueue() delivery and the two synchronous wait interfaces. */
#define _POSIX_REALTIME_SIGNALS 200809L

/* Per-process timers over the clocks exposed by <time.h>. */
#define _POSIX_TIMERS 200809L

/* unistd.h.html, "Constants for Functions": "_POSIX_VDISABLE ... shall
 * always be set to a value other than -1."  0, not the BSD '\377',
 * because src/unistd/sysconf.c's pathconf() already answers
 * _PC_VDISABLE with 0 and POSIX has the two name the same character --
 * a constant that disagreed with the running answer would be a new
 * defect rather than a fix.  If one changes, change both. */
#define _POSIX_VDISABLE 0

/* DELIBERATELY ABSENT: the remaining "Constants for Options and Option
 * Groups" whose text on that page reads "This symbol shall always be
 * set to the value 200809L" -- _POSIX_ASYNCHRONOUS_IO, _POSIX_BARRIERS,
 * _POSIX_CLOCK_SELECTION, _POSIX_MAPPED_FILES, _POSIX_MEMORY_PROTECTION,
 * _POSIX_READER_WRITER_LOCKS, _POSIX_SEMAPHORES,
 * _POSIX_SPIN_LOCKS, _POSIX_THREADS, _POSIX_THREAD_SAFE_FUNCTIONS,
 * _POSIX_TIMEOUTS.  Their absence IS a conformance
 * hole and is recorded as one -- test/POSIX-COVERAGE.md group U, and
 * test/posix-unistd.c's still-fenced
 * test_unistd_mandatory_option_constants -- so this note exists to stop
 * the hole being silent to someone reading the header, which is where a
 * consumer meets it.
 *
 * Why the hole is not closed by adding thirteen #defines.  200809L is a
 * COMPILE-TIME promise; the whole reason for a constant an application
 * tests with #if is that it cannot be re-checked at run time, so a wrong
 * answer here cannot be corrected the way a wrong sysconf() answer can.
 * Not one of the thirteen names an option this tree actually supplies:
 *
 *   - there is no <pthread.h> at all, which leaves _POSIX_THREADS,
 *     _POSIX_BARRIERS, _POSIX_READER_WRITER_LOCKS, _POSIX_SPIN_LOCKS
 *     and _POSIX_TIMEOUTS with no interfaces behind them;
 *   - no <aio.h> (_POSIX_ASYNCHRONOUS_IO), no <semaphore.h>
 *     (_POSIX_SEMAPHORES), no <sys/mman.h> (_POSIX_MAPPED_FILES,
 *     _POSIX_MEMORY_PROTECTION);
 *   - _POSIX_CLOCK_SELECTION needs pthread_condattr_setclock() as well
 *     as the clock_nanosleep() we do have.
 *
 * _POSIX_THREAD_SAFE_FUNCTIONS is the one arguable case, and is left out
 * with the rest: all nineteen of its interfaces do exist here
 * (getpwnam_r(), localtime_r(), flockfile(), ...), but the constant sits
 * inside the option group _POSIX_THREADS heads, and a consumer reading
 * the two together would be misled by a tree with no threads.
 * sysconf(_SC_THREAD_SAFE_FUNCTIONS) answers -1 to match, because a
 * header that stays silent while sysconf() says "supported" is the same
 * contradiction pointing the other way.
 *
 * And POSIX leaves no third answer.  For every OTHER constant in that
 * section the page says "if defined ... shall have a value of -1, 0, or
 * greater", so -1 spells "not supported"; these thirteen have no such
 * form, and silence is the only truthful signal left.  Same distinction
 * <limits.h> already records against _POSIX_THREAD_KEYS_MAX and friends:
 * a number the standard prints is not a claim, an option constant is. */

/* unistd.h.html: "The <unistd.h> header shall define the following
 * symbolic constants for pathconf()".  Unconditional -- a variable an
 * implementation cannot associate with a file still needs a name for
 * pathconf() to reject, which fpathconf.html's "[EINVAL] The
 * implementation does not support an association of the variable name
 * with the specified file" presumes.  Values follow the numbering glibc
 * and musl share, so a consumer built against either sees the selectors
 * it expects; the gap at 12 is their non-POSIX _PC_SOCK_MAXBUF, left
 * free rather than reused. */
#define _PC_LINK_MAX	0
#define _PC_MAX_CANON	1
#define _PC_MAX_INPUT	2
#define _PC_NAME_MAX	3
#define _PC_PATH_MAX	4
#define _PC_PIPE_BUF	5
#define _PC_CHOWN_RESTRICTED	6
#define _PC_NO_TRUNC	7
#define _PC_VDISABLE	8
#define _PC_SYNC_IO	9
#define _PC_ASYNC_IO	10
#define _PC_PRIO_IO	11
#define _PC_FILESIZEBITS	13
#define _PC_REC_INCR_XFER_SIZE	14
#define _PC_REC_MAX_XFER_SIZE	15
#define _PC_REC_MIN_XFER_SIZE	16
#define _PC_REC_XFER_ALIGN	17
#define _PC_ALLOC_SIZE_MIN	18
#define _PC_SYMLINK_MAX	19
#define _PC_2_SYMLINKS	20
/* Issue 7 added _PC_TIMESTAMP_RESOLUTION after glibc and musl had
 * numbered the block above, so neither has a value to copy; 21 extends
 * their sequence rather than opening a private range for one name. */
#define _PC_TIMESTAMP_RESOLUTION	21

/* unistd.h.html: "The <unistd.h> header shall define the following
 * symbolic constants for sysconf()", 125 names, unconditional.  A name
 * this implementation has no real limit for is still valid input:
 * sysconf() reports that with -1 and errno UNTOUCHED (see
 * src/unistd/sysconf.c), which is what separates "option absent" from
 * the [EINVAL] of a name that does not exist.  Defining the names
 * without that second half would only move the failure -- the symbol
 * would appear, a configure probe would pass, and the consumer would
 * stand down its own replacement on the strength of it.
 *
 * Numbering follows glibc/musl, which the fifteen names that were here
 * first already did; the gaps are their non-POSIX entries
 * (_SC_EQUIV_CLASS_MAX at 41, the _SC_PII_* block from 53, the
 * _SC_LEVEL*_CACHE_* block from 185, ...), left free so a later addition
 * can take the value everyone else uses. */
#define _SC_ARG_MAX	0
#define _SC_CHILD_MAX	1
#define _SC_CLK_TCK	2
#define _SC_NGROUPS_MAX	3
#define _SC_OPEN_MAX	4
#define _SC_STREAM_MAX	5
#define _SC_TZNAME_MAX	6
#define _SC_JOB_CONTROL	7
#define _SC_SAVED_IDS	8
#define _SC_REALTIME_SIGNALS	9
#define _SC_PRIORITY_SCHEDULING	10
#define _SC_TIMERS	11
#define _SC_ASYNCHRONOUS_IO	12
#define _SC_PRIORITIZED_IO	13
#define _SC_SYNCHRONIZED_IO	14
#define _SC_FSYNC	15
#define _SC_MAPPED_FILES	16
#define _SC_MEMLOCK	17
#define _SC_MEMLOCK_RANGE	18
#define _SC_MEMORY_PROTECTION	19
#define _SC_MESSAGE_PASSING	20
#define _SC_SEMAPHORES	21
#define _SC_SHARED_MEMORY_OBJECTS	22
#define _SC_AIO_LISTIO_MAX	23
#define _SC_AIO_MAX	24
#define _SC_AIO_PRIO_DELTA_MAX	25
#define _SC_DELAYTIMER_MAX	26
#define _SC_MQ_OPEN_MAX	27
#define _SC_MQ_PRIO_MAX	28
#define _SC_VERSION	29
#define _SC_PAGESIZE	30
#define _SC_RTSIG_MAX	31
#define _SC_SEM_NSEMS_MAX	32
#define _SC_SEM_VALUE_MAX	33
#define _SC_SIGQUEUE_MAX	34
#define _SC_TIMER_MAX	35
#define _SC_BC_BASE_MAX	36
#define _SC_BC_DIM_MAX	37
#define _SC_BC_SCALE_MAX	38
#define _SC_BC_STRING_MAX	39
#define _SC_COLL_WEIGHTS_MAX	40
#define _SC_EXPR_NEST_MAX	42
#define _SC_LINE_MAX	43
#define _SC_RE_DUP_MAX	44
#define _SC_2_VERSION	46
#define _SC_2_C_BIND	47
#define _SC_2_C_DEV	48
#define _SC_2_FORT_DEV	49
#define _SC_2_FORT_RUN	50
#define _SC_2_SW_DEV	51
#define _SC_2_LOCALEDEF	52
#define _SC_IOV_MAX	60
#define _SC_THREADS	67
#define _SC_THREAD_SAFE_FUNCTIONS	68
#define _SC_GETGR_R_SIZE_MAX	69
#define _SC_GETPW_R_SIZE_MAX	70
#define _SC_LOGIN_NAME_MAX	71
#define _SC_TTY_NAME_MAX	72
#define _SC_THREAD_DESTRUCTOR_ITERATIONS	73
#define _SC_THREAD_KEYS_MAX	74
#define _SC_THREAD_STACK_MIN	75
#define _SC_THREAD_THREADS_MAX	76
#define _SC_THREAD_ATTR_STACKADDR	77
#define _SC_THREAD_ATTR_STACKSIZE	78
#define _SC_THREAD_PRIORITY_SCHEDULING	79
#define _SC_THREAD_PRIO_INHERIT	80
#define _SC_THREAD_PRIO_PROTECT	81
#define _SC_THREAD_PROCESS_SHARED	82
#define _SC_NPROCESSORS_CONF	83
#define _SC_NPROCESSORS_ONLN	84
#define _SC_PHYS_PAGES	85
#define _SC_ATEXIT_MAX	87
#define _SC_XOPEN_VERSION	89
#define _SC_XOPEN_UNIX	91
#define _SC_XOPEN_CRYPT	92
#define _SC_XOPEN_ENH_I18N	93
#define _SC_XOPEN_SHM	94
#define _SC_2_CHAR_TERM	95
#define _SC_2_UPE	97
#define _SC_XOPEN_REALTIME	130
#define _SC_XOPEN_REALTIME_THREADS	131
#define _SC_ADVISORY_INFO	132
#define _SC_BARRIERS	133
#define _SC_CLOCK_SELECTION	137
#define _SC_CPUTIME	138
#define _SC_THREAD_CPUTIME	139
#define _SC_MONOTONIC_CLOCK	149
#define _SC_READER_WRITER_LOCKS	153
#define _SC_SPIN_LOCKS	154
#define _SC_REGEXP	155
#define _SC_SHELL	157
#define _SC_SPAWN	159
#define _SC_SPORADIC_SERVER	160
#define _SC_THREAD_SPORADIC_SERVER	161
#define _SC_TIMEOUTS	164
#define _SC_TYPED_MEMORY_OBJECTS	165
#define _SC_2_PBS	168
#define _SC_2_PBS_ACCOUNTING	169
#define _SC_2_PBS_LOCATE	170
#define _SC_2_PBS_MESSAGE	171
#define _SC_2_PBS_TRACK	172
#define _SC_SYMLOOP_MAX	173
#define _SC_2_PBS_CHECKPOINT	175
#define _SC_V6_ILP32_OFF32	176
#define _SC_V6_ILP32_OFFBIG	177
#define _SC_V6_LP64_OFF64	178
#define _SC_V6_LPBIG_OFFBIG	179
#define _SC_HOST_NAME_MAX	180
#define _SC_TRACE	181
#define _SC_TRACE_EVENT_FILTER	182
#define _SC_TRACE_INHERIT	183
#define _SC_TRACE_LOG	184
#define _SC_IPV6	235
#define _SC_RAW_SOCKETS	236
#define _SC_V7_ILP32_OFF32	237
#define _SC_V7_ILP32_OFFBIG	238
#define _SC_V7_LP64_OFF64	239
#define _SC_V7_LPBIG_OFFBIG	240
#define _SC_SS_REPL_MAX	241
#define _SC_TRACE_EVENT_NAME_MAX	242
#define _SC_TRACE_NAME_MAX	243
#define _SC_TRACE_SYS_MAX	244
#define _SC_TRACE_USER_EVENT_MAX	245
#define _SC_XOPEN_STREAMS	246
#define _SC_THREAD_ROBUST_PRIO_INHERIT	247
#define _SC_THREAD_ROBUST_PRIO_PROTECT	248

/* The two mandated names glibc and musl leave for the implementation to
 * number, put above everything either of them uses so a later
 * glibc-compatible addition cannot collide with them.
 *
 * _SC_PAGE_SIZE is NOT an alias of _SC_PAGESIZE here, which is the one
 * place this list departs from those two.  POSIX lists both names and
 * nowhere says they share a value, and a selector is only usable if it
 * can be a distinct switch label -- two names sharing one value cannot
 * both appear in src/unistd/sysconf.c's switch, so the alias spelling
 * makes one of two mandated names unimplementable.  The two *answers*
 * are identical, which is all limits.h.html asks for when it says
 * {PAGESIZE} is "equivalent to {PAGE_SIZE}". */
#define _SC_PAGE_SIZE	300
#define _SC_XOPEN_UUCP	301

/* unistd.h.html: "The <unistd.h> header shall define the following
 * symbolic constants for the confstr() function", 31 names.  Numbering
 * follows glibc/musl, including their reserved-but-unused slots for the
 * _LINTFLAGS members POSIX deleted in Issue 7 (every fourth value in
 * the 1116..1147 block), so a name added back later lands where those
 * two put it.  _CS_POSIX_V7_THREADS_* is past the range either numbers
 * and continues the sequence.
 *
 * The definitions are all this claims.  confstr() answers these with
 * the empty string today and cannot be shown to do otherwise while the
 * separately recorded defect stands (POSIX-COVERAGE.md, "confstr()
 * reports success for an invalid name"): until an unrecognized name is
 * rejected there is no observable difference between "recognized, no
 * value" and "not recognized", so nothing here should be read as
 * confstr() having answers for them. */
#define _CS_PATH	0
#define _CS_POSIX_V6_WIDTH_RESTRICTED_ENVS	1
#define _CS_POSIX_V7_WIDTH_RESTRICTED_ENVS	5
#define _CS_POSIX_V6_ILP32_OFF32_CFLAGS	1116
#define _CS_POSIX_V6_ILP32_OFF32_LDFLAGS	1117
#define _CS_POSIX_V6_ILP32_OFF32_LIBS	1118
#define _CS_POSIX_V6_ILP32_OFFBIG_CFLAGS	1120
#define _CS_POSIX_V6_ILP32_OFFBIG_LDFLAGS	1121
#define _CS_POSIX_V6_ILP32_OFFBIG_LIBS	1122
#define _CS_POSIX_V6_LP64_OFF64_CFLAGS	1124
#define _CS_POSIX_V6_LP64_OFF64_LDFLAGS	1125
#define _CS_POSIX_V6_LP64_OFF64_LIBS	1126
#define _CS_POSIX_V6_LPBIG_OFFBIG_CFLAGS	1128
#define _CS_POSIX_V6_LPBIG_OFFBIG_LDFLAGS	1129
#define _CS_POSIX_V6_LPBIG_OFFBIG_LIBS	1130
#define _CS_POSIX_V7_ILP32_OFF32_CFLAGS	1132
#define _CS_POSIX_V7_ILP32_OFF32_LDFLAGS	1133
#define _CS_POSIX_V7_ILP32_OFF32_LIBS	1134
#define _CS_POSIX_V7_ILP32_OFFBIG_CFLAGS	1136
#define _CS_POSIX_V7_ILP32_OFFBIG_LDFLAGS	1137
#define _CS_POSIX_V7_ILP32_OFFBIG_LIBS	1138
#define _CS_POSIX_V7_LP64_OFF64_CFLAGS	1140
#define _CS_POSIX_V7_LP64_OFF64_LDFLAGS	1141
#define _CS_POSIX_V7_LP64_OFF64_LIBS	1142
#define _CS_POSIX_V7_LPBIG_OFFBIG_CFLAGS	1144
#define _CS_POSIX_V7_LPBIG_OFFBIG_LDFLAGS	1145
#define _CS_POSIX_V7_LPBIG_OFFBIG_LIBS	1146
#define _CS_V6_ENV	1148
#define _CS_V7_ENV	1149
#define _CS_POSIX_V7_THREADS_CFLAGS	1150
#define _CS_POSIX_V7_THREADS_LDFLAGS	1151

#ifdef __cplusplus
}
#endif

#endif
