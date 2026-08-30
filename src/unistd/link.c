/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* Hard links and symbolic links.  Hard links are FileLinkInformation.
 * Symbolic links need SeCreateSymbolicLinkPrivilege or developer mode on
 * Windows, so symlink tries and reports EPERM when it cannot; readlink
 * reads both NTFS symlinks and junctions.  Every NT-specific step (the
 * reparse-point interpretation, the FileLinkInformation/FSCTL calls
 * themselves, and -- as of the same fix open()'s own front door got,
 * src/fcntl/open.c -- readlinkat()'s vfs pre-check, since that calls
 * __vfs_resolve_at() too) lives in src/unistd/nt/plat_unistd.c's
 * __plat_link()/__plat_readlink()/__plat_symlink(); these front doors
 * are left with only the AT_SYMLINK_FOLLOW flag linkat() forwards --
 * genuinely portable, not an NT interpretation step. */
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include "libc.h"
#include "plat_unistd.h"

int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
	/* link.html's "[EINVAL] The value of the flag argument is not
	 * valid" is a may-fail, unlike unlinkat()'s, so an unrecognised bit
	 * is left to mean "flag clear" rather than rejected -- only
	 * AT_SYMLINK_FOLLOW is read. */
	return __plat_link(olddirfd, oldpath, newdirfd, newpath, (flags & AT_SYMLINK_FOLLOW) != 0);
}

int link(const char *a, const char *b) { return linkat(AT_FDCWD, a, AT_FDCWD, b, 0); }

ssize_t readlinkat(int dirfd, const char *path, char *buf, size_t bufsz)
{
	/* The vfs pre-check this front door used to run itself (reject a
	 * fixed-POSIX-namespace path with EINVAL/ENOENT before ever asking
	 * the backend) called __vfs_resolve_at() (src/internal/vfs.c)
	 * directly -- NT-only-overlay machinery, exactly the gap open()'s
	 * own front door had (src/fcntl/open.c). Moved into __plat_readlink()
	 * (src/internal/plat_unistd.h) alongside the rest of the NT-specific
	 * reparse-point interpretation it already owned; a backend with no
	 * such overlay (Linux) needs no equivalent check at all. */
	return __plat_readlink(dirfd, path, buf, bufsz);
}

ssize_t readlink(const char *path, char *buf, size_t bufsz) { return readlinkat(AT_FDCWD, path, buf, bufsz); }

int symlinkat(const char *target, int newdirfd, const char *linkpath)
{
	return __plat_symlink(target, newdirfd, linkpath);
}

int symlink(const char *target, const char *linkpath) { return symlinkat(target, AT_FDCWD, linkpath); }
