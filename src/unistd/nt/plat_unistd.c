/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT implementation of src/internal/plat_unistd.h -- see that header for
 * the contract each function makes.  Everything here was, until this
 * file existed, inline inside the src/unistd/{sleep,getpid,ftruncate,
 * ids,link,fsync,pipe,sysconf,unlink,chdir}.c front doors it now serves;
 * nothing changed in substance, only location and the addition of a
 * POSIX-shaped return (0/-1 or a value with errno already set) in place
 * of a raw NTSTATUS/HANDLE for the front door to interpret.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <unistd.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "libc.h"
#include "ownership_stubs.h"
#include "plat_fd.h"
#include "plat_unistd.h"

/* ======================================================================
 * sleep.c: the process-wide alarm timer, and the clock alarm()/
 * __alertable_delay() share with it.
 * ====================================================================== */

long long __plat_time_now(void)
{
	LARGE_INTEGER now;
	NtQuerySystemTime(&now);
	return now;
}

/* The process-wide alarm timer's own handle.  Created on the first
 * __plat_alarm_arm() that asks for one and then kept for the life of the
 * process -- a single handle, re-armed in place -- so that
 * __plat_alarm_reset_after_fork() can simply forget it (see that
 * function) instead of having to decide whether a handle number is safe
 * to close. */
static HANDLE alarm_timer;
/* Which front-door callback to invoke, and with what identity, the next
 * time alarm_apc runs.  Set together with every successful arm, read
 * only from alarm_apc. */
static __plat_alarm_fn alarm_deliver;

static void NTAPI alarm_apc(PVOID ctx, ULONG lo, LONG hi) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	(void)lo; (void)hi;
	if (alarm_deliver) alarm_deliver((unsigned long)(ULONG_PTR)ctx);
}

int __plat_alarm_arm(long long due, unsigned long seq, __plat_alarm_fn deliver) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	LARGE_INTEGER at = due;

	if (!alarm_timer) {
		OBJECT_ATTRIBUTES oa;
		memset(&oa, 0, sizeof oa);
		oa.Length = sizeof oa;
		/* Unnamed and, deliberately, not OBJ_INHERIT: fork() must not
		 * hand the child a live copy of the parent's alarm
		 * (fork.html), and leaving the handle non-inheritable means
		 * RtlCloneUserProcess's INHERIT_HANDLES never copies it in
		 * the first place. */
		if (!NT_SUCCESS(NtCreateTimer(&alarm_timer, TIMER_ALL_ACCESS, &oa, NotificationTimer))) {
			alarm_timer = 0;
			return -1;
		}
	}
	alarm_deliver = deliver;
	if (!NT_SUCCESS(NtSetTimer(alarm_timer, &at, alarm_apc, (PVOID)(ULONG_PTR)seq, 0, 0, NULL)))
		return -1;
	return 0;
}

void __plat_alarm_cancel(void)
{
	if (alarm_timer) NtCancelTimer(alarm_timer, NULL);
}

void __plat_alarm_reset_after_fork(void)
{
	alarm_timer = 0;
	alarm_deliver = 0;
}

/* ======================================================================
 * getpid.c
 * ====================================================================== */

pid_t __plat_getppid(void)
{
	PROCESS_BASIC_INFORMATION pbi;
	if (!NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), ProcessBasicInformation, &pbi, sizeof pbi, 0)))
		return 1;
	return (pid_t)pbi.InheritedFromUniqueProcessId;
}

/* Moved verbatim from src/unistd/getpid.c's own front door -- no
 * behavior change, only relocation (see plat_unistd.h's own banner). */
pid_t __plat_getpid(void)
{
	return (pid_t)(ULONG_PTR)__teb()->ClientId.UniqueProcess;
}

pid_t __plat_gettid(void)
{
	return (pid_t)(ULONG_PTR)__teb()->ClientId.UniqueThread;
}

/* ======================================================================
 * ftruncate.c
 * ====================================================================== */

int __plat_ftruncate(__plat_handle_t h, off_t len)
{
	IO_STATUS_BLOCK io;
	FILE_END_OF_FILE_INFORMATION eof;
	NTSTATUS st;
	eof.EndOfFile = len;
	st = NtSetInformationFile(h, &io, &eof, sizeof eof, FileEndOfFileInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

/* ======================================================================
 * fsync.c
 * ====================================================================== */

int __plat_fsync(__plat_handle_t h)
{
	IO_STATUS_BLOCK io;
	NTSTATUS st = NtFlushBuffersFile(h, &io);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

/* ======================================================================
 * pipe.c: an anonymous pipe is a named pipe with a name nobody else will
 * guess, which is exactly how kernel32's CreatePipe makes one.  The read
 * end is the server side, created with NtCreateNamedPipeFile; the write
 * end is an ordinary NtOpenFile of the same name.  Both are synchronous
 * and byte-stream, so read and write on them behave as they do on a
 * file.
 *
 * __pipe_handles() (src/internal/libc.h) is the original, NTSTATUS/
 * HANDLE-returning form of this: src/select/select.c's one-shot
 * WriteQuotaAvailable capability probe calls it directly to make a pipe
 * of its own without allocating fds, and keeps doing so unchanged.
 * __plat_pipe() below is the POSIX-shaped form pipe2() uses, implemented
 * on top of it so the two front doors cannot disagree about how a pipe
 * is actually made.
 * ====================================================================== */

NTSTATUS __pipe_handles(HANDLE *rp, HANDLE *wp, int inherit)
{
	static unsigned serial;
	WCHAR name[64];
	UNICODE_STRING us;
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK io;
	LARGE_INTEGER timeout = -1200000000LL;  /* 120s, the default */
	HANDLE r, w;
	NTSTATUS st;
	unsigned pid = (unsigned)(ULONG_PTR)__teb()->ClientId.UniqueProcess;
	const char pfx[] = "\\Device\\NamedPipe\\ntlibc.";
	int i = 0;

	for (; pfx[i]; i++) name[i] = (unsigned char)pfx[i];
	/* "<pid>.<serial>" in hex */
	i = __nt_append_hex32(name, i, pid);
	name[i++] = '.';
	serial++;
	i = __nt_append_hex32(name, i, serial);
	name[i] = 0;
	us.Buffer = name;
	if ((size_t)i > __US_MAX_WCHARS) return STATUS_NAME_TOO_LONG;
	us.Length = (USHORT)(i * sizeof(WCHAR));
	us.MaximumLength = (USHORT)(us.Length + sizeof(WCHAR));
	InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | (inherit ? OBJ_INHERIT : 0), 0, 0);

	st = NtCreateNamedPipeFile(&r, GENERIC_READ | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &oa, &io,
	                           FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE,
	                           FILE_SYNCHRONOUS_IO_NONALERT,
	                           FILE_PIPE_BYTE_STREAM_TYPE, FILE_PIPE_BYTE_STREAM_MODE,
	                           FILE_PIPE_QUEUE_OPERATION, 1, 65536, 65536, &timeout);
	if (!NT_SUCCESS(st)) return st;

	st = NtOpenFile(&w, GENERIC_WRITE | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &io,
	                FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
	if (!NT_SUCCESS(st)) { NtClose(r); return st; }

	*rp = r;
	*wp = w;
	return STATUS_SUCCESS;
}

int __plat_pipe(__plat_handle_t *rp, __plat_handle_t *wp, int inheritable)
{
	HANDLE r, w;
	NTSTATUS st = __pipe_handles(&r, &w, inheritable);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	*rp = r;
	*wp = w;
	return 0;
}

/* ======================================================================
 * sysconf.c
 * ====================================================================== */

long __plat_nprocessors(void)
{
	SYSTEM_BASIC_INFORMATION sbi;
	if (NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof sbi, 0)))
		return sbi.NumberOfProcessors;
	return 1;
}

long __plat_phys_pages(void)
{
	SYSTEM_BASIC_INFORMATION sbi;
	if (NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof sbi, 0)))
		return (long)((unsigned long long)sbi.NumberOfPhysicalPages * sbi.PageSize / 4096);
	return -1;
}

/* ======================================================================
 * unlink.c: unlink and rmdir, open for DELETE and set the disposition.
 * POSIX semantics (the name goes away at once even while other handles
 * are open) are asked for first, on Windows 10 1709 and later; older
 * systems answer STATUS_INVALID_PARAMETER and get the classic
 * delete-on-close.
 * ====================================================================== */

int __plat_unlink(int dirfd, const char *path, int isdir)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	FILE_DISPOSITION_INFORMATION_EX dx;
	FILE_DISPOSITION_INFORMATION d;
	FILE_BASIC_INFORMATION bi;

	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	st = NtOpenFile(&h, DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS,
	                FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT |
	                (isdir ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE));
	__ntpath_free(&np);
	if (st == STATUS_FILE_IS_A_DIRECTORY) { errno = EISDIR; return -1; }
	if (st == STATUS_NOT_A_DIRECTORY) { errno = ENOTDIR; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	/* A read-only file cannot be deleted until the attribute is cleared;
	 * Unix has no such notion, so clear it. */
	if (NT_SUCCESS(NtQueryInformationFile(h, &io, &bi, sizeof bi, FileBasicInformation)) &&
	    (bi.FileAttributes & FILE_ATTRIBUTE_READONLY)) {
		FILE_BASIC_INFORMATION set = {0};
		set.FileAttributes = (bi.FileAttributes & ~FILE_ATTRIBUTE_READONLY) | ((bi.FileAttributes & ~FILE_ATTRIBUTE_READONLY) ? 0 : FILE_ATTRIBUTE_NORMAL);
		NtSetInformationFile(h, &io, &set, sizeof set, FileBasicInformation);
	}

	dx.Flags = FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS;
	st = NtSetInformationFile(h, &io, &dx, sizeof dx, FileDispositionInformationEx);
	if (st == STATUS_INVALID_PARAMETER || st == STATUS_INVALID_INFO_CLASS || st == STATUS_NOT_SUPPORTED || st == STATUS_NOT_IMPLEMENTED) {
		d.DeleteFile = 1;
		st = NtSetInformationFile(h, &io, &d, sizeof d, FileDispositionInformation);
	}
	NtClose(h);
	if (st == STATUS_DIRECTORY_NOT_EMPTY) { errno = ENOTEMPTY; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

/* ======================================================================
 * gethostname.c: moved verbatim from src/unistd/gethostname.c's own
 * front door -- no behaviour change, only location (see plat_unistd.h's
 * own comment: NT has no kernel notion of a hostname at all, so
 * COMPUTERNAME -- the calling process's own environment -- is the only
 * oracle a plain ntdll-only build has for one).
 * ====================================================================== */

void __plat_hostname(char *buf, size_t bufsz)
{
	const char *h = getenv("COMPUTERNAME");
	size_t n, i;
	if (!h) h = "localhost";
	n = strlen(h);
	if (!bufsz) return;
	if (n >= bufsz) n = bufsz - 1;
	for (i = 0; i < n; i++) buf[i] = h[i];
	buf[n] = '\0';
}

/* ======================================================================
 * getcwd.c: moved verbatim from src/unistd/getcwd.c's own front door --
 * no behaviour change, only location (see plat_unistd.h's own comment).
 * getcwd returns the DOS form, C:\dir, with backslashes turned into
 * forward slashes so that programs that split paths on '/' (which is
 * most of them) keep working.  A trailing slash is removed except at a
 * drive root.
 * ====================================================================== */

ssize_t __plat_getcwd(char *buf, size_t bufsz)
{
	WCHAR w[4096];
	ULONG n;
	size_t i;
	int r;

	n = RtlGetCurrentDirectory_U(sizeof w, w);
	if (!n || n > sizeof w) { errno = ERANGE; return -1; }
	n /= sizeof(WCHAR);
	for (i = 0; i < n; i++) if (w[i] == '\\') w[i] = '/';
	if (n > 3 && w[n-1] == '/') n--;
	r = __utf16_to_utf8_buf(w, n, buf, bufsz);
	if (r < 0) return -1;
	return r;
}

/* ======================================================================
 * chdir.c
 *
 * __plat_chdir() absorbed everything src/unistd/chdir.c's own chdir()
 * used to do between its NUL/empty-string check and the old bare
 * __plat_chdir(path) call: __vfs_resolve_at() (src/internal/vfs.c) --
 * the fixed POSIX namespace overlay NT needs because it has no native
 * concept of `/`/`/dev` as anything other than ordinary directories --
 * the kind/native checks, the "/" substitution for a non-native virtual
 * directory, and the {NAME_MAX}-per-component check ordinarily done by
 * src/internal/path.c's own builder (not used here: RtlSetCurrentDirectory_U
 * takes the DOS form directly, so this function hand-builds its own
 * UNICODE_STRING rather than routing through __ntpath()/__ntpath_at()).
 * Nothing below changed in substance from what chdir.c used to run
 * inline, only location -- the same relocation __plat_open() (src/fcntl/
 * nt/plat_fcntl.c) already got. *vfsout reports the resolved vfs kind
 * back to the front door for __vfs_cwd_set() (portable bookkeeping that
 * stays there, see chdir.c's own comment).
 * ====================================================================== */

int __plat_chdir(const char *path, int *vfsout)
{
	WCHAR *w;
	size_t n, i;
	UNICODE_STRING us;
	NTSTATUS st;
	int vfs, kind, native;

	vfs = __vfs_resolve_at(AT_FDCWD, path);
	if (vfs < 0) return -1;
	native = (vfs & __VFS_NATIVE) != 0;
	kind = __VFS_KIND(vfs);
	if (kind == __VFS_MISSING) { errno = ENOENT; return -1; }
	if (kind != __VFS_NONE && kind != __VFS_ROOT && kind != __VFS_DEV) {
		errno = ENOTDIR;
		return -1;
	}
	/* Both virtual directories use the native drive root only as the
	 * process-parameter carrier; pathname dispatch uses vfs above. */
	if (kind != __VFS_NONE && !native) path = "/";
	/* chdir.html ERRORS, shall fail: "[ENAMETOOLONG] The length of a
	 * component of a pathname is longer than {NAME_MAX}."  chdir does
	 * not go through src/internal/path.c's builder -- this function
	 * hand-builds its own UNICODE_STRING for RtlSetCurrentDirectory_U --
	 * so it has to ask for itself, or it would be the one path-taking
	 * interface in the library without the check.  Distinct from the
	 * whole-path bound __US_MAX_WCHARS applies to its own UNICODE_STRING
	 * below; see __name_too_long()'s banner. */
	if (__name_too_long(path)) { errno = ENAMETOOLONG; return -1; }

	w = __utf8_to_utf16(path, &n);
	if (!w) return -1;
	for (i = 0; i < n; i++) if (w[i] == '/') w[i] = '\\';
	/* The path goes into a UNICODE_STRING, whose Length is a USHORT
	 * counting bytes; a longer path would wrap rather than truncate and
	 * we would chdir into some prefix of what was asked for. */
	if (n > __US_MAX_WCHARS) { __free(w); errno = ENAMETOOLONG; return -1; }
	us.Buffer = w;
	us.Length = (USHORT)(n * sizeof(WCHAR));
	us.MaximumLength = (USHORT)(us.Length + sizeof(WCHAR));
	st = RtlSetCurrentDirectory_U(&us);
	/* chdir.html ERRORS [ENOTDIR]: "A component of the path prefix names
	 * an existing file that is neither a directory nor a symbolic link to
	 * a directory."  RtlSetCurrentDirectory_U passes NtOpenFile's status
	 * through, so a non-directory *last* component already arrives as
	 * STATUS_NOT_A_DIRECTORY and needs nothing here; but a non-directory
	 * *prefix* component and a missing one are byte-identical --
	 * STATUS_OBJECT_PATH_NOT_FOUND for both, measured on Windows 11 Pro
	 * 22621 on NTFS -- so no status remap can tell them apart and the
	 * prefix has to be walked.  Only that one status is disambiguated, so
	 * a successful chdir() is still the one call it always was.
	 *
	 * The walk wants an NT path, which is not otherwise built here
	 * (RtlSetCurrentDirectory_U takes the DOS form), so it is converted
	 * here, on a path that has already failed. */
	if (st == STATUS_OBJECT_PATH_NOT_FOUND) {
		UNICODE_STRING nt;
		if (NT_SUCCESS(RtlDosPathNameToNtPathName_U_WithStatus(w, &nt, 0, 0))) {
			int notdir = __nt_prefix_not_dir(&nt, 0);
			RtlFreeHeap(__process_heap(), 0, nt.Buffer);
			if (notdir) { __free(w); errno = ENOTDIR; return -1; }
		}
	}
	__free(w);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	*vfsout = vfs;
	return 0;
}

/* ======================================================================
 * link.c: hard links and symbolic links.  Hard links are
 * FileLinkInformation.  Symbolic links need SeCreateSymbolicLinkPrivilege
 * or developer mode on Windows, so symlink tries and reports EPERM when
 * it cannot; readlink reads both NTFS symlinks and junctions.
 * ====================================================================== */

/* Offset of the union inside a REPARSE_DATA_BUFFER (8 on the wire, the
 * ReparseTag/ReparseDataLength/Reserved header that ReparseDataLength
 * does not count), and the offset of the symlink variant's PathBuffer
 * from the start of that union (12 on the wire).  Both are taken from
 * the struct as the compiler laid it out, because that is what the
 * writes below go through. */
#define RDB_HDR offsetof(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer)
#define SL_HDR  (offsetof(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer.PathBuffer) - RDB_HDR)

typedef struct _FILE_LINK_INFORMATION { // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling follows the NT ABI
	BOOLEAN ReplaceIfExists;
	HANDLE RootDirectory;
	ULONG FileNameLength;
	WCHAR FileName[1];
} FILE_LINK_INFORMATION;

int __plat_link(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int followsym)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	FILE_LINK_INFORMATION *li;
	FILE_ATTRIBUTE_TAG_INFORMATION ti;
	size_t sz;
	ULONG opts;

	/* link.html DESCRIPTION: "If path1 names a symbolic link, ... [if]
	 * the AT_SYMLINK_FOLLOW flag is clear ... a new link is created for
	 * the symbolic link path1 and not its target"; with the flag set,
	 * "a new link is created for the file referred to by path1".  The
	 * whole of that distinction is in this one create option: NT
	 * resolves a reparse point on open unless FILE_OPEN_REPARSE_POINT
	 * asks it not to, so the handle FileLinkInformation is set on -- and
	 * therefore the file the new directory entry names -- is the link
	 * itself with the option, and the target without it.  Nothing below
	 * needs a second path: the attribute query, the [EPERM] decision and
	 * the link all run against whichever file this open picked. */
	opts = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT;
	if (!followsym) opts |= FILE_OPEN_REPARSE_POINT;

	if (__ntpath_at(olddirfd, oldpath, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS, opts);
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	/* link.html ERRORS: "[EPERM] The file named by path1 is a directory
	 * and either the calling process does not have appropriate
	 * privileges or the implementation prohibits using link() on
	 * directories."  NTFS prohibits them outright -- FileLinkInformation
	 * on a directory handle is refused however privileged the caller is
	 * -- so the clause's second branch applies to every directory and
	 * the errno is decided here rather than left to the driver.  Asking
	 * the open handle for its attributes settles it without depending on
	 * which status a particular filesystem picks: left to
	 * NtSetInformationFile, NTFS answers STATUS_FILE_IS_A_DIRECTORY,
	 * which src/internal/errno.c maps to EISDIR -- correct where EISDIR
	 * is a specified errno (open(), rename()), but link.html's ERRORS
	 * list does not contain EISDIR at all.
	 *
	 * The predicate matches src/stdio/misc.c's isdir_attrs(), so
	 * linkat(), renameat() and lstat() agree on what counts as a
	 * directory: NT puts FILE_ATTRIBUTE_DIRECTORY on a symbolic link to
	 * a directory, but POSIX classifies a symbolic link as a
	 * non-directory file whatever it points at, and with
	 * AT_SYMLINK_FOLLOW clear it is the link itself that is being
	 * linked.  With the flag set the open above already followed the
	 * link, so these are the target's attributes and the reparse-point
	 * exemption cannot fire: a symbolic link to a directory is then
	 * "path1 names a directory" and [EPERM] is right. */
	if (NT_SUCCESS(NtQueryInformationFile(h, &io, &ti, sizeof ti, FileAttributeTagInformation)) &&
	    (ti.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
	    !((ti.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
	      (ti.ReparseTag == IO_REPARSE_TAG_SYMLINK || ti.ReparseTag == IO_REPARSE_TAG_MOUNT_POINT ||
	       ti.ReparseTag == IO_REPARSE_TAG_LX_SYMLINK))) {
		NtClose(h);
		errno = EPERM;
		return -1;
	}

	if (__ntpath_at(newdirfd, newpath, &np, OBJ_CASE_INSENSITIVE) < 0) { NtClose(h); return -1; }
	sz = sizeof *li + np.nt.Length;
	li = __malloc(sz);
	if (!li) { NtClose(h); __ntpath_free(&np); return -1; }
	li->ReplaceIfExists = 0;
	li->RootDirectory = np.oa.RootDirectory;
	li->FileNameLength = np.nt.Length;
	__ownership_writable_span(li->FileName, np.nt.Length);
	__ownership_readable_span(np.nt.Buffer, np.nt.Length);
	memcpy(li->FileName, np.nt.Buffer, np.nt.Length);
	st = NtSetInformationFile(h, &io, li, (ULONG)sz, FileLinkInformation);
	__free(li);
	__ntpath_free(&np);
	NtClose(h);
	if (!NT_SUCCESS(st)) {
		/* The same [EPERM] clause, for the volume whose driver cannot
		 * answer the attribute query above (the check is skipped then)
		 * or that reports a directory reparse point this way.  An
		 * already-taken path2 does not arrive here as this status --
		 * ReplaceIfExists is 0, so an existing name, directory
		 * included, is STATUS_OBJECT_NAME_COLLISION, i.e. EEXIST. */
		if (st == STATUS_FILE_IS_A_DIRECTORY) { errno = EPERM; return -1; }
		return __set_errno_status(st);
	}
	return 0;
}

ssize_t __plat_readlink(int dirfd, const char *path, char *buf, size_t bufsz)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	char rb[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
	REPARSE_DATA_BUFFER *r = (REPARSE_DATA_BUFFER *)rb;
	FILE_ATTRIBUTE_TAG_INFORMATION ti;
	const WCHAR *name;
	size_t nlen, i;
	WCHAR *tmp;
	int n, vfs;

	/* Ruling out a virtual-fs path used to be src/unistd/link.c's own
	 * readlinkat() front-door job; moved here for the same reason
	 * __plat_open()/__plat_chdir() absorbed their own vfs pre-checks --
	 * __vfs_resolve_at() (src/internal/vfs.c) is NT-only-overlay
	 * machinery a backend with real native symlinks (Linux) has no use
	 * for at all. */
	vfs = __vfs_resolve_at(dirfd, path);
	if (vfs < 0) return -1;
	if (vfs & __VFS_NATIVE) vfs = __VFS_NONE;
	if (vfs == __VFS_MISSING) { errno = ENOENT; return -1; }
	if (vfs != __VFS_NONE) { errno = EINVAL; return -1; }

	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS,
	                FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT);
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	/* [EINVAL] "The path argument names a file that is not a symbolic
	 * link".  Whether it is one is a file attribute, and asking for the
	 * attribute answers that question directly.  Deciding it from
	 * FSCTL_GET_REPARSE_POINT's status instead only works on a volume
	 * whose driver implements the FSCTL at all: one that does not (FAT,
	 * and several redirectors) refuses the request outright --
	 * STATUS_INVALID_DEVICE_REQUEST, STATUS_NOT_SUPPORTED -- rather than
	 * with STATUS_NOT_A_REPARSE_POINT, and every plain file on such a
	 * volume then reported that refusal's errno in place of EINVAL. */
	st = NtQueryInformationFile(h, &io, &ti, sizeof ti, FileAttributeTagInformation);
	if (NT_SUCCESS(st) && !(ti.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
		NtClose(h);
		errno = EINVAL;
		return -1;
	}
	st = NtFsControlFile(h, 0, 0, 0, &io, FSCTL_GET_REPARSE_POINT, 0, 0, r, sizeof rb);
	NtClose(h);
	if (st == STATUS_NOT_A_REPARSE_POINT) { errno = EINVAL; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	/* PathBuffer + byteOffset/sizeof(WCHAR): converting an NT byte offset
	 * into a WCHAR element index before it is added to a WCHAR*, which
	 * pointer arithmetic then scales by sizeof(WCHAR) itself -- the
	 * correct idiom, not the double-scaling the check is looking for. */
	if (r->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
		name = r->SymbolicLinkReparseBuffer.PathBuffer + r->SymbolicLinkReparseBuffer.PrintNameOffset / sizeof(WCHAR); // NOLINT(bugprone-sizeof-expression,cert-arr39-c)
		nlen = r->SymbolicLinkReparseBuffer.PrintNameLength / sizeof(WCHAR);
		if (!nlen) {
			name = r->SymbolicLinkReparseBuffer.PathBuffer + r->SymbolicLinkReparseBuffer.SubstituteNameOffset / sizeof(WCHAR); // NOLINT(bugprone-sizeof-expression,cert-arr39-c)
			nlen = r->SymbolicLinkReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
		}
	} else if (r->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
		name = r->MountPointReparseBuffer.PathBuffer + r->MountPointReparseBuffer.PrintNameOffset / sizeof(WCHAR); // NOLINT(bugprone-sizeof-expression,cert-arr39-c)
		nlen = r->MountPointReparseBuffer.PrintNameLength / sizeof(WCHAR);
		if (!nlen) {
			name = r->MountPointReparseBuffer.PathBuffer + r->MountPointReparseBuffer.SubstituteNameOffset / sizeof(WCHAR); // NOLINT(bugprone-sizeof-expression,cert-arr39-c)
			nlen = r->MountPointReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
		}
	} else if (r->ReparseTag == IO_REPARSE_TAG_LX_SYMLINK) {
		/* WSL symlink: a version dword followed by a UTF-8 target. */
		const char *t = (const char *)r->GenericReparseBuffer.DataBuffer + 4;
		size_t tl = r->ReparseDataLength - 4;
		if (tl > bufsz) tl = bufsz;
		__ownership_writable_span(buf, tl);
		__ownership_readable_span(t, tl);
		memcpy(buf, t, tl);
		return (ssize_t)tl;
	} else {
		errno = EINVAL;
		return -1;
	}
	/* Strip a \??\ prefix and turn backslashes into slashes. */
	if (nlen >= 4 && name[0] == '\\' && name[1] == '?' && name[2] == '?' && name[3] == '\\') { name += 4; nlen -= 4; }
	{
		size_t units, bytes;
		if (!__size_add_checked(nlen, 1, &units) ||
		    !__size_mul_checked(units, sizeof(WCHAR), &bytes)) {
			errno = ENOMEM; return -1;
		}
		tmp = __malloc(bytes);
	}
	if (!tmp) return -1;
	for (i = 0; i < nlen; i++) tmp[i] = name[i] == '\\' ? '/' : name[i];
	{
		char *u = __utf16_to_utf8(tmp, nlen);
		__free(tmp);
		if (!u) return -1;
		n = (int)strlen(u);
		if ((size_t)n > bufsz) n = (int)bufsz;
		__ownership_writable_span(buf, (size_t)n);
		memcpy(buf, u, n);
		__free(u);
	}
	return n;
}

int __plat_symlink(const char *target, int newdirfd, const char *linkpath)
{
	/* Creating a reparse point needs the link to exist first, then
	 * FSCTL_SET_REPARSE_POINT with a SYMLINK buffer.  Whether it is a
	 * file or directory link depends on the target, which may not exist:
	 * guess file unless the target is a directory now. */
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	WCHAR *wt;
	size_t tl, i, sz, off;
	REPARSE_DATA_BUFFER *r;
	int isdir = 0, relative;
	FILE_NETWORK_OPEN_INFORMATION ni;
	struct __ntpath tp;

	relative = !(target[0] == '/' || target[0] == '\\' || (target[0] && target[1] == ':'));
	if (__ntpath(target, &tp, OBJ_CASE_INSENSITIVE) == 0) {
		if (NT_SUCCESS(NtQueryFullAttributesFile(&tp.oa, &ni)) && (ni.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) isdir = 1;
		__ntpath_free(&tp);
	}

	if (__ntpath_at(newdirfd, linkpath, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	st = NtCreateFile(&h, FILE_WRITE_ATTRIBUTES | DELETE | SYNCHRONIZE, &np.oa, &io, 0, FILE_ATTRIBUTE_NORMAL,
	                  FILE_SHARE_VALID_FLAGS, FILE_CREATE,
	                  FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | (isdir ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE), 0, 0);
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) {
		/* symlink.html has one shall-fail clause for a name that is
		 * already taken -- "[EEXIST] The path2 argument names an
		 * existing file" -- and a directory is a file, so a directory
		 * at linkpath has to reach it too.  FILE_CREATE over an
		 * existing name is STATUS_OBJECT_NAME_COLLISION, which
		 * src/internal/errno.c already maps to EEXIST; but the create
		 * options just above also carry FILE_NON_DIRECTORY_FILE
		 * whenever the target is not a directory *now* -- and a
		 * symbolic link may point at something that does not exist
		 * yet, so that is the common case -- and a filesystem is free
		 * to report that mismatch instead of the collision.  The two
		 * ReactOS drivers differ on exactly this ordering: fastfat
		 * rejects the directory first
		 * (drivers/filesystems/fastfat/create.c:1356), its NTFS driver
		 * takes the disposition first
		 * (drivers/filesystems/ntfs/create.c:429).
		 *
		 * STATUS_FILE_IS_A_DIRECTORY can only arise here from an
		 * existing directory at linkpath -- FILE_CREATE means nothing
		 * was opened, and a directory in the path *prefix* is not an
		 * error -- so it proves the [EEXIST] condition exactly, and
		 * EISDIR, which symlink.html does not list at all, would be a
		 * report about NT's create options rather than about POSIX. */
		if (st == STATUS_FILE_IS_A_DIRECTORY) { errno = EEXIST; return -1; }
		return __set_errno_status(st);
	}

	wt = __utf8_to_utf16(target, &tl);
	if (!wt) { NtClose(h); return -1; }
	for (i = 0; i < tl; i++) if (wt[i] == '/') wt[i] = '\\';
	off = relative ? 0 : 4;
	/* Every length in a REPARSE_DATA_BUFFER is a USHORT counting bytes,
	 * and ReparseDataLength -- the largest of them -- covers the target
	 * twice, once as the substitute name and once as the print name.  A
	 * target long enough to overflow it would wrap rather than truncate,
	 * and the link would be created pointing somewhere else entirely, so
	 * the bound is checked before any of them is narrowed.
	 *
	 * SL_HDR/RDB_HDR come from offsetof rather than from the wire
	 * layout's 12 and 8: PathBuffer's distance from the start of the
	 * struct is whatever the compiler in use puts it at, and writing
	 * through the struct needs the allocation sized to that, not to the
	 * on-the-wire figure.  They agree on the NT target (ULONG is 32-bit
	 * there, so RDB_HDR is 8 and SL_HDR is 12); where ULONG is wider
	 * the wire figures are too small and the second memcpy below ran a
	 * WCHAR past the end of the buffer. */
	if (SL_HDR + (off + 2 * tl) * sizeof(WCHAR) > 0xffffu) {
		FILE_DISPOSITION_INFORMATION d = { 1 };
		NtSetInformationFile(h, &io, &d, sizeof d, FileDispositionInformation);
		NtClose(h);
		__free(wt);
		errno = ENAMETOOLONG;
		return -1;
	}
	sz = RDB_HDR + SL_HDR + (off + 2 * tl) * sizeof(WCHAR);
	r = __malloc(sz);
	if (!r) { __free(wt); NtClose(h); return -1; }
	memset(r, 0, sz);
	r->ReparseTag = IO_REPARSE_TAG_SYMLINK;
	r->SymbolicLinkReparseBuffer.Flags = relative ? SYMLINK_FLAG_RELATIVE : 0;
	{
		WCHAR *pb = r->SymbolicLinkReparseBuffer.PathBuffer;
		size_t i;
		if (!relative) { pb[0] = '\\'; pb[1] = '?'; pb[2] = '?'; pb[3] = '\\'; }
		for (i = 0; i < tl; i++) pb[off + i] = wt[i];
		r->SymbolicLinkReparseBuffer.SubstituteNameOffset = 0;
		r->SymbolicLinkReparseBuffer.SubstituteNameLength = (USHORT)((off + tl) * sizeof(WCHAR));
		for (i = 0; i < tl; i++) pb[off + tl + i] = wt[i];
		r->SymbolicLinkReparseBuffer.PrintNameOffset = (USHORT)((off + tl) * sizeof(WCHAR));
		r->SymbolicLinkReparseBuffer.PrintNameLength = (USHORT)(tl * sizeof(WCHAR));
		r->ReparseDataLength = (USHORT)(SL_HDR + (off + 2 * tl) * sizeof(WCHAR));
	}
	st = NtFsControlFile(h, 0, 0, 0, &io, FSCTL_SET_REPARSE_POINT, r, r->ReparseDataLength + (ULONG)RDB_HDR, 0, 0);
	__free(r);
	__free(wt);
	if (!NT_SUCCESS(st)) {
		FILE_DISPOSITION_INFORMATION d = { 1 };
		NtSetInformationFile(h, &io, &d, sizeof d, FileDispositionInformation);
		NtClose(h);
		if (st == STATUS_PRIVILEGE_NOT_HELD || st == STATUS_ACCESS_DENIED) { errno = EPERM; return -1; }
		return __set_errno_status(st);
	}
	NtClose(h);
	return 0;
}

/* ======================================================================
 * ids.c
 * ====================================================================== */

/* NT has one immutable process identity, represented by the user SID in
 * its primary access token.  SAM and Active Directory account SIDs have
 * the shape S-1-5-21-X-Y-Z-RID.  The final subauthority is the account's
 * RID; the preceding subauthorities identify the issuing machine/domain.
 *
 * A RID alone is not a uid: a domain-joined machine can run a local SAM
 * account and an AD account with the same RID.  Use Cygwin's computational
 * mapping so the two namespaces do not alias:
 *
 *     local account domain       0x30000 + RID
 *     machine's primary domain  0x100000 + RID
 *     other/trusted domain      0xfe500000 + RID when its AD
 *                                trustPosixOffset is unavailable
 *
 * The NTLIBC_USE_KERNEL32 build asks LSA for the local and primary domain
 * SIDs and compares the complete domain part.  The default ntdll-only
 * build cannot call LsaQueryInformationPolicy (advapi32), so it uses the
 * system-provided USERDOMAIN/COMPUTERNAME pair: equal means SAM, unequal
 * means the logged-on domain.  If those values are unavailable it chooses
 * SAM, the only account database present on a non-domain machine.
 *
 * Well-known SIDs use Cygwin's documented fixed mappings.  A failure to
 * open/query the token falls back to the old 1000: POSIX reserves no error
 * return from getuid(), so failure must still produce an ordinary uid_t and
 * must not touch errno. */
#define UID_FALLBACK               ((uid_t)1000)
#define SAM_POSIX_OFFSET           ((uid_t)0x00030000)
#define PRIMARY_POSIX_OFFSET       ((uid_t)0x00100000)
#define NOACCESS_POSIX_OFFSET      ((uid_t)0xfe500000)

enum domain_kind {
	DOMAIN_UNKNOWN,
	DOMAIN_SAM,
	DOMAIN_PRIMARY,
	DOMAIN_TRUSTED
};

static int sid_valid(const SID *sid)
{
	return sid && sid->Revision == SID_REVISION &&
	       sid->SubAuthorityCount > 0 &&
	       sid->SubAuthorityCount <= SID_MAX_SUB_AUTHORITIES;
}

static int sid_authority(const SID *sid)
{
	int i;
	for (i = 0; i < 5; i++)
		if (sid->IdentifierAuthority.Value[i]) return -1;
	return sid->IdentifierAuthority.Value[5];
}

/* domain is a SID with the account RID removed. */
static int sid_in_domain(const SID *sid, const SID *domain)
{
	size_t n;

	if (!sid_valid(sid) || !sid_valid(domain) ||
	    sid->SubAuthorityCount != domain->SubAuthorityCount + 1)
		return 0;
	if (sid->Revision != domain->Revision)
		return 0;
	if (memcmp(&sid->IdentifierAuthority, &domain->IdentifierAuthority,
	           sizeof sid->IdentifierAuthority) != 0)
		return 0;
	n = (size_t)domain->SubAuthorityCount * sizeof(ULONG);
	__ownership_readable_span(sid->SubAuthority, n);
	__ownership_readable_span(domain->SubAuthority, n);
	return memcmp(sid->SubAuthority, domain->SubAuthority, n) == 0;
}

#ifdef NTLIBC_USE_KERNEL32
/* advapi32 is reached dynamically and only in the explicitly enabled
 * higher-level-DLL build, as required by CONTRIBUTING.md. */
typedef NTSTATUS (NTAPI *lsa_open_policy_fn)(UNICODE_STRING *,
    OBJECT_ATTRIBUTES *, ACCESS_MASK, HANDLE *);
typedef NTSTATUS (NTAPI *lsa_query_policy_fn)(HANDLE, ULONG, PVOID *);
typedef NTSTATUS (NTAPI *lsa_free_memory_fn)(PVOID);
typedef NTSTATUS (NTAPI *lsa_close_fn)(HANDLE);

typedef struct {
	UNICODE_STRING DomainName;
	SID *DomainSid;
} POLICY_ACCOUNT_DOMAIN_INFO;

typedef struct {
	UNICODE_STRING Name;
	SID *Sid;
} POLICY_PRIMARY_DOMAIN_INFO;

static int advapi_proc(PVOID dll, const char *name, PVOID *proc)
{
	STRING s;
	size_t n = strlen(name);
	if (n > 0xffffu) return 0;
	s.Length = s.MaximumLength = (USHORT)n;
	s.Buffer = (char *)name;
	return NT_SUCCESS(LdrGetProcedureAddress(dll, &s, 0, proc));
}

static enum domain_kind lsa_domain_kind(const SID *sid)
{
	static WCHAR dllname_buf[] = {
		'a','d','v','a','p','i','3','2','.','d','l','l',0
	};
	UNICODE_STRING dllname;
	PVOID dll = 0, p;
	lsa_open_policy_fn open_policy;
	lsa_query_policy_fn query_policy;
	lsa_free_memory_fn free_memory;
	lsa_close_fn close_policy;
	OBJECT_ATTRIBUTES oa;
	POLICY_ACCOUNT_DOMAIN_INFO *account = 0;
	POLICY_PRIMARY_DOMAIN_INFO *primary = 0;
	HANDLE policy = 0;
	enum domain_kind kind = DOMAIN_UNKNOWN;

	RtlInitUnicodeString(&dllname, dllname_buf);
	if (!NT_SUCCESS(LdrLoadDll(0, 0, &dllname, &dll))) return kind;
	if (!advapi_proc(dll, "LsaOpenPolicy", &p)) goto out;
	open_policy = (lsa_open_policy_fn)p;
	if (!advapi_proc(dll, "LsaQueryInformationPolicy", &p)) goto out;
	query_policy = (lsa_query_policy_fn)p;
	if (!advapi_proc(dll, "LsaFreeMemory", &p)) goto out;
	free_memory = (lsa_free_memory_fn)p;
	if (!advapi_proc(dll, "LsaClose", &p)) goto out;
	close_policy = (lsa_close_fn)p;

	memset(&oa, 0, sizeof oa);
	if (!NT_SUCCESS(open_policy(0, &oa, 0x00000001, &policy))) goto out;
	/* POLICY_INFORMATION_CLASS: PrimaryDomain=3, AccountDomain=5. */
	if (!NT_SUCCESS(query_policy(policy, 3, (PVOID *)&primary))) goto close;
	if (!NT_SUCCESS(query_policy(policy, 5, (PVOID *)&account))) goto close;

	/* On a domain controller both policy entries name the AD domain;
	 * primary wins, matching Cygwin's treatment of that case. */
	if (primary->Sid && sid_in_domain(sid, primary->Sid))
		kind = DOMAIN_PRIMARY;
	else if (account->DomainSid && sid_in_domain(sid, account->DomainSid))
		kind = DOMAIN_SAM;
	else
		kind = DOMAIN_TRUSTED;

close:
	if (account) free_memory(account);
	if (primary) free_memory(primary);
	close_policy(policy);
out:
	LdrUnloadDll(dll);
	return kind;
}
#endif

static int ascii_case_equal(const char *a, const char *b)
{
	unsigned char x, y;
	if (!a || !b || !*a || !*b) return 0;
	do {
		x = (unsigned char)*a++;
		y = (unsigned char)*b++;
		if (x >= 'a' && x <= 'z') x -= 'a' - 'A';
		if (y >= 'a' && y <= 'z') y -= 'a' - 'A';
		if (x != y) return 0;
	} while (x);
	return 1;
}

static enum domain_kind current_domain_kind(const SID *sid)
{
	const char *user_domain, *computer;
#ifdef NTLIBC_USE_KERNEL32
	enum domain_kind kind = lsa_domain_kind(sid);
	if (kind != DOMAIN_UNKNOWN) return kind;
#else
	(void)sid;
#endif
	user_domain = getenv("USERDOMAIN");
	computer = getenv("COMPUTERNAME");
	if (user_domain && *user_domain && computer && *computer)
		return ascii_case_equal(user_domain, computer) ?
		       DOMAIN_SAM : DOMAIN_PRIMARY;
	return DOMAIN_SAM;
}

static uid_t sid_uid(const SID *sid)
{
	ULONG rid;
	int authority;
	uid_t uid;

	if (!sid_valid(sid)) return UID_FALLBACK;
	rid = sid->SubAuthority[sid->SubAuthorityCount - 1];
	authority = sid_authority(sid);

	/* SAM, AD and trusted-domain accounts: S-1-5-21-X-Y-Z-RID. */
	if (authority == 5 && sid->SubAuthorityCount == 5 &&
	    sid->SubAuthority[0] == 21) {
		switch (current_domain_kind(sid)) {
		case DOMAIN_PRIMARY: uid = PRIMARY_POSIX_OFFSET + rid; break;
		case DOMAIN_TRUSTED: uid = NOACCESS_POSIX_OFFSET + rid; break;
		default:             uid = SAM_POSIX_OFFSET + rid; break;
		}
		return uid == (uid_t)-1 ? UID_FALLBACK : uid;
	}

	/* Cygwin's fixed mappings for well-known principals. */
	if (authority == 5 &&
	    (sid->SubAuthorityCount == 1 || sid->SubAuthority[0] == 32))
		uid = rid;
	else if (authority == 5 && sid->SubAuthorityCount > 1)
		uid = (uid_t)(0x1000u * sid->SubAuthority[0] + (rid & 0xffffu));
	else if (authority == 16)
		uid = (uid_t)(0x60000u + rid);
	else if (authority >= 0)
		uid = (uid_t)(0x10000u + 0x100u * (unsigned)authority +
		              (rid & 0xffu));
	else
		return UID_FALLBACK;
	return uid == (uid_t)-1 ? UID_FALLBACK : uid;
}

/* NT has no POSIX group identity distinct from the fixed uid model this
 * file's own banner describes -- the same constant getgid()/getegid()
 * already answered directly (src/unistd/ids.c) before this seam
 * existed; only the location moved, not the value. */
gid_t __plat_detect_gid(void)
{
	return 1000;
}

uid_t __plat_detect_uid(void)
{
	union {
		ULONG_PTR align;
		UCHAR bytes[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
	} buf;
	TOKEN_USER *user = (TOKEN_USER *)buf.bytes;
	SID *sid;
	HANDLE token;
	ULONG got = 0;
	NTSTATUS st;
	uintptr_t start, end, sp;
	size_t sidlen;

	st = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &token);
	if (!NT_SUCCESS(st)) return UID_FALLBACK;
	st = NtQueryInformationToken(token, TokenUser, buf.bytes, sizeof buf.bytes,
	                             &got);
	NtClose(token);
	if (!NT_SUCCESS(st) || got > sizeof buf.bytes || got < sizeof(TOKEN_USER))
		return UID_FALLBACK;

	sid = user->User.Sid;
	start = (uintptr_t)buf.bytes;
	end = start + got;
	sp = (uintptr_t)sid;
	if (!sid || sp < start || sp > end || end - sp < 8) return UID_FALLBACK;
	if (!sid_valid(sid)) return UID_FALLBACK;
	sidlen = 8 + (size_t)sid->SubAuthorityCount * sizeof(ULONG);
	if (sidlen > end - sp) return UID_FALLBACK;
	return sid_uid(sid);
}

/* Which ids exist at all is asked with a named, idempotent, cross-process
 * event keyed by pid: a process which becomes its own group leader
 * publishes that one bit of cross-process state this way.  NT has no
 * query for POSIX pgids, but this is enough to distinguish the
 * transition setpgrp()/setsid() make in another process without
 * inventing a central process registry -- see ids.c's banner. */
static HANDLE pgid_event;
static pid_t pgid_event_owner;

static void pgid_event_name(pid_t pid, WCHAR name[48], UNICODE_STRING *us)
{
	static const char prefix[] = "\\BaseNamedObjects\\ntlibc-pgrp.";
	int i = 0;

	for (; prefix[i]; i++) name[i] = (unsigned char)prefix[i];
	i = __nt_append_hex32(name, i, (unsigned)pid);
	name[i] = 0;
	us->Buffer = name;
	if ((size_t)i > __US_MAX_WCHARS) {
		us->Length = us->MaximumLength = 0;
		return;
	}
	us->Length = (USHORT)(i * sizeof(WCHAR));
	us->MaximumLength = (USHORT)(us->Length + sizeof(WCHAR));
}

void __plat_pgrp_publish_self(pid_t self)
{
	OBJECT_ATTRIBUTES oa;
	UNICODE_STRING us;
	WCHAR name[48];
	HANDLE h;

	if (pgid_event && pgid_event_owner == self) return;
	/* fork() copies the handle value in memory, but this private event is
	 * not inheritable.  A different owner means the value is stale. */
	pgid_event = 0;
	pgid_event_name(self, name, &us);
	InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, 0, 0);
	if (NT_SUCCESS(NtCreateEvent(&h, EVENT_ALL_ACCESS, &oa,
	                             NotificationEvent, FALSE))) {
		pgid_event = h;
		pgid_event_owner = self;
	}
}

int __plat_pgrp_is_leader(pid_t pid)
{
	OBJECT_ATTRIBUTES oa;
	UNICODE_STRING us;
	WCHAR name[48];
	HANDLE h;
	NTSTATUS st;

	pgid_event_name(pid, name, &us);
	InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, 0, 0);
	st = NtCreateEvent(&h, EVENT_ALL_ACCESS, &oa, NotificationEvent, FALSE);
	if (!NT_SUCCESS(st)) return 0;
	NtClose(h);
	return st == STATUS_OBJECT_NAME_EXISTS;
}

int __plat_process_exists(pid_t p)
{
	HANDLE h;
	NTSTATUS st;
	OBJECT_ATTRIBUTES oa;
	CLIENT_ID cid;

	InitializeObjectAttributes(&oa, 0, 0, 0, 0);
	cid.UniqueProcess = (HANDLE)(ULONG_PTR)p;
	cid.UniqueThread = 0;
	st = NtOpenProcess(&h, PROCESS_QUERY_LIMITED_INFORMATION, &oa, &cid);
	if (!NT_SUCCESS(st)) return st == STATUS_ACCESS_DENIED;
	NtClose(h);
	return 1;
}

/* There is nothing for the chown family to set -- NT has no POSIX owner
 * or group, and st_uid/st_gid report this process's current IDs -- but
 * "there is no ownership to change" is not "there is no path to
 * resolve".  So the path is resolved and the object opened for
 * FILE_READ_ATTRIBUTES, which is exactly the evidence chown.html's
 * shall-fail clauses ask for and nothing more; the handle is closed
 * again without a write of any kind.  uid/gid are accepted (the
 * plat_unistd.h contract every backend shares) and ignored, exactly as
 * before this function grew them: there is still nothing on this
 * backend to set them to.
 *
 * __ntpath_at() produces the empty-path [ENOENT], the dirfd [EBADF]/
 * [ENOTDIR] and the path-prefix [ENOTDIR] itself, so only the final open
 * is left to this function.  FILE_OPEN_FOR_BACKUP_INTENT, and neither
 * FILE_DIRECTORY_FILE nor FILE_NON_DIRECTORY_FILE, so that the call
 * works on a directory and on a regular file alike. */
int __plat_chown(int dirfd, const char *path, uid_t uid, gid_t gid, int flags)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	OBJECT_ATTRIBUTES *oa;
	HANDLE h;
	NTSTATUS st;
	ULONG options;

	(void)uid; (void)gid;
	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	options = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT |
		(flags & AT_SYMLINK_NOFOLLOW ? FILE_OPEN_REPARSE_POINT : 0);
	oa = &np.oa;
	st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, oa, &io, FILE_SHARE_VALID_FLAGS, options);
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	NtClose(h);
	return 0;
}

/* fchown(): src/unistd/ids.c's own front door already turned `f` into a
 * validated handle via __fd_get() before calling here, which is all
 * fchown.html's own [EBADF] asks for -- same "nothing to set, but
 * resolution still matters" split as __plat_chown() above, minus any
 * path resolution of its own since a handle needs none. */
int __plat_fchown(__plat_handle_t h, uid_t uid, gid_t gid)
{
	(void)h; (void)uid; (void)gid;
	return 0;
}

/* ======================================================================
 * getentropy.c
 * ====================================================================== */

/* No ntdll export answers "give me random bytes" at all: ntdll's own
 * RtlRandom/RtlRandomEx family are non-cryptographic PRNGs, documented
 * as unsuitable for security purposes.  A real source exists --
 * BCryptGenRandom, CNG's documented, still-supported entropy call --
 * but it lives in bcrypt.dll, not ntdll, which is exactly the situation
 * NTLIBC_USE_KERNEL32 exists for (configure --help: "allow the few
 * places that have no pure-NTDLL way to do something to fall back to
 * kernel32"; "kernel32" there is this project's name for the whole
 * class of higher-level-DLL fallbacks, not literally kernel32.dll only
 * -- src/unistd/ids.c's own lsa_domain_kind() above already loads
 * advapi32.dll the identical way, under the identical flag).  Reached
 * with LdrLoadDll()/LdrGetProcedureAddress() rather than linked against
 * bcrypt's import library, same reasoning as every other
 * NTLIBC_USE_KERNEL32 fallback in this tree (see src/signal/signal.c's
 * install_ctrl_handler() banner): a binary built with this flag still
 * only *links* against ntdll, and only pulls bcrypt.dll into its
 * address space if it actually runs on a build where this was
 * requested. Without the flag, there is no fallback to reach at all,
 * so __plat_getentropy() is not compiled in and the front door
 * (src/unistd/getentropy.c) reports ENOSYS itself. */
#ifdef NTLIBC_USE_KERNEL32
typedef NTSTATUS (NTAPI *bcrypt_gen_random_fn)(PVOID, unsigned char *, ULONG, ULONG);

/* BCRYPT_USE_SYSTEM_PREFERRED_RNG (bcrypt.h): use the system-preferred
 * RNG algorithm rather than an explicit BCRYPT_ALG_HANDLE, so no
 * BCryptOpenAlgorithmProvider()/BCryptCloseAlgorithmProvider() pairing
 * is needed around this -- a single call is the whole of this
 * function. */
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002

int __plat_getentropy(void *buf, size_t buflen)
{
	UNICODE_STRING dllname;
	PVOID dll, proc;
	bcrypt_gen_random_fn gen_random;
	STRING procname;
	NTSTATUS st;

	RtlInitUnicodeString(&dllname, L"bcrypt.dll");
	if (!NT_SUCCESS(LdrLoadDll(0, 0, &dllname, &dll))) { errno = ENOSYS; return -1; }

	procname.Buffer = "BCryptGenRandom";
	procname.Length = procname.MaximumLength = sizeof "BCryptGenRandom" - 1;
	if (!NT_SUCCESS(LdrGetProcedureAddress(dll, &procname, 0, &proc))) {
		LdrUnloadDll(dll);
		errno = ENOSYS;
		return -1;
	}
	gen_random = (bcrypt_gen_random_fn)proc;

	st = gen_random(0, (unsigned char *)buf, (ULONG)buflen, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	LdrUnloadDll(dll);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}
#else
int __plat_getentropy(void *buf, size_t buflen)
{
	(void)buf;
	(void)buflen;
	errno = ENOSYS;
	return -1;
}
#endif

// NOLINTEND(misc-include-cleaner)
