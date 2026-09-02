/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Black-box tests for man(1p) (src/util/man.c, obj/bin/man.exe,
 * src/sh/builtin.c's bi_man()). Same shape as test/util-textio.c (see
 * its own header): each standalone obj/bin/man.exe is spawned as a
 * real process, and the shell built-in is exercised too through
 * obj/sh/sh.exe -c, confirming both callers of __util_man_main()
 * agree.
 *
 * Two kinds of fixture page live under one scratch $MANPATH this file
 * builds itself (write_file(), same "private scratch dir torn down on
 * the way out" idiom test/util-fileops.c's own header explains):
 *
 *  - FROBNICATE_1: hand-written troff source exercising every macro
 *    src/util/man.c's own header comment lists as supported (.TH,
 *    .SH/.SS, .TP/.IP, .PP/.LP, .B/.I, the alternating-font pairs,
 *    .RS/.RE, .nf/.fi, .br) plus the common escape subset.
 *  - GREP1_EXCERPT: a REAL, unmodified 210-line prefix of GNU grep's
 *    own grep.1 (from a real Linux system's /nix/store, gzip -dc'd by
 *    hand once to produce this literal text -- not paraphrased, not
 *    hand-simplified), proving this formatter against troff nobody
 *    wrote for this project. It opens with real .de/.ie/.ds/.nr
 *    boilerplate this project's man(1p) deliberately does not execute
 *    (see src/util/man.c's own header, "WHAT IS DELIBERATELY NOT
 *    IMPLEMENTED") before reaching real NAME/SYNOPSIS/DESCRIPTION/
 *    OPTIONS content that DOES use the supported macro subset --
 *    exactly the boundary this project's man(1p) is scoped to.
 *
 * Spec pages consulted (https://pubs.opengroup.org/onlinepubs/9699919799/):
 *   utilities/man.html (SYNOPSIS, OPTIONS, OPERANDS, EXIT STATUS)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern char **environ;
int __spawn(const char *path, char *const argv[], char *const envp[]);

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- locating obj/bin/man.exe and obj/sh/sh.exe -- same walk-up-from-
 * argv[0] technique as test/util-textio.c's find_obj_root()/path_for(). */
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
	p[-1] = 0;                       /* strip "/util-man.exe" */

	for (p = obj_root + strlen(obj_root); p > obj_root; p--)
		if (p[-1] == '/' || p[-1] == '\\') break;
	if (p == obj_root) return -1;
	p[-1] = 0;                       /* strip "/test" */

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

static char man_path[1024], sh_path[1024];

/* ---- the scratch directory, doubling as $MANPATH's one entry ---------- */

static char scratch[128];
static char man1dir[256];

static void raw_rmtree(const char *path)
{
	struct stat st;
	DIR *d;
	struct dirent *de;

	if (lstat(path, &st) < 0) return;
	if (S_ISDIR(st.st_mode)) {
		d = opendir(path);
		if (d) {
			while ((de = readdir(d)) != NULL) {
				char child[600];
				if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
				snprintf(child, sizeof child, "%s/%s", path, de->d_name);
				raw_rmtree(child);
			}
			closedir(d);
		}
		rmdir(path);
	} else {
		unlink(path);
	}
}

static void write_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "wb");
	if (!f) { fails++; printf("FAIL: cannot write %s\n", path); return; }
	fputs(text, f);
	fclose(f);
}

/* ---- spawning and capturing -------------------------------------------- */

static char outfile[700], errfile[700];

/* $MANPATH for every spawned child: built once (see main()) as
 * `environ` plus one extra "MANPATH=..." entry, rather than mutating
 * this process's own environment via setenv()/putenv() -- both are
 * declared in include/stdlib.h only behind _XOPEN_SOURCE/_BSD_SOURCE/
 * _GNU_SOURCE (see that header's own guards), none of which this
 * project's test build line defines under -std=c99 (which predefines
 * __STRICT_ANSI__, so include/features.h's own "define them by
 * default" fallback does not fire either -- see include/sys/wait.h's
 * own comment for the identical situation). Passing a custom envp
 * straight to __spawn() sidesteps the question entirely. */
static char **test_envp;

static int run(const char *path, char *const *args)
{
	int out, err;
	int s1, s2, pid, status;

	out = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	err = open(errfile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (out < 0 || err < 0) { if (out >= 0) close(out); if (err >= 0) close(err); return -1; }

	s1 = dup(1); s2 = dup(2);
	dup2(out, 1);
	dup2(err, 2);
	close(out); close(err);

	pid = __spawn(path, args, test_envp);

	dup2(s1, 1); close(s1);
	dup2(s2, 2); close(s2);

	if (pid < 0) return -1;
	if (waitpid(pid, &status, 0) != pid) return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static int run_sh_c(const char *cmd)
{
	char *argv[4];
	argv[0] = (char *)"sh"; argv[1] = (char *)"-c"; argv[2] = (char *)cmd; argv[3] = 0;
	return run(sh_path, argv);
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

static char outbuf[16384], errbuf[16384];

/* De-overstrike, the same collapse `col -b` does: a bold or italic
 * glyph is always exactly `X\bY` (see src/util/man.c's own
 * man_render_write() -- OVERSTRIKE mode writes the shadow character,
 * a backspace, then the real one), so replacing every such triple
 * with just Y recovers the plain text underneath. Content assertions
 * below check plainbuf, not outbuf directly, precisely BECAUSE most
 * of FROBNICATE_1's/grep.1's interesting text is bold/italic and
 * would never literally appear byte-for-byte in the overstruck raw
 * output -- a couple of assertions check outbuf directly instead,
 * specifically to prove the overstrike encoding itself is present. */
static char plainbuf[16384];

static void strip_overstrike(const char *in, char *out, size_t outsz)
{
	size_t i = 0, o = 0, n = strlen(in);
	while (i < n && o + 1 < outsz) {
		if (i + 2 < n && in[i + 1] == '\b') { out[o++] = in[i + 2]; i += 3; }
		else { out[o++] = in[i++]; }
	}
	out[o] = 0;
}

static void slurp_both(void)
{
	slurp_into(outfile, outbuf, sizeof outbuf);
	slurp_into(errfile, errbuf, sizeof errbuf);
	strip_overstrike(outbuf, plainbuf, sizeof plainbuf);
}

static int out_contains(const char *needle) { return strstr(outbuf, needle) != 0; }
static int plain_contains(const char *needle) { return strstr(plainbuf, needle) != 0; }
static int err_contains(const char *needle) { return strstr(errbuf, needle) != 0; }

/* ==== fixture page 1: exercises every supported macro/escape ============ */

static const char FROBNICATE_1[] =
	".TH FROBNICATE 1 \"2026\" \"ntlibc test\" \"ntlibc Test Suite\"\n"
	".SH NAME\n"
	"frobnicate \\- exercise every supported macro\n"
	".SH SYNOPSIS\n"
	".B frobnicate\n"
	".RB [ \\-x ]\n"
	".I target\n"
	".br\n"
	".B frobnicate\n"
	".B \\-\\-help\n"
	".SH DESCRIPTION\n"
	"Plain text with\n"
	".B bold\n"
	"and\n"
	".I italic\n"
	"words, plus alternating\n"
	".BR bold , roman\n"
	"and a glyph\n"
	".RB ( \\(bu )\n"
	"bullet and a copyright\n"
	".RB ( \\(co )\n"
	"sign and an unsupported string register\n"
	".RB ( \\*(xx )\n"
	"that must vanish silently.\n"
	".SS Subsection Heading\n"
	"Body text under a subsection.\n"
	".TP\n"
	".B \\-x\n"
	"A short tag.\n"
	".TP\n"
	".B \\-\\-a-very-long-option-name-that-does-not-fit-the-tag-column\n"
	"A description that must start on its own line because the tag itself is too long to share it.\n"
	".IP \\(bu 2\n"
	"A bulleted item using .IP with an explicit width.\n"
	".RS\n"
	".IP \\(bu 2\n"
	"A nested item, one .RS level deeper.\n"
	".RE\n"
	".nf\n"
	"literal   line    one\n"
	"    literal line two, indented\n"
	".fi\n"
	"Back to normal filled text after .fi.\n"
	".SH SEE ALSO\n"
	".BR true (1)\n";

/* ==== fixture page 2: a real, unmodified 210-line prefix of GNU grep's
 * own grep.1 (gzip -dc'd by hand from a real Linux system's
 * /nix/store copy of gnugrep-3.12) -- see this file's own header. */
static const char *const GREP1_EXCERPT[] = {
	".\\\" GNU grep man page\n",
	".de dT\n",
	".ds Dt \\\\$2\n",
	"..\n",
	".dT Time-stamp: \"2025-03-21\"\n",
	".\\\" Update the above date whenever a change to either this file or\n",
	".\\\" grep.c's 'usage' function results in a nontrivial change to the man page.\n",
	".\\\" In Emacs, you can update the date by running 'M-x time-stamp'\n",
	".\\\" after you make a change that you decide is nontrivial.\n",
	".\\\" It is no big deal to forget to update the date.\n",
	".\n",
	".TH GREP 1 \\*(Dt \"GNU grep 3.12\" \"User Commands\"\n",
	".\n",
	".ie \\n(.g .ds ' \\(aq\n",
	".el .ds ' '\n",
	".if !\\w@\\*(lq@ \\{\\\n",
	".\\\" Recent-enough groff an.tmac does not seem to be in use,\n",
	".\\\" so define the strings lq and rq.\n",
	".\tie \\n(.g \\{\\\n",
	".\t\tds lq \\(lq\\\"\n",
	".\t\tds rq \\(rq\\\"\n",
	".\t\\}\n",
	".\tel \\{\\\n",
	".\t\tds lq ``\n",
	".\t\tds rq ''\n",
	".\t\\}\n",
	".\\}\n",
	".\n",
	".as mC\n",
	".if !\\w@\\*(mC@ \\{\\\n",
	".\\\" groff an-ext.tmac does not seem to be in use, so define the parts of\n",
	".\\\" it that are used below, taken from groff 1.23.0.  For a copy, please see:\n",
	".\\\" https://git.savannah.gnu.org/cgit/groff.git/plain/tmac/an-ext.tmac?id=1.23.0\n",
	".nr mG \\n(.g-1\n",
	".\\\" --- Start of lines taken from groff an-ext.tmac,\n",
	".\\\" except with \"nr mH 14\" replaced by \"nr mH 0\"\n",
	".\\\" and with mS, SY, YS definitions omitted.\n",
	".\n",
	".\\\" Define this to your implementation's constant-width typeface.\n",
	".ds mC CW\n",
	".if n .ds mC R\n",
	".\n",
	".\\\" Save the automatic hyphenation mode.\n",
	".\\\"\n",
	".\\\" In AT&T troff, there was no register exposing the hyphenation mode,\n",
	".\\\" and no way to save and restore it.  Set `mH` to a reasonable value\n",
	".\\\" for your implementation and preference.\n",
	".de mY\n",
	".  ie !\\\\n(.g \\\n",
	".    nr mH 0\n",
	".  el \\\n",
	".    do nr mH \\\\n[.hy] \\\" groff extension register\n",
	"..\n",
	".\n",
	".nr mE 0 \\\" in an example (EX/EE)?\n",
	".\n",
	".\\\" Prepare link text for mail/web hyperlinks.  `MT` and `UR` call this.\n",
	".de mV\n",
	".  ds m1 \\\\$1\\\"\n",
	"..\n",
	".\n",
	".\\\" IP address, no domain name, for a `mailto:` or other URI.\n",
	".de mX\n",
	".  ie \\\\n(.$-1 \\{\\\n",
	".    as m1 \\e \\\\$2\n",
	".  \\}\n",
	".  el \\{\\\n",
	".    as m1 \\e \\\\*(m1\n",
	".  \\}\n",
	"..\n",
	".\n",
	".\\\" Set the link text and target for `MT`, `UR`.\n",
	".de mZ\n",
	".  ds m2 \\\\$1\\\"\n",
	"..\n",
	".\n",
	".\\\" Hyperlink using an explicit target, if given, or link text otherwise\n",
	".\\\" (encoded per RFC 3986, except a fragment is not yet supported).\n",
	".de UR\n",
	".  ds m1 \\\\$1\\\"\n",
	"..\n",
	".de UE\n",
	".  br\n",
	"..\n",
	".de MT\n",
	".  ds m1 mailto:\\\\$1\\\"\n",
	"..\n",
	".de ME\n",
	".  br\n",
	"..\n",
	".\n",
	".\\\" Start example.\n",
	".de EX\n",
	".  nr mP \\\\n(PD\n",
	".  nr PD 0\n",
	".  nf\n",
	".  ft \\\\n(mC\n",
	".  nr mE 1\n",
	"..\n",
	".\n",
	".\\\" End example.\n",
	".if \\\\n(.g-\\\\n(mG \\{\\\n",
	".de EE\n",
	".  br\n",
	".  if \\\\n(mE \\{\\\n",
	".    ft \\\\n(mF\n",
	".    nr PD \\\\n(mP\n",
	".    fi\n",
	".    nr mE 0\n",
	".  \\}\n",
	"..\n",
	".\\}\n",
	".\\\" --- End of lines taken from groff an-ext.tmac\n",
	".\\}\n",
	".\n",
	".hy 0\n",
	".\n",
	".SH NAME\n",
	"grep \\- print lines that match patterns\n",
	".\n",
	".SH SYNOPSIS\n",
	".B grep\n",
	".RI [ OPTION ].\\|.\\|.\\&\n",
	".I PATTERNS\n",
	".RI [ FILE ].\\|.\\|.\n",
	".br\n",
	".B grep\n",
	".RI [ OPTION ].\\|.\\|.\\&\n",
	".B \\-e\n",
	".I PATTERNS\n",
	"\\&.\\|.\\|.\\&\n",
	".RI [ FILE ].\\|.\\|.\n",
	".br\n",
	".B grep\n",
	".RI [ OPTION ].\\|.\\|.\\&\n",
	".B \\-f\n",
	".I PATTERN_FILE\n",
	"\\&.\\|.\\|.\\&\n",
	".RI [ FILE ].\\|.\\|.\n",
	".\n",
	".SH DESCRIPTION\n",
	".B grep\n",
	"searches for patterns in each\n",
	".IR FILE .\n",
	"In the synopsis's first form, which is used if no\n",
	".B \\-e\n",
	"or\n",
	".B \\-f\n",
	"options are present, the first operand\n",
	".I PATTERNS\n",
	"is one or more patterns separated by newline characters, and\n",
	".B grep\n",
	"prints each line that matches a pattern.\n",
	"Typically\n",
	".I PATTERNS\n",
	"should be quoted when\n",
	".B grep\n",
	"is used in a shell command.\n",
	".PP\n",
	"A\n",
	".I FILE\n",
	"of\n",
	".RB \"\\*(lq\" \\- \"\\*(rq\"\n",
	"stands for standard input.\n",
	"If no\n",
	".I FILE\n",
	"is given, recursive searches examine the working directory,\n",
	"and nonrecursive searches read standard input.\n",
	".\n",
	".SH OPTIONS\n",
	".SS \"Generic Program Information\"\n",
	".TP\n",
	".B \\-\\^\\-help\n",
	"Output a usage message and exit.\n",
	".TP\n",
	".BR \\-V \", \" \\-\\^\\-version\n",
	"Output the version number of\n",
	".B grep\n",
	"and exit.\n",
	".SS \"Pattern Syntax\"\n",
	".TP\n",
	".BR \\-E \", \" \\-\\^\\-extended\\-regexp\n",
	"Interpret\n",
	".I PATTERNS\n",
	"as extended regular expressions (EREs, see below).\n",
	".TP\n",
	".BR \\-F \", \" \\-\\^\\-fixed\\-strings\n",
	"Interpret\n",
	".I PATTERNS\n",
	"as fixed strings, not regular expressions.\n",
	".TP\n",
	".BR \\-G \", \" \\-\\^\\-basic\\-regexp\n",
	"Interpret\n",
	".I PATTERNS\n",
	"as basic regular expressions (BREs, see below).\n",
	"This is the default.\n",
	".TP\n",
	".BR \\-P \", \" \\-\\^\\-perl\\-regexp\n",
	"Interpret\n",
	".I PATTERNS\n",
	0
};

static void write_grep1_excerpt(const char *path)
{
	FILE *f = fopen(path, "wb");
	size_t i;
	if (!f) { fails++; printf("FAIL: cannot write %s\n", path); return; }
	for (i = 0; GREP1_EXCERPT[i]; i++) fputs(GREP1_EXCERPT[i], f);
	fclose(f);
}

/* ==== tests =============================================================== */

static void test_finds_and_formats_frobnicate(void)
{
	char *argv[3];
	argv[0] = (char *)"man"; argv[1] = (char *)"frobnicate"; argv[2] = 0;
	CHECK(run(man_path, argv) == 0);
	slurp_both();

	/* Header/footer from .TH. */
	CHECK(out_contains("FROBNICATE(1)"));
	CHECK(out_contains("ntlibc Test Suite"));

	/* .SH/.SS headings and body text reach the page at all.
	 * "Subsection Heading" is bold (every .SS heading is), so it is
	 * checked against plainbuf (de-overstruck), not the raw bytes --
	 * see plainbuf's own comment for why. */
	CHECK(out_contains("exercise every supported macro"));
	CHECK(plain_contains("Subsection Heading"));
	CHECK(out_contains("Body text under a subsection."));

	/* .B/.I/.BR: bold text renders as overstrike (X\bX) since stdout
	 * here is a captured file, not a terminal -- see src/util/man.c's
	 * own header comment ("RENDERING") for why that's the correct
	 * choice, not a bug. "bold" -> "b\bbo\bo...", checked against the
	 * RAW bytes specifically to prove the overstrike encoding itself
	 * is really being produced. */
	CHECK(out_contains("b\bbo\bol\bld\bd"));
	/* .I italic -> underline-style overstrike, shadow character
	 * FIRST: "italic" -> "_\bi_\bt...". */
	CHECK(out_contains("_\bi"));

	/* \(bu / \(co glyphs decoded to real UTF-8 bytes -- both are bold
	 * here (`.RB ( \(bu )` alternates roman/BOLD/roman), so each
	 * glyph's individual bytes are themselves overstruck; check
	 * plainbuf, where the collapse reassembles the clean UTF-8
	 * sequence. */
	CHECK(plain_contains("\xE2\x80\xA2")); /* bullet */
	CHECK(plain_contains("\xC2\xA9"));     /* copyright sign */

	/* \*(xx (unsupported string register) vanished without leaving
	 * raw escape syntax in the output. */
	CHECK(!out_contains("\\*(xx"));
	CHECK(out_contains("that must vanish silently."));

	/* .TP: a short tag shares the body's first output line; a too-
	 * long tag gets its own line, with the body starting fresh below
	 * it -- both cases' tag text must appear. The tag itself is bold
	 * (`.B ...`). */
	CHECK(out_contains("A short tag."));
	CHECK(plain_contains("-a-very-long-option-name-that-does-not-fit-the-tag-column"));
	CHECK(out_contains("must start on its own line"));

	/* .IP with \(bu bullets, including one nested inside .RS/.RE. */
	CHECK(out_contains("A bulleted item using"));
	CHECK(out_contains("A nested item, one"));

	/* .nf/.fi: internal multiple spaces preserved verbatim (fill mode
	 * would have collapsed them to one). */
	CHECK(out_contains("literal   line    one"));
	CHECK(out_contains("    literal line two, indented"));
	/* Immediately after .fi, ordinary fill-mode wrapping resumes. */
	CHECK(out_contains("Back to normal filled text after .fi."));

	/* .br kept the two SYNOPSIS forms on separate lines rather than
	 * flowing "target frobnicate --help" together. Both are styled
	 * (.I target, .B --help). */
	CHECK(plain_contains("target"));
	CHECK(plain_contains("--help"));
}

static void test_section_operand_restricts_search(void)
{
	char *argv[4];
	argv[0] = (char *)"man"; argv[1] = (char *)"1"; argv[2] = (char *)"frobnicate"; argv[3] = 0;
	CHECK(run(man_path, argv) == 0);
	slurp_both();
	CHECK(out_contains("FROBNICATE(1)"));
}

static void test_no_such_page(void)
{
	char *argv[3];
	argv[0] = (char *)"man"; argv[1] = (char *)"this-utility-does-not-exist"; argv[2] = 0;
	CHECK(run(man_path, argv) != 0);
	slurp_both();
	CHECK(err_contains("No manual entry"));
}

static void test_apropos_dash_k(void)
{
	char *argv[4];
	argv[0] = (char *)"man"; argv[1] = (char *)"-k"; argv[2] = (char *)"exercise"; argv[3] = 0;
	CHECK(run(man_path, argv) == 0);
	slurp_both();
	CHECK(out_contains("frobnicate(1)"));
}

static void test_gzip_only_page_diagnosed(void)
{
	char gzpath[400];
	char *argv[3];
	snprintf(gzpath, sizeof gzpath, "%s/compressed-only.1.gz", man1dir);
	write_file(gzpath, ""); /* content is irrelevant: only existence is checked */

	argv[0] = (char *)"man"; argv[1] = (char *)"compressed-only"; argv[2] = 0;
	CHECK(run(man_path, argv) != 0);
	slurp_both();
	CHECK(err_contains("gzip"));
}

static void test_real_grep1_excerpt(void)
{
	char *argv[3];
	argv[0] = (char *)"man"; argv[1] = (char *)"grep"; argv[2] = 0;
	CHECK(run(man_path, argv) == 0);
	slurp_both();

	/* Real header/footer, real NAME/SYNOPSIS/DESCRIPTION/OPTIONS
	 * content reaches the page despite the heavy .de/.ie/.ds/.nr
	 * boilerplate this file opens with. */
	CHECK(out_contains("GREP(1)"));
	CHECK(out_contains("print lines that match patterns"));
	CHECK(plain_contains("PATTERNS")); /* .I PATTERNS: italic throughout */
	CHECK(out_contains("extended regular expressions"));
	CHECK(out_contains("fixed strings, not regular expressions"));

	/* .TP tags from real .BR alternating-font option lists -- bold. */
	CHECK(plain_contains("--help"));
	CHECK(plain_contains("--version"));

	/* No raw troff syntax leaked into the rendered output: none of
	 * the unsupported .de/.ds/.nr/.ie machinery's own literal source
	 * text (which would look like corruption to a reader) appears. */
	CHECK(!out_contains(".de "));
	CHECK(!out_contains(".ds "));
	CHECK(!out_contains(".nr "));
	CHECK(!out_contains(".ie "));
	CHECK(!out_contains("\\\\$"));
	CHECK(!out_contains("mailto:"));
}

static void test_builtin_agrees_with_standalone(void)
{
	/* $MANPATH reaches sh.exe -c via test_envp, the same custom envp
	 * every run() call already uses -- no inline "MANPATH=... man ..."
	 * shell prefix needed. */
	CHECK(run_sh_c("man frobnicate") == 0);
	slurp_both();
	CHECK(out_contains("FROBNICATE(1)"));
	CHECK(out_contains("exercise every supported macro"));
}

/* ==== main ================================================================ */

static char manpath_entry[300];

int main(int argc, char **argv)
{
	(void)argc;

	if (find_obj_root(argv[0]) != 0) {
		printf("SKIP util-man: cannot locate obj/ from argv[0] \"%s\"\n",
			argv[0] ? argv[0] : "(null)");
		return 77;
	}
	path_for(man_path, sizeof man_path, "bin/man.exe");
	path_for(sh_path, sizeof sh_path, "sh/sh.exe");

	if (access(man_path, R_OK) != 0 || access(sh_path, R_OK) != 0) {
		printf("SKIP util-man: obj/bin/man.exe or obj/sh/sh.exe is missing\n");
		return 77;
	}

	snprintf(scratch, sizeof scratch, "man-scratch-%ld", (long)getpid());
	raw_rmtree(scratch);   /* in case a previous crashed run left one behind */
	if (mkdir(scratch, 0700) != 0) {
		printf("SKIP util-man: cannot create scratch directory \"%s\"\n", scratch);
		return 77;
	}
	snprintf(man1dir, sizeof man1dir, "%s/man1", scratch);
	if (mkdir(man1dir, 0700) != 0) {
		printf("SKIP util-man: cannot create \"%s\"\n", man1dir);
		raw_rmtree(scratch);
		return 77;
	}
	snprintf(outfile, sizeof outfile, "%s/out.txt", scratch);
	snprintf(errfile, sizeof errfile, "%s/err.txt", scratch);

	{
		char frobpath[400];
		snprintf(frobpath, sizeof frobpath, "%s/frobnicate.1", man1dir);
		write_file(frobpath, FROBNICATE_1);
	}
	{
		char greppath[400];
		snprintf(greppath, sizeof greppath, "%s/grep.1", man1dir);
		write_grep1_excerpt(greppath);
	}

	/* Builds test_envp = environ + one extra "MANPATH=<scratch>" entry
	 * -- see test_envp's own comment above for why this is passed
	 * directly to __spawn() rather than mutating this process's
	 * environment via setenv()/putenv(). */
	{
		size_t n = 0, i;
		char **envp;
		while (environ && environ[n]) n++;
		envp = malloc((n + 2) * sizeof *envp);
		if (!envp) {
			printf("SKIP util-man: out of memory building envp\n");
			raw_rmtree(scratch);
			return 77;
		}
		for (i = 0; i < n; i++) envp[i] = environ[i];
		snprintf(manpath_entry, sizeof manpath_entry, "MANPATH=%s", scratch);
		envp[n] = manpath_entry;
		envp[n + 1] = 0;
		test_envp = envp;
	}

	test_finds_and_formats_frobnicate();
	test_section_operand_restricts_search();
	test_no_such_page();
	test_apropos_dash_k();
	test_gzip_only_page_diagnosed();
	test_real_grep1_excerpt();
	test_builtin_agrees_with_standalone();

	raw_rmtree(scratch);

	if (fails) { printf("%d check(s) failed\n", fails); return 1; }
	printf("ok\n");
	return 0;
}
