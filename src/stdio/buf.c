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
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "stdio_impl.h"

ssize_t __file_read(FILE *f, void *buf, size_t n)
{
	if (f->is_mem) {
		size_t avail = f->mem_pos < f->mem_len ? f->mem_len - f->mem_pos : 0;
		if (n > avail) n = avail;
		if (n) memcpy(buf, f->mem_buf + f->mem_pos, n);
		f->mem_pos += n;
		return (ssize_t)n;
	}
	if (f->fd < 0) { errno = EBADF; return -1; }
	return read(f->fd, buf, n);
}

ssize_t __file_write(FILE *f, const void *buf, size_t n)
{
	if (f->is_mem) {
		size_t avail;
		if (f->mem_dynamic && f->mem_pos + n + 1 > f->mem_size) {
			size_t ns = f->mem_size ? f->mem_size : 128;
			while (ns < f->mem_pos + n + 1) ns *= 2;
			unsigned char *nb = realloc(f->mem_buf, ns);
			if (!nb) { errno = ENOMEM; return -1; }
			f->mem_buf = nb;
			f->mem_size = ns;
		}
		avail = f->mem_pos < f->mem_size ? f->mem_size - f->mem_pos : 0;
		if (n > avail) n = avail;   /* fmemopen: silently truncate, like a full device */
		if (n) memcpy(f->mem_buf + f->mem_pos, buf, n);
		f->mem_pos += n;
		if (f->mem_pos > f->mem_len) f->mem_len = f->mem_pos;
		if (f->mem_len < f->mem_size) f->mem_buf[f->mem_len] = 0;
		if (f->mem_dynamic) {
			if (f->mem_out_ptr) *f->mem_out_ptr = (char *)f->mem_buf;
			if (f->mem_out_size) *f->mem_out_size = f->mem_len;
		}
		return (ssize_t)n;
	}
	if (f->fd < 0) { errno = EBADF; return -1; }
	return write(f->fd, buf, n);
}

long long __file_seek(FILE *f, long long off, int whence)
{
	if (f->is_mem) {
		long long base;
		switch (whence) {
		case SEEK_SET: base = 0; break;
		case SEEK_CUR: base = (long long)f->mem_pos; break;
		case SEEK_END: base = (long long)f->mem_len; break;
		default: errno = EINVAL; return -1;
		}
		if (base + off < 0) { errno = EINVAL; return -1; }
		f->mem_pos = (size_t)(base + off);
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
	if (!f->writable || !f->wpos) { f->wpos = 0; return 0; }
	while (off < f->wpos) {
		ssize_t n = __file_write(f, f->buf + off, f->wpos - off);
		if (n <= 0) { f->err = 1; f->wpos = 0; return -1; }
		off += (size_t)n;
	}
	f->wpos = 0;
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
		if (__fflush_locked(stdout) < 0) r = EOF;
		for (p = __stdio_files; p; p = p->next)
			if (__fflush_locked(p) < 0) r = EOF;
		return r;
	}
	return __fflush_locked(f) < 0 ? EOF : 0;
}
int fflush_unlocked(FILE *f) { return fflush(f); }

int setvbuf(FILE *__restrict f, char *__restrict buf, int mode, size_t size)
{
	fflush(f);
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
	setvbuf(f, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

void setbuffer(FILE *f, char *buf, size_t size)
{
	setvbuf(f, buf, buf ? _IOFBF : _IONBF, size);
}

void setlinebuf(FILE *f)
{
	setvbuf(f, 0, _IOLBF, 0);
}
