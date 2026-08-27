/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <wordexp.h>: shell word expansion.  See src/wordexp/wordexp.c for the
 * parser/expander and its own extensive header comment.
 *
 * wordexp.html DESCRIPTION says wordexp() performs expansion "as
 * described in XCU Word Expansions", i.e. as if by the shell in XBD
 * Shell Command Language.  This header used to open by explaining that
 * this platform had no such shell and that command substitution
 * ($(cmd)/`cmd`) was therefore refused outright with WRDE_CMDSUB,
 * because cmd.exe -- what src/stdio/misc.c's popen() and
 * src/stdlib/system.c hand shell work to -- cannot parse $(...) at all.
 *
 * That gap is closed.  ntlibc now has a POSIX shell of its own
 * (src/sh/, see test/sh-design.md for why a libc grew one and how it
 * links): a set of internal functions compiled into the same libc.a,
 * not a separate interpreter image discovered on PATH.  wordexp() runs
 * a command substitution by calling straight into it (__sh_cmdsub(),
 * src/internal/libc.h), so the substituted command list is parsed and
 * executed in-process, in the subshell environment XCU 2.6.3/2.12
 * require, with its standard output captured and its trailing
 * <newline> sequences removed.  A program that never calls
 * wordexp()/system()/popen() still never links any of it: libc.a is an
 * archive, and a member is pulled in only to satisfy an undefined
 * symbol.
 *
 * WRDE_NOCMD accordingly means what the standard says it means, and
 * only now has anything to refuse: "[f]ail if command substitution is
 * requested" -- wordexp() returns WRDE_CMDSUB for a $(...) or `...`
 * when, and only when, the caller passed WRDE_NOCMD.  Without it, the
 * substitution runs.  It never affects arithmetic expansion, which is
 * not command substitution and which XBD 2.6.4 does not gate behind it;
 * "$((" is read as an arithmetic expansion whenever it parses as one,
 * exactly as 2.6.4 requires ("arithmetic expansion has precedence"),
 * and only otherwise as a command substitution starting with a
 * subshell.
 *
 * What the shell behind that call-out does and does not cover is
 * src/sh/sh.h's banner and test/sh-design.md's business, not this
 * header's -- but one consequence is visible here: a substitution whose
 * command uses a construct that shell has no grammar for (if/while/
 * for/case, functions, aliases) comes back as WRDE_SYNTAX, the same
 * code src/wordexp/arith.c's header documents doing double duty for a
 * malformed arithmetic expression.  A command that simply fails, or is
 * not found, is NOT an error here: the substitution succeeds with
 * whatever the command wrote to standard output (nothing, typically),
 * exactly as in any shell.
 *
 * Implemented, none of which needs the shell: tilde expansion (~ and
 * ~user, via getenv("HOME") and include/pwd.h's getpwnam()), parameter
 * expansion of bare $VAR and ${VAR} against environ, arithmetic
 * expansion ($((expr)), src/wordexp/arith.c), pathname expansion
 * (delegates to <glob.h>), quote removal, and the
 * WRDE_DOOFFS/WRDE_APPEND/WRDE_REUSE bookkeeping flags.
 *
 * Field splitting (XBD 2.6.5), stated exactly, because this is the one
 * area where this header has previously claimed more than the
 * implementation does: <space>/<tab>/<newline> in the *input* text
 * separates fields when unquoted, and so does <space>/<tab>/<newline>
 * in the result of an *unquoted* command substitution (inside
 * double-quotes it does not, per XCU 2.6.3).  Two things it does NOT
 * do, both pre-existing and tracked separately from this header: the
 * result of a parameter expansion is not split (an unquoted $VAR whose
 * value contains a space stays one field), and IFS itself is never
 * consulted -- the three whitespace bytes above are hard-coded, so
 * setting IFS to anything, including the empty string, changes nothing.
 * Read no claim to the contrary into the paragraphs above.
 *
 * One thing this header used to list as missing and no longer is: XCU
 * 2.6's empty-field rule.  "If the complete expansion appropriate for a
 * word results in an empty field, that empty field shall be deleted
 * from the list of fields ... unless the original word contained
 * single-quote or double-quote characters" -- so an unquoted expansion
 * of an unset or null parameter now produces no field at all, where it
 * used to produce one empty one, and "$UNSET" still produces the empty
 * field the quotes require.  That changed because the shell behind
 * __sh_cmdsub() grew positional parameters and `f $1` with none set
 * passing one empty argument is the same defect under another name.
 *
 * The caller has no positional-parameter context, so $1/${10}/$@/$*
 * expand as an empty parameter list and $# expands to "0".  The shell
 * supplies its real positional parameters through the private
 * __wordexp_sh() entry point; see src/internal/libc.h and
 * src/sh/param.c.  $? remains private to that entry point because an
 * arbitrary wordexp() caller has no last-pipeline status.
 *
 * wordexp_t layout and the WRDE_* flags plus WRDE_BADCHAR through
 * WRDE_SYNTAX values are fixed at test/posix-glob.c's choices (that file predates this
 * header and declares its own local, unmodified copies of them, and
 * calls wordexp() through that local prototype rather than this
 * header -- matching values here is what lets a flags/return value
 * built from one file's macros mean the same thing to the other).
 */
#ifndef _WORDEXP_H
#define _WORDEXP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_size_t
#include <bits/alltypes.h>

typedef struct {
	size_t we_wordc;	/* count of words */
	char **we_wordv;	/* list of expanded words */
	size_t we_offs;		/* slots to reserve at we_wordv's front, if WRDE_DOOFFS */
} wordexp_t;

#define WRDE_APPEND	0x01
#define WRDE_DOOFFS	0x02
#define WRDE_NOCMD	0x04
#define WRDE_REUSE	0x08
#define WRDE_SHOWERR	0x10
#define WRDE_UNDEF	0x20

#define WRDE_BADCHAR	1
#define WRDE_BADVAL	2
#define WRDE_CMDSUB	3
#define WRDE_NOSPACE	4
#define WRDE_SYNTAX	5

int wordexp(const char *__restrict, wordexp_t *__restrict, int);
void wordfree(wordexp_t *);

#ifdef __cplusplus
}
#endif
#endif
