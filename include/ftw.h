/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _FTW_H
#define _FTW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <sys/stat.h>

struct FTW {
	int base;
	int level;
};

#define FTW_F	1
#define FTW_D	2
#define FTW_DNR	3
#define FTW_NS	4
#define FTW_SL	5
#define FTW_DP	6
#define FTW_SLN	7

#define FTW_PHYS	0x01
#define FTW_MOUNT	0x02
#define FTW_DEPTH	0x04
#define FTW_CHDIR	0x08

int ftw(const char *, int (*)(const char *, const struct stat *, int), int);
int nftw(const char *, int (*)(const char *, const struct stat *, int, struct FTW *), int, int);

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
