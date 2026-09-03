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
#include <memory_tokens.h>

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

int regcomp(regex_t *__restrict, const char *__restrict, int)
    __attribute__((nonnull(1, 2)));
/* pmatch is deliberately not marked nonnull: it is defensively checked,
 * matching POSIX's "nmatch == 0" convention for "no match offsets wanted". */
int regexec(const regex_t *__restrict, const char *__restrict, size_t, regmatch_t *__restrict, int)
    __attribute__((nonnull(1, 2)));
/* preg is unused here -- POSIX permits an implementation to ignore it.
 * errbuf is only dereferenced when errbuf_size != 0. */
size_t regerror(int errcode, const regex_t *__restrict preg,
	char *__restrict errbuf withtok(writable_span(errbuf_size)),
	size_t errbuf_size);
void regfree(regex_t *) __attribute__((nonnull(1)));

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
