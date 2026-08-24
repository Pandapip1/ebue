/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Regression test for a dangling-RuntimeData bug in __spawn (src/process/
 * spawn.c): RuntimeData -- the field of RTL_USER_PROCESS_PARAMETERS that
 * carries the inherited-descriptor table crt1's __fd_init reads back
 * (src/internal/fd.c) -- used to be set to a pointer to a *separate*
 * heap allocation (__fd_runtime_data's return value) *after*
 * RtlCreateProcessParametersEx had already returned, rather than being
 * handed to that call as its RuntimeInfo argument and packed into the
 * parameters block with everything else.
 *
 * RTL_USER_PROCESS_PARAMETERS crosses into the child as an uninterpreted
 * blob (see spawn.c's comment where RuntimeInfo is now built, citing
 * ReactOS's sdk/lib/rtl/ppb.c and dll/win32/kernel32/client/proc.c,
 * which mirror real Windows here): only the block's own bytes make the
 * trip. A field pointing *outside* the block survives as 8 bytes of
 * parent virtual address, not as a pointer the child can use -- and
 * crt1's __fd_init dereferences it unconditionally whenever it looks
 * nonzero. Two parent processes started from the same binary often get
 * similar enough heap layouts for that stale address to *happen* to be
 * valid in the child too, which is what made this intermittent rather
 * than a hard, always-reproducing failure: downstream, roughly 1 spawn
 * in 150 under make-style load.
 *
 * This spawns many children in a loop, each one inheriting one ordinary
 * (non-standard, not 0/1/2) descriptor purely via the RuntimeData table,
 * and checks that every single one of them can actually read through it
 * -- while deliberately varying argv and environment-block sizes across
 * iterations to churn the parent's heap between spawns, since a flat
 * failure rate is exactly what a coincidentally-still-valid address
 * would hide. (The inherited descriptor is read from, not written to:
 * __fd_init's RuntimeData reconstruction -- a separate, pre-existing bug
 * found while writing this test -- never sets O_WRONLY/O_RDWR on a
 * descriptor it rebuilds, so ntlibc's own write() refuses it with EBADF
 * regardless of the underlying handle's real permissions; O_RDONLY is 0,
 * so a read does not trip that check. That bug is independent of the
 * one this test targets -- it is 100% reproducible, not intermittent --
 * and is not fixed here.)
 *
 * Wine has much lower address-space entropy and a different allocator
 * than real Windows, so a clean pass here is not proof the underlying
 * bug is gone on real Windows -- it is only proof this code path still
 * works at all.  The fix is what src/process/spawn.c's comments and
 * RtlCreateProcessParametersEx call now do; this test exists so a
 * regression -- someone poking a pointer into pp after creation again --
 * has a chance of being caught even under Wine.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

extern char **environ;
int __spawn(const char *, char *const *, char *const *);

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define N_ITERS 300
#define STRESS_FD 5
#define FILLER_MAX 4000

#define RC_BADARGC 2
#define RC_NOENV 3
#define RC_READFAIL 4
#define RC_MISMATCH 5

/* Child role: fd number is argv[2]; read from it and compare against the
 * NTLIBC_STRESS_MARK environment variable, which also exercises env-block
 * marshalling under the same heap churn. */
static int readfd_child(int argc, char **argv)
{
	int fd;
	const char *mark;
	char buf[128];
	ssize_t n;

	if (argc != 3) return RC_BADARGC;
	fd = atoi(argv[2]);
	mark = getenv("NTLIBC_STRESS_MARK");
	if (!mark) return RC_NOENV;
	n = read(fd, buf, sizeof buf - 1);
	if (n <= 0) return RC_READFAIL;
	buf[n] = 0;
	return strcmp(buf, mark) ? RC_MISMATCH : 0;
}

static void test_runtimedata_survives_heap_churn(const char *self)
{
	int i, ok = 0;
	char *filler = malloc(FILLER_MAX + 1);
	int nenv = 0;

	CHECK(filler != 0);
	if (!filler) return;
	while (environ[nenv]) nenv++;

	for (i = 0; i < N_ITERS; i++) {
		int p[2];
		char fdbuf[16], markbuf[64];
		char *av[4];
		char **ev;
		char *markvar = 0, *fillvar = 0;
		int pid, status;
		size_t fillen = (size_t)((i * 137 + 11) % (FILLER_MAX - 32)) + 1;
		size_t marklen;

		/* Vary the size of one environment entry across iterations to
		 * churn the parent's malloc arena between spawns -- the same
		 * kind of variation the downstream report singled out (command
		 * line length, environment size) as plausibly correlated with
		 * failure probability. */
		memset(filler, 'f', fillen);
		filler[fillen] = 0;

		if (pipe(p) != 0) { CHECK(0); continue; }
		/* Land the read end on a fixed, non-standard descriptor so
		 * __fd_init has to reconstruct it from RuntimeData, not from
		 * StandardInput/Output/Error (which are handle-by-value fields,
		 * not pointer fields, and are not what this bug is about). */
		if (dup2(p[0], STRESS_FD) != STRESS_FD) { CHECK(0); close(p[0]); close(p[1]); continue; }
		close(p[0]);

		snprintf(fdbuf, sizeof fdbuf, "%d", STRESS_FD);
		snprintf(markbuf, sizeof markbuf, "MARK-%d-%lu", i, (unsigned long)fillen);
		marklen = strlen(markbuf);

		ev = malloc((size_t)(nenv + 3) * sizeof *ev);
		if (!ev) { CHECK(0); close(p[1]); close(STRESS_FD); continue; }
		markvar = malloc(marklen + sizeof "NTLIBC_STRESS_MARK=");
		fillvar = malloc(FILLER_MAX + sizeof "NTLIBC_STRESS_FILLER=");
		CHECK(markvar != 0 && fillvar != 0);
		if (markvar && fillvar) {
			int k;
			for (k = 0; k < nenv; k++) ev[k] = environ[k];
			sprintf(markvar, "NTLIBC_STRESS_MARK=%s", markbuf);
			sprintf(fillvar, "NTLIBC_STRESS_FILLER=%s", filler);
			ev[nenv] = markvar;
			ev[nenv + 1] = fillvar;
			ev[nenv + 2] = 0;

			av[0] = (char *)self; av[1] = (char *)"--readfd";
			av[2] = fdbuf; av[3] = 0;

			fflush(stdout);
			pid = __spawn(self, av, ev);
			CHECK(pid > 0);

			if (pid > 0) {
				/* The write end feeds the child's read; the parent's
				 * own copy of the read end is already gone (dup2'd
				 * over), so this write cannot deadlock. */
				CHECK((size_t)write(p[1], markbuf, marklen) == marklen);
				close(p[1]);
				close(STRESS_FD);
				CHECK(waitpid(pid, &status, 0) == pid);
				if (WIFEXITED(status) && WEXITSTATUS(status) == 0) ok++;
				else printf("FAIL %s:%d: iter %d exited %d (status 0x%08x)\n",
				            __FILE__, __LINE__, i,
				            WIFEXITED(status) ? WEXITSTATUS(status) : -1, (unsigned)status);
				CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
			} else {
				close(p[1]);
				close(STRESS_FD);
			}
		} else {
			close(p[1]);
			close(STRESS_FD);
		}
		free(ev);
		free(markvar);
		free(fillvar);
	}
	free(filler);
	printf("spawn-runtimedata-stress: %d/%d iterations round-tripped the inherited fd\n",
	       ok, N_ITERS);
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--readfd")) return readfd_child(argc, argv);

	test_runtimedata_survives_heap_churn(argv[0]);

	if (!fails) printf("spawn-runtimedata-stress: all tests passed\n");
	return fails != 0;
}
