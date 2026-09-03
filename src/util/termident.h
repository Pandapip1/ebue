/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Terminal identification shared, verbatim, between mesg(1p) and
 * write(1p) (src/util/mesg.c, src/util/util_write.c): both need to
 * answer "which of my own descriptors is really a terminal, and what,
 * if anything, can this library reach to gate or write messages to
 * it?" -- the same question, asked by two different utilities, so it
 * lives here once rather than being copied (the bar src/util/tablist.h
 * and src/util/modeparse.h both already document for a header of their
 * own instead of src/internal/util.h's flat function list).
 *
 * What "really a terminal" means here, concretely, differs by
 * platform, and neither half is a fabrication:
 *
 *  - NT: isatty()'s __FD_CONSOLE gate (src/unistd/isatty.c) is the
 *    real, correct answer -- but a console has no filesystem path this
 *    library can chmod(): src/stat/chmod.c's fchmodat() returns EROFS
 *    off the synthetic /dev/console object (src/internal/vfs.c's
 *    __vfs_stat() -- a fixed S_IFCHR|0666 with no writable backing
 *    store at all), and fchmod() on a non-regular-file/directory fd is
 *    a silent success no-op (src/stat/chmod.c:21) that touches
 *    nothing. So an NT console is real and identifiable, but this
 *    library has no real permission bit to read back or flip for it --
 *    see mesg.c's own header comment for what that honestly means for
 *    its y/n state.  `opaque` is set for this case.
 *
 *  - Linux: a real tty (a pty slave, most commonly) is a real
 *    character-device node with a real path, and this library's own
 *    chmod()/fchmodat() are real syscalls against it
 *    (src/stat/linux/plat_stat.c). isatty() (src/unistd/linux/
 *    plat_isatty.c) is real here too -- a live tcgetattr() probe, not
 *    the static __FD_CONSOLE classification NT uses (classify_fd()
 *    still never assigns __FD_CONSOLE to anything on Linux; the live
 *    probe is how isatty() answers correctly without it) -- but it is
 *    deliberately not this file's primary test, because it answers a
 *    different, WIDER question than "is this a nameable device I can
 *    chmod() or write(1p) a message into": a Unix98 pty MASTER fd
 *    (/dev/ptmx) passes the identical live ioctl(TCGETS) probe a real
 *    slave does (confirmed live: opening one with posix_openpt() and
 *    calling isatty() on the master fd itself returns 1, same as the
 *    slave), yet /proc/self/fd resolves it to the literal shared
 *    "/dev/ptmx" multiplexer node, not a per-session device this file
 *    could meaningfully chmod() or deliver a write(1p) message into.
 *    So this file finds a real Linux tty its own, narrower way: fstat()
 *    + S_ISCHR(), then resolve the actual device node path via
 *    readlink("/proc/self/fd/<fd>") -- procfs is the Linux kernel's
 *    own, not something ntlibc provides (src/internal/vfs.c's own
 *    banner: Linux "has real native devices and a real native root, so
 *    it needs no overlay" -- every path here is native, not synthetic)
 *    -- and only accepts the result if the resolved path is actually
 *    shaped like a real tty (src/util/termident.c's own
 *    path_looks_like_tty()), which is exactly what filters the ptmx
 *    master case above out. isatty() is still consulted, as a fallback
 *    for a Linux fd this path-based route could not resolve (no procfs
 *    mounted) and unconditionally on NT (no filesystem path a console
 *    fd could resolve to at all) -- see src/util/termident.c's own
 *    describe_fd() for exactly where each check applies.
 */
#ifndef _NTLIBC_UTIL_TERMIDENT_H
#define _NTLIBC_UTIL_TERMIDENT_H

/* path/shortname are always NUL-terminated once __util_find_terminal()
 * returns >= 0.  shortname is the "who"/write(1p)-style short form
 * (the real path with a leading "/dev/" stripped, or ttyname()'s fixed
 * "CON" for an opaque NT console) that write(1p)'s tty operand and
 * banner both compare against / print. */
struct term_ident {
	char path[1024];
	char shortname[64];
	int opaque; /* 1: a real terminal with no writable permission-bit
	             * backing this library can reach (NT); 0: `path` is a
	             * real device node this library resolved for real
	             * (Linux). */
};

/* Searches fd 0, then 1, then 2 -- mesg(1p)/write(1p)'s own shared rule
 * (each utility's own DESCRIPTION: the affected terminal is identified
 * by successively checking standard input, then output, then error)
 * -- for the first one that is really a terminal, and fills *out
 * describing it.  Returns the fd found (0, 1, or 2), or -1 (out
 * untouched) if none of the three is a terminal at all. */
int __util_find_terminal(struct term_ident *out) __attribute__((nonnull(1)));

#endif
