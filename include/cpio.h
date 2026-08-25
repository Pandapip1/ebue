/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <cpio.h> -- the cpio archive format's symbolic constants.
 *
 * cpio.h.html DESCRIPTION: "The <cpio.h> header shall define the
 * symbolic constants needed by the c_mode field of the cpio archive
 * format", and "shall define the following symbolic constant as a
 * string: MAGIC "070707"".  A pure constants header: it declares no
 * functions, so nothing in src/ corresponds to it.
 *
 * POSIX base, not an option.  The header carried the [XSI] margin in
 * earlier issues and was moved to Base in Issue 7, which is the issue
 * this tree audits against -- so there is no option-group margin to
 * hide behind here.
 *
 * The values are the ARCHIVE FORMAT's, and are deliberately not aliased
 * to <sys/stat.h>'s S_I* macros even where they coincide: those are the
 * platform's numbers and these are the file format's.  Aliasing them
 * would let a future platform change silently rewrite archives.
 */

#ifndef	_CPIO_H
#define	_CPIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

/* c_mode field permission bits. */
#define C_IRUSR   0000400        /* Read by owner. */
#define C_IWUSR   0000200        /* Write by owner. */
#define C_IXUSR   0000100        /* Execute by owner. */
#define C_IRGRP   0000040        /* Read by group. */
#define C_IWGRP   0000020        /* Write by group. */
#define C_IXGRP   0000010        /* Execute by group. */
#define C_IROTH   0000004        /* Read by others. */
#define C_IWOTH   0000002        /* Write by others. */
#define C_IXOTH   0000001        /* Execute by others. */
#define C_ISUID   0004000        /* Set user ID. */
#define C_ISGID   0002000        /* Set group ID. */
#define C_ISVTX   0001000        /* On directories, restricted deletion flag. */

/* c_mode field file-type values.  Note these are NOT a bitmask: they
 * are distinct encodings sharing the same field, which is why C_ISCTG
 * (0110000) and C_ISREG (0100000) overlap in bits rather than being
 * independent flags. */
#define C_ISDIR   0040000        /* Directory. */
#define C_ISFIFO  0010000        /* FIFO. */
#define C_ISREG   0100000        /* Regular file. */
#define C_ISBLK   0060000        /* Block special. */
#define C_ISCHR   0020000        /* Character special. */
#define C_ISCTG   0110000        /* Reserved. */
#define C_ISLNK   0120000        /* Symbolic link. */
#define C_ISSOCK  0140000        /* Socket. */

/* Six digits, plus the terminating null the string literal carries. */
#define MAGIC     "070707"

#ifdef __cplusplus
}
#endif

#endif
