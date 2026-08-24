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
pid_t vfork(void);
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
unsigned ualarm(unsigned, unsigned);  /* undefined-ok: alarm() itself is
	already a stub returning 0 (src/unistd/sleep.c) -- there is no
	SIGALRM delivery to build a microsecond-resolution version on top of */
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
int setgroups(size_t, const gid_t *);
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

#define _PC_LINK_MAX	0
#define _PC_MAX_CANON	1
#define _PC_MAX_INPUT	2
#define _PC_NAME_MAX	3
#define _PC_PATH_MAX	4
#define _PC_PIPE_BUF	5
#define _PC_CHOWN_RESTRICTED	6
#define _PC_NO_TRUNC	7
#define _PC_VDISABLE	8

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
#define _SC_VERSION	29
#define _SC_PAGESIZE	30
#define _SC_PAGE_SIZE	30
#define _SC_NPROCESSORS_CONF	83
#define _SC_NPROCESSORS_ONLN	84
#define _SC_PHYS_PAGES	85
#define _SC_LINE_MAX	43
#define _SC_HOST_NAME_MAX	180

#define _CS_PATH	0

#ifdef __cplusplus
}
#endif

#endif
