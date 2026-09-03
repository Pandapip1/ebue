/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ftok(): a pure function of a file's identity (st_dev/st_ino), computed
 * identically on both backends, so unlike the rest of src/ipc/ it needs
 * no linux/ or nt/ split. The fold (id in the top byte, st_dev in the
 * next, st_ino in the low two) matches glibc/musl's layout, which POSIX
 * doesn't require but callers may have learned empirically.
 */
#include <sys/ipc.h>
#include <sys/stat.h>

key_t ftok(const char *path, int id)
{
	struct stat st;

	if (stat(path, &st) < 0)
		return (key_t)-1;
	return (key_t)(((unsigned)(id & 0xff) << 24) |
	               (((unsigned)st.st_dev & 0xff) << 16) |
	               ((unsigned)st.st_ino & 0xffff));
}
