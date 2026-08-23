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

int regcomp(regex_t *__restrict, const char *__restrict, int);
int regexec(const regex_t *__restrict, const char *__restrict, size_t, regmatch_t *__restrict, int);
size_t regerror(int, const regex_t *__restrict, char *__restrict, size_t);
void regfree(regex_t *);

#ifdef __cplusplus
}
#endif

#endif
