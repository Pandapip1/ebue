/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * opendir/readdir/scandir etc., exercised without stdio (not yet built
 * when this was written): failures are reported with write(2, ...) and
 * the process exit code, the way runtests.sh checks every other test.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void fail(const char *msg)
{
	fails++;
	write(2, "FAIL: ", 6);
	write(2, msg, strlen(msg));
	write(2, "\n", 1);
}

#define CHECK(cond) do { if (!(cond)) fail(#cond); } while (0)

static void touch(const char *path)
{
	int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) { fail("touch: open"); return; }
	close(fd);
}

int main(void)
{
	const char *dir = "dirent_test.d";
	const char *names[] = { "alpha", "beta", "gamma10", "gamma9" };
	size_t i;
	DIR *dp;
	struct dirent *d;
	int seen_dot = 0, seen_dotdot = 0, seen_alpha = 0, n = 0, fd, dfd;
	struct dirent **list;
	int nlist;

	/* Leftover from a previous failed run: clear it out. */
	for (i = 0; i < sizeof names / sizeof *names; i++) {
		char p[64];
		strcpy(p, dir); strcat(p, "/"); strcat(p, names[i]);
		unlink(p);
	}
	rmdir(dir);

	if (mkdir(dir, 0755) < 0) { fail("mkdir"); return 1; }
	for (i = 0; i < sizeof names / sizeof *names; i++) {
		char p[64];
		strcpy(p, dir); strcat(p, "/"); strcat(p, names[i]);
		touch(p);
	}
	{
		char p[64];
		strcpy(p, dir); strcat(p, "/subdir");
		if (mkdir(p, 0755) < 0) fail("mkdir subdir");
	}

	/* opendir/readdir: "." and ".." synthesized, every real name seen
	 * exactly once, the subdirectory reported as DT_DIR. */
	dp = opendir(dir);
	CHECK(dp != 0);
	if (dp) {
		int counts[8] = { 0 };
		while ((d = readdir(dp))) {
			n++;
			if (!strcmp(d->d_name, ".")) seen_dot = 1;
			else if (!strcmp(d->d_name, "..")) seen_dotdot = 1;
			else if (!strcmp(d->d_name, "alpha")) { seen_alpha = 1; CHECK(d->d_type == DT_REG); }
			else if (!strcmp(d->d_name, "subdir")) { CHECK(d->d_type == DT_DIR); }
			for (i = 0; i < sizeof names / sizeof *names; i++)
				if (!strcmp(d->d_name, names[i])) counts[i]++;
		}
		CHECK(seen_dot && seen_dotdot && seen_alpha);
		CHECK(n == 2 + (int)(sizeof names / sizeof *names) + 1);   /* + subdir */
		for (i = 0; i < sizeof names / sizeof *names; i++)
			CHECK(counts[i] == 1);

		/* dirfd() plus rewinddir(): counting again from scratch gives
		 * the same total. */
		CHECK(dirfd(dp) >= 0);
		rewinddir(dp);
		{
			int n2 = 0;
			while (readdir(dp)) n2++;
			CHECK(n2 == n);
		}

		/* telldir/seekdir: capture the position right after "." and
		 * "..", read on, then seek back and confirm the same entry
		 * comes out again. */
		rewinddir(dp);
		readdir(dp); readdir(dp);            /* "." then ".." */
		{
			long mark = telldir(dp);
			struct dirent *first = readdir(dp);
			char firstname[256];
			CHECK(first != 0);
			strcpy(firstname, first ? first->d_name : "");
			readdir(dp);                     /* move past it */
			seekdir(dp, mark);
			CHECK(telldir(dp) == mark);
			d = readdir(dp);
			CHECK(d != 0 && !strcmp(d->d_name, firstname));
		}

		closedir(dp);
	}

	/* fdopendir: takes ownership of an fd already open on the directory. */
	fd = open(dir, O_RDONLY | O_DIRECTORY);
	CHECK(fd >= 0);
	if (fd >= 0) {
		dfd = fd;
		dp = fdopendir(fd);
		CHECK(dp != 0);
		if (dp) {
			CHECK(dirfd(dp) == dfd);
			n = 0;
			while (readdir(dp)) n++;
			CHECK(n == 2 + (int)(sizeof names / sizeof *names) + 1);
			closedir(dp);   /* also closes dfd */
		}
	}

	/* scandir + alphasort: results come back sorted, "." and ".." are
	 * ordinary entries scandir() includes just like readdir() does. */
	nlist = scandir(dir, &list, 0, alphasort);
	CHECK(nlist == 2 + (int)(sizeof names / sizeof *names) + 1);
	if (nlist > 0) {
		for (i = 1; i < (size_t)nlist; i++)
			CHECK(strcmp(list[i-1]->d_name, list[i]->d_name) <= 0);
		for (i = 0; i < (size_t)nlist; i++) free(list[i]);
		free(list);
	}

	/* versionsort: "gamma9" before "gamma10". */
	nlist = scandir(dir, &list, 0, versionsort);
	CHECK(nlist > 0);
	if (nlist > 0) {
		int i9 = -1, i10 = -1;
		for (i = 0; i < (size_t)nlist; i++) {
			if (!strcmp(list[i]->d_name, "gamma9")) i9 = (int)i;
			if (!strcmp(list[i]->d_name, "gamma10")) i10 = (int)i;
		}
		CHECK(i9 >= 0 && i10 >= 0 && i9 < i10);
		for (i = 0; i < (size_t)nlist; i++) free(list[i]);
		free(list);
	}

	/* getdents: raw read off a directory fd; every real name shows up,
	 * and so do "." and ".." (NT hands them back like any other
	 * record -- see dirent_internal.h). */
	fd = open(dir, O_RDONLY | O_DIRECTORY);
	CHECK(fd >= 0);
	if (fd >= 0) {
		char buf[4096];
		int r = getdents(fd, (struct dirent *)buf, sizeof buf);
		CHECK(r > 0);
		if (r > 0) {
			int off = 0, got_alpha = 0, got_dot = 0;
			while (off < r) {
				struct dirent *gd = (struct dirent *)(buf + off);
				if (!strcmp(gd->d_name, "alpha")) got_alpha = 1;
				if (!strcmp(gd->d_name, ".") || !strcmp(gd->d_name, "..")) got_dot = 1;
				off += gd->d_reclen;
			}
			CHECK(got_alpha);
			CHECK(got_dot);
		}
		close(fd);
	}

	/* Nonexistent directory: opendir fails, closedir untouched. */
	dp = opendir("dirent_test.d/does/not/exist");
	CHECK(dp == 0);

	/* A plain file is not a directory. */
	{
		char p[64];
		strcpy(p, dir); strcat(p, "/alpha");
		dp = opendir(p);
		CHECK(dp == 0);
	}

	/* Clean up. */
	for (i = 0; i < sizeof names / sizeof *names; i++) {
		char p[64];
		strcpy(p, dir); strcat(p, "/"); strcat(p, names[i]);
		unlink(p);
	}
	{
		char p[64];
		strcpy(p, dir); strcat(p, "/subdir");
		rmdir(p);
	}
	rmdir(dir);

	if (!fails) write(1, "dirent: all tests passed\n", 26);
	return fails != 0;
}
