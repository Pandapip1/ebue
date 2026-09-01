/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * posix_fadvise(): POSIX (XBD posix_fadvise) says the function "shall
 * have no effect on the semantics of other operations on the specified
 * data, although it may affect the performance of other operations" --
 * every advice value is optional and purely advisory, so a conforming
 * implementation is free to do nothing but validate its arguments and
 * report success. NT has no per-handle readahead/cache-priority knob
 * this library reaches for elsewhere, so that is exactly what this
 * does: check the fd, the advice value and the length are all valid,
 * refuse a pipe or FIFO because the ERRORS section says to (the three
 * required error cases: EBADF, EINVAL, ESPIPE) and otherwise no-op.
 *
 * posix_fallocate() is a real implementation, not a no-op: POSIX
 * requires it to "ensure that any required storage for regular file
 * data starting at offset and continuing for len bytes is allocated on
 * the file system storage media" -- a guarantee, not a hint -- which
 * NtSetInformationFile(FileAllocationInformation) genuinely provides on
 * NTFS. If the requested range extends past the current end of file,
 * the end of file is also advanced (matching Linux's fallocate()
 * behaviour for the file size, though not the "may leave a sparse hole"
 * wording some systems allow -- FileAllocationInformation reserves real
 * clusters).
 */
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include "libc.h"
#include "plat_fcntl.h"

int posix_fadvise(int fd, off_t offset, off_t len, int advice) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct __fd *f = __fd_get(fd);
	/* offset carries no constraint of its own: posix_fadvise.html's
	 * [EINVAL] clause names advice and len only, and "the specified
	 * range need not currently exist in the file", so there is nothing
	 * here to check it against. */
	(void)offset;
	/* posix_fadvise() returns the error number directly, not -1/errno. */
	if (!f) return EBADF;
	/* posix_fadvise.html ERRORS, *shall fail*: "[EINVAL] The value of
	 * advice is invalid, or the value of len is less than zero."  One
	 * clause with two halves; the switch below is the advice half.
	 * Note len == 0 is not a degenerate length to be lumped in with
	 * the negatives -- the DESCRIPTION gives it a meaning of its own,
	 * "If len is zero, all data following offset is specified" -- so
	 * the test is strictly `< 0`. */
	if (len < 0) return EINVAL;
	switch (advice) {
	case POSIX_FADV_NORMAL: case POSIX_FADV_RANDOM: case POSIX_FADV_SEQUENTIAL:
	case POSIX_FADV_WILLNEED: case POSIX_FADV_DONTNEED: case POSIX_FADV_NOREUSE:
		break;
	default:
		return EINVAL;
	}
	/* posix_fadvise.html ERRORS, *shall fail*: "[ESPIPE] The fd
	 * argument is associated with a pipe or FIFO."  This function used
	 * to return 0 here for every descriptor that got past the two
	 * checks above, because it never looked at what the descriptor
	 * actually was -- and "no effect is a conforming effect" does not
	 * reach this clause: the page requires the call to *fail*, not
	 * merely to do nothing.
	 *
	 * __FD_PIPE is exactly the clause's set and no more.
	 * __handle_type() (src/internal/fd.c) assigns it to
	 * FILE_DEVICE_NAMED_PIPE -- which on NT is both an anonymous
	 * pipe() pair and a named FIFO, there being no separate device
	 * type for the two -- and to FILE_DEVICE_MAILSLOT; a regular file,
	 * directory, console, character device or socket each get a type
	 * of their own and cannot land here.  posix_fallocate() below
	 * spells its identically worded clause the same way.
	 *
	 * Placed last, after both halves of [EINVAL], deliberately.  POSIX
	 * orders none of these three against each other, so a pipe given
	 * with a bogus advice may conformingly yield either answer.  The
	 * rule chosen is the one posix_fallocate() already follows --
	 * validate the arguments the caller passed, then the object they
	 * name -- so the two functions in this file cannot be caught
	 * disagreeing about a descriptor that fails two clauses at once.
	 * [EBADF] first is the one ordering that is forced rather than
	 * chosen: f->type cannot be read until __fd_get() has produced an
	 * f to read it from. */
	if (f->type == __FD_PIPE) return ESPIPE;
	return 0;
}

/* The maximum file size of the volume a handle lives on, for
 * posix_fallocate()'s [EFBIG].
 *
 * posix_fallocate.html's clause is "[EFBIG] The value of offset+len is
 * greater than the maximum file size" -- a property of the VOLUME, not
 * of the operating system, which is why this is derived per volume
 * rather than being one constant for all of them.
 *
 * NT cannot be asked directly, and cannot be left to answer for itself.
 * Measured on this tree: ftruncate() to 32 TiB, 256 TiB, 8 PiB, 2^61
 * and 2^62 all fail identically with STATUS mapping to ENOMEM, and the
 * threshold tracks the volume's FREE SPACE (1 TiB succeeded), not any
 * file-size limit.  So NT reports "no room", never "too big for this
 * file system", and the bound has to be computed here.
 *
 * The rule is Microsoft's own.  BEWARE: two current Learn pages
 * disagree, and the wrong one is the more quotable.
 *
 *   - "File System Functionality Comparison", Limits table, gives
 *     "Maximum file size | NTFS | 2^64-1 bytes".  That is the on-disk
 *     field width, not what the implementation supports, and it is what
 *     someone will find if they go looking to "correct" this function.
 *   - "NTFS overview", Support for large volumes, gives the real rule:
 *     "The actual maximum volume and file size depends on the cluster
 *     size and the total number of clusters supported by NTFS (up to
 *     2^32-1 clusters)", with a table headed "Largest volume and file":
 *     4 KB cluster -> 16 TB, 64 KB -> 256 TB, 2048 KB (max) -> 8 PB.
 *
 * The second is used, because cluster_size * (2^32 - 1) reproduces that
 * table exactly: 4096 * 4294967295 = 17592186040320, i.e. the 16 TB row,
 * to the byte.  A derivation that independently reproduces the
 * authority's own published figure is sourced rather than guessed.
 *
 * ANY query failure yields LLONG_MAX, i.e. no limit of ours, and so does
 * a volume that reports a zero cluster size.  That is deliberate and it
 * is the whole fallback policy: an unrecognised volume must not be
 * treated as having a limit of zero.  It also means [EFBIG] is
 * BEST-EFFORT OFF NTFS -- FAT32's real 4 GiB ceiling is enforced by NT,
 * not by us, and the cluster formula does not describe it.  The
 * asymmetry justifies that: guessing too permissive is corrected by the
 * kernel, which refuses the operation itself; guessing too restrictive
 * is corrected by nobody, because this function would refuse before the
 * kernel is ever asked, on a request that would have worked.
 *
 * Under Wine the numbers describe the HOST file system rather than an
 * NTFS volume (ext4 reports a 4096 cluster, which happens to give the
 * same 16 TiB).  That is a divergence, but a harmless one: it can only
 * move the bound, and the fallback is permissive either way.
 *
 * The query itself and the derivation above now live in
 * __plat_volume_max_file_size() (src/internal/plat_fcntl.h) -- this
 * comment stays here, where the decision it documents is made. */

int posix_fallocate(int fd, off_t offset, off_t len) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct __fd *f = __fd_get(fd);
	long long want, alloc_size, eof;
	int grow_alloc;

	if (!f) return EBADF;
	if (offset < 0 || len < 0) return EINVAL;
	if (f->type == __FD_PIPE) return ESPIPE;
	/* posix_fallocate.html ERRORS, *shall fail*: "[EBADF] The fd argument
	 * references a file that was opened without write permission."  This
	 * function allocates storage and may extend the file, so a descriptor
	 * the caller cannot write through must be refused -- previously it
	 * was not checked at all, and posix_fallocate(rd, 0, 5) on a
	 * read-only descriptor with the range already inside the file
	 * returned 0, promising an allocation the caller can never use.
	 * (A range PAST the end happened to fail as well, but only as a side
	 * effect of NT refusing the FileEndOfFileInformation set on a
	 * read-only handle; that is environment-dependent and is not this
	 * clause.)  This is the same test src/unistd/write.c applies, and it
	 * is only correct because inherited descriptors now record their real
	 * access mode (src/internal/fd.c's accmode_of) -- before that, every
	 * descriptor inherited across a spawn read back as O_RDONLY and this
	 * check would have refused writable ones.
	 *
	 * Placed after the [ESPIPE] arm on purpose.  A pipe's read end
	 * satisfies both clauses at once and POSIX orders neither, but
	 * [ESPIPE] says the more useful thing about a pipe, and the existing
	 * assertion for it predates this check. */
	if ((f->flags & O_ACCMODE) == O_RDONLY) return EBADF;
	/* posix_fallocate.html ERRORS, *shall fail*: "[ENODEV] The fd argument
	 * does not refer to a regular file."  This used to be spelled
	 * `if (si.Directory) return EBADF;` -- a comment that named the right
	 * condition over code that returned the wrong errno for it, and that
	 * tested only one of the several ways a descriptor can fail to be a
	 * regular file.  [EBADF] on this page means two OTHER things ("is not
	 * a valid file descriptor", and "was opened without write
	 * permission"), so a caller that distinguishes them was told something
	 * false about its own descriptor.
	 *
	 * __FD_FILE is exactly the right predicate: __handle_type() assigns it
	 * to a disk/network/CD/tape file-system object that is NOT a directory
	 * (directories become __FD_DIR), so everything else -- a character
	 * device such as NUL, a console, a socket, a pipe -- is by
	 * construction not a regular file.  A pipe never reaches here because
	 * [ESPIPE] above is more specific and POSIX gives it its own clause. */
	if (f->type != __FD_FILE) return ENODEV;
	/* posix_fallocate.html ERRORS, *shall fail*: "[EFBIG] The value of
	 * offset+len is greater than the maximum file size."
	 *
	 * COMPARE BEFORE ADDING.  DO NOT "SIMPLIFY" THIS BACK TO
	 * `want = offset + len; if (want < 0) return EFBIG;`.  That is what
	 * this used to say, and it relies on the sum having ALREADY wrapped
	 * -- signed integer overflow, which is undefined in C, so a compiler
	 * is entitled to delete the test as unreachable, and no argument pair
	 * whose sum fits in a long long ever set it.  [EFBIG] was therefore
	 * unreachable by any defined execution, and UndefinedBehaviorSanitizer
	 * flagged the addition under `make asan`.  Both operands are known
	 * non-negative here (the [EINVAL] arm above rejects negatives), so
	 * this form cannot overflow and needs no wrap to work.
	 *
	 * The volume's real limit is only consulted for a request already too
	 * large to be plausible.  4 GiB is chosen because it is obviously
	 * below any limit a real volume reports -- it is FAT32's entire
	 * ceiling -- not because it is tuned to anything; the point is that
	 * the ordinary path issues no volume query at all and so cannot
	 * acquire a new failure mode from one. */
	if (offset > LLONG_MAX - len) return EFBIG;
	want = (long long)offset + (long long)len;
	/* RLIMIT_FSIZE (src/misc/resource.c): the process file-size limit is
	 * the other half of the same [EFBIG] clause as the volume maximum
	 * below -- posix_fallocate.html's "[EFBIG] The value of offset+len is
	 * greater than the maximum file size".  Like ftruncate, this cannot
	 * partially succeed, so it fails outright. */
	if (__fsize_allow(want) < 0) return EFBIG;
	if (want > 4LL * 1024 * 1024 * 1024 && want > __plat_volume_max_file_size(f->h))
		return EFBIG;

	if (__plat_file_extent(f->h, &alloc_size, &eof) < 0) return errno;

	/* The second half of this guard is a data-loss interlock, not an
	 * optimisation -- see src/internal/plat_fcntl.h's __plat_fallocate()
	 * and its implementation (src/fcntl/nt/plat_fcntl.c) for the full
	 * account of why AllocationSize and EndOfFile must be compared
	 * separately rather than just against `want`, including the sparse-
	 * file and Wine-hole cases that make the second conjunct load-
	 * bearing rather than redundant. */
	grow_alloc = want > alloc_size && want >= eof;
	return __plat_fallocate(f->h, want, eof, grow_alloc);
}
