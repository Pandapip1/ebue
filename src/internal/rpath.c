/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Implementation of the $ORIGIN-relative DLL search declared in
 * include/ntlibc/rpath.h -- see that file for the design, search order
 * and threat model, and include/ntlibc/delayload.h for the delay-load
 * mechanism built on top of it.
 *
 * This file only ever runs code when something calls
 * ntlibc_rpath_load()/_sym() -- which happens only from
 * ntlibc_delayLoadHelper2() (src/internal/delayload.c), which itself
 * only runs on the first call through a generated delay-load stub.
 * Nothing here is reached from crt1.c or any other startup path, so a
 * program that never delay-loads anything never pays for it.
 *
 * The one loading primitive used is LdrLoadDll()/LdrGetProcedureAddress()
 * (both ntdll exports, declared in nt.h) -- the same pair
 * src/signal/signal.c already uses to reach kernel32's
 * SetConsoleCtrlHandler at runtime. Every candidate path handed to
 * LdrLoadDll here is already fully qualified (built from the image
 * directory or an explicit absolute __rpath entry), so the NT loader's
 * own search order -- which does include the current working directory
 * on many configurations -- is never invoked by this file.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "libc.h"
#include "ntlibc/rpath.h"

/* ---- the image's own directory ($ORIGIN) ------------------------------ */

/* __progname_full (crt1.c) is the image path as ImagePathName gave it --
 * a native, backslash-separated path.  Computed once, on first use, and
 * cached: this is the only state this file keeps beyond the per-call
 * error record below. */
static char *image_dir(void)
{
	static char *dir;
	static int done;
	size_t n, i;

	if (done) return dir;
	done = 1;

	if (!__progname_full) return dir; /* NULL: caller sees the same as OOM */

	n = strlen(__progname_full);
	for (i = n; i > 0 && __progname_full[i-1] != '\\' && __progname_full[i-1] != '/'; i--) ;
	if (i == 0) {
		/* No separator at all -- nothing sensible to strip; treat the
		 * image as living in "." rather than guessing. */
		dir = __malloc(2);
		if (dir) { dir[0] = '.'; dir[1] = 0; }
		return dir;
	}
	/* i is the index just past the last separator; strip it too, unless
	 * that separator is the one after a bare drive letter ("C:\"),
	 * which has to stay to still name the root directory. */
	if (i > 1 && !(i == 3 && __progname_full[1] == ':')) i--;
	dir = __malloc(i + 1);
	if (dir) { memcpy(dir, __progname_full, i); dir[i] = 0; }
	return dir;
}

static int is_absolute(const char *p)
{
	if (!p[0]) return 0;
	if (p[0] == '/' || p[0] == '\\') return 1;
	return ((p[0] | 0x20) >= 'a' && (p[0] | 0x20) <= 'z') && p[1] == ':';
}

static int has_path_component(const char *p)
{
	for (; *p; p++)
		if (*p == '/' || *p == '\\') return 1;
	return 0;
}

/* dir "\" tail, with every '/' normalised to '\\'. Malloc'd; NULL on OOM. */
static char *join(const char *dir, const char *tail)
{
	size_t dl = strlen(dir), tl = strlen(tail), i;
	char *p = __malloc(dl + 1 + tl + 1);
	if (!p) return 0;
	memcpy(p, dir, dl);
	p[dl] = '\\';
	memcpy(p + dl + 1, tail, tl);
	p[dl + 1 + tl] = 0;
	for (i = 0; i < dl + 1 + tl; i++)
		if (p[i] == '/') p[i] = '\\';
	return p;
}

/* ---- last-failure record ----------------------------------------------- */

static struct {
	int valid;
	NTSTATUS status;
	char what[1024];
} last_err;

static void set_err(NTSTATUS st, const char *what)
{
	last_err.valid = 1;
	last_err.status = st;
	snprintf(last_err.what, sizeof last_err.what, "%s", what);
}

const char *ntlibc_rpath_error(void)
{
	static char buf[1200];
	const char *reason;

	if (!last_err.valid) return "no error";
	switch (last_err.status) {
	case STATUS_DLL_NOT_FOUND:
	case STATUS_OBJECT_NAME_NOT_FOUND:
		reason = "DLL not found";
		break;
	case STATUS_ENTRYPOINT_NOT_FOUND:
		reason = "symbol not found";
		break;
	case STATUS_INVALID_IMAGE_FORMAT:
	case STATUS_INVALID_IMAGE_NOT_MZ:
	case STATUS_INVALID_IMAGE_WIN_32:
	case STATUS_INVALID_IMAGE_WIN_64:
		reason = "not a valid image for this architecture";
		break;
	case STATUS_NO_MEMORY:
		reason = "out of memory";
		break;
	case STATUS_NAME_TOO_LONG:
		reason = "path too long";
		break;
	default:
		reason = 0;
	}
	if (reason)
		snprintf(buf, sizeof buf, "%s: %s (NTSTATUS 0x%08lx)", last_err.what, reason, (unsigned long)last_err.status);
	else
		snprintf(buf, sizeof buf, "%s: NTSTATUS 0x%08lx", last_err.what, (unsigned long)last_err.status);
	return buf;
}

/* ---- loading ------------------------------------------------------------ */

static NTSTATUS try_load(const char *path, PVOID *handle)
{
	WCHAR *w;
	size_t wn;
	UNICODE_STRING us;
	NTSTATUS st;

	w = __utf8_to_utf16(path, &wn);
	if (!w) return STATUS_NO_MEMORY;
	if (wn > __US_MAX_WCHARS) { __free(w); return STATUS_NAME_TOO_LONG; }
	us.Buffer = w;
	us.Length = (USHORT)(wn * sizeof(WCHAR));
	us.MaximumLength = (USHORT)(us.Length + sizeof(WCHAR));
	st = LdrLoadDll(NULL, NULL, &us, handle);
	__free(w);
	return st;
}

ntlibc_dll_t *ntlibc_rpath_load(const char *dllname)
{
	PVOID handle;
	NTSTATUS st;
	char *path;

	if (!dllname || !*dllname) { set_err(STATUS_OBJECT_NAME_NOT_FOUND, ""); return 0; }

	if (has_path_component(dllname)) {
		if (is_absolute(dllname)) {
			path = join("", dllname); /* normalises slashes; dir="" leaves a leading '\\' */
			if (path && path[0] == '\\') memmove(path, path + 1, strlen(path));
		} else {
			char *dir = image_dir();
			if (!dir) { set_err(STATUS_NO_MEMORY, dllname); return 0; }
			path = join(dir, dllname);
		}
		if (!path) { set_err(STATUS_NO_MEMORY, dllname); return 0; }
		st = try_load(path, &handle);
		if (!NT_SUCCESS(st)) { set_err(st, path); __free(path); return 0; }
		__free(path);
		return handle;
	}

	{
		const char *const *entry = __rpath;
		int tried = 0;
		for (; entry && *entry; entry++) {
			char *dir;
			char *full;

			tried = 1;
			if (is_absolute(*entry)) {
				dir = __malloc(strlen(*entry) + 1);
				if (dir) strcpy(dir, *entry);
			} else {
				char *base = image_dir();
				dir = base ? join(base, *entry) : 0;
			}
			if (!dir) { set_err(STATUS_NO_MEMORY, dllname); return 0; }
			full = join(dir, dllname);
			__free(dir);
			if (!full) { set_err(STATUS_NO_MEMORY, dllname); return 0; }

			st = try_load(full, &handle);
			if (NT_SUCCESS(st)) { __free(full); return handle; }
			set_err(st, full); /* overwritten by a later entry's failure, if any */
			__free(full);
		}
		if (!tried) set_err(STATUS_DLL_NOT_FOUND, dllname);
		return 0;
	}
}

void *ntlibc_rpath_sym(ntlibc_dll_t *dll, const char *symbol)
{
	ANSI_STRING name;
	PVOID proc;
	NTSTATUS st;

	if (!dll || !symbol) { set_err(STATUS_ENTRYPOINT_NOT_FOUND, symbol ? symbol : ""); return 0; }

	name.Buffer = (char *)symbol;
	{
		size_t l = strlen(symbol);
		if (l > 0xffffu) { set_err(STATUS_NAME_TOO_LONG, symbol); return 0; }
		name.Length = name.MaximumLength = (USHORT)l;
	}
	st = LdrGetProcedureAddress(dll, &name, 0, &proc);
	if (!NT_SUCCESS(st)) { set_err(st, symbol); return 0; }
	return proc;
}

_Noreturn void ntlibc_rpath_fail(const char *dllfile, const char *symbol)
{
	fprintf(stderr, "%s: delay-load of %s!%s failed: %s\n",
	        __progname ? __progname : "?", dllfile, symbol, ntlibc_rpath_error());
	abort();
}
