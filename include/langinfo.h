/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <langinfo.h>: names for locale data.  See src/misc/langinfo.c.
 *
 * Values are implementation-defined and assigned as a dense range 0..54 so
 * src/misc/langinfo.c can index one array and reject any out-of-range item
 * (including the (nl_item)-1 a caller may probe with) with one bounds
 * test. DAY_1..DAY_7, ABDAY_1..ABDAY_7, MON_1..MON_12 and ABMON_1..ABMON_12
 * must each stay contiguous and ascending, since callers index into them.
 */
#ifndef _LANGINFO_H
#define _LANGINFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <nl_types.h>

#define __NEED_locale_t

#include <bits/alltypes.h>

/* LC_CTYPE */
#define CODESET      0

/* LC_TIME */
#define D_T_FMT      1
#define D_FMT        2
#define T_FMT        3
#define T_FMT_AMPM   4
#define AM_STR       5
#define PM_STR       6

#define DAY_1        7
#define DAY_2        8
#define DAY_3        9
#define DAY_4       10
#define DAY_5       11
#define DAY_6       12
#define DAY_7       13

#define ABDAY_1     14
#define ABDAY_2     15
#define ABDAY_3     16
#define ABDAY_4     17
#define ABDAY_5     18
#define ABDAY_6     19
#define ABDAY_7     20

#define MON_1       21
#define MON_2       22
#define MON_3       23
#define MON_4       24
#define MON_5       25
#define MON_6       26
#define MON_7       27
#define MON_8       28
#define MON_9       29
#define MON_10      30
#define MON_11      31
#define MON_12      32

#define ABMON_1     33
#define ABMON_2     34
#define ABMON_3     35
#define ABMON_4     36
#define ABMON_5     37
#define ABMON_6     38
#define ABMON_7     39
#define ABMON_8     40
#define ABMON_9     41
#define ABMON_10    42
#define ABMON_11    43
#define ABMON_12    44

#define ERA         45
#define ERA_D_FMT   46
#define ERA_D_T_FMT 47
#define ERA_T_FMT   48
#define ALT_DIGITS  49

/* LC_NUMERIC */
#define RADIXCHAR   50
#define THOUSEP     51

/* LC_MESSAGES */
#define YESEXPR     52
#define NOEXPR      53

/* LC_MONETARY */
#define CRNCYSTR    54

char *nl_langinfo(nl_item);
char *nl_langinfo_l(nl_item, locale_t);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
