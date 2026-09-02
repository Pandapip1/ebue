/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)

/* This front door's own job is only what plat_unistd.h's __plat_getcwd()
 * comment says a backend should NOT have to know: the __VFS_ROOT/
 * __VFS_DEV overlay special cases (portable bookkeeping, same as
 * chdir.c's own __vfs_cwd_set() split), and getcwd.html's buf/size
 * contract -- NULL buf means "malloc exactly what's needed", size 0
 * with a non-NULL buf is [EINVAL], and a result that would not fit is
 * [ERANGE].  What a "current directory" even means on this backend --
 * NT's DOS-form UTF-16 RtlGetCurrentDirectory_U, Linux's byte-for-byte
 * getcwd(2) -- is entirely __plat_getcwd()'s job now (src/unistd/{nt,
 * linux}/plat_unistd.c), not this file's. */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "libc.h"
#include "plat_unistd.h"

withtok(heap_allocated)
char *getcwd(char *buf withtok(heap_allocated), size_t size)
{
	char tmp[4096 * 3];
	size_t len;
	ssize_t r;
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

	r = __plat_getcwd(tmp, sizeof tmp);
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
	{
		size_t i;
		for (i = 0; i <= len; i++) buf[i] = tmp[i];
	}
	return buf;
}

withtok(heap_allocated)
char *get_current_dir_name(void)
{
	return getcwd(0, 0);
}

// NOLINTEND(misc-include-cleaner)
