/* C library internals and platform ABI fields intentionally use the
 * implementation-reserved namespace so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Entry points for ntlibc's own POSIX.1-2017 (XCU) standard utilities.
 * Each `__util_<name>_main()` is the whole of utility <name>'s logic,
 * implemented once in src/util/<name>.c (mkdir, chmod and printf are the
 * three exceptions -- src/util/mkdir_util.c, src/util/chmod_util.c and
 * src/util/util_printf.c, to avoid colliding with the ar member names
 * src/stat/mkdir.c, src/stat/chmod.c and, for printf, this library's own
 * enormous and heavily-used src/stdio/printf.c already own; see each
 * file's own header comment) and shared by two callers:
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
 * (Tier 2's six -- cut/paste/tr/expand/unexpand/fold -- have no ar
 * member-name collision of their own: `find src -name '<name>.c'` turns
 * up nothing else named cut.c, paste.c, tr.c, expand.c, unexpand.c or
 * fold.c anywhere in this tree, so all six live at the plain
 * src/util/<name>.c this comment describes as the default.)
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

#include <stdlib.h>

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* Diagnostics are always secondary to an error status the utility is
 * already returning.  Check the write, but preserve the primary errno and
 * outcome because a diagnostic failure has no more useful status to report. */
static inline void __util_diagf(const char *fmt, ...)
	__attribute__((format(printf, 1, 2), nonnull(1)));
static inline void __util_diagf(const char *fmt, ...)
{
	int saved_errno = errno;
	va_list ap;
	va_start(ap, fmt);
	if (vfprintf(stderr, fmt, ap) < 0) {
		/* The utility's primary failure remains authoritative. */
	}
	va_end(ap);
	errno = saved_errno;
}

/* Shared checked sizing for the utility implementations below.  Keep the
 * arithmetic out of malloc/realloc arguments so an untrusted input length
 * cannot wrap into a small allocation. */
static inline int __util_size_add(size_t a, size_t b, size_t *out)
{
	if (b > (size_t)-1 - a) return 0;
	*out = a + b;
	return 1;
}

static inline int __util_size_mul(size_t a, size_t b, size_t *out)
{
	if (b && a > (size_t)-1 / b) return 0;
	*out = a * b;
	return 1;
}

withtok(heap_allocated)
static inline void *__util_mallocarray(size_t count, size_t element_size)
{
	size_t bytes;
	if (!__util_size_mul(count, element_size, &bytes)) return NULL;
	return malloc(bytes);
}

withtok(heap_allocated)
static inline void *__util_reallocarray(
	void *ptr consume_if_nonnull_return(heap_allocated), size_t count,
	size_t element_size)
{
	size_t bytes;
	if (!__util_size_mul(count, element_size, &bytes)) return NULL;
	return realloc(ptr, bytes);
}

static inline int __util_array_capacity(size_t current, size_t used, // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
	size_t additional, size_t initial, size_t element_size, size_t *out)
{
	size_t minimum, maximum, capacity;
	if (!initial || !element_size ||
	    !__util_size_add(used, additional, &minimum)) return 0;
	maximum = (size_t)-1 / element_size;
	if (minimum > maximum || current > maximum) return 0;
	capacity = current < initial ? initial : current;
	while (capacity < minimum) {
		if (capacity > maximum / 2) { capacity = minimum; break; }
		capacity *= 2;
	}
	*out = capacity;
	return 1;
}

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

/* Tier 2: text I/O utilities (XCU cat(1p), echo(1p), tee(1p), wc(1p),
 * head(1p), tail(1p)) -- the first batch of the tier after Tier 1's
 * pathname/filesystem utilities above.  Every one of these reads
 * standard input, a file, or both and writes to standard output, so
 * none is __pure__ the way true/false are; each still gets nonnull(2)
 * for the same reason the Tier-1 filesystem utilities above do -- a
 * real argv from a real caller is never NULL, and each formats argv[0]
 * or an operand from argv into a diagnostic on at least one path. */
int __util_cat_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_echo_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_head_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_tail_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_tee_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_wc_main(int argc, char **argv) __attribute__((nonnull(2)));

/* Tier 2 continued: the data-copying/reporting tier (dd(1p), df(1p),
 * du(1p), cksum(1p)) plus the two uuencoding utilities (uuencode(1p),
 * uudecode(1p)) -- see each src/util/<name>.c for its own XCU citations
 * and documented scope narrowings (df's "no operands" case, dd's conv=
 * coverage, du's -r reading).  None is __pure__: dd/uuencode/uudecode
 * read real files (or stdin) and dd/uudecode write them, df/du query
 * and walk the real filesystem, and cksum reads real files -- every one
 * genuinely depends on outside state a repeated call could see change.
 * Each still gets nonnull(2) for the same reason the Tier 1 block above
 * does: a real argv from a real caller is never NULL, and a usage-error
 * path taken with argc==1 still needs argv[0] for its own diagnostic. */
int __util_cksum_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_dd_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_df_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_du_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_uudecode_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_uuencode_main(int argc, char **argv) __attribute__((nonnull(2)));

/* Tier 2 continued: text-formatting/file-splitting utilities (XCU
 * printf(1p), od(1p), pr(1p), tabs(1p), split(1p), csplit(1p)).
 * Alphabetical, same as the tiers above.  None is __pure__:
 * printf/od/pr/tabs write to stdout unconditionally as their whole
 * purpose, and split/csplit do real filesystem I/O creating the piece
 * files.  Each gets nonnull(2) for the same reason as the tiers above:
 * a real argv from a real caller is never NULL, and each function's
 * own usage-error path formats argv[0] into a diagnostic before any
 * argc check could matter. */
int __util_csplit_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_od_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_pr_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_printf_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_split_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_tabs_main(int argc, char **argv) __attribute__((nonnull(2)));

/* Tier 2 continued: sorting/set-operation utilities (XCU sort(1p),
 * uniq(1p), comm(1p), join(1p), tsort(1p)) -- none is __pure__: all
 * read a real file or stdin (or, for sort -o/uniq's second operand,
 * write one), so a repeated call with the same argv is not guaranteed
 * to answer the same way twice (a changed input file, a different
 * stdin stream). Each has its whole logic in src/util/<name>.c, no
 * basename collision with any existing src/ file (checked before
 * naming these -- see this header's own comment above for why that
 * check matters). */
int __util_comm_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_join_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_sort_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_tsort_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_uniq_main(int argc, char **argv) __attribute__((nonnull(2)));

/* Tier 2 continued: text-formatting utilities (XCU cut(1p), paste(1p),
 * tr(1p), expand(1p), unexpand(1p), fold(1p)).  None is __pure__: all
 * six read standard input or a file operand and write to standard
 * output, so a repeated call is not guaranteed to see the same bytes
 * twice (a pipe, a file another process is still writing, etc.) even
 * though none of them ever writes anything back to the filesystem the
 * way the mkdir/rmdir/mkfifo/ln/chmod/touch block above does. */
int __util_cut_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_expand_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_fold_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_paste_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_tr_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_unexpand_main(int argc, char **argv) __attribute__((nonnull(2)));

/* Tier 4: "bigger engines" -- real archive/content-format parsers,
 * rather than line/field-oriented text tools (XCU pax(1p), ar(1p),
 * file(1p)).  Each has its whole logic in src/util/<name>.c EXCEPT
 * file(1p), whose implementation is src/util/util_file.c -- not
 * src/util/file.c -- specifically to avoid colliding with this
 * library's own, unrelated src/stdio/file.c: tcc's `-ar` archiver
 * (this project's own $(AR)) truncates every archive member to its
 * basename, so two different file.c anywhere under src/ would become
 * the same "file.o" member in lib/libc.a (see this header's own
 * comment above, and src/util/util_file.c's, for the full story).
 * pax.c and ar.c have no such collision (checked with `find src -name
 * 'pax.c'`/`find src -name 'ar.c'` before adding them). None of the
 * three is __pure__: pax and ar both do real archive/filesystem I/O
 * by design, and file(1p) at minimum stat()s (and, for a regular
 * file, opens and reads a peek of) every operand. */
int __util_ar_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_file_main(int argc, char **argv) __attribute__((nonnull(2)));
int __util_pax_main(int argc, char **argv) __attribute__((nonnull(2)));

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
withtok(heap_allocated) __attribute__((nonnull(1, 2)))
char *__util_join_basename(const char *dir, const char *src);

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
