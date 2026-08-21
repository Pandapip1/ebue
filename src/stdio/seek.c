/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fseek/ftell and friends.  The FILE's own idea of its position is the
 * fd's (or memory block's) position adjusted by whatever is sitting in
 * the buffer: subtract unread look-ahead bytes when reading, add
 * unwritten ones when writing.
 */
#include <stdio.h>
#include "stdio_impl.h"

int fseeko(FILE *f, off_t off, int whence)
{
	long long r;
	if (f->wpos && __fflush_locked(f) < 0) return -1;
	if (whence == SEEK_CUR)
		off -= (off_t)(f->rend - f->rpos) + (off_t)f->nunget;
	f->rpos = f->rend = 0;
	f->nunget = 0;
	r = __file_seek(f, off, whence);
	if (r < 0) return -1;
	f->eof = 0;
	return 0;
}

int fseek(FILE *f, long off, int whence) { return fseeko(f, (off_t)off, whence); }

off_t ftello(FILE *f)
{
	long long pos = __file_seek(f, 0, SEEK_CUR);
	if (pos < 0) return -1;
	if (f->readable) pos -= (long long)(f->rend - f->rpos) + f->nunget;
	else pos += (long long)f->wpos;
	return (off_t)pos;
}

long ftell(FILE *f)
{
	off_t o = ftello(f);
	return (long)o;
}

void rewind(FILE *f)
{
	fseeko(f, 0, SEEK_SET);
	f->err = 0;
}

int fgetpos(FILE *__restrict f, fpos_t *__restrict pos)
{
	off_t o = ftello(f);
	if (o < 0) return -1;
	pos->__lldata = o;
	return 0;
}

int fsetpos(FILE *f, const fpos_t *pos)
{
	return fseeko(f, (off_t)pos->__lldata, SEEK_SET);
}
