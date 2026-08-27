/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ntstubs.c -- the ntdll side of the world, for native (Linux) builds.
 *
 * The point of this file is to let the *real* src/*.c be compiled and
 * linked by a native clang with ASan/UBSan/libFuzzer.  Nothing here is
 * part of ntlibc: it stands in for ntdll.dll, which is the one thing a
 * native build cannot have.  Everything ntlibc itself computes -- format
 * conversion, number parsing, buffer sizing -- runs unmodified.
 *
 * Three grades of stub live here:
 *
 *   real       RtlAllocateHeap & friends (ASan's allocator, so ntlibc's
 *              heap use is redzone-checked); the file system -- a
 *              simulated volume in memory, described above NtCreateFile
 *              below, with the paths, handles, directories, pipes and
 *              information classes ntlibc actually uses; process
 *              creation, which fork+execve really performs; process
 *              cloning (RtlCloneUserProcess, so fork()), which a real
 *              host fork(2) performs, described above it; the clocks;
 *              RtlUTF8ToUnicodeN/RtlUnicodeToUTF8N (a from-spec
 *              conversion; see the note above them), RtlInitUnicodeString.
 *   plausible  NtQueryVolumeInformationFile for descriptors 0-2, which a
 *              native run cannot classify beyond "a character device".
 *   refusing   everything else: STATUS_NOT_IMPLEMENTED.  Any ntlibc code
 *              path that reaches one of those is simply not covered by
 *              the native build, and will report an error rather than
 *              pretend to work.  What is left is chiefly the
 *              object-manager symbolic links and NtFsControlFile.
 *
 * Host services are reached through syscall(2) rather than through
 * write()/read()/malloc(), because those names belong to ntlibc in this
 * link and calling them would recurse straight back into the library.
 *
 * A caveat that outranks everything in this file: NTSTATUS is `long`
 * (src/internal/nt.h), which is 32-bit on the NT target and 64-bit here,
 * so nt.h's own ((NTSTATUS)0xC0000034L) is a *positive* number natively
 * and NT_SUCCESS() answers "success" for every failure status.  Until
 * that typedef is int, no error this file reports is seen as an error by
 * the library, and the tests that check an error path are skipped by
 * tools/asan-build.sh for that reason.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "libc.h"

extern long syscall(long, ...);
extern void *__interceptor_malloc(size_t);
extern void __interceptor_free(void *);
extern void *__interceptor_realloc(void *, size_t);
extern size_t __sanitizer_get_allocated_size(const void *);

#ifndef STATUS_SOME_NOT_MAPPED
#define STATUS_SOME_NOT_MAPPED ((NTSTATUS)0x80000005L)
#endif
#ifndef STATUS_END_OF_FILE
#define STATUS_END_OF_FILE ((NTSTATUS)0xC0000011L)
#endif

#define SYS_read  0
#define SYS_write 1
#define SYS_mmap  9
#define SYS_exit_group 231
#define SYS_clock_gettime 228
#define SYS_nanosleep 35
#define SYS_getpid 39
#define SYS_getppid 110
#define SYS_close 3
#define SYS_ftruncate 77
#define SYS_memfd_create 319
#define SYS_ioctl 16
#define SYS_lseek 8

/* Handles are (fd + 1), so that 0 stays "no handle". */
#define H2FD(h) ((int)(long)(h) - 1)
#define FD2H(f) ((HANDLE)(long)((f) + 1))
#define SHIM_TOKEN_HANDLE ((HANDLE)(LONG_PTR)-4)

/* ------------------------------------------------------------------ heap */

PVOID NTAPI RtlAllocateHeap(PVOID heap, ULONG flags, SIZE_T n)
{
	void *p;
	(void)heap;
	p = __interceptor_malloc(n ? n : 1);
	if (p && (flags & HEAP_ZERO_MEMORY)) memset(p, 0, n);
	return p;
}

BOOLEAN NTAPI RtlFreeHeap(PVOID heap, ULONG flags, PVOID p)
{
	(void)heap; (void)flags;
	__interceptor_free(p);
	return 1;
}

PVOID NTAPI RtlReAllocateHeap(PVOID heap, ULONG flags, PVOID p, SIZE_T n)
{
	(void)heap; (void)flags;
	return __interceptor_realloc(p, n ? n : 1);
}

SIZE_T NTAPI RtlSizeHeap(PVOID heap, ULONG flags, PVOID p)
{
	(void)heap; (void)flags;
	return p ? __sanitizer_get_allocated_size(p) : 0;
}

PVOID NTAPI RtlCreateHeap(ULONG a, PVOID b, SIZE_T c, SIZE_T d, PVOID e, PVOID f)
{
	(void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
	return (PVOID)(long)0x1000;
}

/* ------------------------------------------------------- the PEB and TEB */

static RTL_USER_PROCESS_PARAMETERS shim_pp;
static PEB shim_peb;
static char shim_teb[4096];
PPEB __peb = &shim_peb;

PTEB __teb(void)
{
	/* On NT this is gs:0x30.  Natively there is no TEB, and the only
	 * thing ntlibc reads out of it is the last-error slot, so a plain
	 * zeroed block is enough. */
	return (PTEB)shim_teb;
}

PPEB NTAPI RtlGetCurrentPeb(void) { return &shim_peb; }

/* RtlGetVersion (src/misc/uname.c's uname()): "plausible" grade -- a
 * native Linux run has no real NT version to report, but the real
 * function is documented to always succeed and never leaves its
 * output unfilled, so a NOTIMPL/STATUS_NOT_IMPLEMENTED stub would be
 * dishonest about the one thing every real caller can rely on (that
 * the struct comes back filled in). A fixed, clearly-a-placeholder
 * Windows 10 version number is closer to a real answer than either
 * leaving the struct zeroed or refusing outright. */
NTSTATUS NTAPI RtlGetVersion(RTL_OSVERSIONINFOW *vi)
{
	vi->dwMajorVersion = 10;
	vi->dwMinorVersion = 0;
	vi->dwBuildNumber = 19045;
	vi->dwPlatformId = 2;   /* VER_PLATFORM_WIN32_NT */
	vi->szCSDVersion[0] = 0;
	return STATUS_SUCCESS;
}

/*
 * Natively there is no crt1.o: glibc's startup calls main() directly, so
 * the parts of __libc_start_main() that ntlibc code depends on have to
 * happen in a constructor instead.  ASan's own initialisation runs at
 * priority 1, ahead of this.
 *
 * A constructor that declares (argc, argv, envp) parameters is handed the
 * process's real ones -- a glibc-specific extension of the .init_array
 * calling convention (the same values __libc_start_main gives main()),
 * good only for a native Linux build, which is all this file is for.  It
 * exists here for one reason: telling an execve()'d child apart from a
 * process the test harness started directly, and recovering what the
 * *real* argv/envp said despite __argv/environ below being reset to a
 * placeholder on every start.  See the two uses below and in
 * RtlCreateUserProcess.
 */
char **__argv;
int __argc;
char *__progname;
char *__progname_full;
static char *shim_argv[2] = { (char *)"ntlibc-native", 0 };

/* RtlCreateUserProcess appends this to the envp it hands the real execve,
 * so that the child's constructor -- which otherwise has no way to tell
 * "started fresh by the test harness" from "execve'd by this file" --
 * knows to rebuild environ from its real, inherited envp instead of
 * resetting it to empty.  Never left in the environment __ntshim_init()
 * builds: filtered out below. */
#define XCHILD_MARK "_NTLIBC_XCHILD=1"
#define XHOST_PREFIX "_NTLIBC_XHOST="
#define XVFS_PREFIX "_NTLIBC_XVFS="
static char *host_self;
static int vfs_snapshot_fd = -1;

/*
 * The exit code a host wait4() reports is 8 bits (WEXITSTATUS); the exit
 * code this file's own NtTerminateProcess is asked for can be a full NT
 * status -- in particular __NT_SIGNAL_EXIT(sig) = 0xE0DE0000 | sig, which
 * a plain _exit(status) low-byte-truncates down to just `sig`, indistin-
 * guishable on the host from a process that legitimately called
 * exit(sig).  That is the whole reason waitpid-overflow and posix-signal
 * are skipped natively (see not_native() in tools/asan-build.sh) -- but
 * the fix does not have to live in src/*.c, which already gets this
 * right for the real NT target; it only has to get the *full* code from
 * the dying process to the one that reaps it, out of band from the
 * host's own 8-bit accounting.
 *
 * A small table in an anonymous, memfd-backed MAP_SHARED page does that:
 * whichever process ends another (NtTerminateProcess, self or via a
 * handle) records {pid, code} in it before the host-visible exit/kill
 * happens, and proc_poll() below prefers that record over the
 * host-status heuristic whenever one matches the pid it just reaped.
 * The mapping needs no extra plumbing to reach a child: RtlCloneUserProcess's
 * real fork(2) shares it automatically (fork does not unmap anything),
 * and RtlCreateUserProcess's real fork+execve passes the *fd number* to
 * the child via envp (XSTATUS_FD_MARK below), the same trick XCHILD_MARK
 * already uses for "was this execve'd by this file" -- the fd itself
 * survives execve (memfd_create() does not set FD_CLOEXEC here), so the
 * child's own __ntshim_init() just re-mmaps the fd it inherited instead
 * of creating a fresh, unrelated table. */
#define XSTATUS_FD_PREFIX "_NTLIBC_XSTATUS_FD="
#define XSTATUS_N 4096
struct xstatus_ent { int pid; int code; };
static struct xstatus_ent *xstatus_tab;
static int xstatus_fd = -1;

static void xstatus_init(char **envp)
{
	int i, fd = -1;
	size_t bytes = sizeof(struct xstatus_ent) * (size_t)XSTATUS_N;

	if (envp) for (i = 0; envp[i]; i++)
		if (!strncmp(envp[i], XSTATUS_FD_PREFIX, sizeof(XSTATUS_FD_PREFIX) - 1)) {
			/* No <stdlib.h> here (this file only pulls in <stdio.h> and
			 * <string.h>, ntlibc's own -- atoi() would either be
			 * undeclared or, worse, ntlibc's own not-yet-initialised
			 * one), so a minimal digit parse in place of it. */
			const char *s = envp[i] + sizeof(XSTATUS_FD_PREFIX) - 1;
			fd = 0;
			while (*s >= '0' && *s <= '9') fd = fd * 10 + (*s++ - '0');
			break;
		}
	if (fd < 0) {
		fd = (int)syscall(SYS_memfd_create, "ntlibc-xstatus", 0);
		if (fd < 0) return;                     /* degrade to the host-status heuristic */
		if (syscall(SYS_ftruncate, fd, (long)bytes) < 0) { syscall(SYS_close, fd); return; }
	}
	xstatus_tab = (struct xstatus_ent *)syscall(SYS_mmap, 0, (long)bytes,
	                                            3 /*PROT_READ|PROT_WRITE*/, 1 /*MAP_SHARED*/, fd, 0);
	if (xstatus_tab == (void *)-1) { xstatus_tab = 0; return; }
	xstatus_fd = fd;
}

/* Record the full code a process is about to end with, so proc_poll() on
 * the reaping side can recover it despite the host's 8-bit wait status.
 * A no-op if this process never got a table (memfd_create/mmap failed --
 * proc_poll()'s heuristic fallback is all that's left in that case). */
static void xstatus_record(int pid, int code)
{
	if (!xstatus_tab) return;
	xstatus_tab[(unsigned)pid % XSTATUS_N].pid = pid;
	xstatus_tab[(unsigned)pid % XSTATUS_N].code = code;
}

static void vfs_init(void);            /* the simulated file system, below */
static void materialize_argv0(const char *name, const char *host);  /* below, once nodes exist */
static void vfs_snapshot_init(char **envp);       /* below, once nodes exist */
static int vfs_snapshot_export(void);             /* below, once nodes exist */
static void mirror_init(char **envp);             /* the corpus mirror, below */

__attribute__((constructor(200))) void __ntshim_init(int argc, char **argv, char **envp)
{
	vfs_init();
	shim_peb.ProcessHeap = (PVOID)(long)0x1000;
	shim_peb.ProcessParameters = &shim_pp;
	shim_pp.StandardInput  = FD2H(0);
	shim_pp.StandardOutput = FD2H(1);
	shim_pp.StandardError  = FD2H(2);
	{
		/* getpid()/gettid() read the TEB's ClientId, which on NT the
		 * kernel fills in; here the host's own ids stand in for it. */
		PTEB tb = (PTEB)shim_teb;
		tb->ClientId.UniqueProcess = (HANDLE)(long)syscall(SYS_getpid);
		tb->ClientId.UniqueThread = (HANDLE)(long)syscall(SYS_getpid);
	}
	__argc = 1;
	__argv = shim_argv;
	__progname = shim_argv[0];
	__progname_full = shim_argv[0];

	/* setenv()/putenv() realloc environ and free() its old entries, so
	 * every entry has to be on the heap the same way crt1.c's
	 * build_environ() leaves it -- entries out of the real envp cannot be
	 * used as-is, they have to be copied in.  A process this file itself
	 * execve'd (RtlCreateUserProcess below) marked its envp so that case
	 * is told apart from the test harness's own, arbitrary environment:
	 * without that, every native test would see the harness's real
	 * environment instead of the empty one "layout" above promises. */
	{
		int n = 0, i, j = 0, xchild = 0;
		if (envp) for (n = 0; envp[n]; n++)
			if (!strcmp(envp[n], XCHILD_MARK)) xchild = 1;
		if (xchild) {
			environ = __interceptor_malloc(sizeof(char *) * (size_t)n);
			for (i = 0; i < n; i++) {
				if (!strcmp(envp[i], XCHILD_MARK)) continue;
				if (!strncmp(envp[i], XSTATUS_FD_PREFIX, sizeof(XSTATUS_FD_PREFIX) - 1)) continue;
				if (!strncmp(envp[i], XHOST_PREFIX, sizeof(XHOST_PREFIX) - 1)) continue;
				if (!strncmp(envp[i], XVFS_PREFIX, sizeof(XVFS_PREFIX) - 1)) continue;
				environ[j] = __interceptor_malloc(strlen(envp[i]) + 1);
				if (environ[j]) strcpy(environ[j], envp[i]);
				j++;
			}
			environ[j] = 0;
		} else {
			environ = __interceptor_malloc(sizeof(char *));
			environ[0] = 0;
		}
	}
	vfs_snapshot_init(envp);
	__fd_init();
	/* crt1.c calls __fenv_init() here too, and natively there is no
	 * crt1.  Without this the startup floating-point environment is
	 * never captured, FE_DFL_ENV falls back to its first-use capture,
	 * and "the environment installed at program startup" silently
	 * becomes "the environment when someone first asked" -- which is the
	 * behaviour src/math/fenv.c explicitly rejects.  Anything that links
	 * ntlibc's objects without its crt1 has to make this call. */
	__fenv_init();
	xstatus_init(envp);

	/* One more thing a real execve() gives a child and a constructor
	 * cannot fabricate: a file at its own on-disk path.  Without this, a
	 * test that opens argv[0] -- test/exec.c's failed-exec/cloexec check,
	 * which does this in the *original* process, not even a spawned one
	 * -- finds nothing, because the volume above starts with only C:\work
	 * and C:\tmp.  This is the one deliberate exception to "nothing else
	 * in this file touches the host file system" (see the file-system
	 * comment below): it only ever reads, never shadows a path a test
	 * itself populates, and only for this one path. */
	{
		const char *source = 0;
		int i;
		if (envp) for (i = 0; envp[i]; i++)
			if (!strncmp(envp[i], XHOST_PREFIX, sizeof(XHOST_PREFIX) - 1))
				source = envp[i] + sizeof(XHOST_PREFIX) - 1;
		if (!source && argc > 0 && argv && argv[0] && argv[0][0] == '/') source = argv[0];
		if (source) {
			host_self = __interceptor_malloc(strlen(source) + 1);
			if (host_self) strcpy(host_self, source);
		}
		if (argc > 0 && argv && host_self) materialize_argv0(argv[0], host_self);
	}

	/* NTLIBC_FUZZ_MIRROR: make one host directory visible in the volume,
	 * so libFuzzer can find a corpus directory and write back to it.  See
	 * the block comment above mirror_init().  Reads envp rather than
	 * environ, which the loop above has just emptied. */
	mirror_init(envp);

	/* A native *test* binary never calls ntlibc's exit(): glibc's
	 * start-up calls main(), main() returns, and glibc's exit() ends the
	 * process, so __stdio_exit() does not run and anything left in a FILE
	 * buffer is lost.  A libFuzzer harness is the other case -- measured
	 * under gdb, libFuzzer's FuzzerDriver ends a timed run with exit(0),
	 * which binds to ntlibc's definition in this executable, so there
	 * __funcs_on_exit() and __stdio_exit() do run -- but only on the
	 * orderly path; a crash, an abort or a sanitizer report skips them,
	 * and those are exactly the runs whose output matters.  libFuzzer's
	 * own diagnostics go through this stdout/stderr (its fprintf/stderr
	 * references bind to ntlibc's, likewise the definitions here), so
	 * they would vanish.  Unbuffered costs nothing and loses nothing. */
	setvbuf(stdout, 0, _IONBF, 0);
	setvbuf(stderr, 0, _IONBF, 0);
}

/* -------------------------------------------------------------- file I/O
 *
 * A simulated file system, entirely in memory.
 *
 * The native build has no ntdll, so every file call ntlibc makes has to
 * land somewhere.  It lands here rather than on the host: the fuzzers
 * drive these entry points millions of times, and a harness that creates,
 * truncates and deletes real files that many times would be slow, would
 * leave debris, and -- since test/unistd exercises unlink, rename, chmod
 * and ftruncate -- would let a library bug damage something real.  In
 * memory it is hermetic, deterministic (which is what a fuzzing corpus
 * needs), and ASan sees the simulated file system's own allocations too,
 * so a mistake in ntlibc's handle or buffer use still gets caught.
 *
 * What is modelled, and how faithfully:
 *
 *   nodes      A node is a file or a directory: contents (a growable byte
 *              buffer), the four NT timestamps, FileAttributes, a link
 *              count and an index number.  That is what src/stat/stat.c
 *              and src/internal/fd.c actually read back; nothing more is
 *              invented.  Directory entries are separate from nodes, so
 *              hard links (FileLinkInformation) work and NumberOfLinks
 *              means something.
 *   handles    A HANDLE is an index into a table of file objects, plus
 *              one, so 0 stays "no handle" -- the scheme the shim already
 *              used for stdin/stdout/stderr, whose pseudo-handles 1, 2
 *              and 3 are simply the first three entries of that table and
 *              are marked as devices rather than files.  A file object
 *              holds the node, the byte offset, the granted access and
 *              the create options; NtDuplicateObject makes a second
 *              handle onto the *same* file object, so a dup'd descriptor
 *              shares the position, as it does on NT and on POSIX.
 *   paths      ntlibc hands in NT paths (\??\C:\dir\file), which is where
 *              the translation belongs: RtlDosPathNameToNtPathName_U_-
 *              WithStatus below does the DOS -> NT half (current
 *              directory, drive letters, . and .. collapsing, reserved
 *              device names) exactly as ntdll does, and the object
 *              manager half -- walking \??\C:\... down the node tree --
 *              happens in one place, resolve().  So the path handling in
 *              src/internal/path.c is really exercised, not bypassed.
 *   case       Names compare case-insensitively (ASCII only; NT uses a
 *              full Unicode upcase table, this does not), because that is
 *              what NT does and what OBJ_CASE_INSENSITIVE asks for.  A
 *              case-sensitive simulation would let tests pass here that
 *              would fail on the target.
 *   order      Directory enumeration is: ".", "..", then the entries in
 *              the order they were created.  NTFS returns them in B-tree
 *              (roughly case-insensitive alphabetical) order and FAT in
 *              creation order; NT guarantees neither, so creation order
 *              is a legal answer and a stable one.
 *   layout     The initial tree is C:\ with C:\work (the starting current
 *              directory) and C:\tmp.  The environment starts empty, so
 *              tmpfile()/tmpnam()/mkstemp() fall back to the current
 *              directory (see tmpdir() in src/stdio/misc.c); C:\tmp is
 *              there for a test that sets $TMPDIR itself.
 *
 * Where NT and POSIX differ, NT wins here.  In particular a positioned
 * read or write (an explicit ByteOffset) *does* move the file object's
 * position, because these handles are synchronous and that is what NT
 * does -- src/internal/fdpos.c exists to compensate for it, and would go
 * untested against a POSIX-like stub.
 *
 * Not simulated, and refused rather than faked: share-mode conflicts
 * (every open is as if FILE_SHARE_VALID_FLAGS), security descriptors,
 * reparse points and symlinks, alternate streams, extended attributes
 * other than the single $LXMOD word used by stat/chmod, short (8.3)
 * names, and volumes other than C:.
 */

#define VFS_HANDLES 1024

struct vnode;

struct vent {
	struct vent *next;
	struct vnode *node;
	WCHAR *name;
	size_t namelen;
};

struct vnode {
	int isdir;
	int nlink;                   /* directory entries naming this node */
	int refs;                    /* open file objects */
	int delete_pending;
	unsigned long long id;       /* the "index number" (st_ino) */
	ULONG attrs;
	ULONG lxmod;
	int have_lxmod;
	LARGE_INTEGER ctime, atime, mtime, chtime;
	unsigned char *data;         /* file contents */
	long long size;
	size_t cap;
	struct vent *entries;        /* directory contents, creation order */
	struct vnode *parent;        /* directories only */
	WCHAR *name;                 /* directories only: name in the parent */
	size_t namelen;
};

enum { OF_FREE = 0, OF_STD, OF_NULLDEV, OF_VFS, OF_PIPE, OF_PROC };

/* An anonymous pipe, which src/unistd/pipe.c makes the way kernel32's
 * CreatePipe does: NtCreateNamedPipeFile for the read end and NtOpenFile
 * of the same named-pipe device name for the write end.  A byte queue
 * with an end count reproduces what the tests observe; there is no
 * blocking (see NtReadFile). */
/* Matches the quotas src/unistd/pipe.c asks NtCreateNamedPipeFile for,
 * and the host pipe(2) capacity Linux gives by default. */
#define PIPE_QUOTA 65536

#ifndef FILE_PIPE_CLIENT_END
#define FILE_PIPE_CLIENT_END 0
#define FILE_PIPE_SERVER_END 1
#endif

struct vpipe {
	struct vpipe *next;
	WCHAR *name;
	size_t namelen;
	/* Backed by a real host pipe(2), not a heap buffer: RtlCloneUserProcess
	 * below makes a real fork(2) child, and a heap-backed queue would be
	 * ordinary process memory -- private to whichever side's copy wrote to
	 * it, invisible to the other, exactly the "handle value that means
	 * nothing in the child" problem fork.c's own header describes for a
	 * table entry that wasn't marked inheritable.  A real pipe does not
	 * have that problem: the host kernel duplicates the fd table across a
	 * real fork(2) (or a real fork+execve -- RtlCreateUserProcess above),
	 * both ends keep pointing at the same kernel pipe object, and a write
	 * from one process is readable from the other, same as two ends of an
	 * anonymous pipe are meant to behave.
	 *
	 * Deliberately *blocking*, unlike the old heap-backed version: NT's
	 * NtReadFile on a synchronous pipe handle genuinely blocks until data
	 * arrives, and now that a real, possibly cross-process writer can
	 * exist, letting the host kernel do that blocking is both the more
	 * faithful simulation and the only way it can work at all -- an
	 * immediate STATUS_PIPE_EMPTY can't wait for a sibling process the
	 * way this stub cannot loop on the caller's behalf without one.  Every
	 * test that reads from a pipe already writes to it first from the
	 * same process (so the read never actually waits) or arranges for
	 * every writer to be closed before reading (so it gets EOF, not a
	 * hang) -- see NtReadFile. */
	int rfd, wfd;
	int readers, writers;
};

struct ofile {
	int kind;
	int refs;                    /* handles onto this file object */
	int fd;                      /* OF_STD: the host descriptor */
	struct vnode *node;          /* OF_VFS */
	struct vpipe *pipe;          /* OF_PIPE */
	int writer;                  /* OF_PIPE: this handle is the write end */
	struct vnode *dir;           /* the directory it was opened through */
	WCHAR *name;                 /* the name it was opened by */
	size_t namelen;
	long long pos;
	ACCESS_MASK access;
	ULONG options;
	int delete_on_close;
	unsigned long scan;          /* NtQueryDirectoryFile cursor */
	int pid;                     /* OF_PROC */
	int exited;
	int exitcode;
	int snapshot_fd;             /* OF_PROC: child's shared VFS snapshot */
};

static struct ofile *vhandles[VFS_HANDLES];
static struct vpipe *vpipes;
static struct ofile stdfiles[3];
static struct vnode *vroot;
static struct vnode *vcwd;
static unsigned long long next_id = 100;

#define VOLUME_SERIAL 0x4e544653u

/* Geometry of the simulated volume, used by NtQueryVolumeInformationFile's
 * size classes below.  512-byte sectors, eight to an allocation unit, is
 * the 4096-byte NTFS default cluster; the counts make a 64 GiB volume that
 * is a little under half full, with a slightly smaller caller-available
 * figure standing in for a quota. */
#define VOLUME_BYTES_PER_SECTOR  512u
#define VOLUME_SECTORS_PER_UNIT  8u
#define VOLUME_TOTAL_UNITS       16777216LL   /* 64 GiB in 4 KiB clusters */
#define VOLUME_FREE_UNITS        9000000LL
#define VOLUME_AVAIL_UNITS       8000000LL

/* ---- small helpers ---- */

static const WCHAR w_dot[1] = { '.' };
static const WCHAR w_dotdot[2] = { '.', '.' };
static const WCHAR w_nul[3] = { 'N', 'U', 'L' };
static const WCHAR w_con[3] = { 'C', 'O', 'N' };
static const WCHAR w_empty[1] = { 0 };
/* the NT name of the named-pipe device */
static const WCHAR w_pipedev[18] = { '\\','D','e','v','i','c','e','\\',
	'N','a','m','e','d','P','i','p','e','\\' };
static const WCHAR w_ntpfx[6] = { '\\', '?', '?', '\\', 'C', ':' };

static void *vmalloc(size_t n) { return __interceptor_malloc(n ? n : 1); }
static void vfree(void *p) { __interceptor_free(p); }

static LARGE_INTEGER now_nt(void)
{
	LARGE_INTEGER t;
	NtQuerySystemTime(&t);
	return t;
}

static WCHAR upcase(WCHAR c)
{
	/* ASCII only: NT folds with a Unicode upcase table, this does not.
	 * Every name the tests use is ASCII. */
	return (c >= 'a' && c <= 'z') ? (WCHAR)(c - 32) : c;
}

static int wieq(const WCHAR *a, size_t an, const WCHAR *b, size_t bn)
{
	size_t i;
	if (an != bn) return 0;
	for (i = 0; i < an; i++) if (upcase(a[i]) != upcase(b[i])) return 0;
	return 1;
}

static WCHAR *wdup(const WCHAR *s, size_t n)
{
	WCHAR *p = vmalloc((n + 1) * sizeof(WCHAR));
	if (!p) return 0;
	if (n) memcpy(p, s, n * sizeof(WCHAR));
	p[n] = 0;
	return p;
}

static size_t wlen(const WCHAR *s)
{
	size_t n = 0;
	while (s[n]) n++;
	return n;
}

/* A NAME?PATTERN match with NT's two wildcards.  ntlibc always passes a
 * null FileName to NtQueryDirectoryFile, so this only exists so that a
 * caller that does pass one is not silently given the whole directory. */
static int wmatch(const WCHAR *pat, size_t pn, const WCHAR *name, size_t nn)
{
	size_t p = 0, n = 0, star = (size_t)-1, mark = 0;
	while (n < nn) {
		if (p < pn && (pat[p] == '?' || upcase(pat[p]) == upcase(name[n]))) { p++; n++; }
		else if (p < pn && pat[p] == '*') { star = p++; mark = n; }
		else if (star != (size_t)-1) { p = star + 1; n = ++mark; }
		else return 0;
	}
	while (p < pn && pat[p] == '*') p++;
	return p == pn;
}

/* ---- the node tree ---- */

static struct vnode *node_new(int isdir)
{
	struct vnode *v = vmalloc(sizeof *v);
	if (!v) return 0;
	memset(v, 0, sizeof *v);
	v->isdir = isdir;
	v->id = next_id++;
	v->attrs = isdir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
	v->ctime = v->atime = v->mtime = v->chtime = now_nt();
	return v;
}

static void node_release(struct vnode *v)
{
	/* A node stays alive while a name or a handle refers to it, which is
	 * what makes tmpfile()'s "unlink it and keep writing" work. */
	if (!v || v->nlink > 0 || v->refs > 0) return;
	vfree(v->data);
	vfree(v->name);
	vfree(v);
}

static struct vent *dir_find(struct vnode *dir, const WCHAR *name, size_t n)
{
	struct vent *e;
	for (e = dir->entries; e; e = e->next)
		if (wieq(e->name, e->namelen, name, n)) return e;
	return 0;
}

static struct vent *dir_add(struct vnode *dir, const WCHAR *name, size_t n, struct vnode *node)
{
	struct vent *e, **tail;
	e = vmalloc(sizeof *e);
	if (!e) return 0;
	e->next = 0;
	e->node = node;
	e->name = wdup(name, n);
	e->namelen = n;
	if (!e->name) { vfree(e); return 0; }
	for (tail = &dir->entries; *tail; tail = &(*tail)->next) ;
	*tail = e;
	node->nlink++;
	if (node->isdir) {
		node->parent = dir;
		vfree(node->name);
		node->name = wdup(name, n);
		node->namelen = n;
	}
	dir->mtime = dir->chtime = now_nt();
	return e;
}

static void dir_remove(struct vnode *dir, struct vent *victim)
{
	struct vent **p;
	for (p = &dir->entries; *p; p = &(*p)->next) {
		if (*p == victim) {
			struct vnode *n = victim->node;
			*p = victim->next;
			vfree(victim->name);
			vfree(victim);
			n->nlink--;
			dir->mtime = dir->chtime = now_nt();
			node_release(n);
			return;
		}
	}
}

/* A native process launch has to cross a real execve(), which would
 * otherwise replace this in-memory volume with a fresh empty one.  Carry a
 * compact tree snapshot in an inherited memfd so the child sees the same
 * files and current directory that a real NT child sees on the shared
 * filesystem.  Handles and pipes have their own inheritance mechanisms;
 * this format is deliberately only the linked directory tree. */
#define VFS_SNAPSHOT_MAGIC 0x4e545646u /* "NTVF" */
#define VFS_SNAPSHOT_MAX_FILE (256u * 1024u * 1024u)

struct vfs_snapshot_rec {
	ULONG type;                    /* 0=end directory, 1=directory, 2=file */
	ULONG namelen;                 /* WCHARs */
	unsigned long long size;       /* file bytes */
	ULONG attrs;
	ULONG lxmod;
	ULONG flags;                   /* bit 0: this node is vcwd; bit 1: lxmod */
};

static int host_write_all(int fd, const void *buf, size_t len)
{
	const unsigned char *p = buf;
	while (len) {
		long n = syscall(SYS_write, fd, p, len);
		if (n <= 0) return -1;
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int host_read_all(int fd, void *buf, size_t len)
{
	unsigned char *p = buf;
	while (len) {
		long n = syscall(SYS_read, fd, p, len);
		if (n <= 0) return -1;
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int vfs_snapshot_write_dir(int fd, struct vnode *dir)
{
	struct vent *e;
	for (e = dir->entries; e; e = e->next) {
		struct vnode *v = e->node;
		struct vfs_snapshot_rec r;
		memset(&r, 0, sizeof r);
		r.type = v->isdir ? 1 : 2;
		r.namelen = (ULONG)e->namelen;
		r.size = v->isdir ? 0 : (unsigned long long)v->size;
		r.attrs = v->attrs;
		r.lxmod = v->lxmod;
		r.flags = (v == vcwd ? 1u : 0u) | (v->have_lxmod ? 2u : 0u);
		if (host_write_all(fd, &r, sizeof r) < 0 ||
		    host_write_all(fd, e->name, e->namelen * sizeof(WCHAR)) < 0)
			return -1;
		if (v->isdir) {
			if (vfs_snapshot_write_dir(fd, v) < 0) return -1;
		} else if (v->size && host_write_all(fd, v->data, (size_t)v->size) < 0) {
			return -1;
		}
	}
	{
		struct vfs_snapshot_rec end;
		memset(&end, 0, sizeof end);
		return host_write_all(fd, &end, sizeof end);
	}
}

static int vfs_snapshot_write(int fd)
{
	ULONG magic = VFS_SNAPSHOT_MAGIC;
	if (syscall(SYS_ftruncate, fd, 0) < 0 ||
	    syscall(SYS_lseek, fd, 0, 0 /*SEEK_SET*/) < 0) return -1;
	if (host_write_all(fd, &magic, sizeof magic) < 0 ||
	    vfs_snapshot_write_dir(fd, vroot) < 0) return -1;
	return 0;
}

static int vfs_snapshot_export(void)
{
	int fd = (int)syscall(SYS_memfd_create, "ntlibc-vfs", 0);
	if (fd < 0) return -1;
	if (vfs_snapshot_write(fd) < 0) { syscall(SYS_close, fd); return -1; }
	return fd;
}

static int vfs_snapshot_read_dir(int fd, struct vnode *dir)
{
	for (;;) {
		struct vfs_snapshot_rec r;
		struct vent *e;
		struct vnode *v;
		WCHAR name[256];
		if (host_read_all(fd, &r, sizeof r) < 0) return -1;
		if (!r.type) return 0;
		if ((r.type != 1 && r.type != 2) || !r.namelen || r.namelen > 255 ||
		    r.size > VFS_SNAPSHOT_MAX_FILE) return -1;
		if (host_read_all(fd, name, r.namelen * sizeof(WCHAR)) < 0) return -1;
		e = dir_find(dir, name, r.namelen);
		if (e) {
			v = e->node;
			if (v->isdir != (r.type == 1)) return -1;
		} else {
			v = node_new(r.type == 1);
			if (!v || !dir_add(dir, name, r.namelen, v)) return -1;
		}
		v->attrs = r.attrs;
		v->lxmod = r.lxmod;
		v->have_lxmod = (r.flags & 2) != 0;
		if (r.flags & 1) vcwd = v;
		if (v->isdir) {
			if (vfs_snapshot_read_dir(fd, v) < 0) return -1;
		} else {
			vfree(v->data);
			v->data = r.size ? vmalloc((size_t)r.size) : 0;
			if (r.size && !v->data) return -1;
			v->size = (long long)r.size;
			v->cap = (size_t)r.size;
			if (r.size && host_read_all(fd, v->data, (size_t)r.size) < 0) return -1;
		}
	}
}

static void vfs_snapshot_init(char **envp)
{
	int fd = -1, child = 0;
	size_t i;
	ULONG magic = 0;
	if (envp) for (i = 0; envp[i]; i++)
		if (!strcmp(envp[i], XCHILD_MARK)) { child = 1; break; }
	if (!child) return;
	if (envp) for (i = 0; envp[i]; i++) {
		if (!strncmp(envp[i], XVFS_PREFIX, sizeof(XVFS_PREFIX) - 1)) {
			const char *p = envp[i] + sizeof(XVFS_PREFIX) - 1;
			fd = 0;
			while (*p >= '0' && *p <= '9') fd = fd * 10 + *p++ - '0';
			if (*p) fd = -1;
			break;
		}
	}
	if (fd < 0) return;
	syscall(SYS_lseek, fd, 0, 0 /*SEEK_SET*/);
	if (host_read_all(fd, &magic, sizeof magic) == 0 && magic == VFS_SNAPSHOT_MAGIC)
		(void)vfs_snapshot_read_dir(fd, vroot);
	vfs_snapshot_fd = fd;
}

static void vfs_snapshot_sync(void)
{
	if (vfs_snapshot_fd >= 0) (void)vfs_snapshot_write(vfs_snapshot_fd);
}

__attribute__((destructor)) static void vfs_snapshot_fini(void)
{
	vfs_snapshot_sync();
}

#define SYS_openat 257
#define SYS_pipe2  293

/* Put a copy of a real host file into the volume, at the same place
 * src/internal/path.c's dos_from_posix() would put it: an absolute path
 * with no drive letter is rooted at the current drive, so "/a/b/c" and
 * "\??\C:\a\b\c" name the same node.  Called from __ntshim_init() with
 * argv[0], the only host path this file has any business mirroring (see
 * the call site).  Read-only and additive -- an existing entry at the
 * target name, file or directory, is left alone rather than replaced, so
 * this can never clobber something a test created first. */
static void materialize_argv0(const char *name, const char *host)
{
	struct vnode *dir;
	const char *p;
	unsigned char *data = 0;
	size_t cap = 0, len = 0;
	long fd;

	if (!name || !*name || !host || host[0] != '/') return;
	dir = name[0] == '/' ? vroot : vcwd;
	p = name + (name[0] == '/');
	while (*p) {
		const char *start = p;
		WCHAR wname[512];
		size_t clen, wn, i;
		int last;

		while (*p && *p != '/') p++;
		clen = (size_t)(p - start);
		if (*p) p++;
		if (!clen || (clen == 1 && start[0] == '.')) continue;
		last = (*p == 0);
		wn = clen < 512 ? clen : 511;
		for (i = 0; i < wn; i++) wname[i] = (WCHAR)(unsigned char)start[i];

		if (!last) {
			struct vent *e = dir_find(dir, wname, wn);
			if (e && e->node->isdir) { dir = e->node; continue; }
			if (e) return;                 /* a file sits where a dir should */
			{
				struct vnode *nd = node_new(1);
				if (!nd || !dir_add(dir, wname, wn, nd)) return;
				dir = nd;
			}
			continue;
		}

		if (dir_find(dir, wname, wn)) return;   /* already there: leave it */

		fd = syscall(SYS_openat, -100 /*AT_FDCWD*/, host, 0 /*O_RDONLY*/, 0);
		if (fd < 0) return;
		for (;;) {
			unsigned char buf[65536];
			long n = syscall(SYS_read, fd, buf, sizeof buf);
			if (n <= 0) break;
			if (len + (size_t)n > cap) {
				size_t want = (len + (size_t)n) * 2 + 4096;
				unsigned char *nd = __interceptor_realloc(data, want);
				if (!nd) break;
				data = nd; cap = want;
			}
			memcpy(data + len, buf, (size_t)n);
			len += (size_t)n;
		}
		syscall(SYS_close, fd);
		{
			struct vnode *nf = node_new(0);
			if (!nf || !dir_add(dir, wname, wn, nf)) { vfree(data); return; }
			nf->data = data;
			nf->size = (long long)len;
			nf->cap = cap;
			nf->lxmod = 0755;
			nf->have_lxmod = 1;
		}
		return;
	}
}

/* ---- the corpus mirror ------------------------------------------------
 *
 * Why this exists.  libFuzzer's corpus is a *directory*: it stats it,
 * lists it, reads every file in it at start-up, and writes each newly
 * interesting input back into it as a file named after the input's SHA1.
 * It does all of that through the C library it is linked against, which
 * in this executable is ntlibc -- so its stat/opendir/open land in the
 * volume above, which starts out holding only C:\work and C:\tmp.  The
 * corpus directory is simply not in it, and libFuzzer refuses to start:
 *
 *     ERROR: The required directory "/x/corpus" does not exist
 *
 * That is the whole of the "no corpus survives a run" problem, and it is
 * worth being precise about it because the comment this replaces was
 * not: NtCreateFile is *not* unimplemented here and does not answer
 * STATUS_NOT_IMPLEMENTED.  It is fully implemented, against the in-memory
 * volume; stat() of the corpus directory fails with a plain ENOENT
 * because nothing ever put that directory in the volume.  The gap is a
 * missing directory, not a missing syscall.
 *
 * NTLIBC_FUZZ_MIRROR=<host directory> closes it.  At start-up the named
 * host directory tree is copied into the volume at the same path --
 * src/internal/path.c's dos_from_posix() maps "/a/b" onto "\??\C:\a\b",
 * so a host absolute path names the same node here with no translation
 * -- and a file inside that subtree is written back to the host when the
 * last handle that was opened for writing is closed.
 *
 * Write-through on close rather than a flush at exit, deliberately: the
 * artefact that matters most is the one libFuzzer writes on a crash, and
 * it writes that and then calls _Exit(), which runs no atexit handler.
 * A close hook catches the crash artefact and the corpus unit alike.
 *
 * Deletions are mirrored too, and they have to be.  libFuzzer's
 * -reduce_inputs (on by default) unlinks a corpus file the moment a
 * smaller input reaches the same coverage, so its live corpus stays
 * minimal -- 1326 files on disk against 374 it was actually using, after
 * two 15s runs of fuzz_strtod.  Without mirrored deletes the host
 * directory keeps every input any run ever wrote and grows without bound,
 * which for a nightly cron is a cache that eventually costs more to load
 * than the fuzzing is worth.  With them, the host directory *is*
 * libFuzzer's minimized corpus.
 *
 * libFuzzer's own answer to this, `-merge=1`, does not work here and was
 * measured not to: merge re-execs the harness once per input
 * (MERGE-OUTER: attempt N) so that a crashing input cannot lose the whole
 * merge, and those child processes go through ntstubs' RtlCreateUserProcess
 * rather than a real fork of a real binary.  A 1326-file merge produced
 * "0 new files with 0 new features added".
 *
 * The unlink is fenced hard: only a path that resolves inside the
 * mirrored subtree, only a regular file, never a directory.  A directory
 * this file created on the host is left there.
 *
 * Unset, none of this runs and the volume behaves exactly as before, so
 * `make asan` and every native test see no change at all.
 */

#define SYS_mkdirat    258
#define SYS_unlinkat   263
#define SYS_getdents64 217

#define MIRROR_MAX_FILE   (16u * 1024u * 1024u)
#define MIRROR_MAX_DEPTH  16
#define MIRROR_PATH_MAX   3072

static struct vnode *mirror_root;          /* the mirrored dir in the volume */
static char mirror_host[MIRROR_PATH_MAX];  /* its host path, no trailing / */
static size_t mirror_hostlen;

/* getdents64's record, spelled out because this file has no host headers. */
struct mdirent64 {
	unsigned long long d_ino;
	long long d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[1];
};
#define MDT_DIR 4
#define MDT_REG 8

/* Slurp a host file into a fresh vnode.  Returns 0 on any failure, and
 * on failure has created nothing: a half-read corpus entry is worse than
 * a missing one, since libFuzzer would keep it and treat it as input. */
static struct vnode *mirror_slurp(const char *host)
{
	unsigned char *data = 0;
	size_t cap = 0, len = 0;
	struct vnode *nf;
	long fd = syscall(SYS_openat, -100 /*AT_FDCWD*/, host, 0 /*O_RDONLY*/, 0);

	if (fd < 0) return 0;
	for (;;) {
		unsigned char buf[65536];
		long n = syscall(SYS_read, fd, buf, sizeof buf);
		if (n < 0) { syscall(SYS_close, fd); vfree(data); return 0; }
		if (n == 0) break;
		if (len + (size_t)n > MIRROR_MAX_FILE) { syscall(SYS_close, fd); vfree(data); return 0; }
		if (len + (size_t)n > cap) {
			size_t want = (len + (size_t)n) * 2 + 4096;
			unsigned char *nd = __interceptor_realloc(data, want);
			if (!nd) { syscall(SYS_close, fd); vfree(data); return 0; }
			data = nd; cap = want;
		}
		memcpy(data + len, buf, (size_t)n);
		len += (size_t)n;
	}
	syscall(SYS_close, fd);
	nf = node_new(0);
	if (!nf) { vfree(data); return 0; }
	nf->data = data;
	nf->size = (long long)len;
	nf->cap = cap;
	return nf;
}

/* Copy one host directory's contents into `dir`.  Additive: a name the
 * volume already carries is left alone, so this can never clobber
 * something a harness created first.  Symlinks and every other file type
 * are skipped -- getdents64's d_type says DT_LNK for them and nothing
 * here follows one, so a symlinked corpus entry pointing at /etc/shadow
 * is ignored rather than read. */
static void mirror_import(struct vnode *dir, char *host, size_t hostlen, int depth)
{
	char buf[32768];
	long fd;

	if (depth > MIRROR_MAX_DEPTH) return;
	fd = syscall(SYS_openat, -100, host, 0x10000 /*O_RDONLY|O_DIRECTORY*/, 0);
	if (fd < 0) return;
	for (;;) {
		long n = syscall(SYS_getdents64, fd, buf, sizeof buf);
		long off;
		if (n <= 0) break;
		for (off = 0; off < n; ) {
			struct mdirent64 *de = (struct mdirent64 *)(buf + off);
			const char *nm = de->d_name;
			size_t nl = strlen(nm), i;
			WCHAR wname[256];
			off += de->d_reclen;
			if (!nl || nl > 255) continue;
			if (nm[0] == '.' && (nl == 1 || (nl == 2 && nm[1] == '.'))) continue;
			if (de->d_type != MDT_DIR && de->d_type != MDT_REG) continue;
			if (hostlen + 1 + nl >= MIRROR_PATH_MAX) continue;
			for (i = 0; i < nl; i++) {
				if ((unsigned char)nm[i] > 127) break;
				wname[i] = (WCHAR)(unsigned char)nm[i];
			}
			if (i != nl) continue;             /* non-ASCII: not ours to fold */
			host[hostlen] = '/';
			memcpy(host + hostlen + 1, nm, nl + 1);
			if (de->d_type == MDT_DIR) {
				struct vent *e = dir_find(dir, wname, nl);
				struct vnode *nd;
				if (e) { if (e->node->isdir) mirror_import(e->node, host, hostlen + 1 + nl, depth + 1); }
				else if ((nd = node_new(1)) != 0) {
					if (dir_add(dir, wname, nl, nd)) mirror_import(nd, host, hostlen + 1 + nl, depth + 1);
					else { nd->nlink = 0; node_release(nd); }
				}
			} else if (!dir_find(dir, wname, nl)) {
				struct vnode *nf = mirror_slurp(host);
				if (nf && !dir_add(dir, wname, nl, nf)) { nf->nlink = 0; node_release(nf); }
			}
			host[hostlen] = 0;
		}
	}
	syscall(SYS_close, fd);
}

/* Called from __ntshim_init() with the real envp: `environ` has been
 * replaced by an empty one by then (see the comment there), so getenv()
 * would find nothing. */
static void mirror_init(char **envp)
{
	static const char key[] = "NTLIBC_FUZZ_MIRROR=";
	const char *val = 0;
	char path[MIRROR_PATH_MAX];
	size_t len, i;
	struct vnode *dir;
	const char *p;

	if (envp) for (i = 0; envp[i]; i++)
		if (!strncmp(envp[i], key, sizeof key - 1)) val = envp[i] + sizeof key - 1;
	if (!val || val[0] != '/') return;         /* unset, or not absolute: off */
	len = strlen(val);
	while (len > 1 && val[len - 1] == '/') len--;
	if (len < 2 || len >= MIRROR_PATH_MAX) return;
	memcpy(path, val, len);
	path[len] = 0;

	/* Walk the volume to that path, creating the directories on the way:
	 * the mirror root has to exist here before libFuzzer stats it. */
	dir = vroot;
	p = path + 1;
	while (*p) {
		const char *start = p;
		WCHAR wname[256];
		size_t clen, j;
		struct vent *e;
		while (*p && *p != '/') p++;
		clen = (size_t)(p - start);
		if (*p) p++;
		if (!clen || clen > 255) { if (!clen) continue; return; }
		for (j = 0; j < clen; j++) {
			if ((unsigned char)start[j] > 127) return;
			wname[j] = (WCHAR)(unsigned char)start[j];
		}
		e = dir_find(dir, wname, clen);
		if (e) {
			if (!e->node->isdir) return;       /* a file sits where a dir must be */
			dir = e->node;
		} else {
			struct vnode *nd = node_new(1);
			if (!nd) return;
			if (!dir_add(dir, wname, clen, nd)) { nd->nlink = 0; node_release(nd); return; }
			dir = nd;
		}
	}
	memcpy(mirror_host, path, len + 1);
	mirror_hostlen = len;
	mirror_root = dir;
	mirror_import(dir, mirror_host, mirror_hostlen, 0);
	mirror_host[mirror_hostlen] = 0;           /* mirror_import borrows the buffer */
}

/* The host path of (dir, leaf) if it lies inside the mirror, else -1. */
static int mirror_relpath(struct vnode *dir, const WCHAR *leaf, size_t leaflen,
                          char *out, size_t outsz)
{
	struct vnode *chain[MIRROR_MAX_DEPTH];
	int depth = 0, i;
	size_t len;

	if (!mirror_root || !dir || !leaf || !leaflen) return -1;
	for (; dir && dir != mirror_root; dir = dir->parent) {
		if (depth == MIRROR_MAX_DEPTH || !dir->name) return -1;
		chain[depth++] = dir;
	}
	if (dir != mirror_root) return -1;
	len = mirror_hostlen;
	if (len >= outsz) return -1;
	memcpy(out, mirror_host, len);
	for (i = depth - 1; i >= 0; i--) {
		size_t j, n = chain[i]->namelen;
		if (len + 1 + n >= outsz) return -1;
		out[len++] = '/';
		for (j = 0; j < n; j++) {
			WCHAR c = chain[i]->name[j];
			if (c < 32 || c > 126 || c == '/') return -1;
			out[len++] = (char)c;
		}
	}
	if (len + 1 + leaflen >= outsz) return -1;
	out[len++] = '/';
	for (i = 0; (size_t)i < leaflen; i++) {
		WCHAR c = leaf[i];
		if (c < 32 || c > 126 || c == '/') return -1;
		out[len++] = (char)c;
	}
	out[len] = 0;
	return 0;
}

/* mkdir -p over the directory part, ignoring EEXIST (and everything else:
 * the open below is the real test of whether it worked). */
static void mirror_mkparents(char *path)
{
	size_t i;
	for (i = mirror_hostlen + 1; path[i]; i++) {
		if (path[i] != '/') continue;
		path[i] = 0;
		syscall(SYS_mkdirat, -100, path, 0755);
		path[i] = '/';
	}
}

/* Write a mirrored file back to the host.  Called from of_drop() when the
 * last handle onto it goes away, which is where libFuzzer's fclose of a
 * freshly written corpus unit -- or of a crash artefact -- ends up. */
static void mirror_flush(struct ofile *f)
{
	char path[MIRROR_PATH_MAX];
	long fd;
	long long off;
	struct vnode *n;

	if (!mirror_root || !f || f->kind != OF_VFS) return;
	n = f->node;
	if (!n || n->isdir || f->delete_on_close || n->delete_pending) return;
	if (!(f->access & (FILE_WRITE_DATA | FILE_APPEND_DATA))) return;
	/* The name may already be gone: unlink() with POSIX semantics removes
	 * it while the handle is still open (do_dispose), and re-creating the
	 * host file here would undo the unlink mirror_unlink just did. */
	if (!f->dir) return;
	{
		struct vent *e = dir_find(f->dir, f->name, f->namelen);
		if (!e || e->node != n) return;
	}
	if (mirror_relpath(f->dir, f->name, f->namelen, path, sizeof path) < 0) return;
	mirror_mkparents(path);
	fd = syscall(SYS_openat, -100, path, 0x241 /*O_WRONLY|O_CREAT|O_TRUNC*/, 0644);
	if (fd < 0) return;
	for (off = 0; off < n->size; ) {
		long w = syscall(SYS_write, fd, n->data + off, (size_t)(n->size - off));
		if (w <= 0) break;
		off += w;
	}
	syscall(SYS_close, fd);
}

/* Remove a mirrored file from the host, when the volume has just dropped
 * its last link to it.  Same fence as mirror_flush: nothing outside the
 * subtree named by NTLIBC_FUZZ_MIRROR, and never a directory. */
static void mirror_unlink(struct ofile *f)
{
	char path[MIRROR_PATH_MAX];

	if (!mirror_root || !f || f->kind != OF_VFS) return;
	if (!f->node || f->node->isdir) return;
	if (mirror_relpath(f->dir, f->name, f->namelen, path, sizeof path) < 0) return;
	syscall(SYS_unlinkat, -100 /*AT_FDCWD*/, path, 0);
}

/* The NT path of a node: "\??\C:" followed by each ancestor's name.
 * Directories carry their own name, so a renamed parent takes its whole
 * subtree with it, exactly as it does on NT. */
static WCHAR *node_path(struct vnode *dir, const WCHAR *leaf, size_t leaflen, size_t *outlen)
{
	struct vnode *chain[64];
	int depth = 0;
	size_t len = 6, i;
	WCHAR *p;

	for (; dir && dir->parent && depth < 64; dir = dir->parent) chain[depth++] = dir;
	for (i = 0; i < (size_t)depth; i++) len += 1 + chain[i]->namelen;
	if (leaf) len += 1 + leaflen;
	if (len == 6) len++;                       /* the root itself: "\??\C:\" */
	p = vmalloc((len + 1) * sizeof(WCHAR));
	if (!p) return 0;
	memcpy(p, w_ntpfx, sizeof w_ntpfx);
	len = 6;
	for (i = 0; i < (size_t)depth; i++) {
		struct vnode *d = chain[depth - 1 - i];
		p[len++] = '\\';
		memcpy(p + len, d->name, d->namelen * sizeof(WCHAR));
		len += d->namelen;
	}
	if (leaf) {
		p[len++] = '\\';
		memcpy(p + len, leaf, leaflen * sizeof(WCHAR));
		len += leaflen;
	}
	if (len == 6) p[len++] = '\\';
	p[len] = 0;
	if (outlen) *outlen = len;
	return p;
}

/* ---- handles ---- */

static struct ofile *of_get(HANDLE h)
{
	long i = (long)h - 1;
	if (i < 0 || i >= VFS_HANDLES) return 0;
	return vhandles[i];
}

static NTSTATUS of_install(struct ofile *f, PHANDLE out)
{
	long i;
	for (i = 0; i < VFS_HANDLES; i++) {
		if (!vhandles[i]) {
			vhandles[i] = f;
			f->refs++;
			*out = (HANDLE)(long)(i + 1);
			return STATUS_SUCCESS;
		}
	}
	return STATUS_TOO_MANY_OPENED_FILES;
}

static void of_drop(struct ofile *f)
{
	if (--f->refs > 0) return;
	if (f->kind == OF_STD) { f->refs = 1; return; }   /* static, never freed */
	if (f->kind == OF_PIPE) {
		struct vpipe *p = f->pipe, **pp;
		/* Close each real end exactly once, when its last handle drops
		 * (across every process sharing the vpipe, not just this one --
		 * see the fd table note above): that is what lets the other side
		 * see EOF/EPIPE the way a real pipe's peer would. */
		if (f->writer) { if (--p->writers == 0 && p->wfd >= 0) { syscall(SYS_close, p->wfd); p->wfd = -1; } }
		else            { if (--p->readers == 0 && p->rfd >= 0) { syscall(SYS_close, p->rfd); p->rfd = -1; } }
		if (!p->readers && !p->writers) {
			for (pp = &vpipes; *pp; pp = &(*pp)->next)
				if (*pp == p) { *pp = p->next; break; }
			vfree(p->name);
			vfree(p);
		}
	}
	if (f->kind == OF_VFS) {
		struct vnode *n = f->node;
		int unlinked = 0;
		/* Before the node is touched: the last handle onto a mirrored
		 * file that was opened for writing is what puts it on the host. */
		mirror_flush(f);
		if (f->delete_on_close && !n->delete_pending) n->delete_pending = 1;
		n->refs--;
		if (n->delete_pending && n->refs == 0 && f->dir) {
			struct vent *e = dir_find(f->dir, f->name, f->namelen);
			/* dir_remove drops the last link and releases the node with
			 * it, so releasing again here would be a use-after-free. */
			if (e && e->node == n) { mirror_unlink(f); dir_remove(f->dir, e); unlinked = 1; }
		}
		if (!unlinked) node_release(n);
		vfree(f->name);
	}
	if (f->kind == OF_PROC && f->snapshot_fd >= 0)
		syscall(SYS_close, f->snapshot_fd);
	vfree(f);
}

/* ---- resolving an NT path to a (directory, leaf) pair ---- */

struct vpath {
	struct vnode *dir;           /* the directory holding the leaf */
	const WCHAR *leaf;           /* 0 when the path names a directory itself */
	size_t leaflen;
	int nulldev;                 /* \??\NUL */
	int condev;                  /* \??\CON */
	const WCHAR *pipename;       /* the named-pipe device namespace */
	size_t pipelen;
};

static NTSTATUS resolve(POBJECT_ATTRIBUTES oa, struct vpath *out)
{
	const WCHAR *p;
	size_t n;
	struct vnode *dir;

	memset(out, 0, sizeof *out);
	if (!oa || !oa->ObjectName || (!oa->ObjectName->Buffer && oa->ObjectName->Length))
		return STATUS_OBJECT_NAME_INVALID;
	p = oa->ObjectName->Buffer;
	n = oa->ObjectName->Length / sizeof(WCHAR);

	/* The named-pipe device is its own object namespace, not part of a
	 * volume, so it is matched before the drive-letter syntax. */
	if (!oa->RootDirectory && n > 18 && wieq(p, 18, w_pipedev, 18)) {
		out->pipename = p + 18;
		out->pipelen = n - 18;
		return STATUS_SUCCESS;
	}
	if (oa->RootDirectory) {
		struct ofile *f = of_get(oa->RootDirectory);
		if (!f || f->kind != OF_VFS) return STATUS_INVALID_HANDLE;
		if (!f->node->isdir) return STATUS_NOT_A_DIRECTORY;
		dir = f->node;
		/* A name relative to a directory handle must not be rooted. */
		if (n && p[0] == '\\') return STATUS_OBJECT_PATH_SYNTAX_BAD;
	} else {
		if (n < 4 || p[0] != '\\' || p[1] != '?' || p[2] != '?' || p[3] != '\\')
			return STATUS_OBJECT_PATH_SYNTAX_BAD;
		p += 4; n -= 4;
		if (wieq(p, n, w_nul, 3)) { out->nulldev = 1; return STATUS_SUCCESS; }
		if (wieq(p, n, w_con, 3)) { out->condev = 1; return STATUS_SUCCESS; }
		if (n < 2 || upcase(p[0]) != 'C' || p[1] != ':') return STATUS_OBJECT_PATH_NOT_FOUND;
		p += 2; n -= 2;
		if (n && p[0] != '\\') return STATUS_OBJECT_PATH_SYNTAX_BAD;
		if (n) { p++; n--; }
		dir = vroot;
	}

	/* Walk the components.  The last one is the leaf and is not required
	 * to exist; every one before it must be an existing directory. */
	while (n) {
		size_t len;
		struct vent *e;
		for (len = 0; len < n && p[len] != '\\'; len++) ;
		if (!len) return STATUS_OBJECT_NAME_INVALID;   /* "a\\b" */
		if (len == n) {
			/* the leaf; "." and ".." still name a directory */
			if (len == 1 && p[0] == '.') { out->dir = dir; return STATUS_SUCCESS; }
			if (len == 2 && p[0] == '.' && p[1] == '.') {
				out->dir = dir->parent ? dir->parent : dir;
				return STATUS_SUCCESS;
			}
			out->dir = dir;
			out->leaf = p;
			out->leaflen = len;
			return STATUS_SUCCESS;
		}
		if (len == 1 && p[0] == '.') { p += 2; n -= 2; continue; }
		if (len == 2 && p[0] == '.' && p[1] == '.') {
			if (dir->parent) dir = dir->parent;
			p += 3; n -= 3;
			continue;
		}
		e = dir_find(dir, p, len);
		if (!e) return STATUS_OBJECT_PATH_NOT_FOUND;
		if (!e->node->isdir) return STATUS_OBJECT_PATH_NOT_FOUND;
		dir = e->node;
		p += len + 1; n -= len + 1;
		if (!n) { out->dir = dir; return STATUS_SUCCESS; }   /* trailing "\" */
	}
	out->dir = dir;   /* the path named a directory outright */
	return STATUS_SUCCESS;
}

/* The node a resolved path names, or 0 if it does not exist. */
static struct vnode *vpath_node(struct vpath *vp)
{
	struct vent *e;
	if (!vp->leaf) return vp->dir;
	e = dir_find(vp->dir, vp->leaf, vp->leaflen);
	return e ? e->node : 0;
}

/* ---- NtCreateFile and NtOpenFile ---- */

static NTSTATUS file_grow(struct vnode *v, long long need)
{
	unsigned char *p;
	size_t want;
	if (need <= (long long)v->cap) return STATUS_SUCCESS;
	want = (size_t)need + (size_t)need / 2 + 64;
	p = __interceptor_realloc(v->data, want);
	if (!p) return STATUS_NO_MEMORY;
	v->data = p;
	v->cap = want;
	return STATUS_SUCCESS;
}

static NTSTATUS file_setsize(struct vnode *v, long long size)
{
	NTSTATUS st;
	if (size < 0) return STATUS_INVALID_PARAMETER;
	if (size > v->size) {
		st = file_grow(v, size);
		if (!NT_SUCCESS(st)) return st;
		memset(v->data + v->size, 0, (size_t)(size - v->size));
	}
	v->size = size;
	v->mtime = v->chtime = now_nt();
	return STATUS_SUCCESS;
}

#define WRITE_ACCESS (FILE_WRITE_DATA | FILE_APPEND_DATA)

/* The object manager turns the generic rights into specific ones through
 * the type's generic mapping before the file system ever sees them, so
 * every access check below can be written in terms of the specific bits
 * (src/unistd/pipe.c, for one, asks for plain GENERIC_READ/WRITE). */
static ACCESS_MASK map_generic(ACCESS_MASK a)
{
	if (a & GENERIC_READ) a |= FILE_GENERIC_READ;
	if (a & GENERIC_WRITE) a |= FILE_GENERIC_WRITE;
	if (a & GENERIC_EXECUTE) a |= FILE_GENERIC_EXECUTE;
	if (a & GENERIC_ALL) a |= FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE;
	return a & ~(ACCESS_MASK)(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

static NTSTATUS do_create(PHANDLE out, ACCESS_MASK access, POBJECT_ATTRIBUTES oa,
                          PIO_STATUS_BLOCK io, ULONG attrs, ULONG disposition, ULONG options)
{
	struct vpath vp;
	struct vnode *node;
	struct ofile *f;
	NTSTATUS st;
	ULONG result;
	int created = 0;

	if (io) { io->Status = 0; io->Information = 0; }
	access = map_generic(access);
	st = resolve(oa, &vp);
	if (!NT_SUCCESS(st)) return st;

	if (vp.nulldev || vp.condev) {
		if (disposition == FILE_CREATE) return STATUS_OBJECT_NAME_COLLISION;
		if (vp.condev) {
			/* CON is the console: reads come from descriptor 0 and
			 * writes go to 1, which is as close as a native build gets. */
			st = of_install(&stdfiles[(access & WRITE_ACCESS) ? 1 : 0], out);
		} else {
			f = vmalloc(sizeof *f);
			if (!f) return STATUS_NO_MEMORY;
			memset(f, 0, sizeof *f);
			f->kind = OF_NULLDEV;
			st = of_install(f, out);
			if (!NT_SUCCESS(st)) vfree(f);
		}
		if (NT_SUCCESS(st) && io) io->Information = FILE_OPENED;
		return st;
	}

	if (vp.pipename) {
		/* Opening an existing pipe by name: the client (write) end. */
		struct vpipe *p;
		if (disposition == FILE_CREATE) return STATUS_OBJECT_NAME_COLLISION;
		for (p = vpipes; p; p = p->next)
			if (wieq(p->name, p->namelen, vp.pipename, vp.pipelen)) break;
		if (!p) return STATUS_OBJECT_NAME_NOT_FOUND;
		f = vmalloc(sizeof *f);
		if (!f) return STATUS_NO_MEMORY;
		memset(f, 0, sizeof *f);
		f->kind = OF_PIPE;
		f->pipe = p;
		f->access = access;
		f->writer = (access & WRITE_ACCESS) != 0;
		st = of_install(f, out);
		if (!NT_SUCCESS(st)) { vfree(f); return st; }
		if (f->writer) p->writers++; else p->readers++;
		if (io) { io->Status = STATUS_SUCCESS; io->Information = FILE_OPENED; }
		return STATUS_SUCCESS;
	}

	node = vpath_node(&vp);
	if (node && node->delete_pending) return STATUS_DELETE_PENDING;

	if (!node) {
		if (disposition == FILE_OPEN || disposition == FILE_OVERWRITE)
			return STATUS_OBJECT_NAME_NOT_FOUND;
		if (!vp.leaf) return STATUS_OBJECT_NAME_INVALID;
		node = node_new((options & FILE_DIRECTORY_FILE) != 0);
		if (!node) return STATUS_NO_MEMORY;
		node->attrs = node->isdir ? FILE_ATTRIBUTE_DIRECTORY
		                          : (attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
		                                      FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY))
		                            | FILE_ATTRIBUTE_ARCHIVE;
		if (!dir_add(vp.dir, vp.leaf, vp.leaflen, node)) {
			node->nlink = 0;
			node_release(node);
			return STATUS_NO_MEMORY;
		}
		created = 1;
		result = FILE_CREATED;
	} else {
		if (disposition == FILE_CREATE) return STATUS_OBJECT_NAME_COLLISION;
		if (node->isdir && (options & FILE_NON_DIRECTORY_FILE)) return STATUS_FILE_IS_A_DIRECTORY;
		if (!node->isdir && (options & FILE_DIRECTORY_FILE)) return STATUS_NOT_A_DIRECTORY;
		/* Data access to a directory without FILE_DIRECTORY_FILE is what
		 * NT refuses with STATUS_FILE_IS_A_DIRECTORY; src/fcntl/open.c
		 * retries as a directory when it sees that. */
		if (node->isdir && !(options & FILE_DIRECTORY_FILE) &&
		    (access & (FILE_READ_DATA | WRITE_ACCESS)))
			return STATUS_FILE_IS_A_DIRECTORY;
		if ((node->attrs & FILE_ATTRIBUTE_READONLY) &&
		    ((access & WRITE_ACCESS) || disposition == FILE_OVERWRITE ||
		     disposition == FILE_OVERWRITE_IF || disposition == FILE_SUPERSEDE))
			return STATUS_ACCESS_DENIED;
		if (node->isdir && (options & FILE_DELETE_ON_CLOSE) && node->entries)
			return STATUS_DIRECTORY_NOT_EMPTY;
		result = FILE_OPENED;
		if (disposition == FILE_OVERWRITE || disposition == FILE_OVERWRITE_IF ||
		    disposition == FILE_SUPERSEDE) {
			st = file_setsize(node, 0);
			if (!NT_SUCCESS(st)) return st;
			node->attrs = (attrs & ~FILE_ATTRIBUTE_NORMAL) | FILE_ATTRIBUTE_ARCHIVE;
			result = disposition == FILE_SUPERSEDE ? FILE_SUPERSEDED : FILE_OVERWRITTEN;
		}
	}

	f = vmalloc(sizeof *f);
	if (!f) { st = STATUS_NO_MEMORY; goto fail; }
	memset(f, 0, sizeof *f);
	f->kind = OF_VFS;
	f->node = node;
	/* A path that named a directory outright (or the volume root) has no
	 * leaf; such a handle is known by the directory's own name. */
	if (vp.leaf) {
		f->dir = vp.dir;
		f->name = wdup(vp.leaf, vp.leaflen);
		f->namelen = vp.leaflen;
	} else {
		f->dir = node->parent;
		f->name = wdup(node->name ? node->name : w_empty, node->name ? node->namelen : 0);
		f->namelen = node->name ? node->namelen : 0;
	}
	f->access = access;
	f->options = options;
	f->delete_on_close = (options & FILE_DELETE_ON_CLOSE) != 0;
	if (!f->name) { vfree(f); st = STATUS_NO_MEMORY; goto fail; }
	st = of_install(f, out);
	if (!NT_SUCCESS(st)) { vfree(f->name); vfree(f); goto fail; }
	node->refs++;
	if (io) { io->Status = STATUS_SUCCESS; io->Information = result; }
	return STATUS_SUCCESS;

fail:
	if (created) {
		struct vent *e = dir_find(vp.dir, vp.leaf, vp.leaflen);
		if (e) dir_remove(vp.dir, e);
	}
	return st;
}

NTSTATUS NTAPI NtCreateFile(PHANDLE out, ACCESS_MASK access, POBJECT_ATTRIBUTES oa,
                            PIO_STATUS_BLOCK io, LARGE_INTEGER *alloc, ULONG attrs,
                            ULONG share, ULONG disposition, ULONG options,
                            PVOID ea, ULONG ealen)
{
	NTSTATUS st;
	(void)alloc; (void)share;
	st = do_create(out, access, oa, io, attrs, disposition, options);
	if (NT_SUCCESS(st) && io && io->Information == FILE_CREATED && ea && ealen >= 19) {
		__NT_FILE_FULL_EA_INFORMATION *x = ea;
		if (x->EaNameLength == 6 && x->EaValueLength == 4 &&
		    !memcmp(x->EaName, "$LXMOD", 6)) {
			unsigned char *p = (unsigned char *)x->EaName + 7;
			struct ofile *f = of_get(*out);
			if (f && f->kind == OF_VFS) {
				f->node->lxmod = (ULONG)p[0] | (ULONG)p[1] << 8 |
				                 (ULONG)p[2] << 16 | (ULONG)p[3] << 24;
				f->node->have_lxmod = 1;
			}
		}
	}
	return st;
}

NTSTATUS NTAPI NtOpenFile(PHANDLE out, ACCESS_MASK access, POBJECT_ATTRIBUTES oa,
                          PIO_STATUS_BLOCK io, ULONG share, ULONG options)
{
	(void)share;
	return do_create(out, access, oa, io, 0, FILE_OPEN, options);
}

NTSTATUS NTAPI NtQueryEaFile(HANDLE h, PIO_STATUS_BLOCK io, PVOID buf,
			     ULONG len, BOOLEAN single, PVOID list, ULONG listlen,
			     PULONG index, BOOLEAN restart)
{
	struct ofile *f = of_get(h);
	__NT_FILE_GET_EA_INFORMATION *get = list;
	__NT_FILE_FULL_EA_INFORMATION *ea = buf;
	unsigned char *p;
	(void)single; (void)index; (void)restart;
	if (!f || f->kind != OF_VFS) return STATUS_INVALID_HANDLE;
	if (!(f->access & FILE_READ_EA)) return STATUS_ACCESS_DENIED;
	if (!f->node->have_lxmod) return STATUS_NOT_FOUND;
	if (!get || listlen < 12 || get->EaNameLength != 6 ||
	    memcmp(get->EaName, "$LXMOD", 6)) return STATUS_NOT_FOUND;
	if (len < 19) return STATUS_BUFFER_TOO_SMALL;
	memset(ea, 0, 19);
	ea->EaNameLength = 6;
	ea->EaValueLength = 4;
	memcpy(ea->EaName, "$LXMOD", 7);
	p = (unsigned char *)ea->EaName + 7;
	p[0] = (unsigned char)f->node->lxmod;
	p[1] = (unsigned char)(f->node->lxmod >> 8);
	p[2] = (unsigned char)(f->node->lxmod >> 16);
	p[3] = (unsigned char)(f->node->lxmod >> 24);
	if (io) { io->Status = STATUS_SUCCESS; io->Information = 19; }
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtSetEaFile(HANDLE h, PIO_STATUS_BLOCK io, PVOID buf, ULONG len)
{
	struct ofile *f = of_get(h);
	__NT_FILE_FULL_EA_INFORMATION *ea = buf;
	unsigned char *p;
	if (!f || f->kind != OF_VFS) return STATUS_INVALID_HANDLE;
	if (!(f->access & FILE_WRITE_EA)) return STATUS_ACCESS_DENIED;
	if (!ea || len < 19 || ea->EaNameLength != 6 || ea->EaValueLength != 4 ||
	    memcmp(ea->EaName, "$LXMOD", 6)) return STATUS_INVALID_PARAMETER;
	p = (unsigned char *)ea->EaName + 7;
	f->node->lxmod = (ULONG)p[0] | (ULONG)p[1] << 8 |
	                 (ULONG)p[2] << 16 | (ULONG)p[3] << 24;
	f->node->have_lxmod = 1;
	if (io) { io->Status = STATUS_SUCCESS; io->Information = 0; }
	return STATUS_SUCCESS;
}

/* The server (read) end of a pipe, and the only way one is created. */
NTSTATUS NTAPI NtCreateNamedPipeFile(PHANDLE out, ULONG access, POBJECT_ATTRIBUTES oa,
                                     PIO_STATUS_BLOCK io, ULONG share, ULONG disposition,
                                     ULONG options, ULONG type, ULONG readmode, ULONG completion,
                                     ULONG instances, ULONG inbuf, ULONG outbuf,
                                     LARGE_INTEGER *timeout)
{
	struct vpath vp;
	struct vpipe *p;
	struct ofile *f;
	NTSTATUS st;
	(void)share; (void)options; (void)completion; (void)instances;
	(void)inbuf; (void)outbuf; (void)timeout;

	/* Only the byte-stream kind is modelled; a message-mode pipe keeps
	 * record boundaries, which this queue does not. */
	if (type != FILE_PIPE_BYTE_STREAM_TYPE || readmode != FILE_PIPE_BYTE_STREAM_MODE)
		return STATUS_NOT_IMPLEMENTED;
	access = map_generic(access);
	st = resolve(oa, &vp);
	if (!NT_SUCCESS(st)) return st;
	if (!vp.pipename) return STATUS_OBJECT_PATH_NOT_FOUND;
	for (p = vpipes; p; p = p->next)
		if (wieq(p->name, p->namelen, vp.pipename, vp.pipelen)) {
			if (disposition == FILE_CREATE) return STATUS_OBJECT_NAME_COLLISION;
			break;
		}
	if (!p) {
		int fds[2];
		p = vmalloc(sizeof *p);
		if (!p) return STATUS_NO_MEMORY;
		memset(p, 0, sizeof *p);
		p->name = wdup(vp.pipename, vp.pipelen);
		p->namelen = vp.pipelen;
		if (!p->name) { vfree(p); return STATUS_NO_MEMORY; }
		if (syscall(SYS_pipe2, fds, 0) < 0) {
			vfree(p->name); vfree(p); return STATUS_INSUFFICIENT_RESOURCES;
		}
		p->rfd = fds[0];
		p->wfd = fds[1];
		p->next = vpipes;
		vpipes = p;
	}
	f = vmalloc(sizeof *f);
	if (!f) return STATUS_NO_MEMORY;
	memset(f, 0, sizeof *f);
	f->kind = OF_PIPE;
	f->pipe = p;
	f->access = access;
	f->writer = 0;
	st = of_install(f, out);
	if (!NT_SUCCESS(st)) { vfree(f); return st; }
	p->readers++;
	if (io) { io->Status = STATUS_SUCCESS; io->Information = FILE_CREATED; }
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtClose(HANDLE h)
{
	long i = (long)h - 1;
	struct ofile *f;
	if (h == SHIM_TOKEN_HANDLE) return STATUS_SUCCESS;
	if (i < 0 || i >= VFS_HANDLES || !vhandles[i]) return STATUS_INVALID_HANDLE;
	f = vhandles[i];
	vhandles[i] = 0;
	of_drop(f);
	return STATUS_SUCCESS;
}

/* A fixed local-SAM token identity for getuid()'s native tests.  The RID
 * is deliberately not 1000: retaining the old hardcoded uid would fail
 * every test which observes this S-1-5-21-111-222-333-4242 fixture. */
NTSTATUS NTAPI NtOpenProcessToken(HANDLE process, ACCESS_MASK access,
                                  PHANDLE token)
{
	if (process != NtCurrentProcess() || !(access & TOKEN_QUERY) || !token)
		return STATUS_INVALID_PARAMETER;
	*token = SHIM_TOKEN_HANDLE;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtQueryInformationToken(HANDLE token,
                                       TOKEN_INFORMATION_CLASS cls,
                                       PVOID buf, ULONG len, PULONG outlen)
{
	static const ULONG subauth[] = { 21, 111, 222, 333, 4242 };
	const ULONG sidlen = 8 + sizeof subauth;
	const ULONG need = sizeof(TOKEN_USER) + sidlen;
	TOKEN_USER *user;
	SID *sid;

	if (!outlen) return STATUS_ACCESS_VIOLATION;
	*outlen = need;
	if (token != SHIM_TOKEN_HANDLE) return STATUS_INVALID_HANDLE;
	if (cls != TokenUser) return STATUS_INVALID_INFO_CLASS;
	if (!buf || len < need) return STATUS_BUFFER_TOO_SMALL;

	memset(buf, 0, need);
	user = (TOKEN_USER *)buf;
	sid = (SID *)((UCHAR *)buf + sizeof(TOKEN_USER));
	user->User.Sid = sid;
	sid->Revision = SID_REVISION;
	sid->SubAuthorityCount = sizeof subauth / sizeof subauth[0];
	sid->IdentifierAuthority.Value[5] = 5;
	memcpy(sid->SubAuthority, subauth, sizeof subauth);
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtDuplicateObject(HANDLE srcproc, HANDLE src, HANDLE dstproc, PHANDLE dst,
                                 ACCESS_MASK access, ULONG attrs, ULONG options)
{
	struct ofile *f = of_get(src);
	NTSTATUS st;
	(void)srcproc; (void)dstproc; (void)access; (void)attrs;
	if (!f) return STATUS_INVALID_HANDLE;
	/* A duplicate names the same file object, so the two handles share
	 * one byte offset -- which is what dup() promises. */
	st = of_install(f, dst);
	if (NT_SUCCESS(st) && (options & DUPLICATE_CLOSE_SOURCE)) NtClose(src);
	return st;
}

/* ---- read and write ---- */

NTSTATUS NTAPI NtWriteFile(HANDLE h, HANDLE ev, PIO_APC_ROUTINE apc, PVOID ctx,
                           PIO_STATUS_BLOCK io, const void *buf, ULONG len,
                           LARGE_INTEGER *off, PULONG key)
{
	struct ofile *f = of_get(h);
	struct vnode *v;
	long long at;
	NTSTATUS st;
	(void)ev; (void)apc; (void)ctx; (void)key;

	if (!f) return STATUS_INVALID_HANDLE;
	if (f->kind == OF_STD) {
		long n = syscall(SYS_write, f->fd, buf, (size_t)len);
		if (n < 0) return STATUS_INVALID_DEVICE_REQUEST;
		if (io) { io->Status = STATUS_SUCCESS; io->Information = (ULONG_PTR)n; }
		return STATUS_SUCCESS;
	}
	if (f->kind == OF_NULLDEV) {
		if (io) { io->Status = STATUS_SUCCESS; io->Information = len; }
		return STATUS_SUCCESS;
	}
	if (f->kind == OF_PIPE) {
		struct vpipe *p = f->pipe;
		if (!f->writer) return STATUS_ACCESS_DENIED;
		/* p->readers is this process's own view -- exactly right in the
		 * common, unforked case, and a fast local check even when it
		 * isn't -- but a clone's copy of the object can go stale
		 * relative to another process sharing the same underlying pipe,
		 * so the real write() below, whose EPIPE the kernel derives from
		 * the fd table it actually shares, is the authoritative check. */
		if (!p->readers) return STATUS_PIPE_BROKEN;
		if (len) {
			long n = syscall(SYS_write, p->wfd, buf, (size_t)len);
			if (n < 0) return STATUS_PIPE_BROKEN;
			if (io) { io->Status = STATUS_SUCCESS; io->Information = (ULONG_PTR)n; }
			return STATUS_SUCCESS;
		}
		if (io) { io->Status = STATUS_SUCCESS; io->Information = 0; }
		return STATUS_SUCCESS;
	}
	v = f->node;
	if (v->isdir) return STATUS_INVALID_DEVICE_REQUEST;
	if (!(f->access & WRITE_ACCESS)) return STATUS_ACCESS_DENIED;

	/* An append-only handle (FILE_APPEND_DATA without FILE_WRITE_DATA,
	 * which is what open() grants for O_APPEND) always writes at the end,
	 * whatever offset is asked for; so does an explicit
	 * FILE_WRITE_TO_END_OF_FILE. */
	if (!(f->access & FILE_WRITE_DATA)) at = v->size;
	else if (!off || *off == FILE_USE_FILE_POINTER_POSITION) at = f->pos;
	else if (*off == FILE_WRITE_TO_END_OF_FILE) at = v->size;
	else if (*off < 0) return STATUS_INVALID_PARAMETER;
	else at = *off;

	if (len) {
		st = file_grow(v, at + len);
		if (!NT_SUCCESS(st)) return st;
		if (at > v->size) memset(v->data + v->size, 0, (size_t)(at - v->size));
		memcpy(v->data + at, buf, len);
		if (at + len > v->size) v->size = at + len;
		v->mtime = v->chtime = now_nt();
	}
	/* Synchronous handles move to the end of the transfer even when the
	 * offset was explicit; src/internal/fdpos.c puts it back. */
	f->pos = at + len;
	if (io) { io->Status = STATUS_SUCCESS; io->Information = len; }
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtReadFile(HANDLE h, HANDLE ev, PIO_APC_ROUTINE apc, PVOID ctx,
                          PIO_STATUS_BLOCK io, PVOID buf, ULONG len,
                          LARGE_INTEGER *off, PULONG key)
{
	struct ofile *f = of_get(h);
	struct vnode *v;
	long long at;
	ULONG n;
	(void)ev; (void)apc; (void)ctx; (void)key;

	if (!f) return STATUS_INVALID_HANDLE;
	if (f->kind == OF_STD) {
		long r = syscall(SYS_read, f->fd, buf, (size_t)len);
		if (r < 0) return STATUS_INVALID_DEVICE_REQUEST;
		if (io) { io->Status = STATUS_SUCCESS; io->Information = (ULONG_PTR)r; }
		return r == 0 ? STATUS_END_OF_FILE : STATUS_SUCCESS;
	}
	if (f->kind == OF_NULLDEV) {
		if (io) { io->Status = STATUS_END_OF_FILE; io->Information = 0; }
		return STATUS_END_OF_FILE;
	}
	if (f->kind == OF_PIPE) {
		struct vpipe *p = f->pipe;
		long r;
		if (f->writer) return STATUS_ACCESS_DENIED;
		if (!len) { if (io) { io->Status = STATUS_SUCCESS; io->Information = 0; } return STATUS_SUCCESS; }
		/* The host fd blocks (see struct vpipe): this genuinely waits
		 * for a writer, the way a real synchronous NT read would, rather
		 * than reporting STATUS_PIPE_EMPTY for "nothing yet" -- there is
		 * no way to loop back to the caller and try again later the way
		 * an overlapped/async caller could.  0 is the kernel's own
		 * end-of-file: authoritative for "every writer, in every process
		 * sharing this pipe, is gone", which this process's own
		 * p->writers count cannot be once a clone is involved (see
		 * NtWriteFile's note).  STATUS_PIPE_EMPTY is dead code below in
		 * this build -- the host fd this file hands out is never put in
		 * non-blocking mode -- but it is left as the return for a
		 * negative read() rather than asserted away, since it remains
		 * the correct NT status for that case if that ever changes. */
		r = syscall(SYS_read, p->rfd, buf, (size_t)len);
		if (r > 0) { if (io) { io->Status = STATUS_SUCCESS; io->Information = (ULONG_PTR)r; } return STATUS_SUCCESS; }
		if (r == 0) return STATUS_PIPE_BROKEN;
		return STATUS_PIPE_EMPTY;
	}
	v = f->node;
	if (v->isdir) return STATUS_INVALID_DEVICE_REQUEST;
	if (!(f->access & FILE_READ_DATA)) return STATUS_ACCESS_DENIED;

	if (!off || *off == FILE_USE_FILE_POINTER_POSITION) at = f->pos;
	else if (*off < 0) return STATUS_INVALID_PARAMETER;
	else at = *off;

	if (at >= v->size) {
		if (io) { io->Status = STATUS_END_OF_FILE; io->Information = 0; }
		return STATUS_END_OF_FILE;
	}
	n = (ULONG)(v->size - at < (long long)len ? v->size - at : (long long)len);
	memcpy(buf, v->data + at, n);
	f->pos = at + n;
	if (io) { io->Status = STATUS_SUCCESS; io->Information = n; }
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtFlushBuffersFile(HANDLE h, PIO_STATUS_BLOCK io)
{
	struct ofile *f = of_get(h);
	if (!f) return STATUS_INVALID_HANDLE;
	if (io) { io->Status = STATUS_SUCCESS; io->Information = 0; }
	return STATUS_SUCCESS;   /* nothing is buffered anywhere but memory */
}

/* ---- querying ---- */

static void fill_basic(struct vnode *v, FILE_BASIC_INFORMATION *bi)
{
	bi->CreationTime = v->ctime;
	bi->LastAccessTime = v->atime;
	bi->LastWriteTime = v->mtime;
	bi->ChangeTime = v->chtime;
	bi->FileAttributes = v->attrs;
}

static long long alloc_size(struct vnode *v)
{
	/* Rounded to a 4K cluster, the way a real volume reports it. */
	return v->isdir ? 0 : ((v->size + 4095) & ~4095LL);
}

static NTSTATUS query_name(struct ofile *f, PVOID buf, ULONG len, PIO_STATUS_BLOCK io)
{
	FILE_NAME_INFORMATION *ni = buf;
	WCHAR *path;
	size_t plen, want, fit;

	/* NT reports the path below the volume: "\dir\file". */
	path = node_path(f->dir, f->name, f->namelen, &plen);
	if (!path) return STATUS_NO_MEMORY;
	want = plen - 6;                              /* drop "\??\C:" */
	if (len < sizeof(ULONG)) { vfree(path); return STATUS_INFO_LENGTH_MISMATCH; }
	ni->FileNameLength = (ULONG)(want * sizeof(WCHAR));
	fit = (len - offsetof(FILE_NAME_INFORMATION, FileName)) / sizeof(WCHAR);
	if (fit > want) fit = want;
	memcpy(ni->FileName, path + 6, fit * sizeof(WCHAR));
	vfree(path);
	if (io) io->Information = offsetof(FILE_NAME_INFORMATION, FileName) + fit * sizeof(WCHAR);
	return fit < want ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
}

NTSTATUS NTAPI NtQueryInformationFile(HANDLE h, PIO_STATUS_BLOCK io, PVOID buf,
                                      ULONG len, FILE_INFORMATION_CLASS cls)
{
	struct ofile *f = of_get(h);
	struct vnode *v;

	if (!f) return STATUS_INVALID_HANDLE;
	if (io) { io->Status = STATUS_SUCCESS; io->Information = 0; }
	/* src/select/select.c's __fd_probe() polls a pipe's readability via
	 * this class; answer it from the real host pipe fd (FIONREAD) so
	 * that path -- and thus test/posix-sysmisc.c's select()/poll() pipe
	 * tests -- runs for real here too, not just under Wine. */
	if (f->kind == OF_PIPE && cls == FilePipeLocalInformation) {
		struct vpipe *p = f->pipe;
		FILE_PIPE_LOCAL_INFORMATION *pli = buf;
		int avail = 0;

		if (len < sizeof *pli) return STATUS_INFO_LENGTH_MISMATCH;
		memset(pli, 0, sizeof *pli);
		/* Both ends still open on this process's view: p->readers/
		 * writers is exactly right in the common, unforked case that
		 * every select()/poll() test here exercises (see NtWriteFile's
		 * own comment on this same field for the cross-process
		 * caveat). */
		pli->NamedPipeState = (p->readers && p->writers) ? FILE_PIPE_CONNECTED_STATE : 0;
		if (p->rfd >= 0) syscall(SYS_ioctl, p->rfd, 0x541B /* FIONREAD */, &avail);
		/* ReadDataAvailable is what THIS end can read, so it is the
		 * queue depth only for the read end; the write end reads
		 * nothing. */
		pli->ReadDataAvailable = f->writer ? 0 : (ULONG)avail;

		/* WriteQuotaAvailable is answered for real here, because it is
		 * now load-bearing in src/select/select.c and neither of the
		 * other two environments can exercise it: wine-9.0 (what
		 * `make check` runs against) hardcodes it to 0, so select.c's
		 * wqa_works() probe disables the path there entirely.  This
		 * native build is therefore the only oracle outside real-NT
		 * CI, and a 0 here would silently disable the very logic the
		 * ASan run exists to cover.
		 *
		 * Follows the rule measured on Windows Server 2025 build
		 * 26100: an end's WriteQuotaAvailable is its write-direction
		 * quota minus the bytes currently buffered in that direction.
		 * The host pipe carries exactly one direction (writer -> reader),
		 * so the write end's buffered count is the host FIONREAD and
		 * the read end -- whose write direction is never used by
		 * src/unistd/pipe.c -- sits at its full quota. */
		pli->InboundQuota = PIPE_QUOTA;
		pli->OutboundQuota = PIPE_QUOTA;
		pli->NamedPipeEnd = f->writer ? FILE_PIPE_CLIENT_END : FILE_PIPE_SERVER_END;
		/* Clamped, not merely subtracted.  A write larger than the
		 * buffer gets queued, so a buffered count can transiently
		 * exceed the quota; an unclamped PIPE_QUOTA - avail would
		 * underflow this ULONG into a huge value that reads as
		 * "enormous room available" -- the same wrong answer the
		 * hardcoded always-writable used to give, reached by a
		 * different route and much harder to spot. */
		pli->WriteQuotaAvailable = f->writer
			? (ULONG)(avail <= 0 ? PIPE_QUOTA : avail >= PIPE_QUOTA ? 0 : PIPE_QUOTA - avail)
			: PIPE_QUOTA;
		if (io) io->Information = sizeof *pli;
		return STATUS_SUCCESS;
	}
	if (f->kind != OF_VFS) {
		/* A console, a pipe or the null device: NT answers the position
		 * and size classes with STATUS_INVALID_DEVICE_REQUEST rather than
		 * inventing a size, and so does this. */
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	v = f->node;
	switch (cls) {
	case FileBasicInformation:
		if (len < sizeof(FILE_BASIC_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;
		fill_basic(v, buf);
		if (io) io->Information = sizeof(FILE_BASIC_INFORMATION);
		return STATUS_SUCCESS;
	case FileStandardInformation: {
		FILE_STANDARD_INFORMATION *si = buf;
		if (len < sizeof *si) return STATUS_INFO_LENGTH_MISMATCH;
		si->AllocationSize = alloc_size(v);
		si->EndOfFile = v->isdir ? 0 : v->size;
		si->NumberOfLinks = (ULONG)(v->nlink > 0 ? v->nlink : 1);
		si->DeletePending = (BOOLEAN)v->delete_pending;
		si->Directory = (BOOLEAN)v->isdir;
		if (io) io->Information = sizeof *si;
		return STATUS_SUCCESS;
	}
	case FileInternalInformation: {
		FILE_INTERNAL_INFORMATION *ii = buf;
		if (len < sizeof *ii) return STATUS_INFO_LENGTH_MISMATCH;
		ii->IndexNumber = (LARGE_INTEGER)v->id;
		if (io) io->Information = sizeof *ii;
		return STATUS_SUCCESS;
	}
	case FilePositionInformation: {
		FILE_POSITION_INFORMATION *pi = buf;
		if (len < sizeof *pi) return STATUS_INFO_LENGTH_MISMATCH;
		pi->CurrentByteOffset = f->pos;
		if (io) io->Information = sizeof *pi;
		return STATUS_SUCCESS;
	}
	case FileAttributeTagInformation: {
		FILE_ATTRIBUTE_TAG_INFORMATION *ti = buf;
		if (len < sizeof *ti) return STATUS_INFO_LENGTH_MISMATCH;
		ti->FileAttributes = v->attrs;
		ti->ReparseTag = 0;      /* nothing here is a reparse point */
		if (io) io->Information = sizeof *ti;
		return STATUS_SUCCESS;
	}
	case FileModeInformation: {
		FILE_MODE_INFORMATION *mi = buf;
		if (len < sizeof *mi) return STATUS_INFO_LENGTH_MISMATCH;
		mi->Mode = f->options;
		if (io) io->Information = sizeof *mi;
		return STATUS_SUCCESS;
	}
	case FileAccessInformation: {
		FILE_ACCESS_INFORMATION *ai = buf;
		if (len < sizeof *ai) return STATUS_INFO_LENGTH_MISMATCH;
		ai->AccessFlags = f->access;
		if (io) io->Information = sizeof *ai;
		return STATUS_SUCCESS;
	}
	case FileNameInformation:
		return query_name(f, buf, len, io);
	case FileNetworkOpenInformation: {
		FILE_NETWORK_OPEN_INFORMATION *no = buf;
		if (len < sizeof *no) return STATUS_INFO_LENGTH_MISMATCH;
		no->CreationTime = v->ctime;
		no->LastAccessTime = v->atime;
		no->LastWriteTime = v->mtime;
		no->ChangeTime = v->chtime;
		no->AllocationSize = alloc_size(v);
		no->EndOfFile = v->isdir ? 0 : v->size;
		no->FileAttributes = v->attrs;
		if (io) io->Information = sizeof *no;
		return STATUS_SUCCESS;
	}
	default:
		/* Including FileAllInformation: nothing in ntlibc asks for it, and
		 * a half-filled FILE_ALL_INFORMATION would be worse than a refusal. */
		return STATUS_INVALID_INFO_CLASS;
	}
}

NTSTATUS NTAPI NtQueryVolumeInformationFile(HANDLE h, PIO_STATUS_BLOCK io, PVOID buf,
                                            ULONG len, FS_INFORMATION_CLASS cls)
{
	struct ofile *f = of_get(h);

	if (!f) return STATUS_INVALID_HANDLE;
	if (io) { io->Status = STATUS_SUCCESS; io->Information = 0; }
	switch (cls) {
	case FileFsDeviceInformation: {
		FILE_FS_DEVICE_INFORMATION *d = buf;
		if (len < sizeof *d) return STATUS_INFO_LENGTH_MISMATCH;
		/* stdin/stdout/stderr of a native test run are pipes or ttys;
		 * calling them character devices is the conservative answer (no
		 * seeking, no directory).  Simulated files live on a disk. */
		d->DeviceType = f->kind == OF_VFS ? FILE_DEVICE_DISK :
		                f->kind == OF_PIPE ? FILE_DEVICE_NAMED_PIPE : FILE_DEVICE_NULL;
		d->Characteristics = 0;
		if (io) io->Information = sizeof *d;
		return STATUS_SUCCESS;
	}
	case FileFsVolumeInformation: {
		FILE_FS_VOLUME_INFORMATION *vi = buf;
		if (len < offsetof(FILE_FS_VOLUME_INFORMATION, VolumeLabel)) return STATUS_INFO_LENGTH_MISMATCH;
		if (f->kind != OF_VFS) return STATUS_INVALID_DEVICE_REQUEST;
		memset(vi, 0, offsetof(FILE_FS_VOLUME_INFORMATION, VolumeLabel));
		vi->VolumeSerialNumber = VOLUME_SERIAL;
		vi->VolumeLabelLength = 0;
		if (io) io->Information = offsetof(FILE_FS_VOLUME_INFORMATION, VolumeLabel);
		return STATUS_SUCCESS;
	}
	/* The three classes src/stat/statvfs.c needs.
	 *
	 * These used to fall through to STATUS_INVALID_INFO_CLASS, which is
	 * not something a real NT volume can answer: every NT file system
	 * that can host a file -- NTFS, FAT, exFAT, even a network redirector
	 * -- answers FileFsSizeInformation and FileFsAttributeInformation,
	 * and NTFS answers FileFsFullSizeInformation too.  Refusing them made
	 * statvfs(".") fail outright here, and with it every one of
	 * test/posix-sysmisc.c's statvfs assertions, on a path that works
	 * perfectly well against a real volume.  The stub was the wrong side
	 * of that disagreement, not statvfs.c.
	 *
	 * The numbers are a synthetic volume rather than a report on the
	 * host: this file system lives in this process's heap and has no
	 * capacity of its own to report.  They are chosen to be the shape NT
	 * really produces -- a power-of-two cluster built from a sector count
	 * and a sector size, and Caller <= Actual <= Total, which is the
	 * quota relationship FILE_FS_FULL_SIZE_INFORMATION exists to express
	 * and which POSIX mirrors as f_bavail <= f_bfree <= f_blocks.  A test
	 * that asserted a *particular* capacity would be asserting this stub,
	 * so none does; what is asserted is the invariants, and those hold. */
	case FileFsFullSizeInformation: {
		FILE_FS_FULL_SIZE_INFORMATION *fs = buf;
		if (len < sizeof *fs) return STATUS_INFO_LENGTH_MISMATCH;
		if (f->kind != OF_VFS) return STATUS_INVALID_DEVICE_REQUEST;
		fs->TotalAllocationUnits = VOLUME_TOTAL_UNITS;
		fs->ActualAvailableAllocationUnits = VOLUME_FREE_UNITS;
		/* strictly below the unrestricted figure, so a statvfs() that
		 * mixed up ActualAvailable (f_bfree) and CallerAvailable
		 * (f_bavail) would be visible rather than a tie. */
		fs->CallerAvailableAllocationUnits = VOLUME_AVAIL_UNITS;
		fs->SectorsPerAllocationUnit = VOLUME_SECTORS_PER_UNIT;
		fs->BytesPerSector = VOLUME_BYTES_PER_SECTOR;
		if (io) io->Information = sizeof *fs;
		return STATUS_SUCCESS;
	}
	case FileFsSizeInformation: {
		/* The pre-NTFS-quota class.  statvfs.c only reaches it when the
		 * full-size class above is refused, which this stub never does --
		 * it is here because a real volume answers it, and a stub that
		 * answered only the class the current caller happens to prefer
		 * would quietly stop modelling NT the moment that preference
		 * changed.  It reports one free figure, not two: that is the
		 * whole difference between the classes. */
		FILE_FS_SIZE_INFORMATION *fs = buf;
		if (len < sizeof *fs) return STATUS_INFO_LENGTH_MISMATCH;
		if (f->kind != OF_VFS) return STATUS_INVALID_DEVICE_REQUEST;
		fs->TotalAllocationUnits = VOLUME_TOTAL_UNITS;
		fs->AvailableAllocationUnits = VOLUME_AVAIL_UNITS;
		fs->SectorsPerAllocationUnit = VOLUME_SECTORS_PER_UNIT;
		fs->BytesPerSector = VOLUME_BYTES_PER_SECTOR;
		if (io) io->Information = sizeof *fs;
		return STATUS_SUCCESS;
	}
	case FileFsAttributeInformation: {
		/* FileSystemName is variable-length and the caller is expected
		 * to over-allocate for it (statvfs.c does); the fixed head is
		 * all that must fit for the call to be answerable, so a caller
		 * that supplied only that gets the head and STATUS_SUCCESS the
		 * way NT gives it -- not a length mismatch. */
		FILE_FS_ATTRIBUTE_INFORMATION *at = buf;
		static const WCHAR fsname[4] = { 'N', 'T', 'F', 'S' };
		ULONG head = offsetof(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName);
		if (len < head) return STATUS_INFO_LENGTH_MISMATCH;
		if (f->kind != OF_VFS) return STATUS_INVALID_DEVICE_REQUEST;
		/* Not FILE_READ_ONLY_VOLUME: this file system is writable, and
		 * the tests create files on it. */
		at->FileSystemAttributes = 0;
		at->MaximumComponentNameLength = 255;   /* NTFS, in characters */
		at->FileSystemNameLength = (ULONG)sizeof fsname;
		if (len >= head + sizeof fsname) {
			memcpy(at->FileSystemName, fsname, sizeof fsname);
			if (io) io->Information = head + sizeof fsname;
			return STATUS_SUCCESS;
		}
		if (io) io->Information = head;
		return STATUS_BUFFER_OVERFLOW;
	}
	default:
		return STATUS_INVALID_INFO_CLASS;
	}
}

NTSTATUS NTAPI NtQueryFullAttributesFile(POBJECT_ATTRIBUTES oa, FILE_NETWORK_OPEN_INFORMATION *no)
{
	struct vpath vp;
	struct vnode *v;
	NTSTATUS st = resolve(oa, &vp);
	if (!NT_SUCCESS(st)) return st;
	if (vp.nulldev || vp.condev) {
		memset(no, 0, sizeof *no);
		no->FileAttributes = FILE_ATTRIBUTE_DEVICE;
		return STATUS_SUCCESS;
	}
	v = vpath_node(&vp);
	if (!v) return STATUS_OBJECT_NAME_NOT_FOUND;
	no->CreationTime = v->ctime;
	no->LastAccessTime = v->atime;
	no->LastWriteTime = v->mtime;
	no->ChangeTime = v->chtime;
	no->AllocationSize = alloc_size(v);
	no->EndOfFile = v->isdir ? 0 : v->size;
	no->FileAttributes = v->attrs;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtQueryAttributesFile(POBJECT_ATTRIBUTES oa, FILE_BASIC_INFORMATION *bi)
{
	struct vpath vp;
	struct vnode *v;
	NTSTATUS st = resolve(oa, &vp);
	if (!NT_SUCCESS(st)) return st;
	if (vp.nulldev || vp.condev) {
		memset(bi, 0, sizeof *bi);
		bi->FileAttributes = FILE_ATTRIBUTE_DEVICE;
		return STATUS_SUCCESS;
	}
	v = vpath_node(&vp);
	if (!v) return STATUS_OBJECT_NAME_NOT_FOUND;
	fill_basic(v, bi);
	return STATUS_SUCCESS;
}

/* ObjectNameInformation: the full NT name of an open handle.  Wine
 * reports drive paths in exactly this \??\C:\... form and
 * src/internal/path.c has a fast path for it. */
NTSTATUS NTAPI NtQueryObject(HANDLE h, OBJECT_INFORMATION_CLASS cls, PVOID buf, ULONG len, PULONG ret)
{
	struct { UNICODE_STRING Name; WCHAR Buffer[1]; } *oni = buf;
	struct ofile *f = of_get(h);
	WCHAR *path;
	size_t plen, need;

	if (!f) return STATUS_INVALID_HANDLE;
	if (cls != ObjectNameInformation) return STATUS_INVALID_INFO_CLASS;
	if (f->kind != OF_VFS) return STATUS_OBJECT_TYPE_MISMATCH;
	path = node_path(f->dir, f->name, f->namelen, &plen);
	if (!path) return STATUS_NO_MEMORY;
	need = sizeof *oni + plen * sizeof(WCHAR);
	if (ret) *ret = (ULONG)need;
	if (len < need) { vfree(path); return STATUS_INFO_LENGTH_MISMATCH; }
	oni->Name.Buffer = oni->Buffer;
	oni->Name.Length = (USHORT)(plen * sizeof(WCHAR));
	oni->Name.MaximumLength = (USHORT)((plen + 1) * sizeof(WCHAR));
	memcpy(oni->Buffer, path, (plen + 1) * sizeof(WCHAR));
	vfree(path);
	return STATUS_SUCCESS;
}

/* ---- setting ---- */

static NTSTATUS do_rename(struct ofile *f, PVOID buf, ULONG len, int ex)
{
	FILE_RENAME_INFORMATION *ri = buf;
	OBJECT_ATTRIBUTES oa;
	UNICODE_STRING us;
	struct vpath vp;
	struct vnode *target;
	struct vent *e;
	NTSTATUS st;
	int replace;

	if (len < offsetof(FILE_RENAME_INFORMATION, FileName)) return STATUS_INFO_LENGTH_MISMATCH;
	if (ri->FileNameLength > len - offsetof(FILE_RENAME_INFORMATION, FileName))
		return STATUS_INFO_LENGTH_MISMATCH;
	if (ri->FileNameLength > 0xfffe) return STATUS_NAME_TOO_LONG;
	replace = ex ? (ri->Flags & FILE_RENAME_REPLACE_IF_EXISTS) != 0 : ri->ReplaceIfExists != 0;

	us.Buffer = ri->FileName;
	us.Length = (USHORT)ri->FileNameLength;
	us.MaximumLength = us.Length;
	InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, ri->RootDirectory, 0);
	st = resolve(&oa, &vp);
	if (!NT_SUCCESS(st)) return st;
	if (!vp.leaf) return STATUS_OBJECT_NAME_INVALID;
	if (vp.nulldev || vp.condev) return STATUS_ACCESS_DENIED;

	e = dir_find(f->dir, f->name, f->namelen);
	if (!e || e->node != f->node) return STATUS_OBJECT_NAME_NOT_FOUND;

	target = vpath_node(&vp);
	if (target && target != f->node) {
		if (!replace) return STATUS_OBJECT_NAME_COLLISION;
		/* NT will not replace a directory, or replace a file with a
		 * directory; POSIX says the same thing with EISDIR/ENOTDIR. */
		if (target->isdir != f->node->isdir) return STATUS_ACCESS_DENIED;
		if (target->isdir && target->entries) return STATUS_DIRECTORY_NOT_EMPTY;
		dir_remove(vp.dir, dir_find(vp.dir, vp.leaf, vp.leaflen));
	}
	if (target == f->node && vp.dir == f->dir) {
		/* Same file, possibly a change of letter case only. */
		WCHAR *nn = wdup(vp.leaf, vp.leaflen);
		if (!nn) return STATUS_NO_MEMORY;
		vfree(e->name);
		e->name = nn;
		e->namelen = vp.leaflen;
	} else {
		if (!dir_add(vp.dir, vp.leaf, vp.leaflen, f->node)) return STATUS_NO_MEMORY;
		dir_remove(f->dir, e);
	}
	{
		WCHAR *nn = wdup(vp.leaf, vp.leaflen);
		if (!nn) return STATUS_NO_MEMORY;
		vfree(f->name);
		f->name = nn;
		f->namelen = vp.leaflen;
		f->dir = vp.dir;
	}
	f->node->chtime = now_nt();
	return STATUS_SUCCESS;
}

static NTSTATUS do_link(struct ofile *f, PVOID buf, ULONG len)
{
	FILE_RENAME_INFORMATION *li = buf;      /* same layout as FILE_LINK_INFORMATION */
	OBJECT_ATTRIBUTES oa;
	UNICODE_STRING us;
	struct vpath vp;
	NTSTATUS st;

	if (len < offsetof(FILE_RENAME_INFORMATION, FileName)) return STATUS_INFO_LENGTH_MISMATCH;
	if (f->node->isdir) return STATUS_FILE_IS_A_DIRECTORY;
	us.Buffer = li->FileName;
	us.Length = (USHORT)li->FileNameLength;
	us.MaximumLength = us.Length;
	InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, li->RootDirectory, 0);
	st = resolve(&oa, &vp);
	if (!NT_SUCCESS(st)) return st;
	if (!vp.leaf) return STATUS_OBJECT_NAME_INVALID;
	if (vpath_node(&vp)) {
		if (!li->ReplaceIfExists) return STATUS_OBJECT_NAME_COLLISION;
		dir_remove(vp.dir, dir_find(vp.dir, vp.leaf, vp.leaflen));
	}
	if (!dir_add(vp.dir, vp.leaf, vp.leaflen, f->node)) return STATUS_NO_MEMORY;
	f->node->chtime = now_nt();
	return STATUS_SUCCESS;
}

static NTSTATUS do_dispose(struct ofile *f, int del, int posix, int ignore_readonly)
{
	struct vnode *v = f->node;
	if (!del) { v->delete_pending = 0; return STATUS_SUCCESS; }
	if (!(f->access & DELETE)) return STATUS_ACCESS_DENIED;
	if ((v->attrs & FILE_ATTRIBUTE_READONLY) && !ignore_readonly) return STATUS_CANNOT_DELETE;
	if (v->isdir && v->entries) return STATUS_DIRECTORY_NOT_EMPTY;
	if (v == vroot) return STATUS_CANNOT_DELETE;
	if (posix) {
		/* POSIX semantics: the name goes now, the node lives until the
		 * last handle closes. */
		struct vent *e = f->dir ? dir_find(f->dir, f->name, f->namelen) : 0;
		if (e && e->node == v) { mirror_unlink(f); dir_remove(f->dir, e); }
		return STATUS_SUCCESS;
	}
	v->delete_pending = 1;       /* classic: unlinked when the last handle closes */
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtSetInformationFile(HANDLE h, PIO_STATUS_BLOCK io, PVOID buf,
                                    ULONG len, FILE_INFORMATION_CLASS cls)
{
	struct ofile *f = of_get(h);
	struct vnode *v;

	if (!f) return STATUS_INVALID_HANDLE;
	if (io) { io->Status = STATUS_SUCCESS; io->Information = 0; }
	if (f->kind != OF_VFS) return STATUS_INVALID_DEVICE_REQUEST;
	v = f->node;
	switch (cls) {
	case FilePositionInformation: {
		FILE_POSITION_INFORMATION *pi = buf;
		if (len < sizeof *pi) return STATUS_INFO_LENGTH_MISMATCH;
		if (pi->CurrentByteOffset < 0) return STATUS_INVALID_PARAMETER;
		f->pos = pi->CurrentByteOffset;
		return STATUS_SUCCESS;
	}
	case FileEndOfFileInformation: {
		FILE_END_OF_FILE_INFORMATION *ei = buf;
		if (len < sizeof *ei) return STATUS_INFO_LENGTH_MISMATCH;
		if (v->isdir) return STATUS_INVALID_PARAMETER;
		if (!(f->access & WRITE_ACCESS)) return STATUS_ACCESS_DENIED;
		return file_setsize(v, ei->EndOfFile);
	}
	case FileAllocationInformation:
		/* The allocated size is derived from the file size here, so there
		 * is nothing to preallocate; NT would round it up to a cluster. */
		if (len < sizeof(FILE_ALLOCATION_INFORMATION)) return STATUS_INFO_LENGTH_MISMATCH;
		if (!(f->access & WRITE_ACCESS)) return STATUS_ACCESS_DENIED;
		return STATUS_SUCCESS;
	case FileBasicInformation: {
		FILE_BASIC_INFORMATION *bi = buf;
		if (len < sizeof *bi) return STATUS_INFO_LENGTH_MISMATCH;
		/* 0 means "leave alone" and -1 "stop updating it automatically",
		 * which is indistinguishable from leaving it alone here. */
		if (bi->CreationTime > 0) v->ctime = bi->CreationTime;
		if (bi->LastAccessTime > 0) v->atime = bi->LastAccessTime;
		if (bi->LastWriteTime > 0) v->mtime = bi->LastWriteTime;
		if (bi->ChangeTime > 0) v->chtime = bi->ChangeTime;
		if (bi->FileAttributes) {
			ULONG a = bi->FileAttributes & ~(ULONG)FILE_ATTRIBUTE_DIRECTORY;
			if (a == FILE_ATTRIBUTE_NORMAL) a = 0;
			v->attrs = a | (v->isdir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE);
			v->chtime = now_nt();
		}
		return STATUS_SUCCESS;
	}
	case FileDispositionInformation: {
		FILE_DISPOSITION_INFORMATION *di = buf;
		if (len < sizeof *di) return STATUS_INFO_LENGTH_MISMATCH;
		return do_dispose(f, di->DeleteFile, 0, 0);
	}
	case FileDispositionInformationEx: {
		FILE_DISPOSITION_INFORMATION_EX *dx = buf;
		if (len < sizeof *dx) return STATUS_INFO_LENGTH_MISMATCH;
		return do_dispose(f, (dx->Flags & FILE_DISPOSITION_DELETE) != 0,
		                  (dx->Flags & FILE_DISPOSITION_POSIX_SEMANTICS) != 0,
		                  (dx->Flags & FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE) != 0);
	}
	case FileRenameInformation:   return do_rename(f, buf, len, 0);
	case FileRenameInformationEx: return do_rename(f, buf, len, 1);
	case FileLinkInformation:     return do_link(f, buf, len);
	default:
		return STATUS_INVALID_INFO_CLASS;
	}
}

/* ---- directory enumeration ---- */

NTSTATUS NTAPI NtQueryDirectoryFile(HANDLE h, HANDLE ev, PIO_APC_ROUTINE apc, PVOID ctx,
                                    PIO_STATUS_BLOCK io, PVOID buf, ULONG len,
                                    FILE_INFORMATION_CLASS cls, BOOLEAN single,
                                    PUNICODE_STRING mask, BOOLEAN restart)
{
	struct ofile *f = of_get(h);
	struct vnode *dir;
	struct vent *e;
	unsigned long idx = 0;
	ULONG used = 0, prev = 0xffffffffu;
	int any = 0;
	(void)ev; (void)apc; (void)ctx;

	if (!f) return STATUS_INVALID_HANDLE;
	if (f->kind != OF_VFS || !f->node->isdir) return STATUS_INVALID_PARAMETER;
	if (!(f->access & FILE_LIST_DIRECTORY)) return STATUS_ACCESS_DENIED;
	/* Only the class ntlibc uses is served; a half-filled record of some
	 * other shape would be worse than a refusal. */
	if (cls != FileIdBothDirectoryInformation) return STATUS_INVALID_INFO_CLASS;
	if (io) { io->Status = STATUS_SUCCESS; io->Information = 0; }
	if (restart) f->scan = 0;
	dir = f->node;

	/* "." and ".." first -- NTFS hands them back as ordinary records and
	 * src/dirent counts on that -- then entries in creation order. */
	for (e = 0, idx = 0; ; idx++) {
		const WCHAR *name;
		size_t namelen;
		struct vnode *node;
		FILE_ID_BOTH_DIR_INFORMATION *fi;
		ULONG need;

		if (idx == 0) { name = w_dot; namelen = 1; node = dir; }
		else if (idx == 1) { name = w_dotdot; namelen = 2;
		                     node = dir->parent ? dir->parent : dir; }
		else {
			unsigned long k = 2;
			for (e = dir->entries; e && k < idx; e = e->next) k++;
			if (!e) break;
			name = e->name; namelen = e->namelen; node = e->node;
		}
		if (idx < f->scan) continue;
		if (mask && mask->Length && !wmatch(mask->Buffer, mask->Length / sizeof(WCHAR), name, namelen))
			continue;

		need = (ULONG)(offsetof(FILE_ID_BOTH_DIR_INFORMATION, FileName) + namelen * sizeof(WCHAR));
		if (used + need > len) {
			if (!any) return STATUS_BUFFER_OVERFLOW;
			break;
		}
		fi = (FILE_ID_BOTH_DIR_INFORMATION *)((char *)buf + used);
		memset(fi, 0, offsetof(FILE_ID_BOTH_DIR_INFORMATION, FileName));
		fi->FileIndex = (ULONG)idx;
		fi->CreationTime = node->ctime;
		fi->LastAccessTime = node->atime;
		fi->LastWriteTime = node->mtime;
		fi->ChangeTime = node->chtime;
		fi->EndOfFile = node->isdir ? 0 : node->size;
		fi->AllocationSize = alloc_size(node);
		fi->FileAttributes = node->attrs;
		fi->FileNameLength = (ULONG)(namelen * sizeof(WCHAR));
		fi->FileId = (LARGE_INTEGER)node->id;
		memcpy(fi->FileName, name, namelen * sizeof(WCHAR));
		if (prev != 0xffffffffu)
			((FILE_ID_BOTH_DIR_INFORMATION *)((char *)buf + prev))->NextEntryOffset = used - prev;
		prev = used;
		used += (need + 7) & ~7u;
		any = 1;
		f->scan = idx + 1;
		if (single) break;
	}
	if (!any) return STATUS_NO_MORE_FILES;
	if (io) { io->Status = STATUS_SUCCESS; io->Information = used; }
	return STATUS_SUCCESS;
}

/* ---- DOS paths, and the current directory ---- */

/* The DOS path of a directory, "C:\dir\sub". */
static WCHAR *dos_path(struct vnode *dir, size_t *outlen)
{
	size_t n;
	WCHAR *nt = node_path(dir, 0, 0, &n), *p;
	if (!nt) return 0;
	p = wdup(nt + 4, n - 4);        /* drop "\??\" */
	vfree(nt);
	if (p && outlen) *outlen = n - 4;
	return p;
}

/* Reserved DOS device names, which RtlDosPathNameToNtPathName_U turns
 * into \??\NUL and friends wherever in the tree they appear.  Only the
 * two src/internal/path.c can produce are recognised. */
static int reserved_device(const WCHAR *p, size_t n)
{
	return wieq(p, n, w_nul, 3) || wieq(p, n, w_con, 3);
}

NTSTATUS NTAPI RtlDosPathNameToNtPathName_U_WithStatus(PCWSTR dos, PUNICODE_STRING out,
                                                       PCWSTR *filepart, PVOID reserved)
{
	const WCHAR *comp[256];
	size_t clen[256];
	int nc = 0, i;
	const WCHAR *p;
	size_t n, len, at;
	WCHAR *res;
	WCHAR *cwdp = 0;
	size_t cwdlen = 0;

	(void)reserved;
	if (!dos || !*dos || !out) return STATUS_OBJECT_NAME_INVALID;
	n = wlen(dos);
	p = dos;

	/* \\?\ and \\.\ pass through with no canonicalisation on NT; neither
	 * form is produced by src/internal/path.c, so they are refused here
	 * rather than half-implemented. */
	if (n >= 2 && (p[0] == '\\') && (p[1] == '\\')) return STATUS_OBJECT_PATH_NOT_FOUND;

	if (n >= 2 && ((upcase(p[0]) >= 'A' && upcase(p[0]) <= 'Z')) && p[1] == ':') {
		if (upcase(p[0]) != 'C') return STATUS_OBJECT_PATH_NOT_FOUND;   /* one volume */
		p += 2; n -= 2;
		if (!n || p[0] != '\\') {
			/* "C:rel" is relative to the drive's current directory. */
			cwdp = dos_path(vcwd, &cwdlen);
			if (!cwdp) return STATUS_NO_MEMORY;
		}
	} else if (p[0] != '\\') {
		cwdp = dos_path(vcwd, &cwdlen);
		if (!cwdp) return STATUS_NO_MEMORY;
	}

	/* Split the current directory, then the argument, into components,
	 * resolving "." and ".." as we go -- which is what makes
	 * stat("sub/../a.txt") name a.txt. */
	if (cwdp) {
		size_t k = 2;                     /* past "C:" */
		while (k < cwdlen) {
			size_t l;
			while (k < cwdlen && cwdp[k] == '\\') k++;
			for (l = 0; k + l < cwdlen && cwdp[k + l] != '\\'; l++) ;
			if (!l) break;
			if (nc == 256) { vfree(cwdp); return STATUS_NAME_TOO_LONG; }
			comp[nc] = cwdp + k; clen[nc] = l; nc++;
			k += l;
		}
	}
	while (n) {
		size_t l;
		while (n && *p == '\\') { p++; n--; }
		for (l = 0; l < n && p[l] != '\\'; l++) ;
		if (!l) break;
		if (l == 1 && p[0] == '.') { /* nothing */ }
		else if (l == 2 && p[0] == '.' && p[1] == '.') { if (nc) nc--; }
		else {
			/* Dropping a component would name a different file, so a
			 * path deeper than this refuses rather than truncates. */
			if (nc == 256) { vfree(cwdp); return STATUS_NAME_TOO_LONG; }
			comp[nc] = p; clen[nc] = l; nc++;
		}
		p += l; n -= l;
	}

	/* A reserved device name anywhere resolves to \??\NAME. */
	if (nc && reserved_device(comp[nc-1], clen[nc-1])) {
		res = vmalloc((4 + clen[nc-1] + 1) * sizeof(WCHAR));
		if (!res) { vfree(cwdp); return STATUS_NO_MEMORY; }
		res[0] = '\\'; res[1] = '?'; res[2] = '?'; res[3] = '\\';
		memcpy(res + 4, comp[nc-1], clen[nc-1] * sizeof(WCHAR));
		len = 4 + clen[nc-1];
		res[len] = 0;
		vfree(cwdp);
		out->Buffer = res;
		out->Length = (USHORT)(len * sizeof(WCHAR));
		out->MaximumLength = (USHORT)((len + 1) * sizeof(WCHAR));
		if (filepart) *filepart = res + 4;
		return STATUS_SUCCESS;
	}

	len = 6;                              /* "\??\C:" */
	for (i = 0; i < nc; i++) len += 1 + clen[i];
	if (nc == 0) len++;                   /* "\??\C:\" */
	if (len * sizeof(WCHAR) > 0xfffe) { vfree(cwdp); return STATUS_NAME_TOO_LONG; }
	res = vmalloc((len + 1) * sizeof(WCHAR));
	if (!res) { vfree(cwdp); return STATUS_NO_MEMORY; }
	memcpy(res, w_ntpfx, 6 * sizeof(WCHAR));
	at = 6;
	for (i = 0; i < nc; i++) {
		res[at++] = '\\';
		memcpy(res + at, comp[i], clen[i] * sizeof(WCHAR));
		if (filepart && i == nc - 1) *filepart = res + at;
		at += clen[i];
	}
	if (!nc) { res[at++] = '\\'; if (filepart) *filepart = 0; }
	res[at] = 0;
	vfree(cwdp);
	out->Buffer = res;
	out->Length = (USHORT)(at * sizeof(WCHAR));
	out->MaximumLength = (USHORT)((at + 1) * sizeof(WCHAR));
	return STATUS_SUCCESS;
}

BOOLEAN NTAPI RtlDosPathNameToNtPathName_U(PCWSTR dos, PUNICODE_STRING out,
                                           PCWSTR *filepart, PVOID reserved)
{
	return (BOOLEAN)NT_SUCCESS(RtlDosPathNameToNtPathName_U_WithStatus(dos, out, filepart, reserved));
}

ULONG NTAPI RtlGetCurrentDirectory_U(ULONG len, PWSTR buf)
{
	size_t n;
	WCHAR *p = dos_path(vcwd, &n);
	ULONG need;
	if (!p) return 0;
	/* NT reports the current directory with a trailing backslash and,
	 * when the buffer is too small, returns the size it needs including
	 * the terminator -- src/process/spawn.c relies on that. */
	need = (ULONG)((n + 2) * sizeof(WCHAR));
	if (len < need) { vfree(p); return need; }
	memcpy(buf, p, n * sizeof(WCHAR));
	buf[n] = '\\';
	buf[n + 1] = 0;
	vfree(p);
	return (ULONG)((n + 1) * sizeof(WCHAR));
}

NTSTATUS NTAPI RtlSetCurrentDirectory_U(PUNICODE_STRING dos)
{
	UNICODE_STRING nt;
	OBJECT_ATTRIBUTES oa;
	struct vpath vp;
	struct vnode *v;
	WCHAR *z;
	NTSTATUS st;

	if (!dos || !dos->Buffer) return STATUS_OBJECT_NAME_INVALID;
	z = wdup(dos->Buffer, dos->Length / sizeof(WCHAR));
	if (!z) return STATUS_NO_MEMORY;
	st = RtlDosPathNameToNtPathName_U_WithStatus(z, &nt, 0, 0);
	vfree(z);
	if (!NT_SUCCESS(st)) return st;
	InitializeObjectAttributes(&oa, &nt, OBJ_CASE_INSENSITIVE, 0, 0);
	st = resolve(&oa, &vp);
	if (NT_SUCCESS(st)) {
		v = vpath_node(&vp);
		if (!v) st = STATUS_OBJECT_NAME_NOT_FOUND;
		else if (!v->isdir) st = STATUS_NOT_A_DIRECTORY;
		else vcwd = v;
	}
	vfree(nt.Buffer);
	return st;
}

/* ---- start-up ---- */

static void vfs_init(void)
{
	int i;
	static const WCHAR work[4] = { 'w', 'o', 'r', 'k' };
	static const WCHAR tmp[3] = { 't', 'm', 'p' };
	struct vnode *w, *t;

	for (i = 0; i < 3; i++) {
		stdfiles[i].kind = OF_STD;
		stdfiles[i].fd = i;
		stdfiles[i].refs = 1;
		vhandles[i] = &stdfiles[i];
	}
	vroot = node_new(1);
	vroot->nlink = 1;                  /* the volume root is never removed */
	w = node_new(1);
	t = node_new(1);
	dir_add(vroot, work, 4, w);
	dir_add(vroot, tmp, 3, t);
	vcwd = w;
}

/* ---------------------------------------------------------------- clocks */

/* A FIXED SYSTEM CLOCK, FOR HARNESSES WHOSE TARGET READS "NOW".
 *
 * Off unless a harness asks for it, and a harness asks for it in its own
 * LLVMFuzzerInitialize, which libFuzzer runs before any input.
 *
 * WHY IT EXISTS.  A fuzz target that consults the wall clock is not
 * reproducible: src/time/getdate.c seeds its working struct tm from
 * time(0) so that fields a template does not mention default to today
 * (getdate.html: "elements ... not specified by the [matched] template
 * shall be set the same as their equivalents in the current time and
 * date"), which means the same input takes different branches on
 * different days, and a crash artefact may not reproduce from the input
 * that produced it.  A fuzzer whose findings do not reproduce is worth
 * very little.
 *
 * WHY FREEZING IS A FAITHFUL MODEL AND NOT A CONVENIENT FICTION -- which
 * matters, because a stub that encodes a belief rather than a platform
 * behaviour cannot be evidence about the code it serves.  NT's system
 * time is not a high-resolution counter: it advances at the timer tick,
 * about 15.6 ms by default, and NtQuerySystemTime returns the same value
 * for every call in between.  A program that queries it several times
 * inside one tick observes exactly what this produces -- an unchanging
 * clock.  Freezing is therefore a state the real platform genuinely
 * presents, at a duration this build stretches, not one it never does.
 * What it is NOT is a model of a clock that never advances at all, and
 * anything testing elapsed time must not use it; that is why it is
 * opt-in and why NtQueryPerformanceCounter below is deliberately left
 * alone -- a target measuring an interval should still see one.
 *
 * The instant is supplied by the caller and is arbitrary: no value here
 * is derived from anything under test.
 */
static long long fixed_time_ns100 = -1;

void __ntfuzz_freeze_clock(long long unix_seconds)
{
	/* 100ns ticks since 1601-01-01; 11644473600s from then to the
	 * Unix epoch, the same conversion the live path below uses. */
	fixed_time_ns100 = (unix_seconds + 11644473600LL) * 10000000LL;
}

NTSTATUS NTAPI NtQuerySystemTime(LARGE_INTEGER *t)
{
	struct { long sec, nsec; } ts = { 0, 0 };
	if (fixed_time_ns100 >= 0) { *t = fixed_time_ns100; return STATUS_SUCCESS; }
	syscall(SYS_clock_gettime, 0 /*CLOCK_REALTIME*/, &ts);
	/* 100ns ticks since 1601-01-01; 11644473600s from then to the epoch. */
	*t = (long long)(ts.sec + 11644473600LL) * 10000000LL + ts.nsec / 100;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtQueryPerformanceCounter(LARGE_INTEGER *c, LARGE_INTEGER *f)
{
	struct { long sec, nsec; } ts = { 0, 0 };
	syscall(SYS_clock_gettime, 1 /*CLOCK_MONOTONIC*/, &ts);
	*c = (long long)ts.sec * 10000000LL + ts.nsec / 100;
	if (f) *f = 10000000LL;
	return STATUS_SUCCESS;
}

/*
 * Waitable timers, as far as src/unistd/sleep.c's alarm() uses them:
 * one unnamed NotificationTimer per process, armed with an absolute
 * deadline and a user APC, cancellable, re-armable in place.
 *
 * The APC is the part worth modelling rather than stubbing out.  On NT
 * an expired timer queues its routine to the thread that armed it and
 * the kernel runs it at the head of that thread's next *alertable*
 * wait; that timing is the entire shape of SIGALRM delivery in this
 * library, so a stub that never called the routine would leave
 * test/posix-unistd-ids.c's test_alarm() asserting against a silence
 * this file invented.  NtDelayExecution below therefore does what the
 * kernel does: when it is alertable and the armed deadline falls
 * inside the delay, it sleeps as far as the deadline, calls the
 * routine, and reports STATUS_USER_APC.  A non-alertable delay leaves
 * the expiry queued, which is what makes the "a program that never
 * sleeps never sees its SIGALRM" boundary visible here too.
 *
 * One timer, no handle-table entry: alarm() creates its timer once and
 * never closes it (see there), so nothing needs NtClose to know about
 * this object, and the returned handle is a value outside the vhandles
 * range that of_get() will simply not find.
 */
#define TIMER_HANDLE ((HANDLE)(long)0x7100)

static PTIMER_APC_ROUTINE timer_apc;
static PVOID timer_apc_ctx;
static long long timer_due;      /* absolute NT time; 0 = not armed */
static int timer_queued;         /* expired, routine not yet run */

NTSTATUS NTAPI NtCreateTimer(PHANDLE h, ACCESS_MASK access, POBJECT_ATTRIBUTES oa, TIMER_TYPE type)
{
	(void)access; (void)oa; (void)type;
	if (!h) return STATUS_INVALID_PARAMETER;
	*h = TIMER_HANDLE;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtSetTimer(HANDLE h, LARGE_INTEGER *due, PTIMER_APC_ROUTINE apc, PVOID ctx,
                          BOOLEAN resume, LONG period, BOOLEAN *prev)
{
	(void)resume; (void)period;
	if (h != TIMER_HANDLE || !due) return STATUS_INVALID_HANDLE;
	if (prev) *prev = (BOOLEAN)(timer_due != 0);
	/* alarm() only ever passes an absolute deadline; a relative one
	 * would be a negative value here and is not modelled because
	 * nothing in this library asks for it. */
	if (*due < 0) return STATUS_INVALID_PARAMETER;
	timer_due = *due;
	timer_apc = apc;
	timer_apc_ctx = ctx;
	timer_queued = 0;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtCancelTimer(HANDLE h, BOOLEAN *prev)
{
	if (h != TIMER_HANDLE) return STATUS_INVALID_HANDLE;
	if (prev) *prev = (BOOLEAN)(timer_due != 0);
	timer_due = 0;
	timer_queued = 0;
	return STATUS_SUCCESS;
}

/*
 * This one has to really sleep.  libFuzzer runs a watchdog thread that
 * loops on sleep(1); returning immediately turns that into a spin that
 * starves the fuzzing thread -- the symptom is three executions in ninety
 * seconds.
 */
NTSTATUS NTAPI NtDelayExecution(BOOLEAN alertable, LARGE_INTEGER *t)
{
	struct { long sec, nsec; } ts;
	long long ticks;
	int fire = 0;
	if (!t) return STATUS_SUCCESS;
	ticks = *t;
	if (ticks >= 0) return STATUS_SUCCESS;   /* absolute time: not supported */
	ticks = -ticks;                          /* relative, in 100ns units */

	if (alertable && (timer_queued || timer_due)) {
		LARGE_INTEGER now;
		long long left;
		NtQuerySystemTime(&now);
		left = timer_queued ? 0 : timer_due - now;
		if (left <= 0) {                 /* already due: run it at once */
			ticks = 0;
			fire = 1;
		} else if (left < ticks) {       /* comes due inside this wait */
			ticks = left;
			fire = 1;
		}
	} else if (alertable == 0 && timer_due) {
		/* Non-alertable: the expiry is noticed but not delivered, and
		 * stays queued for the next alertable wait -- measured to be
		 * what NT and Wine both do. */
		LARGE_INTEGER now;
		NtQuerySystemTime(&now);
		if (timer_due - now <= ticks) timer_queued = 1;
	}

	if (ticks > 0) {
		ts.sec = (long)(ticks / 10000000LL);
		ts.nsec = (long)((ticks % 10000000LL) * 100);
		syscall(SYS_nanosleep, &ts, (void *)0);
	}
	if (fire) {
		PTIMER_APC_ROUTINE apc = timer_apc;
		PVOID ctx = timer_apc_ctx;
		timer_due = 0;
		timer_queued = 0;
		if (apc) apc(ctx, 0, 0);
		return STATUS_USER_APC;
	}
	return STATUS_SUCCESS;
}

/*
 * NtYieldExecution() -- src/misc/sched.c's sched_yield().
 *
 * Two things to model.  The yield itself is a real host sched_yield(2):
 * the point of the primitive is to relinquish the processor, and a stub
 * that returned without doing so would turn any spin-on-yield loop in
 * ntlibc into a busy wait here rather than showing it up.
 *
 * The status is the interesting half.  NT returns STATUS_SUCCESS only
 * when it actually switched to another thread, and the *informational*
 * (high bit clear, so NT_SUCCESS() is true) STATUS_NO_YIELD_PERFORMED
 * 0x40000024 when there was nothing else runnable -- which is what
 * kernel32's SwitchToThread() reports as FALSE, and what Wine returns
 * routinely.  These test binaries are single-threaded, so the honest
 * answer for every call is the second one, and it is the answer that
 * exercises the path test/posix-sysmisc.c's test_sched_yield() calls
 * the realistic way to get sched_yield() wrong: an implementation that
 * forwarded this status as a failure would return nonzero here, and its
 * loop of a thousand calls would catch it.  Returning STATUS_SUCCESS
 * would leave that assertion testing nothing.
 */
#define SYS_sched_yield 24
#define STATUS_NO_YIELD_PERFORMED ((NTSTATUS)0x40000024L)

NTSTATUS NTAPI NtYieldExecution(void)
{
	syscall(SYS_sched_yield);
	return STATUS_NO_YIELD_PERFORMED;
}

/* --------------------------------------------------------------- process
 *
 * Starting a child, and waiting for it.
 *
 * Unlike the file system above, this cannot be simulated in memory: what
 * __spawn asks for is that *another copy of the test program* runs, so
 * the host has to start a real process.  fork+execve is the whole of it.
 * The NT shape is kept where it is observable -- the process parameters
 * really are built and taken apart again, the command line really is
 * re-parsed by the rules crt1.c's split_cmdline uses, so a quoting bug in
 * src/process/spawn.c shows up here as a mangled argv rather than being
 * skipped over -- and the differences are these:
 *
 *   - The image path is an NT path (\??\C:\...) which is turned back into
 *     a host path.  The simulated volume's root doubles as the host root
 *     for this one purpose, because the image has to be a file the host
 *     kernel can actually execute.
 *   - NT creates the process suspended and __spawn resumes it;
 *     fork+execve starts running at once, so NtResumeThread is a no-op.
 *   - Handle inheritance is the host's: an inherited descriptor is one
 *     the child gets because fork copies the descriptor table, not
 *     because OBJ_INHERIT was set.  The simulated file system does not
 *     cross a real execve -- it is ordinary heap memory, and execve
 *     replaces the address space -- so the child gets its own, empty but
 *     for the starting layout, and cannot see a file its parent made
 *     there or a handle onto one (only descriptors 0-2, real host fds,
 *     survive the trip, the same as they would for any host program).
 *     __ntshim_init() below closes the one gap that actually matters to
 *     test/exec.c: it re-materialises a real copy of argv[0] into the
 *     freshly started volume (materialize_argv0(), above the node-tree
 *     helpers), so a process -- exec'd or not -- can always open its own
 *     on-disk path, and it rebuilds environ from the *real* envp execve()
 *     was given when RtlCreateUserProcess marked that envp as this file's
 *     own (XCHILD_MARK), rather than resetting environ to empty as a
 *     freshly started process otherwise does.  Nothing here shares the
 *     rest of the volume, or handles beyond 0-2, across the exec: a
 *     child cannot see a file its parent created elsewhere, or a
 *     descriptor its parent opened onto one.  No test needs it to.
 *   - A child killed by a host signal is reported with the exit code this
 *     library itself uses for a signal death, __NT_SIGNAL_EXIT(sig), so
 *     that waitpid() decodes it the way it would on NT.
 */

#define SYS_fork      57
#define SYS_execve    59
#define SYS_wait4     61
#define SYS_kill      62
#define SYS_faccessat 269

static char *utf8dup(const WCHAR *w, size_t n)
{
	ULONG need = 0;
	char *out;
	if (!w) return 0;
	RtlUnicodeToUTF8N(0, 0, &need, w, (ULONG)(n * sizeof(WCHAR)));
	out = vmalloc((size_t)need + 1);
	if (!out) return 0;
	RtlUnicodeToUTF8N(out, need, &need, w, (ULONG)(n * sizeof(WCHAR)));
	out[need] = 0;
	return out;
}

static void free_strv(char **v)
{
	size_t i;
	if (!v) return;
	for (i = 0; v[i]; i++) vfree(v[i]);
	vfree(v);
}

/* The exact inverse of src/process/spawn.c's quoting, which is the parse
 * every Windows C runtime -- and crt/crt1.c's split_cmdline -- performs:
 * the program name is delimited by quotes alone, and in the arguments a
 * backslash only escapes when it precedes a quote. */
static char **cmdline_to_argv(const WCHAR *p, size_t n)
{
	WCHAR *buf;
	char **argv;
	int argc = 0, cap = 8;
	size_t i = 0;

	buf = vmalloc((n + 1) * sizeof(WCHAR));
	argv = vmalloc(sizeof(char *) * (size_t)cap);
	if (!buf || !argv) { vfree(buf); vfree(argv); return 0; }

	{
		size_t o = 0;
		int inq = 0;
		while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
		while (i < n) {
			if (p[i] == '"') { inq = !inq; i++; continue; }
			if (!inq && (p[i] == ' ' || p[i] == '\t')) break;
			buf[o++] = p[i++];
		}
		argv[argc++] = utf8dup(buf, o);
	}
	for (;;) {
		size_t o = 0;
		int inq = 0;
		while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
		if (i >= n) break;
		for (;;) {
			if (i >= n) break;
			if (p[i] == '\\') {
				size_t nb = 0, k;
				while (i < n && p[i] == '\\') { nb++; i++; }
				if (i < n && p[i] == '"') {
					for (k = 0; k < nb / 2; k++) buf[o++] = '\\';
					if (nb & 1) { buf[o++] = '"'; i++; }
				} else {
					for (k = 0; k < nb; k++) buf[o++] = '\\';
				}
				continue;
			}
			if (p[i] == '"') {
				if (inq && i + 1 < n && p[i+1] == '"') { buf[o++] = '"'; i += 2; continue; }
				inq = !inq; i++;
				continue;
			}
			if (!inq && (p[i] == ' ' || p[i] == '\t')) break;
			buf[o++] = p[i++];
		}
		if (argc + 1 >= cap) {
			char **nv = vmalloc(sizeof(char *) * (size_t)cap * 2);
			if (!nv) { vfree(buf); free_strv(argv); return 0; }
			memcpy(nv, argv, sizeof(char *) * (size_t)argc);
			vfree(argv);
			argv = nv;
			cap *= 2;
		}
		argv[argc++] = utf8dup(buf, o);
	}
	argv[argc] = 0;
	vfree(buf);
	return argv;
}

/* The environment block: NAME=VALUE strings, each NUL-terminated, the
 * whole ended by an empty one. */
static char **env_to_envp(const WCHAR *block)
{
	char **v;
	int n = 0, cap = 16;
	const WCHAR *p = block;

	v = vmalloc(sizeof(char *) * (size_t)cap);
	if (!v) return 0;
	while (p && *p) {
		size_t len = wlen(p);
		if (n + 1 >= cap) {
			char **nv = vmalloc(sizeof(char *) * (size_t)cap * 2);
			if (!nv) { free_strv(v); return 0; }
			memcpy(nv, v, sizeof(char *) * (size_t)n);
			vfree(v);
			v = nv;
			cap *= 2;
		}
		v[n++] = utf8dup(p, len);
		p += len + 1;
	}
	v[n] = 0;
	return v;
}

static char *nt_to_host_path(const WCHAR *p, size_t n)
{
	char *s;
	size_t i;
	if (n >= 6 && p[0] == '\\' && p[1] == '?' && p[2] == '?' && p[3] == '\\' &&
	    upcase(p[4]) == 'C' && p[5] == ':') { p += 6; n -= 6; }
	s = utf8dup(p, n);
	if (!s) return 0;
	for (i = 0; s[i]; i++) if (s[i] == '\\') s[i] = '/';
	if (!s[0]) { vfree(s); s = utf8dup((const WCHAR[]){ '/' }, 1); }
	return s;
}

/* Classify an image that exists only in the simulated volume.  Native
 * process creation can execute one thing: another copy of this ELF test
 * binary.  Tests make such copies through ntlibc's own read/write calls,
 * so they have no corresponding host file for faccessat()/execve(). */
static int vfs_image_kind(const UNICODE_STRING *image)
{
	OBJECT_ATTRIBUTES oa;
	struct vpath vp;
	struct vnode *v;
	NTSTATUS st;

	InitializeObjectAttributes(&oa, (PUNICODE_STRING)image,
	                           OBJ_CASE_INSENSITIVE, 0, 0);
	st = resolve(&oa, &vp);
	if (!NT_SUCCESS(st) || !(v = vpath_node(&vp)) || v->isdir) return -1;
	if (v->size >= 4 && v->data && v->data[0] == 0x7f &&
	    v->data[1] == 'E' && v->data[2] == 'L' && v->data[3] == 'F') return 1;
	return 0;
}

static NTSTATUS dup_ustr(UNICODE_STRING *dst, const UNICODE_STRING *src)
{
	memset(dst, 0, sizeof *dst);
	if (!src || !src->Buffer) return STATUS_SUCCESS;
	dst->Buffer = wdup(src->Buffer, src->Length / sizeof(WCHAR));
	if (!dst->Buffer) return STATUS_NO_MEMORY;
	dst->Length = src->Length;
	dst->MaximumLength = (USHORT)(src->Length + sizeof(WCHAR));
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlCreateProcessParametersEx(PRTL_USER_PROCESS_PARAMETERS *out,
                                            PUNICODE_STRING image, PUNICODE_STRING dllpath,
                                            PUNICODE_STRING curdir, PUNICODE_STRING cmdline,
                                            PVOID env, PUNICODE_STRING title,
                                            PUNICODE_STRING desktop, PUNICODE_STRING shell,
                                            PUNICODE_STRING runtime, ULONG flags)
{
	RTL_USER_PROCESS_PARAMETERS *pp;
	(void)title; (void)desktop; (void)shell; (void)runtime; (void)flags;

	pp = vmalloc(sizeof *pp);
	if (!pp) return STATUS_NO_MEMORY;
	memset(pp, 0, sizeof *pp);
	pp->MaximumLength = pp->Length = sizeof *pp;
	pp->Flags = flags;
	if (!NT_SUCCESS(dup_ustr(&pp->ImagePathName, image)) ||
	    !NT_SUCCESS(dup_ustr(&pp->CommandLine, cmdline)) ||
	    !NT_SUCCESS(dup_ustr(&pp->DllPath, dllpath)) ||
	    !NT_SUCCESS(dup_ustr(&pp->CurrentDirectory.DosPath, curdir))) {
		RtlDestroyProcessParameters(pp);
		return STATUS_NO_MEMORY;
	}
	if (env) {
		const WCHAR *p = env;
		size_t n = 0;
		while (p[n]) n += wlen(p + n) + 1;
		n++;                                  /* the terminating empty string */
		pp->Environment = vmalloc(n * sizeof(WCHAR));
		if (!pp->Environment) { RtlDestroyProcessParameters(pp); return STATUS_NO_MEMORY; }
		memcpy(pp->Environment, env, n * sizeof(WCHAR));
		pp->EnvironmentSize = n * sizeof(WCHAR);
	}
	*out = pp;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlDestroyProcessParameters(PRTL_USER_PROCESS_PARAMETERS pp)
{
	if (!pp) return STATUS_SUCCESS;
	/* Only what RtlCreateProcessParametersEx allocated: RuntimeData points
	 * at the caller's own buffer. */
	vfree(pp->ImagePathName.Buffer);
	vfree(pp->CommandLine.Buffer);
	vfree(pp->DllPath.Buffer);
	vfree(pp->CurrentDirectory.DosPath.Buffer);
	vfree(pp->Environment);
	vfree(pp);
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlCreateUserProcess(PUNICODE_STRING image, ULONG attrs,
                                    PRTL_USER_PROCESS_PARAMETERS pp, PVOID psd, PVOID tsd,
                                    HANDLE parent, BOOLEAN inherit, HANDLE debug, HANDLE token,
                                    RTL_USER_PROCESS_INFORMATION *info)
{
	char *host;
	char **argv, **envp, **xenvp = 0;
	char *xstatus_entry = 0;
	char *xhost_entry = 0;
	char *xvfs_entry = 0;
	struct ofile *f;
	NTSTATUS st;
	long pid;
	int n, vfsfd = -1;
	(void)attrs; (void)psd; (void)tsd; (void)parent; (void)inherit; (void)debug; (void)token;

	if (!image || !pp || !info) return STATUS_INVALID_PARAMETER;
	host = nt_to_host_path(image->Buffer, image->Length / sizeof(WCHAR));
	if (!host) return STATUS_NO_MEMORY;
	argv = cmdline_to_argv(pp->CommandLine.Buffer, pp->CommandLine.Length / sizeof(WCHAR));
	envp = env_to_envp(pp->Environment);
	if (!argv || !envp) { st = STATUS_NO_MEMORY; goto out; }
	/* Checked before forking so that "no such program" is the status NT
	 * gives, rather than a child that exists just long enough to fail. */
	if (syscall(SYS_faccessat, -100 /*AT_FDCWD*/, host, 1 /*X_OK*/, 0) < 0) {
		int kind = vfs_image_kind(image);
		if (kind == 1 && host_self) {
			vfree(host);
			host = vmalloc(strlen(host_self) + 1);
			if (!host) { st = STATUS_NO_MEMORY; goto out; }
			strcpy(host, host_self);
		} else {
			st = kind == 0 ? STATUS_INVALID_IMAGE_FORMAT : STATUS_OBJECT_NAME_NOT_FOUND;
			goto out;
		}
	}

	f = vmalloc(sizeof *f);
	if (!f) { st = STATUS_NO_MEMORY; goto out; }
	memset(f, 0, sizeof *f);
	f->kind = OF_PROC;
	f->exitcode = STATUS_PENDING;
	f->snapshot_fd = -1;

	/* envp plus the marker __ntshim_init() looks for -- see XCHILD_MARK's
	 * definition -- so the child knows to rebuild environ from what
	 * execve() actually gave it rather than resetting it to empty, and
	 * (if this process has one) the fd of the shared exit-status table --
	 * see XSTATUS_FD_PREFIX's definition -- so the child records into the
	 * same table this process's own waitpid() will read from, rather than
	 * a fresh one of its own that nothing else can see. xenvp wraps
	 * envp's entries in a new array; the strings themselves are still
	 * envp's to free below. */
	for (n = 0; envp[n]; n++) ;
	xenvp = vmalloc(sizeof(char *) * (size_t)(n + 5));
	if (xenvp) {
		int m = n;
		memcpy(xenvp, envp, sizeof(char *) * (size_t)n);
		xenvp[m++] = (char *)XCHILD_MARK;
		if (host_self) {
			xhost_entry = vmalloc(sizeof(XHOST_PREFIX) + strlen(host_self));
			if (xhost_entry) {
				strcpy(xhost_entry, XHOST_PREFIX);
				strcat(xhost_entry, host_self);
				xenvp[m++] = xhost_entry;
			}
		}
		vfsfd = vfs_snapshot_export();
		if (vfsfd >= 0) {
			xvfs_entry = vmalloc(sizeof(XVFS_PREFIX) + 10);
			if (xvfs_entry) {
				snprintf(xvfs_entry, sizeof(XVFS_PREFIX) + 10, "%s%d",
				         XVFS_PREFIX, vfsfd);
				xenvp[m++] = xvfs_entry;
			} else {
				syscall(SYS_close, vfsfd);
				vfsfd = -1;
			}
		}
		if (xstatus_fd >= 0) {
			xstatus_entry = vmalloc(sizeof(XSTATUS_FD_PREFIX) + 10);
			if (xstatus_entry) {
				snprintf(xstatus_entry, sizeof(XSTATUS_FD_PREFIX) + 10, "%s%d",
				         XSTATUS_FD_PREFIX, xstatus_fd);
				xenvp[m++] = xstatus_entry;
			}
		}
		xenvp[m] = 0;
	}

	pid = syscall(SYS_fork);
	if (pid < 0) { vfree(f); st = STATUS_INSUFFICIENT_RESOURCES; goto out; }
	if (pid == 0) {
		syscall(SYS_execve, host, argv, xenvp ? xenvp : envp);
		syscall(SYS_exit_group, 127);
	}
	f->snapshot_fd = vfsfd;
	vfsfd = -1;
	f->pid = (int)pid;
	st = of_install(f, &info->Process);
	if (!NT_SUCCESS(st)) {
		if (f->snapshot_fd >= 0) syscall(SYS_close, f->snapshot_fd);
		vfree(f);
		goto out;
	}
	/* There is one thread and it is the process; the caller closes this
	 * handle separately, so it is a second reference to the same object. */
	st = of_install(f, &info->Thread);
	if (!NT_SUCCESS(st)) { NtClose(info->Process); goto out; }
	info->ClientId.UniqueProcess = (HANDLE)(LONG_PTR)pid;
	info->ClientId.UniqueThread = (HANDLE)(LONG_PTR)pid;
	st = STATUS_SUCCESS;

out:
	if (vfsfd >= 0) syscall(SYS_close, vfsfd);
	vfree(host);
	free_strv(argv);
	free_strv(envp);
	vfree(xenvp);           /* the wrapper array only; envp's own strings are envp's */
	vfree(xstatus_entry);
	vfree(xhost_entry);
	vfree(xvfs_entry);
	return st;
}

NTSTATUS NTAPI NtResumeThread(HANDLE h, PULONG count)
{
	/* fork+execve has no suspended state to leave. */
	struct ofile *f = of_get(h);
	if (!f || f->kind != OF_PROC) return STATUS_INVALID_HANDLE;
	if (count) *count = 1;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtSuspendProcess(HANDLE h)
{
	/* OF_PROC is backed by a real Linux child, so make process suspension
	 * stateful for native job-control tests.  The raw host signal number is
	 * intentional: this shim cannot include the host's signal.h alongside
	 * ntlibc's headers (and NtTerminateProcess below does the same for 9). */
	struct ofile *f = of_get(h);
	if (!f || f->kind != OF_PROC) return STATUS_INVALID_HANDLE;
	return syscall(SYS_kill, (long)f->pid, 19) < 0
		? STATUS_ACCESS_DENIED : STATUS_SUCCESS;
}

NTSTATUS NTAPI NtResumeProcess(HANDLE h)
{
	struct ofile *f = of_get(h);
	if (!f || f->kind != OF_PROC) return STATUS_INVALID_HANDLE;
	return syscall(SYS_kill, (long)f->pid, 18) < 0
		? STATUS_ACCESS_DENIED : STATUS_SUCCESS;
}

/* Reap the child if it has finished: 1 = reaped, 0 = still running (only
 * possible for nohang), -1 = the host wait4() itself failed.
 *
 * That last case is not hypothetical: RtlCloneUserProcess below makes a
 * clone with a real host fork(), and a process handle for a *third*
 * process -- one that existed before that fork and travelled into the
 * clone as plain memory in __children, exactly as fork.c's header
 * describes -- names a pid that is the host's child of the original
 * process, not of the clone.  The host kernel enforces real parentage:
 * wait4() on a pid that is not the caller's child fails with ECHILD
 * regardless of WNOHANG, immediately rather than by timing out.  That is
 * reported back as STATUS_INVALID_HANDLE (-> EBADF), one of the outcomes
 * test/fork-handles-win.c and test/process-win.c's test_prefork_handle
 * already treat as an acceptable, honest failure -- never a fabricated
 * status and never a hang. */
static int proc_poll(struct ofile *f, int nohang)
{
	int status = 0;
	long r;
	if (f->exited) return 1;
	r = syscall(SYS_wait4, (long)f->pid, &status, (long)(nohang ? 1 /*WNOHANG*/ : 0), 0);
	if (r == f->pid) {
		f->exited = 1;
		/* The child rewrites the inherited memfd as it exits.  Merge its
		 * file-tree changes before waitpid() returns, which is when a real
		 * shared filesystem would already expose them to the parent. */
		if (f->snapshot_fd >= 0) {
			ULONG magic = 0;
			syscall(SYS_lseek, f->snapshot_fd, 0, 0);
			if (host_read_all(f->snapshot_fd, &magic, sizeof magic) == 0 &&
			    magic == VFS_SNAPSHOT_MAGIC)
				(void)vfs_snapshot_read_dir(f->snapshot_fd, vroot);
			syscall(SYS_close, f->snapshot_fd);
			f->snapshot_fd = -1;
		}
		/* Prefer the out-of-band table (see its comment above): it has
		 * the real, full-width code NtTerminateProcess was asked to end
		 * this pid with, where the host's own wait status only carries
		 * 8 bits of it.  Fall back to the host-status heuristic when
		 * there is no matching entry -- a process that died some other
		 * way than through this file's own NtTerminateProcess, chiefly
		 * a genuine crash (a real ASan/UBSan finding, or a real signal)
		 * that this stub never got a chance to intercept. */
		if (xstatus_tab && xstatus_tab[(unsigned)f->pid % XSTATUS_N].pid == f->pid) {
			f->exitcode = xstatus_tab[(unsigned)f->pid % XSTATUS_N].code;
			xstatus_tab[(unsigned)f->pid % XSTATUS_N].pid = 0;   /* consumed; shrink the pid-reuse window */
		}
		else if ((status & 0x7f) == 0) f->exitcode = (status >> 8) & 0xff;
		else f->exitcode = __NT_SIGNAL_EXIT(status & 0x7f);   /* killed by a real host signal */
		return 1;
	}
	if (r < 0) return -1;
	return 0;
}

NTSTATUS NTAPI NtWaitForSingleObject(HANDLE h, BOOLEAN alertable, LARGE_INTEGER *timeout)
{
	struct ofile *f = of_get(h);
	int r;
	(void)alertable;
	if (!f) return STATUS_INVALID_HANDLE;
	if (f->kind != OF_PROC) {
		/* A file or pipe handle is signalled when its I/O completes, and
		 * every transfer here completes before it returns. */
		return STATUS_SUCCESS;
	}
	/* Only "wait forever" and "do not wait" are distinguished: a real
	 * timeout would need a timed wait4, which Linux has no equivalent of.
	 * src/process/wait.c uses exactly those two. */
	r = proc_poll(f, timeout && *timeout == 0);
	if (r > 0) return STATUS_WAIT_0;
	if (r == 0) return STATUS_TIMEOUT;
	return STATUS_INVALID_HANDLE;   /* not actually waitable from here; see proc_poll */
}

/* src/select/select.c is the only caller here, and only for its
 * console-wait path -- which nothing in this native ASan build can ever
 * reach: consoles aren't fuzzed/tested here (no OF_CONSOLE kind exists in
 * this shim), so this exists purely to satisfy the linker, not to be a
 * faithful WaitAny.  A real implementation would need a genuine
 * multi-object blocking wait, which this Linux-hosted shim has no
 * primitive for (same limitation NtWaitForSingleObject's own comment
 * above notes) -- so this only ever peeks each handle non-blockingly via
 * NtWaitForSingleObject(..., 0-timeout) and reports STATUS_TIMEOUT
 * instead of truly blocking when nothing is ready. */
NTSTATUS NTAPI NtWaitForMultipleObjects(ULONG count, HANDLE *handles, ULONG waittype, BOOLEAN alertable, LARGE_INTEGER *timeout)
{
	LARGE_INTEGER zero = 0;
	ULONG i;
	(void)waittype;
	for (i = 0; i < count; i++) {
		NTSTATUS st = NtWaitForSingleObject(handles[i], alertable, &zero);
		if (st == STATUS_WAIT_0) return (NTSTATUS)(STATUS_WAIT_0 + i);
	}
	(void)timeout;
	return STATUS_TIMEOUT;
}

NTSTATUS NTAPI NtQueryInformationProcess(HANDLE h, PROCESSINFOCLASS cls, PVOID buf,
                                         ULONG len, PULONG ret)
{
	struct ofile *f = h == NtCurrentProcess() ? 0 : of_get(h);

	if (h != NtCurrentProcess() && (!f || f->kind != OF_PROC)) return STATUS_INVALID_HANDLE;
	switch (cls) {
	case ProcessBasicInformation: {
		PROCESS_BASIC_INFORMATION *pbi = buf;
		if (len < sizeof *pbi) return STATUS_INFO_LENGTH_MISMATCH;
		memset(pbi, 0, sizeof *pbi);
		if (f) {
			proc_poll(f, 1);
			pbi->ExitStatus = f->exited ? (NTSTATUS)f->exitcode : STATUS_PENDING;
			pbi->UniqueProcessId = (ULONG_PTR)f->pid;
			pbi->InheritedFromUniqueProcessId = (ULONG_PTR)syscall(SYS_getpid);
		} else {
			pbi->ExitStatus = STATUS_PENDING;     /* still running */
			pbi->PebBaseAddress = &shim_peb;
			pbi->UniqueProcessId = (ULONG_PTR)syscall(SYS_getpid);
			pbi->InheritedFromUniqueProcessId = (ULONG_PTR)syscall(SYS_getppid);
		}
		if (ret) *ret = sizeof *pbi;
		return STATUS_SUCCESS;
	}
	case ProcessTimes: {
		KERNEL_USER_TIMES *kt = buf;
		struct { long sec, nsec; } ts = { 0, 0 };
		if (len < sizeof *kt) return STATUS_INFO_LENGTH_MISMATCH;
		if (f) return STATUS_NOT_IMPLEMENTED;   /* only this process's own times */
		memset(kt, 0, sizeof *kt);
		/* CLOCK_PROCESS_CPUTIME_ID is the sum of the two NT reports and
		 * cannot be split into kernel and user time, so it is all
		 * reported as user time; clock_gettime() adds them anyway. */
		syscall(SYS_clock_gettime, 2 /*CLOCK_PROCESS_CPUTIME_ID*/, &ts);
		kt->UserTime = (LARGE_INTEGER)ts.sec * 10000000LL + ts.nsec / 100;
		NtQuerySystemTime(&kt->CreateTime);
		if (ret) *ret = sizeof *kt;
		return STATUS_SUCCESS;
	}
	default:
		return STATUS_INVALID_INFO_CLASS;
	}
}

/* src/misc/resource.c's setpriority() sets its own (and, for a process
 * this library itself spawned, that child's) NT-visible priority class
 * via this, then checks the return -- unlike NtCreateJobObject/friends
 * above, a real STATUS_SUCCESS is needed here for the native asan build
 * of test/posix-sysmisc.c to see the same round trip 'make check' does
 * under Wine. Nothing is actually done with the class (this host process
 * really changing its own OS scheduling priority would be a surprising
 * side effect of running a test suite), which is fine: ntlibc's own
 * cached nice value, not a requery of this, is what getpriority() reads
 * back for the process's own priority (see include/sys/resource.h). */
NTSTATUS NTAPI NtSetInformationProcess(HANDLE h, PROCESSINFOCLASS cls, PVOID buf, ULONG len)
{
	(void)h;
	switch (cls) {
	case ProcessPriorityClass:
		if (len != sizeof(PROCESS_PRIORITY_CLASS)) return STATUS_INFO_LENGTH_MISMATCH;
		return STATUS_SUCCESS;
	default:
		return STATUS_NOT_IMPLEMENTED;
	}
}

NTSTATUS NTAPI NtTerminateProcess(HANDLE h, NTSTATUS code)
{
	struct ofile *f = h && h != NtCurrentProcess() ? of_get(h) : 0;
	if (f && f->kind == OF_PROC) {
		/* Record the code *this process* asked to end f->pid with before
		 * actually killing it: proc_poll()'s reaper wants the real code
		 * (e.g. kill()'s __NT_SIGNAL_EXIT(sig)), not just "SIGKILL",
		 * which is the only signal a raw host kill can reliably promise
		 * delivery of.  See xstatus_record()'s comment. */
		xstatus_record(f->pid, (int)code);
		if (syscall(SYS_kill, (long)f->pid, 9 /*SIGKILL*/) < 0) return STATUS_ACCESS_DENIED;
		return STATUS_SUCCESS;
	}
	xstatus_record((int)syscall(SYS_getpid), (int)code);
	vfs_snapshot_sync();
	syscall(SYS_exit_group, (long)(int)code);
	return STATUS_SUCCESS;
}

/*
 * RtlCloneUserProcess -- src/process/fork.c's one real dependency, and so
 * fork()'s.
 *
 * Unlike RtlCreateUserProcess above, there is no second image to start:
 * the whole point of a clone is that the child resumes *this* call, with
 * a copy of this process's own address space.  A host fork(2) is exactly
 * that primitive, so it is used directly rather than reconstructed from
 * pieces the way RtlCreateUserProcess reconstructs "start a new image"
 * from fork+execve.  Every simulated NT object this file keeps -- vroot,
 * vcwd, vpipes, vhandles, the ofile structs themselves -- is ordinary
 * process memory, so the host's fork() carries all of it into the child
 * automatically, the same "it's just memory" property fork.c's own
 * header comment claims for __fds and the ntlibc heap.
 *
 * What real NT's INHERIT_HANDLES flag would selectively copy, plain
 * fork() copies unconditionally -- there is no per-handle OBJ_INHERIT
 * bookkeeping in this file's object table for it to consult even if it
 * wanted to be selective (NtDuplicateObject's attrs argument is already
 * ignored, above). That is not a gap that matters here: it makes every
 * handle "inherit", which is a superset of real NT's behaviour, and the
 * one place a test distinguishes the two -- a process handle for a
 * *third* process that predates the fork, reaching the clone only as a
 * plain-memory table entry (test/fork-handles-win.c, test/process-win.c's
 * test_prefork_handle) -- still gets a real, principled failure: the
 * clone is the host's *sibling* of that third process, not its parent,
 * so proc_poll's wait4() on it fails on its own, honestly, without any
 * handle-table trickery needed to produce that outcome.
 *
 * RTL_CLONE_PROCESS_FLAGS_CREATE_SUSPENDED asks for the new thread to
 * start suspended, to be released by a later NtResumeThread.  A host
 * fork() child starts running immediately, at the same point in this
 * very function -- but that is fine: nothing between here and fork.c's
 * NtResumeThread(info.Thread, 0) call runs in the child at all (the
 * child returns STATUS_PROCESS_CLONED right below and takes the
 * `if (st == STATUS_PROCESS_CLONED)` branch, which never touches the
 * thread handle), and NtResumeThread on this file's OF_PROC objects is
 * already a documented no-op for the same reason RtlCreateUserProcess's
 * fork+execve needs it to be one.
 */
NTSTATUS NTAPI RtlCloneUserProcess(ULONG flags, PVOID psd, PVOID tsd, HANDLE debug,
                                   RTL_USER_PROCESS_INFORMATION *info)
{
	struct ofile *f;
	NTSTATUS st;
	long pid;
	(void)flags; (void)psd; (void)tsd; (void)debug;

	if (!info) return STATUS_INVALID_PARAMETER;
	memset(info, 0, sizeof *info);

	pid = syscall(SYS_fork);
	if (pid < 0) return STATUS_INSUFFICIENT_RESOURCES;
	if (pid == 0) {
		/* The child: this call is returning for the second time, in a
		 * process that is a copy of the caller's.  info is never filled
		 * in on this path on real NT either -- the child tells itself
		 * apart from the parent by this return value alone.
		 *
		 * On real NT the kernel gives the clone a genuinely new PID/TID
		 * and the TEB the child wakes up in already reflects that --
		 * nothing in fork() itself has to ask for it.  A host fork(2)
		 * child instead wakes up inside a byte-for-byte copy of the
		 * parent's address space, __ntshim_init()'s shim_teb included,
		 * so getpid()/gettid() (which just read TEB.ClientId, same as
		 * the real ones) would otherwise keep reporting the *parent's*
		 * ids forever.  __ntshim_init() itself cannot fix this: it is a
		 * constructor, and constructors do not run again across a raw
		 * fork(2).  So this is done here instead, in the one place a
		 * clone's identity actually changes. */
		{
			PTEB tb = (PTEB)shim_teb;
			long me = syscall(SYS_getpid);
			tb->ClientId.UniqueProcess = (HANDLE)me;
			tb->ClientId.UniqueThread = (HANDLE)me;
		}
		return STATUS_PROCESS_CLONED;
	}

	/* The parent: track the new process exactly like a spawned one, so
	 * waitpid() on it goes through the same NtWaitForSingleObject /
	 * NtQueryInformationProcess path as every other child. */
	f = vmalloc(sizeof *f);
	if (!f) return STATUS_NO_MEMORY;
	memset(f, 0, sizeof *f);
	f->kind = OF_PROC;
	f->exitcode = STATUS_PENDING;
	f->pid = (int)pid;
	f->snapshot_fd = -1;

	st = of_install(f, &info->Process);
	if (!NT_SUCCESS(st)) { vfree(f); return st; }
	/* One thread, and it is the process; a second reference to the same
	 * object, same as RtlCreateUserProcess above. */
	st = of_install(f, &info->Thread);
	if (!NT_SUCCESS(st)) { NtClose(info->Process); return st; }
	info->ClientId.UniqueProcess = (HANDLE)(LONG_PTR)pid;
	info->ClientId.UniqueThread = (HANDLE)(LONG_PTR)pid;
	return STATUS_SUCCESS;
}

/* --------------------------------------------------------------- strings */

void NTAPI RtlInitUnicodeString(PUNICODE_STRING s, PCWSTR src)
{
	size_t n = 0;
	if (src) while (src[n]) n++;
	s->Length = (USHORT)(n * sizeof(WCHAR));
	s->MaximumLength = (USHORT)((n + 1) * sizeof(WCHAR));
	s->Buffer = (PWSTR)src;
}

ULONG NTAPI RtlNtStatusToDosError(NTSTATUS st) { return (ULONG)st & 0xffff; }

/*
 * RtlUTF8ToUnicodeN / RtlUnicodeToUTF8N.
 *
 * These two are ntdll's, not ntlibc's -- src/internal/utf.c is a wrapper
 * around them.  Written from the documented behaviour (malformed input is
 * replaced with U+FFFD and reported as STATUS_SOME_NOT_MAPPED; a short
 * destination is filled as far as it goes and reported as
 * STATUS_BUFFER_TOO_SMALL), so that what the utf harness actually
 * exercises is utf.c's *buffer sizing*: the "UTF-16 is never longer in
 * code units than UTF-8 is in bytes" and "at most 3 bytes per code unit"
 * claims it allocates on.  It does not exercise ntdll's converter, and is
 * not evidence about it.
 */
#define REPL 0xFFFDu

NTSTATUS NTAPI RtlUTF8ToUnicodeN(PWSTR dst, ULONG dstbytes, PULONG written,
                                 const char *src, ULONG srcbytes)
{
	const unsigned char *s = (const unsigned char *)src, *end = s + srcbytes;
	ULONG out = 0;
	int lossy = 0, full = 0;

	if (!src) return STATUS_INVALID_PARAMETER;
	while (s < end) {
		unsigned int cp;
		int extra, i;
		unsigned char c = *s++;

		if (c < 0x80)                { cp = c; extra = 0; }
		else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; extra = 1; }
		else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; extra = 2; }
		else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; extra = 3; }
		else                         { cp = REPL; extra = 0; lossy = 1; }

		for (i = 0; i < extra; i++) {
			if (s >= end || (*s & 0xc0) != 0x80) { cp = REPL; extra = -1; lossy = 1; break; }
			cp = (cp << 6) | (*s++ & 0x3f);
		}
		if (extra > 0) {
			/* overlong, surrogate, or out of range */
			if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
			    (extra == 3 && cp < 0x10000) || cp > 0x10FFFF ||
			    (cp >= 0xD800 && cp <= 0xDFFF)) { cp = REPL; lossy = 1; }
		}

		if (cp >= 0x10000) {
			if (dst && out + 2 * sizeof(WCHAR) > dstbytes) { full = 1; break; }
			if (dst) {
				dst[out / sizeof(WCHAR)]     = (WCHAR)(0xD800 + ((cp - 0x10000) >> 10));
				dst[out / sizeof(WCHAR) + 1] = (WCHAR)(0xDC00 + ((cp - 0x10000) & 0x3ff));
			}
			out += 2 * sizeof(WCHAR);
		} else {
			if (dst && out + sizeof(WCHAR) > dstbytes) { full = 1; break; }
			if (dst) dst[out / sizeof(WCHAR)] = (WCHAR)cp;
			out += sizeof(WCHAR);
		}
	}
	if (written) *written = out;
	if (full) return STATUS_BUFFER_TOO_SMALL;
	return lossy ? STATUS_SOME_NOT_MAPPED : STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlUnicodeToUTF8N(char *dst, ULONG dstbytes, PULONG written,
                                 PCWSTR src, ULONG srcbytes)
{
	ULONG i, n = srcbytes / sizeof(WCHAR), out = 0;
	int lossy = 0, full = 0;

	if (!src) return STATUS_INVALID_PARAMETER;
	if (srcbytes % sizeof(WCHAR)) return STATUS_INVALID_PARAMETER;
	for (i = 0; i < n; i++) {
		unsigned int cp = src[i];
		int len;
		unsigned char buf[4];

		if (cp >= 0xD800 && cp <= 0xDBFF) {
			if (i + 1 < n && src[i+1] >= 0xDC00 && src[i+1] <= 0xDFFF)
				cp = 0x10000 + ((cp - 0xD800) << 10) + (src[++i] - 0xDC00);
			else { cp = REPL; lossy = 1; }
		} else if (cp >= 0xDC00 && cp <= 0xDFFF) {
			cp = REPL; lossy = 1;
		}

		if (cp < 0x80)          { len = 1; buf[0] = (unsigned char)cp; }
		else if (cp < 0x800)    { len = 2; buf[0] = (unsigned char)(0xc0 | (cp >> 6));
		                                    buf[1] = (unsigned char)(0x80 | (cp & 0x3f)); }
		else if (cp < 0x10000)  { len = 3; buf[0] = (unsigned char)(0xe0 | (cp >> 12));
		                                    buf[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
		                                    buf[2] = (unsigned char)(0x80 | (cp & 0x3f)); }
		else                    { len = 4; buf[0] = (unsigned char)(0xf0 | (cp >> 18));
		                                    buf[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
		                                    buf[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
		                                    buf[3] = (unsigned char)(0x80 | (cp & 0x3f)); }

		if (dst && out + (ULONG)len > dstbytes) { full = 1; break; }
		if (dst) memcpy(dst + out, buf, (size_t)len);
		out += (ULONG)len;
	}
	if (written) *written = out;
	if (full) return STATUS_BUFFER_TOO_SMALL;
	return lossy ? STATUS_SOME_NOT_MAPPED : STATUS_SUCCESS;
}

/*
 * libFuzzer is built with _FORTIFY_SOURCE, so its Printf() calls
 * __vfprintf_chk rather than vfprintf.  Its FILE* is ntlibc's stderr (the
 * only stderr in this executable), but __vfprintf_chk would come from
 * glibc and would read that pointer as a glibc FILE -- so every diagnostic
 * libFuzzer prints, and every crash artefact it announces, silently
 * vanished.  Routing the checked forms back to ntlibc's own stdio is what
 * makes the fuzzer able to talk.
 */
int __vfprintf_chk(FILE *f, int flag, const char *fmt, __builtin_va_list ap)
{
	(void)flag;
	return vfprintf(f, fmt, ap);
}

int __fprintf_chk(FILE *f, int flag, const char *fmt, ...)
{
	__builtin_va_list ap;
	int r;
	(void)flag;
	__builtin_va_start(ap, fmt);
	r = vfprintf(f, fmt, ap);
	__builtin_va_end(ap);
	return r;
}

int __printf_chk(int flag, const char *fmt, ...)
{
	__builtin_va_list ap;
	int r;
	(void)flag;
	__builtin_va_start(ap, fmt);
	r = vfprintf(stdout, fmt, ap);
	__builtin_va_end(ap);
	return r;
}

int __snprintf_chk(char *b, size_t n, int flag, size_t slen, const char *fmt, ...)
{
	__builtin_va_list ap;
	int r;
	(void)flag; (void)slen;
	__builtin_va_start(ap, fmt);
	r = vsnprintf(b, n, fmt, ap);
	__builtin_va_end(ap);
	return r;
}

void *__memcpy_chk(void *d, const void *s, size_t n, size_t dlen)
{
	(void)dlen;
	return memcpy(d, s, n);
}

/* ------------------------------------------------- everything not native */

#define NOTIMPL(name, proto) NTSTATUS NTAPI name proto { return STATUS_NOT_IMPLEMENTED; }

NOTIMPL(NtFsControlFile, (HANDLE a, HANDLE b, PIO_APC_ROUTINE c, PVOID d, PIO_STATUS_BLOCK e,
                          ULONG f, PVOID g, ULONG h, PVOID i, ULONG j))
/* src/socket/afdsupport.c's __afd_ioctl(): every AFD request (bind,
 * listen, connect, accept, send, recv, select/poll, disconnect) goes
 * through this.  No simulated \Device\Afd exists over this file's
 * in-memory volume -- modelling a real TCP/IP stack is out of scope for
 * this stub file -- so it always refuses, the same as NtFsControlFile
 * above.  __afd_open() itself (NtCreateFile against \Device\Afd\Endpoint)
 * still goes through the ordinary NtCreateFile() stub below and is not
 * refused by this; only the ioctls past that point are.  This makes
 * every AFD call fail the same honest way test/posix-socket.c's own
 * runtime capability probe already handles for Wine (see that file's
 * banner) -- bind() fails, the probe prints one SKIP line, and the
 * network-dependent assertions are skipped rather than run against a
 * socket that was never really wired up.  Needed only so the link
 * succeeds; src/socket/*.c itself is fully exercised under `make check`
 * against real ntdll/Wine. */
NOTIMPL(NtDeviceIoControlFile, (HANDLE a, HANDLE b, PIO_APC_ROUTINE c, PVOID d, PIO_STATUS_BLOCK e,
                                ULONG f, PVOID g, ULONG h, PVOID i, ULONG j))
NOTIMPL(NtOpenProcess, (PHANDLE a, ACCESS_MASK b, POBJECT_ATTRIBUTES c, PCLIENT_ID d))
NOTIMPL(NtOpenSymbolicLinkObject, (PHANDLE a, ACCESS_MASK b, POBJECT_ATTRIBUTES c))
NOTIMPL(NtQuerySymbolicLinkObject, (HANDLE a, PUNICODE_STRING b, PULONG c))
NOTIMPL(NtQuerySystemInformation, (SYSTEM_INFORMATION_CLASS a, PVOID b, ULONG c, PULONG d))
NOTIMPL(NtSetSystemTime, (LARGE_INTEGER *a, LARGE_INTEGER *b))
/* src/misc/resource.c's setrlimit(): the job-object route it takes for
 * RLIMIT_NPROC/CPU/AS/DATA is best-effort (its own soft/hard state is
 * what getrlimit() actually reads back), so a real host process never
 * needing job objects at all is fine leaving these NOTIMPL. */
NOTIMPL(NtCreateJobObject, (PHANDLE a, ACCESS_MASK b, POBJECT_ATTRIBUTES c))
NOTIMPL(NtAssignProcessToJobObject, (HANDLE a, HANDLE b))
NOTIMPL(NtSetInformationJobObject, (HANDLE a, JOBOBJECTINFOCLASS b, PVOID c, ULONG d))
/* src/signal/signal.c's segv_code() calls this to tell SEGV_MAPERR from
 * SEGV_ACCERR, but only from inside exception_handler(), which this
 * file's RtlAddVectoredExceptionHandler() (below) never actually
 * invokes -- a native build has no real NT exception to forward (see
 * this file's header comment and test/posix-signal.c's
 * NATIVE_NO_FAULT_BRIDGE). Needed only so the link succeeds. */
NOTIMPL(NtQueryVirtualMemory, (HANDLE a, PVOID b, MEMORY_INFORMATION_CLASS c, PVOID d, SIZE_T e, SIZE_T *f))

/* ---- virtual memory, backed by the host's real mappings ----------
 *
 * src/mman/mman.c is the first thing in this library to use the
 * NtAllocateVirtualMemory/NtFreeVirtualMemory/NtProtectVirtualMemory
 * family, and it links into libc.a, so without these four every test in
 * the native build fails to link -- which is how they got here.
 *
 * They are FUNCTIONAL rather than NOTIMPL, deliberately.  A NOTIMPL
 * would have fixed the link and left src/mman/mman.c with zero ASan
 * coverage, and the risky part of that file is not the NT calls -- it is
 * the per-page `live` bookkeeping around them, an array index derived
 * from a pointer difference, which is exactly the sort of thing ASan
 * exists to catch and exactly the sort of thing a NOTIMPL stub would
 * hide by never letting the code run.
 *
 * This is not modelling NT's allocator so that a test can assert against
 * the model -- the objection recorded for spawn-stdhandle-attr above.
 * The subject under test is mman.c's bookkeeping; the host's mmap(2) is
 * standing in for the *substrate*, the same way this file's NtCreateFile
 * stands in for the volume.  Where the substrate genuinely differs from
 * NT the difference is noted below rather than papered over.
 *
 * The mapping onto host primitives:
 *
 *   MEM_RESERVE|MEM_COMMIT, base NULL -> mmap(NULL, ..., MAP_ANONYMOUS)
 *   MEM_COMMIT, base inside a reservation -> mprotect() to the requested
 *     protection.  NT would zero a freshly committed page; the host has
 *     already done that in the MEM_DECOMMIT below, which is why decommit
 *     is implemented as a fresh anonymous MAP_FIXED rather than as
 *     mprotect(PROT_NONE) -- MAP_FIXED over an anonymous range discards
 *     the old pages and the next read sees zeroes, which is the NT
 *     behaviour mman.c's MAP_FIXED discard clause depends on.
 *   MEM_DECOMMIT -> mmap(MAP_FIXED|MAP_ANONYMOUS, PROT_NONE)
 *   MEM_RELEASE  -> munmap() of the whole reservation.  NT takes base
 *     with size 0 for this, so the size has to come from somewhere: the
 *     small table below remembers what this stub handed out.
 */
/* Raw syscalls, not the <sys/mman.h> wrappers.  Two reasons, and both
 * are the point rather than an inconvenience:
 *
 *   - This file is compiled -nostdinc against ntlibc's own headers, so
 *     <sys/mman.h> here is include/sys/mman.h and mmap() here would be
 *     src/mman/mman.c's mmap() -- i.e. the code under test calling
 *     itself through its own substrate.  Unbounded recursion, and a
 *     measurement of nothing.
 *   - The asan build passes only -D_XOPEN_SOURCE=700, so MAP_ANONYMOUS
 *     is not even visible here.  That is the _BSD_SOURCE/_GNU_SOURCE
 *     gate in include/sys/mman.h doing exactly its job, demonstrated by
 *     accident: a strictly-POSIX translation unit does not see the
 *     extension.
 *
 * So the constants below are the host kernel's own, spelled out the way
 * this file already spells AT_FDCWD as -100. */
#define SYS_mprotect 10
#define SYS_munmap   11
#define SYS_mlock    149
#define SYS_munlock  150

#define H_PROT_NONE  0x0
#define H_PROT_READ  0x1
#define H_PROT_WRITE 0x2
#define H_PROT_EXEC  0x4
#define H_MAP_PRIVATE   0x02
#define H_MAP_FIXED     0x10
#define H_MAP_ANONYMOUS 0x20

#define NTSTUB_VM_MAX 256
static struct { void *base; size_t size; } ntstub_vm[NTSTUB_VM_MAX];

/* Tracked file-backed section views (NtMapViewOfSection, below), needed
 * here too: NtFreeVirtualMemory's MEM_DECOMMIT has to refuse a range
 * that belongs to one, matching real NT (a section view is not memory
 * NtFreeVirtualMemory owns -- see include/sys/mman.h's banner and
 * src/mman/mman.c's mmap()). Declared this early only for that check;
 * NtCreateSection()/NtMapViewOfSection()/NtUnmapViewOfSection()
 * themselves are defined together, further down. */
#define NTSTUB_VIEW_MAX 64
struct ntstub_view { void *base; size_t size; struct vnode *v; long long off; int writable_shared; };
static struct ntstub_view ntstub_views[NTSTUB_VIEW_MAX];

/* Whether [base, base+size) overlaps any tracked section view. */
static int ntstub_view_overlaps(void *base, size_t size)
{
	int i;
	unsigned char *lo = base, *hi = lo + size;
	for (i = 0; i < NTSTUB_VIEW_MAX; i++) {
		unsigned char *vlo, *vhi;
		if (!ntstub_views[i].base) continue;
		vlo = ntstub_views[i].base;
		vhi = vlo + ntstub_views[i].size;
		if (lo < vhi && vlo < hi) return 1;
	}
	return 0;
}

static long ntstub_prot(ULONG page)
{
	switch (page) {
	case PAGE_NOACCESS:          return H_PROT_NONE;
	case PAGE_READONLY:          return H_PROT_READ;
	case PAGE_READWRITE:         return H_PROT_READ | H_PROT_WRITE;
	case PAGE_EXECUTE:           return H_PROT_EXEC;
	case PAGE_EXECUTE_READ:      return H_PROT_READ | H_PROT_EXEC;
	case PAGE_EXECUTE_READWRITE: return H_PROT_READ | H_PROT_WRITE | H_PROT_EXEC;
	default:                     return H_PROT_READ | H_PROT_WRITE;
	}
}

/* mmap(2) reports failure as a small negative errno in the return value,
 * not as MAP_FAILED, when it is reached through syscall(2) this way. */
static int ntstub_mmap_failed(long r) { return r < 0 && r > -4096; }

NTSTATUS NTAPI NtAllocateVirtualMemory(HANDLE proc, PVOID *base, ULONG_PTR zb,
                                       SIZE_T *size, ULONG type, ULONG prot)
{
	int i;
	long r;
	(void)proc; (void)zb;
	if (!base || !size || !*size) return STATUS_INVALID_PARAMETER;

	if (type & MEM_RESERVE) {
		r = syscall(SYS_mmap, 0, (long)*size, ntstub_prot(prot),
		            (long)(H_MAP_PRIVATE | H_MAP_ANONYMOUS), -1L, 0L);
		if (ntstub_mmap_failed(r)) return STATUS_NO_MEMORY;
		for (i = 0; i < NTSTUB_VM_MAX; i++) {
			if (ntstub_vm[i].base) continue;
			ntstub_vm[i].base = (void *)r;
			ntstub_vm[i].size = *size;
			*base = (void *)r;
			return STATUS_SUCCESS;
		}
		syscall(SYS_munmap, r, (long)*size);
		return STATUS_NO_MEMORY;
	}

	/* MEM_COMMIT over a subrange of an existing reservation. */
	if (!*base) return STATUS_INVALID_PARAMETER;
	if (syscall(SYS_mprotect, (long)*base, (long)*size, ntstub_prot(prot)) != 0)
		return STATUS_NO_MEMORY;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtFreeVirtualMemory(HANDLE proc, PVOID *base, SIZE_T *size, ULONG type)
{
	int i;
	long r;
	(void)proc;
	if (!base || !*base || !size) return STATUS_INVALID_PARAMETER;

	if (type & MEM_RELEASE) {
		/* NT takes base with size 0 here, so the size has to come from
		 * somewhere: this table remembers what the stub handed out. */
		for (i = 0; i < NTSTUB_VM_MAX; i++) {
			if (ntstub_vm[i].base != *base) continue;
			syscall(SYS_munmap, (long)ntstub_vm[i].base, (long)ntstub_vm[i].size);
			ntstub_vm[i].base = NULL;
			ntstub_vm[i].size = 0;
			return STATUS_SUCCESS;
		}
		return STATUS_INVALID_PARAMETER;
	}

	/* MEM_DECOMMIT: a fresh anonymous MAP_FIXED over the subrange rather
	 * than mprotect(PROT_NONE).  Both make the pages inaccessible, but
	 * only this one discards the old pages so that a later commit reads
	 * as zero -- which is the NT behaviour src/mman/mman.c's MAP_FIXED
	 * discard clause depends on.  mprotect alone would leave the old
	 * bytes in place and quietly make that clause untestable here. */
	if (!*size) return STATUS_INVALID_PARAMETER;
	/* Refuse a range that belongs to a file-backed section view, the
	 * same way real NT does (include/sys/mman.h's banner): a view is
	 * placed and removed as a WHOLE, and there is no NT primitive for
	 * decommitting part of one.  src/mman/mman.c's munmap() calls this
	 * unconditionally on every mapping, including file-backed ones, and
	 * never checks the status -- it relies on this failing harmlessly.
	 * Measured the hard way: without this check, this stub's own
	 * MEM_DECOMMIT genuinely PROT_NONEs the view (a real anonymous
	 * mapping, underneath), and src/mman/mman.c's own subsequent
	 * NtUnmapViewOfSection() -- which writes the view's now-PROT_NONE
	 * bytes back to the vnode for a dirty MAP_SHARED mapping -- then
	 * SEGVs reading them.
	 *
	 * The exact failure status is not load-bearing -- src/mman/mman.c
	 * never inspects it (see above) -- so this reuses
	 * STATUS_INVALID_PARAMETER, already declared and already this
	 * function's answer for the sibling guard clauses just above,
	 * rather than adding a made-up NTSTATUS value this file cannot
	 * verify against real NT. */
	if (ntstub_view_overlaps(*base, (size_t)*size)) return STATUS_INVALID_PARAMETER;
	r = syscall(SYS_mmap, (long)*base, (long)*size, H_PROT_NONE,
	            (long)(H_MAP_PRIVATE | H_MAP_ANONYMOUS | H_MAP_FIXED), -1L, 0L);
	if (ntstub_mmap_failed(r)) return STATUS_INVALID_PARAMETER;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtProtectVirtualMemory(HANDLE proc, PVOID *base, SIZE_T *size,
                                      ULONG prot, PULONG old)
{
	(void)proc;
	if (!base || !*base || !size) return STATUS_INVALID_PARAMETER;
	if (old) *old = PAGE_READWRITE;   /* not tracked; nothing reads it back */
	if (syscall(SYS_mprotect, (long)*base, (long)*size, ntstub_prot(prot)) != 0)
		return STATUS_INVALID_PARAMETER;
	return STATUS_SUCCESS;
}

/* mlock(2)/munlock(2) -- which is what Wine's NtLockVirtualMemory does
 * for the current process too (dlls/ntdll/unix/virtual.c:6254).  Bounded
 * by RLIMIT_MEMLOCK here exactly as it is there, so test/posix-mman.c's
 * measured skip path is reachable from this build as well.  That is the
 * point of keying that skip on the attempt rather than on the platform:
 * it works unchanged across Wine, real NT, and this native build. */
NTSTATUS NTAPI NtLockVirtualMemory(HANDLE proc, PVOID *base, SIZE_T *size, ULONG unk)
{
	(void)proc; (void)unk;
	if (!base || !*base || !size) return STATUS_INVALID_PARAMETER;
	if (syscall(SYS_mlock, (long)*base, (long)*size) != 0) return STATUS_ACCESS_DENIED;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtUnlockVirtualMemory(HANDLE proc, PVOID *base, SIZE_T *size, ULONG unk)
{
	(void)proc; (void)unk;
	if (!base || !*base || !size) return STATUS_INVALID_PARAMETER;
	if (syscall(SYS_munlock, (long)*base, (long)*size) != 0) return STATUS_ACCESS_DENIED;
	return STATUS_SUCCESS;
}

/* ---- file-backed sections (src/mman/mman.c's map_file(), Pass 2) --
 *
 * NtCreateSection()/NtMapViewOfSection()/NtUnmapViewOfSection() cannot
 * be hand off to the host's mmap(fd, ...) the way the anonymous-VM
 * stubs above hand MEM_RESERVE straight to mmap(MAP_ANONYMOUS): an
 * OF_VFS handle here has no real host file descriptor behind it at all
 * -- "files" are the in-memory vnode tree described above NtCreateFile,
 * not real files on disk (this file's header banner).
 *
 * What IS available, and genuine rather than invented: the vnode's own
 * `data`/`size`, directly readable in this same process.  So a view is
 * real host anonymous memory (same substrate, same ASan coverage as the
 * anon-VM stubs), seeded by copying the vnode's bytes in at map time
 * (so a caller reading a file-backed mapping sees the file's actual
 * contents, not garbage or zero) and, for a writable MAP_SHARED view,
 * copied back into the vnode at unmap time (so mmap/write/munmap/
 * reopen/mmap -- the shape test/posix-mman.c's own test_mmap_file_backed
 * checks -- sees the write persist).  This is real memory with real
 * per-page bookkeeping under ASan, which is the point (see the anon-VM
 * stubs' own banner for why that outranks a NOTIMPL here too); the
 * places it is NOT faithful to NT, spelled out rather than papered
 * over:
 *
 *   - No cross-process sharing.  A second, independent view of the same
 *     section (this stub's native build never forks two live processes
 *     sharing a section handle the way real NT would) does not observe
 *     a first view's writes.  Nothing measured against this library
 *     opens a section from two processes.
 *   - No file-access-mode enforcement.  A real, working section on a
 *     read-only-opened file rejects a writable request; this stub does
 *     not model that, because src/mman/mman.c's own O_ACCMODE check
 *     (mmap(), before map_file() is ever called) already rejects the
 *     one case that matters to a caller -- MAP_SHARED+PROT_WRITE on a
 *     read-only descriptor -- so no code path here ever asks this stub
 *     to enforce it.
 *   - Growing the file past its current vnode capacity on a write past
 *     EOF is declined (see NtUnmapViewOfSection below) rather than
 *     guessed at: this stub has no ftruncate()-equivalent write path of
 *     its own to invent, and doing so quietly would be exactly the kind
 *     of plausible-but-wrong host-specific behaviour this file's header
 *     warns against.
 */
#define NTSTUB_SECTION_MAX 64
struct ntstub_section { struct vnode *v; long long size; };
static struct ntstub_section ntstub_sections[NTSTUB_SECTION_MAX];

/* ntstub_views[]/ntstub_view_overlaps() are declared earlier, alongside
 * ntstub_vm[] -- NtFreeVirtualMemory needs them too. See there. */

/* Section handles live in their own tagged range, well clear of
 * vhandles[]'s 1..VFS_HANDLES so of_get() can never alias one. */
#define NTSTUB_SECTION_BASE 0x40000000L

NTSTATUS NTAPI NtCreateSection(PHANDLE out, ACCESS_MASK access, POBJECT_ATTRIBUTES oa,
                               LARGE_INTEGER *maxsize, ULONG prot, ULONG alloc, HANDLE file)
{
	struct ofile *f;
	int i;
	(void)access; (void)oa; (void)prot; (void)alloc;
	if (!out || !file) return STATUS_INVALID_PARAMETER;
	f = of_get(file);
	if (!f || f->kind != OF_VFS) return STATUS_INVALID_HANDLE;
	for (i = 0; i < NTSTUB_SECTION_MAX; i++) {
		if (ntstub_sections[i].v) continue;
		ntstub_sections[i].v = f->node;
		ntstub_sections[i].size = maxsize ? *maxsize : f->node->size;
		*out = (HANDLE)(NTSTUB_SECTION_BASE + i);
		return STATUS_SUCCESS;
	}
	return STATUS_TOO_MANY_OPENED_FILES;
}

static struct ntstub_section *ntstub_section_get(HANDLE h)
{
	long i = (long)h - NTSTUB_SECTION_BASE;
	if (i < 0 || i >= NTSTUB_SECTION_MAX || !ntstub_sections[i].v) return 0;
	return &ntstub_sections[i];
}

NTSTATUS NTAPI NtMapViewOfSection(HANDLE section, HANDLE proc, PVOID *base, ULONG_PTR zb,
                                  SIZE_T commit, LARGE_INTEGER *secoff, SIZE_T *viewsize,
                                  SECTION_INHERIT inherit, ULONG alloctype, ULONG win32prot)
{
	struct ntstub_section *s = ntstub_section_get(section);
	long long off = secoff ? *secoff : 0;
	size_t want, avail, page = 4096, rounded;
	long r;
	int i;
	(void)proc; (void)zb; (void)commit; (void)inherit; (void)alloctype;
	if (!s || !base || !viewsize) return STATUS_INVALID_PARAMETER;
	if (off < 0 || off > s->size) return STATUS_INVALID_PARAMETER;
	avail = (size_t)(s->size - off);
	want = *viewsize ? *viewsize : avail;
	if (want > avail) want = avail;
	rounded = (want + page - 1) & ~(page - 1);
	if (!rounded) rounded = page;

	r = syscall(SYS_mmap, (long)*base, (long)rounded, H_PROT_READ | H_PROT_WRITE,
	            (long)(H_MAP_PRIVATE | H_MAP_ANONYMOUS | (*base ? H_MAP_FIXED : 0)), -1L, 0L);
	if (ntstub_mmap_failed(r)) return STATUS_NO_MEMORY;

	if (want && s->v->data) {
		long long n = (long long)want;
		if (off + n > s->v->size) n = s->v->size - off;
		if (n > 0) memcpy((void *)r, s->v->data + off, (size_t)n);
	}

	for (i = 0; i < NTSTUB_VIEW_MAX; i++) {
		if (ntstub_views[i].base) continue;
		ntstub_views[i].base = (void *)r;
		ntstub_views[i].size = rounded;
		ntstub_views[i].v = s->v;
		ntstub_views[i].off = off;
		/* Only these two Win32Protect values name a genuine MAP_SHARED
		 * writer (src/mman/mman.c's prot_to_view()); PAGE_WRITECOPY /
		 * PAGE_EXECUTE_WRITECOPY are MAP_PRIVATE, whose writes
		 * mmap.html requires never reach the object. */
		ntstub_views[i].writable_shared =
			(win32prot == PAGE_READWRITE || win32prot == PAGE_EXECUTE_READWRITE);
		break;
	}
	*base = (void *)r;
	*viewsize = rounded;
	return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtUnmapViewOfSection(HANDLE proc, PVOID base)
{
	int i;
	(void)proc;
	for (i = 0; i < NTSTUB_VIEW_MAX; i++) {
		if (ntstub_views[i].base != base) continue;
		if (ntstub_views[i].writable_shared && ntstub_views[i].v->data) {
			long long n = (long long)ntstub_views[i].size;
			if (ntstub_views[i].off + n > ntstub_views[i].v->size)
				n = ntstub_views[i].v->size - ntstub_views[i].off;
			if (n > 0)
				memcpy(ntstub_views[i].v->data + ntstub_views[i].off,
				       ntstub_views[i].base, (size_t)n);
		}
		syscall(SYS_munmap, (long)ntstub_views[i].base, (long)ntstub_views[i].size);
		ntstub_views[i].base = 0;
		return STATUS_SUCCESS;
	}
	return STATUS_INVALID_PARAMETER;
}

/* src/file/flock.c's NtLockFile()/NtUnlockFile() pair: no simulated
 * byte-range lock table exists over the in-memory volume this file's
 * NtCreateFile() stub above serves, so there is nothing genuine to
 * grant or release here. flock()'s own tracking (lockstate[] there)
 * never calls NtUnlockFile() unless an earlier NtLockFile() reported
 * success, so a lock request that always refuses leaves that side
 * consistent too -- test/posix-termios.c's flock() coverage already
 * tolerates a failing lock/unlock as a note rather than a hard
 * assertion for exactly this kind of environment gap (see that file
 * and src/file/flock.c's own banners). */
NOTIMPL(NtLockFile, (HANDLE a, HANDLE b, PIO_APC_ROUTINE c, PVOID d, PIO_STATUS_BLOCK e,
                     LARGE_INTEGER *f, LARGE_INTEGER *g, PULONG h, BOOLEAN i, BOOLEAN j))
NOTIMPL(NtUnlockFile, (HANDLE a, PIO_STATUS_BLOCK b, LARGE_INTEGER *c, LARGE_INTEGER *d, PULONG e))

PVOID NTAPI RtlAddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER h)
{
	(void)first; (void)h;
	return (PVOID)(long)1;
}

/* ------------------------------------------------- the struct stat seam
 *
 * Only compiled into the libFuzzer harnesses (fuzz/Makefile renames
 * `stat` to __real_stat in the library objects with objcopy, and this
 * file supplies the host-layout `stat` those objects no longer answer
 * to); tools/asan-build.sh's test binaries do not use it, and see
 * ntlibc's stat() unchanged.  fuzz/statshim.h has the whole story and
 * the measured offsets.
 */
#ifdef NTLIBC_FUZZ_STATWRAP
#include <sys/stat.h>
#include "statshim.h"

int __real_stat(const char *path, struct stat *st);

/* This IS `stat` in the harness link, and __real_stat is ntlibc's own.
 *
 * fuzz/Makefile renames `stat` to __real_stat throughout the library
 * objects with objcopy, definition and internal references together, so
 * ntlibc's own callers -- src/glob/glob.c, src/ftw/ftw.c,
 * src/stdlib/mktemp.c -- reach ntlibc's stat() with ntlibc's struct
 * stat, and nothing can come between them.  What is left holding the
 * name `stat` is this function, which answers in the *host's* layout,
 * and the only caller that can still reach it is the compiler runtime:
 * libFuzzer, compiled long ago against the host headers, calling stat()
 * to decide whether its corpus path is a directory.  See fuzz/statshim.h
 * for the measured field offsets and the whole story.
 *
 * The predecessor of this function was __wrap_stat, under
 * -Wl,--wrap=stat.  That was wrong: --wrap is a link-wide rename, so
 * every one of ntlibc's six internal stat() call sites was getting a
 * 144-byte host struct stat written into its 120-byte ntlibc one.  The
 * shim had been reviewed and its blast radius stated as "confined to
 * fuzz/"; it was not, and nothing noticed until fuzz_glob became the
 * first harness ever to reach an internal stat() call.
 *
 * A harness is NOT part of the renamed set -- it is compiled from
 * fuzz_*.c against ntlibc's headers and linked as-is -- so a plain
 * stat() call in a harness lands here and overruns its buffer the same
 * way.  Harnesses must call __real_stat(); fuzz/fuzz_glob.c does.
 *
 * Defined under a private name and aliased to `stat` in assembly,
 * because <sys/stat.h> -- included just above, so that `struct stat` and
 * __real_stat's prototype are ntlibc's -- already declares stat() with
 * ntlibc's own signature, and this one deliberately takes a void * that
 * is a host struct stat.  A C definition would be a conflicting
 * redeclaration; the alias is the same symbol either way. */
static int host_layout_stat(const char *path, void *hostbuf)
{
	struct stat st;
	struct ntfuzz_stat s;
	int r;

	/* The caller's buffer is a *host* struct stat, which is larger than
	 * ntlibc's; never write through it directly. */
	if (!hostbuf) return __real_stat(path, 0);
	r = __real_stat(path, &st);
	if (r != 0) return r;
	s.dev = st.st_dev;   s.ino = st.st_ino;   s.rdev = st.st_rdev;
	s.nlink = st.st_nlink;
	s.mode = st.st_mode; s.uid = st.st_uid;   s.gid = st.st_gid;
	s.size = st.st_size; s.blksize = st.st_blksize; s.blocks = st.st_blocks;
	s.atim_sec = st.st_atim.tv_sec; s.atim_nsec = st.st_atim.tv_nsec;
	s.mtim_sec = st.st_mtim.tv_sec; s.mtim_nsec = st.st_mtim.tv_nsec;
	s.ctim_sec = st.st_ctim.tv_sec; s.ctim_nsec = st.st_ctim.tv_nsec;
	__ntfuzz_pack_stat(hostbuf, &s);
	return 0;
}
__asm__(".globl stat\n\t.set stat, host_layout_stat");
#endif
