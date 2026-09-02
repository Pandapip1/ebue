/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getdents: the GNU raw-directory-read call, taken directly off a fd
 * rather than a DIR.
 *
 * Record decoding goes through __plat_dir_decode_one() (src/internal/
 * plat_dirent.h), the same one src/dirent/readdir.c's __dirstream_next()
 * uses; this file no longer parses FILE_ID_BOTH_DIR_INFORMATION or
 * linux_dirent64 itself. Like readdir(), "." and ".." come through as
 * ordinary records straight from the backend (see dirent_internal.h) and
 * need no special handling here.
 *
 * A single __plat_dir_read() fill can hold more records than a given
 * call's `out`/`size` has room to decode into -- this project's own
 * struct dirent is a fixed 280 bytes (dominated by d_name[256]), well
 * above a real backend record's typical size (short names), so it is
 * routine for one fill to decode into more bytes than a caller's own
 * buffer holds. The backend's read position has already moved past that
 * whole fill by the time this function sees it, so anything decodable
 * but not yet handed to the caller has to survive to this fd's NEXT
 * getdents() call rather than being re-fetched -- a fresh backend read
 * at that point would only return whatever comes AFTER the fill already
 * consumed, silently skipping it. That leftover lives in f->dbuf/
 * dbufpos/dbuflen (struct __fd, src/internal/libc.h), the same
 * "buffer plus a cursor into it" shape __dirstream_next() already uses
 * for DIR, just owned by the fd table slot instead of an opendir()'d
 * object since getdents() has no such object of its own. */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- GNU feature-test macro has its specified reserved spelling
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include "dirent_internal.h"

static int append_missing(struct __fd *restrict f,
	struct dirent *out, size_t size)
{
	size_t used = 0;
	unsigned char total = __vfs_mandatory_count(f->vfs);
	while (f->vnext < total &&
	       used + sizeof(struct dirent) <= size) {
		int i = f->vnext++;
		struct dirent *d;
		if (f->vseen & (1u << i)) continue;
		d = (struct dirent *)((char *)out + used);
		memset(d, 0, sizeof *d);
		d->d_ino = (ino_t)__vfs_mandatory_kind(f->vfs, i);
		d->d_type = f->vfs == __VFS_ROOT ? __DT_DIR : __DT_CHR;
		d->d_reclen = sizeof *d;
		(void)strlcpy(d->d_name, __vfs_mandatory_name(f->vfs, i),
		              sizeof d->d_name);
		used += sizeof *d;
		d->d_off = (off_t)f->vnext;
	}
	return (int)used;
}

int getdents(int fd, struct dirent *out withtok(writable_span(size)),
	size_t size)
{
	struct __fd *f = __fd_get(fd);
	struct __dirent_raw r;
	size_t used = 0;

	if (!f) return -1;
	if (f->type != __FD_DIR) { errno = ENOTDIR; return -1; }
	if (size < sizeof(struct dirent)) { errno = EINVAL; return -1; }
	if (!f->vfs_native && (f->vfs == __VFS_ROOT || f->vfs == __VFS_DEV)) {
		static const char *const root_names[] = { ".", "..", "dev" };
		static const unsigned char root_types[] = { __DT_DIR, __DT_DIR, __DT_DIR };
		static const char *const dev_names[] = { ".", "..", "console", "null", "tty" };
		static const unsigned char dev_types[] = { __DT_DIR, __DT_DIR, __DT_CHR, __DT_CHR, __DT_CHR };
		const char *const *names = f->vfs == __VFS_ROOT ? root_names : dev_names;
		const unsigned char *types = f->vfs == __VFS_ROOT ? root_types : dev_types;
		size_t total = f->vfs == __VFS_ROOT ? 3 : 5;
		size_t index = f->pos < 0 ? 0 : (size_t)f->pos;
		while (index < total && used + sizeof(struct dirent) <= size) {
			struct dirent *d = (struct dirent *)((char *)out + used);
			memset(d, 0, sizeof *d);
			d->d_ino = (ino_t)(index < 2 ? (f->vfs == __VFS_ROOT ? __VFS_ROOT :
			             (index == 0 ? __VFS_DEV : __VFS_ROOT)) :
			             (f->vfs == __VFS_ROOT ? __VFS_DEV : __VFS_CONSOLE + index - 2));
			d->d_type = types[index];
			d->d_reclen = sizeof *d;
			(void)strlcpy(d->d_name, names[index], sizeof d->d_name);
			index++;
			used += sizeof *d;
			d->d_off = (off_t)index;
		}
		f->pos = (long long)index;
		return (int)used;
	}

	if (!f->dbuf) {
		f->dbuf = __malloc(__DIRBUF_SIZE);
		if (!f->dbuf) { errno = ENOMEM; return -1; }
		f->dbufpos = f->dbuflen = 0;
	}

	for (;;) {
		if (used + sizeof(struct dirent) > size) break;
		if (f->dbufpos >= f->dbuflen ||
		    !__plat_dir_decode_one(f->dbuf, f->dbuflen, &f->dbufpos, &r)) {
			/* Nothing left already fetched -- only now, with the
			 * caller's own buffer confirmed to have room for at
			 * least one more record (the check above), ask the
			 * backend for more. Checking capacity first keeps this
			 * fd's dbufpos from ever moving past a record this call
			 * cannot actually deliver. */
			ssize_t n = __plat_dir_read(f->h, f->dbuf, __DIRBUF_SIZE, 0);
			if (n < 0) return used ? (int)used : -1;
			if (n == 0) {
				if (f->vfs_native) {
					int m = append_missing(f,
						(struct dirent *)((char *)out + used), size - used);
					used += (size_t)m;
				}
				break;
			}
			f->dbuflen = (size_t)n;
			f->dbufpos = 0;
			continue;
		}
		{
			struct dirent *d = (struct dirent *)((char *)out + used);
			memset(d, 0, sizeof *d);
			d->d_ino = r.ino;
			d->d_type = r.type;
			d->d_reclen = sizeof *d;
			memcpy(d->d_name, r.name, sizeof d->d_name);
			if (f->vfs_native) f->vseen |= __vfs_mandatory_seen(f->vfs, d->d_name);
			used += sizeof *d;
			d->d_off = (off_t)used;
		}
	}
	return (int)used;
}

// NOLINTEND(misc-include-cleaner)
