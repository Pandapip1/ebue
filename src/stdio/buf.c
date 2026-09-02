/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The buffering layer: one buffer per FILE, used for reading ahead or
 * for collecting bytes to write, never both at once.  Switching
 * direction flushes a pending write, or seeks back over bytes read
 * ahead but not yet consumed by the caller -- the same trick fseek
 * uses, since "the caller asked to write now" is exactly the situation
 * fseek normally handles.
 *
 * __file_read/__file_write/__file_seek are the one place a fd and a
 * fmemopen/open_memstream block look the same: everything above this
 * file only ever touches a FILE through them.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- GNU feature-test macro has its specified reserved spelling
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include "ownership_stubs.h"
#include "stdio_impl.h"

ssize_t __file_read(FILE *f, void *buf, size_t n)
{
	if (f->is_mem) {
		size_t avail = f->mem_pos < f->mem_len ? f->mem_len - f->mem_pos : 0;
		if (n > avail) n = avail;
		if (n) {
			const unsigned char *src = f->mem_buf + f->mem_pos;
			__ownership_readable_span(src, n);
			memmove(buf, src, n);
		}
		f->mem_pos += n;
		return (ssize_t)n;
	}
	if (f->fd < 0) { errno = EBADF; return -1; }
	return read(f->fd, buf, n);
}

/* Publish an open_memstream()/open_wmemstream() buffer to the caller's
 * pointer and size.
 *
 * open_memstream.html: "the pointer ... and the size ... shall be
 * updated" after a successful fflush() or fclose(), and *sizep is "the
 * smaller of the current buffer length and the number of characters
 * between the beginning of the buffer and the current file position
 * indicator".
 *
 * BOTH HALVES OF THAT WERE WRONG.  The size stored was mem_len alone --
 * the high-water length -- so after seeking BACKWARD *sizep kept
 * describing bytes past the position, and it was only ever written from
 * the write path, so an fflush() that followed a seek published nothing
 * at all.  Measured against glibc: write "hello world", seek to 5,
 * fflush -> glibc reports 5, this reported 11.
 *
 * open_wmemstream.html: sizep is "the number of wide characters", so the
 * byte count is divided down.  The minimum is taken FIRST and divided
 * after, because both operands are byte counts; dividing first would
 * round each independently. */
static void mem_publish(FILE *f)
{
	size_t n;

	if (!f->mem_dynamic) return;
	if (f->mem_out_ptr) *f->mem_out_ptr = (char *)f->mem_buf;
	if (!f->mem_out_size) return;
	n = f->mem_pos < f->mem_len ? f->mem_pos : f->mem_len;
	*f->mem_out_size = f->wmem ? n / sizeof(wchar_t) : n;
}

ssize_t __file_write(FILE *f, const void *buf, size_t n)
{
	if (f->is_mem) {
		size_t avail;
		size_t need;
		/* The terminator this block keeps past mem_len is one null
		 * BYTE for a byte stream and one null WIDE CHARACTER for an
		 * open_wmemstream() one, which is why it is a width and not a
		 * literal 1: mem_* are byte counts in both cases (so the growth
		 * arithmetic here is shared), and only the terminator and the
		 * reported size differ. */
		size_t term = f->wmem ? sizeof(wchar_t) : 1;
		/* fmemopen.html, append modes: the position is reset to the end
		 * of the contents before each write, so a write goes there and
		 * not to wherever the caller last seeked.  Without this,
		 * fmemopen(buf, 10, "a+") on "hello" followed by a seek to 0
		 * and a 3-byte write produced "h104o" where "hello104" is
		 * required -- the write landed at the seek position and
		 * overwrote live content. */
		if (f->mem_append) f->mem_pos = f->mem_len;
		if (f->mem_dynamic) {
			if (f->mem_pos > (size_t)LLONG_MAX ||
			    n > (size_t)LLONG_MAX - f->mem_pos) {
				errno = EOVERFLOW;
				return -1;
			}
			need = f->mem_pos + n;
			if (need > SIZE_MAX - term) {
				errno = EOVERFLOW;
				return -1;
			}
			need += term;
			/* ntlibc's usable object-size range ends at PTRDIFF_MAX: offsets
			 * and differences within an allocated object must remain in the
			 * ABI's signed pointer-difference type.  Reject a larger request
			 * before the geometric growth itself crosses the size_t boundary. */
			if (need > (size_t)PTRDIFF_MAX) {
				errno = ENOMEM;
				return -1;
			}
			if (need > f->mem_size) {
				size_t ns = f->mem_size ? f->mem_size : 128;
				while (ns < need) {
					if (ns > (size_t)PTRDIFF_MAX / 2) { ns = need; break; }
					ns *= 2;
				}
				{
					unsigned char *nb = realloc(f->mem_buf, ns);
					if (!nb) { errno = ENOMEM; return -1; }
					f->mem_buf = nb;
					f->mem_size = ns;
				}
			}
		}
		avail = f->mem_pos < f->mem_size ? f->mem_size - f->mem_pos : 0;
		if (n > avail) n = avail;   /* fmemopen: silently truncate, like a full device */
		if (n) {
			unsigned char *dst = f->mem_buf + f->mem_pos;
			__ownership_writable_span(dst, n);
			__ownership_readable_span(buf, n);
			memmove(dst, buf, n);
		}
		f->mem_pos += n;
		if (f->mem_pos > f->mem_len) f->mem_len = f->mem_pos;
		if (f->mem_len <= f->mem_size && term <= f->mem_size - f->mem_len) {
			unsigned char *term_dst = f->mem_buf + f->mem_len;
			memset(term_dst, 0, term);
		}
		mem_publish(f);
		return (ssize_t)n;
	}
	if (f->fd < 0) { errno = EBADF; return -1; }
	__ownership_readable_span(buf, n);
	return write(f->fd, buf, n);
}

long long __file_seek(FILE *f, long long off, int whence)
{
	if (f->is_mem) {
		long long base, pos;
		switch (whence) {
		case SEEK_SET: base = 0; break;
		case SEEK_CUR:
			if (f->mem_pos > (size_t)LLONG_MAX) { errno = EOVERFLOW; return -1; }
			base = (long long)f->mem_pos;
			break;
		case SEEK_END:
			if (f->mem_len > (size_t)LLONG_MAX) { errno = EOVERFLOW; return -1; }
			base = (long long)f->mem_len;
			break;
		default: errno = EINVAL; return -1;
		}
		if ((off > 0 && base > LLONG_MAX - off) ||
		    (off < 0 && base < LLONG_MIN - off)) {
			errno = EOVERFLOW;
			return -1;
		}
		pos = base + off;
		if (pos < 0) { errno = EINVAL; return -1; }
		/* fmemopen.html: "An attempt to seek a memory buffer stream to
		 * a negative position or to a position larger than the buffer
		 * size shall fail."  A FIXED buffer has a size to exceed; an
		 * open_memstream() buffer grows on demand, so only the negative
		 * half of that clause applies to it and mem_dynamic is exempt
		 * here.  Without this an fmemopen(buf, 10, "r+") stream would
		 * seek to 11 and report ftell() == 11 for a ten-byte buffer. */
		if (!f->mem_dynamic && (unsigned long long)pos > f->mem_size) {
			errno = EINVAL;
			return -1;
		}
		f->mem_pos = (size_t)pos;
		return (long long)f->mem_pos;
	}
	if (f->fd < 0) { errno = EBADF; return -1; }
	return lseek(f->fd, off, whence);
}

void __ensure_buf(FILE *f)
{
	size_t sz;
	if (f->buf) return;
	sz = f->bufmode == _IONBF ? 1 : (f->bufsz ? f->bufsz : BUFSIZ);
	f->buf = malloc(sz);
	if (f->buf) { f->bufsz = sz; f->user_buf = 0; }
	else f->bufsz = 0;
}

int __fflush_locked(FILE *f)
{
	size_t off = 0;

	if (f->writable && f->wpos) {
		size_t end = f->wpos;
		while (off < end) {
			ssize_t n = __file_write(f, f->buf + off, end - off);
			if (n <= 0) { f->err = 1; f->wpos = 0; return -1; }
			if ((size_t)n > end - off) { errno = EIO; f->err = 1; f->wpos = 0; return -1; }
			off += (size_t)n;
		}
	}
	f->wpos = 0;

	/* fflush.html DESCRIPTION: for a stream open for reading, "the file
	 * offset of the underlying open file description shall be set to
	 * the file position of the stream, and any [ungetc()] characters
	 * ... that have not subsequently been read from the stream shall be
	 * discarded (without further changing the file offset)." __fill()
	 * may have read ahead of where the caller has actually consumed, so
	 * the fd has to be seeked back by exactly that much. A memory-backed
	 * stream (fmemopen/open_memstream) has no separate fd offset -- its
	 * "position" is mem_pos, which read-ahead never moves apart from the
	 * stream's own view -- so there is nothing to resync there. */
	if (f->readable) {
		long long ahead = (long long)(f->rend - f->rpos);
		if (ahead && !f->is_mem) {
			if (__file_seek(f, -ahead, SEEK_CUR) < 0) {
				/* A non-seekable stream has no underlying offset to
				 * resynchronise.  Its buffered input remains live. */
				if (errno == ESPIPE) {
					mem_publish(f);
					return 0;
				}
				f->err = 1; return -1;
			}
		}
		f->nunget = 0;
		f->nwunget = 0;
		f->rpos = f->rend = 0;
	}
	/* "shall be updated ... after a successful fflush() or fclose()" --
	 * and a seek since the last write may have moved the position, which
	 * is what *sizep is measured against, so this cannot be left to the
	 * write path alone. */
	mem_publish(f);
	return 0;
}

int __toread(FILE *f)
{
	if (f->wpos) return __fflush_locked(f);
	return 0;
}

int __towrite(FILE *f)
{
	if (f->rpos < f->rend || f->nunget) {
		long long unread = (long long)(f->rend - f->rpos) + f->nunget;
		f->rpos = f->rend = 0;
		f->nunget = 0;
		f->nwunget = 0;
		if (__file_seek(f, -unread, SEEK_CUR) < 0) { f->err = 1; return -1; }
	}
	return 0;
}

int __fill(FILE *f)
{
	ssize_t n;
	__ensure_buf(f);
	f->rpos = f->rend = 0;
	if (!f->buf) { f->err = 1; return -1; }
	n = __file_read(f, f->buf, f->bufsz);
	if (n < 0) { f->err = 1; return -1; }
	if (n == 0) { f->eof = 1; return 0; }
	f->rend = (size_t)n;
	return (int)n;
}

int fflush(FILE *f)
{
	if (!f) {
		FILE *p;
		int r = 0;
		if (__fflush_locked(stdin) < 0) r = EOF;
		if (__fflush_locked(stdout) < 0) r = EOF;
		if (__fflush_locked(stderr) < 0) r = EOF;
		for (p = __stdio_files; p; p = p->next)
			if (__fflush_locked(p) < 0) r = EOF;
		return r;
	}
	return __fflush_locked(f) < 0 ? EOF : 0;
}

int setvbuf(FILE *__restrict f, char *__restrict buf, int mode, size_t size) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	/* setvbuf.html RETURN VALUE: "Otherwise, it shall return a non-zero
	 * value if an invalid value is given for type ...".  _IOLBF is a
	 * real mode here, not a synonym for full buffering -- rw.c flushes
	 * on '\n' when bufmode == _IOLBF -- so all three of _IOFBF/_IOLBF/
	 * _IONBF are genuinely honored and anything else is rejected. */
	if (mode != _IOFBF && mode != _IOLBF && mode != _IONBF) {
		errno = EINVAL;
		return -1;
	}
	if (fflush(f) < 0) return -1;
	if (f->buf && !f->user_buf) free(f->buf);
	f->buf = 0;
	f->bufsz = 0;
	f->user_buf = 0;
	f->rpos = f->rend = f->wpos = 0;
	f->bufmode = (unsigned char)mode;
	if (mode == _IONBF) return 0;
	if (buf) {
		f->buf = (unsigned char *)buf;
		f->bufsz = size ? size : BUFSIZ;
		f->user_buf = 1;
		return 0;
	}
	/* No buffer given: remember the size and allocate on first use. */
	f->bufsz = size ? size : BUFSIZ;
	return 0;
}

void setbuf(FILE *__restrict f, char *__restrict buf)
{
	/* ISO C gives this void wrapper no channel for setvbuf() failure. */
	(void)setvbuf(f, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

void setlinebuf(FILE *f)
{
	/* BSD's void wrapper likewise cannot propagate setvbuf() failure. */
	(void)setvbuf(f, 0, _IOLBF, 0);
}

// NOLINTEND(misc-include-cleaner)
