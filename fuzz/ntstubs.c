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
 *              creation, which fork+execve really performs; the clocks;
 *              RtlUTF8ToUnicodeN/RtlUnicodeToUTF8N (a from-spec
 *              conversion; see the note above them), RtlInitUnicodeString.
 *   plausible  NtQueryVolumeInformationFile for descriptors 0-2, which a
 *              native run cannot classify beyond "a character device".
 *   refusing   everything else: STATUS_NOT_IMPLEMENTED.  Any ntlibc code
 *              path that reaches one of those is simply not covered by
 *              the native build, and will report an error rather than
 *              pretend to work.  What is left is chiefly process cloning
 *              (RtlCloneUserProcess, so fork()), the object-manager
 *              symbolic links, and NtFsControlFile.
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
#define SYS_exit_group 231
#define SYS_clock_gettime 228
#define SYS_nanosleep 35
#define SYS_getpid 39
#define SYS_getppid 110

/* Handles are (fd + 1), so that 0 stays "no handle". */
#define H2FD(h) ((int)(long)(h) - 1)
#define FD2H(f) ((HANDLE)(long)((f) + 1))

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

/*
 * Natively there is no crt1.o: glibc's startup calls main() directly, so
 * the parts of __libc_start_main() that ntlibc code depends on have to
 * happen in a constructor instead.  ASan's own initialisation runs at
 * priority 1, ahead of this.
 */
char **__argv;
int __argc;
char *__progname;
char *__progname_full;
static char *shim_argv[2] = { (char *)"ntlibc-native", 0 };


static void vfs_init(void);   /* the simulated file system, below */

__attribute__((constructor(200))) void __ntshim_init(void)
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
	/* setenv()/putenv() realloc environ, so it has to start out on the
	 * heap the same way crt1.c's build_environ() leaves it. */
	environ = __interceptor_malloc(sizeof(char *));
	environ[0] = 0;
	__fd_init();

	/* Nothing calls ntlibc's exit() in a native build -- glibc's start-up
	 * calls main() and glibc's exit() ends it -- so __stdio_exit() never
	 * runs and anything left in a FILE buffer is lost.  libFuzzer's own
	 * diagnostics go through these two (its fprintf/stderr references bind
	 * to ntlibc's, which are the definitions in this executable), so they
	 * would vanish.  Unbuffered costs nothing here and loses nothing. */
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
 * reparse points and symlinks, alternate streams, extended attributes,
 * short (8.3) names, and volumes other than C:.
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
struct vpipe {
	struct vpipe *next;
	WCHAR *name;
	size_t namelen;
	unsigned char *data;
	size_t len, cap;
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
};

static struct ofile *vhandles[VFS_HANDLES];
static struct vpipe *vpipes;
static struct ofile stdfiles[3];
static struct vnode *vroot;
static struct vnode *vcwd;
static unsigned long long next_id = 100;

#define VOLUME_SERIAL 0x4e544653u

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
		if (f->writer) p->writers--; else p->readers--;
		if (!p->readers && !p->writers) {
			for (pp = &vpipes; *pp; pp = &(*pp)->next)
				if (*pp == p) { *pp = p->next; break; }
			vfree(p->name);
			vfree(p->data);
			vfree(p);
		}
	}
	if (f->kind == OF_VFS) {
		struct vnode *n = f->node;
		int unlinked = 0;
		if (f->delete_on_close && !n->delete_pending) n->delete_pending = 1;
		n->refs--;
		if (n->delete_pending && n->refs == 0 && f->dir) {
			struct vent *e = dir_find(f->dir, f->name, f->namelen);
			/* dir_remove drops the last link and releases the node with
			 * it, so releasing again here would be a use-after-free. */
			if (e && e->node == n) { dir_remove(f->dir, e); unlinked = 1; }
		}
		if (!unlinked) node_release(n);
		vfree(f->name);
	}
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
	(void)alloc; (void)share; (void)ea; (void)ealen;
	return do_create(out, access, oa, io, attrs, disposition, options);
}

NTSTATUS NTAPI NtOpenFile(PHANDLE out, ACCESS_MASK access, POBJECT_ATTRIBUTES oa,
                          PIO_STATUS_BLOCK io, ULONG share, ULONG options)
{
	(void)share;
	return do_create(out, access, oa, io, 0, FILE_OPEN, options);
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
		p = vmalloc(sizeof *p);
		if (!p) return STATUS_NO_MEMORY;
		memset(p, 0, sizeof *p);
		p->name = wdup(vp.pipename, vp.pipelen);
		p->namelen = vp.pipelen;
		if (!p->name) { vfree(p); return STATUS_NO_MEMORY; }
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
	if (i < 0 || i >= VFS_HANDLES || !vhandles[i]) return STATUS_INVALID_HANDLE;
	f = vhandles[i];
	vhandles[i] = 0;
	of_drop(f);
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
		if (!p->readers) return STATUS_PIPE_BROKEN;
		if (len) {
			if (p->len + len > p->cap) {
				size_t want = p->len + len + 4096;
				unsigned char *nb = __interceptor_realloc(p->data, want);
				if (!nb) return STATUS_NO_MEMORY;
				p->data = nb;
				p->cap = want;
			}
			memcpy(p->data + p->len, buf, len);
			p->len += len;
		}
		if (io) { io->Status = STATUS_SUCCESS; io->Information = len; }
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
		ULONG take;
		if (f->writer) return STATUS_ACCESS_DENIED;
		if (!p->len) {
			/* Empty: NT blocks a synchronous read until a writer
			 * supplies data, which a single-threaded native build
			 * cannot do -- it would deadlock.  With every write end
			 * closed the answer is the real one, STATUS_PIPE_BROKEN
			 * (end of file); otherwise the closest NT status for
			 * "nothing to read right now" is returned rather than
			 * hanging. */
			if (!p->writers) return STATUS_PIPE_BROKEN;
			return STATUS_PIPE_EMPTY;
		}
		take = (ULONG)(p->len < (size_t)len ? p->len : (size_t)len);
		memcpy(buf, p->data, take);
		memmove(p->data, p->data + take, p->len - take);
		p->len -= take;
		if (io) { io->Status = STATUS_SUCCESS; io->Information = take; }
		return STATUS_SUCCESS;
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
NTSTATUS NTAPI NtQueryObject(HANDLE h, ULONG cls, PVOID buf, ULONG len, PULONG ret)
{
	struct { UNICODE_STRING Name; WCHAR Buffer[1]; } *oni = buf;
	struct ofile *f = of_get(h);
	WCHAR *path;
	size_t plen, need;

	if (!f) return STATUS_INVALID_HANDLE;
	if (cls != 1) return STATUS_INVALID_INFO_CLASS;
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
		if (e && e->node == v) dir_remove(f->dir, e);
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

NTSTATUS NTAPI NtQuerySystemTime(LARGE_INTEGER *t)
{
	struct { long sec, nsec; } ts = { 0, 0 };
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
 * This one has to really sleep.  libFuzzer runs a watchdog thread that
 * loops on sleep(1); returning immediately turns that into a spin that
 * starves the fuzzing thread -- the symptom is three executions in ninety
 * seconds.
 */
NTSTATUS NTAPI NtDelayExecution(BOOLEAN alertable, LARGE_INTEGER *t)
{
	struct { long sec, nsec; } ts;
	long long ticks;
	(void)alertable;
	if (!t) return STATUS_SUCCESS;
	ticks = *t;
	if (ticks >= 0) return STATUS_SUCCESS;   /* absolute time: not supported */
	ticks = -ticks;                          /* relative, in 100ns units */
	ts.sec = (long)(ticks / 10000000LL);
	ts.nsec = (long)((ticks % 10000000LL) * 100);
	syscall(SYS_nanosleep, &ts, (void *)0);
	return STATUS_SUCCESS;
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
 *     kernel can actually execute.  Nothing else in this file touches the
 *     host file system.
 *   - NT creates the process suspended and __spawn resumes it;
 *     fork+execve starts running at once, so NtResumeThread is a no-op.
 *   - Handle inheritance is the host's: an inherited descriptor is one
 *     the child gets because fork copies the descriptor table, not
 *     because OBJ_INHERIT was set.  The simulated file system does not
 *     cross the fork -- the child gets its own, empty but for the
 *     starting layout -- so a child cannot see a file its parent made.
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
	char **argv, **envp;
	struct ofile *f;
	NTSTATUS st;
	long pid;
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
		st = STATUS_OBJECT_NAME_NOT_FOUND;
		goto out;
	}

	f = vmalloc(sizeof *f);
	if (!f) { st = STATUS_NO_MEMORY; goto out; }
	memset(f, 0, sizeof *f);
	f->kind = OF_PROC;
	f->exitcode = STATUS_PENDING;

	pid = syscall(SYS_fork);
	if (pid < 0) { vfree(f); st = STATUS_INSUFFICIENT_RESOURCES; goto out; }
	if (pid == 0) {
		syscall(SYS_execve, host, argv, envp);
		syscall(SYS_exit_group, 127);
	}
	f->pid = (int)pid;
	st = of_install(f, &info->Process);
	if (!NT_SUCCESS(st)) { vfree(f); goto out; }
	/* There is one thread and it is the process; the caller closes this
	 * handle separately, so it is a second reference to the same object. */
	st = of_install(f, &info->Thread);
	if (!NT_SUCCESS(st)) { NtClose(info->Process); goto out; }
	info->ClientId.UniqueProcess = (HANDLE)(LONG_PTR)pid;
	info->ClientId.UniqueThread = (HANDLE)(LONG_PTR)pid;
	st = STATUS_SUCCESS;

out:
	vfree(host);
	free_strv(argv);
	free_strv(envp);
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

/* Reap the child if it has finished; 1 = it has, 0 = still running. */
static int proc_poll(struct ofile *f, int nohang)
{
	int status = 0;
	long r;
	if (f->exited) return 1;
	r = syscall(SYS_wait4, (long)f->pid, &status, (long)(nohang ? 1 /*WNOHANG*/ : 0), 0);
	if (r != f->pid) return 0;
	f->exited = 1;
	if ((status & 0x7f) == 0) f->exitcode = (status >> 8) & 0xff;
	else f->exitcode = __NT_SIGNAL_EXIT(status & 0x7f);   /* killed by a signal */
	return 1;
}

NTSTATUS NTAPI NtWaitForSingleObject(HANDLE h, BOOLEAN alertable, LARGE_INTEGER *timeout)
{
	struct ofile *f = of_get(h);
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
	if (proc_poll(f, timeout && *timeout == 0)) return STATUS_WAIT_0;
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

NTSTATUS NTAPI NtTerminateProcess(HANDLE h, NTSTATUS code)
{
	struct ofile *f = h && h != NtCurrentProcess() ? of_get(h) : 0;
	if (f && f->kind == OF_PROC) {
		if (syscall(SYS_kill, (long)f->pid, 9 /*SIGKILL*/) < 0) return STATUS_ACCESS_DENIED;
		return STATUS_SUCCESS;
	}
	syscall(SYS_exit_group, (long)(int)code);
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
NOTIMPL(NtOpenProcess, (PHANDLE a, ACCESS_MASK b, POBJECT_ATTRIBUTES c, PCLIENT_ID d))
NOTIMPL(NtOpenSymbolicLinkObject, (PHANDLE a, ACCESS_MASK b, POBJECT_ATTRIBUTES c))
NOTIMPL(NtQuerySymbolicLinkObject, (HANDLE a, PUNICODE_STRING b, PULONG c))
NOTIMPL(NtQuerySystemInformation, (SYSTEM_INFORMATION_CLASS a, PVOID b, ULONG c, PULONG d))
NOTIMPL(NtSetSystemTime, (LARGE_INTEGER *a, LARGE_INTEGER *b))
NOTIMPL(RtlCloneUserProcess, (ULONG a, PVOID b, PVOID c, HANDLE d,
                              RTL_USER_PROCESS_INFORMATION *e))

PVOID NTAPI RtlAddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER h)
{
	(void)first; (void)h;
	return (PVOID)(long)1;
}
