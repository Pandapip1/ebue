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

void *__fd_runtime_data(size_t *len);

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
		if (!nb) return -1;
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

static WCHAR *build_cmdline(char *const argv[])
{
	WCHAR *buf = 0;
	size_t len = 0, cap = 0;
	int i;
	for (i = 0; argv[i]; i++) {
		size_t wl;
		WCHAR *w = __utf8_to_utf16(argv[i], &wl);
		int rc;
		if (!w) { free(buf); return 0; }
		rc = append_arg(&buf, &len, &cap, w);
		__free(w);
		if (rc < 0) { free(buf); return 0; }
	}
	if (!buf) { buf = malloc(sizeof(WCHAR)); if (buf) buf[0] = 0; return buf; }
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
		WCHAR *w = __utf8_to_utf16(envp[i], &wl);
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
	UNICODE_STRING imageDos, cmdLine, cur;
	WCHAR *wcmd = 0, *wenv = 0, *wimage = 0;
	WCHAR curbuf[4096];
	void *runtime = 0;
	size_t runtime_len = 0;
	NTSTATUS st;
	int pid = -1, i;
	ULONG curlen;

	if (__ntpath(path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;

	wimage = __utf8_to_utf16(path, 0);
	{ size_t k; if (wimage) for (k = 0; wimage[k]; k++) if (wimage[k] == '/') wimage[k] = '\\'; }
	wcmd = build_cmdline(argv);
	wenv = build_env_block(envp ? envp : __environ);
	if (!wimage || !wcmd || !wenv) { errno = ENOMEM; goto out; }

	RtlInitUnicodeString(&imageDos, wimage);
	cmdLine.Buffer = wcmd;
	cmdLine.Length = (USHORT)(wcslen_(wcmd) * sizeof(WCHAR));
	cmdLine.MaximumLength = cmdLine.Length + sizeof(WCHAR);
	curlen = RtlGetCurrentDirectory_U(sizeof curbuf, curbuf);
	cur.Buffer = curbuf;
	cur.Length = (USHORT)curlen;
	cur.MaximumLength = sizeof curbuf;

	st = RtlCreateProcessParametersEx(&pp, &imageDos, 0, &cur, &cmdLine, wenv, 0, 0, 0, 0,
	                                  RTL_USER_PROC_PARAMS_NORMALIZED);
	if (!NT_SUCCESS(st)) { __set_errno_status(st); goto out; }

	/* Standard handles and the inheritable descriptor table. */
	runtime = __fd_runtime_data(&runtime_len);
	{
		struct __fd *f0 = __fd_get(0), *f1 = __fd_get(1), *f2 = __fd_get(2);
		errno = 0;
		pp->StandardInput = f0 ? f0->h : 0;
		pp->StandardOutput = f1 ? f1->h : 0;
		pp->StandardError = f2 ? f2->h : 0;
		pp->WindowFlags |= STARTF_USESTDHANDLES;
	}
	if (runtime && runtime_len) {
		/* RuntimeData points into the block; the child copies it. */
		pp->RuntimeData.Buffer = (PWSTR)runtime;
		pp->RuntimeData.Length = (USHORT)runtime_len;
		pp->RuntimeData.MaximumLength = (USHORT)runtime_len;
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
		/* The table is full; the process still runs.  waitpid(pid)
		 * reopens it by pid (src/process/wait.c) and verifies it is our
		 * child; waitpid(-1)/wait() will not see it. */
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
