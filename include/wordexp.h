/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <wordexp.h>: shell word expansion, as described in XCU Word
 * Expansions (wordexp.html DESCRIPTION).  See src/wordexp/wordexp.c
 * for the parser/expander.
 *
 * Command substitution ($(cmd)/`cmd`) runs through ntlibc's own
 * internal POSIX shell (src/sh/, see test/sh-design.md for why a libc
 * grew one and how it links) via __sh_cmdsub() (src/internal/libc.h):
 * the command list is parsed and executed in-process, in the subshell
 * environment XCU 2.6.3/2.12 require, with standard output captured
 * and trailing <newline> sequences removed.  A program that never
 * calls wordexp()/system()/popen() never links any of it, since
 * libc.a only pulls in members that satisfy an undefined symbol.
 *
 * WRDE_NOCMD: "[f]ail if command substitution is requested" --
 * wordexp() returns WRDE_CMDSUB for a $(...) or `...` only when the
 * caller passed WRDE_NOCMD; without it, the substitution runs.  It
 * never affects arithmetic expansion, which XBD 2.6.4 gives precedence
 * over command substitution ("$((" is read as arithmetic whenever it
 * parses as one, and only otherwise as a command substitution starting
 * with a subshell).
 *
 * A substitution whose command uses a construct the internal shell has
 * no grammar for (if/while/for/case, functions, aliases) comes back as
 * WRDE_SYNTAX, the same code src/wordexp/arith.c uses for a malformed
 * arithmetic expression.  A command that simply fails, or is not
 * found, is NOT an error here: the substitution succeeds with whatever
 * it wrote to standard output (nothing, typically), exactly as in any
 * shell.
 *
 * Implemented, none of which needs the shell: tilde expansion (~ and
 * ~user, via getenv("HOME") and include/pwd.h's getpwnam()), parameter
 * expansion of $VAR/${VAR}, ${#VAR}, the -, +, = and ? default/alternate
 * operators, and #/##/%/%% pattern removal against environ (assignments
 * are visible for the duration of one wordexp() call), arithmetic
 * expansion ($((expr)), src/wordexp/arith.c), pathname expansion
 * (delegates to <glob.h>), ordinary and POSIX.1-2024 dollar-single
 * quoting, quote removal, and the
 * WRDE_DOOFFS/WRDE_APPEND/WRDE_REUSE bookkeeping flags.
 *
 * Field splitting (XBD 2.6.5): unquoted expansion results are split on
 * IFS, with <space>/<tab>/<newline> as the default and no splitting for
 * a null IFS.  This applies to parameter and command-substitution
 * results; double-quoted results are not split.  Unquoted whitespace in
 * the input language separates words independently of IFS.
 *
 * Per XCU 2.6's empty-field rule ("If the complete expansion
 * appropriate for a word results in an empty field, that empty field
 * shall be deleted from the list of fields ... unless the original
 * word contained single-quote or double-quote characters"), an
 * unquoted expansion of an unset or null parameter produces no field
 * at all, while "$UNSET" still produces the empty field the quotes
 * require.
 *
 * The caller has no positional-parameter context, so $1/${10}/$@/$*
 * expand as an empty parameter list and $# expands to "0".  The shell
 * supplies its real positional parameters through the private
 * __wordexp_sh() entry point; see src/internal/libc.h and
 * src/sh/param.c.  $? remains private to that entry point because an
 * arbitrary wordexp() caller has no last-pipeline status.
 *
 * wordexp_t's layout and the WRDE_* flags plus the WRDE_BADCHAR through
 * WRDE_SYNTAX values must match test/posix-glob.c's own local copies of
 * them (that file declares its own prototype rather than including
 * this header).
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

/* pwordexp is required: wordexp() dereferences it unconditionally on
 * every return path. words is not marked -- wordexp() only forwards
 * it into validate_words()/expand_impl(). wordfree() accepts NULL (and
 * a zeroed wordexp_t) by design and is not marked either. */
int wordexp(const char *__restrict, wordexp_t *__restrict, int)
    __attribute__((nonnull(2)));
void wordfree(wordexp_t *);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
