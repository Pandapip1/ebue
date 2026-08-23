/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 2: executing the AST sh.h/parse.c build -- simple commands
 * only. PATH lookup goes through the existing __find_program()
 * (src/process/find_program.c); starting the process goes through the
 * existing __spawn()/waitpid() (src/process/spawn.c, src/process/
 * wait.c). Nothing here forks: a plain simple command does not need
 * the shell's own process split off, only __spawn's new image, and
 * test/sh-design.md's fork-only-where-POSIX-requires-a-subshell rule
 * is specifically about *subshells* -- '(' compound lists and
 * command substitution, both later stages.
 *
 * Word realization reuses the already-public wordexp() (src/wordexp/
 * wordexp.c) rather than re-implementing quote removal/parameter/
 * pathname expansion here: each sh_word.text is, by construction (see
 * sh.h and parse.c's header comment), already in exactly the form
 * wordexp() expects as one input word -- operator characters cannot
 * appear unquoted in it, and quotes/backslashes are untouched. Passing
 * a command's words through wordexp() with WRDE_APPEND therefore does
 * real parameter/tilde/pathname expansion and field splitting for
 * free, correctly (an unquoted "$FOO" containing spaces splits into
 * several argv entries, a glob expands, etc.) -- something a from-
 * scratch reimplementation here would either have to duplicate or get
 * subtly wrong. A word containing command substitution ($(...) or
 * `...`, real syntax since parse.c's word-boundary fix) surfaces as
 * WRDE_CMDSUB here and is reported as "not yet supported": stage 5 is
 * what wires wordexp's command-substitution call-out to this module's
 * own list execution, which is exactly what turns that error into a
 * result.
 *
 * Deliberately NOT implemented yet, all later stages:
 *   - pipelines of more than one command, and '!' negation (stage 3)
 *   - redirections, including here-documents (stage 3)
 *   - subshells / brace groups (stage 4; needs the fork() this file's
 *     header comment says a plain simple command does not)
 *   - '&' actually backgrounding rather than running synchronously
 *     (job control is out of scope for this project entirely -- see
 *     test/sh-design.md -- but *not waiting* for an async list item
 *     is still future work, tracked here rather than silently assumed)
 * __sh_exec_pipeline()/__sh_exec_command() return -1 (with no status
 * written) for any of these; __sh_exec_list()/__sh_exec_andor() stop
 * and propagate that -1 rather than guessing at a status.
 */
#include <string.h>
#include <stdlib.h>
#include <wordexp.h>
#include <unistd.h>
#include <sys/wait.h>
#include "libc.h"
#include "sh.h"

static char *xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = __malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

static void free_strv(char **v, size_t n)
{
	size_t i;
	if (!v) return;
	for (i = 0; i < n; i++) __free(v[i]);
	__free(v);
}

/* Splits an assignment word's raw text ("NAME=value...", guaranteed by
 * parse.c's is_assignment_word() to have an unquoted '=' right after
 * an unquoted NAME) and expands the value half via wordexp(). Per
 * 2.9.1, only tilde and parameter expansion (plus quote removal) apply
 * to an assignment's value -- no field splitting, no pathname
 * expansion -- but wordexp() does not expose that narrower mode, so
 * this takes its first resulting word and accepts the (documented,
 * rare-in-practice) over-permissiveness of an unquoted glob/multi-word
 * $var in a value expanding more than a strict implementation would;
 * tightening this is exactly the sort of thing stage 5's extraction of
 * wordexp's inlined quote-scan (see test/sh-design.md's stage-5 notes)
 * is for. If expansion fails (most commonly WRDE_CMDSUB -- command
 * substitution in an assignment is not yet supported either), the
 * value falls back to its literal raw text rather than the assignment
 * being silently dropped. On success, *name and *val are __malloc'd
 * and owned by the caller; returns 0, or -1 on a malformed assignment word
 * (should not happen given is_assignment_word) or OOM.
 */
static int split_assignment(const char *raw, char **name, char **val)
{
	const char *eq = strchr(raw, '=');
	size_t nlen;
	wordexp_t we;
	int have_we = 0;
	int rc = 0;

	if (!eq) return -1;
	nlen = (size_t)(eq - raw);
	*name = __malloc(nlen + 1);
	if (!*name) return -1;
	memcpy(*name, raw, nlen);
	(*name)[nlen] = 0;

	if (eq[1] && wordexp(eq + 1, &we, 0) == 0) {
		have_we = 1;
		*val = xstrdup(we.we_wordc ? we.we_wordv[0] : "");
	} else {
		*val = xstrdup(eq[1] ? eq + 1 : "");
	}
	if (!*val) { __free(*name); rc = -1; }
	if (have_we) wordfree(&we);
	return rc;
}

/* Replaces an existing "name=" entry in *vp (freeing the old string),
 * or appends a new one, growing *vp and *cap as needed. Takes
 * ownership of `entry` either way. */
static int env_set(char ***vp, size_t *n, size_t *cap, char *entry, size_t namelen)
{
	char **v = *vp;
	size_t i;
	for (i = 0; i < *n; i++) {
		if (strncmp(v[i], entry, namelen) == 0 && v[i][namelen] == '=') {
			__free(v[i]);
			v[i] = entry;
			return 0;
		}
	}
	if (*n + 1 >= *cap) {
		size_t nc = *cap ? *cap * 2 : 16;
		char **nv = __malloc((nc + 1) * sizeof *nv);
		if (!nv) { __free(entry); return -1; }
		memcpy(nv, v, *n * sizeof *nv);
		__free(v);
		v = *vp = nv;
		*cap = nc;
	}
	v[(*n)++] = entry;
	v[*n] = 0;
	return 0;
}

/* Builds a private envp for a command with an assignment prefix: a
 * full copy of the current environment (never the shell's own
 * `environ` -- test/sh-design.md is explicit that a substituted/run
 * command must never be able to clobber the caller's environ) with
 * each assignment applied on top. *out_n receives the entry count
 * (excluding the NULL terminator); returns NULL on OOM. */
static char **build_child_envp(const struct sh_word *assigns, size_t *out_n)
{
	size_t n = 0, cap, i;
	char **v;
	const struct sh_word *a;

	for (n = 0; __environ && __environ[n]; n++) continue;
	cap = n + 8;
	v = __malloc((cap + 1) * sizeof *v);
	if (!v) return 0;
	for (i = 0; i < n; i++) {
		v[i] = xstrdup(__environ[i]);
		if (!v[i]) { free_strv(v, i); return 0; }
	}
	v[n] = 0;

	for (a = assigns; a; a = a->next) {
		char *name, *val, *entry;
		size_t nlen, vlen;
		if (split_assignment(a->text, &name, &val)) { free_strv(v, n); return 0; }
		nlen = strlen(name);
		vlen = strlen(val);
		entry = __malloc(nlen + 1 + vlen + 1);
		if (!entry) { __free(name); __free(val); free_strv(v, n); return 0; }
		memcpy(entry, name, nlen);
		entry[nlen] = '=';
		memcpy(entry + nlen + 1, val, vlen + 1);
		__free(name);
		__free(val);
		if (env_set(&v, &n, &cap, entry, nlen)) { free_strv(v, n); return 0; }
	}
	*out_n = n;
	return v;
}

/* 2.9.1: a simple command with no cmd_word at all -- only variable
 * assignments -- is still valid, and those assignments affect the
 * *current* execution environment (this process's real environment),
 * not a child's. */
static int exec_assignment_only(const struct sh_command *cmd, int *status)
{
	const struct sh_word *a;
	for (a = cmd->assigns; a; a = a->next) {
		char *name, *val;
		if (split_assignment(a->text, &name, &val)) continue;
		setenv(name, val, 1);
		__free(name);
		__free(val);
	}
	*status = 0;
	return 0;
}

static int exec_simple(const struct sh_command *cmd, int *status)
{
	wordexp_t we;
	const struct sh_word *w;
	int first = 1, rc;
	char *resolved;
	char **envp = 0;
	size_t envn = 0;
	pid_t pid;
	int wstatus;

	if (cmd->redirs) return -1; /* stage 3 */
	if (!cmd->words) return exec_assignment_only(cmd, status);

	for (w = cmd->words; w; w = w->next) {
		rc = wordexp(w->text, &we, first ? 0 : WRDE_APPEND);
		if (rc) {
			if (!first) wordfree(&we);
			return -1; /* most commonly WRDE_CMDSUB -- stage 5 */
		}
		first = 0;
	}
	if (we.we_wordc == 0) { wordfree(&we); *status = 0; return 0; } /* every word expanded away */

	if (cmd->assigns) {
		envp = build_child_envp(cmd->assigns, &envn);
		if (!envp) { wordfree(&we); return -1; }
	}

	resolved = __find_program(we.we_wordv[0], 1);
	if (!resolved) {
		*status = 127; /* command not found -- matches system()'s exit-127 clause */
		wordfree(&we);
		free_strv(envp, envn);
		return 0;
	}

	pid = __spawn(resolved, we.we_wordv, envp);
	__free(resolved);
	wordfree(&we);
	free_strv(envp, envn);
	if (pid < 0) { *status = 126; return 0; } /* found but could not execute */

	if (waitpid(pid, &wstatus, 0) < 0) return -1;
	*status = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 128 + WTERMSIG(wstatus);
	return 0;
}

int __sh_exec_command(const struct sh_command *cmd, int *status)
{
	if (cmd->kind != SH_CMD_SIMPLE) return -1; /* stage 4: subshell/brace */
	return exec_simple(cmd, status);
}

int __sh_exec_pipeline(const struct sh_pipeline *pl, int *status)
{
	int rc;
	if (pl->ncommands != 1) return -1; /* stage 3 */
	rc = __sh_exec_command(&pl->commands[0], status);
	if (rc) return rc;
	if (pl->bang) *status = (*status == 0);
	return 0;
}

int __sh_exec_andor(const struct sh_andor *a, int *status)
{
	int rc = __sh_exec_pipeline(&a->pipeline, status);
	if (rc) return rc;
	for (a = a->next; a; a = a->next) {
		if (a->op == SH_AO_AND && *status != 0) continue;
		if (a->op == SH_AO_OR && *status == 0) continue;
		rc = __sh_exec_pipeline(&a->pipeline, status);
		if (rc) return rc;
	}
	return 0;
}

int __sh_exec_list(const struct sh_list *list, int *status)
{
	const struct sh_list_item *it;
	*status = 0;
	if (!list) return 0;
	for (it = list->items; it; it = it->next) {
		int rc = __sh_exec_andor(it->andor, status);
		if (rc) return rc;
		/* SH_SEP_AMP: true backgrounding is future work -- see this
		 * file's header comment -- so an async item still just runs
		 * synchronously for now, exactly like SH_SEP_SEQ/SH_SEP_END. */
	}
	return 0;
}
