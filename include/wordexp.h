/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <wordexp.h>: shell word expansion, minus the parts that genuinely
 * need a POSIX shell interpreter this platform does not have.  See
 * src/wordexp/wordexp.c for the parser/expander and its own extensive
 * header comment; the short version, repeated here because it is the
 * one thing this file's design hinges on:
 *
 * wordexp.html DESCRIPTION says wordexp() performs expansion "as
 * described in XCU Word Expansions", i.e. as if by the shell in XBD
 * Shell Command Language.  This platform has no such shell (see
 * src/stdio/misc.c's popen(), which hands shell work to cmd.exe /c
 * specifically because there is no /bin/sh, and cmd.exe cannot parse
 * $(...) or $((...)) at all -- a different, incompatible grammar).
 * Command substitution ($(cmd)/`cmd`) and arithmetic expansion
 * ($((expr))) both require running an embedded, arbitrarily complex
 * *command list*, which is genuinely out of a libc function's reach.
 *
 * WHAT WORDEXP() RETURNS FOR A CONSTRUCT IT CANNOT PERFORM: the spec
 * defines WRDE_CMDSUB for exactly one case -- "command substitution
 * requested" while WRDE_NOCMD is set.  It says nothing directly about
 * command substitution appearing when WRDE_NOCMD is *not* set, beyond
 * "command substitution is permitted" (i.e. the caller is telling
 * wordexp() it is willing to have a command run for it).  Since this
 * implementation can never run one -- there is no shell to hand it
 * to, not even conditionally -- returning anything other than an
 * error there would mean either silently dropping the construct or
 * fabricating output, both of which are wrong answers dressed up as
 * success. WRDE_CMDSUB is the closest honest fit ("a command
 * substitution was requested and refused"), so this implementation
 * returns WRDE_CMDSUB for *any* unquoted $(...), `...`, or $((...))
 * it encounters, unconditionally -- not only when WRDE_NOCMD is set.
 * WRDE_NOCMD therefore has no observable effect here beyond what
 * happens anyway; it is accepted so callers that pass it (the common,
 * safety-conscious case) are not rejected outright.
 *
 * Genuine gaps implemented: tilde expansion (~ and ~user, via
 * getenv("HOME") and include/pwd.h's getpwnam()), parameter expansion
 * of bare $VAR and ${VAR} against environ, pathname expansion
 * (delegates to <glob.h>), quote removal, and the WRDE_DOOFFS/
 * WRDE_APPEND/WRDE_REUSE bookkeeping flags -- including the field
 * splitting that bookkeeping needs (splitting literal, already-in-
 * memory text on unquoted IFS whitespace is the "trivial by itself"
 * half of XBD 2.6.5 this file's own commentary calls out; it is only
 * the *general* case -- tracking split boundaries through a dynamic,
 * command-substitution-produced result -- that is N/A here, and that
 * case cannot arise because command substitution always fails first).
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
