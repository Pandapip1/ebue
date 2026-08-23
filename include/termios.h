/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * termios(3), general terminal interface:
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/termios.h.html
 * and the tc*()/cf*() function pages linked from there.  Implemented in
 * src/termios/termios.c, against the one kind of "terminal" this
 * platform has: an NT console (__FD_CONSOLE, see src/internal/libc.h
 * and src/unistd/isatty.c, which already gates on it).
 *
 * The mapping onto NT is genuinely partial, not a blanket yes or no --
 * see src/termios/termios.c's file banner for the clause-by-clause
 * accounting (what maps onto GetConsoleMode()/SetConsoleMode() for
 * real, what is accepted and stored but never applied because no NT
 * console concept backs it, and what is impossible outright because a
 * console has no serial line under it).  Short version: c_lflag's
 * ISIG/ICANON/ECHO are real (ENABLE_PROCESSED_INPUT/ENABLE_LINE_INPUT/
 * ENABLE_ECHO_INPUT); everything serial-line-shaped (c_cflag's baud/
 * parity/stop-bit/flow-control bits, cfgetispeed()/cfsetospeed(), the
 * output side of tcflush()/tcdrain(), tcsendbreak()) is not, and is
 * honestly N/A rather than faked.
 */
#ifndef _TERMIOS_H
#define _TERMIOS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_pid_t
#include <bits/alltypes.h>

typedef unsigned long tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

/* c_cc[] subscripts (termios.h.html "c_cc[] Subscript"/"Value" table).
 * See src/termios/termios.c for which of these have any console
 * analogue at all (short answer: none are independently settable --
 * VINTR's Ctrl-C and VEOF's Ctrl-Z are fixed, non-reprogrammable keys
 * the console recognises on its own; the rest have no console concept
 * whatsoever). All 16 round-trip through tcgetattr()/tcsetattr()
 * as plain stored bytes regardless. */
#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VTIME    5
#define VMIN     6
#define VSTART   7
#define VSTOP    8
#define VSUSP    9
#define VEOL     10
#define VREPRINT 11
#define VDISCARD 12
#define VWERASE  13
#define VLNEXT   14
#define VEOL2    15
#define NCCS     16

struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_cc[NCCS];
	/* Not POSIX-mandated struct members (POSIX instead requires
	 * cfgetispeed()/cfsetispeed() etc. below); added the way *BSD
	 * does, as the simplest honest place for cfsetispeed() et al to
	 * store a value that nothing on this platform ever reads back --
	 * see cfgetispeed.html's N/A note in src/termios/termios.c. */
	speed_t c_ispeed;
	speed_t c_ospeed;
};

/* c_iflag: input processing. Accepted and stored, round-tripped
 * through tcgetattr()/tcsetattr(), but not applied to anything -- NT's
 * ReadConsole() does not run a line discipline over console input that
 * any of these could hook (see src/termios/termios.c). */
#define IGNBRK  0000001
#define BRKINT  0000002
#define IGNPAR  0000004
#define PARMRK  0000010
#define INPCK   0000020
#define ISTRIP  0000040
#define INLCR   0000100
#define IGNCR   0000200
#define ICRNL   0000400
#define IXON    0002000
#define IXANY   0004000
#define IXOFF   0010000

/* c_oflag: output processing. Same status as c_iflag above. */
#define OPOST   0000001
#define ONLCR   0000004
#define OCRNL   0000010
#define ONOCR   0000020
#define ONLRET  0000040
#define OFILL   0000100
#define OFDEL   0000200

/* c_cflag: hardware control -- CSIZE/PARENB/PARODD/CSTOPB/CRTSCTS
 * describe a serial line's wire encoding (character size, parity,
 * stop bits, RTS/CTS flow control). A console handle has none of
 * these: console I/O is already framed as whole UTF-16 code units
 * through ReadConsole()/WriteConsole(), and there are no RTS/CTS
 * signal lines on a console to gate. Genuinely N/A, not merely
 * unimplemented -- accepted and stored like c_iflag/c_oflag, never
 * applied to anything, because there is nothing here for it to apply
 * to. */
#define CSIZE   0000060
#define CS5     0000000
#define CS6     0000020
#define CS7     0000040
#define CS8     0000060
#define CSTOPB  0000100
#define CREAD   0000200
#define PARENB  0000400
#define PARODD  0001000
#define HUPCL   0002000
#define CLOCAL  0004000
#define CRTSCTS 020000000000

/* c_lflag: local modes. ISIG/ICANON/ECHO are the real, load-bearing
 * three -- src/termios/termios.c maps them onto
 * ENABLE_PROCESSED_INPUT/ENABLE_LINE_INPUT/ENABLE_ECHO_INPUT via
 * kernel32's GetConsoleMode()/SetConsoleMode() (NTLIBC_USE_KERNEL32
 * only; see CONTRIBUTING.md -- there is no ntdll path to console mode
 * at all). The rest (ECHOE, ECHOK, ECHONL, NOFLSH, TOSTOP, IEXTEN) are
 * accepted and stored only, same as c_iflag/c_oflag. */
#define ISIG    0000001
#define ICANON  0000002
#define ECHO    0000010
#define ECHOE   0000020
#define ECHOK   0000040
#define ECHONL  0000100
#define NOFLSH  0000200
#define TOSTOP  0000400
#define IEXTEN  0100000

/* tcsetattr() optional_actions (tcsetattr.html DESCRIPTION). */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* tcflush() queue_selector (tcflush.html DESCRIPTION). */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

/* tcflow() action (tcflow.html DESCRIPTION). */
#define TCOOFF 0
#define TCOON  1
#define TCIOFF 2
#define TCION  3

/* cfgetispeed()/cfsetispeed() etc. speed_t values. POSIX leaves the
 * encoding unspecified (opaque B* constants on most systems, because a
 * real UART only supports a fixed set of rates); ntlibc has no real
 * serial line to encode a rate *for* (see the cfgetispeed.html N/A
 * note in src/termios/termios.c), so these are just the bps number
 * itself -- the simplest honest choice when the value is never read
 * back by anything but cfgetispeed(). */
#define B0        0
#define B50       50
#define B75       75
#define B110      110
#define B134      134
#define B150      150
#define B200      200
#define B300      300
#define B600      600
#define B1200     1200
#define B1800     1800
#define B2400     2400
#define B4800     4800
#define B9600     9600
#define B19200    19200
#define B38400    38400

int tcgetattr(int, struct termios *);
int tcsetattr(int, int, const struct termios *);
speed_t cfgetispeed(const struct termios *);
speed_t cfgetospeed(const struct termios *);
int cfsetispeed(struct termios *, speed_t);
int cfsetospeed(struct termios *, speed_t);
int tcflush(int, int);
int tcdrain(int);
int tcflow(int, int);
int tcsendbreak(int, int);
pid_t tcgetsid(int);

#ifdef __cplusplus
}
#endif
#endif
