/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Black-box tests for ntlibc's Tier 7 (Software Development option
 * tier) `strip` (XCU strip(1p)). Same spawn/capture technique as
 * test/util-archive.c: the standalone obj/bin/strip.exe is spawned as
 * a real process (via __spawn()+waitpid()), and the shell built-in is
 * exercised too (via obj/sh/sh.exe -c), confirming both callers of
 * __util_strip_main() (src/internal/util.h) agree.
 *
 * Unlike every other utility test in this tree, this one's core
 * assertion is BEHAVIORAL, not textual: `strip` rewrites an executable
 * file in place, so "it produced smaller output" or "the option parsed"
 * proves nothing about whether the result is still a working program.
 * test_strip_still_runs() below takes a real, already-built
 * obj/bin/cat.exe (a small, genuine fixture this build produces for
 * itself -- prerequisite-wired in the Makefile's own obj/test/
 * util-strip.exe rule), copies it, strips the copy, and SPAWNS the
 * stripped copy, checking both its exit status and its real stdout
 * output against known-good input -- proof the stripped binary still
 * executes correctly, not just that stripping "ran without crashing".
 * It also confirms a real size reduction happened and that the symbol
 * table is genuinely gone from the stripped copy (by directly reading
 * the ELF section header table back out and checking no section is
 * named ".symtab" any more), on PLATFORM=linux, where obj/bin/ executables are
 * real ELF PIE binaries (see src/dlfcn/linux/plat_dlfcn.c's own
 * IRELATIVE-relocation banner for why this exact PIE shape matters --
 * this test is real, running proof `strip` does not break it). On
 * PLATFORM=nt, obj/bin/ executables are PE, and this build cannot run a
 * stripped PE binary in this environment (Wine is not available/usable
 * here) -- so every behavioral assertion below is skipped (not faked,
 * see is_elf()'s own guard at the top of each test) when the fixture
 * this build actually produced is not ELF. src/util/strip.c's own PE
 * path is exercised only by compilation on PLATFORM=nt, not by this
 * test -- see that file's own header banner for why its PE support is
 * deliberately narrower, unverified-by-execution bonus scope.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
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

/* Truncates `s` at its last path separator (in place). Returns -1 if
 * `s` contains no separator at all, leaving `s` untouched. */
static int strip_last_component(char *s)
{
	size_t i;

	for (i = strlen(s); i > 0 && s[i - 1] != '/' && s[i - 1] != '\\'; i--)
		;
	if (i == 0) return -1;
	s[i - 1] = 0;
	return 0;
}

static int find_obj_root(const char *argv0)
{
	if (!argv0 || !*argv0) return -1;
	if (strlen(argv0) >= sizeof obj_root) return -1;
	strcpy(obj_root, argv0);

	if (strip_last_component(obj_root) != 0) return -1; /* strip "/util-strip.exe" */
	if (strip_last_component(obj_root) != 0) return -1; /* strip "/test" */

	return 0;
}

static void path_for(char *out, size_t outlen, const char *rel)
{
	char sep = strchr(obj_root, '\\') ? '\\' : '/';
	char relcopy[256];
	size_t i;

	strncpy(relcopy, rel, sizeof relcopy - 1);
	relcopy[sizeof relcopy - 1] = 0;
	if (sep == '\\')
		for (i = 0; relcopy[i]; i++)
			if (relcopy[i] == '/') relcopy[i] = '\\';
	snprintf(out, outlen, "%s%c%s", obj_root, sep, relcopy);
}

#define OUTFILE "util-strip-out.txt"
#define ERRFILE "util-strip-err.txt"

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

static int out_contains(const char *needle)
{
	char buf[8192];
	slurp_into(OUTFILE, buf, sizeof buf);
	return strstr(buf, needle) != 0;
}

static long file_size(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0) return -1;
	return (long)st.st_size;
}

static int copy_file(const char *src, const char *dst)
{
	FILE *in = fopen(src, "rb");
	FILE *out;
	char buf[65536];
	size_t n;
	struct stat st;

	if (!in) return -1;
	out = fopen(dst, "wb");
	if (!out) { fclose(in); return -1; }
	while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return -1; }
	}
	fclose(in);
	fclose(out);
	if (stat(src, &st) == 0) chmod(dst, st.st_mode & 07777);
	return 0;
}

/* Minimal local ELF64 read-back, deliberately independent of
 * src/util/strip.c's own copy -- this test wants to verify strip's
 * *output*, not re-trust strip's own idea of what it wrote. Returns 1
 * if a section named exactly `name` exists, 0 if the file is not a
 * readable ELF64 or the section is genuinely absent. */
static int elf_has_section(const char *path, const char *name)
{
	FILE *f = fopen(path, "rb");
	unsigned char ehdr[64];
	uint64_t shoff;
	uint16_t shnum, shentsize, shstrndx;
	unsigned char *shdrs = NULL;
	unsigned char *shstr = NULL;
	int found = 0;
	int i;

	if (!f) return 0;
	if (fread(ehdr, 1, sizeof ehdr, f) != sizeof ehdr) { fclose(f); return 0; }
	if (ehdr[0] != 0x7f || ehdr[1] != 'E' || ehdr[2] != 'L' || ehdr[3] != 'F') { fclose(f); return 0; }
	memcpy(&shoff, ehdr + 40, 8);
	memcpy(&shentsize, ehdr + 58, 2);
	memcpy(&shnum, ehdr + 60, 2);
	memcpy(&shstrndx, ehdr + 62, 2);
	if (shentsize != 64 || shnum == 0 || shstrndx >= shnum) { fclose(f); return 0; }

	shdrs = malloc((size_t)shnum * 64);
	if (!shdrs) { fclose(f); return 0; }
	if (fseek(f, (long)shoff, SEEK_SET) != 0 ||
	    fread(shdrs, 1, (size_t)shnum * 64, f) != (size_t)shnum * 64) {
		free(shdrs); fclose(f); return 0;
	}

	{
		uint64_t sh_off, sh_size;
		unsigned char *ss = shdrs + (size_t)shstrndx * 64;
		memcpy(&sh_off, ss + 24, 8);
		memcpy(&sh_size, ss + 32, 8);
		shstr = malloc((size_t)sh_size);
		if (!shstr) { free(shdrs); fclose(f); return 0; }
		if (fseek(f, (long)sh_off, SEEK_SET) != 0 ||
		    fread(shstr, 1, (size_t)sh_size, f) != sh_size) {
			free(shstr); free(shdrs); fclose(f); return 0;
		}
		for (i = 0; i < shnum; i++) {
			uint32_t sh_name;
			unsigned char *sh = shdrs + (size_t)i * 64;
			memcpy(&sh_name, sh, 4);
			if (sh_name < sh_size && strcmp((char *)shstr + sh_name, name) == 0) {
				found = 1;
				break;
			}
		}
		free(shstr);
	}
	free(shdrs);
	fclose(f);
	return found;
}

static int is_elf(const char *path)
{
	FILE *f = fopen(path, "rb");
	unsigned char m[4];
	int ok;
	if (!f) return 0;
	ok = fread(m, 1, 4, f) == 4 && m[0] == 0x7f && m[1] == 'E' && m[2] == 'L' && m[3] == 'F';
	fclose(f);
	return ok;
}

static char strip_path[1024], cat_path[1024], sh_path[1024];

static int run_sh_c(const char *cmd)
{
	char *argv[] = { (char *)"sh", (char *)"-c", (char *)cmd, 0 };
	return run(sh_path, argv);
}

/* Spawns the stripped binary at `path` on the fixed known-good input
 * fixture (written by the caller beforehand) and checks it still runs
 * correctly -- the shared shape of the actual acceptance check across
 * every stripped-copy variant below. */
static void check_stripped_binary_still_runs(const char *path)
{
	char *argv[] = { (char *)path, (char *)"scratch/strip_input.txt", 0 };
	CHECK(run(path, argv) == 0);
	CHECK(out_contains("stripped binary still works"));
}

/* ==== the real acceptance test: strip a working binary, run it ========== */

static void test_strip_still_runs(void)
{
	long before, after;

	if (!is_elf(cat_path)) {
		printf("SKIP test_strip_still_runs: obj/bin/cat.exe is not ELF on this platform "
		       "(PE cannot be executed in this environment -- see this file's own header)\n");
		return;
	}

	CHECK(copy_file(cat_path, "scratch/cat_copy") == 0);
	before = file_size("scratch/cat_copy");
	CHECK(before > 0);
	CHECK(elf_has_section("scratch/cat_copy", ".symtab"));

	{
		char *strip_argv[] = { (char *)"strip", (char *)"scratch/cat_copy", 0 };
		CHECK(run(strip_path, strip_argv) == 0);
	}

	after = file_size("scratch/cat_copy");
	CHECK(after > 0);
	CHECK(after < before); /* a real size reduction happened */
	CHECK(!elf_has_section("scratch/cat_copy", ".symtab")); /* symbol table genuinely gone */
	CHECK(!elf_has_section("scratch/cat_copy", ".strtab"));

	/* The real acceptance test: spawn the STRIPPED binary and confirm
	 * it still runs correctly -- feed it known input via a file operand
	 * (cat with a file operand, not stdin, so this test does not also
	 * need to juggle a piped fd across __spawn()) and check both its
	 * exit status and its actual output. */
	{
		FILE *in = fopen("scratch/strip_input.txt", "wb");
		CHECK(in != 0);
		if (in) { fputs("stripped binary still works\n", in); fclose(in); }
	}
	check_stripped_binary_still_runs("scratch/cat_copy");
}

static void test_strip_builtin_agreement(void)
{
	if (!is_elf(cat_path)) {
		printf("SKIP test_strip_builtin_agreement: not ELF on this platform\n");
		return;
	}
	CHECK(copy_file(cat_path, "scratch/cat_copy2") == 0);
	CHECK(run_sh_c("strip scratch/cat_copy2") == 0);
	CHECK(!elf_has_section("scratch/cat_copy2", ".symtab"));

	check_stripped_binary_still_runs("scratch/cat_copy2");
}

/* ==== -o output-file form ================================================= */

static void test_strip_dash_o(void)
{
	long orig;
	if (!is_elf(cat_path)) { printf("SKIP test_strip_dash_o: not ELF on this platform\n"); return; }

	CHECK(copy_file(cat_path, "scratch/cat_orig") == 0);
	orig = file_size("scratch/cat_orig");
	unlink("scratch/cat_stripped");

	{
		char *argv[] = { (char *)"strip", (char *)"-o", (char *)"scratch/cat_stripped",
		                  (char *)"scratch/cat_orig", 0 };
		CHECK(run(strip_path, argv) == 0);
	}

	/* -o must leave the original untouched and write a separate,
	 * smaller output file. */
	CHECK(file_size("scratch/cat_orig") == orig);
	CHECK(elf_has_section("scratch/cat_orig", ".symtab"));
	CHECK(file_size("scratch/cat_stripped") > 0);
	CHECK(file_size("scratch/cat_stripped") < orig);
	CHECK(!elf_has_section("scratch/cat_stripped", ".symtab"));

	check_stripped_binary_still_runs("scratch/cat_stripped");
}

/* ==== usage errors ========================================================= */

static void test_strip_usage_errors(void)
{
	char *no_operand[] = { (char *)"strip", 0 };
	char *extra_operand[] = { (char *)"strip", (char *)"a", (char *)"b", 0 };
	char *bad_opt[] = { (char *)"strip", (char *)"-z", (char *)"a", 0 };
	char *missing_o_arg[] = { (char *)"strip", (char *)"-o", 0 };

	CHECK(run(strip_path, no_operand) == 2);
	CHECK(run(strip_path, extra_operand) == 2);
	CHECK(run(strip_path, bad_opt) == 2);
	CHECK(run(strip_path, missing_o_arg) == 2);
}

static void test_strip_missing_file(void)
{
	char *argv[] = { (char *)"strip", (char *)"scratch/does_not_exist_xyz", 0 };
	CHECK(run(strip_path, argv) == 1);
}

static void test_strip_non_object(void)
{
	char *argv[] = { (char *)"strip", (char *)"scratch/not_an_object.txt", 0 };
	FILE *f = fopen("scratch/not_an_object.txt", "wb");
	if (f) { fputs("plain text, not ELF or PE\n", f); fclose(f); }
	CHECK(run(strip_path, argv) == 1);
}

/* ==== scratch directory setup/teardown ==================================== */

static void rmtree_scratch(void)
{
	unlink("scratch/cat_copy"); unlink("scratch/cat_copy2");
	unlink("scratch/cat_orig"); unlink("scratch/cat_stripped");
	unlink("scratch/strip_input.txt"); unlink("scratch/not_an_object.txt");
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
		printf("SKIP util-strip: cannot locate obj/ from argv[0] \"%s\"\n",
			argv[0] ? argv[0] : "(null)");
		return 77;
	}
	path_for(strip_path, sizeof strip_path, "bin/strip.exe");
	path_for(cat_path, sizeof cat_path, "bin/cat.exe");
	path_for(sh_path, sizeof sh_path, "sh/sh.exe");

	if (access(strip_path, R_OK) != 0 || access(cat_path, R_OK) != 0 || access(sh_path, R_OK) != 0) {
		printf("SKIP util-strip: one or more of strip/cat/sh binaries is missing\n");
		return 77;
	}

	rmtree_scratch();
	if (mkdir("scratch", 0755) != 0) {
		printf("SKIP util-strip: cannot create scratch/ (%s)\n", strerror(errno));
		return 77;
	}
	{
		FILE *k = fopen("scratch/.keep", "wb");
		if (k) fclose(k);
	}

	test_strip_still_runs();
	test_strip_builtin_agreement();
	test_strip_dash_o();
	test_strip_usage_errors();
	test_strip_missing_file();
	test_strip_non_object();

	cleanup_artifacts();

	if (fails) { printf("util-strip: failures: %d\n", fails); return 1; }
	printf("util-strip: all ok (strip -- standalone, builtin, -o form; ELF still runs after stripping)\n");
	return 0;
}
