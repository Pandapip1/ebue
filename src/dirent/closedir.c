/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * closedir mirrors close(): the fd table owns the HANDLE, this just
 * releases the fd slot through the usual path and then the DIR itself.
 */
#include <unistd.h>
#include "dirent_internal.h"

int closedir(DIR *dp)
{
	int r = close(dp->fd);
	__free(dp->buf);
	__free(dp);
	return r;
}
