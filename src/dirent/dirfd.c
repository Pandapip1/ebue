/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "dirent_internal.h"

int dirfd(DIR *dp)
{
	return dp->fd;
}
