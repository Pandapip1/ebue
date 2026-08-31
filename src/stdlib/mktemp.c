/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* mkstemp and friends: fill the XXXXXX with random letters and try to
 * create exclusively, retrying on EEXIST. */
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <features.h>

static uint32_t rnd_state;

/* xorshift32: every shift here is deliberately allowed to lose bits off
 * the top -- that is the whole mixing step -- not an overflow bug. */
__wraps static uint32_t rnd(void)
{
	uint32_t x = rnd_state;
	if (!x) {
		int local;
		x = (uint32_t)(uintptr_t)&local ^ ((uint32_t)getpid() * 2654435761u) ^ (uint32_t)(uintptr_t)&rnd_state;
		if (!x) x = 1;
	}
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	rnd_state = x;
	return x;
}

/* Randomise the six X's that end len bytes before the end of template.
 * Returns -1 with EINVAL if they are not there.
 *
 * tmpl is required: `strlen(tmpl)` is unconditional as this function's
 * very first real statement, with no NULL check. All three of this
 * file's real callers (mkostemps(), mkdtemp(), mktemp()) forward their
 * own tmpl parameter unchanged, and no caller anywhere in this tree
 * passes NULL for it. */
static int fill(char *tmpl, int suffix) __attribute__((nonnull(1)));
static int fill(char *tmpl, int suffix)
{
	static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	size_t l = strlen(tmpl);
	char *p;
	int i;
	if (suffix < 0 || l < 6 + (size_t)suffix) { errno = EINVAL; return -1; }
	p = tmpl + l - suffix - 6;
	for (i = 0; i < 6; i++) if (p[i] != 'X') { errno = EINVAL; return -1; }
	for (i = 0; i < 6; i++) p[i] = chars[rnd() % 62];
	return 0;
}

int mkostemps(char *tmpl, int suffix, int flags)
{
	int i, fd;
	flags &= ~O_ACCMODE;
	for (i = 0; i < 100; i++) {
		if (fill(tmpl, suffix) < 0) return -1;
		fd = open(tmpl, flags | O_CREAT | O_EXCL | O_RDWR, 0600);
		if (fd >= 0 || errno != EEXIST) return fd;
	}
	return -1;
}

int mkstemps(char *tmpl, int suffix) { return mkostemps(tmpl, suffix, 0); }
int mkostemp(char *tmpl, int flags) { return mkostemps(tmpl, 0, flags); }
int mkstemp(char *tmpl) { return mkostemps(tmpl, 0, 0); }

char *mkdtemp(char *tmpl)
{
	int i;
	for (i = 0; i < 100; i++) {
		if (fill(tmpl, 0) < 0) return 0;
		if (mkdir(tmpl, 0700) == 0) return tmpl;
		if (errno != EEXIST) return 0;
	}
	return 0;
}

char *mktemp(char *tmpl)
{
	struct stat st;
	int i;
	for (i = 0; i < 100; i++) {
		if (fill(tmpl, 0) < 0) { tmpl[0] = 0; return tmpl; }
		if (stat(tmpl, &st) < 0) {
			if (errno == ENOENT) { errno = 0; return tmpl; }
			tmpl[0] = 0; return tmpl;
		}
	}
	tmpl[0] = 0;
	errno = EEXIST;
	return tmpl;
}
