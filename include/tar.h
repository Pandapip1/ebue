/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <tar.h> -- the ustar archive format's symbolic constants.
 */

#ifndef	_TAR_H
#define	_TAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

/* TMAGLEN counts TMAGIC including its trailing NUL (6); TVERSLEN counts
 * TVERSION excluding it (2) -- an asymmetry from the standard, not a
 * typo. */
#define TMAGIC    "ustar"
#define TMAGLEN   6
#define TVERSION  "00"
#define TVERSLEN  2

/* AREGTYPE is the pre-ustar way of marking a regular file; both it and
 * REGTYPE mean the same thing. */
#define REGTYPE   '0'
#define AREGTYPE  '\0'
#define LNKTYPE   '1'
#define SYMTYPE   '2'
#define CHRTYPE   '3'
#define BLKTYPE   '4'
#define DIRTYPE   '5'
#define FIFOTYPE  '6'
#define CONTTYPE  '7'

/* Deliberately NOT aliases for <sys/stat.h>'s S_I* macros: these are
 * the archive format's own bits, fixed by the file format. */
#define TSUID     04000
#define TSGID     02000
#define TSVTX     01000
#define TUREAD    00400
#define TUWRITE   00200
#define TUEXEC    00100
#define TGREAD    00040
#define TGWRITE   00020
#define TGEXEC    00010
#define TOREAD    00004
#define TOWRITE   00002
#define TOEXEC    00001

#ifdef __cplusplus
}
#endif

#endif
