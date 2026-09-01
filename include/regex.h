/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _REGEX_H
#define _REGEX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_size_t
#define __NEED_regoff_t
#include <bits/alltypes.h>

typedef struct {
	size_t re_nsub;		/* number of parenthesized subexpressions */
	void *__opaque;		/* implementation-private compiled form */
} regex_t;

typedef struct {
	regoff_t rm_so;
	regoff_t rm_eo;
} regmatch_t;

#define REG_EXTENDED	0x01
#define REG_ICASE	0x02
#define REG_NOSUB	0x04
#define REG_NEWLINE	0x08

#define REG_NOTBOL	0x01
#define REG_NOTEOL	0x02

#define REG_NOMATCH	1
#define REG_BADPAT	2
#define REG_ECOLLATE	3
#define REG_ECTYPE	4
#define REG_EESCAPE	5
#define REG_ESUBREG	6
#define REG_EBRACK	7
#define REG_EPAREN	8
#define REG_EBRACE	9
#define REG_BADBR	10
#define REG_ERANGE	11
#define REG_ESPACE	12
#define REG_BADRPT	13

/* preg/pattern both required (POSIX: undefined if either does not
 * designate a valid object/string). src/regex/regex.c's own body
 * dereferences preg unconditionally on every return path
 * (`preg->__opaque = ...`), and pattern -- though only ever assigned
 * into `struct parser`'s own `p` field there, never dereferenced by
 * regcomp() directly -- is what every one of that struct's own parser
 * functions dereferences through that field, with no NULL check
 * anywhere in the chain; see src/regex/regex.c's own ere_branch()/
 * bre_branch() comments for the specific findings this resolves. */
int regcomp(regex_t *__restrict, const char *__restrict, int)
    __attribute__((nonnull(1, 2)));
/* preg/string both required (preg->__opaque and strlen(string) are
 * both dereferenced unconditionally at entry). pmatch is deliberately
 * NOT marked: src/regex/regex.c's own body defensively checks it
 * (`if (matched && nmatch > 0 && pmatch && ...)`), matching POSIX's own
 * "nmatch == 0" convention for "the caller does not want match
 * offsets" -- the same "real check, not decoration" reasoning
 * 9be895e's own commit established for setenv/unsetenv's `name`. */
int regexec(const regex_t *__restrict, const char *__restrict, size_t, regmatch_t *__restrict, int)
    __attribute__((nonnull(1, 2)));
/* preg is deliberately NOT marked here: src/regex/regex.c's own body
 * never dereferences it (`(void)preg;`) -- POSIX explicitly permits an
 * implementation to ignore it. errbuf is likewise not marked: it is
 * only dereferenced when errbuf_size != 0, POSIX's own documented
 * convention for "just tell me how big a buffer I would need". */
size_t regerror(int, const regex_t *__restrict, char *__restrict, size_t);
/* preg required: src/regex/regex.c's own body dereferences it
 * unconditionally (`struct rx *rx = preg->__opaque;`), with no
 * defensive check of preg itself (only of the rx it derives). */
void regfree(regex_t *) __attribute__((nonnull(1)));

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
