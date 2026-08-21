/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rewinddir: RestartScan = TRUE on the next NtQueryDirectoryFile call so
 * the kernel goes back to the first entry (".", on a regular directory).
 */
#include "dirent_internal.h"

void rewinddir(DIR *dp)
{
	dp->bufpos = 0;
	dp->buflen = 0;
	dp->restart = 1;
	dp->done = 0;
	dp->tell = 0;
}
