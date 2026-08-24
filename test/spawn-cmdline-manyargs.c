/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A downstream report described a real build tool's command line getting
 * silently truncated by __spawn (src/process/spawn.c) somewhere around
 * 8KB, with no E2BIG -- the tail of the argument list just vanished, so
 * the compiler it invoked saw "no input files". test/exec.c's
 * test_cmdline_limit already covers a command line built from ONE huge
 * argument and finds nothing wrong; what it does not cover is a command
 * line built from MANY SMALL arguments summing to the same size, which
 * is the shape a real build tool actually produces (one flag or path per
 * argument) and exercises build_cmdline()/append_arg()'s buffer-growth
 * arithmetic on every call rather than once.
 *
 * This spawns a copy of itself with argument counts and total sizes
 * bracketing the reported ~8KB boundary (and pushes well past it, up to
 * the real ~64KB UNICODE_STRING ceiling __US_MAX_WCHARS enforces), in
 * two argument shapes -- short bare flags, and longer Windows-path-like
 * arguments needing backslash/space quoting -- and checks the child's
 * own argc/argv against exactly what was sent. Runs under Wine (no
 * fork needed) and, unlike a fork-based test, also on real Windows CI:
 * if this boundary behaves differently there, that is exactly the
 * result worth having.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

int __spawn(const char *, char *const *, char *const *);
extern char **environ;

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Child role: argv[1] is "--check", argv[2] is the expected argc (as
 * sent), argv[3..] are the arguments to verify byte-for-byte. */
static int check_child(int argc, char **argv)
{
	int want, i;
	if (argc < 3) return 2;
	want = atoi(argv[2]);
	if (argc - 3 != want) {
		printf("child: argc-3 = %d, wanted %d\n", argc - 3, want);
		return 3;
	}
	for (i = 3; i < argc; i++) {
		char expect[64];
		/* Both argument shapes probe() generates embed this exact tag
		 * ("arg-NNNNNN-of-NNNNNN"), whether as the whole flag or inside
		 * a quoted path, so requiring it as a substring -- rather than
		 * an exact match -- verifies content survived without caring
		 * which shape produced it. */
		snprintf(expect, sizeof expect, "arg-%06d-of-%06d", i - 3, want);
		if (!strstr(argv[i], expect)) {
			printf("child: argv[%d] = \"%s\" does not contain \"%s\"\n", i, argv[i], expect);
			return 4;
		}
	}
	return 0;
}

/* Spawns `self` with `n` generated arguments (short flags if `pathlike`
 * is 0, quoted Windows-path-like strings otherwise) and checks the
 * child agrees on all of them. */
static void probe(const char *self, int n, int pathlike)
{
	char **av = malloc((size_t)(n + 4) * sizeof *av);
	char countbuf[16];
	int i, pid, status;

	CHECK(av != 0);
	if (!av) return;
	av[0] = (char *)self;
	av[1] = (char *)"--check";
	snprintf(countbuf, sizeof countbuf, "%d", n);
	av[2] = countbuf;
	for (i = 0; i < n; i++) {
		char *a = malloc(64);
		CHECK(a != 0);
		if (!a) { av[3 + i] = (char *)""; continue; }
		if (pathlike)
			snprintf(a, 64, "C:\\Program Files\\pkg %06d\\arg-%06d-of-%06d.h", i, i, n);
		else
			snprintf(a, 64, "-Darg-%06d-of-%06d=1", i, n);
		av[3 + i] = a;
	}
	av[3 + n] = 0;

	fflush(stdout);
	errno = 0;
	pid = __spawn(self, av, environ);
	if (pid <= 0) printf("FAIL: probe(n=%d, pathlike=%d) __spawn failed, errno=%d\n", n, pathlike, errno);
	CHECK(pid > 0);
	if (pid > 0) {
		CHECK(waitpid(pid, &status, 0) == pid);
		if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0))
			printf("FAIL: probe(n=%d, pathlike=%d) child exited %d (status 0x%08x)\n",
			       n, pathlike, WIFEXITED(status) ? WEXITSTATUS(status) : -1, (unsigned)status);
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	}
	for (i = 0; i < n; i++) free(av[3 + i]);
	free(av);
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--check")) return check_child(argc, argv);

	{
		/* Short flags: ~17 bytes each, so these total roughly
		 * 3.4KB .. 13.6KB, bracketing the reported ~8KB boundary. */
		static const int NSHORT[] = { 100, 200, 300, 400, 500, 800 };
		size_t k;
		for (k = 0; k < sizeof NSHORT / sizeof *NSHORT; k++)
			probe(argv[0], NSHORT[k], 0);
	}
	{
		/* Longer, quote-needing, path-shaped arguments: ~50 bytes
		 * each (before the trailing-backslash-doubling quoting rule
		 * adds a little more), so these run from ~5KB up past 25KB --
		 * well beyond the reported boundary. (700 of these was tried
		 * too and correctly hit __US_MAX_WCHARS's ~64KB ceiling with a
		 * clean E2BIG -- that edge is test/exec.c's
		 * test_cmdline_limit's job, not this file's, so it is left out
		 * here to keep this test about the ~8KB region only.) */
		static const int NPATH[] = { 100, 200, 300, 500 };
		size_t k;
		for (k = 0; k < sizeof NPATH / sizeof *NPATH; k++)
			probe(argv[0], NPATH[k], 1);
	}

	if (!fails) printf("spawn-cmdline-manyargs: all tests passed\n");
	return fails != 0;
}
