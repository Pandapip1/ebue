/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)

/* getcwd returns the DOS form, C:\dir, with backslashes turned into
 * forward slashes so that programs that split paths on '/' (which is
 * most of them) keep working.  A trailing slash is removed except at a
 * drive root. */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "libc.h"

withtok(heap_allocated)
char *getcwd(char *buf withtok(heap_allocated), size_t size)
{
	WCHAR w[4096];
	char tmp[4096 * 3];
	ULONG n;
	size_t i, len;
	int r;
	int vfs = __vfs_cwd_get();
	if (vfs == __VFS_ROOT || vfs == __VFS_DEV) {
		const char *path = vfs == __VFS_ROOT ? "/" : "/dev";
		len = strlen(path);
		if (!buf) {
			if (!size) size = len + 1;
			if (len + 1 > size) { errno = ERANGE; return 0; }
			buf = malloc(size);
			if (!buf) return 0;
		} else if (!size) { errno = EINVAL; return 0; }
		else if (len + 1 > size) { errno = ERANGE; return 0; }
		memcpy(buf, path, len + 1);
		return buf;
	}

	n = RtlGetCurrentDirectory_U(sizeof w, w);
	if (!n || n > sizeof w) { errno = ERANGE; return 0; }
	n /= sizeof(WCHAR);
	for (i = 0; i < n; i++) if (w[i] == '\\') w[i] = '/';
	if (n > 3 && w[n-1] == '/') n--;
	r = __utf16_to_utf8_buf(w, n, tmp, sizeof tmp);
	if (r < 0) return 0;
	len = (size_t)r;
	if (!buf) {
		if (!size) size = len + 1;
		if (len + 1 > size) { errno = ERANGE; return 0; }
		buf = malloc(size);
		if (!buf) return 0;
	} else if (!size) {
		errno = EINVAL; return 0;
	} else if (len + 1 > size) {
		errno = ERANGE; return 0;
	}
	if (snprintf(buf, size, "%s", tmp) != (int)len) {
		errno = ERANGE;
		return 0;
	}
	return buf;
}

withtok(heap_allocated)
char *get_current_dir_name(void)
{
	return getcwd(0, 0);
}

// NOLINTEND(misc-include-cleaner)
