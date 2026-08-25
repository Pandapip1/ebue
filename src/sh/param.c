/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The shell's positional parameters (XCU 2.5.1) and the special
 * parameter 0, which is *not* one of them.
 *
 * ---- Why this is a file of its own -------------------------------------
 *
 * Every other piece of shell state this project has needed so far lives
 * in the real `environ`: an assignment is setenv(), an expansion is
 * getenv(), and src/sh/exec.c says so at length.  Positional parameters
 * cannot be stored that way and it is worth being explicit about why,
 * because "just put them in the environment as $1, $2, ..." looks
 * plausible until three separate rules break:
 *
 *  - 2.5.1 makes them an *ordered list* with a length, and `shift`
 *    renumbers the whole list.  Renumbering N environment variables is
 *    not atomic and leaves the list observably wrong halfway through.
 *  - "$@" must expand to a list whose length is known (2.5.2: "if
 *    there are no positional parameters, the expansion of '@' shall
 *    generate zero fields"), and environ has no way to say "there are
 *    exactly three" that a stray inherited `1=...` cannot corrupt.
 *  - 2.9.5 requires a shell function to *save and restore* the caller's
 *    entire list.  A save/restore of a contiguous array is three lines;
 *    of a scattered set of environment entries it is a transaction.
 *
 * And, decisively, XCU 2.5.1's list is not exported: a child process
 * must not inherit the shell's `$1` as an environment variable named
 * "1".  Storing them in environ would export them.
 *
 * So: one array, owned here, never visible to a child.  The array holds
 * $1 at index 0; `n` is the value of `$#` (2.5.2: "the command name
 * (parameter 0) shall not be counted in the number given by '#'").
 *
 * ---- $0 ---------------------------------------------------------------
 *
 * 2.5.2 lists '0' among the *special* parameters, not the positional
 * ones, and 2.5.1 says a positional parameter is "denoted by the
 * decimal value represented by one or more digits, other than the
 * single digit 0".  It therefore lives in its own variable here:
 * `shift` never touches it, `$#` never counts it, `set` never replaces
 * it, and 2.9.5 says a function call leaves it unchanged.  Keeping it
 * out of the array is what makes all four of those true by
 * construction rather than by four separate special cases.
 *
 * Spec pages: XCU 2.5.1 Positional Parameters, 2.5.2 Special
 * Parameters, 2.9.5 Function Definition Command, and the `set`/`shift`
 * special built-ins in 2.14.
 */
#include <string.h>
#include "libc.h"
#include "sh.h"

/* $1..$n, as a NULL-free array of __malloc'd strings: v[k] is $(k+1). */
static char **pv;
static int pn;

/* $0.  NULL means "never set", which only happens in a process that
 * links the engine without going through sh/main.c -- a test binary, or
 * wordexp()'s command substitution inside an arbitrary program.  "sh"
 * is what such a shell is; sh(1p) makes $0 the shell or script name. */
static char *pzero;

static char *dup_str(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = __malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

static void free_vec(char **v, int n)
{
	int i;
	if (!v) return;
	for (i = 0; i < n; i++) __free(v[i]);
	__free(v);
}

const char *__sh_param_zero(void)
{
	return pzero ? pzero : "sh";
}

int __sh_param_set_zero(const char *s)
{
	char *d = dup_str(s);
	if (!d) return -1;
	__free(pzero);
	pzero = d;
	return 0;
}

int __sh_param_count(void)
{
	return pn;
}

/* $n, 1-based.  NULL for an unset parameter -- 2.5.2's '@' talks about
 * "each positional parameter that is set", and an out-of-range index is
 * simply not set.  Callers must not pass 0: that is $0, which is not a
 * positional parameter and comes from __sh_param_zero(). */
const char *__sh_param_get(int n)
{
	if (n < 1 || n > pn) return 0;
	return pv[n - 1];
}

/* Replaces the whole list with a copy of argv[0..n).  This is `set`'s
 * "All positional parameters shall be unset before any new values are
 * assigned", and a function call's "[t]he operands to the command
 * temporarily shall become the positional parameters" (2.9.5).
 *
 * The new array is built *before* the old one is released, so a failure
 * partway through leaves the shell's parameters exactly as they were
 * rather than half-assigned. */
int __sh_params_replace(char *const *argv, int n)
{
	char **nv = 0;
	int i;

	if (n < 0) n = 0;
	if (n > 0) {
		nv = __malloc((size_t)n * sizeof *nv);
		if (!nv) return -1;
		for (i = 0; i < n; i++) {
			nv[i] = dup_str(argv[i]);
			if (!nv[i]) { free_vec(nv, i); return -1; }
		}
	}
	free_vec(pv, pn);
	pv = nv;
	pn = n;
	return 0;
}

/* shift(1p): "Positional parameter 1 shall be assigned the value of
 * parameter (1+n) ... The value n shall be an unsigned decimal integer
 * less than or equal to the value of the special parameter '#'.  ...
 * If n is 0, the positional and special parameters are not changed."
 *
 * Returns -1 without touching anything for an out-of-range n, which is
 * the case shift(1p)'s EXIT STATUS makes a nonzero status. */
int __sh_params_shift(int n)
{
	int i;

	if (n < 0 || n > pn) return -1;
	if (n == 0) return 0;
	for (i = 0; i < n; i++) __free(pv[i]);
	for (i = n; i < pn; i++) pv[i - n] = pv[i];
	pn -= n;
	return 0;
}

/* ---- save/restore, for 2.9.5 and for subshell environments ------------
 *
 * 2.9.5: "When the function completes, the values of the positional
 * parameters and the special parameter '#' shall be restored to the
 * values they had before the function was executed."  Nesting and
 * recursion fall out of that being a *move* rather than a copy: each
 * frame owns the list it took, and there is exactly one owner of every
 * array at every moment, so a recursive call cannot alias its parent's.
 *
 * $0 is deliberately not part of the saved state: 2.9.5 says "[t]he
 * special parameter 0 shall be unchanged", so there is nothing to
 * restore. */
void __sh_params_take(struct sh_params *out)
{
	out->v = pv;
	out->n = pn;
	pv = 0;
	pn = 0;
}

void __sh_params_install(struct sh_params *in)
{
	free_vec(pv, pn);
	pv = in->v;
	pn = in->n;
	in->v = 0;
	in->n = 0;
}

void __sh_params_free(struct sh_params *p)
{
	free_vec(p->v, p->n);
	p->v = 0;
	p->n = 0;
}
