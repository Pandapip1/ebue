/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * FILE* lifetime: fopen/fdopen/freopen/fclose, the three standard
 * streams, and __stdio_exit, which exit() calls to flush and close
 * whatever is still open.  Every FILE that fopen/fdopen/fmemopen hands
 * out is linked into __stdio_files so __stdio_exit can find it without
 * the caller having to remember to.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- GNU feature-test macro has its specified reserved spelling
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "stdio_impl.h"

FILE *__stdio_files;

static unsigned char stdin_buf[BUFSIZ], stdout_buf[BUFSIZ];

static FILE stdin_f  = { .fd = 0, .bufmode = _IOFBF, .user_buf = 1, .readable = 1, .buf = stdin_buf,  .bufsz = sizeof stdin_buf,  .no_close = 1 }; // NOLINT(cert-fio38-c,misc-non-copyable-objects) -- the stdio implementation owns this backing object; no FILE is copied
static FILE stdout_f = { .fd = 1, .bufmode = _IOLBF, .user_buf = 1, .writable = 1, .buf = stdout_buf, .bufsz = sizeof stdout_buf, .no_close = 1 }; // NOLINT(cert-fio38-c,misc-non-copyable-objects) -- the stdio implementation owns this backing object; no FILE is copied
static FILE stderr_f = { .fd = 2, .bufmode = _IONBF, .writable = 1, .no_close = 1 }; // NOLINT(cert-fio38-c,misc-non-copyable-objects) -- the stdio implementation owns this backing object; no FILE is copied

FILE *const stdin = &stdin_f;
FILE *const stdout = &stdout_f;
FILE *const stderr = &stderr_f;

/* fopen's mode string turned into open()'s flags.  "b" is accepted and
 * ignored (everything here is binary already); "x" (C11) maps to O_EXCL. */
int __fmodeflags(const char *mode)
{
	int flags;
	switch (mode[0]) {
	case 'r': flags = O_RDONLY; break;
	case 'w': flags = O_WRONLY | O_CREAT | O_TRUNC; break;
	case 'a': flags = O_WRONLY | O_CREAT | O_APPEND; break;
	default: errno = EINVAL; return -1;
	}
	mode++;
	for (; *mode; mode++) {
		switch (*mode) {
		case '+': flags = (flags & ~O_ACCMODE) | O_RDWR; break;
		case 'x': flags |= O_EXCL; break;
		case 'b': case 't': break;
		case 'e': flags |= O_CLOEXEC; break;
		default: break;
		}
	}
	return flags;
}

FILE *__file_new(int fd, int flags) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	FILE *f = malloc(sizeof *f); // NOLINT(cert-fio38-c,misc-non-copyable-objects) -- the stdio implementation allocates its private FILE representation; it does not copy one
	if (!f) return 0;
	memset(f, 0, sizeof *f); // NOLINT(cert-fio38-c,misc-non-copyable-objects) -- initializes new private FILE storage before it becomes a stream; no live FILE is copied
	f->fd = fd;
	f->pid = -1;
	switch (flags & O_ACCMODE) { // NOLINT(bugprone-switch-missing-default-case) -- parsed stdio modes produce only the three valid access-mode encodings
	case O_RDONLY: f->readable = 1; break;
	case O_WRONLY: f->writable = 1; break;
	case O_RDWR: f->readable = f->writable = 1; break;
	}
	f->bufmode = isatty(fd) ? _IOLBF : _IOFBF;
	f->next = __stdio_files;
	__stdio_files = f;
	return f;
}

void __file_free(FILE *f)
{
	FILE **pp;
	for (pp = &__stdio_files; *pp; pp = &(*pp)->next) {
		if (*pp == f) { *pp = f->next; break; }
	}
	if (f->buf && !f->user_buf) free(f->buf);
	if (f->is_mem && f->mem_owned && f->mem_buf) free(f->mem_buf);
	free(f);
}

FILE *fopen(const char *__restrict path withtok(null_terminated),
	const char *__restrict mode withtok(null_terminated)) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	int flags = __fmodeflags(mode);
	int fd;
	FILE *f;
	if (flags < 0) return 0;
	fd = open(path, flags, 0666);
	if (fd < 0) return 0;
	f = __file_new(fd, flags);
	if (!f) { int e = errno; (void)close(fd); errno = e; return 0; }
	return f;
}

FILE *fdopen(int fd, const char *mode)
{
	int flags = __fmodeflags(mode);
	struct __fd *desc = __fd_get(fd);
	FILE *f;
	if (flags < 0) return 0;
	if (!desc) return 0;
	f = __file_new(fd, flags);
	if (!f) return 0;
	if ((flags & O_APPEND) && (flags & O_ACCMODE) != O_RDONLY) {
		/* fdopen(...,"a") establishes append mode even when the caller
		 * opened the descriptor without O_APPEND.  Keep the shared fd
		 * description in that mode so writes after an intervening seek
		 * still append, and seed the stream position from the current end
		 * so ftello() can include buffered, unflushed output. */
		desc->flags |= O_APPEND;
		if (fseek(f, 0, SEEK_END) < 0) {
			__file_free(f);
			return 0;
		}
	}
	return f;
}

FILE *freopen(const char *__restrict path, const char *__restrict mode, FILE *__restrict f) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	int flags = __fmodeflags(mode);
	int fd, oldfd;

	if (flags < 0) return 0;
	(void)fflush(f);
	oldfd = f->fd;

	if (path) {
		if (f->is_mem) {
			if (f->mem_dynamic && f->mem_buf) free(f->mem_buf);
			f->is_mem = 0; f->mem_buf = 0; f->mem_size = f->mem_len = f->mem_pos = 0;
		} else if (oldfd >= 0) {
			(void)close(oldfd);
		}
		fd = open(path, flags, 0666);
		if (fd < 0) { __file_free(f); return 0; }
		f->fd = fd;
	} else {
		/* Reopening the same file with a new mode: just re-derive flags.
		 * f->fd already is oldfd, so there is nothing to reassign. */
		if (oldfd < 0) { __file_free(f); return 0; }
	}

	f->readable = f->writable = 0;
	switch (flags & O_ACCMODE) { // NOLINT(bugprone-switch-missing-default-case) -- parsed stdio modes produce only the three valid access-mode encodings
	case O_RDONLY: f->readable = 1; break;
	case O_WRONLY: f->writable = 1; break;
	case O_RDWR: f->readable = f->writable = 1; break;
	}
	f->eof = f->err = 0;
	f->rpos = f->rend = f->wpos = 0;
	f->nunget = 0;
	f->wide = 0;
	memset(&f->wst_in, 0, sizeof f->wst_in);
	memset(&f->wst_out, 0, sizeof f->wst_out);
	f->wunget = 0;
	f->nwunget = 0;
	/* O_APPEND enforces append writes even when this best-effort positioning
	 * cannot seek (for example, on a pipe).  Such streams remain valid. */
	if (flags & O_APPEND)
		(void)fseek(f, 0, SEEK_END); // NOLINT(cert-err33-c) -- O_APPEND, not the current offset, guarantees append semantics
	return f;
}

int fclose(FILE *f)
{
	int r = fflush(f);
	if (!f->is_mem && !f->no_close && f->fd >= 0) {
		if (close(f->fd) < 0) r = EOF;
	}
	if (f->no_close) {
		/* stdin/stdout/stderr are never freed; just reset them. */
		f->rpos = f->rend = f->wpos = 0;
		f->nunget = 0;
		return r;
	}
	__file_free(f);
	return r;
}

int fileno(FILE *f)
{
	if (f->is_mem || f->fd < 0) { errno = EBADF; return -1; }
	return f->fd;
}

int feof(FILE *f) { return f->eof != 0; }
int ferror(FILE *f) { return f->err != 0; }
void clearerr(FILE *f) { f->eof = f->err = 0; }

/* flockfile/funlockfile: there is no threading here (libpthread.a is an
 * empty placeholder archive), so a FILE needs no real lock -- these exist
 * only so that programs written against a threaded libc still link. */
void flockfile(FILE *f) { (void)f; }
int ftrylockfile(FILE *f) { (void)f; return 0; }
void funlockfile(FILE *f) { (void)f; }

/* exit() calls this to flush and close everything still open, the way a
 * real process shutdown (or _exit after a clean run) is expected to
 * leave nothing buffered unwritten.
 *
 * WHY THE RE-ENTRANCY GUARD.  This is not defensive programming; it
 * closes a measured infinite recursion that ended in a fault.  A flush
 * here can itself raise a signal whose default action is to terminate,
 * and the terminate path comes straight back through this function:
 *
 *     fflush(f) -> write() -> STATUS_PIPE_BROKEN
 *               -> __raise_internal(SIGPIPE)          [src/unistd/write.c]
 *               -> default action is terminate        [src/signal/signal.c]
 *               -> __stdio_exit()                     [here]
 *               -> fflush(f) ...
 *
 * and the second pass re-flushes the SAME stream, because the buffer it
 * is trying to drain was never drained -- the write that would have
 * emptied it did not return.  Measured on a pipe whose read end had
 * closed: roughly 1 MB of stack consumed, then EXCEPTION_STACK_OVERFLOW
 * (0xC00000FD).  Worse than the fault itself, the process then died
 * reporting exit code 0 -- the overflow destroys the
 * __ENCODE_SIGNAL_EXIT(SIGPIPE) that should have been reported -- so a
 * crashed program looked to its parent like a successful one.  (A plain
 * unbuffered SIGPIPE death from the same library exits 13, correctly.)
 *
 * The guard is deliberately on the WHOLE function rather than per-FILE.
 * Once a fatal signal is being delivered the process is ending; flushing
 * what we can once and then getting out of the way is the whole
 * contract, and a second entry has, by construction, nothing new to
 * flush that the first entry is not already inside of.  A per-FILE
 * "flush in progress" flag would let the remaining streams still be
 * flushed after a broken one, which is strictly more output but also a
 * second place for this same cycle to hide; if that is ever wanted it
 * should come with its own test for the two-broken-pipes case.
 *
 * Note this leaves the streams AFTER the offending one unflushed on the
 * signal path.  That is a deliberate loss and not a regression in
 * behaviour anyone could have relied on: the alternative on that path
 * was a stack overflow, which flushed nothing at all.  It is also closer
 * to POSIX, where death by an unhandled signal does not flush stdio.
 *
 * The vectored exception handler calls this too (src/signal/signal.c),
 * which is how the stack-overflow handler used to re-enter the same
 * cycle with no stack left; the guard covers that call site as well. */
void __stdio_exit(void)
{
	static int in_progress;
	FILE *f;

	if (in_progress) return;
	in_progress = 1;

	(void)fflush(stdout);
	(void)fflush(stderr);
	for (f = __stdio_files; f; f = f->next)
		(void)fflush(f);
	/* Buffers are not freed and fds not closed: the process is about to
	 * end and NtTerminateProcess reclaims everything at once. Flushing
	 * is the only observable effect that matters. */

	/* Deliberately NOT cleared.  There is no "after" for this function:
	 * every caller is on its way to __exit_internal().  Clearing it would only
	 * re-arm the recursion for a second fatal signal arriving during the
	 * same shutdown. */
}

// NOLINTEND(misc-include-cleaner)
