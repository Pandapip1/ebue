/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 6a: a real built-in *dispatcher*, and the built-in utilities
 * the compound-command grammar (stages 6b onward) needs before it is
 * usable at all.
 *
 * ---- Why this file exists ---------------------------------------------
 *
 * Until this stage src/sh/execute.c had exactly one built-in, `cd`, matched
 * with a raw strcmp() on the *unexpanded* first word, and its own
 * comment said so explicitly: "this builtin exists to make stage 4's
 * subshell/brace tests exercisable, not to be a general-purpose builtin
 * dispatcher".  Two things are wrong with growing more built-ins that
 * way, and both are fixed here rather than replicated:
 *
 *  - **It matched the wrong string.**  XCU 2.9.1 ("Command Search and
 *    Execution") searches for the command name *after* the word
 *    expansions of 2.9.1 step 1 have run, so `c=cd; $c /tmp` is a `cd`
 *    and `'cd' /tmp` is one too.  Matching raw source text answers a
 *    different question.  This file is therefore consulted from
 *    exec.c's spawn path with the already-expanded argv, which is the
 *    string 2.9.1 actually names.
 *  - **There was nowhere to record what a built-in *is*.**  2.14
 *    distinguishes *special* built-ins (whose failure exits a
 *    non-interactive shell, 2.8.1) from regular ones, and 2.12 makes
 *    "the working directory" and the shell's own environment part of
 *    the shell execution environment -- so some built-ins must run in
 *    this process and some must not when they are one stage of a
 *    multi-command pipeline (2.12: "each command of a multi-command
 *    pipeline is in a subshell environment").  A strcmp() chain has no
 *    room for either fact; a table has a column for each.
 *
 * ---- Why these particular utilities, and why some of them cannot be
 * programs at all ---------------------------------------------------
 *
 * `test`/`[`, `true` and `false` also exist as real standalone
 * executables now (obj/bin/test.exe etc., src/util/test.c and friends,
 * declared in src/internal/util.h) -- but stay registered here too,
 * deliberately, not as a historical leftover.  sh/main.c's comment
 * about a script getting an honest exit 127 for a missing utility
 * still describes what __find_program() does when PATH lookup fails;
 * what changed is that these three no longer depend on lookup and a
 * working __spawn() succeeding at all.  That is exactly the property
 * that matters at an early bootstrap point (see the memory this
 * project's POSIX-utilities plan cites): a builtin runs in this
 * process, unconditionally, before anything has proven `fork`/`exec`
 * or a populated PATH work yet.  `:` and `exit` have no such
 * standalone form and never could: they are 2.14 special built-ins
 * whose entire effect is on the shell's own execution environment (a
 * subprocess `exit.exe` could never end its parent's execution), the
 * same reason `cd`, `set`, `shift` and `return` below stay builtin-only.
 *
 * Counted across 100887 lines of five real autoconf `configure` scripts
 * (keywords at statement position), `test` is used 5488 times -- 229x
 * more often than the `[ ... ]` spelling, which appears 24 times.  Both
 * are the same utility (see this file's `[` handling), but the ratio is
 * why `test` is not an afterthought to a bracket implementation.
 *
 * What `test` actually implements (XCU test(1p)) is documented in
 * src/util/test.c, where the expression engine now lives.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "libc.h"
#include "sh.h"
#include "util.h"


/* Every bi_*() below is reached only through builtins[].fn, always with
 * the address of a real, on-stack struct sh_builtin_ctx the dispatcher
 * (execute.c's spawn_stage()) builds itself -- never NULL -- and each
 * one dereferences ctx unconditionally on entry (ctx->argc, ctx->status
 * or ctx->last_status), with no defensive check anywhere in this file. */

/* test(1p)/[(1p): the whole expression engine now lives in
 * src/util/test.c as __util_test_main(), shared with the standalone
 * obj/bin/test.exe -- see src/internal/util.h's header comment. */
static int bi_test(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_test(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_test_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== Tier 1: pathname utilities ========================================
 *
 * basename(1p), dirname(1p), pathchk(1p), pwd(1p), and the two non-XCU
 * fellow travelers readlink and realpath (see src/util/readlink.c's own
 * comment for why those two are here).  Every one of these has its whole
 * logic in src/util/<name>.c as __util_<name>_main(), shared with the
 * standalone obj/bin/<name>.exe the same way bi_test() above shares
 * src/util/test.c -- see src/internal/util.h's header comment.  None of
 * the six changes anything XCU 2.12 counts as part of the shell
 * execution environment (unlike `cd`), so `env_effect` is 0 for all of
 * them in the table below, the same as test/true/false. */
static int bi_basename(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_basename(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_basename_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_dirname(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_dirname(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_dirname_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_pathchk(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_pathchk(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_pathchk_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_pwd(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_pwd(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_pwd_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_readlink(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_readlink(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_readlink_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_realpath(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_realpath(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_realpath_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== the trivial four ================================================== */

/* XCU 2.14: ": [argument...] -- This utility shall only expand command
 * arguments.  It is used when a command is needed, as in the then
 * condition of an if command, but nothing is to be done by the
 * command.  EXIT STATUS: Zero."  The expansion has already happened by
 * the time this runs (exec.c calls the dispatcher with expanded argv),
 * which is exactly the specified behaviour. */
static int bi_colon(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_colon(struct sh_builtin_ctx *ctx)
{
	ctx->status = 0;
	return 0;
}

/* XCU true(1p) / false(1p): "shall return with exit code zero" /
 * "shall return with a non-zero exit code".  Regular utilities, not
 * 2.14 special built-ins -- they are built in here only because this
 * platform has no true.exe/false.exe for __find_program() to find (see
 * this file's header). */
static int bi_true(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_true(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_true_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_false(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_false(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_false_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== rm / cp / mv (XCU rm(1p), cp(1p), mv(1p)) ========================
 *
 * Same reason as test/true/false above for staying registered here even
 * though obj/bin/rm.exe, obj/bin/cp.exe and obj/bin/mv.exe also exist
 * now (src/util/rm.c, src/util/cp.c, src/util/mv.c, all declared in
 * src/internal/util.h): a builtin runs in this process, unconditionally,
 * without depending on __find_program()/__spawn() succeeding.  None of
 * the three change anything XCU 2.12 lists as part of the shell
 * execution environment (not the working directory, not a shell
 * variable, not the positional parameters), so `env_effect` is 0 for
 * all three -- a pipeline stage that runs one is free to do so in its
 * own subshell environment exactly like `test` above. */
static int bi_rm(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_rm(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_rm_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_cp(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_cp(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_cp_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_mv(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_mv(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_mv_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== the Tier-1 filesystem utilities ==================================
 *
 * mkdir(1p), rmdir(1p), mkfifo(1p), ln(1p), chmod(1p), touch(1p): same
 * reasoning as test/true/false above -- each also exists as a real
 * standalone obj/bin/<name>.exe (src/util/<name>.c, declared in
 * src/internal/util.h), and stays registered here too so a script run
 * before PATH lookup or __spawn() can be trusted still has them.  None
 * of these six is a 2.14 special built-in and none has any effect on the
 * shell execution environment itself (2.12's list -- working directory,
 * shell variables, open files, and so on) the way `cd` does, so
 * `env_effect` is 0 for all six, same as `test`/`true`/`false` above. */
static int bi_mkdir(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_mkdir(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_mkdir_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_rmdir(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_rmdir(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_rmdir_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_mkfifo(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_mkfifo(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_mkfifo_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_ln(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_ln(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_ln_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_chmod(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_chmod(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_chmod_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_touch(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_touch(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_touch_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== Tier 2: text I/O utilities =======================================
 *
 * cat(1p), echo(1p), tee(1p), wc(1p), head(1p), tail(1p) -- the first
 * batch of the tier after the Tier-1 filesystem utilities above.  Same
 * reasoning as every other batch in this file for staying registered
 * here even though each also exists as a real standalone
 * obj/bin/<name>.exe (src/util/<name>.c, declared in src/internal/
 * util.h): a builtin runs in this process, unconditionally, without
 * depending on __find_program()/__spawn() succeeding.  None of the six
 * changes anything XCU 2.12 lists as part of the shell execution
 * environment, so `env_effect` is 0 for all six below. */
static int bi_cat(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_cat(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_cat_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_echo(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_echo(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_echo_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_tee(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_tee(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_tee_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_wc(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_wc(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_wc_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_head(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_head(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_head_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_tail(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_tail(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_tail_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== the data-copying/reporting tier ===================================
 *
 * dd(1p), df(1p), du(1p), cksum(1p), uuencode(1p), uudecode(1p): same
 * reasoning as the Tier 1 filesystem utilities above -- each also exists
 * as a real standalone obj/bin/<name>.exe (src/util/<name>.c, declared in
 * src/internal/util.h), and stays registered here too so a script run
 * before PATH lookup or __spawn() can be trusted still has them.  None of
 * these six is a 2.14 special built-in and none has any effect on the
 * shell execution environment itself (2.12's list), so `env_effect` is 0
 * for all six, same as every other regular built-in above.  dd(1p)'s own
 * SIGINT handling (src/util/dd.c's header comment) is written to be safe
 * running in-process exactly like this: it never calls exit()/_exit()
 * from its handler, only sets a flag its own copy loop polls, and it
 * restores the previous SIGINT disposition before returning -- so
 * running `dd` as a built-in never leaves this shell process with a
 * stale signal handler or gets torn down by one. */
static int bi_dd(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_dd(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_dd_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_df(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_df(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_df_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_du(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_du(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_du_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_cksum(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_cksum(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_cksum_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_uuencode(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_uuencode(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_uuencode_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_uudecode(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_uudecode(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_uudecode_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== Tier 2: text-formatting/file-splitting utilities =================
 *
 * printf(1p), od(1p), pr(1p), tabs(1p), split(1p), csplit(1p) -- same
 * reasoning as the tier above: each also exists as a real standalone
 * obj/bin/<name>.exe (src/util/<name>.c, or src/util/util_printf.c for
 * printf -- see that file's own header for the ar member-name collision
 * it avoids -- declared in src/internal/util.h), and stays registered
 * here too so a script run before PATH lookup or __spawn() can be
 * trusted still has them.  None of these six is a 2.14 special built-in
 * and none has any effect on the shell execution environment itself
 * (2.12's list), so `env_effect` is 0 for all six. */
static int bi_printf(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_printf(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_printf_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_od(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_od(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_od_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_pr(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_pr(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_pr_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_tabs(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_tabs(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_tabs_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_split(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_split(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_split_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_csplit(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_csplit(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_csplit_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== Tier 2: sorting/set-operation utilities ==========================
 *
 * sort(1p), uniq(1p), comm(1p), join(1p), tsort(1p): same reasoning as
 * the Tier-1 filesystem utilities above -- each also exists as a real
 * standalone obj/bin/<name>.exe (src/util/<name>.c, declared in
 * src/internal/util.h), and stays registered here too so a script run
 * before PATH lookup or __spawn() can be trusted still has them.  None
 * of these five is a 2.14 special built-in and none has any effect on
 * the shell execution environment itself (2.12's list), so `env_effect`
 * is 0 for all five, same as the rest of this table. */
static int bi_sort(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_sort(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_sort_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_uniq(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_uniq(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_uniq_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_comm(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_comm(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_comm_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_join(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_join(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_join_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_tsort(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_tsort(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_tsort_main(ctx->argc, ctx->argv);
	return 0;
}

/* XCU 2.14: "exit [n] -- ... shall cause the shell to exit with the
 * exit status specified by the unsigned decimal integer n.  If n is
 * specified, but its value is not between 0 and 255 inclusively, the
 * exit status is undefined.  ... If n is not specified, the exit status
 * shall be that of the last command executed."
 *
 * `env_mutate == 0` is exec.c's flag for "this command's effect on the
 * shell execution environment is going to be discarded anyway" -- a
 * stage of a multi-command pipeline, which 2.12 places in a subshell
 * environment.  `exit` there exits *that* subshell, so the requested
 * status becomes the stage's status and no shell-wide unwind is
 * started.  The same is true of `( exit 3 )`, but that is handled one
 * level up, by exec_group() consuming the pending exit, because a
 * subshell's body is a whole list rather than a single command. */
static int bi_exit(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_exit(struct sh_builtin_ctx *ctx)
{
	int st;
	/* Explicitly discarded stderr results in this file are secondary
	 * diagnostics for a built-in status that is already determined; another
	 * write to the same failed stream cannot report them more reliably. */

	if (ctx->argc > 1) {
		char *end;
		long v = strtol(ctx->argv[1], &end, 10);
		if (end == ctx->argv[1] || *end) {
			/* 2.8.1: "an error in a special built-in utility ...
			 * shall cause a non-interactive shell to exit"; the
			 * status is implementation-defined and 2 is what
			 * bash/dash use for a numeric-argument error here. */
			(void)fprintf(stderr, "exit: %s: numeric argument required\n", ctx->argv[1]);
			st = 2;
		} else {
			st = (int)(v & 0xff);
		}
	} else {
		st = ctx->last_status;
	}
	ctx->status = st;
	if (ctx->env_mutate) __sh_flow_exit(st);
	return 0;
}

/* XCU cd(1p), and XCU 2.12: "Working directory as set by cd" is part of
 * the shell execution environment, so this can only ever run in the
 * shell's own process -- there is no cd.exe on any platform, and there
 * could not usefully be one.  Moved here from src/sh/execute.c, which
 * implemented it inline against the *unexpanded* first word and said in
 * its own comment that it was "not to be a general-purpose builtin
 * dispatcher"; this file is that dispatcher, and cd is now dispatched
 * on the expanded command name like every other built-in (XCU 2.9.1).
 *
 * Still deliberately not a complete cd(1p): no CDPATH search, no -L/-P
 * logical/physical distinction, no "cd -" to OLDPWD.  PWD and OLDPWD
 * are updated so a later $PWD read is not silently stale. */
static int bi_cd(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_cd(struct sh_builtin_ctx *ctx)
{
	const char *target = ctx->argc > 1 ? ctx->argv[1] : getenv("HOME");
	char *oldcwd, *newcwd;

	if (!target || !*target) {
		/* cd(1p): "If ... HOME is unset or null, the results are
		 * unspecified" -- failing the command is a conforming
		 * choice. */
		ctx->status = 1;
		return 0;
	}
	oldcwd = getcwd(0, 0);
	if (chdir(target) < 0) {
		free(oldcwd);
		ctx->status = 1;
		return 0;
	}
	newcwd = getcwd(0, 0);
	if (oldcwd) setenv("OLDPWD", oldcwd, 1);
	if (newcwd) setenv("PWD", newcwd, 1);
	free(oldcwd);
	free(newcwd);
	ctx->status = 0;
	return 0;
}

/* ==== set / shift: the positional parameters (XCU 2.5.1) =============== */

/* set(1p) with no options and no arguments: "set shall write the names
 * and values of all shell variables in the collation sequence of the
 * current locale.  Each name shall start on a separate line, using the
 * format: "%s=%s\n" ... The value string shall be written with
 * appropriate quoting ... The output shall be suitable for reinput to
 * the shell".
 *
 * "Suitable for reinput" is the part that a naive `printf("%s\n", e)`
 * over environ gets wrong the moment a value contains a space, a '$' or
 * a quote -- and gets wrong *silently*, producing output that looks
 * right and means something else when fed back.  So the value is
 * single-quoted (XCU 2.2.2: "[e]nclosing characters in single-quotes
 * shall preserve the literal value of each character within the
 * single-quotes"), with the one character that cannot appear inside
 * single-quotes -- a single-quote -- written as the standard
 * '\''  splice: close, escape one, reopen.
 *
 * The deviation that remains, stated rather than hidden: this shell's
 * only variable store is the real `environ` (see src/sh/execute.c), so
 * what is listed is the environment, not a separate set of unexported
 * shell variables, and there is no collation-order sort. */
static int write_quoted(const char *v)
{
	if (fputc('\'', stdout) == EOF) return -1;
	for (; *v; v++) {
		if (*v == '\'') {
			if (fputs("'\\''", stdout) < 0) return -1;
		} else if (fputc(*v, stdout) == EOF) return -1;
	}
	return fputc('\'', stdout) == EOF ? -1 : 0;
}

static int set_list_variables(void)
{
	extern char **environ;
	char **e;

	for (e = environ; e && *e; e++) {
		const char *eq = strchr(*e, '=');
		if (!eq) {
			if (fputs(*e, stdout) < 0 || fputc('\n', stdout) == EOF) return -1;
			continue;
		}
		if (fwrite(*e, 1, (size_t)(eq - *e), stdout) != (size_t)(eq - *e) ||
		    fputc('=', stdout) == EOF || write_quoted(eq + 1) < 0 ||
		    fputc('\n', stdout) == EOF) return -1;
	}
	return fflush(stdout) == 0 ? 0 : -1;
}

/* set(1p): "The remaining arguments shall be assigned in order to the
 * positional parameters.  The special parameter '#' shall be set to
 * reflect the number of positional parameters.  All positional
 * parameters shall be unset before any new values are assigned", and
 * "[t]he command set -- without argument shall unset all positional
 * parameters and set the special parameter '#' to zero."
 *
 * Options are not implemented, and that is a refusal rather than a
 * silent no-op: `set -e` that did nothing would change the meaning of
 * every subsequent failure in the script without the script being able
 * to tell, which is exactly what sh/main.c's refusal list exists to
 * prevent.  set(1p)'s EXIT STATUS makes ">0  An invalid option was
 * specified, or an error occurred" the right shape for saying so.
 *
 * `env_effect` is 0 in the table below, not 1, for the same reason
 * `exit`'s is: the no-operand form *writes to standard output*, which a
 * pipeline stage must still do ("set | ..." is an ordinary idiom), so
 * the utility has to run either way and decides for itself which half
 * of its behaviour a subshell environment discards. */
static int bi_set(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_set(struct sh_builtin_ctx *ctx)
{
	int first = 1;

	if (ctx->argc == 1) {
		ctx->status = set_list_variables() == 0 ? 0 : 1;
		return 0;
	}
	if (strcmp(ctx->argv[1], "--") == 0) {
		first = 2;
	} else if (ctx->argv[1][0] == '-' || ctx->argv[1][0] == '+') {
		(void)fprintf(stderr, "set: %s: options are not implemented -- see "
		                "test/sh-design.md\n", ctx->argv[1]);
		ctx->status = 2;
		return 0;
	}
	/* 2.12 puts a multi-command pipeline's stages in a subshell
	 * environment, and this process is not one: renumbering the real
	 * shell's parameters from a stage would leak out of a subshell
	 * that is supposed to be discarded.  Not doing it is
	 * indistinguishable from doing it in a discarded subshell. */
	if (!ctx->env_mutate) { ctx->status = 0; return 0; }
	if (__sh_params_replace(ctx->argv + first, ctx->argc - first) < 0) {
		(void)fprintf(stderr, "set: out of memory\n");
		ctx->status = 2;
		return 0;
	}
	ctx->status = 0;
	return 0;
}

/* shift(1p): "The value n shall be an unsigned decimal integer less
 * than or equal to the value of the special parameter '#'.  If n is not
 * given, it shall be assumed to be 1.  If n is 0, the positional and
 * special parameters are not changed."  EXIT STATUS: "[i]f the n
 * operand is invalid or is greater than "$#" ... a non-zero exit status
 * shall be returned."
 *
 * "Unsigned decimal integer" is taken literally: a leading '-' or '+',
 * or any trailing text, is invalid rather than something to salvage,
 * because `shift $x` with an $x that expanded to nothing or to a word
 * is precisely the case where guessing produces a wrong-but-plausible
 * argument list further down the script. */
static int bi_shift(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_shift(struct sh_builtin_ctx *ctx)
{
	long n = 1;

	if (ctx->argc > 2) {
		(void)fprintf(stderr, "shift: too many operands\n");
		ctx->status = 2;
		return 0;
	}
	if (ctx->argc == 2) {
		const char *a = ctx->argv[1];
		char *end;
		if (!*a || !(*a >= '0' && *a <= '9')) {
			(void)fprintf(stderr, "shift: %s: not an unsigned decimal integer\n", a);
			ctx->status = 2;
			return 0;
		}
		n = strtol(a, &end, 10);
		if (*end) {
			(void)fprintf(stderr, "shift: %s: not an unsigned decimal integer\n", a);
			ctx->status = 2;
			return 0;
		}
	}
	if (n > __sh_param_count()) {
		(void)fprintf(stderr, "shift: can only shift %d positional parameter%s\n",
			__sh_param_count(), __sh_param_count() == 1 ? "" : "s");
		ctx->status = 2;
		return 0;
	}
	/* Same subshell reasoning as bi_set() above. */
	if (!ctx->env_mutate) { ctx->status = 0; return 0; }
	if (__sh_params_shift((int)n) < 0) { ctx->status = 2; return 0; }
	ctx->status = 0;
	return 0;
}

/* ==== return (XCU 2.9.5, return(1p)) =================================== */

/* return(1p): "The return utility shall cause the shell to stop
 * executing the current function or dot script.  If the shell is not
 * currently executing a function or dot script, the results are
 * unspecified."  EXIT STATUS: "The value of the special parameter '?'
 * shall be set to n, an unsigned decimal integer, or to the exit status
 * of the last command executed if n is not specified."
 *
 * "Unspecified" outside a function is resolved as a diagnosed error
 * rather than as an alias for `exit`, which is the other historical
 * choice (return(1p) RATIONALE: "In the System V shell this is an
 * error, whereas in the KornShell, the effect is the same as exit").
 * The System V reading is the conservative one here: a script that
 * writes `return` at top level has almost certainly lost track of where
 * it is, and quietly exiting the whole shell at that point is the kind
 * of silent, plausible-looking behaviour this shell keeps refusing.
 * 2.14 requires the status to be nonzero when a special built-in
 * reports an error without aborting.
 *
 * There is no unwinding to do in that case either -- __sh_flow_return()
 * is what a function call consumes, and setting it with no function
 * frame above would stop the rest of the program for no reason. */
static int bi_return(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_return(struct sh_builtin_ctx *ctx)
{
	int st = ctx->last_status;

	if (ctx->argc > 2) {
		(void)fprintf(stderr, "return: too many operands\n");
		ctx->status = 2;
		return 0;
	}
	if (ctx->argc == 2) {
		const char *a = ctx->argv[1];
		char *end;
		long v;
		if (!*a || !(*a >= '0' && *a <= '9')) {
			(void)fprintf(stderr, "return: %s: not an unsigned decimal integer\n", a);
			ctx->status = 2;
			return 0;
		}
		v = strtol(a, &end, 10);
		if (*end) {
			(void)fprintf(stderr, "return: %s: not an unsigned decimal integer\n", a);
			ctx->status = 2;
			return 0;
		}
		/* "If n is not an unsigned decimal integer, or is greater than
		 * 255, the results are unspecified" -- the same 8-bit
		 * truncation `exit` already applies here, since that is what a
		 * wait status can carry. */
		st = (int)(v & 0xff);
	}
	if (!__sh_in_function()) {
		(void)fprintf(stderr, "return: not currently executing a function\n");
		ctx->status = 2;
		return 0;
	}
	ctx->status = st;
	if (ctx->env_mutate) __sh_flow_return(st);
	return 0;
}

/* ==== the dispatcher ==================================================== */

/* `special` is XCU 2.14's distinction, recorded because 2.8.1 hangs
 * consequences off it; `env_effect` says the utility changes something
 * 2.12 lists as part of the shell execution environment, so exec.c must
 * not run it in-process when the invocation's effect is scoped to a
 * subshell environment that is about to be discarded. */
static const struct sh_builtin builtins[] = {
	{ ":",     1, 0, bi_colon },
	/* `exit`'s env_effect is 0 on purpose: its effect in a subshell
	 * environment *is* the exit status, which bi_exit() produces
	 * either way -- so unlike `cd` it must still run when env_mutate
	 * is 0, and it consults ctx->env_mutate itself to decide whether
	 * to start a shell-wide unwind. */
	{ "exit",  1, 0, bi_exit },
	/* 2.14 special built-ins.  `env_effect` 0 for both: see bi_set()'s
	 * header comment -- each has an output half that must run in a
	 * pipeline stage and a mutating half that must not, so each
	 * consults ctx->env_mutate itself rather than being skipped
	 * wholesale the way `cd` is. */
	{ "set",   1, 0, bi_set },
	{ "shift", 1, 0, bi_shift },
	/* `return`'s env_effect is 0 for the same reason `exit`'s is: its
	 * effect in a subshell environment *is* the exit status, which
	 * bi_return() produces either way, and it consults ctx->env_mutate
	 * itself to decide whether to start an unwind. */
	{ "return", 1, 0, bi_return },
	{ "cd",    0, 1, bi_cd },
	{ "test",  0, 0, bi_test },
	{ "[",     0, 0, bi_test },
	{ "true",  0, 0, bi_true },
	{ "false", 0, 0, bi_false },
	{ "basename", 0, 0, bi_basename },
	{ "dirname",  0, 0, bi_dirname },
	{ "pathchk",  0, 0, bi_pathchk },
	{ "pwd",      0, 0, bi_pwd },
	{ "readlink", 0, 0, bi_readlink },
	{ "realpath", 0, 0, bi_realpath },
	{ "rm",    0, 0, bi_rm },
	{ "cp",    0, 0, bi_cp },
	{ "mv",    0, 0, bi_mv },
	{ "mkdir",  0, 0, bi_mkdir },
	{ "rmdir",  0, 0, bi_rmdir },
	{ "mkfifo", 0, 0, bi_mkfifo },
	{ "ln",     0, 0, bi_ln },
	{ "chmod",  0, 0, bi_chmod },
	{ "touch",  0, 0, bi_touch },
	{ "cat",    0, 0, bi_cat },
	{ "echo",   0, 0, bi_echo },
	{ "tee",    0, 0, bi_tee },
	{ "wc",     0, 0, bi_wc },
	{ "head",   0, 0, bi_head },
	{ "tail",   0, 0, bi_tail },
	{ "dd",       0, 0, bi_dd },
	{ "df",       0, 0, bi_df },
	{ "du",       0, 0, bi_du },
	{ "cksum",    0, 0, bi_cksum },
	{ "uuencode", 0, 0, bi_uuencode },
	{ "uudecode", 0, 0, bi_uudecode },
	{ "printf", 0, 0, bi_printf },
	{ "od",     0, 0, bi_od },
	{ "pr",     0, 0, bi_pr },
	{ "tabs",   0, 0, bi_tabs },
	{ "split",  0, 0, bi_split },
	{ "csplit", 0, 0, bi_csplit },
	{ "sort",  0, 0, bi_sort },
	{ "uniq",  0, 0, bi_uniq },
	{ "comm",  0, 0, bi_comm },
	{ "join",  0, 0, bi_join },
	{ "tsort", 0, 0, bi_tsort },
	{ 0, 0, 0, 0 }
};

const struct sh_builtin *__sh_builtin_lookup(const char *name)
{
	size_t i;
	for (i = 0; builtins[i].name; i++)
		if (strcmp(builtins[i].name, name) == 0) return &builtins[i];
	return 0;
}
