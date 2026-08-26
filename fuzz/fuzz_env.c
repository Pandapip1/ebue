/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getenv/setenv/putenv/unsetenv and the `environ` array they maintain --
 * src/env/getenv.c and src/env/setenv.c.  Small, but it is a
 * hand-managed NULL-terminated pointer array that four functions
 * reallocate, shift down, free elements of, and deliberately do NOT free
 * elements of, with a side table recording which strings belong to the
 * caller.  Every one of those is an ownership rule enforced by code
 * rather than by a type, and the sequence of calls decides whether they
 * hold -- which is why this is a fuzz target and not a unit test.
 *
 * The input is read as a program: a series of records, each an
 * operation (set / set-no-overwrite / put / unset / get) and a NAME or
 * NAME=VALUE string.  A single input therefore performs a whole
 * sequence against one environment, which is what exposes the
 * interesting cases: unsetenv of a name that appears twice (its loop
 * runs until __env_find stops finding it), setenv over a putenv string
 * (which must not be freed), putenv of a string with no '=' (which is
 * specified to behave as unsetenv), and a name containing '=' (EINVAL).
 *
 * PUTENV STRINGS COME FROM A STATIC ARENA, and putenv calls are capped
 * process-wide.  Both are forced by the implementation, and the second
 * is a finding in its own right:
 *
 *   - putenv() takes ownership of the caller's pointer and the
 *     environment keeps it, so a putenv'd string must outlive every
 *     later call.  A stack or heap buffer would be a use-after-free in
 *     the harness, not in the library.
 *   - src/env/setenv.c appends to `putenv_strings` on EVERY putenv()
 *     call, unconditionally -- it does not check whether the pointer is
 *     already tracked -- and nothing ever removes an entry, not even
 *     unsetenv().  So N putenv() calls cost 8N bytes that are never
 *     released, and is_putenv()'s linear scan makes every subsequent
 *     setenv/unsetenv O(N).  A fuzzer runs tens of thousands of inputs a
 *     second, so uncapped this harness would spend its entire budget in
 *     that scan and then exhaust memory.  The growth is reachable
 *     memory, so LeakSanitizer does not report it; it is recorded here
 *     because this is where it was found.
 *
 * THE ENVIRONMENT IS PRUNED BETWEEN INPUTS, for the same reason putenv
 * is capped -- and the finding that forced it is worth recording, since
 * it was reported as a library bug and is not one.
 *
 * Nothing here ever bounded how large `environ` got.  One environment
 * for the life of the process is deliberate: that is what makes an
 * earlier input's leftovers visible to a later one.  But an input runs
 * as many as 128 records, most of which name something new, and no
 * input is obliged to unset what it set -- so the entry count only ever
 * climbs, and check_environ()'s MAXENV backstop is not a property of
 * the library at all.  It is a number this harness was always going to
 * walk past given enough inputs.  It did, at input #32125 of a 120 s
 * run:
 *
 *     MISMATCH environ has no terminator within MAXENV
 *       ntlibc: 4097
 *       glibc : 4096
 *
 * The terminator was exactly where it belonged; there were simply 4096
 * entries in front of it, put there by the 32124 inputs before this
 * one.  Which is why the artefact libFuzzer wrote for it replays clean
 * under `tools/fuzz.sh --repro` -- and why it is worse than a wasted
 * report: the nightly saves its corpus, so a crash artefact that
 * reproduces nothing is replayed at start-up every night until someone
 * deletes it by hand.
 *
 * The same unbounded growth is why a run got slower the longer it went.
 * check_environ() and count_named() each walk the whole array and each
 * run per record, so an input costs time quadratic in an environment
 * that never shrinks.  Measured across that run, exec/s at inputs
 * #7193 / #12695 / #19639 / #28463 was 7193 / 3173 / 1510 / 694 -- the
 * last stretch searching at a tenth the rate of the first, for the same
 * wall clock.
 *
 * env_prune() therefore trims the environment back to ENVLOW entries
 * whenever an input starts with more than ENVHIGH of them.  It keeps
 * the cross-input carry-over the harness wants -- ENVLOW entries of it
 * -- and every branch of the switch below stays reachable; what it
 * gives up is only the unbounded tail, which tested nothing but this
 * file's own arithmetic.  MAXENV stays where it is, doing the job it
 * was named for: with the prune in place an input cannot start above
 * ENVHIGH or add more than 128, so an array that reaches MAXENV has
 * genuinely lost its terminator, and is walked no further than that.
 *
 * WHAT IS ASSERTED.
 *
 *   - environ is always NULL-terminated, every entry before the
 *     terminator is a readable string containing '=', and the number of
 *     entries is bounded -- read in full, so ASan sees a lost
 *     terminator as an overflow rather than a silent walk;
 *   - after setenv(name, value, 1), getenv(name) returns exactly value;
 *   - after setenv(name, value, 0) on an existing name, getenv is
 *     unchanged ("shall not be changed", setenv.html);
 *   - after unsetenv(name), getenv(name) is NULL and NO entry for that
 *     name remains -- the "appears twice" case is the reason unsetenv's
 *     loop exists, and a loop that stops early leaves one behind;
 *   - a name that is empty or contains '=' is EINVAL for both setenv
 *     and unsetenv, and the environment is left alone;
 *   - putenv of a string with no '=' removes the name, per putenv.html's
 *     cross-reference to unsetenv.
 */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

extern char **environ;
extern void oracle_mismatch_i(const char *, const char *, long long, long long);
extern void oracle_mismatch_s(const char *, const char *, const char *, const char *);

#define REC      64
#define MAXENV   4096
#define ENVHIGH  512            /* see env_prune() and the banner */
#define ENVLOW   256
#define PUTARENA 128
#define PUTCAP   1024           /* see the banner */

static char putarena[PUTARENA][REC + 1];
static int putslot;
static int putcalls;

/* environ must stay a NULL-terminated array of NAME=VALUE strings. */
static void check_environ(const char *ctx)
{
	int n = 0;

	if (!environ) return;                   /* legal: an empty environment */
	while (environ[n]) {
		volatile char sink = 0;
		size_t i;
		for (i = 0; environ[n][i]; i++) sink ^= environ[n][i];
		(void)sink;
		if (!memchr(environ[n], '=', i))
			oracle_mismatch_s("an environ entry has no '='", ctx, environ[n], "NAME=VALUE");
		if (environ[n][0] == '=')
			oracle_mismatch_s("an environ entry has an empty name", ctx, environ[n], "NAME=VALUE");
		if (++n > MAXENV) {
			oracle_mismatch_i("environ has no terminator within MAXENV", ctx, n, MAXENV);
			return;
		}
	}
}

/* How many entries name `name`?  unsetenv must leave zero.
 *
 * THE COMPARISON IS CASE-INSENSITIVE, and that is not a shortcut: this
 * is a Windows libc, and src/env/getenv.c's name_eq() folds case on
 * purpose -- a program asking for "PATH" has to find the "Path" that
 * Windows actually puts in the environment block.  __env_find(), and
 * therefore getenv(), setenv() and unsetenv(), all inherit that.  A
 * case-sensitive count here disagrees with the library about what
 * "the same name" means, and since the fuzzer keeps one environment
 * across every input in the process, an earlier input that set "N"
 * makes a later getenv("n") succeed while the count says zero.  The
 * first version of this harness did exactly that and reported
 * "getenv returned a value for a name not in environ" on the input
 * "@nn=" -- a harness defect, not a library one. */
static int count_named(const char *name)
{
	size_t l = strlen(name);
	int n = 0, i;

	if (!environ) return 0;
	for (i = 0; environ[i]; i++)
		if (!strncasecmp(environ[i], name, l) && environ[i][l] == '=') n++;
	return n;
}

/* How many entries environ has, stopping at MAXENV so that an array
 * whose terminator has been lost is a bounded walk here too. */
static int env_count(void)
{
	int n = 0;

	if (!environ) return 0;
	while (n <= MAXENV && environ[n]) n++;
	return n;
}

/* Trim the environment back to ENVLOW entries once it passes ENVHIGH.
 * See the banner for what went wrong without this.
 *
 * Pruning through unsetenv() rather than by rewriting environ directly
 * is the point: it retires an entry down the same ownership path
 * everything else here uses, so a putenv'd string is removed the way
 * src/env/setenv.c expects and is_putenv() still keeps it from being
 * free()d.  The cost is that the loop leans on one of the functions
 * under test, so it is bounded twice -- it gives up if unsetenv()
 * fails, and it gives up if a call does not actually shrink the array.
 * Neither is silently swallowed: whatever made unsetenv() stop removing
 * names is what the assertions in case 3 below exist to report, and
 * they run on the very next record.
 *
 * Called with the environment possibly empty (environ itself NULL), so
 * the environ[0] dereference below is reached only via n > ENVLOW. */
static void env_prune(void)
{
	int n = env_count();

	if (n <= ENVHIGH) return;
	while (n > ENVLOW) {
		char name[REC + 1];
		size_t nlen = strcspn(environ[0], "=");
		int m;

		/* An empty or over-long name is check_environ()'s to report;
		 * every entry here came from a REC-sized record. */
		if (nlen == 0 || nlen > REC) break;
		memcpy(name, environ[0], nlen);
		name[nlen] = 0;
		if (unsetenv(name) != 0) break;
		m = env_count();
		if (m >= n) break;
		n = m;
	}
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	size_t pos = 0;

	env_prune();
	if (size < 2) return 0;

	while (pos + 2 <= size) {
		int op = data[pos] % 5;
		size_t len = data[pos + 1] % REC;
		char rec[REC + 1];
		char name[REC + 1];
		size_t nlen;

		pos += 2;
		if (len > size - pos) len = size - pos;
		memcpy(rec, data + pos, len);
		rec[len] = 0;
		pos += len;
		if (memchr(rec, 0, len)) continue;      /* not one string */

		/* The NAME part is everything before the first '='; setenv and
		 * unsetenv take a name, putenv takes the whole thing. */
		nlen = strcspn(rec, "=");
		memcpy(name, rec, nlen);
		name[nlen] = 0;

		switch (op) {
		case 0:                                 /* setenv, overwrite */
		case 1: {                               /* setenv, no overwrite */
			const char *value = rec[nlen] ? rec + nlen + 1 : "";
			char kept[REC + 1];
			const char *had = getenv(name);
			int overwrite = (op == 0);
			int r;

			kept[0] = 0;
			if (had) { strncpy(kept, had, REC); kept[REC] = 0; }

			errno = 0;
			r = setenv(name, value, overwrite);
			check_environ(rec);

			if (nlen == 0 || rec[0] == '=') {
				/* An empty name: setenv.html gives EINVAL. */
				if (r == 0)
					oracle_mismatch_i("setenv accepted an empty name", rec, 0, -1);
				else if (errno != EINVAL)
					oracle_mismatch_i("setenv(empty name) errno != EINVAL", rec,
					                  errno, EINVAL);
				break;
			}
			if (r != 0) break;                  /* ENOMEM is legal */
			{
				const char *now = getenv(name);
				if (!now)
					oracle_mismatch_s("getenv found nothing after a successful setenv",
					                  rec, "(null)", value);
				else if (overwrite && strcmp(now, value) != 0)
					oracle_mismatch_s("setenv(overwrite) did not store the value",
					                  rec, now, value);
				else if (!overwrite && had && strcmp(now, kept) != 0)
					oracle_mismatch_s("setenv(no overwrite) changed an existing value",
					                  rec, now, kept);
			}
			break;
		}
		case 2: {                               /* putenv */
			char *s;
			if (putcalls >= PUTCAP) break;
			s = putarena[putslot];
			putslot = (putslot + 1) % PUTARENA;
			/* The arena is a ring, and putenv() stores the caller's
			 * pointer rather than a copy, so a slot that is about to
			 * be reused may still BE an environ entry.  Overwriting
			 * it in place would rewrite that entry's text behind the
			 * library's back -- and the first version of this harness
			 * did exactly that, then reported "an environ entry has
			 * no '='" for the entry it had itself just corrupted.
			 * A harness defect, not a library one.  Retiring the name
			 * first is enough: unsetenv() removes the entry that
			 * points here, and is_putenv() keeps setenv.c from
			 * free()ing a pointer it does not own. */
			if (s[0]) {
				char stale[REC + 1];
				size_t sl = strcspn(s, "=");
				memcpy(stale, s, sl);
				stale[sl] = 0;
				if (sl) unsetenv(stale);
			}
			memcpy(s, rec, len + 1);
			putcalls++;
			if (putenv(s) == 0) {
				check_environ(rec);
				if (nlen && s[nlen] == '=') {
					const char *now = getenv(name);
					if (!now || strcmp(now, s + nlen + 1) != 0)
						oracle_mismatch_s("getenv disagrees with what putenv stored",
						                  rec, now ? now : "(null)", s + nlen + 1);
				} else if (nlen) {
					/* No '=': putenv.html defers to unsetenv. */
					if (getenv(name) != 0)
						oracle_mismatch_s("putenv without '=' did not remove the name",
						                  rec, getenv(name), "(null)");
				}
			}
			break;
		}
		case 3: {                               /* unsetenv */
			int r;
			errno = 0;
			r = unsetenv(name);
			check_environ(rec);
			if (nlen == 0) {
				if (r == 0)
					oracle_mismatch_i("unsetenv accepted an empty name", rec, 0, -1);
				else if (errno != EINVAL)
					oracle_mismatch_i("unsetenv(empty name) errno != EINVAL", rec,
					                  errno, EINVAL);
				break;
			}
			if (r != 0) break;
			if (getenv(name) != 0)
				oracle_mismatch_s("getenv still finds an unset name", rec,
				                  getenv(name), "(null)");
			if (count_named(name) != 0)
				oracle_mismatch_i("environ still holds an entry for an unset name",
				                  rec, count_named(name), 0);
			break;
		}
		default: {                              /* getenv */
			const char *v = getenv(name);
			if (v) {
				volatile char sink = 0;
				size_t i;
				for (i = 0; v[i]; i++) sink ^= v[i];
				(void)sink;
				/* The value getenv returns must be the tail of a real
				 * environ entry, not a pointer into nowhere. */
				if (count_named(name) == 0)
					oracle_mismatch_s("getenv returned a value for a name not in environ",
					                  rec, v, "(null)");
			}
			break;
		}
		}
	}
	check_environ("final");
	return 0;
}
