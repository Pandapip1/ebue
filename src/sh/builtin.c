/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A real built-in *dispatcher*, and the built-in utilities the
 * compound-command grammar needs to be usable at all.
 *
 * ---- Why this file exists ---------------------------------------------
 *
 * A raw strcmp() on a command's *unexpanded* first word is the naive
 * way to add a built-in, and src/sh/execute.c's dispatcher deliberately
 * does not work that way. Two things are wrong with that approach, and
 * both are fixed here rather than replicated:
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
 * deliberately, not as a historical leftover.  script.c's comment
 * about a script getting an honest exit 127 for a missing utility
 * still describes what __find_program() does when PATH lookup fails;
 * these three simply do not depend on that lookup, or on a working
 * __spawn() succeeding, at all.  That is exactly the property
 * that matters at an early bootstrap point (see the memory this
 * project's POSIX-utilities plan cites): a builtin runs in this
 * process, unconditionally, before anything has proven `fork`/`exec`
 * or a populated PATH work yet.  `:` and `exit` have no such
 * standalone form and never could: they are 2.14 special built-ins
 * whose entire effect is on the shell's own execution environment (a
 * subprocess `exit.exe` could never end its parent's execution), the
 * same reason `cd`, `set`, `shift`, `return` and `umask` below stay
 * builtin-only.
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
#include <sys/stat.h>
#include "libc.h"
#include "sh.h"
#include "util.h"
#include "ownership_stubs.h"


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

/* ==== Tier 2: text-formatting utilities ================================
 *
 * cut(1p), paste(1p), tr(1p), expand(1p), unexpand(1p), fold(1p): same
 * reasoning as the Tier-1 block above -- each also exists as a real
 * standalone obj/bin/<name>.exe (src/util/<name>.c, declared in
 * src/internal/util.h), and stays registered here too so a script run
 * before PATH lookup or __spawn() can be trusted still has them.  None
 * of these six is a 2.14 special built-in and none has any effect on
 * the shell execution environment itself (2.12's list), so `env_effect`
 * is 0 for all six, same as the Tier-1 block. */
static int bi_cut(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_cut(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_cut_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_paste(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_paste(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_paste_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_tr(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_tr(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_tr_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_expand(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_expand(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_expand_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_unexpand(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_unexpand(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_unexpand_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_fold(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_fold(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_fold_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== Tier 4: "bigger engine" utilities =================================
 *
 * patch(1p): same reasoning as every tier above -- it also exists as a
 * real standalone obj/bin/patch.exe (src/util/patch.c, declared in
 * src/internal/util.h), and stays registered here too.  Not a 2.14
 * special built-in and has no effect on the shell execution environment
 * itself, so env_effect is 0, same as the rest of this table. */
static int bi_patch(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_patch(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_patch_main(ctx->argc, ctx->argv);
	return 0;
}

/* Tier 4 continued: sed(1p) -- src/util/sed.c's __util_sed_main() is the
 * whole of it, shared with the standalone obj/bin/sed.exe (declared in
 * src/internal/util.h), registered here too so a script run before PATH
 * lookup or __spawn() can be trusted still has it.  Not a 2.14 special
 * built-in, no effect on the shell execution environment itself, so
 * `env_effect` is 0, same as the rest of this table. */
static int bi_sed(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_sed(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_sed_main(ctx->argc, ctx->argv);
	return 0;
}

/* Tier 4 continued: grep(1p) -- same reasoning as every tier above: it
 * also exists as a real standalone obj/bin/grep.exe (src/util/grep.c,
 * declared in src/internal/util.h), and stays registered here too so a
 * script run before PATH lookup or __spawn() can be trusted still has
 * it.  Not a 2.14 special built-in and has no effect on the shell
 * execution environment itself, so env_effect is 0, same as the rest
 * of this table. */
static int bi_grep(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_grep(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_grep_main(ctx->argc, ctx->argv);
	return 0;
}

/* Tier 4 continued: pax(1p), ar(1p), file(1p) -- archive/content-format
 * utilities.  Each also exists as a real standalone obj/bin/<name>.exe
 * (src/util/pax.c, src/util/ar.c, src/util/util_file.c -- the last
 * named to dodge an ar-member-name collision with this library's own
 * src/stdio/file.c, see src/internal/util.h's own comment), declared
 * in src/internal/util.h, and stays registered here too so a script
 * run before PATH lookup or __spawn() can be trusted still has them.
 * None of these three is a 2.14 special built-in and none has any
 * effect on the shell execution environment itself (2.12's list), so
 * `env_effect` is 0 for all three, same as every other row below. */
static int bi_pax(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_pax(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_pax_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_ar(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_ar(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_ar_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_file(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_file(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_file_main(ctx->argc, ctx->argv);
	return 0;
}

/* Tier 7 (Software Development option group, this project's own plan's
 * final tier): nm(1p), a real from-scratch ELF64 object-file
 * symbol-table reader -- see src/util/nm.c's own header for the full
 * scope writeup (ELF64 only, no archive-member iteration, no PE object
 * support). Also exists as a real standalone obj/bin/nm.exe
 * (src/util/nm.c, declared in src/internal/util.h), registered here too
 * for the same "trusted before PATH lookup/__spawn() succeeds" reason
 * as every other regular built-in above. Not a 2.14 special built-in,
 * no effect on the shell execution environment, so `env_effect` is 0. */
static int bi_nm(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_nm(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_nm_main(ctx->argc, ctx->argv);
	return 0;
}

/* Tier 4 continued: find(1p), xargs(1p), expr(1p), ls(1p) -- same
 * reasoning as every tier above: each also exists as a real standalone
 * obj/bin/<name>.exe (src/util/<name>.c, declared in src/internal/
 * util.h), and stays registered here too so a script run before PATH
 * lookup or __spawn() can be trusted still has them -- which matters
 * more than usual for `find -exec`/`xargs` specifically, since both of
 * those *themselves* depend on __find_program()/__spawn() to run
 * whatever they were told to invoke; running find/xargs/expr/ls
 * in-process here needs none of that to already work.  None of these
 * four is a 2.14 special built-in and none has any effect on the shell
 * execution environment itself (2.12's list -- none of them `cd`s or
 * assigns a shell variable), so `env_effect` is 0 for all four, same
 * as every other regular built-in in this table. */
static int bi_find(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_find(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_find_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_xargs(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_xargs(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_xargs_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_expr(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_expr(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_expr_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_ls(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_ls(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_ls_main(ctx->argc, ctx->argv);
	return 0;
}

/* Tier 4 continued: awk(1p) -- same reasoning as every tier above: it
 * also exists as a real standalone obj/bin/awk.exe (src/util/awk.c,
 * declared in src/internal/util.h), and stays registered here too so
 * a script run before PATH lookup or __spawn() can be trusted still
 * has it. Not a 2.14 special built-in and has no effect on the shell
 * execution environment itself, so env_effect is 0, same as the rest
 * of this table. */
static int bi_awk(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_awk(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_awk_main(ctx->argc, ctx->argv);
	return 0;
}

/* Tier 4 continued: diff(1p), cmp(1p) -- same reasoning as every other
 * block above -- each also exists as a real standalone
 * obj/bin/<name>.exe (src/util/<name>.c, declared in src/internal/
 * util.h), and stays registered here too so a script run before PATH
 * lookup or __spawn() can be trusted still has them.  Neither is a
 * 2.14 special built-in and neither has any effect on the shell
 * execution environment itself (2.12's list), so `env_effect` is 0 for
 * both, same as the rest of this table. */
static int bi_diff(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_diff(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_diff_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_cmp(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_cmp(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_cmp_main(ctx->argc, ctx->argv);
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
 * could not usefully be one.  Implemented here rather than in
 * src/sh/execute.c so it is dispatched on the expanded command name,
 * like every other built-in (XCU 2.9.1), rather than matched against
 * the *unexpanded* first word.
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

/* XCU umask(1p), and XCU 2.12: "The file creation mask ... as set by
 * umask" is part of the shell execution environment, so -- like `cd`
 * just above -- this can only ever usefully run in the shell's own
 * process: a standalone umask.exe would fork, set only ITS OWN mask,
 * and exit, with nothing left to observe the change. So unlike test/
 * true/false there is no src/util/umask.c and no bin/umask.exe (see
 * src/internal/util.h's own header comment for the two-caller pattern
 * this deliberately does not follow here, and this file's own header
 * comment on why `cd`, `set`, `shift` and `return` are the same way).
 *
 * This is also the fix for a real, confirmed bug: every at/batch job
 * body src/util/atbatch.c generates captures the submitting shell's
 * umask and re-emits it as a plain `umask NNNN` line at the top, per
 * at(1p)/batch(1p)'s own requirement to preserve and restore the
 * caller's umask in the job's execution environment -- so a shell with
 * no `umask` builtin at all refused every single at/batch job on its
 * very first line (src/sh/script.c's unimplemented_builtins preflight).
 *
 * SYNOPSIS: "umask [-S] [mask]".  Only an octal mask operand is
 * implemented -- a symbolic one (`umask u+rwx`) is refused with a
 * diagnostic rather than silently misparsed as an octal number; a
 * real, tracked gap, not an oversight (nothing this project generates,
 * including atbatch.c's own output, ever needs it).  -S prints the
 * resulting (or, with no mask operand, the current) mask in symbolic
 * form; without it, only an omitted mask operand prints anything at
 * all -- setting the mask is silent, matching every other shell's own
 * umask(1p). */
static void print_umask_symbolic(unsigned mask)
{
	static const char classes[3] = { 'u', 'g', 'o' };
	int i;

	for (i = 0; i < 3; i++) {
		unsigned bits = (~mask >> ((2 - i) * 3)) & 07u;
		if (i) putchar(',');
		putchar(classes[i]);
		putchar('=');
		if (bits & 4u) putchar('r');
		if (bits & 2u) putchar('w');
		if (bits & 1u) putchar('x');
	}
	putchar('\n');
}

static int bi_umask(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_umask(struct sh_builtin_ctx *ctx)
{
	int argi = 1;
	int symbolic = 0;
	const char *s;
	char *end;
	unsigned long v = 0;
	int bad;

	if (ctx->argc > 1 && !strcmp(ctx->argv[1], "-S")) {
		symbolic = 1;
		argi = 2;
	}

	if (argi >= ctx->argc) {
		if (symbolic) print_umask_symbolic(__umask_get());
		else printf("%04o\n", __umask_get());
		ctx->status = 0;
		return 0;
	}
	if (ctx->argc > argi + 1) {
		(void)fprintf(stderr, "umask: too many operands\n");
		ctx->status = 1;
		return 0;
	}

	s = ctx->argv[argi];
	bad = !*s || *s < '0' || *s > '7';
	if (!bad) {
		v = strtoul(s, &end, 8);
		bad = *end != 0 || v > 07777;
	}
	if (bad) {
		(void)fprintf(stderr,
			"umask: %s: not an octal mode -- symbolic mode operands are not implemented\n", s);
		ctx->status = 1;
		return 0;
	}
	umask((mode_t)v);
	if (symbolic) print_umask_symbolic(__umask_get());
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

/* Shared by bi_set() (prefix "", set(1p)'s own "%s=%s\n") and bi_export()
 * (prefix "export ", export(1p)'s "the format 'export name=value'") --
 * both are this same environ walk with the same reinput-safe quoting,
 * differing only in what precedes each line. */
static int list_variables(const char *prefix)
{
	extern char **environ;
	char **e;

	for (e = environ; e && *e; e++) {
		size_t name_length = strcspn(*e, "=");
		if (fputs(prefix, stdout) < 0) return -1;
		if (!(*e)[name_length]) {
			if (fputs(*e, stdout) < 0 || fputc('\n', stdout) == EOF) return -1;
			continue;
		}
		__ownership_readable_span(*e, name_length);
		if (fwrite(*e, 1, name_length, stdout) != name_length ||
		    fputc('=', stdout) == EOF || write_quoted(*e + name_length + 1) < 0 ||
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
 * to tell, which is exactly what script.c's refusal preflight exists to
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
		ctx->status = list_variables("") == 0 ? 0 : 1;
		return 0;
	}
	if (strcmp(ctx->argv[1], "--") == 0) {
		first = 2;
	} else if (ctx->argv[1][0] == '-' || ctx->argv[1][0] == '+') {
		(void)fprintf(stderr, "set: %s: options are not implemented\n",
		              ctx->argv[1]);
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

/* ==== export (XCU 2.14 special built-in, export(1p)) ===================
 *
 * bi_set()'s header comment above already states the deviation that
 * matters here: this shell's only variable store is the real `environ`
 * (src/sh/execute.c), so every "NAME=value" assignment -- with or
 * without `export` in front of it -- already calls setenv() and is
 * therefore already visible in a subsequently spawned command's real
 * environment (execute.c's build_child_envp() copies straight from
 * `environ`).  There is no separate exported/unexported distinction to
 * maintain, which settles two of export(1p)'s three forms without any
 * new state to track:
 *
 *  - `export NAME=value` performs exactly the setenv() a bare
 *    `NAME=value` assignment already performs.
 *  - `export NAME` on an existing NAME changes nothing: NAME's current
 *    value is already the environ entry export(1p) asks this to
 *    produce.
 *  - `export NAME` on a NAME that is not yet set needs no "pending
 *    export" flag either, for the same reason: this shell's *next*
 *    plain assignment to NAME calls setenv() unconditionally
 *    (exec_assignment_only(), execute.c), producing the identical
 *    environ entry export(1p) would have produced by remembering NAME
 *    now and consulting that memory at assignment time.  There is
 *    nothing to remember between the two, so a bare NAME with no '='
 *    below is a genuine no-op.
 *
 * What does not already exist without this builtin is the
 * `export`/`export -p` listing form (export(1p): "the format
 * 'export name=value'", reusing list_variables() above), and the fact
 * that `export`/`export X=1` was refused outright before this builtin
 * existed -- see src/sh/script.c's unimplemented_builtins and
 * bi_umask()'s own comment above on the real at/batch bug this half of
 * the fix is for: src/util/atbatch.c emits `export NAME=value` at the
 * top of every generated job body, so a shell with no `export` built
 * in refused every real at/batch job on that line.
 *
 * `env_effect` is 0 in the table below, same as bi_set()'s and for the
 * same reason: the no-operand/-p form must still run and print in a
 * pipeline stage ("export -p | ...") even though that stage's
 * assignments must not leak out of its subshell environment, so the
 * mutating half checks ctx->env_mutate itself rather than being
 * skipped wholesale the way `cd` is. */
static int is_valid_name(const char *s) __attribute__((nonnull(1)));
static int is_valid_name(const char *s)
{
	size_t i;

	if (!s[0] || (s[0] >= '0' && s[0] <= '9')) return 0;
	for (i = 0; s[i]; i++) {
		char c = s[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') || c == '_')) return 0;
	}
	return 1;
}

static int bi_export(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_export(struct sh_builtin_ctx *ctx)
{
	int i;
	int status = 0;

	if (ctx->argc == 1 || strcmp(ctx->argv[1], "-p") == 0) {
		if (ctx->argc > 2) {
			(void)fprintf(stderr, "export: -p: too many operands\n");
			ctx->status = 2;
			return 0;
		}
		ctx->status = list_variables("export ") == 0 ? 0 : 1;
		return 0;
	}

	/* Same subshell reasoning as bi_set()/bi_shift() above. */
	if (!ctx->env_mutate) { ctx->status = 0; return 0; }

	for (i = 1; i < ctx->argc; i++) {
		const char *arg = ctx->argv[i];
		size_t namelen = strcspn(arg, "=");
		char *name = __malloc(namelen + 1);

		if (!name) {
			(void)fprintf(stderr, "export: out of memory\n");
			ctx->status = 2;
			return 0;
		}
		__ownership_writable_span(name, namelen);
		__ownership_readable_span(arg, namelen);
		memcpy(name, arg, namelen);
		name[namelen] = 0;
		if (!is_valid_name(name)) {
			(void)fprintf(stderr, "export: %s: not a valid identifier\n", name);
			status = 1;
			__free(name);
			continue;
		}
		/* A bare NAME with no '=' needs no state change at all -- see
		 * this function's own header comment. */
		if (arg[namelen] == '=') setenv(name, arg + namelen + 1, 1);
		__free(name);
	}
	ctx->status = status;
	return 0;
}

/* ==== readonly (XCU 2.14 special built-in, readonly(1p)) ===============
 *
 * Unlike export, this one is not a semantic no-op waiting to be made
 * official: export(1p)'s three forms all turned out to already be true
 * of every assignment this shell performs (bi_export()'s header comment
 * just above), but readonly(1p) asks for something no assignment here
 * does on its own -- "[i]t shall be an error for [a read-only name] to
 * appear as a name in a subsequent ... assignment". That is real
 * enforcement, not bookkeeping, and it needs a real place to check: a
 * name's read-only mark lives in src/sh/readonly.c's small side-table
 * (see that file's header for why `environ` cannot hold it), and every
 * place an assignment actually happens has to consult it. For a plain
 * "NAME=value" command that is exec_assignment_only() (src/sh/
 * execute.c), which now refuses and diagnoses rather than calling
 * setenv() when the name is marked.
 *
 * Two forms follow directly from that side-table:
 *
 *  - `readonly NAME=value` sets NAME (rejected if it is already marked
 *    -- readonly(1p) does not exempt re-declaring the same name from
 *    its own "shall be an error" sentence, and neither does this) and
 *    marks it.
 *  - `readonly NAME` with no '=' marks an existing -- or not yet
 *    existing -- NAME read-only without touching any value. Marking a
 *    name that has never been assigned is deliberately supported rather
 *    than noted as a gap: __sh_readonly_mark() (src/sh/readonly.c)
 *    tracks names, not environ entries, so there is nothing that needs
 *    NAME to already be set first, and the first real assignment
 *    afterwards is exactly what exec_assignment_only()'s new check is
 *    for.
 *
 * What is a stated gap, matching bi_export()'s own header comment on
 * the deviations it chose to state rather than hide: only a bare
 * "NAME=value" command goes through the enforcement point above. An
 * assignment *prefix* on another command ("NAME=value cmd") builds a
 * private child environment (build_child_envp(), execute.c) that never
 * touches this shell's own `environ`, and a `for NAME in ...` loop
 * variable (exec_for(), execute.c) is a third, separate setenv() call
 * site. Neither consults __sh_readonly_is(); a read-only NAME can still
 * be shadowed for one child's environment or driven by a `for` loop
 * without either being refused. Threading the same check through both
 * would mean giving spawn_stage() and exec_for() a way to report a
 * variable-assignment error back up through paths that today only ever
 * return "ran" or "OOM" -- a real change to those two functions' own
 * contracts, not a readonly-specific fix, so it is named here rather
 * than done as a side effect of this builtin.
 *
 * `special` is 1 in the table below: readonly is XCU 2.14's own list.
 * `env_effect` is 0, same as export's and for the same reason -- the
 * no-operand/-p listing form must still run and print in a pipeline
 * stage, so the mutating half (setting a value, marking a name) checks
 * ctx->env_mutate itself instead of the whole built-in being skipped
 * the way `cd` is. */
static int list_readonly_variables(void)
{
	size_t i, n = __sh_readonly_count();

	for (i = 0; i < n; i++) {
		const char *name = __sh_readonly_name(i);
		const char *val = getenv(name);
		if (fputs("readonly ", stdout) < 0 || fputs(name, stdout) < 0) return -1;
		if (val && (fputc('=', stdout) == EOF || write_quoted(val) < 0)) return -1;
		if (fputc('\n', stdout) == EOF) return -1;
	}
	return fflush(stdout) == 0 ? 0 : -1;
}

static int bi_readonly(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_readonly(struct sh_builtin_ctx *ctx)
{
	int i;
	int status = 0;

	if (ctx->argc == 1 || strcmp(ctx->argv[1], "-p") == 0) {
		if (ctx->argc > 2) {
			(void)fprintf(stderr, "readonly: -p: too many operands\n");
			ctx->status = 2;
			return 0;
		}
		ctx->status = list_readonly_variables() == 0 ? 0 : 1;
		return 0;
	}

	/* Same subshell reasoning as bi_set()/bi_export() above. */
	if (!ctx->env_mutate) { ctx->status = 0; return 0; }

	for (i = 1; i < ctx->argc; i++) {
		const char *arg = ctx->argv[i];
		size_t namelen = strcspn(arg, "=");
		char *name = __malloc(namelen + 1);

		if (!name) {
			(void)fprintf(stderr, "readonly: out of memory\n");
			ctx->status = 2;
			return 0;
		}
		__ownership_writable_span(name, namelen);
		__ownership_readable_span(arg, namelen);
		memcpy(name, arg, namelen);
		name[namelen] = 0;
		if (!is_valid_name(name)) {
			(void)fprintf(stderr, "readonly: %s: not a valid identifier\n", name);
			status = 1;
			__free(name);
			continue;
		}
		if (arg[namelen] == '=') {
			if (__sh_readonly_is(name)) {
				(void)fprintf(stderr, "readonly: %s: readonly variable\n", name);
				status = 1;
				__free(name);
				continue;
			}
			setenv(name, arg + namelen + 1, 1);
		}
		if (__sh_readonly_mark(name) < 0) {
			(void)fprintf(stderr, "readonly: out of memory\n");
			ctx->status = 2;
			__free(name);
			return 0;
		}
		__free(name);
	}
	ctx->status = status;
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

/* ==== Tier 4: "bigger engines" ==========================================
 *
 * ed(1p), m4(1p): same reasoning as every other batch in this file for
 * staying registered here even though each also exists as a real
 * standalone obj/bin/<name>.exe (src/util/<name>.c, declared in
 * src/internal/util.h) -- a builtin runs in this process,
 * unconditionally, without depending on __find_program()/__spawn()
 * succeeding. Both are more stateful than most of the utilities above
 * (ed's edit buffer, m4's macro table), so both take particular care
 * documented in their own src/util/<name>.c header comments to never
 * call exit()/_exit() internally and to leave no static/global state
 * behind that could leak into a later command in this same shell
 * session -- see src/internal/util.h's own Tier 4 comment. Neither has
 * any effect on the shell execution environment itself (2.12's list),
 * so `env_effect` is 0 for both, same as the rest of this table. */
static int bi_ed(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_ed(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_ed_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_m4(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_m4(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_m4_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== Tier 5: process/environment utilities =============================
 *
 * time(1p), timeout: same reasoning as every other batch in this file
 * for staying registered here even though each also exists as a real
 * standalone obj/bin/<name>.exe (src/util/util_time.c,
 * src/util/timeout.c, declared in src/internal/util.h) -- a builtin
 * runs in this process, unconditionally, without depending on
 * __find_program()/__spawn() succeeding to be found in the first
 * place. Neither has any effect on the shell execution environment
 * itself (2.12's list) -- both only spawn and wait on a child of their
 * own -- so `env_effect` is 0 for both, same as the rest of this
 * table; neither is a 2.14 special built-in either. See each file's
 * own header comment for the real, deliberate scope narrowing each
 * documents (timeout is not even an XCU-mandatory utility at all --
 * see its own header comment for how that was verified). */
static int bi_time(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_time(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_time_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_timeout(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_timeout(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_timeout_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== stty(1p), tty(1p) ===================================================
 *
 * Same reasoning as every other row in this table for staying
 * registered here even though each also exists as a real standalone
 * obj/bin/<name>.exe (src/util/stty.c, src/util/tty.c, declared in
 * src/internal/util.h): a builtin runs in this process, unconditionally,
 * without depending on __find_program()/__spawn() succeeding. Neither
 * has any effect on the shell execution environment itself (2.12's
 * list) -- both only read (stty, absent -a/-g, writes) real terminal
 * state on fd 0 -- so `env_effect` is 0 for both, same as the rest of
 * this table; neither is a 2.14 special built-in either. */
static int bi_stty(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_stty(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_stty_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_tty(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_tty(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_tty_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== Tier 8: tput(1p) ===================================================
 *
 * Same reasoning as every other batch in this file for staying
 * registered here even though it also exists as a real standalone
 * obj/bin/tput.exe (src/util/tput.c, declared in src/internal/util.h)
 * -- a builtin runs in this process, unconditionally, without depending
 * on __find_program()/__spawn() succeeding.  No effect on the shell
 * execution environment itself (2.12's list) -- it only reads $TERM and
 * writes to stdout -- so `env_effect` is 0, and it is not a 2.14
 * special built-in either. */
static int bi_tput(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_tput(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_tput_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== Tier 9: SCCS tooling -- admin(1p), get(1p) =========================
 *
 * Same reasoning as every other batch in this file for staying
 * registered here even though each also exists as a real standalone
 * obj/bin/<name>.exe (src/util/admin.c, src/util/get.c, declared in
 * src/internal/util.h) -- a builtin runs in this process,
 * unconditionally, without depending on __find_program()/__spawn()
 * succeeding.  Neither changes anything XCU 2.12 lists as part of the
 * shell execution environment (both only do their own file I/O), so
 * `env_effect` is 0 for both, and neither is a 2.14 special built-in. */
static int bi_admin(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_admin(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_admin_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_get(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_get(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_get_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== Tier 6: terminal messaging =========================================
 *
 * write(1p)/mesg(1p): the plan's own "explicitly deferred / out of
 * scope" tier, revisited -- see src/util/mesg.c and
 * src/util/util_write.c's own header comments for what is real here
 * given ntlibc's one-real-user model. Registered here for the same
 * reason every other tier is: a builtin runs in this process,
 * unconditionally, without depending on __find_program()/__spawn()
 * succeeding. Neither has any effect on the shell execution
 * environment itself, so `env_effect` is 0 for both, same as the rest
 * of this table; neither is a 2.14 special built-in. */
static int bi_mesg(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_mesg(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_mesg_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_write(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_write(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_write_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== at(1p)/batch(1p)/crontab(1p): deferred and scheduled jobs =========
 *
 * Same "also a real standalone obj/bin/<name>.exe, registered here so
 * it works with no __find_program()/__spawn() dependency" reasoning
 * as every other entry in this table -- see src/util/at.c,
 * src/util/batch.c and src/util/crontab.c's own header comments for
 * the real at(1p)/batch(1p)/crontab(1p) semantics each implements.
 *
 * atd and crond -- the daemons that actually run a submitted job or a
 * crontab entry once it is due -- are deliberately NOT registered
 * here at all: src/util/atd.c's and src/util/crond.c's own header
 * comments explain why a long-lived background process is the one
 * shape in this whole project that a shell builtin cannot honestly
 * be. bin/atd.c and bin/crond.c are their only callers. */
static int bi_at(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_at(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_at_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_batch(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_batch(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_batch_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_crontab(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_crontab(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_crontab_main(ctx->argc, ctx->argv);
	return 0;
}

static int bi_mailx(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_mailx(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_mailx_main(ctx->argc, ctx->argv);
	return 0;
}

/* man(1p) -- see src/util/man.c's own header comment for the real
 * troff-macro-subset formatter this wraps. env_effect is 0: it only
 * ever reads $MANPATH pages and writes to stdout/a pager, changing
 * nothing XCU 2.12 lists as part of the shell execution environment. */
static int bi_man(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_man(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_man_main(ctx->argc, ctx->argv);
	return 0;
}

/* ==== Tier 7 (Software Development option tier): strip(1p) ==============
 *
 * Same reasoning as every other batch in this file for staying
 * registered here even though it also exists as a real standalone
 * obj/bin/strip.exe (src/util/strip.c, declared in src/internal/util.h)
 * -- a builtin runs in this process, unconditionally, without depending
 * on __find_program()/__spawn() succeeding. No effect on the shell
 * execution environment itself (2.12's list) -- it only rewrites the
 * file named by its own operand -- so `env_effect` is 0, and it is not
 * a 2.14 special built-in either. */
static int bi_strip(struct sh_builtin_ctx *ctx) __attribute__((nonnull(1)));
static int bi_strip(struct sh_builtin_ctx *ctx)
{
	ctx->status = __util_strip_main(ctx->argc, ctx->argv);
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
	/* env_effect 0, same as `set`/`shift` just above and for the same
	 * reason: see bi_export()'s own header comment. */
	{ "export", 1, 0, bi_export },
	/* env_effect 0, same as `export` just above and for the same
	 * reason: see bi_readonly()'s own header comment. */
	{ "readonly", 1, 0, bi_readonly },
	/* `return`'s env_effect is 0 for the same reason `exit`'s is: its
	 * effect in a subshell environment *is* the exit status, which
	 * bi_return() produces either way, and it consults ctx->env_mutate
	 * itself to decide whether to start an unwind. */
	{ "return", 1, 0, bi_return },
	{ "cd",    0, 1, bi_cd },
	/* env_effect 1, same as `cd` just above: umask is XCU 2.12's file
	 * creation mask, so a pipeline stage's own invocation must not
	 * actually change it -- see exec.c's header comment on this
	 * column. */
	{ "umask", 0, 1, bi_umask },
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
	{ "cut",      0, 0, bi_cut },
	{ "paste",    0, 0, bi_paste },
	{ "tr",       0, 0, bi_tr },
	{ "expand",   0, 0, bi_expand },
	{ "unexpand", 0, 0, bi_unexpand },
	{ "fold",     0, 0, bi_fold },
	{ "patch", 0, 0, bi_patch },
	{ "sed",   0, 0, bi_sed },
	{ "grep",  0, 0, bi_grep },
	{ "pax",  0, 0, bi_pax },
	{ "ar",   0, 0, bi_ar },
	{ "file", 0, 0, bi_file },
	{ "nm",   0, 0, bi_nm },
	{ "find",  0, 0, bi_find },
	{ "xargs", 0, 0, bi_xargs },
	{ "expr",  0, 0, bi_expr },
	{ "ls",    0, 0, bi_ls },
	{ "awk",   0, 0, bi_awk },
	{ "ed",    0, 0, bi_ed },
	{ "m4",    0, 0, bi_m4 },
	{ "diff",  0, 0, bi_diff },
	{ "cmp",   0, 0, bi_cmp },
	{ "time",    0, 0, bi_time },
	{ "timeout", 0, 0, bi_timeout },
	{ "stty",    0, 0, bi_stty },
	{ "tty",     0, 0, bi_tty },
	{ "tput",    0, 0, bi_tput },
	{ "admin",   0, 0, bi_admin },
	{ "get",     0, 0, bi_get },
	{ "mesg",    0, 0, bi_mesg },
	{ "write",   0, 0, bi_write },
	{ "at",      0, 0, bi_at },
	{ "batch",   0, 0, bi_batch },
	{ "crontab", 0, 0, bi_crontab },
	{ "mailx",   0, 0, bi_mailx },
	{ "man",     0, 0, bi_man },
	{ "strip",   0, 0, bi_strip },
	{ 0, 0, 0, 0 }
};

const struct sh_builtin *__sh_builtin_lookup(const char *name)
{
	size_t i;
	for (i = 0; builtins[i].name; i++)
		if (strcmp(builtins[i].name, name) == 0) return &builtins[i];
	return 0;
}
