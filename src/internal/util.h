/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Entry points for ntlibc's own POSIX.1-2017 (XCU) standard utilities.
 * Each `__util_<name>_main()` is the whole of utility <name>'s logic,
 * implemented once in src/util/<name>.c (mkdir and chmod are the two
 * exceptions -- src/util/mkdir_util.c and src/util/chmod_util.c, to
 * avoid colliding with the ar member names src/stat/mkdir.c and
 * src/stat/chmod.c already own; see either file's own header comment)
 * and shared by two callers:
 *
 *  - bin/<name>.c, a thin main() building the standalone obj/bin/<name>.exe
 *    -- the same "entry point out here, logic in the library" split
 *    sh/main.c already uses for __sh_main() (src/sh/script.c).
 *  - src/sh/builtin.c's bi_<name>() wrapper, which registers the same
 *    utility as a shell built-in that runs in-process, with no fork/exec
 *    (and so no dependency on __find_program()/__spawn() succeeding) --
 *    see that file's own header comment for why this matters for early
 *    bootstrap, not just convenience.
 *
 * Every __util_<name>_main() takes the same shape argc/argv `main()`
 * does (argv[0] is the utility's own name, matching XCU 2.9.1's "first
 * word" and this platform's PATH-search convention) and returns a real
 * process exit status -- 0 for success, matching each utility's own
 * XCU page's EXIT STATUS section otherwise -- never a raw errno or a
 * boolean.  Neither caller above interprets the return value any
 * further: bin/<name>.c hands it straight to the OS as its own exit
 * code, and bi_<name>() assigns it straight to ctx->status.
 */
#ifndef _NTLIBC_UTIL_H
#define _NTLIBC_UTIL_H

/* Tier 1: pathname utilities (XCU basename(1p), dirname(1p), pathchk(1p),
 * pwd(1p)), plus readlink and realpath -- both real GNU/BSD utilities this
 * project's own POSIX-utilities plan folds into this tier even though
 * neither has an XCU page of its own (see src/util/readlink.c and
 * src/util/realpath.c for the caveat spelled out in full).  None of the
 * six is __pure__: pwd and realpath read the real filesystem, basename/
 * dirname/pathchk/readlink still touch errno or stat() a path, so a
 * repeated call with the same argv is not guaranteed to answer the same
 * way twice -- unlike true(1p)/false(1p) below. */
int __util_basename_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_dirname_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_pathchk_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_pwd_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_readlink_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_realpath_main(int argc, char **argv) __attribute__((nonnull(2)));

/* rm(1p), cp(1p) and mv(1p) do real, potentially destructive filesystem
 * work, so none of them are __pure__ -- unlike true/false below, or
 * test(1p) which never affects the filesystem it inspects. */
int __util_cp_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_mv_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_rm_main(int argc, char **argv) __attribute__((nonnull(2)));

/* Alphabetical.  All six below do real filesystem I/O -- creating,
 * removing, linking or restamping something -- so none is __pure__ the
 * way true/false are; each still gets nonnull(2) because each
 * unconditionally reads argv[0] or argv[1] before any argc check could
 * matter (a usage-error path taken with argc==1 still formats argv[0]
 * into its own diagnostic, or -- test's own reasoning, restated here --
 * simply because a real argv from a real caller is never NULL and this
 * says so). */
int __util_chmod_main(int argc, char **argv) __attribute__((nonnull(2)));
/* Both ignore their arguments entirely and return a fixed status, so
 * both are genuinely side-effect-free regardless of what is passed --
 * pure in the strict __attribute__ sense, not just in the true(1p)/
 * false(1p) naming sense. */
int __util_false_main(int argc, char **argv) __attribute__((__pure__));
int __util_ln_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_mkdir_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_mkfifo_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_rmdir_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_test_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_touch_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_true_main(int argc, char **argv) __attribute__((__pure__));

/* ---- plumbing shared between src/util/cp.c, src/util/mv.c and
 * src/util/rm.c -----------------------------------------------------
 *
 * mv(1p)'s cross-filesystem fallback (rename() failing EXDEV) is a
 * copy-then-remove-source, so it needs cp's file/tree copy and rm's
 * tree removal; cp's own target_dir form and mv's target_dir form need
 * the identical "target/basename(source)" path construction.  Declared
 * here rather than duplicated three times or left static-and-copied,
 * per this project's "genuine duplication is worth avoiding" rule.
 *
 * `force`, where present, is -f's meaning in cp(1p): "If a file
 * descriptor for dest_file cannot be obtained ... unlink dest_file and
 * proceed" -- retry once after unlinking the destination rather than
 * failing outright.  None of the four are __pure__: all touch the
 * filesystem. */
int __util_copy_regular_file(const char *src, const char *dst, int force) __attribute__((nonnull(1, 2)));
int __util_copy_tree(const char *src, const char *dst, int force) __attribute__((nonnull(1, 2)));
int __util_remove_tree(const char *path) __attribute__((nonnull(1)));
char *__util_join_basename(const char *dir, const char *src) __attribute__((nonnull(1, 2)));

#endif
