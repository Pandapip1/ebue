/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fmemopen and open_memstream: a FILE with no fd at all, read and
 * written straight out of a block of memory.  __file_read/write/seek
 * in buf.c already know how to talk to one (that is what f->is_mem
 * means to them); all that is done here is setting one up.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include "stdio_impl.h"

FILE *fmemopen(void *__restrict buf, size_t size, const char *__restrict mode)
{
	int flags = __fmodeflags(mode);
	unsigned char *b = buf;
	int owned = 0;
	FILE *f;

	if (flags < 0 || size == 0) { errno = EINVAL; return 0; }
	if (!b) {
		b = malloc(size);
		if (!b) return 0;
		owned = 1;
		b[0] = 0;
	}
	f = malloc(sizeof *f);
	if (!f) { if (owned) free(b); return 0; }
	memset(f, 0, sizeof *f);
	f->fd = -1;
	f->pid = -1;
	f->is_mem = 1;
	f->mem_owned = (unsigned char)owned;
	f->mem_buf = b;
	f->mem_size = size;
	switch (flags & O_ACCMODE) {
	case O_RDONLY: f->readable = 1; f->mem_len = size; break;
	case O_WRONLY: f->writable = 1; f->mem_len = 0; break;
	default: f->readable = f->writable = 1; f->mem_len = mode[0] == 'r' ? size : 0; break;
	}
	if (mode[0] == 'a') {
		size_t l = 0;
		while (l < size && b[l]) l++;
		f->mem_len = l;
		f->mem_pos = l;
	}
	f->bufmode = _IOFBF;
	f->next = __stdio_files;
	__stdio_files = f;
	return f;
}

FILE *open_memstream(char **bufp, size_t *sizep)
{
	FILE *f;
	unsigned char *b;

	if (!bufp || !sizep) { errno = EINVAL; return 0; }
	b = malloc(1);
	if (!b) return 0;
	b[0] = 0;
	f = malloc(sizeof *f);
	if (!f) { free(b); return 0; }
	memset(f, 0, sizeof *f);
	f->fd = -1;
	f->pid = -1;
	f->is_mem = 1;
	f->mem_dynamic = 1;
	f->writable = 1;
	f->mem_buf = b;
	f->mem_size = 1;
	f->mem_out_ptr = bufp;
	f->mem_out_size = sizep;
	*bufp = (char *)b;
	*sizep = 0;
	f->bufmode = _IOFBF;
	f->next = __stdio_files;
	__stdio_files = f;
	return f;
}
