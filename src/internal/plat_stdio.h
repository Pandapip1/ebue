/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform interface src/stdio/misc.c's renameat() calls into
 * instead of raw Nt{OpenFile,QueryInformationFile,SetInformationFile,
 * Close} calls.  See src/stdio/nt/plat_stdio.c for the implementation
 * these declare.
 *
 * `struct __ntpath` (src/internal/libc.h) is unavoidably part of this
 * interface's currency, the same way plat_mem.h's __plat_mem_map_file()
 * necessarily takes an off_t: it IS a resolved NT path (a UNICODE_STRING
 * plus OBJECT_ATTRIBUTES), used the same way by every other file-path
 * caller across this tree that this migration does not touch, so there
 * is no POSIX-shaped alternative to hand it as. What IS relocated, per
 * the general contract, is every actual NTSTATUS-level interpretation
 * step: STATUS_ACCESS_DENIED disambiguated into [ENOTEMPTY]/[EISDIR]
 * using types established before the rename was attempted (see
 * renameat()'s own comment on why that decision needs the real status
 * in hand, not a generic errno afterward), STATUS_NOT_SAME_DEVICE into
 * [EXDEV], and the FILE_RENAME_INFORMATION[Ex]-with-fallback dance
 * itself. Attribute values (ULONG FileAttributes/ReparseTag) are passed
 * as plain `unsigned long` rather than as ULONG, and isdir_attrs() --
 * the predicate that turns them into a boolean, shared verbatim with
 * src/stat/stat.c's own copy -- stays in the front door: it is a pure
 * POSIX-vs-NT-attribute mapping with no syscall in it at all.
 */
#ifndef _NTLIBC_PLAT_STDIO_H
#define _NTLIBC_PLAT_STDIO_H

#include <stddef.h>
#include "plat_handle.h"

struct __ntpath;

/* Open `old`'s already-resolved NT path (op) with DELETE|FILE_READ_
 * ATTRIBUTES|SYNCHRONIZE -- renameat()'s own handle for the eventual
 * FILE_RENAME_INFORMATION[Ex] set via __plat_rename_set() below, kept
 * open across both calls into this interface.  *attrs/*tag are
 * FileAttributeTagInformation's two fields, needed by renameat()'s own
 * isdir_attrs() to decide whether `old` is a directory; a failed query
 * (rather than a failed open) leaves them 0, exactly as the pre-
 * relocation `NT_SUCCESS(...) &&` short-circuit did -- 0 reads as "not
 * a reparse point, not a directory" either way, so this is not a
 * behavior change.  0/-1(errno) via return -- only the OPEN's own
 * failure is reported; the attribute query's failure is absorbed as
 * above. */
int __plat_rename_open_old(struct __ntpath *op, __plat_handle_t *h_out,
                           unsigned long *attrs, unsigned long *tag);

/* Best-effort probe of `new`'s NT path: does it exist, and if so, what
 * are its FileAttributeTagInformation fields?  *exists is always
 * written; *attrs/*tag only when *exists is set.  No handle is kept --
 * new is never opened again after this call, exactly as before -- and
 * no failure is reported outward: an unreadable `new` is exactly like a
 * nonexistent one for renameat()'s purposes. */
void __plat_query_new_attrs(struct __ntpath *np, int *exists,
                            unsigned long *attrs, unsigned long *tag);

/* Apply the rename: FILE_RENAME_INFORMATION[Ex] with FILE_RENAME_
 * REPLACE_IF_EXISTS | FILE_RENAME_POSIX_SEMANTICS tried first via the
 * Ex info class, falling back to the plain info class and flag on
 * [STATUS_INVALID_PARAMETER]/[STATUS_INVALID_INFO_CLASS]/
 * [STATUS_NOT_SUPPORTED]/[STATUS_NOT_IMPLEMENTED] (an NT that does not
 * know the newer class/flag).  `h` (from __plat_rename_open_old()) is
 * closed before this returns, on every path -- the caller never closes
 * it itself.  0 on success; on failure, [EXDEV] for
 * STATUS_NOT_SAME_DEVICE, [ENOTEMPTY]/[EISDIR] (chosen by `old_isdir`)
 * when NT's STATUS_ACCESS_DENIED is standing in for a directory-shaped
 * refusal rather than a real permission failure, and the generic
 * mapping for everything else. */
int __plat_rename_set(__plat_handle_t h, struct __ntpath *np, int old_isdir, int new_isdir);

#endif
