/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Turning the paths programs use into the paths the object manager wants.
 *
 * A program hands in UTF-8 with either kind of slash, relative or
 * absolute, possibly with a drive letter and possibly not; ntdll wants
 * UTF-16 in the \??\C:\... form, inside a UNICODE_STRING, inside an
 * OBJECT_ATTRIBUTES.  RtlDosPathNameToNtPathName_U does the hard part
 * (resolving relative paths against the current directory, . and ..,
 * per-drive current directories, UNC names); this file does the rest.
 *
 * A rooted path with no drive ("/usr/bin/sh") is taken relative to the
 * root of the current drive, which is the same thing Windows itself does
 * with "\usr\bin\sh".  "/dev/null" is the one Unix device name given a
 * meaning: it becomes NUL.  Everything else is passed through.
 */
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include "libc.h"

/* XBD <limits.h> {NAME_MAX}: "Maximum number of bytes in a filename
 * (not including the terminating null of a string)."  BYTES, not
 * characters, and that distinction is the whole subtlety here.
 *
 * fchmodat.html ERRORS, shall fail -- and the identical clause is
 * boilerplate on open, openat, stat, fstatat, access, faccessat, unlink,
 * unlinkat, mkdir, mkdirat, link, linkat, symlink, symlinkat, rename,
 * renameat, chmod, chdir, utimensat, opendir, ... : "[ENAMETOOLONG] The
 * length of a component of a pathname is longer than {NAME_MAX}."
 *
 * THIS IS NOT THE [ENAMETOOLONG] THIS LIBRARY ALREADY HAD.  Those same
 * pages list a SECOND, MAY-FAIL [ENAMETOOLONG] about the length of the
 * whole pathname, and that is the one __ntpath()/__ntpath_at()/chdir()
 * have always reported, as the __US_MAX_WCHARS bound -- a name a
 * UNICODE_STRING cannot describe.  The two are easy to conflate and the
 * difference matters: that bound is ~32k code units, so it says nothing
 * whatever about a 300-byte component sitting inside a short path.  This
 * function implements the shall-fail per-component clause; the
 * whole-path bound stays where it was.
 *
 * It lives here, in the one place __ntpath() and __ntpath_at()'s
 * relative branch both route through, rather than in any caller.  The
 * clause is on every path-taking interface in this library, not on
 * fchmodat() -- fchmodat is merely where it was noticed.
 *
 * WHAT THIS CHANGES, MEASURED RATHER THAN REASONED.  NTFS bounds a
 * component at 255 UTF-16 CODE UNITS; {NAME_MAX} bounds it at 255
 * BYTES.  On ASCII the two agree and nothing moves: measured under Wine
 * before this check, a 255-byte component opened and a 256-byte one
 * failed -- with [ENOENT], which is the bug, NT having answered about a
 * name it could not form.  They part company on multi-byte UTF-8: 100
 * CJK characters are 300 bytes but only 100 code units, and before this
 * commit open(), openat(), mkdir() and the rest CREATED such a name
 * successfully.  They now refuse it.
 *
 * That is deliberate, it is what POSIX asks for, and it is what glibc
 * does -- measured on ext4, whose own limit is likewise 255 bytes, where
 * the same 300-byte name fails with [ENAMETOOLONG] through open, openat
 * and chmod alike.  The cost is named here so nobody has to rediscover
 * it: a long non-ASCII filename NTFS would have accepted is no longer
 * reachable through this library.
 *
 * Zero-length pieces -- a doubled separator, the empty piece before a
 * leading slash -- are not components and are not measured.  Both
 * separators are recognised because dos_from_posix() has not yet run
 * when a caller's path reaches here and either may be present. */
int __name_too_long(const char *path)
{
	const char *p = path;

	while (*p) {
		const char *start = p;
		while (*p && *p != '/' && *p != '\\') p++;
		if ((size_t)(p - start) > NAME_MAX) return 1;
		if (*p) p++;
	}
	return 0;
}

static WCHAR *dos_from_posix(const char *path, size_t *wlen, int *trailing)
{
	WCHAR *w;
	size_t i, n;

	if (__name_too_long(path)) { errno = ENAMETOOLONG; return 0; }
	/* The two emulated /dev nodes are terminal device objects, not
	 * directories.  Reject a child path before the exact-name mapping
	 * below; otherwise it is treated as an ordinary missing /dev tree and
	 * incorrectly reported as ENOENT. */
	if ((!strncmp(path, "/dev/null", 9) && (path[9] == '/' || path[9] == '\\'))
	 || (!strncmp(path, "/dev/tty", 8) && (path[8] == '/' || path[8] == '\\'))) {
		errno = ENOTDIR;
		return 0;
	}
	if (!strcmp(path, "/dev/null")) path = "NUL";
	else if (!strcmp(path, "/dev/tty")) path = "CON";
	else if (!strncmp(path, "/dev/", 5)) {
		/* /dev/stdin etc. are handled by open() via the fd table; anything
		 * else under /dev has no Windows counterpart. */
	}
	w = __utf8_to_utf16(path, &n);
	if (!w) return 0;
	for (i = 0; i < n; i++)
		if (w[i] == '/') w[i] = '\\';
	/* Strip a trailing slash: "dir/" must mean "dir" to NtCreateFile --
	 * but remember it was there ("root" paths like "/" or "C:\" do not
	 * count: they can only ever name a directory) so the caller can
	 * still reject the name if what it resolves to is not one. */
	if (trailing) *trailing = n > 1 && w[n-1] == '\\' && !(n == 3 && w[1] == ':');
	while (n > 1 && w[n-1] == '\\' && !(n == 3 && w[1] == ':')) w[--n] = 0;
	if (wlen) *wlen = n;
	return w;
}

/* access.html ERRORS ENOTDIR / open.html DESCRIPTION: a trailing slash
 * requires the resolved name to be a directory.  The slash itself is
 * already stripped from *out (NtCreateFile does not accept one), so this
 * re-checks the object type with a handle-less attribute query.  A name
 * that does not exist yet, or that a query cannot be answered for some
 * other reason, is left to whatever real operation the caller goes on to
 * do -- this only rejects a trailing slash on something that positively
 * exists and is not a directory. */
static int reject_if_not_dir(struct __ntpath *out)
{
	FILE_BASIC_INFORMATION bi;
	NTSTATUS st = NtQueryAttributesFile(&out->oa, &bi);
	if (NT_SUCCESS(st) && !(bi.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
		__ntpath_free(out);
		errno = ENOTDIR;
		return -1;
	}
	return 0;
}

/* Where __nt_prefix_not_dir() may start truncating an NT path.  A
 * drive path ("\??\C:\...") may lose everything below "\??\C:\"; a name
 * that is not of that shape -- "\??\NUL", a UNC name ("\??\UNC\server\
 * share\..."), whose leading components are not files at all -- has no
 * prefix this can say anything about, and is reported as having none. */
static size_t nt_prefix_root(const UNICODE_STRING *nt)
{
	const WCHAR *b = nt->Buffer;
	size_t n = nt->Length / sizeof(WCHAR);

	if (n < 7 || b[0] != '\\' || b[1] != '?' || b[2] != '?' || b[3] != '\\') return n;
	if (!(((b[4] | 0x20) >= 'a' && (b[4] | 0x20) <= 'z') && b[5] == ':' && b[6] == '\\')) return n;
	return 7;
}

/* open.html (and stat, access, unlink, mkdir, utime, ... -- the clause is
 * boilerplate across the file-system surface) ERRORS [ENOTDIR]: "A
 * component of the path prefix names an existing file that is neither a
 * directory nor a symbolic link to a directory."
 *
 * NT gives no way to tell that apart from a prefix that simply is not
 * there: the object manager answers both with
 * STATUS_OBJECT_PATH_NOT_FOUND, which maps to ENOENT (right for the
 * second case, wrong for the first).  So the prefix is checked here, the
 * same way reject_if_not_dir() checks the last component for the
 * trailing-slash half of the very same clause: a handle-less attribute
 * query, and a verdict only when the answer is positive.
 *
 * Cost is one query for a path that has a prefix at all; the deeper
 * ancestors are only ever looked at once a nearer one has come back
 * missing, i.e. on a path that was going to fail regardless.  The walk
 * runs from the nearest ancestor outwards, so the first one that exists
 * decides: if it is a directory the whole prefix is a directory chain (a
 * directory's own parents cannot be anything else) and there is nothing
 * to report; if it is not, that is the POSIX ENOTDIR case.  Anything
 * else -- a query that fails for some other reason, a name with no
 * prefix to speak of -- is left to the real operation, exactly as
 * reject_if_not_dir() leaves it.
 *
 * The walk is exposed rather than kept private to __ntpath() because
 * chdir() needs the same verdict but does not come through this file's
 * path builder: it hand-builds a UNICODE_STRING for
 * RtlSetCurrentDirectory_U(), so it reaches the walk with an NT path it
 * built itself.  Hence the arguments are the NT path and the
 * RootDirectory handle it is relative to (0 for an absolute one) rather
 * than a struct __ntpath.
 *
 * Where truncation may start follows from that handle: an NT path with
 * no root handle ("\??\C:\a\b") keeps everything up to and including
 * the drive's backslash, while every component of a name relative to a
 * RootDirectory handle is a prefix component and may be cut.
 *
 * Returns 1 when a component of the path prefix positively exists and is
 * not a directory, 0 otherwise; errno is not touched.
 *
 * A caveat for anyone testing this under Wine rather than on NT: Wine's
 * NtQueryAttributesFile (dlls/ntdll/unix/file.c) passes the Unix name
 * lookup_unix_name() built relative to the root handle straight to
 * get_file_info(), i.e. to a plain stat() against the *process* working
 * directory, so a RootDirectory-relative query answers about the wrong
 * file -- "not found", or positively about a same-named file in the cwd
 * if one happens to exist.  That hits the pre-existing
 * reject_if_not_dir() the same way (under Wine, openat(dirfd, "file/",
 * ...) is not rejected either), and NT resolves such a name properly --
 * ObOpenObjectByName is handed the whole OBJECT_ATTRIBUTES, root handle
 * included -- so the dirfd-relative half of both checks is a
 * real-Windows question, not a Wine one. */
int __nt_prefix_not_dir(const UNICODE_STRING *nt, HANDLE root)
{
	FILE_BASIC_INFORMATION bi;
	UNICODE_STRING cut = *nt;
	OBJECT_ATTRIBUTES oa;
	size_t floor = root ? 0 : nt_prefix_root(nt);
	size_t i = nt->Length / sizeof(WCHAR);

	InitializeObjectAttributes(&oa, &cut, OBJ_CASE_INSENSITIVE, root, 0);
	while (i > floor) {
		NTSTATUS st;
		if (nt->Buffer[--i] != '\\') continue;
		/* `i` starts at nt->Length / sizeof(WCHAR) and only ever
		 * decreases, so i * sizeof(WCHAR) <= nt->Length, and nt->Length
		 * is itself a USHORT the caller already fits.  This narrowing
		 * re-narrows a value that arrived as a USHORT, so it cannot
		 * wrap.
		 * USHORT-safe: bounded by the source string's own Length. */
		cut.Length = (USHORT)(i * sizeof(WCHAR));
		st = NtQueryAttributesFile(&oa, &bi);
		if (NT_SUCCESS(st))
			return !(bi.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
		if (st != STATUS_OBJECT_NAME_NOT_FOUND && st != STATUS_OBJECT_PATH_NOT_FOUND)
			return 0;
	}
	return 0;
}

/* The __ntpath-side wrapper: the same verdict, but freeing the path and
 * reporting it the way the rest of this file reports a failure. */
static int reject_if_prefix_not_dir(struct __ntpath *out, HANDLE root)
{
	if (!__nt_prefix_not_dir(&out->nt, root)) return 0;
	__ntpath_free(out);
	errno = ENOTDIR;
	return -1;
}

/* Defined below, next to normalize_rel(), which it uses. */
static int nt_path_over_max_path(const WCHAR *dos, size_t n, int *trailing,
                                 struct __ntpath *out, ULONG attributes);

int __ntpath(const char *path, struct __ntpath *out, ULONG attributes)
{
	WCHAR *dos;
	size_t n;
	int trailing;
	NTSTATUS st;

	if (!path) { errno = EFAULT; return -1; }
	if (!*path) { errno = ENOENT; return -1; }

	dos = dos_from_posix(path, &n, &trailing);
	if (!dos) return -1;

	/* Same ceiling, and the same reason, as the hand-built UNICODE_STRING
	 * in __ntpath_at() below: a name past __US_MAX_WCHARS code units
	 * cannot be described by one at all.  POSIX wants ENAMETOOLONG for an
	 * over-long name, and reporting it here rather than letting the Rtl's
	 * failure fall into the catch-all ENOENT below is what makes every
	 * caller of this layer agree with chdir(), which has always checked
	 * its own hand-built string (src/unistd/chdir.c). */
	if (n > __US_MAX_WCHARS) { __free(dos); errno = ENAMETOOLONG; return -1; }

	memset(out, 0, sizeof *out);
	st = RtlDosPathNameToNtPathName_U_WithStatus(dos, &out->nt, 0, 0);
	if (NT_SUCCESS(st)) {
		out->buf = out->nt.Buffer;
		out->dos = dos;
		InitializeObjectAttributes(&out->oa, &out->nt, attributes, 0, 0);
	} else if (st == STATUS_NAME_TOO_LONG &&
	           !nt_path_over_max_path(dos, n, &trailing, out, attributes)) {
		/* The Rtl refused a name real NT can perfectly well open; the
		 * NT path was built here instead.  See that function. */
		__free(dos);
	} else {
		__free(dos);
		/* A relative name that fits on its own can still overflow once it
		 * is resolved against the current directory; the Rtl says so with
		 * STATUS_NAME_TOO_LONG, which is the same ENAMETOOLONG case. */
		errno = st == STATUS_NO_MEMORY ? ENOMEM :
			st == STATUS_NAME_TOO_LONG ? ENAMETOOLONG : ENOENT;
		return -1;
	}
	if (trailing && reject_if_not_dir(out)) return -1;
	if (reject_if_prefix_not_dir(out, 0)) return -1;
	return 0;
}

/* Lexical resolution of "." and ".." in a RootDirectory-relative name.
 *
 * XBD 4.13 Pathname Resolution, which every page specifying an *at()
 * function invokes for its path argument: "The special filename dot
 * shall refer to the directory specified by its predecessor.  The
 * special filename dot-dot shall refer to the parent directory of its
 * predecessor directory."  The NT object manager does not implement
 * either in a name resolved against a RootDirectory handle -- it takes
 * the name as a literal sequence of components -- so without this pass
 * openat(dfd, "./f", ...) failed with ENOENT while openat(dfd, "f", ...)
 * on the same file succeeded, and no spelling of ".." could reach a
 * parent at all.
 *
 * WHY LEXICAL, AND WHAT IT COSTS.  Resolving ".." by string surgery is
 * not what XBD 4.13 asks for: where a preceding component is a symbolic
 * link, the parent of the link's target is not the lexical parent, and
 * a lexical pass also collapses across components that do not exist or
 * are not directories, which 4.13 requires to fail.  This is done
 * anyway, deliberately, because it is what the REST OF THIS LIBRARY
 * already does: the AT_FDCWD and absolute branch resolves through
 * RtlDosPathNameToNtPathName_U, i.e. Windows path normalisation, which
 * Microsoft documents as a string pass performed before the file system
 * is consulted ("This function does not verify that the resulting path
 * and file name are valid, or that they see an existing file on the
 * associated volume" -- GetFullPathName Remarks).  Measured against this
 * tree through that branch: stat("d/nonexistent/../f") and
 * stat("d/regularfile/../f") both return 0.
 *
 * So the choice here is not "correct or lexical", it is "agree with the
 * other branch or disagree with it".  A library that answers the same
 * question two different ways depending on whether the caller passed
 * AT_FDCWD or a directory descriptor is worse than one that answers
 * consistently, because the inconsistency is undebuggable.  The gap
 * itself is fenced as its own finding; see test/posix-unreferenced.c,
 * test_pathres_dotdot_over_nondir().
 *
 * Operates in place -- the result is never longer than the input.
 * Returns 0 with *np and *trailing updated, or 1 if the name escapes
 * above the RootDirectory (a leading ".." with nothing to pop), which
 * a RootDirectory-relative name cannot express and which the caller
 * resolves a different way. */
static int normalize_rel(WCHAR *w, size_t *np, int *trailing)
{
	size_t n = *np, out = 0, i = 0;
	int lastdot = 0;

	while (i < n) {
		size_t j = i, len;
		while (j < n && w[j] != '\\') j++;
		len = j - i;
		if (len == 0) {
			/* a doubled separator: no component at all */
		} else if (len == 1 && w[i] == '.') {
			lastdot = 1;
		} else if (len == 2 && w[i] == '.' && w[i+1] == '.') {
			if (out == 0) return 1;               /* above the root */
			while (out > 0 && w[out-1] != '\\') out--;
			if (out > 0) out--;                   /* and its separator */
			lastdot = 1;
		} else {
			/* out < i whenever out > 0 (each emitted component is at
			 * least as short as its source and i has passed a
			 * separator), so this never overwrites the source. */
			if (out) w[out++] = '\\';
			memmove(w + out, w + i, len * sizeof(WCHAR));
			out += len;
			lastdot = 0;
		}
		i = j < n ? j + 1 : j;
	}
	w[out] = 0;
	*np = out;
	/* A name whose last component was "." or ".." names a directory by
	 * construction, and dos_from_posix computed `trailing` from the
	 * ORIGINAL string -- so "sub/." would otherwise stop requiring sub
	 * to be a directory once the "." is gone.  An empty result is the
	 * RootDirectory itself, which the caller has already established is
	 * a directory. */
	if (lastdot) *trailing = 1;
	if (out == 0) *trailing = 0;
	return 0;
}

/* THE {MAX_PATH} CEILING THE Rtl PUTS ON EVERY DOS NAME, AND WHY THIS
 * FUNCTION EXISTS.
 *
 * MEASURED ON REAL WINDOWS (GitHub windows-latest / Server 2025, CI run
 * 32822306367, all three windows-test legs agreeing), from a working
 * directory of "D:\a\ntlibc\ntlibc":
 *
 *   open("chm.d/<255 bytes>")               -> -1, [ENAMETOOLONG]  (280)
 *   open("chm.d/<254 bytes>")               -> -1, [ENAMETOOLONG]  (279)
 *   open("<255 bytes>")                     -> -1, [ENAMETOOLONG]  (274)
 *   chdir("chm.d"); open("<255 bytes>")     -> -1, [ENAMETOOLONG]  (280)
 *   openat(dirfd_of_chm.d, "<255 bytes>")   ->  ok
 *   open("\\?\D:\a\ntlibc\ntlibc\chm.d\<255 bytes>") -> ok
 *
 * The last two are what identify the culprit.  NTFS is happy with the
 * 255-code-unit component, and NtCreateFile is happy with the 284-byte
 * name -- the *same file*, created successfully, when the NT path is
 * handed over ready-made.  The only step that differs between the
 * failing and succeeding forms is RtlDosPathNameToNtPathName_U's
 * DOS->NT conversion, and the only route to [ENAMETOOLONG] through
 * __ntpath()'s Rtl branch is STATUS_NAME_TOO_LONG (the component check
 * in __name_too_long() and the __US_MAX_WCHARS ceiling are both ruled
 * out by the 254-byte and 274-byte cases above).  So: the Rtl applies
 * the Win32 {MAX_PATH} = 260 ceiling to any name it has to normalise,
 * and the "\\?\" local-device prefix -- which it copies through
 * verbatim rather than normalising -- is the documented way past it
 * (Microsoft, "Maximum Path Length Limitation":
 * https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
 * #enable-long-paths-in-windows-10-version-1607-and-later).
 *
 * That ceiling is not this library's to keep.  <limits.h> says
 * PATH_MAX is 4096 and sysconf(_PC_PATH_MAX) reports it, so a caller is
 * entitled to a 4000-byte path; before this, every path-taking
 * interface silently stopped at 260 on real Windows.  NONE OF THIS IS
 * VISIBLE UNDER WINE: Wine's RtlDosPathNameToNtPathName_U has no such
 * ceiling, so all six lines above succeed there, which is why the gap
 * survived until a test happened to build a path past 260.
 *
 * Rather than route every name through a hand-built NT path -- which
 * would mean reimplementing Windows path normalisation (drive-relative
 * "C:foo", UNC, device names, the reserved names) for the 99.9% of
 * paths the Rtl already handles correctly -- this runs ONLY as a
 * fallback, after the Rtl has refused with STATUS_NAME_TOO_LONG, i.e.
 * only on names that until now returned -1 outright.  A name that works
 * today takes exactly the path it took before.
 *
 * What it handles is correspondingly narrow: a drive-absolute name
 * ("X:\..."), a drive-rooted one ("\..." , taking the drive from the
 * current directory) and a plain relative one (joined onto the current
 * directory).  Drive-relative "X:rel", and a name whose ".." climbs
 * above the drive root, are declined -- the caller then reports the
 * Rtl's [ENAMETOOLONG] as before, which is no worse than what happened
 * before this existed.  "." and ".." are resolved lexically by
 * normalize_rel(), the same pass and the same documented caveat as the
 * __ntpath_at() branch below.
 *
 * Returns 0 with *out built (and *trailing possibly updated), or -1
 * without touching errno meaningfully -- the caller reports the Rtl's
 * own verdict in that case.  On success *out owns a single __malloc'd
 * buffer, held in ->dos exactly as the __ntpath_at() branch does, so
 * __ntpath_free() releases it. */
static int drive_letter(WCHAR c)
{
	return ((c | 0x20) >= 'a' && (c | 0x20) <= 'z');
}

static int nt_path_over_max_path(const WCHAR *dos, size_t n, int *trailing,
                                 struct __ntpath *out, ULONG attributes)
{
	WCHAR cur[4096];
	WCHAR *w = 0, *joined = 0;
	const WCHAR *body;      /* what follows "X:"; always starts with '\' */
	WCHAR letter;
	size_t bodyn, curn = 0, bn, len;
	ULONG got;

	if (n >= 3 && drive_letter(dos[0]) && dos[1] == ':' && dos[2] == '\\') {
		letter = dos[0];
		body = dos + 2;
		bodyn = n - 2;
	} else if (n >= 2 && dos[1] == ':') {
		return -1;              /* drive-relative "X:rel": declined */
	} else {
		got = RtlGetCurrentDirectory_U(sizeof cur, cur);
		if (!got || got > sizeof cur) return -1;
		curn = got / sizeof(WCHAR);
		if (curn < 2 || !drive_letter(cur[0]) || cur[1] != ':') return -1;
		letter = cur[0];
		if (dos[0] == '\\') {
			body = dos;
			bodyn = n;
		} else {
			/* "X:\a\b" + "\" + the relative name.  The current
			 * directory's own trailing separator (present only at a
			 * drive root) is dropped so the join never doubles it --
			 * normalize_rel() would swallow a doubled one anyway, but
			 * the arithmetic below is easier to check without it. */
			while (curn > 2 && cur[curn-1] == '\\') curn--;
			joined = __malloc((curn - 2 + 1 + n + 1) * sizeof(WCHAR));
			if (!joined) return -1;
			memcpy(joined, cur + 2, (curn - 2) * sizeof(WCHAR));
			joined[curn - 2] = '\\';
			memcpy(joined + curn - 1, dos, n * sizeof(WCHAR));
			bodyn = curn - 1 + n;
			joined[bodyn] = 0;
			body = joined;
		}
	}

	/* "\??\" + "X:" + "\" + the normalised body, which normalize_rel()
	 * writes without a leading separator.  It only ever shortens, so the
	 * allocation below is an upper bound. */
	w = __malloc((4 + 3 + bodyn + 1) * sizeof(WCHAR));
	if (!w) { __free(joined); return -1; }
	w[0] = '\\'; w[1] = '?'; w[2] = '?'; w[3] = '\\';
	w[4] = letter; w[5] = ':'; w[6] = '\\';
	bn = bodyn - 1;                 /* body[0] is the separator at w[6] */
	memcpy(w + 7, body + 1, bn * sizeof(WCHAR));
	__free(joined);
	if (normalize_rel(w + 7, &bn, trailing)) { __free(w); return -1; }
	len = 7 + bn;
	w[len] = 0;

	/* The same UNICODE_STRING ceiling the rest of this file applies. */
	if (len > __US_MAX_WCHARS) { __free(w); return -1; }

	memset(out, 0, sizeof *out);
	out->nt.Buffer = w;
	/* USHORT-safe: len is bounded by __US_MAX_WCHARS just above, so
	 * (len + 1) * sizeof(WCHAR) still fits a USHORT. */
	out->nt.Length = (USHORT)(len * sizeof(WCHAR));
	out->nt.MaximumLength = (USHORT)(out->nt.Length + sizeof(WCHAR));
	out->buf = 0;                   /* w is freed as ->dos */
	out->dos = w;
	InitializeObjectAttributes(&out->oa, &out->nt, attributes, 0, 0);
	return 0;
}

int __ntpath_at(int dirfd, const char *path, struct __ntpath *out, ULONG attributes)
{
	int absolute;

	if (!path) { errno = EFAULT; return -1; }
	/* "path is an empty string" is [ENOENT] on every page that specifies
	 * an *at() function -- open.html ("or path points to an empty
	 * string"), stat.html, access.html, unlink.html, mkdir.html,
	 * chmod.html, utimensat.html, readlink.html, link.html ("path1 or
	 * path2"), symlink.html, rename.html ("either old or new") -- and no
	 * page's *at()-specific ERRORS subsection carves out an exception.
	 * __ntpath() has said so since it was written; this branch did not.
	 *
	 * DO NOT "SIMPLIFY" THIS AWAY on the grounds that the object manager
	 * copes with an empty name perfectly well.  It does, and that is
	 * precisely the problem.  An empty UNICODE_STRING names the
	 * RootDirectory handle itself, so without this guard every *at()
	 * function silently operated on the descriptor's own directory:
	 * fchmodat(dfd, "", 0644, 0) changed that directory's mode and
	 * returned 0, and openat/fstatat/faccessat/utimensat likewise
	 * succeeded on the wrong object.  (The others reached NT and returned
	 * some incidental errno -- EISDIR, EEXIST, EINVAL -- never ENOENT.)
	 *
	 * The comment that used to sit below this, on the relative branch,
	 * read "An empty name (\"\") opens the directory itself".  That
	 * sentence is TRUE about the NT object manager and FALSE as a
	 * statement of what this function should do with a caller's empty
	 * path: it described a mechanism and then let the mechanism decide
	 * the policy, and the code faithfully implemented the comment.  Both
	 * were wrong at the POSIX layer for the same reason.  The empty NT
	 * name is correct as an ENCODING -- the branch below deliberately
	 * produces one for "." -- and wrong as a POLICY for caller input.
	 * Keep the two apart.
	 *
	 * This is not the AT_EMPTY_PATH case either: that flag is a Linux
	 * extension, it is not in POSIX.1-2017, and this library neither
	 * defines it nor has any caller that asks for it.  A caller meaning
	 * "the directory itself" spells it ".". */
	if (!*path) { errno = ENOENT; return -1; }
	absolute = path[0] == '/' || path[0] == '\\' ||
		(((path[0] | 0x20) >= 'a' && (path[0] | 0x20) <= 'z') && path[1] == ':');
	if (dirfd == AT_FDCWD || absolute) return __ntpath(path, out, attributes);

	/* Relative to a directory handle: the object manager resolves a
	 * relative name against RootDirectory, so the name is given as-is,
	 * with slashes fixed and without the DOS->NT conversion.  "." becomes
	 * the empty NT name, which is how the object manager spells "the
	 * RootDirectory itself" -- that is this encoding's legitimate use.  A
	 * caller's own empty path is a different thing and was rejected as
	 * [ENOENT] above; see the note there. */
	{
		struct __fd *f = __fd_get(dirfd);
		WCHAR *w;
		size_t n;
		int trailing;
		if (!f) return -1;
		if (f->type != __FD_DIR) { errno = ENOTDIR; return -1; }
		int esc;
		w = dos_from_posix(path, &n, &trailing);
		if (!w) return -1;
		esc = normalize_rel(w, &n, &trailing);
		if (esc) {
			/* The name reaches above the descriptor's directory, which
			 * a RootDirectory-relative name has no way to say.  Resolve
			 * the descriptor to an absolute path and hand the whole
			 * thing to __ntpath(), which normalises through the Rtl --
			 * the same answer the AT_FDCWD branch would give.  One extra
			 * query, and only in this case: a name that stays at or
			 * below the descriptor never gets here.
			 *
			 * Deliberately NOT done for every relative name.  Resolving
			 * by path would throw away what the *at() family exists for
			 * -- the descriptor pins the directory even if it is renamed
			 * out from under the caller -- so it is the fallback for the
			 * one shape that cannot be expressed, not the strategy. */
			char *dir, *joined;
			size_t dl;
			int rc;
			__free(w);
			dir = __handle_path(f->h);
			if (!dir) return -1;
			dl = strlen(dir);
			joined = __malloc(dl + 1 + strlen(path) + 1);
			if (!joined) { __free(dir); errno = ENOMEM; return -1; }
			memcpy(joined, dir, dl);
			/* "C:\\" already ends in one */
			if (dl && dir[dl-1] != '\\' && dir[dl-1] != '/') joined[dl++] = '\\';
			strcpy(joined + dl, path);
			__free(dir);
			rc = __ntpath(joined, out, attributes);
			__free(joined);
			return rc;
		}
		/* UNICODE_STRING.Length is a USHORT count of bytes and
		 * MaximumLength has to hold one more code unit, so a name past
		 * 32766 code units cannot be described -- and narrowing it would
		 * wrap rather than truncate, naming some prefix of the caller's
		 * path instead of failing. */
		if (n > __US_MAX_WCHARS) {
			__free(w);
			errno = ENAMETOOLONG;
			return -1;
		}
		memset(out, 0, sizeof *out);
		out->nt.Buffer = w;
		out->nt.Length = (USHORT)(n * sizeof(WCHAR));
		out->nt.MaximumLength = (USHORT)(out->nt.Length + sizeof(WCHAR));
		out->buf = 0;      /* w is freed as dos */
		out->dos = w;
		InitializeObjectAttributes(&out->oa, &out->nt, attributes, f->h, 0);
		if (trailing && reject_if_not_dir(out)) return -1;
		/* Relative to RootDirectory: every component of this name is a
		 * path prefix component, so the walk may truncate anywhere. */
		if (reject_if_prefix_not_dir(out, f->h)) return -1;
		return 0;
	}
}

void __ntpath_free(struct __ntpath *p)
{
	if (p->buf) RtlFreeHeap(__process_heap(), 0, p->buf);
	if (p->dos) __free(p->dos);
	p->buf = 0; p->dos = 0;
}

/* The DOS path of an open handle: FileNameInformation gives the path
 * below the volume's device; the volume itself is found by matching the
 * device name against each drive letter's. That is what kernel32's
 * GetFinalPathNameByHandle does too.  Here the cheaper route is taken:
 * NtQueryObject's ObjectNameInformation gives the full NT name
 * (\Device\HarddiskVolume3\dir\file), and the drive is found by asking
 * each of A: through Z: for its target.  Returns a malloc'd UTF-8 path. */
NTSTATUS NTAPI NtOpenSymbolicLinkObject(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
NTSTATUS NTAPI NtQuerySymbolicLinkObject(HANDLE, PUNICODE_STRING, PULONG);

char *__handle_path(HANDLE h)
{
	char buf[sizeof(OBJECT_NAME_INFORMATION) + 2048 * sizeof(WCHAR)];
	OBJECT_NAME_INFORMATION *oni = (OBJECT_NAME_INFORMATION *)buf;
	ULONG len = 0;
	NTSTATUS st;
	WCHAR drive[7] = { '\\', '?', '?', '\\', 'A', ':', 0 };
	WCHAR target[512];
	UNICODE_STRING us, tus;
	OBJECT_ATTRIBUTES oa;
	int c;

	st = NtQueryObject(h, ObjectNameInformation, oni, sizeof buf, &len);
	if (!NT_SUCCESS(st)) { __set_errno_status(st); return 0; }

	/* Under Wine (and in some other cases) ObjectNameInformation comes
	 * back already in \??\C:\... form instead of \Device\HarddiskVolumeN\...;
	 * such a name is already a drive path, so just strip the \??\ prefix
	 * rather than going through the device/symlink matching below, which
	 * only knows how to match \Device\... names. */
	{
		size_t nlen = oni->Name.Length / sizeof(WCHAR);
		WCHAR *nb = oni->Name.Buffer;
		if (nlen >= 6 && nb[0] == '\\' && nb[1] == '?' && nb[2] == '?' && nb[3] == '\\' &&
		    ((nb[4] >= 'A' && nb[4] <= 'Z') || (nb[4] >= 'a' && nb[4] <= 'z')) && nb[5] == ':') {
			return __utf16_to_utf8(nb + 4, nlen - 4);
		}
	}

	RtlInitUnicodeString(&us, drive);
	InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, 0, 0);
	for (c = 'A'; c <= 'Z'; c++) {
		HANDLE lh;
		ULONG tl;
		drive[4] = (WCHAR)c;
		us.Length = 6 * sizeof(WCHAR);
		if (!NT_SUCCESS(NtOpenSymbolicLinkObject(&lh, 0x1 /* SYMBOLIC_LINK_QUERY */, &oa))) continue;
		tus.Buffer = target; tus.Length = 0; tus.MaximumLength = sizeof target;
		st = NtQuerySymbolicLinkObject(lh, &tus, &tl);
		NtClose(lh);
		if (!NT_SUCCESS(st)) continue;
		tl = tus.Length / sizeof(WCHAR);
		if (oni->Name.Length / sizeof(WCHAR) >= tl &&
		    !memcmp(oni->Name.Buffer, target, tl * sizeof(WCHAR)) &&
		    (oni->Name.Length / sizeof(WCHAR) == tl || oni->Name.Buffer[tl] == '\\')) {
			size_t rest = oni->Name.Length / sizeof(WCHAR) - tl;
			WCHAR *w = __malloc((rest + 3) * sizeof(WCHAR));
			char *r;
			if (!w) return 0;
			w[0] = (WCHAR)c; w[1] = ':';
			memcpy(w + 2, oni->Name.Buffer + tl, rest * sizeof(WCHAR));
			if (!rest) { w[2] = '\\'; rest = 1; }
			r = __utf16_to_utf8(w, rest + 2);
			__free(w);
			return r;
		}
	}
	/* Not on a drive letter (a pipe, a UNC path): give the NT name. */
	return __utf16_to_utf8(oni->Name.Buffer, oni->Name.Length / sizeof(WCHAR));
}
