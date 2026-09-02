/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * tput(1p): `tput [-T type] operand`
 *
 * ---- what POSIX actually mandates ----------------------------------------
 *
 * The real XCU tput.html OPERANDS section is narrow -- verified against
 * the live spec text (Austin Group base, IEEE Std 1003.1-2017) before
 * writing this file, not assumed from general Unix `tput` folklore:
 *
 *   "The following strings shall be supported as operands by the
 *   implementation in the POSIX locale:
 *
 *    clear   Display the clear-screen sequence.
 *    init    Display the sequence that initializes the user's terminal
 *            in an implementation-defined manner.
 *    reset   Display the sequence that resets the user's terminal in an
 *            implementation-defined manner.
 *
 *   If a terminal does not support any of the operations described by
 *   these operands, this shall not be considered an error condition."
 *
 * DESCRIPTION, on terminal-type selection: "If this option [-T] is not
 * supplied and the TERM variable is unset or null, an unspecified
 * default terminal type shall be used. The setting of type shall take
 * precedence over the value in TERM."  This implementation's own
 * "unspecified default" choice is "dumb" (documented at TERM_DEFAULT
 * below) -- a real, named terminal type with no capabilities at all,
 * rather than silently picking something that pretends to know more
 * than nothing was actually specified.
 *
 * EXIT STATUS (quoted, then this file's own concrete mapping onto it):
 *   0     "The requested string was written successfully."
 *   1     "Unspecified."
 *   2     "Usage error."
 *   3     "No information is available about the specified terminal type."
 *   4     "The specified operand is invalid."
 *   >4    "An error occurred."
 * This implementation's mapping: 0 success; 1 a *recognised* capname
 * that the selected terminal type's entry simply does not define (the
 * "unspecified" bucket, used the same way real tput uses it for "the
 * terminal lacks this capability" -- POSIX leaves the exact meaning of
 * 1 up to the implementation); 2 a missing operand, a `-T` with no
 * argument, or a parameterized capname (`cup`) given the wrong number
 * of parameters; 3 `-T`/`$TERM` names a terminal type outside this
 * file's built-in table; 4 the operand is not `clear`/`init`/`reset`
 * and not a name in TERM_CAPNAMES below.
 *
 * ---- the capname extension beyond strict POSIX -----------------------
 *
 * Every real-world `tput` -- System V's, ncurses', BSD's -- additionally
 * accepts an arbitrary terminfo/termcap capability name as the operand
 * (`tput cols`, `tput bold`, `tput cup 5 10`, ...) and looks it up in a
 * terminal capability database.  POSIX's own OPERANDS section above does
 * not mandate this at all, but it is universal practice and exactly what
 * this project's own POSIX-utilities task direction asked for by name
 * (`tput cols`, `tput lines`, `tput bold`, `tput sgr0`).  Implemented
 * here as a deliberate, clearly-labeled extension: this file's table's
 * ten names (cols, lines, bold, smso, rmso, smul, rmul, rev, sgr0, cup)
 * -- the bounded set this project's task direction named -- rather than
 * a general capname-lookup mechanism accepting anything a real terminfo
 * file might define. `clear` is shared between the two: the POSIX
 * operand and the capname both mean the same escape sequence, so there
 * is only one table field for it.  Long (terminfo) names only -- the
 * two-letter termcap short-name aliases (co/li/md/so/se/us/ue/mr/me/cl/
 * cm/is2/rs1) are a real, additional tput/termcap compatibility feature
 * this file does not add, to keep the lookup table one name per
 * capability.
 *
 * ---- why a built-in table, not a real terminfo database reader -------
 *
 * A compiled terminfo binary (term(5)) is a real, well-specified,
 * parseable format, and this host's own dev environment happens to have
 * one (NixOS's `/run/current-system/sw/share/terminfo`, findable with
 * `infocmp`) -- but that path is an artifact of *this developer's*
 * machine, not something any binary this project actually ships could
 * rely on: Windows NT, ntlibc's other target, has no terminal database
 * of any kind, ever, and this project's native-Linux target is a
 * from-scratch bootstrap environment (see boot/kaem/*.kaem's own header
 * comment) that cannot assume ncurses-data/terminfo is installed either.
 * Reading the Nix-store copy would make this file work by accident on
 * one sandbox, not more honestly for a single real ntlibc user -- so
 * this table is deliberately what shipped instead: five terminal types
 * (xterm, xterm-256color, vt100, ansi, dumb) this project's own users
 * will realistically hit, with capability strings drawn from `infocmp`
 * against the real system database above (so they are the *real*
 * escape sequences those terminal types actually use, just hand-copied
 * into source rather than read from a binary file at run time) minus
 * one real, documented narrowing: `$<N>` padding/delay notations
 * (vt100's `bold=\E[1m$<2>` and friends) are stripped -- that syntax
 * exists for hardware terminals with real transmission-speed timing
 * constraints, which neither an NT console nor a modern pty has, so
 * every historical terminfo/termcap library on a modern OS treats
 * padding as a no-op too. An unrecognised `-T`/`$TERM` fails cleanly
 * (exit 3) rather than fabricating capabilities for a terminal type
 * this file knows nothing about, per this project's own task direction.
 *
 * `cols`/`lines` still try one real, live answer first -- ioctl(1,
 * TIOCGWINSZ) (src/ioctl/ioctl.c) -- before falling back to the table's
 * static value, the same "ask the real terminal if one is actually
 * attached, else fall back to the database" order every real tput/
 * ncurses uses (COLUMNS/LINES env, then ioctl, then terminfo). Only
 * the ioctl step is implemented here (no COLUMNS/LINES env override) --
 * a deliberate narrowing, not an oversight: env-var override is a
 * separate, additional layer real implementations add on top of the
 * ioctl/database answer, not a substitute for it, and this project's
 * own bootstrap use case has no need for it yet.
 *
 * `cup`'s row/col parameters are taken 0-based (curses' own convention
 * for cursor position, and what a program driving `tput cup` through a
 * shell is normally computing), then written out incremented by one --
 * matching the real `%i%p1%d;%p2%dH` terminfo string every terminal in
 * this table actually has (`%i` is terminfo's own "increment both
 * parameters" operator; ANSI CUP is 1-based). No general terminfo
 * parameter-string interpreter (`%p`, `%d`, `%?`, arithmetic, ...) is
 * implemented -- cup is the only parameterized capability this table
 * carries, and all covered entries share the identical CSI-row;col-H
 * shape, so it is hand-written once (print_cup() below) rather than
 * building a general tparm() for a single caller.
 *
 * `init`/`reset` are real POSIX operands (quoted above) but this
 * table defines no is1/is2/is3/rs1/rs2/rs3-equivalent sequence for any
 * covered terminal (matching the real terminfo entries themselves,
 * where xterm/vt100/ansi mostly leave these capabilities empty too) --
 * so both operands succeed (exit 0) and write nothing, per the spec's
 * own "shall not be considered an error condition" sentence quoted
 * above.  `longname` is not implemented: it is real historical `tput`
 * practice, not a POSIX operand, and this table has no long-description
 * field to back it with.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "util.h"

/* This implementation's own "unspecified default" (this file's header
 * comment, DESCRIPTION quote) when -T is absent and $TERM is unset or
 * null. */
#define TERM_DEFAULT "dumb"

struct term_entry {
	const char *name;
	int cols;    /* -1: capability not defined for this terminal */
	int lines;   /* -1: capability not defined for this terminal */
	const char *bold, *smso, *rmso, *smul, *rmul, *rev, *sgr0, *clear;
	int has_cup; /* cursor addressing (cup): all covered terminals but
	              * "dumb" share the identical \E[%i%p1%d;%p2%dH shape,
	              * so only whether it exists needs recording. */
};

/* Capability strings below are the real escape sequences these five
 * terminal types actually use (verified against `infocmp` reading this
 * dev host's own system terminfo database -- see this file's header
 * comment for exactly what was, and was not, carried over). */
static const struct term_entry term_table[] = {
	{ "xterm",          80, 24, "\033[1m", "\033[7m", "\033[27m", "\033[4m", "\033[24m", "\033[7m", "\033(B\033[m", "\033[H\033[2J", 1 },
	{ "xterm-256color", 80, 24, "\033[1m", "\033[7m", "\033[27m", "\033[4m", "\033[24m", "\033[7m", "\033(B\033[m", "\033[H\033[2J", 1 },
	{ "vt100",          80, 24, "\033[1m", "\033[7m", "\033[m",   "\033[4m", "\033[m",   "\033[7m", "\033[m\017",  "\033[H\033[J",  1 },
	{ "ansi",           80, 24, "\033[1m", "\033[7m", "\033[m",   "\033[4m", "\033[m",   "\033[7m", "\033[0;10m", "\033[H\033[J",  1 },
	{ "dumb",           80, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
};
#define TERM_TABLE_N (sizeof term_table / sizeof term_table[0])

static const struct term_entry *lookup_term(const char *name)
{
	size_t i;
	for (i = 0; i < TERM_TABLE_N; i++)
		if (strcmp(term_table[i].name, name) == 0) return &term_table[i];
	return 0;
}

/* Live terminal size via TIOCGWINSZ (src/ioctl/ioctl.c), falling back
 * to the table's static value on any failure (not a tty, no console,
 * wrong platform) -- see this file's header comment for the real
 * "ask the terminal, then the database" ordering this follows. */
static int live_dimension(int want_cols)
{
	struct winsize ws;
	if (!isatty(1)) return -1;
	if (ioctl(1, TIOCGWINSZ, &ws) < 0) return -1;
	return want_cols ? (int)ws.ws_col : (int)ws.ws_row;
}

static int print_numeric(int table_value, int want_cols)
{
	int live = live_dimension(want_cols);
	int v = live >= 0 ? live : table_value;
	if (v < 0) return 1; /* not defined for this terminal: "unspecified" */
	printf("%d", v);
	return 0;
}

static int print_string(const char *s)
{
	if (!s) return 1; /* not defined for this terminal: "unspecified" */
	if (fputs(s, stdout) == EOF) return 5; /* real I/O failure: ">4 An error occurred" */
	return 0;
}

/* cup row col: 0-based on input (this file's header comment), written
 * out as the real \E[%i%p1%d;%p2%dH shape (1-based, CSI row;col H)
 * every covered non-dumb terminal actually has. */
static int print_cup(const struct term_entry *t, const char *rowarg, const char *colarg)
{
	char *end1, *end2;
	long row, col;

	if (!t->has_cup) return 1; /* not defined for this terminal */
	row = strtol(rowarg, &end1, 10);
	col = strtol(colarg, &end2, 10);
	if (*end1 || *end2 || !*rowarg || !*colarg || row < 0 || col < 0)
		return 2; /* usage error: not valid non-negative integers */
	if (printf("\033[%ld;%ldH", row + 1, col + 1) < 0) return 5;
	return 0;
}

int __util_tput_main(
	int argc, char **argv elements_withtok(null_terminated, argc))
{
	const char *type = 0, *op;
	const struct term_entry *t;
	int i = 1;

	if (argc >= 3 && strcmp(argv[1], "-T") == 0) {
		type = argv[2];
		i = 3;
	} else if (argc >= 2 && strncmp(argv[1], "-T", 2) == 0 && argv[1][2]) {
		type = argv[1] + 2;
		i = 2;
	} else if (argc >= 2 && strcmp(argv[1], "-T") == 0) {
		__util_diagf("tput: -T: option requires an argument\n");
		return 2;
	}

	if (!type) {
		type = getenv("TERM");
		if (!type || !*type) type = TERM_DEFAULT;
	}

	if (i >= argc) {
		__util_diagf("tput: missing operand\n");
		return 2;
	}
	op = argv[i];

	t = lookup_term(type);
	if (!t) {
		__util_diagf("tput: %s: unknown terminal type\n", type);
		return 3;
	}

	if (strcmp(op, "clear") == 0) return print_string(t->clear);
	if (strcmp(op, "init") == 0 || strcmp(op, "reset") == 0) return 0;
	if (strcmp(op, "cols") == 0) return print_numeric(t->cols, 1);
	if (strcmp(op, "lines") == 0) return print_numeric(t->lines, 0);
	if (strcmp(op, "bold") == 0) return print_string(t->bold);
	if (strcmp(op, "smso") == 0) return print_string(t->smso);
	if (strcmp(op, "rmso") == 0) return print_string(t->rmso);
	if (strcmp(op, "smul") == 0) return print_string(t->smul);
	if (strcmp(op, "rmul") == 0) return print_string(t->rmul);
	if (strcmp(op, "rev") == 0) return print_string(t->rev);
	if (strcmp(op, "sgr0") == 0) return print_string(t->sgr0);
	if (strcmp(op, "cup") == 0) {
		if (i + 2 >= argc) {
			__util_diagf("tput: cup: requires row and column operands\n");
			return 2;
		}
		return print_cup(t, argv[i + 1], argv[i + 2]);
	}

	__util_diagf("tput: %s: invalid operand\n", op);
	return 4;
}

// NOLINTEND(misc-include-cleaner)
