/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Internal interfaces shared between the parts of ntlibc.  Nothing in here
 * is visible to programs; everything begins with a double underscore.
 */
#ifndef _NTLIBC_LIBC_H
#define _NTLIBC_LIBC_H

#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include "nt.h"

/* ---- process-wide state ------------------------------------------------ */
extern PPEB __peb;                           /* this process's PEB */
extern char **environ;
#define __environ environ
extern char **__argv;
extern int __argc;
extern char *__progname;                     /* argv[0] */
extern char *__progname_full;                /* image path, UTF-8 */

/* environ helpers shared by getenv.c and setenv.c.  __env_find returns the
 * slot in environ holding "name=..." for the first l bytes of name, or
 * NULL.  __putenv installs s (which must contain a '='; l is the length of
 * the name part) and takes ownership of `owned` if non-NULL. */
char **__env_find(const char *name, size_t l);
int __putenv(char *s, size_t l, char *owned);

PTEB __teb(void);                            /* this thread's TEB */
#define __process_heap() (__peb->ProcessHeap)

/* ---- errno ------------------------------------------------------------- */
int __errno_from_status(NTSTATUS);           /* map, do not set */
int __set_errno_status(NTSTATUS);            /* errno = map(st); return -1 */
int __errno_from_doserror(unsigned);

/* ---- UTF-8 <-> UTF-16 -------------------------------------------------- */
/* Convert a NUL-terminated UTF-8 string into a freshly malloc'd
 * NUL-terminated UTF-16 one; NULL with errno on failure.  *wlen, if not
 * NULL, receives the length in WCHARs excluding the terminator. */
WCHAR *__utf8_to_utf16(const char *, size_t *wlen);
/* Convert n WCHARs into a freshly malloc'd NUL-terminated UTF-8 string. */
char *__utf16_to_utf8(const WCHAR *, size_t n);
/* Convert into a caller-supplied buffer; returns bytes written excluding
 * the terminator, or -1 with errno (ERANGE if the buffer is too small). */
int __utf16_to_utf8_buf(const WCHAR *, size_t n, char *, size_t);
size_t wcslen_(const WCHAR *);

/* ---- UNICODE_STRING ---------------------------------------------------- */
/* The longest string a UNICODE_STRING can describe: Length counts bytes
 * in a USHORT, and MaximumLength has to hold Length plus a terminating
 * NUL, so 65535 bytes minus that NUL -- 32766 UTF-16 code units.  A
 * longer string narrowed into those fields does not truncate, it wraps,
 * and the object manager is handed some prefix of what was meant; every
 * hand-built UNICODE_STRING that a caller's data reaches checks against
 * this before narrowing. */
#define __US_MAX_WCHARS ((size_t)((0xffffu - sizeof(WCHAR)) / sizeof(WCHAR)))

/* ---- paths ------------------------------------------------------------- */
/* A path ready to hand to the object manager: the NT path in a
 * UNICODE_STRING, the OBJECT_ATTRIBUTES wrapping it, and the buffer it
 * lives in, which __ntpath_free releases. */
struct __ntpath {
	UNICODE_STRING nt;
	OBJECT_ATTRIBUTES oa;
	WCHAR *dos;            /* the DOS (Win32) form, for RtlDosSearchPath etc. */
	void *buf;             /* RtlDosPathNameToNtPathName_U's allocation */
};
/* Translate a POSIX-ish path into NT form.  Forward slashes become
 * backslashes; "/dev/null" becomes NUL; a relative path is resolved
 * against the current directory by the Rtl.  Returns 0 or -1 with errno. */
int __ntpath(const char *path, struct __ntpath *out, ULONG attributes);
/* Like __ntpath but the path is relative to the directory handle dirfd
 * refers to (AT_FDCWD for the current directory). */
int __ntpath_at(int dirfd, const char *path, struct __ntpath *out, ULONG attributes);
void __ntpath_free(struct __ntpath *);
/* The DOS-form absolute path of a handle, UTF-8, malloc'd. */
char *__handle_path(HANDLE);

/* The guts of open()/openat(): resolve and open, handing back the raw
 * handle and its __FD_* classification without installing a descriptor.
 * Returns 0, or -1 with errno. */
int __open_handle(int dirfd, const char *path, int flags, unsigned mode,
                  HANDLE *out, int *typeout);
/* The guts of unlink()/rmdir()/unlinkat(); isdir selects the rmdir
 * behaviour.  Returns 0, or -1 with errno. */
int __unlink_at(int dirfd, const char *path, int isdir);
/* The guts of stat()/fstat(): fill *st from an open handle of __FD_* type
 * `type`.  Returns 0, or -1 with errno.  (struct stat is only ever used
 * through this pointer here, so <sys/stat.h> stays out of this header.) */
struct stat;
int __fstat_handle(HANDLE h, int type, struct stat *st);

/* ---- the descriptor table ---------------------------------------------- */
#define FD_MAX 1024

enum {
	__FD_FILE = 1,         /* a disk file */
	__FD_DIR,              /* a directory handle */
	__FD_CONSOLE,          /* a console (ConDrv) handle */
	__FD_PIPE,             /* a pipe, named or anonymous */
	__FD_CHAR,             /* NUL, COM, and other character devices */
	__FD_SOCKET,
	__FD_UNKNOWN
};

struct __fd {
	HANDLE h;              /* NULL when the slot is free */
	unsigned flags;        /* O_APPEND, O_NONBLOCK, O_CLOEXEC as given to open */
	unsigned char type;    /* __FD_* */
	unsigned char eof;     /* a pipe/console that has reported end of input */
	unsigned char dirflag; /* for directories: 0 or FILE_OPEN_REPARSE_POINT used */
	unsigned char pad;
	long long pos;         /* the position of an O_APPEND/async-opened handle; -1 = use the kernel's */
};

extern struct __fd __fds[FD_MAX];

int __fd_alloc(int lowest);                  /* a free slot >= lowest, or -1 (EMFILE) */
int __fd_install(HANDLE, unsigned flags, int type);    /* alloc + fill; -1 with errno */
int __fd_install_at(int fd, HANDLE, unsigned flags, int type);
struct __fd *__fd_get(int fd);               /* NULL with errno=EBADF */
HANDLE __fd_handle(int fd);                  /* NULL with errno=EBADF */
int __fd_pos_save(HANDLE, long long *pos);   /* FilePositionInformation; -1 with errno */
void __fd_pos_restore(HANDLE, long long pos);/* put it back after positioned I/O */
int __handle_type(HANDLE);                   /* classify by device type */
int __fd_close_all_cloexec(void);
void __fd_init(void);                        /* fds 0-2 from the PEB, 3+ from RuntimeData */
/* Serialise the inheritable part of the descriptor table into a freshly
 * malloc'd blob for a child's RTL_USER_PROCESS_PARAMETERS RuntimeData;
 * *len receives its size.  NULL with errno on failure. */
void *__fd_runtime_data(size_t *len);

/* ---- children ---------------------------------------------------------- */
/* The size of the statically allocated part of the child table.  It is
 * not a limit: the table grows onto the heap past this point rather than
 * dropping a child's process handle, which would make the child
 * unreapable for good (see src/process/children.c). */
#define CHILD_MAX_ 256

/* Refuse to grow the child table past this many entries; a process with
 * a million unreaped children has a leak, not a capacity problem, and
 * the cap keeps child_grow()'s doubling away from integer overflow.
 *
 * This, not CHILD_MAX_, is what sysconf(_SC_CHILD_MAX) reports: NT has
 * no fixed per-user process limit for it to describe, so the honest
 * answer is the ceiling on what this libc can still waitpid() for.
 * Reporting CHILD_MAX_ there would understate it by four orders of
 * magnitude now that the table grows. */
#define CHILD_CAP_LIMIT_ (1 << 20)
struct __child {
	int pid;
	HANDLE h;
	int done;               /* reaped status is available */
	int status;
};
extern struct __child *__children;   /* __child_cap entries, pid==0 is free */
extern int __child_cap;
int __child_add(int pid, HANDLE);
struct __child *__child_find(int pid);
void __child_remove(struct __child *);

/* Start a program: the equivalent of posix_spawn.  Returns the child pid
 * (tracked in __children) or -1 with errno. */
int __spawn(const char *path, char *const argv[], char *const envp[]);
/* Resolve a program name the way execvp does: PATH search plus the .exe
 * suffix Windows wants.  Returns a malloc'd absolute path or NULL. */
char *__find_program(const char *name, int use_path);

/* ---- heap -------------------------------------------------------------- */
void *__malloc(size_t);
void __free(void *);

/* ---- time -------------------------------------------------------------- */
#define __TICKS_PER_SEC 10000000LL
#define __TICKS_1601_TO_1970 116444736000000000LL
static inline long long __nt_to_unix_sec(long long t) { return (t - __TICKS_1601_TO_1970) / __TICKS_PER_SEC; }
static inline long __nt_to_unix_nsec(long long t) { return (long)((t - __TICKS_1601_TO_1970) % __TICKS_PER_SEC) * 100; }
static inline long long __unix_to_nt(long long sec, long nsec) { return sec * __TICKS_PER_SEC + nsec / 100 + __TICKS_1601_TO_1970; }

/* ---- stdio internals --------------------------------------------------- */
void __stdio_exit(void);                     /* flush everything at exit */

/* ---- exit -------------------------------------------------------------- */
void __funcs_on_exit(void);
void __libc_exit_fini(void);
_Noreturn void __nt_exit(int);

/* ---- signals ------------------------------------------------------------ */
/* Windows has no separate "killed by signal" field: a process exit code is
 * one 32-bit DWORD, and waitpid() has nothing else to look at.  A process
 * this library ends on behalf of a signal (kill(), abort(), the default
 * action in __raise_internal(), the vectored exception handler) therefore
 * exits with __NT_SIGNAL_EXIT(sig) and waitpid() decodes exactly that.
 *
 * 0xE0DE0000 is an NTSTATUS with severity error (bits 31-30) and the
 * customer-defined bit (bit 29) set, so it can never be an NT status code
 * the system produces, and it is far outside the 0..255 an exit() can
 * return -- the two spaces cannot collide.  (The old scheme, 128 + signo,
 * is a *shell* convention; using it here stole exit codes 129..192 from
 * exit() and made e.g. exit(130) look like death by SIGABRT.) */
#define __NT_SIGNAL_EXIT_BASE 0xE0DE0000u
#define __NT_SIGNAL_EXIT(sig) ((int)(__NT_SIGNAL_EXIT_BASE | ((unsigned)(sig) & 0x7fu)))
#define __NT_IS_SIGNAL_EXIT(code) (((unsigned)(code) & ~0x7fu) == __NT_SIGNAL_EXIT_BASE)

void __signal_init(void);
int __raise_internal(int);

/* ---- misc -------------------------------------------------------------- */
int __is_wow64(void);
unsigned __rand_next(void);
/* getopt's diagnostic writer, shared with getopt_long. */
void __getopt_msg(const char *msg, const char *optname, size_t l);
/* The strerror table lookup, shared with strerror_r.  Never NULL. */
const char *__strerror_msg(int e);

/* ---- WOW64 clone repair (see arch/i386/src/wow64_fixup.c) -------------- */
/* Repair the FS-base and stuck-SRW-lock damage RtlCloneUserProcess leaves
 * in a cloned child under WOW64 -- see fork.c's header comment and
 * wow64_fixup.c's for why this is needed and what it does.  process and
 * thread are the clone's handles, still CREATE_SUSPENDED; call this
 * before ever resuming the thread.  Only meaningful, and only
 * implemented, on i386 -- WOW64 has no meaning for a native x86_64
 * ntlibc process, so this is a no-op there. */
#ifdef __i386__
void __wow64_fixup_clone(HANDLE process, HANDLE thread);
#else
static inline void __wow64_fixup_clone(HANDLE process, HANDLE thread) { (void)process; (void)thread; }
#endif

#define __container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

#endif
