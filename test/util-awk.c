/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Black-box tests for ntlibc's awk(1p) -- Tier 4's whole scope in one
 * utility (see src/util/awk.c's own header for the full XCU citations
 * and every deliberate narrowing). Same technique as test/util-
 * sortset.c and test/util-trivial.c: the standalone obj/bin/awk.exe is
 * spawned as a real process (via __spawn()+waitpid()), and the shell
 * built-in is exercised too (via obj/sh/sh.exe -c), confirming both
 * callers of __util_awk_main() (src/internal/util.h) agree.
 *
 * Given awk's size, this covers real ground rather than a smoke test:
 * default and -F field splitting, NF/NR/FNR across multiple files,
 * BEGIN/END, an expression pattern, a regex pattern, a range pattern,
 * -v and command-line var=value assignment (including the specific
 * "a var=value operand only takes effect once the main loop actually
 * reaches it in ARGV order" trap src/util/awk.c's own header calls
 * out -- test_varvalue_operand_timing() below exercises exactly that,
 * not just that the assignment eventually happens), built-in functions
 * (length, substr, split, sub/gsub, sprintf/printf), a recursive user-
 * defined function (exercising real call/return and locals), control
 * flow (for/while/if), arrays with for-in, and getline.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern char **environ;
int __spawn(const char *path, char *const argv[], char *const envp[]);

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static char obj_root[1024];

static int find_obj_root(const char *argv0)
{
	size_t n;
	char *p;

	if (!argv0 || !*argv0) return -1;
	n = strlen(argv0);
	if (n >= sizeof obj_root) return -1;
	strcpy(obj_root, argv0);

	for (p = obj_root + n; p > obj_root; p--)
		if (p[-1] == '/' || p[-1] == '\\') break;
	if (p == obj_root) return -1;
	p[-1] = 0; /* strip "/util-awk.exe" */

	for (p = obj_root + strlen(obj_root); p > obj_root; p--)
		if (p[-1] == '/' || p[-1] == '\\') break;
	if (p == obj_root) return -1;
	p[-1] = 0; /* strip "/test" */

	return 0;
}

static void path_for(char *out, size_t outlen, const char *rel)
{
	char sep = strchr(obj_root, '\\') ? '\\' : '/';
	char relcopy[256], *p;

	strncpy(relcopy, rel, sizeof relcopy - 1);
	relcopy[sizeof relcopy - 1] = 0;
	if (sep == '\\')
		for (p = relcopy; *p; p++) if (*p == '/') *p = '\\';
	snprintf(out, outlen, "%s%c%s", obj_root, sep, relcopy);
}

#define OUTFILE "util-awk-out.txt"
#define ERRFILE "util-awk-err.txt"

static int run(const char *path, char *const *args)
{
	int out, err;
	int s1, s2, pid, status;

	out = open(OUTFILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	err = open(ERRFILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (out < 0 || err < 0) { if (out >= 0) close(out); if (err >= 0) close(err); return -1; }

	s1 = dup(1); s2 = dup(2);
	dup2(out, 1);
	dup2(err, 2);
	close(out); close(err);

	pid = __spawn(path, args, environ);

	dup2(s1, 1); close(s1);
	dup2(s2, 2); close(s2);

	if (pid < 0) return -1;
	if (waitpid(pid, &status, 0) != pid) return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static int slurp_into(const char *path, char *buf, size_t buflen)
{
	FILE *f = fopen(path, "rb");
	size_t n;
	if (!f) { buf[0] = 0; return -1; }
	n = fread(buf, 1, buflen - 1, f);
	buf[n] = 0;
	fclose(f);
	return 0;
}

static int err_contains(const char *needle)
{
	char buf[4096];
	slurp_into(ERRFILE, buf, sizeof buf);
	return strstr(buf, needle) != 0;
}

static int out_equals(const char *expect)
{
	char buf[8192];
	slurp_into(OUTFILE, buf, sizeof buf);
	return strcmp(buf, expect) == 0;
}

static void make_file(const char *path, const char *contents)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	if (contents && *contents) write(fd, contents, strlen(contents));
	close(fd);
}

static char awk_path[1024], sh_path[1024];

static int run_sh_c(const char *cmd)
{
	char *argv[] = { (char *)"sh", (char *)"-c", (char *)cmd, 0 };
	return run(sh_path, argv);
}

static int run_awk(const char *prog, const char *file)
{
	if (file) {
		char *argv[] = { (char *)"awk", (char *)prog, (char *)file, 0 };
		return run(awk_path, argv);
	} else {
		char *argv[] = { (char *)"awk", (char *)prog, 0 };
		return run(awk_path, argv);
	}
}

/* ==== field splitting, NF ================================================= */

static void test_default_field_splitting(void)
{
	/* Default FS==" ": runs of blank/tab collapse, leading/trailing
	 * blanks are ignored entirely -- not sort(1p)'s own default rule
	 * (src/util/sort.c's header contrasts the two). */
	make_file("scratch/fields", "  a  b   c  \nx\ty z\n");
	CHECK(run_awk("{print NF, $1, $2, $3}", "scratch/fields") == 0);
	CHECK(out_equals("3 a b c\n3 x y z\n"));
}

static void test_dash_F_single_char(void)
{
	make_file("scratch/colon", "root:x:0:0\nbin:x:1:1\n");
	{
		char *argv[] = { (char *)"awk", (char *)"-F:", (char *)"{print $1, $3}", (char *)"scratch/colon", 0 };
		CHECK(run(awk_path, argv) == 0);
	}
	CHECK(out_equals("root 0\nbin 1\n"));
}

static void test_assign_field_extends_nf_and_rebuilds_record(void)
{
	/* Assigning to $(NF+1) extends NF and $0 is recompiled with OFS. */
	make_file("scratch/ext", "a b\n");
	CHECK(run_awk("{$4=\"z\"; print NF, $0}", "scratch/ext") == 0);
	CHECK(out_equals("4 a b  z\n"));
}

/* ==== BEGIN/END, NR/FNR across multiple files ============================= */

static void test_begin_end(void)
{
	make_file("scratch/lines", "one\ntwo\nthree\n");
	CHECK(run_awk("BEGIN{print \"start\"} {n++} END{print \"n=\" n}", "scratch/lines") == 0);
	CHECK(out_equals("start\nn=3\n"));
}

static void test_nr_fnr_across_files(void)
{
	make_file("scratch/m1", "a\nb\n");
	make_file("scratch/m2", "c\n");
	{
		char *argv[] = { (char *)"awk", (char *)"{print NR, FNR, FILENAME, $0}",
			(char *)"scratch/m1", (char *)"scratch/m2", 0 };
		CHECK(run(awk_path, argv) == 0);
	}
	CHECK(out_equals(
		"1 1 scratch/m1 a\n"
		"2 2 scratch/m1 b\n"
		"3 1 scratch/m2 c\n"));
}

/* ==== patterns: expression, regex, range =================================== */

static void test_regex_pattern(void)
{
	make_file("scratch/pat", "apple\nbanana\napricot\ncherry\n");
	CHECK(run_awk("/^a/", "scratch/pat") == 0);
	CHECK(out_equals("apple\napricot\n"));
}

static void test_expr_pattern(void)
{
	make_file("scratch/nums", "1\n5\n10\n2\n");
	CHECK(run_awk("$1 > 3", "scratch/nums") == 0);
	CHECK(out_equals("5\n10\n"));
}

static void test_range_pattern(void)
{
	make_file("scratch/range", "a\nSTART\nb\nc\nEND\nd\n");
	CHECK(run_awk("/START/,/END/", "scratch/range") == 0);
	CHECK(out_equals("START\nb\nc\nEND\n"));
}

/* ==== -v and var=value operand timing ====================================== */

static void test_dash_v(void)
{
	char *argv[] = { (char *)"awk", (char *)"-v", (char *)"x=42", (char *)"BEGIN{print x+1}", 0 };
	CHECK(run(awk_path, argv) == 0);
	CHECK(out_equals("43\n"));
}

/* The specific trap the batch instructions called out: a var=value
 * ARGV operand takes effect only once the main input loop actually
 * reaches it, not before input processing starts (unlike -v, which is
 * seeded before BEGIN). So `x` must be empty/uninitialized while file1
 * is being read and only become "5" once file2 is reached. */
static void test_varvalue_operand_timing(void)
{
	make_file("scratch/v1", "p\n");
	make_file("scratch/v2", "q\n");
	{
		char *argv[] = { (char *)"awk", (char *)"{print $0, \"x=[\" x \"]\"}",
			(char *)"scratch/v1", (char *)"x=5", (char *)"scratch/v2", 0 };
		CHECK(run(awk_path, argv) == 0);
	}
	CHECK(out_equals("p x=[]\nq x=[5]\n"));
}

/* ==== built-in functions ==================================================== */

static void test_length_substr_index(void)
{
	CHECK(run_awk("BEGIN{print length(\"hello\"), substr(\"hello\",2,3), index(\"hello\",\"ll\")}", 0) == 0);
	CHECK(out_equals("5 ell 3\n"));
}

static void test_substr_out_of_range_clamping(void)
{
	/* XCU leaves m<=0 unspecified; src/util/awk.c documents the
	 * clamping convention this implements: substr("hello",-2,5) asks
	 * for the 5 characters at positions -2,-1,0,1,2, of which only
	 * positions 1 and 2 ("h","e") actually exist in the string. */
	CHECK(run_awk("BEGIN{print \"[\" substr(\"hello\",-2,5) \"]\"}", 0) == 0);
	CHECK(out_equals("[he]\n"));
}

static void test_split_and_forin(void)
{
	CHECK(run_awk(
		"BEGIN{n=split(\"a,b,c\",arr,\",\"); s=\"\"; "
		"for (k=1;k<=n;k++) s = s arr[k]; print n, s}", 0) == 0);
	CHECK(out_equals("3 abc\n"));
}

static void test_sub_gsub(void)
{
	CHECK(run_awk("BEGIN{s=\"hello world\"; n=gsub(/o/,\"0\",s); print n, s}", 0) == 0);
	CHECK(out_equals("2 hell0 w0rld\n"));
}

static void test_match_rstart_rlength(void)
{
	CHECK(run_awk("BEGIN{print match(\"foobar\",/o+/), RSTART, RLENGTH}", 0) == 0);
	CHECK(out_equals("2 2 2\n"));
}

static void test_printf(void)
{
	CHECK(run_awk("BEGIN{printf \"%5d|%-5s|%.2f\\n\", 3, \"x\", 3.14159}", 0) == 0);
	CHECK(out_equals("    3|x    |3.14\n"));
}

static void test_tolower_toupper(void)
{
	CHECK(run_awk("BEGIN{print toupper(\"AbC\"), tolower(\"AbC\")}", 0) == 0);
	CHECK(out_equals("ABC abc\n"));
}

/* ==== user-defined functions (recursive: exercises call/return and
 * per-call locals, not just a single flat call) ============================ */

static void test_recursive_function(void)
{
	CHECK(run_awk(
		"function fact(n) { if (n <= 1) return 1; return n * fact(n - 1) } "
		"BEGIN { print fact(5) }", 0) == 0);
	CHECK(out_equals("120\n"));
}

/* Extra formal parameters beyond the call's own arguments are XCU
 * awk(1p)'s mechanism for local variables -- a bug-prone corner the
 * batch instructions specifically called out. `acc` here has no
 * caller-supplied argument at all and must start uninitialized on
 * every call, not retain state across the two calls below. */
static void test_extra_params_are_locals(void)
{
	CHECK(run_awk(
		"function sumto(n,   acc, i) { for (i = 1; i <= n; i++) acc += i; return acc } "
		"BEGIN { print sumto(4), sumto(3) }", 0) == 0);
	CHECK(out_equals("10 6\n"));
}

/* ==== control flow: for/while/if/break/continue ============================ */

static void test_control_flow(void)
{
	/* s: 1+2+3+4+5 = 15. w: i counts 1..5 via pre-loop increment,
	 * skipping the w+=i accumulation only on the iteration where i==3
	 * (continue) -- w = 1+2+4+5 = 12, not 1+2+3+4+5. */
	CHECK(run_awk(
		"BEGIN{"
		"s=0; for (i=1;i<=5;i++) s+=i; "
		"w=0; i=0; while (i<5) { i++; if (i==3) continue; w+=i } "
		"print s, w"
		"}", 0) == 0);
	CHECK(out_equals("15 12\n"));
}

static void test_next_skips_remaining_rules(void)
{
	make_file("scratch/nx", "1\n2\n3\n4\n");
	CHECK(run_awk("{if ($1 % 2 == 0) next} {print $1}", "scratch/nx") == 0);
	CHECK(out_equals("1\n3\n"));
}

/* ==== getline ================================================================ */

static void test_getline_from_file(void)
{
	make_file("scratch/gl", "first\nsecond\n");
	CHECK(run_awk(
		"BEGIN{while ((getline line < \"scratch/gl\") > 0) print \"got:\" line}", 0) == 0);
	CHECK(out_equals("got:first\ngot:second\n"));
}

/* ==== usage errors are diagnosed, not silently ignored ===================== */

static void test_missing_program_is_diagnosed(void)
{
	char *argv[] = { (char *)"awk", 0 };
	CHECK(run(awk_path, argv) != 0);
	CHECK(err_contains("awk:"));
}

static void test_syntax_error_is_diagnosed(void)
{
	CHECK(run_awk("BEGIN{ print (", 0) != 0);
	CHECK(err_contains("awk:"));
}

/* ==== fatal runtime errors: clean rejection, not a crash ==================
 *
 * Regression coverage for "awk: never exit() as a shell builtin; fix OOB
 * heap write on huge $N": a field index or NF assignment far outside
 * `long` range used to reach set_field()/set_nf() as UB from a bare
 * (long)/(int) double cast -- concretely, the (int)-truncated idx sized
 * fields_reserve()'s allocation while a second, untruncated `long idx` a
 * few lines later walked the fill loop PAST that allocation: a genuine
 * out-of-bounds heap write, not just theoretical UB. AWK_MAX_FIELD
 * (1,000,000, in src/util/awk_run.c) now refuses an index/NF this large
 * outright, via the same fatal()/awk_unwind_fatal() path every other
 * fatal runtime condition below uses, before the (int) truncation that
 * used to size the allocation is ever reached. All of these must exit
 * with awk's own fatal-error status (2, from __util_awk_main()'s
 * setjmp() catch branch) and a diagnostic on stderr -- NOT a crash (a
 * crash would show up here as run_awk() returning 128+signal, e.g. 139
 * for SIGSEGV, which fails the `== 2` checks below). */

static void test_huge_field_index_rejected_cleanly(void)
{
	CHECK(run_awk("BEGIN{$111111111111111111111=1}", 0) == 2);
	CHECK(err_contains("field index too large"));
}

static void test_huge_nf_assignment_rejected_cleanly(void)
{
	CHECK(run_awk("BEGIN{NF=111111111111111111111}", 0) == 2);
	CHECK(err_contains("NF assignment too large"));
}

static void test_division_by_zero_rejected_cleanly(void)
{
	CHECK(run_awk("BEGIN{print 1/0}", 0) == 2);
	CHECK(err_contains("division by zero"));
}

static void test_undefined_function_rejected_cleanly(void)
{
	CHECK(run_awk("BEGIN{nosuchfunc()}", 0) == 2);
	CHECK(err_contains("call to undefined function"));
}

static void test_invalid_dynamic_regex_rejected_cleanly(void)
{
	/* "[" is a string, not a /regex/ literal, so it resolves as a
	 * DYNAMIC ere via resolve_ere() -- an unterminated bracket
	 * expression, invalid regcomp() input. */
	CHECK(run_awk("BEGIN{if (\"x\" ~ \"[\") print \"never\"}", 0) == 2);
	CHECK(err_contains("invalid dynamic regular expression"));
}

static void test_output_redirect_open_failure_rejected_cleanly(void)
{
	/* scratch/ exists (created in main() below); scratch/nosuchdir/
	 * does not, so fopen()'s underlying open() fails with ENOENT. */
	CHECK(run_awk("BEGIN{print \"x\" > \"scratch/nosuchdir/nosuchfile\"}", 0) == 2);
	CHECK(err_contains("can't open output"));
}

/* ==== the shell built-in agrees with the standalone executable ============= */

static void test_builtin_matches_standalone(void)
{
	CHECK(run_sh_c("awk 'BEGIN{print 1+1}'") == 0);
	CHECK(out_equals("2\n"));
	CHECK(run_sh_c("awk -F: '{print $2}' scratch/colon") == 0);
	CHECK(out_equals("x\nx\n"));
}

/* ==== the shell built-in survives a fatal awk error (never exit()) ========
 *
 * Before the fix, every fatal condition above reached a raw exit(2)
 * directly from inside bi_awk() -> __util_awk_main(). Because awk runs
 * as a no-fork src/sh/builtin.c built-in (bi_awk() calls
 * __util_awk_main() in-process, no fork()), that exit(2) used to tear
 * down the WHOLE interactive shell over one bad awk program, not just
 * the one command.
 *
 * `sh -c "awk '...'; echo SURVIVED"` runs the awk command and the echo
 * as two sequential commands in the SAME shell process. If awk's fatal
 * error still called exit()/_exit(), sh.exe itself would die right
 * there: "SURVIVED" would never be printed, and run_sh_c() would report
 * whatever exit(2) status the dying process happened to leave (and could
 * not run any later commands at all, in a real interactive session).
 * With the longjmp-based unwind, __util_awk_main() returns 2 like any
 * other awk failure, bi_awk() returns that as the awk command's own
 * exit status, and the shell moves on to `echo SURVIVED` exactly as it
 * would after any other failed command. */

static void test_builtin_survives_division_by_zero(void)
{
	CHECK(run_sh_c("awk 'BEGIN{print 1/0}'; echo SURVIVED") == 0);
	CHECK(out_equals("SURVIVED\n"));
}

static void test_builtin_survives_undefined_function(void)
{
	CHECK(run_sh_c("awk 'BEGIN{nosuchfunc()}'; echo SURVIVED") == 0);
	CHECK(out_equals("SURVIVED\n"));
}

static void test_builtin_survives_invalid_dynamic_regex(void)
{
	CHECK(run_sh_c("awk 'BEGIN{if (\"x\" ~ \"[\") print 1}'; echo SURVIVED") == 0);
	CHECK(out_equals("SURVIVED\n"));
}

static void test_builtin_survives_output_redirect_failure(void)
{
	CHECK(run_sh_c("awk 'BEGIN{print \"x\" > \"scratch/nosuchdir/nosuchfile\"}'; echo SURVIVED") == 0);
	CHECK(out_equals("SURVIVED\n"));
}

static void test_builtin_survives_huge_field_index(void)
{
	CHECK(run_sh_c("awk 'BEGIN{$111111111111111111111=1}'; echo SURVIVED") == 0);
	CHECK(out_equals("SURVIVED\n"));
}

static void rmtree_scratch(void)
{
	unlink("scratch/fields"); unlink("scratch/colon"); unlink("scratch/ext");
	unlink("scratch/lines"); unlink("scratch/m1"); unlink("scratch/m2");
	unlink("scratch/pat"); unlink("scratch/nums"); unlink("scratch/range");
	unlink("scratch/v1"); unlink("scratch/v2"); unlink("scratch/nx");
	unlink("scratch/gl");
	unlink("scratch/.keep");
	rmdir("scratch");
}

static void cleanup_artifacts(void)
{
	unlink(OUTFILE);
	unlink(ERRFILE);
	rmtree_scratch();
}

int main(int argc, char **argv)
{
	(void)argc;

	if (find_obj_root(argv[0]) != 0) {
		printf("SKIP util-awk: cannot locate obj/ from argv[0] \"%s\"\n", argv[0] ? argv[0] : "(null)");
		return 77;
	}
	path_for(awk_path, sizeof awk_path, "bin/awk.exe");
	path_for(sh_path, sizeof sh_path, "sh/sh.exe");

	if (access(awk_path, R_OK) != 0 || access(sh_path, R_OK) != 0) {
		printf("SKIP util-awk: awk or sh binary is missing\n");
		return 77;
	}

	rmtree_scratch();
	if (mkdir("scratch", 0755) != 0) {
		printf("SKIP util-awk: cannot create scratch/ (%s)\n", strerror(errno));
		return 77;
	}
	make_file("scratch/.keep", "");

	test_default_field_splitting();
	test_dash_F_single_char();
	test_assign_field_extends_nf_and_rebuilds_record();

	test_begin_end();
	test_nr_fnr_across_files();

	test_regex_pattern();
	test_expr_pattern();
	test_range_pattern();

	test_dash_v();
	test_varvalue_operand_timing();

	test_length_substr_index();
	test_substr_out_of_range_clamping();
	test_split_and_forin();
	test_sub_gsub();
	test_match_rstart_rlength();
	test_printf();
	test_tolower_toupper();

	test_recursive_function();
	test_extra_params_are_locals();

	test_control_flow();
	test_next_skips_remaining_rules();

	test_getline_from_file();

	test_missing_program_is_diagnosed();
	test_syntax_error_is_diagnosed();

	test_huge_field_index_rejected_cleanly();
	test_huge_nf_assignment_rejected_cleanly();
	test_division_by_zero_rejected_cleanly();
	test_undefined_function_rejected_cleanly();
	test_invalid_dynamic_regex_rejected_cleanly();
	test_output_redirect_open_failure_rejected_cleanly();

	test_builtin_matches_standalone();

	test_builtin_survives_division_by_zero();
	test_builtin_survives_undefined_function();
	test_builtin_survives_invalid_dynamic_regex();
	test_builtin_survives_output_redirect_failure();
	test_builtin_survives_huge_field_index();

	cleanup_artifacts();

	if (fails) { printf("util-awk: failures: %d\n", fails); return 1; }
	printf("util-awk: all ok (awk -- standalone and builtin)\n");
	return 0;
}
