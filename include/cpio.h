/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <cpio.h> -- the cpio archive format's symbolic constants.
 *
 * Deliberately not aliased to <sys/stat.h>'s S_I* macros even where
 * they coincide: those are the platform's numbers, these are the file
 * format's, and aliasing them would let a platform change silently
 * rewrite archives.
 */

#ifndef	_CPIO_H
#define	_CPIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define C_IRUSR   0000400
#define C_IWUSR   0000200
#define C_IXUSR   0000100
#define C_IRGRP   0000040
#define C_IWGRP   0000020
#define C_IXGRP   0000010
#define C_IROTH   0000004
#define C_IWOTH   0000002
#define C_IXOTH   0000001
#define C_ISUID   0004000
#define C_ISGID   0002000
#define C_ISVTX   0001000

/* Distinct encodings sharing one field, not a bitmask -- C_ISCTG
 * (0110000) and C_ISREG (0100000) overlap in bits. */
#define C_ISDIR   0040000
#define C_ISFIFO  0010000
#define C_ISREG   0100000
#define C_ISBLK   0060000
#define C_ISCHR   0020000
#define C_ISCTG   0110000
#define C_ISLNK   0120000
#define C_ISSOCK  0140000

#define MAGIC     "070707"

#ifdef __cplusplus
}
#endif

#endif
