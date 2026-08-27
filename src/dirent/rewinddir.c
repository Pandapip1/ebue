/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rewinddir: RestartScan = TRUE on the next NtQueryDirectoryFile call so
 * the kernel goes back to the first entry (".", on a regular directory).
 */
#include "dirent_internal.h"

void rewinddir(DIR *dp)
{
	struct __fd *f = __fd_get(dp->fd);
	if (f && f->vfs) {
		f->pos = 0;
		f->vseen = 0;
		f->vnext = 0;
	}
	dp->bufpos = 0;
	dp->buflen = 0;
	dp->restart = 1;
	dp->done = 0;
	dp->tell = 0;
	dp->vseen = 0;
	dp->vnext = 0;
}
