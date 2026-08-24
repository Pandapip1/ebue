/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Starting another program.
 *
 * Windows starts a process from an image file, not by copying a running
 * one, so __spawn is what execve and posix_spawn are built on: it builds
 * the child's process parameters (command line, environment, current
 * directory, and the inherited descriptor table), creates the process
 * suspended with RtlCreateUserProcess, and resumes it.
 *
 * Two things about handing a child its file descriptors are not obvious
 * and are done here because otherwise redirection fails silently:
 *
 *   - Only handles marked OBJ_INHERIT are copied into the child, so the
 *     ones the child should keep are duplicated inheritable first (that
 *     is what __fd_runtime_data does), and the numbers are written into
 *     the RuntimeData block the child's crt1 reads back.
 *
 *   - The child's own startup would overwrite StandardInput/Output/Error
 *     in its process parameters with fresh console handles unless
 *     STARTF_USESTDHANDLES is set in WindowFlags.  RtlCreateUserProcess
 *     offers no way to set it, so it is written into the parameter block
 *     directly.  (This is the same decision ReactOS's kernel32
 *     SetUpHandles makes; it was measured to be necessary on Windows 11.)
 *
 * The command line is built by the quoting rules CommandLineToArgvW and
 * every Windows C runtime agree on, so that an argument with spaces,
 * quotes or backslashes survives the round trip into the child's argv.
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "libc.h"

/* Append one argument to a UTF-16 command-line buffer, quoting it if it
 * contains whitespace, a quote, or is empty. */
static int append_arg(WCHAR **buf, size_t *len, size_t *cap, const WCHAR *arg)
{
	size_t n = 0, i;
	int need_quote = arg[0] == 0;
	for (i = 0; arg[i]; i++) if (arg[i] == ' ' || arg[i] == '\t' || arg[i] == '"' || arg[i] == '\n' || arg[i] == '\v') need_quote = 1;
	n = i;
	/* worst case: quotes + every char doubled (backslashes) + a space */
	if (*len + 2 * n + 4 >= *cap) {
		size_t nc = (*cap + 2 * n + 16) * 2;
		WCHAR *nb = realloc(*buf, nc * sizeof(WCHAR));
		if (!nb) { errno = ENOMEM; return -1; }
		*buf = nb; *cap = nc;
	}
	if (*len) (*buf)[(*len)++] = ' ';
	if (!need_quote) {
		memcpy(*buf + *len, arg, n * sizeof(WCHAR));
		*len += n;
		return 0;
	}
	(*buf)[(*len)++] = '"';
	for (i = 0; i < n; i++) {
		size_t nb = 0;
		while (i < n && arg[i] == '\\') { nb++; i++; }
		if (i == n) {
			size_t k; for (k = 0; k < nb * 2; k++) (*buf)[(*len)++] = '\\';
			break;
		} else if (arg[i] == '"') {
			size_t k; for (k = 0; k < nb * 2 + 1; k++) (*buf)[(*len)++] = '\\';
			(*buf)[(*len)++] = arg[i];
		} else {
			size_t k; for (k = 0; k < nb; k++) (*buf)[(*len)++] = '\\';
			(*buf)[(*len)++] = arg[i];
		}
	}
	(*buf)[(*len)++] = '"';
	return 0;
}

/* Append argv[0].  The program name is *not* read back by the rules
 * append_arg encodes for: every parser -- Wine's CommandLineToArgvW
 * (dlls/shcore/main.c, "The executable path ends at the next quote, no
 * matter what"), the Microsoft C runtimes, and this library's own
 * crt1.c split_cmdline -- treats it specially.  Backslashes in it are
 * always literal and never escape anything; a quote only turns the
 * "whitespace is literal" state on and off and is otherwise dropped.
 *
 * So the encoding is: emit the name as it stands, wrapped in one pair of
 * quotes if it is empty or contains a space or a tab.  Doubling
 * backslashes, which append_arg would do, would arrive doubled.
 *
 * A name containing a quote has no encoding at all under these rules --
 * the quote it would need to protect itself is the same character that
 * ends the name -- so that is refused with EINVAL rather than passed on
 * to be silently mangled into a different name and a stray argument. */
static int append_prog(WCHAR **buf, size_t *len, size_t *cap, const WCHAR *arg)
{
	size_t n, i;
	int need_quote = arg[0] == 0;
	for (i = 0; arg[i]; i++) {
		if (arg[i] == '"') { errno = EINVAL; return -1; }
		if (arg[i] == ' ' || arg[i] == '\t') need_quote = 1;
	}
	n = i;
	if (*len + n + 4 >= *cap) {
		size_t nc = (*cap + n + 16) * 2;
		WCHAR *nb = realloc(*buf, nc * sizeof(WCHAR));
		if (!nb) { errno = ENOMEM; return -1; }
		*buf = nb; *cap = nc;
	}
	if (need_quote) (*buf)[(*len)++] = '"';
	memcpy(*buf + *len, arg, n * sizeof(WCHAR));
	*len += n;
	if (need_quote) (*buf)[(*len)++] = '"';
	return 0;
}

static WCHAR *build_cmdline(char *const argv[])
{
	WCHAR *buf = 0;
	size_t len = 0, cap = 0;
	int i;
	for (i = 0; argv[i]; i++) {
		size_t wl;
		WCHAR *w = __utf8_to_utf16(argv[i], &wl);
		int rc;
		if (!w) { errno = ENOMEM; free(buf); return 0; }
		rc = i ? append_arg(&buf, &len, &cap, w) : append_prog(&buf, &len, &cap, w);
		__free(w);
		if (rc < 0) { free(buf); return 0; }   /* errno set by the appender */
	}
	if (!buf) {
		buf = malloc(sizeof(WCHAR));
		if (!buf) { errno = ENOMEM; return 0; }
		buf[0] = 0;
		return buf;
	}
	buf[len] = 0;
	return buf;
}

/* The environment as one UTF-16 block of NAME=VALUE\0 ... \0\0. */
static WCHAR *build_env_block(char *const envp[])
{
	size_t cap = 256, len = 0;
	WCHAR *blk = malloc(cap * sizeof(WCHAR));
	int i;
	if (!blk) return 0;
	for (i = 0; envp && envp[i]; i++) {
		size_t wl;
		WCHAR *w;
		/* Two shapes of entry cannot be expressed in a Windows
		 * environment block at all, and are dropped rather than passed
		 * on, because handing either to the child is worse than losing
		 * it:
		 *
		 *   - A zero-length entry would be written as an empty string,
		 *     which is exactly the block's terminator, so everything
		 *     after it would be invisible to the child.
		 *
		 *   - An entry with no '=' anywhere has no name/value split, so
		 *     there is nothing for it to mean.  Windows rejects that
		 *     shape outright: kernelbase's SetEnvironmentStringsW walks
		 *     the block and fails with ERROR_INVALID_PARAMETER for any
		 *     entry whose wcschr(p, '=') is NULL (Wine
		 *     dlls/kernelbase/process.c; the rule is matched against
		 *     real Windows by dlls/kernel32/tests/environ.c,
		 *     test_SetEnvironmentStrings, which asserts L"testenv\0" is
		 *     refused with exactly that error).  Passing one through
		 *     failed the entire spawn on real NT -- EINVAL here, since
		 *     ERROR_INVALID_PARAMETER is DOS error 87 and
		 *     src/internal/errno.c maps it that way -- while Wine let it
		 *     through, so it only ever showed up on Windows.
		 *
		 * An entry that merely *starts* with '=' is a different thing
		 * and is kept: "=C:=C:\dir" is Windows' own shape for a
		 * per-drive current directory, its name is "=C:", and ntdll
		 * accepts a name like that (Wine dlls/ntdll/tests/env.c,
		 * test_RtlSetEnvironmentVariable: setting L"=too" succeeds,
		 * while L"me=too" -- an '=' inside the name -- is
		 * STATUS_INVALID_PARAMETER).  Dropping one would silently change
		 * a child's per-drive working directories.  crt1.c's
		 * build_environ hands them back unchanged. */
		if (!envp[i][0] || !strchr(envp[i], '=')) continue;
		w = __utf8_to_utf16(envp[i], &wl);
		if (!w) { free(blk); return 0; }
		if (len + wl + 2 >= cap) {
			WCHAR *nb;
			while (len + wl + 2 >= cap) cap *= 2;
			nb = realloc(blk, cap * sizeof(WCHAR));
			if (!nb) { __free(w); free(blk); return 0; }
			blk = nb;
		}
		memcpy(blk + len, w, wl * sizeof(WCHAR));
		len += wl;
		blk[len++] = 0;
		__free(w);
	}
	blk[len++] = 0;   /* terminating empty string */
	return blk;
}

int __spawn(const char *path, char *const argv[], char *const envp[])
{
	struct __ntpath np;
	RTL_USER_PROCESS_PARAMETERS *pp = 0;
	RTL_USER_PROCESS_INFORMATION info;
	UNICODE_STRING imageDos, cmdLine, cur, runtimeUS;
	WCHAR *wcmd = 0, *wenv = 0, *wimage = 0;
	WCHAR curbuf[4096];
	void *runtime = 0;
	size_t runtime_len = 0;
	NTSTATUS st;
	int pid = -1, i;
	ULONG curlen;
	size_t cmdlen;

	if (__ntpath(path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;

	wimage = __utf8_to_utf16(path, 0);
	if (!wimage) { errno = ENOMEM; goto out; }
	{ size_t k; for (k = 0; wimage[k]; k++) if (wimage[k] == '/') wimage[k] = '\\'; }
	wcmd = build_cmdline(argv);
	if (!wcmd) goto out;   /* errno set by build_cmdline */
	wenv = build_env_block(envp ? envp : __environ);
	if (!wenv) { errno = ENOMEM; goto out; }

	/* Everything below goes into a UNICODE_STRING, whose Length is a
	 * USHORT counting *bytes*.  A longer string does not truncate, it
	 * wraps: the child would be handed a random prefix of its own
	 * command line and split that.  So each length is checked against
	 * what the field can hold before it is narrowed. */
	cmdlen = wcslen_(wcmd);
	if (cmdlen > __US_MAX_WCHARS) { errno = E2BIG; goto out; }

	RtlInitUnicodeString(&imageDos, wimage);
	cmdLine.Buffer = wcmd;
	cmdLine.Length = (USHORT)(cmdlen * sizeof(WCHAR));
	cmdLine.MaximumLength = (USHORT)(cmdLine.Length + sizeof(WCHAR));
	curlen = RtlGetCurrentDirectory_U(sizeof curbuf, curbuf);
	/* RtlGetCurrentDirectory_U reports the size it needed when the buffer
	 * was too small, so a long enough directory would leave curlen past
	 * the end of curbuf as well as past a USHORT. */
	if (curlen > sizeof curbuf - sizeof(WCHAR)) { errno = ENAMETOOLONG; goto out; }
	cur.Buffer = curbuf;
	cur.Length = (USHORT)curlen;
	cur.MaximumLength = sizeof curbuf;

	/* The inheritable descriptor table, built *before* the parameters
	 * block so it can be handed to RtlCreateProcessParametersEx as the
	 * RuntimeInfo argument and packed INTO the block, the same way
	 * cmdLine, cur and wenv are.  RTL_USER_PROCESS_PARAMETERS crosses
	 * into the child as an uninterpreted blob -- Windows copies the
	 * block's own bytes, full stop, it does not chase any pointer a
	 * field holds to fetch data from outside it (ReactOS's
	 * RtlCreateProcessParameters/CreateProcessInternalW, which mirror
	 * that real behaviour exactly, only ever reach memory the block
	 * itself contains: sdk/lib/rtl/ppb.c RtlpCopyParameterString copies
	 * each string in at creation time, and
	 * dll/win32/kernel32/client/proc.c's CreateProcessInternalW writes
	 * just the block, `ProcessParameters->Length` bytes, with
	 * NtWriteVirtualMemory).  A field set *after* creation to point
	 * outside the block -- which is what this code used to do here --
	 * is not a "pointer" as far as that copy is concerned; it is 8
	 * bytes of parent virtual address that the child inherits verbatim
	 * and then dereferences as if it meant something in the child's own
	 * address space.  It usually does, because two runs of the same
	 * binary tend to get the same heap layout, which is exactly why
	 * this was intermittent rather than always broken.  (Wine's
	 * emulation of NtCreateUserProcess, dlls/ntdll/unix/env.c
	 * create_startup_info/append_string, happens to re-marshal every
	 * such field by value regardless of where it points, so this bug is
	 * invisible under Wine; that is Wine filling a gap in its own
	 * userspace reimplementation; on real Windows nothing does that.)
	 *
	 * Its length is bounded by the descriptor table -- at most
	 * sizeof(int) + FD_MAX * (1 + sizeof(HANDLE)) bytes, an order of
	 * magnitude below what a USHORT holds -- so unlike the command line
	 * this narrowing cannot wrap. */
	runtime = __fd_runtime_data(&runtime_len);
	runtimeUS.Buffer = (PWSTR)runtime;
	runtimeUS.Length = (USHORT)runtime_len;
	runtimeUS.MaximumLength = (USHORT)runtime_len;

	st = RtlCreateProcessParametersEx(&pp, &imageDos, 0, &cur, &cmdLine, wenv, 0, 0, 0,
	                                  runtime ? &runtimeUS : 0,
	                                  RTL_USER_PROC_PARAMS_NORMALIZED);
	if (!NT_SUCCESS(st)) { __set_errno_status(st); goto out; }

	/* Standard handles: HANDLE values stored by *value* in the block,
	 * unlike RuntimeData above.  A HANDLE crosses into the child through
	 * NT's ordinary handle-inheritance mechanism (RtlCreateUserProcess
	 * is called below with inherit=TRUE, and __fd_runtime_data just
	 * marked every non-cloexec handle OBJ_INHERIT) which preserves the
	 * numeric value in the child's own handle table; it is not an
	 * address that has to mean the same thing in two address spaces, so
	 * setting these fields after creation is fine. */
	{
		struct __fd *f0 = __fd_get(0), *f1 = __fd_get(1), *f2 = __fd_get(2);
		errno = 0;
		/* A close-on-exec standard descriptor is not the child's to have,
		 * and its handle is not inheritable anyway, so pass nothing. */
		if (f0 && (f0->flags & O_CLOEXEC)) f0 = 0;
		if (f1 && (f1->flags & O_CLOEXEC)) f1 = 0;
		if (f2 && (f2->flags & O_CLOEXEC)) f2 = 0;
		pp->StandardInput = f0 ? f0->h : 0;
		pp->StandardOutput = f1 ? f1->h : 0;
		pp->StandardError = f2 ? f2->h : 0;
		pp->WindowFlags |= STARTF_USESTDHANDLES;
	}

	memset(&info, 0, sizeof info);
	info.Length = sizeof info;
	st = RtlCreateUserProcess(&np.nt, OBJ_CASE_INSENSITIVE, pp, 0, 0, 0, TRUE, 0, 0, &info);
	if (!NT_SUCCESS(st)) {
		if (st == STATUS_OBJECT_NAME_NOT_FOUND || st == STATUS_OBJECT_PATH_NOT_FOUND) errno = ENOENT;
		else if (st == STATUS_INVALID_IMAGE_FORMAT || st == STATUS_INVALID_IMAGE_NOT_MZ) errno = ENOEXEC;
		else __set_errno_status(st);
		goto out;
	}

	NtResumeThread(info.Thread, 0);
	NtClose(info.Thread);
	pid = (int)(ULONG_PTR)info.ClientId.UniqueProcess;
	if (__child_add(pid, info.Process) < 0) {
		/* The table grows on demand (src/process/children.c), so this
		 * only happens when the heap is exhausted.  Degrade rather than
		 * fail the spawn: the process still runs, but it is unwaitable
		 * -- waitpid() only ever consults the table (src/process/wait.c
		 * used to reopen the pid instead, which was wrong; see the
		 * comment there). */
		NtClose(info.Process);
	}

out:
	if (pp) RtlDestroyProcessParameters(pp);
	__ntpath_free(&np);
	if (wimage) __free(wimage);
	if (wcmd) free(wcmd);
	if (wenv) free(wenv);
	if (runtime) __free(runtime);
	(void)i;
	return pid;
}
