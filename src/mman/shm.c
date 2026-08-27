/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * POSIX shared-memory objects, represented by ordinary backing files in a
 * private directory under the process temporary directory.  This is not a
 * shortcut around the VM implementation: src/mman/mman.c maps a regular file
 * through NtCreateSection/NtMapViewOfSection, so the returned descriptor has
 * exactly the object shape mmap(), ftruncate(), fstat(), close() and fork()
 * already understand.  unlink() asks NT for POSIX deletion semantics first,
 * which preserves open and mapped references while removing the name and
 * permits the same name to be created again immediately.
 *
 * The namespace directory is shared by processes that inherit the same
 * TMPDIR/TMP/TEMP setting.  POSIX leaves names without an initial slash and
 * names containing additional slashes implementation-defined; ntlibc accepts
 * the former for compatibility with the Open POSIX Test Suite and rejects the
 * latter.  Restricting the component to the portable filename character set
 * also avoids giving DOS device names and separators a second interpretation.
 */
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

static const char *shm_tmpdir(void)
{
	const char *dir = getenv("TMPDIR");
	if (!dir || !*dir) dir = getenv("TMP");
	if (!dir || !*dir) dir = getenv("TEMP");
	if (!dir || !*dir) dir = ".";
	return dir;
}

static int portable_name_char(unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

static char *shm_path(const char *name)
{
	const char *dir;
	const char *component;
	size_t dirlen, namelen, pathlen;
	char *path;
	size_t i;

	if (!name) { errno = EINVAL; return NULL; }
	namelen = strlen(name);
	/* {PATH_MAX} includes the terminating null; {NAME_MAX} does not. */
	if (namelen >= PATH_MAX) { errno = ENAMETOOLONG; return NULL; }
	component = name[0] == '/' ? name + 1 : name;
	namelen = strlen(component);
	if (namelen > NAME_MAX) { errno = ENAMETOOLONG; return NULL; }
	if (!namelen || !strcmp(component, ".") || !strcmp(component, "..")) {
		errno = EINVAL;
		return NULL;
	}
	for (i = 0; i < namelen; i++)
		if (!portable_name_char((unsigned char)component[i])) {
			errno = EINVAL;
			return NULL;
		}

	dir = shm_tmpdir();
	dirlen = strlen(dir);
	pathlen = dirlen + sizeof "/ntlibc-shm/" - 1 + namelen;
	if (pathlen >= PATH_MAX) { errno = ENAMETOOLONG; return NULL; }
	path = malloc(pathlen + 1);
	if (!path) return NULL;
	memcpy(path, dir, dirlen);
	memcpy(path + dirlen, "/ntlibc-shm/", sizeof "/ntlibc-shm/" - 1);
	memcpy(path + dirlen + sizeof "/ntlibc-shm/" - 1,
	       component, namelen + 1);
	return path;
}

static int ensure_namespace(const char *path)
{
	char *slash;
	char *dir = strdup(path);
	int saved;

	if (!dir) return -1;
	slash = strrchr(dir, '/');
	*slash = 0;
	if (mkdir(dir, 0777) < 0 && errno != EEXIST) {
		saved = errno;
		free(dir);
		errno = saved;
		return -1;
	}
	free(dir);
	return 0;
}

int shm_open(const char *name, int oflag, mode_t mode)
{
	char *path;
	int fd, saved;
	int access = oflag & O_ACCMODE;

	if ((oflag & ~(O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC)) ||
	    (access != O_RDONLY && access != O_RDWR)) {
		errno = EINVAL;
		return -1;
	}
	path = shm_path(name);
	if (!path) return -1;
	if (ensure_namespace(path) < 0) { free(path); return -1; }

	/* shm_open.html requires FD_CLOEXEC on every returned descriptor. */
	fd = open(path, oflag | O_CLOEXEC, mode);
	saved = errno;
	free(path);
	errno = saved;
	return fd;
}

int shm_unlink(const char *name)
{
	char *path = shm_path(name);
	int result, saved;

	if (!path) return -1;
	result = unlink(path);
	saved = errno;
	free(path);
	errno = saved;
	return result;
}
