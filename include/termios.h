/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * termios(3), general terminal interface:
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/termios.h.html
 * and the tc*()/cf*() function pages linked from there.  Two backends,
 * one per platform (never both in the same build -- see src/termios/
 * termios.c's own banner): src/termios/termios.c, against the one kind
 * of "terminal" NT has: an NT console (__FD_CONSOLE, see
 * src/internal/libc.h and src/unistd/isatty.c, which already gates on
 * it); and src/termios/linux/plat_termios.c, real on Linux via
 * ioctl(2) against any genuine tty/pty fd.
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
 * honestly N/A rather than faked.  On Linux none of that is N/A: a real
 * tty/pty has a genuine line discipline and (for a pty) genuine baud/
 * control-mode storage, so src/termios/linux/plat_termios.c's own
 * banner documents real ioctl(2) coverage for every clause instead.
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

/* c_oflag delay masks. [XSI] in termios.h.html's Output Modes table,
 * alongside ONLCR/OCRNL/ONOCR/ONLRET/OFILL/OFDEL above, and in scope
 * here for the same reason those are: this tree compiles
 * -D_XOPEN_SOURCE=700. Each name is a field mask over its own values
 * rather than a single flag bit, so a value lies inside its mask and no
 * mask may overlap another or any of the flag bits above; the layout is
 * the conventional one, which fits in the bits left free above OFDEL.
 * NL0/CR0/TAB0/BS0/VT0/FF0 are zero because "no delay" is the field
 * being clear, not a value set in it.
 *
 * Nothing here ever waits: a console write is finished by the time
 * WriteConsole() returns, and there is no wire to pad a delay out on,
 * so a delay field is accepted and stored and never applied -- the same
 * status ONLCR and OFILL already have, which is why it is a reason to
 * define these rather than to leave them out. The names have to exist
 * for code that merely mentions one to compile, and code that reads a
 * c_oflag back and clears TABDLY out of it is doing nothing this
 * platform cannot honour. */
#define NLDLY   0000400
#define NL0     0000000
#define NL1     0000400
#define CRDLY   0003000
#define CR0     0000000
#define CR1     0001000
#define CR2     0002000
#define CR3     0003000
#define TABDLY  0014000
#define TAB0    0000000
#define TAB1    0004000
#define TAB2    0010000
#define TAB3    0014000
#define BSDLY   0020000
#define BS0     0000000
#define BS1     0020000
#define VTDLY   0040000
#define VT0     0000000
#define VT1     0040000
#define FFDLY   0100000
#define FF0     0000000
#define FF1     0100000

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

/* t is required by every one of these six: src/termios/termios.c's
 * own bodies dereference it unconditionally (tcgetattr()'s own
 * `t->c_iflag = shadow.iflag;` and friends once get_console()
 * succeeds; tcsetattr()'s own `shadow.iflag = t->c_iflag;` and
 * friends once the act check passes; the four cf*speed() one-liners
 * dereference t directly with nothing else in their own bodies at
 * all), with no NULL check of t itself anywhere. Every real call site
 * in this tree (test/posix-termios.c, test/posix-dl.c) always passes
 * the address of a real, on-stack struct termios, never NULL --
 * confirmed against test/posix-termios.c's own `tcsetattr(consolefd,
 * 999, 0)` too, whose `0` for t is reached only because the earlier
 * `act` validation (a real, load-bearing check) rejects the call
 * before t is ever touched. */
int tcgetattr(int, struct termios *) __attribute__((nonnull(2)));
int tcsetattr(int, int, const struct termios *) __attribute__((nonnull(3)));
speed_t cfgetispeed(const struct termios *) __attribute__((nonnull(1)));
speed_t cfgetospeed(const struct termios *) __attribute__((nonnull(1)));
int cfsetispeed(struct termios *, speed_t) __attribute__((nonnull(1)));
int cfsetospeed(struct termios *, speed_t) __attribute__((nonnull(1)));
int tcflush(int, int);
int tcdrain(int);
int tcflow(int, int);
int tcsendbreak(int, int);
pid_t tcgetsid(int);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
