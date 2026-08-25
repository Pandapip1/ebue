/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <tar.h> -- the ustar archive format's symbolic constants.
 *
 * tar.h.html DESCRIPTION: "The <tar.h> header shall define the following
 * symbolic constants with the indicated values".  A pure constants
 * header: it declares no functions and there is no behaviour behind it,
 * so nothing in src/ corresponds to this file.  Consumers -- GNU tar and
 * pax among them -- read the ustar typeflags and magic from here rather
 * than defining their own.
 *
 * POSIX base, not an option: the SYNOPSIS box carries no option-group
 * margin marker.  The only [XSI] on the page is on the single constant
 * TSVTX, which is marked below.  It is defined unconditionally anyway,
 * the way the rest of this tree treats XSI constants that cost nothing.
 */

#ifndef	_TAR_H
#define	_TAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

/* General definitions.
 *
 * Note the asymmetry, which is the standard's and not a typo here:
 * TMAGIC is "used in the magic field ... INCLUDING the trailing null
 * byte", so TMAGLEN is 6 for the five characters of "ustar" plus the
 * NUL; TVERSION is used "EXCLUDING the trailing null byte", so TVERSLEN
 * is 2 for exactly the two characters of "00". */
#define TMAGIC    "ustar"        /* ustar plus null byte. */
#define TMAGLEN   6              /* Length of the above. */
#define TVERSION  "00"           /* 00 without a null byte. */
#define TVERSLEN  2              /* Length of the above. */

/* Typeflag field definitions.  These are the bytes that appear in the
 * archive itself, so the values are exact rather than minima. */
#define REGTYPE   '0'            /* Regular file. */
#define AREGTYPE  '\0'           /* Regular file. */
#define LNKTYPE   '1'            /* Link. */
#define SYMTYPE   '2'            /* Symbolic link. */
#define CHRTYPE   '3'            /* Character special. */
#define BLKTYPE   '4'            /* Block special. */
#define DIRTYPE   '5'            /* Directory. */
#define FIFOTYPE  '6'            /* FIFO special. */
#define CONTTYPE  '7'            /* Reserved. */

/* Mode field bit definitions (octal).  Deliberately NOT aliases for the
 * <sys/stat.h> S_I* macros: these are the archive format's own bits and
 * are fixed by the file format, whereas the S_I* values are the
 * platform's.  They coincide on this platform and are still spelled out
 * here, because a future divergence must not silently rewrite archives. */
#define TSUID     04000          /* Set UID on execution. */
#define TSGID     02000          /* Set GID on execution. */
#define TSVTX     01000          /* [XSI] On directories, restricted deletion flag. */
#define TUREAD    00400          /* Read by owner. */
#define TUWRITE   00200          /* Write by owner. */
#define TUEXEC    00100          /* Execute/search by owner. */
#define TGREAD    00040          /* Read by group. */
#define TGWRITE   00020          /* Write by group. */
#define TGEXEC    00010          /* Execute/search by group. */
#define TOREAD    00004          /* Read by other. */
#define TOWRITE   00002          /* Write by other. */
#define TOEXEC    00001          /* Execute/search by other. */

#ifdef __cplusplus
}
#endif

#endif
