/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getentropy(): fill a caller-given buffer with cryptographically
 * strong random bytes. Not POSIX (a BSD/glibc/Linux extension,
 * originally from OpenBSD), but no different in kind from the rest of
 * <unistd.h>'s _GNU_SOURCE/_BSD_SOURCE-guarded surface.
 *
 * Previously left undefined-ok on the theory that using bcrypt.dll's
 * BCryptGenRandom -- the only real entropy source reachable at all,
 * ntdll having none -- meant taking on a "routine dependency" this
 * library otherwise avoids. That framing missed that the project
 * already has a sanctioned answer to exactly this situation:
 * NTLIBC_USE_KERNEL32 (see configure --help, and src/unistd/ids.c's
 * advapi32 use / src/signal/signal.c's SetConsoleCtrlHandler use for
 * two existing examples of the same "load a higher-level DLL only in
 * the build that explicitly asked for it" shape). This just routes
 * getentropy() through that same, already-existing door instead of
 * refusing to build it at all -- see src/unistd/nt/plat_unistd.c's
 * __plat_getentropy() for the NT side (real under NTLIBC_USE_KERNEL32,
 * ENOSYS without it) and src/unistd/linux/plat_unistd.c's for the Linux
 * side (real unconditionally, via getrandom(2) -- no higher-level
 * dependency question even arises there).
 *
 * getentropy(3) (not a POSIX page; OpenBSD/glibc): "the maximum
 * permitted value for the length argument is 256... If the buflen
 * argument is greater than 256, error EIO." */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <unistd.h>
#include <errno.h>
#include "libc.h"
#include "plat_unistd.h"

int getentropy(void *buf, size_t buflen)
{
	if (buflen > 256) { errno = EIO; return -1; }
	return __plat_getentropy(buf, buflen);
}

// NOLINTEND(misc-include-cleaner)
