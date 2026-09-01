/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getdents: the GNU raw-directory-read call, taken directly off a fd
 * rather than a DIR.  Continuation across calls comes for free from the
 * backend's own enumeration cursor -- NT: RestartScan = FALSE (the
 * default state of a freshly opened handle) picks up from wherever the
 * kernel's own cursor on that handle left off; Linux: the directory fd's
 * own file-position cursor advances the same way a real getdents64(2)
 * caller's would -- so nothing needs to be remembered here between
 * calls.
 *
 * Record decoding goes through __plat_dir_decode_one() (src/internal/
 * plat_dirent.h), the same one src/dirent/readdir.c's __dirstream_next()
 * uses; this file no longer parses FILE_ID_BOTH_DIR_INFORMATION or
 * linux_dirent64 itself. Like readdir(), "." and ".." come through as
 * ordinary records straight from the backend (see dirent_internal.h) and
 * need no special handling here.
 *
 * One pre-existing limitation this refactor carries forward unchanged,
 * not introduced by it: a single call only ever decodes as much of ONE
 * __plat_dir_read() fill as fits in the caller's `out`/`size`. If that
 * fill holds more records than `out` has room for, the rest are not
 * returned by this or any later call -- the backend's cursor has already
 * advanced past all of them by the time this function sees the buffer.
 * True of the original NT-only implementation this replaces too (it
 * `break`s out of its own NextEntryOffset walk the same way); fixing it
 * would mean carrying a decode position across getdents() calls the way
 * DIR does, which is a real behavior change out of scope for a
 * redesign whose job is to keep every backend's observable behavior
 * exactly as it was, just decoded through a shared, portable step. */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- GNU feature-test macro has its specified reserved spelling
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include "dirent_internal.h"
#include "ownership_stubs.h"

static int append_missing(struct __fd *f, struct dirent *out, size_t size)
{
	size_t used = 0;
	int total = __vfs_mandatory_count(f->vfs);
	/* vnext is an unsigned byte and starts at zero; mandatory_count is
	 * exactly 0, 1 or 3.  Snapshot that finite maximum independently of
	 * the descriptor member which records progress across calls. */
	unsigned remaining = (unsigned)total;
	while (remaining > 0 && f->vnext < total &&
	       used + sizeof(struct dirent) <= size) {
		int i = f->vnext++;
		struct dirent *d;
		remaining--;
		if (f->vseen & (1u << i)) continue;
		d = (struct dirent *)((char *)out + used);
		__ownership_writable_span(d, sizeof *d);
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
	unsigned char buf[8192];
	struct __dirent_raw r;
	size_t bufpos, buflen;
	ssize_t n;
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
			__ownership_writable_span(d, sizeof *d);
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

	n = __plat_dir_read(f->h, buf, sizeof buf, 0);
	if (n < 0) return -1;
	if (n == 0)
		return f->vfs_native ? append_missing(f, out, size) : 0;

	bufpos = 0;
	buflen = (size_t)n;
	while (used + sizeof(struct dirent) <= size &&
	       __plat_dir_decode_one(buf, buflen, &bufpos, &r)) {
		struct dirent *d = (struct dirent *)((char *)out + used);
		__ownership_writable_span(d, sizeof *d);
		memset(d, 0, sizeof *d);
		d->d_ino = r.ino;
		d->d_type = r.type;
		d->d_reclen = sizeof *d;
		__ownership_writable_span(d->d_name, sizeof d->d_name);
		memcpy(d->d_name, r.name, sizeof d->d_name);
		if (f->vfs_native) f->vseen |= __vfs_mandatory_seen(f->vfs, d->d_name);
		used += sizeof *d;
		d->d_off = (off_t)used;
	}
	return (int)used;
}

// NOLINTEND(misc-include-cleaner)
