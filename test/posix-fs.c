/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Native rooted filesystems win when present; the fixed POSIX root and
 * device objects fill only the mandatory names they do not provide.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int fails;
#define CHECK(x) do { if (!(x)) { fails++; printf("FAIL %s:%d: %s (errno=%d)\n", __FILE__, __LINE__, #x, errno); } } while (0)

static int nameeq(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		unsigned char ac = (unsigned char)*a, bc = (unsigned char)*b;
		if (ac >= 'A' && ac <= 'Z') ac += 'a' - 'A';
		if (bc >= 'A' && bc <= 'Z') bc += 'a' - 'A';
		if (ac != bc) return 0;
	}
	return *a == *b;
}

/* Native directories may contain any number of additional entries.  The
 * fixed namespace is a union: each mandatory entry must appear exactly once. */
static int lists_mandatory(const char *path, const char *const *want, int nwant)
{
	DIR *dp = opendir(path);
	struct dirent *d;
	int seen[8] = {0}, i;
	if (!dp) return 0;
	while ((d = readdir(dp))) {
		for (i = 0; i < nwant; i++) if (nameeq(d->d_name, want[i])) seen[i]++;
	}
	closedir(dp);
	for (i = 0; i < nwant; i++) if (seen[i] != 1) return 0;
	return 1;
}

static int getdents_lists_mandatory(const char *path, const char *const *want, int nwant)
{
	struct dirent entries[64];
	int seen[8] = {0}, fd, n, i, j;
	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0) return 0;
	while ((n = getdents(fd, entries, sizeof entries)) > 0) {
		for (i = 0; i < n / (int)sizeof entries[0]; i++)
			for (j = 0; j < nwant; j++)
				if (nameeq(entries[i].d_name, want[j])) seen[j]++;
	}
	close(fd);
	if (n < 0) return 0;
	for (i = 0; i < nwant; i++) if (seen[i] != 1) return 0;
	return 1;
}

static int inherited_child(void)
{
	struct stat st;
	char cwd[4096];
	int fd;
	if (!getcwd(cwd, sizeof cwd)) return 2;
	if (fstat(9, &st) < 0 || !S_ISDIR(st.st_mode)) return 3;
	/* console is commonly absent from a native /dev, so this also verifies
	 * that the native-directory overlay marker survived process creation. */
	fd = openat(9, "console", O_RDONLY);
	if (fd < 0) return 4;
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	static const char *const root_entries[] = { "dev" };
	static const char *const dev_entries[] = { "console", "null", "tty" };
	struct stat root, dev, con, nul, tty, fst;
	char oldcwd[4096], cwd[4096], byte;
	char linkbuf[8], *canonical;
	int synthetic_dev;
	int rootfd, devfd, nullfd, dupfd;
	pid_t pid = -1;
	int status;
	char *child_argv[3];

	if (argc > 1 && !strcmp(argv[1], "--inherited")) return inherited_child();

	CHECK(getcwd(oldcwd, sizeof oldcwd) == oldcwd);
	CHECK(stat("/", &root) == 0 && S_ISDIR(root.st_mode));
	CHECK(stat("/dev", &dev) == 0 && S_ISDIR(dev.st_mode));
	CHECK(stat("/dev/console", &con) == 0 && S_ISCHR(con.st_mode));
	CHECK(stat("/dev/null", &nul) == 0 && S_ISCHR(nul.st_mode));
	CHECK(lstat("/dev/tty", &tty) == 0 && S_ISCHR(tty.st_mode));
	CHECK(root.st_ino != 0 && dev.st_ino != 0);
	canonical = realpath("/dev/../dev/null", 0);
	CHECK(canonical != 0);
	free(canonical);
	errno = 0;
	CHECK(stat("/dev/null/", &fst) == -1 && errno == ENOTDIR);
	errno = 0;
	CHECK(readlink("/dev/null", linkbuf, sizeof linkbuf) == -1 && errno == EINVAL);
	CHECK(lists_mandatory("/", root_entries, 1));
	CHECK(lists_mandatory("/dev", dev_entries, 3));
	CHECK(getdents_lists_mandatory("/", root_entries, 1));
	CHECK(getdents_lists_mandatory("/dev", dev_entries, 3));
	canonical = realpath("/dev", 0);
	synthetic_dev = canonical && !strcmp(canonical, "/dev");
	free(canonical);

	rootfd = open("/", O_RDONLY | O_DIRECTORY);
	CHECK(rootfd >= 0);
	devfd = rootfd < 0 ? -1 : openat(rootfd, "dev", O_RDONLY | O_DIRECTORY);
	CHECK(devfd >= 0);
	if (devfd >= 0) {
		CHECK(fstat(devfd, &fst) == 0 && fst.st_dev == dev.st_dev && fst.st_ino == dev.st_ino);
		nullfd = openat(devfd, "null", O_RDWR);
		CHECK(nullfd >= 0);
		if (nullfd >= 0) {
			CHECK(fstat(nullfd, &fst) == 0 && fst.st_ino == nul.st_ino);
			CHECK(write(nullfd, "x", 1) == 1);
			CHECK(read(nullfd, &byte, 1) == 0);
			dupfd = dup(nullfd);
			CHECK(dupfd >= 0);
			if (dupfd >= 0) {
				CHECK(fstat(dupfd, &fst) == 0 && fst.st_ino == nul.st_ino);
				close(dupfd);
			}
			close(nullfd);
		}
		close(devfd);
	}
	if (rootfd >= 0) close(rootfd);
	nullfd = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0666);
	CHECK(nullfd >= 0);
	if (nullfd >= 0) close(nullfd);

	if (synthetic_dev) {
		errno = 0;
		CHECK(open("/dev/missing", O_RDONLY) == -1 && errno == ENOENT);
		errno = 0;
		CHECK(open("/dev/missing", O_CREAT | O_WRONLY, 0600) == -1 && errno == EROFS);
		errno = 0;
		CHECK(unlink("/dev/null") == -1 && errno == EROFS);
		errno = 0;
		CHECK(mkdir("/dev/new", 0700) == -1 && errno == EROFS);
	}

	CHECK(chdir("/dev") == 0);
	CHECK(getcwd(cwd, sizeof cwd) == cwd);
	nullfd = open("null", O_RDONLY);
	CHECK(nullfd >= 0);
	if (nullfd >= 0) close(nullfd);
	devfd = open(".", O_RDONLY | O_DIRECTORY);
	CHECK(devfd >= 0);
	if (devfd >= 0) {
		CHECK(dup2(devfd, 9) == 9);
		if (devfd != 9) close(devfd);
		child_argv[0] = argv[0]; child_argv[1] = (char *)"--inherited"; child_argv[2] = 0;
		CHECK(posix_spawn(&pid, argv[0], 0, 0, child_argv, environ) == 0);
		if (pid > 0) {
			CHECK(waitpid(pid, &status, 0) == pid);
			CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
		}
		close(9);
	}
	CHECK(chdir("..") == 0);
	CHECK(getcwd(cwd, sizeof cwd) == cwd);
	CHECK(chdir(oldcwd) == 0);

	return fails != 0;
}
