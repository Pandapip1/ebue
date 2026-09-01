/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * readdir/readdir_r, and __dirstream_next which every other file in this
 * directory that walks a DIR's entries (scandir, seekdir) goes through.
 * __dirstream_next() is now entirely backend-neutral: it refills dp->buf
 * via __plat_dir_read() and decodes one record at a time via
 * __plat_dir_decode_one() (src/internal/plat_dirent.h) -- neither this
 * file nor dirent_internal.h parses FILE_ID_BOTH_DIR_INFORMATION or
 * linux_dirent64 directly anymore; that moved into each backend's own
 * src/dirent/{nt,linux}/plat_dirent.c.
 *
 * d_reclen is always sizeof(struct dirent): unlike Linux's real getdents,
 * nothing here ever packs entries tighter than that, so there is no
 * shorter length to report.  d_off is dp->tell after the entry is
 * counted -- see dirent_internal.h for why that, and not a kernel byte
 * offset, is what telldir()/seekdir() work with.  "." and ".." arrive as
 * ordinary records straight from the backend (see dirent_internal.h);
 * nothing here treats them specially.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- GNU feature-test macro has its specified reserved spelling
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "dirent_internal.h"
#include "ownership_stubs.h"

static int advance_offset(DIR *dp, struct dirent *out)
{
	if (dp->tell < 0 || dp->tell >= LONG_MAX) {
		errno = EOVERFLOW;
		return -1;
	}
	dp->tell = (long)((unsigned long)dp->tell + 1UL);
	out->d_off = dp->tell;
	return 0;
}

int __dirstream_next(DIR *dp, struct __dirent_raw *out)
{
	__plat_handle_t h;
	ssize_t n;

	for (;;) {
		if (dp->bufpos < dp->buflen &&
		    __plat_dir_decode_one(dp->buf, dp->buflen, &dp->bufpos, out))
			return 1;
		/* Either the buffer was already exhausted, or
		 * __plat_dir_decode_one() says nothing is left at/after
		 * bufpos in THIS fill -- either way, a refill is needed. */
		dp->bufpos = dp->buflen;
		if (dp->done) return 0;

		h = __fd_handle(dp->fd);
		if (!h) return 0;      /* __fd_handle already set errno = EBADF */

		n = __plat_dir_read(h, dp->buf, __DIRBUF_SIZE, dp->restart);
		dp->restart = 0;
		if (n < 0) return 0;   /* errno already set by the backend */
		if (n == 0) { dp->done = 1; return 0; }

		dp->buflen = (size_t)n;
		dp->bufpos = 0;
	}
}

/* All three are required by every one of this static helper's callers
 * (this file's own fill(), single call site): dp and out are the same
 * always-valid handles their own callers hold (see this family's
 * comments in include/dirent.h and dirent_internal.h), and r is always
 * `&r`, the address of fill()'s own on-stack __dirstream_next() output,
 * never NULL. */
static int make_real(DIR *dp, const struct __dirent_raw *r, struct dirent *out)
    __attribute__((nonnull(1, 2, 3)));
static int make_real(DIR *dp, const struct __dirent_raw *r, struct dirent *out)
{
	__ownership_writable_span(out, sizeof *out);
	memset(out, 0, sizeof *out);
	out->d_ino = r->ino;
	out->d_type = r->type;
	out->d_reclen = sizeof *out;
	__ownership_writable_span(out->d_name, sizeof out->d_name);
	memcpy(out->d_name, r->name, sizeof out->d_name);
	return advance_offset(dp, out);
}

/* 0 = filled *out; 1 = end of directory; -1 = error, errno set.
 *
 * dp is this family's usual required handle; out is required too --
 * both of fill()'s callers below pass a real object's address
 * (readdir_r's own `entry` parameter, or readdir's `&dp->ent`), never
 * NULL, and nothing in this function ever checks out for NULL before
 * writing through it. */
static int fill(DIR *dp, struct dirent *out) __attribute__((nonnull(1, 2)));
static int fill(DIR *dp, struct dirent *out)
{
	struct __fd *f = __fd_get(dp->fd);
	if (!f) return -1;
	if (!f->vfs_native && (f->vfs == __VFS_ROOT || f->vfs == __VFS_DEV)) {
		static const char *const root_names[] = { ".", "..", "dev" };
		static const unsigned char root_types[] = { __DT_DIR, __DT_DIR, __DT_DIR };
		static const char *const dev_names[] = { ".", "..", "console", "null", "tty" };
		static const unsigned char dev_types[] = { __DT_DIR, __DT_DIR, __DT_CHR, __DT_CHR, __DT_CHR };
		const char *const *names = f->vfs == __VFS_ROOT ? root_names : dev_names;
		const unsigned char *types = f->vfs == __VFS_ROOT ? root_types : dev_types;
		size_t count = f->vfs == __VFS_ROOT ? 3 : 5;
		if ((size_t)dp->tell >= count) { dp->done = 1; return 1; }
		__ownership_writable_span(out, sizeof *out);
		memset(out, 0, sizeof *out);
		out->d_ino = (ino_t)(dp->tell < 2 ? (f->vfs == __VFS_ROOT ? __VFS_ROOT :
		                    (dp->tell == 0 ? __VFS_DEV : __VFS_ROOT)) :
		                    (f->vfs == __VFS_ROOT ? __VFS_DEV : __VFS_CONSOLE + dp->tell - 2));
		out->d_type = types[dp->tell];
		out->d_reclen = sizeof *out;
		(void)strlcpy(out->d_name, names[dp->tell], sizeof out->d_name);
		return advance_offset(dp, out);
	}
	{
		struct __dirent_raw r;
		if (__dirstream_next(dp, &r)) {
			if (make_real(dp, &r, out) < 0) return -1;
			if (f->vfs_native) dp->vseen |= __vfs_mandatory_seen(f->vfs, out->d_name);
			return 0;
		}
		if (!dp->done) return -1;
	}
	if (f->vfs_native) {
		int total = __vfs_mandatory_count(f->vfs);
		while (dp->vnext < total) {
			int i = dp->vnext++;
			if (dp->vseen & (1u << i)) continue;
			__ownership_writable_span(out, sizeof *out);
			memset(out, 0, sizeof *out);
			out->d_ino = (ino_t)__vfs_mandatory_kind(f->vfs, i);
			out->d_type = f->vfs == __VFS_ROOT ? __DT_DIR : __DT_CHR;
			out->d_reclen = sizeof *out;
			(void)strlcpy(out->d_name, __vfs_mandatory_name(f->vfs, i),
			              sizeof out->d_name);
			return advance_offset(dp, out);
		}
	}
	return 1;
}

int readdir_r(DIR *dp, struct dirent *entry, struct dirent **result)
{
	int r = fill(dp, entry);
	if (r < 0) { *result = 0; return errno; }
	*result = r ? 0 : entry;
	return 0;
}

struct dirent *readdir(DIR *dp)
{
	int r = fill(dp, &dp->ent);
	return r ? 0 : &dp->ent;
}

// NOLINTEND(misc-include-cleaner)
