/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ioctl(): NOT a POSIX interface -- POSIX deliberately specifies
 * termios(3) (include/termios.h) instead of a general ioctl(2) for
 * terminal control, precisely because ioctl request numbers and
 * semantics are not standardized across systems. This header exists
 * anyway because it is a de-facto-universal BSD/SVR4 extension that a
 * large amount of portable-in-practice code assumes exists alongside
 * termios.h (FIONREAD and TIOCGWINSZ above all). Implemented in
 * src/ioctl/ioctl.c, which documents the exact, small set of requests
 * given a real answer here and what an unrecognised request does
 * (nothing silent -- see that file's banner).
 *
 * Request numbers below match Linux's (asm-generic/ioctls.h) rather
 * than inventing new ones: nothing in POSIX or any other standard
 * assigns these values, but a huge amount of existing source calls
 * ioctl() with the numeric macro from <sys/ioctl.h>, never a literal,
 * so matching the most common convention costs nothing and avoids
 * surprising a portable program that happens to hardcode one anyway.
 */
#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

/* TIOCGWINSZ: terminal window size (src/ioctl/ioctl.c, backed by
 * kernel32's GetConsoleScreenBufferInfo() -- NTLIBC_USE_KERNEL32 only,
 * same reason as termios.h's ISIG/ICANON/ECHO). */
struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};
#define TIOCGWINSZ 0x5413

/* FIONREAD: bytes immediately readable without blocking. Real for a
 * pipe (the same NtQueryInformationFile(FilePipeLocalInformation)
 * ReadDataAvailable field src/select/select.c's __fd_probe() already
 * queries for pipe readability -- not duplicated logic, the same NT
 * mechanism applied to get a byte count instead of a boolean) and for
 * a regular file (bytes remaining until EOF, via
 * FileStandardInformation/FilePositionInformation). Not supported for
 * anything else -- see src/ioctl/ioctl.c. */
#define FIONREAD 0x541B

/* FIONBIO: toggle O_NONBLOCK. See src/ioctl/ioctl.c for exactly what
 * O_NONBLOCK does and does not change in this library today. */
#define FIONBIO 0x5421

int ioctl(int, unsigned long, ...);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
