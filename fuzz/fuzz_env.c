/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fuzzes getenv/setenv/putenv/unsetenv and the `environ` array
 * (src/env/getenv.c, setenv.c) -- a hand-managed NULL-terminated pointer
 * array whose ownership rules (what gets freed, what must not be) are
 * enforced by code, not types, and only a sequence of calls can violate.
 * The input is read as a program: a series of (operation, NAME or
 * NAME=VALUE) records run against one persistent environment, so a
 * single input can build the interesting cases directly -- a name that
 * appears twice, setenv over a putenv'd string, putenv with no '=',
 * a name containing '='.
 *
 * putenv() takes ownership of the caller's pointer, so putenv strings
 * come from a static ring arena rather than the stack (a stack buffer
 * would be a harness use-after-free, not a library one) -- retiring a
 * slot's old name via unsetenv() before reuse, since overwriting it in
 * place would corrupt a live environ entry behind the library's back.
 * putenv calls are capped (PUTCAP): src/env/setenv.c's putenv_strings
 * side table grows unconditionally on every call and is never pruned,
 * even by unsetenv(), so is_putenv()'s linear scan would make every
 * later setenv/unsetenv O(N) and eventually exhaust memory -- reachable
 * memory, so invisible to LeakSanitizer.
 *
 * environ itself is pruned back to ENVLOW entries once an input starts
 * above ENVHIGH (env_prune(), via unsetenv() so ownership stays
 * consistent). Without it, one persistent environment across tens of
 * thousands of inputs a run eventually walks check_environ()'s MAXENV
 * backstop into a false "lost terminator" report -- a harness artifact
 * from unbounded growth, not a real bug -- and made every input's
 * check_environ()/count_named() walk slower than the last. With the
 * prune, an array that actually reaches MAXENV has genuinely lost its
 * terminator.
 *
 * count_named()'s comparison is deliberately case-insensitive to match
 * src/env/getenv.c's name_eq(): this is a Windows libc, and a
 * case-sensitive count here would disagree with the library about what
 * "the same name" means (an earlier "N" would make a later "n" lookup
 * succeed while the count said zero) -- caught as a harness false
 * positive before landing here.
 *
 * Asserted: environ stays NULL-terminated with every entry containing
 * '=' and the count bounded (read in full, so ASan sees a lost
 * terminator as an overflow); setenv(...,1) always takes effect and
 * setenv(...,0) never overwrites an existing value; unsetenv() leaves
 * no entry for that name even if it appeared twice; an empty or
 * '='-containing name is EINVAL for setenv/unsetenv; putenv of a string
 * with no '=' removes the name, per putenv.html's cross-reference to
 * unsetenv.
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
#define ENVHIGH  512
#define ENVLOW   256
#define PUTARENA 128
#define PUTCAP   1024

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

/* Case-insensitive to match src/env/getenv.c's name_eq() -- see header. */
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

/* Prunes via unsetenv() rather than rewriting environ directly, so a
 * putenv'd string is retired down the same ownership path setenv.c
 * expects. Bails if unsetenv() fails or stops shrinking the array,
 * rather than looping forever; either is a real failure the next
 * record's assertions will report, not silently swallowed. */
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
			/* A reused slot may still be a live environ entry; retire
			 * its name via unsetenv() before overwriting the text. */
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
