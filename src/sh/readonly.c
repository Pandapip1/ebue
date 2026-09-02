/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The read-only *attribute* a name can be given by the `readonly`
 * special built-in (XCU 2.14, readonly(1p)), kept apart from its value.
 *
 * src/sh/builtin.c's bi_export() header comment explains why this
 * shell's only variable store is the real `environ`: every assignment,
 * exported or not, is already a setenv(). `readonly` needs something
 * environ cannot give it, though -- a name's *value* lives there, but
 * environ has no per-entry flag for "further assignment to this name
 * must fail", and no third-party code that reads environ (this file's
 * own callers included) may invent one by, say, reserving a sentinel
 * value or a shadow name; that would corrupt a real value the script
 * chose. So the read-only *set* -- which names are marked, independent
 * of whether they currently have a value -- gets the one array this
 * file owns, the same shape param.c uses for the same reason: state
 * with no home in environ gets a small dedicated store instead of a
 * bolted-on convention over it.
 *
 * There is no unmark: nothing in this shell's currently-implemented
 * subset ever needs one. `unset` (XCU 2.14) would, since unset(1p)
 * requires unsetting a read-only variable to fail loudly rather than
 * removing the mark, but `unset` is separately unimplemented
 * (src/sh/script.c's unimplemented_builtins) and stays out of scope
 * here -- see the readonly builtin's own header comment in builtin.c.
 * A mark, once made, is therefore permanent for the life of the
 * process, which is exactly readonly(1p)'s own contract: "Once a
 * variable is set to become read-only in this manner, it shall be an
 * error for it to appear as a name in a subsequent readonly [or
 * assignment] ... command."
 */
#include <string.h>
#include "libc.h"
#include "sh.h"

/* NUL-terminated names, __malloc'd; names[i] is never freed or
 * reordered once appended, since nothing here ever removes one. */
static char **names;
static size_t count;
static size_t cap;

static size_t find(const char *name)
{
	size_t i;
	for (i = 0; i < count; i++)
		if (strcmp(names[i], name) == 0) return i;
	return count;
}

int __sh_readonly_is(const char *name)
{
	return find(name) < count;
}

int __sh_readonly_mark(const char *name)
{
	size_t len;
	char *dup;

	if (find(name) < count) return 0;

	len = strlen(name) + 1;
	dup = __malloc(len);
	if (!dup) return -1;
	memcpy(dup, name, len);

	if (count == cap) {
		size_t ncap = cap ? cap * 2 : 8;
		char **nv = (char **)__malloc(ncap * sizeof *nv);
		if (!nv) { __free(dup); return -1; }
		memcpy((void *)nv, (const void *)names, count * sizeof *nv);
		__free((void *)names);
		names = nv;
		cap = ncap;
	}
	names[count++] = dup;
	return 0;
}

size_t __sh_readonly_count(void)
{
	return count;
}

/* i is 1:1 with a slot this file itself appended in __sh_readonly_mark()
 * above; every real caller (builtin.c's readonly listing) loops
 * `i < __sh_readonly_count()`, so an out-of-range i never happens in
 * practice. Returns NULL rather than asserting for the same reason
 * __sh_param_get() does (param.c): a defensive out-of-range answer, not
 * a contract callers are meant to rely on. */
const char *__sh_readonly_name(size_t i)
{
	return i < count ? names[i] : 0;
}
