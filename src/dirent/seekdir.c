/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * telldir/seekdir: see dirent_internal.h for why dp->tell -- a plain
 * count of entries returned, not a kernel offset -- is what these work
 * with.  Seeking backward rewinds and replays; seeking forward from
 * wherever the stream already is just discards entries, same as glibc
 * does whenever its cached-offset trick doesn't apply.  A location past
 * the end of the directory is a no-op past the point readdir() stops
 * returning entries -- telldir() afterward reports how far it actually
 * got, not the requested location.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include "dirent_internal.h"

long telldir(DIR *dp)
{
	return dp->tell;
}

void seekdir(DIR *dp, long loc)
{
	unsigned long remaining;

	if (loc < 0) return;
	if (loc < dp->tell) rewinddir(dp);
	/* A successful readdir() advances dp->tell by exactly one.  Snapshot
	 * that exact maximum number of successful calls as an independent
	 * rank; end-of-directory and errors still stop at the same call. */
	remaining = (unsigned long)loc - (unsigned long)dp->tell;
	while (remaining > 0 && dp->tell < loc) {
		remaining--;
		if (!readdir(dp)) break;
	}
}

// NOLINTEND(misc-include-cleaner)
