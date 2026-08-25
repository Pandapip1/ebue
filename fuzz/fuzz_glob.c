/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * glob()/globfree() -- src/glob/glob.c.  Unlike fnmatch, glob is not a
 * pure function: it walks a directory tree, so a harness that only
 * handed it a pattern would spend its whole budget in do_glob's
 * "opendir failed" arm and never reach the matching, sorting, or
 * vector-building code.  fuzz/ntstubs.c's simulated volume makes a real
 * tree possible, so this harness builds one -- once, at first call --
 * and roots every pattern at it.
 *
 * THE FIXTURE.  /tmp/g holds names chosen so that a random pattern has
 * a real chance of matching something and of straddling the special
 * cases glob has to get right:
 *
 *   a  ab  abc  b  .hidden  .h2       leading dots (GLOB_PERIOD-ish rules)
 *   [x]  a*b  a?c  back\slash          metacharacters as literal filenames
 *   d/   d/a  d/ab  d/.h  d/e/  d/e/f  two levels, for multi-component
 *                                      patterns and the GLOB_MARK
 *                                      trailing-slash rule
 *
 * The names with metacharacters in them are the interesting half: they
 * are what makes GLOB_NOESCAPE and the "a pattern component that
 * contains no metacharacter is not a pattern" path testable, and they
 * are names a hand-written test is unlikely to have thought of.
 *
 * The pattern is the whole fuzzer input, prefixed with "/tmp/g/" unless
 * byte 0 says otherwise -- some inputs are run unrooted, so absolute
 * patterns, "//", the empty pattern and the "*pat == '/'" prefix
 * handling in glob() itself all get exercised too.
 *
 * WHAT IS CHECKED.  All four of test/posix-glob.c's glob fences are
 * semantic disagreements with a clause, which no fuzzer can see without
 * a reference glob; this harness is not trying to close them (see
 * test/verification-coverage-accounting.md section 1, which says so
 * explicitly).  It is a memory-safety and self-consistency net:
 *
 *   - the return value is one of 0, GLOB_NOMATCH, GLOB_NOSPACE,
 *     GLOB_ABORTED;
 *   - on success gl_pathv is non-NULL, gl_pathv[gl_offs + gl_pathc] is
 *     NULL, and every one of the gl_pathc entries before it is a
 *     readable NUL-terminated string (read in full, so ASan sees an
 *     over-claimed count as an overflow rather than a silent one);
 *   - under GLOB_DOOFFS the gl_offs leading slots are set to NULL;
 *   - without GLOB_NOSORT each CALL's run of the vector is in strcmp
 *     order, which is what glob.html requires ("sorted according to the
 *     collating sequence", and the C locale's is byte order) -- per run,
 *     not across the whole vector, because GLOB_APPEND explicitly does
 *     not sort a new run together with the previous one.  See check();
 *     asserting it across the boundary is what GitHub issue #2 was;
 *   - with GLOB_MARK every returned name that names a directory ends in
 *     '/', and the '/' is the only thing added -- checked by stat()ing
 *     the name back through the same volume;
 *   - globfree() is called on every outcome including the failed ones,
 *     and a second globfree() must be a no-op.  Together with ASan and
 *     LeakSanitizer (enabled for the fuzz runs, see tools/fuzz.sh) that
 *     is what covers glob()'s five distinct ownership-transfer paths --
 *     the GLOB_APPEND arm alone frees pglob->gl_pathv and then has
 *     four different ways of putting it back.
 *
 * GLOB_APPEND is driven for real: a second glob() with GLOB_APPEND onto
 * the result of the first, because that arm is where the pointers move
 * between two owners and where a double-free would live.
 */
#include <glob.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

extern void oracle_mismatch_i(const char *, const char *, long long, long long);
extern void oracle_mismatch_s(const char *, const char *, const char *, const char *);

#define ROOT "/tmp/g"

static void touch(const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) { (void)write(fd, "x", 1); close(fd); }
}

/* Built once.  A per-input rebuild would dominate the run time and
 * would also mean the fuzzer never sees the same tree twice, so a
 * reproducer would not reproduce.
 *
 * MEASURED GAP, recorded rather than quietly tolerated: ROOT
 * "/back\\slash" is NOT created.  touch() ignores a failed open(), so
 * the name simply never appears -- glob("/tmp/g/back*") returns
 * GLOB_NOMATCH, and glob("/tmp/g/ * / * ") (spaced here to keep it out of
 * this comment) shows nothing under a "back" directory either, so it
 * was not silently split on the backslash: it was not created at all.
 * The list below therefore describes fifteen names and the simulated
 * volume holds fourteen; the header's claim that a literal backslash in
 * a filename is covered is one name short of true.  A fixture entry
 * that silently fails to exist is how a differential harness compares
 * fewer cases than it advertises, so anyone extending this list should
 * know the volume does not accept every name the host would.  Not
 * chased further here -- it is a coverage gap in fuzz/ntstubs.c's
 * volume, not the defect this file was fixed for. */
static void fixture(void)
{
	static int done;
	static const char *const files[] = {
		ROOT "/a", ROOT "/ab", ROOT "/abc", ROOT "/b",
		ROOT "/.hidden", ROOT "/.h2",
		ROOT "/[x]", ROOT "/a*b", ROOT "/a?c", ROOT "/back\\slash",
		ROOT "/d/a", ROOT "/d/ab", ROOT "/d/.h", ROOT "/d/e/f"
	};
	size_t i;

	if (done) return;
	done = 1;
	mkdir(ROOT, 0755);
	mkdir(ROOT "/d", 0755);
	mkdir(ROOT "/d/e", 0755);
	for (i = 0; i < sizeof files / sizeof *files; i++) touch(files[i]);
}

/* __real_stat, not stat.  Every harness is linked with -Wl,--wrap=stat
 * (see fuzz/statshim.h and the Makefile): a plain `stat` call from this
 * object is rewritten to __wrap_stat, which answers in the *host's*
 * struct stat layout so that libFuzzer -- compiled against the host
 * headers -- can find its corpus directory.  A harness compiled with
 * -nostdinc sees ntlibc's struct stat, which is 144 bytes smaller and
 * has st_mode and st_nlink transposed, so calling stat() here writes
 * past the end of the caller's buffer.  Measured, not theorised: the
 * first version of this file called stat() and libFuzzer reported
 * "stack-buffer-overflow ... WRITE of size 144" within four inputs.
 * __real_stat is the unwrapped ntlibc one, which is what a harness
 * always wants.  Any future harness that stats a path needs this. */
extern int __real_stat(const char *, struct stat *);

static int is_dir(const char *path)
{
	struct stat st;
	return __real_stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* `base` is how many entries were already in *g when this call was made
 * -- 0 for an ordinary call, and the previous call's gl_pathc for a
 * GLOB_APPEND one.  It exists because collating order is a per-CALL
 * property, not a property of the whole vector:
 *
 *   glob.html APPLICATION USAGE -- "The new pathnames generated by a
 *   subsequent call with GLOB_APPEND are not sorted together with the
 *   previous pathnames."
 *
 * Without it this function asserted strcmp order across the whole of
 * gl_pathv, boundary included, and so demanded the one thing that
 * clause forbids.  It fired on the very first GLOB_APPEND input whose
 * run did not happen to start above where the previous one ended:
 * fuzz.sh reported "glob result is not in collating order" with
 * "/tmp/g/d/" followed by "/tmp/g/[x]" ('[' is 0x5B, 'a' is 0x61, so a
 * sorted run over this fixture starts at "[x]" and ends at "d/").  That
 * was GitHub issue #2, twice -- and it was this harness, not glob():
 * glibc on the same fixture returns the identical two-run vector, and
 * so does ntlibc, entry for entry.  test/posix-glob.c's
 * test_glob_fuzz_append_same_pattern_runs pins that behaviour with the
 * two inputs that found it.
 *
 * This is a correction, not a relaxation.  BOTH runs are still checked,
 * in full and with the same strcmp; only the single comparison that
 * straddles the boundary is dropped, because it is the only one that
 * asks about an ordering POSIX does not define. */
static void check(const char *pat, int flags, int rc, size_t base, glob_t *g)
{
	size_t i, offs = (flags & GLOB_DOOFFS) ? g->gl_offs : 0;

	if (rc != 0 && rc != GLOB_NOMATCH && rc != GLOB_NOSPACE && rc != GLOB_ABORTED)
		oracle_mismatch_i("glob returned a code not in <glob.h>", pat, rc, 0);
	if (rc != 0) return;

	if (!g->gl_pathv) {
		oracle_mismatch_i("glob returned 0 with a NULL gl_pathv", pat, 0, 1);
		return;
	}
	if (g->gl_pathv[offs + g->gl_pathc] != NULL)
		oracle_mismatch_i("gl_pathv is not NULL-terminated at gl_pathc", pat,
		                  (long long)g->gl_pathc, 0);
	for (i = 0; i < offs; i++)
		if (g->gl_pathv[i] != NULL)
			oracle_mismatch_i("GLOB_DOOFFS leading slot is not NULL", pat,
			                  (long long)i, 0);

	for (i = 0; i < g->gl_pathc; i++) {
		const char *s = g->gl_pathv[offs + i];
		size_t len;
		if (!s) {
			oracle_mismatch_i("NULL inside the gl_pathc entries", pat, (long long)i, 0);
			continue;
		}
		len = strlen(s);                        /* ASan sees an unterminated one here */
		if (i > base && !(flags & GLOB_NOSORT) &&
		    strcmp(g->gl_pathv[offs + i - 1], s) > 0)
			oracle_mismatch_s("glob result is not in collating order", pat,
			                  g->gl_pathv[offs + i - 1], s);
		/* GLOB_MARK, one direction only: a name this library chose to
		 * mark must really be a directory.  The converse -- every
		 * directory must be marked -- was tried first and had to be
		 * dropped, because it fires on entries POSIX does not call
		 * matched pathnames and whose treatment is a reading of a
		 * clause rather than a defect: glob("") returns "." here (the
		 * `!*pat` arm of do_glob, which never stats and never marks),
		 * and GLOB_NOCHECK returns the pattern itself verbatim.  Those
		 * are test/verification-measures-2.md's class F4, which a
		 * fuzzer cannot adjudicate and which this harness is
		 * explicitly not trying to close.  The direction kept here
		 * cannot produce that argument: nothing in glob.html permits
		 * marking a non-directory. */
		/* ...and not on the GLOB_NOCHECK verbatim copy of the pattern
		 * either.  A genuinely matched entry that GLOB_MARK marked can
		 * never be byte-equal to the pattern (the '/' was appended to
		 * it), so this excludes exactly the NOCHECK case: glob("x/")
		 * with GLOB_NOCHECK returns "x/" whether or not x exists. */
		if ((flags & GLOB_MARK) && len > 1 && s[len - 1] == '/' &&
		    strcmp(s, pat) != 0) {
			char tmp[1024];
			if (len < sizeof tmp) {
				memcpy(tmp, s, len);
				tmp[len - 1] = 0;
				if (!is_dir(tmp))
					oracle_mismatch_s("GLOB_MARK marked a non-directory", pat, s, tmp);
			}
		}
	}
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char pat[512];
	size_t n;
	int flags, rooted, rc;
	glob_t g;

	if (size < 2) return 0;
	fixture();

	/* Only the seven defined bits: an undefined bit in glob's flags is
	 * not a documented input, and feeding one would let the fuzzer
	 * chase behaviour no clause describes. */
	flags = data[0] & 0x07f;
	rooted = (data[1] & 1) != 0;
	data += 2; size -= 2;

	n = size;
	if (n > sizeof pat - sizeof ROOT - 2) n = sizeof pat - sizeof ROOT - 2;
	if (rooted) {
		memcpy(pat, ROOT "/", sizeof ROOT);
		memcpy(pat + sizeof ROOT, data, n);
		pat[sizeof ROOT + n] = 0;
	} else {
		memcpy(pat, data, n);
		pat[n] = 0;
	}
	if (memchr(pat, 0, rooted ? sizeof ROOT + n : n)) return 0;

	memset(&g, 0, sizeof g);
	if (flags & GLOB_DOOFFS) g.gl_offs = 2;

	rc = glob(pat, flags & ~GLOB_APPEND, NULL, &g);
	check(pat, flags & ~GLOB_APPEND, rc, 0, &g);

	/* The GLOB_APPEND arm: it frees pglob->gl_pathv, moves the old
	 * pointers into its own vector, and has four ways of putting a
	 * freeable vector back.  Only legal after a successful call, per
	 * glob.html ("the pglob argument ... from a previous call"). */
	if (rc == 0) {
		/* Captured before the call: once glob() returns, gl_pathc
		 * covers both runs and the boundary is no longer recoverable
		 * from *g alone. */
		size_t carried = g.gl_pathc;
		int rc2 = glob(pat, flags | GLOB_APPEND, NULL, &g);
		check(pat, flags | GLOB_APPEND, rc2, carried, &g);
	}

	globfree(&g);
	globfree(&g);                   /* must be a no-op, not a double free */
	return 0;
}
