/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _LIMITS_H
#define _LIMITS_H

#include <features.h>
#include <bits/limits.h>

#define CHAR_BIT 8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
/* Plain `char`'s signedness is implementation-defined, not fixed by the
 * standard -- and this project's own aarch64 target is a real case where
 * it differs from the x86/tcc default: __CHAR_UNSIGNED__ is the GCC/Clang
 * predefined macro that reports it (verified: clang defines it for
 * aarch64 targets and leaves it undefined for x86_64; tcc, used only for
 * the genuinely-signed-char i386/x86_64 win32 targets, never defines it
 * either way, which still lands on the correct #else branch below). */
#ifdef __CHAR_UNSIGNED__
#define CHAR_MIN 0
#define CHAR_MAX UCHAR_MAX
#else
#define CHAR_MIN (-128)
#define CHAR_MAX 127
#endif
#define SHRT_MIN  (-1-0x7fff)
#define SHRT_MAX  0x7fff
#define USHRT_MAX 0xffff
#define INT_MIN  (-1-0x7fffffff)
#define INT_MAX  0x7fffffff
#define UINT_MAX 0xffffffffU
#define LONG_MIN (-LONG_MAX-1)
#define ULONG_MAX (2UL*LONG_MAX+1)
#define LLONG_MIN (-LLONG_MAX-1)
#define ULLONG_MAX (2ULL*LLONG_MAX+1)

#define MB_LEN_MAX 4

#define PIPE_BUF 4096
#define FILESIZEBITS 64
#define NAME_MAX 255
#define PATH_MAX 4096
#define NGROUPS_MAX 32
#define ARG_MAX 131072
#define IOV_MAX 1024
#define SYMLOOP_MAX 40
#define WORD_BIT 32
/* SSIZE_MAX: "Maximum value for an object of type ssize_t" -- ssize_t is
 * typedef'd from the arch's pointer-width _Addr (int on i386, long long
 * on x86_64; see arch/{i386,x86_64}/bits/alltypes.h.in), so its actual maximum is
 * arch-specific and is defined in bits/limits.h alongside LONG_MAX,
 * *not* derived from LONG_MAX here (long stays 32-bit on both arches
 * under this target's LLP64 model, so LONG_MAX would silently truncate
 * SSIZE_MAX to 2^31-1 on x86_64, where ssize_t is actually 64 bits --
 * see test/posix-limits.c). */
#define TZNAME_MAX 6
#define TTY_NAME_MAX 32
#define HOST_NAME_MAX 255
#define OPEN_MAX 1024
#define RTSIG_MAX 30
#define SIGQUEUE_MAX 32
#define TIMER_MAX 32
#define MQ_OPEN_MAX 1024
#define MQ_PRIO_MAX 32768
#define AIO_LISTIO_MAX 64
#define AIO_MAX 256
#define AIO_PRIO_DELTA_MAX 0

#define _POSIX_AIO_LISTIO_MAX   2
#define _POSIX_AIO_MAX          1
#define _POSIX_ARG_MAX          4096
#define _POSIX_CHILD_MAX        25
#define _POSIX_CLOCKRES_MIN     20000000
#define _POSIX_DELAYTIMER_MAX   32
#define _POSIX_HOST_NAME_MAX    255
#define _POSIX_LINK_MAX         8
#define _POSIX_LOGIN_NAME_MAX   9
#define _POSIX_MQ_OPEN_MAX      8
#define _POSIX_MQ_PRIO_MAX      32
#define _POSIX_MAX_CANON        255
#define _POSIX_MAX_INPUT        255
#define _POSIX_NAME_MAX         14
#define _POSIX_NGROUPS_MAX      8
#define _POSIX_OPEN_MAX         20
#define _POSIX_PATH_MAX         256
#define _POSIX_PIPE_BUF         512
#define _POSIX_RE_DUP_MAX       255
#define _POSIX_RTSIG_MAX        8
#define _POSIX_SEM_NSEMS_MAX    256
#define _POSIX_SEM_VALUE_MAX    32767
#define _POSIX_SIGQUEUE_MAX     32
#define _POSIX_SSIZE_MAX        32767
#define _POSIX_STREAM_MAX       8
#define _POSIX_SYMLINK_MAX      255
#define _POSIX_SYMLOOP_MAX      8
#define _POSIX_TIMER_MAX        32
#define _POSIX_TTY_NAME_MAX     9
#define _POSIX_TZNAME_MAX       6
#define _POSIX2_BC_BASE_MAX     99
#define _POSIX2_BC_DIM_MAX      2048
#define _POSIX2_BC_SCALE_MAX    99
#define _POSIX2_BC_STRING_MAX   1000
#define _POSIX2_CHARCLASS_NAME_MAX 14
#define _POSIX2_COLL_WEIGHTS_MAX 2
#define _POSIX2_EXPR_NEST_MAX   32
#define _POSIX2_LINE_MAX        2048
#define _POSIX2_RE_DUP_MAX      255

/* The rest of the "Minimum Values" table.  These are EXACT values, not
 * floors this implementation chooses: limits.h.html says the header
 * "shall define the following symbolic constants with the values shown.
 * These are the most restrictive values for certain features on an
 * implementation conforming to this volume of POSIX.1-2017."  They state
 * what a strictly conforming APPLICATION may rely on, so defining them
 * claims nothing whatever about ntlibc -- which is exactly why they are
 * safe to define while the corresponding capabilities are absent.
 *
 * That is the opposite of <unistd.h>'s _POSIX_THREADS, where defining
 * the constant WOULD be a claim about a capability we do not have.  The
 * two look alike and are not: one is a number the standard prints, the
 * other is an assertion that an option group is present. */
#define _POSIX_THREAD_DESTRUCTOR_ITERATIONS 4
#define _POSIX_THREAD_KEYS_MAX  128
#define _POSIX_THREAD_THREADS_MAX 64

/* [XSI].  Fenced separately from the three above because the triage
 * differs -- an absent XSI constant is a missing option group, which a
 * conforming implementation may lack, whereas an absent base constant is
 * a conformance hole.  They are defined here because ntlibc does not
 * decline XSI: it compiles -D_XOPEN_SOURCE=700, implements
 * readv()/writev() and <sys/uio.h>, and publishes IOV_MAX -- so
 * _XOPEN_IOV_MAX is the floor for a limit this library already has. */
#define _XOPEN_IOV_MAX          16
#define _XOPEN_NAME_MAX         255
#define _XOPEN_PATH_MAX         1024

/* "Runtime Increasable Values" -- limits.h.html: "The magnitude
 * limitations in the following list shall be fixed by specific
 * implementations.  An application should assume that the value of the
 * symbolic constant defined by <limits.h> in a specific implementation
 * is the minimum that pertains whenever the application is run under
 * that implementation."
 *
 * So unlike the Minimum Values above, these are NOT the standard's
 * numbers: each is a promise about THIS library, which sysconf() may
 * report as larger at run time but never as smaller.  Each must be at
 * least the printed minimum, which for every entry here is the
 * corresponding _POSIX2_ or _POSIX_ constant defined above.
 *
 * Seven of the nine are set to exactly that floor, because there is no
 * larger capability to claim: bc is not part of this library, and
 * collation is C-locale only (src/misc/locale.c).  Two are set higher,
 * because setting them to the floor would UNDERSTATE what this
 * implementation actually does -- and a Runtime Increasable value that
 * understates is a different kind of wrong from one that overstates,
 * but it is still wrong. */
#define BC_BASE_MAX             _POSIX2_BC_BASE_MAX
#define BC_DIM_MAX              _POSIX2_BC_DIM_MAX
#define BC_SCALE_MAX            _POSIX2_BC_SCALE_MAX
#define BC_STRING_MAX           _POSIX2_BC_STRING_MAX
#define CHARCLASS_NAME_MAX      _POSIX2_CHARCLASS_NAME_MAX
#define COLL_WEIGHTS_MAX        _POSIX2_COLL_WEIGHTS_MAX
#define EXPR_NEST_MAX           _POSIX2_EXPR_NEST_MAX

/* 4096, not the 2048 floor: src/unistd/sysconf.c already answers
 * _SC_LINE_MAX with 4096.  The header value being the compile-time
 * minimum while sysconf reports more is legal, but having the two
 * disagree for no reason is precisely the drift the ledger gate exists
 * to catch.  If one changes, change both. */
#define LINE_MAX                4096

/* 32767, not the 255 floor: src/regex/regex.c's DUP_MAX is 32767 and it
 * reports REG_BADBR past that, so 255 would publish a limit four
 * hundred times tighter than the one the code enforces.  If DUP_MAX
 * changes, change this with it. */
#define RE_DUP_MAX              32767

/* "Other Invariant Values" -- limits.h.html: "The <limits.h> header
 * shall define the following symbolic constants:".  Unconditional, and
 * unlike the Runtime Increasable group above these cannot grow at run
 * time: there is no sysconf() counterpart for any of them.  Each is set
 * to the standard's printed Minimum Acceptable Value, which is all this
 * library has grounds to claim. */
#define NL_LANGMAX              14      /* [XSI] */
#define NL_MSGMAX               32767
#define NL_SETMAX               255
#define NL_TEXTMAX              _POSIX2_LINE_MAX

/* NL_ARGMAX bounds n in a "%n$" conversion specification.  9 is the
 * standard's floor, and src/stdio/printf.c honours it exactly: an index
 * in [1,9] is served, one above it is refused with [EINVAL] rather than
 * read from somewhere the caller never wrote.  Raising this is a
 * one-constant change there -- the table is NL_ARGMAX entries of frame
 * -- but nothing asks for more, and the frame is paid by a formatter
 * every program in the tree goes through.
 *
 * The value was defined here for some time BEFORE printf implemented
 * %n$ at all, which was deliberate rather than an oversight, and the
 * reasoning is kept because it is the reasoning for every other
 * constant in this file: omitting it breaks a conforming consumer that
 * merely REFERENCES the constant -- sizing a buffer, a configure probe,
 * an #ifdef -- without ever writing %n$, which is a compile failure in
 * code doing nothing wrong, and this library exists to bootstrap
 * configure and friends.  Defining it could only mislead a consumer
 * that writes %n$, and such a consumer was already broken by printf
 * whatever this header said. */
#define NL_ARGMAX               9

/* [XSI].  Also defined by <sys/resource.h>, which needs it for the
 * nice() range; the two are the same token sequence, so the
 * redefinition is benign (C99 6.10.3p2) and both headers may be
 * included together.  Same arrangement as SEEK_SET across <stdio.h>,
 * <unistd.h> and <fcntl.h>.  If one moves, move both. */
#define NZERO                   20

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
