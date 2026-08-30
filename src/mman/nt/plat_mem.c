/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT implementation of src/internal/plat_mem.h -- see that header for
 * the contract each function makes.  Everything here was, until this
 * file existed, inline inside src/mman/mman.c; nothing changed in
 * substance, only location and the addition of a POSIX-shaped return
 * (0/-1 with errno set) in place of a raw NTSTATUS.
 */
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "libc.h"
#include "plat_mem.h"

#define MMAP_PAGE 4096u
static size_t pground(size_t n) { return (n + MMAP_PAGE - 1) & ~(size_t)(MMAP_PAGE - 1); }

/* mmap.html "Protection Options" -> NT page protection.  PROT_WRITE
 * without PROT_READ has no NT spelling (there is no write-only page
 * protection), so it widens to read/write; POSIX permits that outright:
 * "an implementation may permit accesses other than those specified by
 * prot". */
static ULONG prot_to_page(int prot)
{
	if (prot & PROT_EXEC) {
		if (prot & PROT_WRITE) return PAGE_EXECUTE_READWRITE;
		if (prot & PROT_READ)  return PAGE_EXECUTE_READ;
		return PAGE_EXECUTE;
	}
	if (prot & PROT_WRITE) return PAGE_READWRITE;
	if (prot & PROT_READ)  return PAGE_READONLY;
	return PAGE_NOACCESS;
}

/* Same table, but for a MAP_PRIVATE section view: mmap.html says a
 * MAP_PRIVATE write "shall be visible only to the calling process" and
 * "It is unspecified whether this change to the mapped file is visible
 * to other processes... or is carried through to the underlying object."
 * -- i.e. the write must not reach the file. NT's answer to that is
 * copy-on-write (PAGE_WRITECOPY/PAGE_EXECUTE_WRITECOPY): the first write
 * to a page forks it to a private, pagefile-backed copy instead of
 * dirtying the section. Win32's own FILE_MAP_COPY works against a
 * section created with PAGE_READONLY, so this needs no extra access
 * beyond what the file was opened with. */
static ULONG prot_to_view(int prot, int private)
{
	if (!private) return prot_to_page(prot);
	if (prot & PROT_EXEC) {
		if (prot & PROT_WRITE) return PAGE_EXECUTE_WRITECOPY;
		if (prot & PROT_READ)  return PAGE_EXECUTE_READ;
		return PAGE_EXECUTE;
	}
	if (prot & PROT_WRITE) return PAGE_WRITECOPY;
	if (prot & PROT_READ)  return PAGE_READONLY;
	return PAGE_NOACCESS;
}

int __plat_mem_reserve(void **base_inout, size_t len, int prot)
{
	PVOID base = *base_inout;
	SIZE_T size = len;
	NTSTATUS st = NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size,
	                                      MEM_RESERVE | MEM_COMMIT, prot_to_page(prot));
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	*base_inout = base;
	return 0;
}

int __plat_mem_commit_fixed(void *base, size_t len, int prot)
{
	PVOID p = base;
	SIZE_T z = len;
	NTSTATUS st;
	/* Decommit then commit, so the previous mapping's modifications are
	 * actually discarded rather than left in place by a bare commit
	 * over already-committed pages -- see mman.c's MAP_FIXED banner. */
	NtFreeVirtualMemory(NtCurrentProcess(), &p, &z, MEM_DECOMMIT);
	p = base;
	z = len;
	st = NtAllocateVirtualMemory(NtCurrentProcess(), &p, 0, &z, MEM_COMMIT, prot_to_page(prot));
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_mem_decommit(void *base, size_t len)
{
	PVOID p = base;
	SIZE_T z = len;
	NTSTATUS st = NtFreeVirtualMemory(NtCurrentProcess(), &p, &z, MEM_DECOMMIT);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_mem_release(void *base, size_t len)
{
	PVOID b = base;
	SIZE_T z = 0;
	NTSTATUS st;
	(void)len; /* MEM_RELEASE takes the whole reservation; see plat_mem.h */
	st = NtFreeVirtualMemory(NtCurrentProcess(), &b, &z, MEM_RELEASE);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_mem_protect(void *addr, size_t len, int prot)
{
	PVOID p = addr;
	SIZE_T z = len;
	ULONG old = 0;
	NTSTATUS st = NtProtectVirtualMemory(NtCurrentProcess(), &p, &z, prot_to_page(prot), &old);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_mem_lock(void *addr, size_t len)
{
	PVOID p = addr;
	SIZE_T z = len;
	NTSTATUS st = NtLockVirtualMemory(NtCurrentProcess(), &p, &z, 1);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_mem_unlock(void *addr, size_t len)
{
	PVOID p = addr;
	SIZE_T z = len;
	NTSTATUS st = NtUnlockVirtualMemory(NtCurrentProcess(), &p, &z, 1);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

/* Create a section over `fh` and map a view of it at *base_inout (a
 * hint, or NULL to let NT choose).  Tries the broadest section
 * protection the caller's prot/flags could need first, and falls back
 * to a read-only section on [STATUS_ACCESS_DENIED] -- a handle opened
 * O_RDONLY cannot back a PAGE_READWRITE section, but MAP_PRIVATE still
 * works against a PAGE_READONLY one via copy-on-write (see
 * prot_to_view).  The section handle is closed before returning either
 * way: the view holds its own reference, so nothing is leaked by not
 * keeping it. */
int __plat_mem_map_file(__plat_handle_t fh, int prot, int flags, off_t off,
                        size_t viewbytes, void **base_inout)
{
	HANDLE section;
	IO_STATUS_BLOCK io;
	FILE_STANDARD_INFORMATION si;
	NTSTATUS st;
	LARGE_INTEGER secoff;
	SIZE_T viewsize;
	ULONG maxprot;
	PVOID base = *base_inout;
	int private = (flags & MAP_PRIVATE) != 0;
	long long eof = -1;

	/* Wine can retain writes made past EOF in the shared cache page even
	 * across close()+reopen()+NtCreateSection().  POSIX requires every
	 * mapping operation to zero-fill that partial page.  Capture the
	 * logical EOF before mapping so a writable shared view can restore
	 * the required zero tail below without extending the file. */
	if (!private && (prot & PROT_WRITE)) {
		st = NtQueryInformationFile(fh, &io, &si, sizeof si,
		                            FileStandardInformation);
		if (!NT_SUCCESS(st)) { errno = st == (NTSTATUS)STATUS_NO_MEMORY ? ENOMEM : ENOTSUP; return -1; }
		eof = si.EndOfFile;
	}

	maxprot = (prot & PROT_EXEC) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
	st = NtCreateSection(&section, SECTION_ALL_ACCESS, NULL, NULL,
	                     maxprot, SEC_COMMIT, fh);
	if (st == (NTSTATUS)STATUS_ACCESS_DENIED) {
		maxprot = (prot & PROT_EXEC) ? PAGE_EXECUTE_READ : PAGE_READONLY;
		st = NtCreateSection(&section, SECTION_ALL_ACCESS, NULL, NULL,
		                     maxprot, SEC_COMMIT, fh);
	}
	if (!NT_SUCCESS(st)) { errno = st == (NTSTATUS)STATUS_NO_MEMORY ? ENOMEM : ENOTSUP; return -1; }

	/* ViewSize=0 means "map from SectionOffset to the end of the
	 * section" -- NtCreateSection above set the section's size to the
	 * file's own length (MaximumSize=NULL), so this maps exactly the
	 * bytes the file has, and NT rounds the accessible range up to the
	 * next page boundary and zero-fills the tail on its own (the same
	 * behaviour mmap.html requires: "the system shall always zero-fill
	 * any partial page at the end of an object").  An explicit ViewSize
	 * of the caller's rounded `len` was tried first and rejected with
	 * [STATUS_INVALID_VIEW_SIZE] whenever `len` rounds past the file's
	 * exact byte length, which is every mapping that covers a whole
	 * small file -- i.e. the common case, not an edge one. `viewbytes`
	 * still bounds what mmap() tells its caller was mapped; NT's actual
	 * view can only be smaller when the file is shorter than `len`
	 * implies, which is the caller's own error to make. */
	secoff = (LARGE_INTEGER)off;
	viewsize = 0;
	st = NtMapViewOfSection(section, NtCurrentProcess(), &base, 0, 0,
	                        &secoff, &viewsize, ViewShare, 0,
	                        prot_to_view(prot, private));
	NtClose(section);
	if (!NT_SUCCESS(st)) { errno = st == (NTSTATUS)STATUS_NO_MEMORY ? ENOMEM : ENOTSUP; return -1; }
	if (eof > off && (eof & (MMAP_PAGE - 1)) != 0 &&
	    (unsigned long long)(eof - off) < viewbytes) {
		size_t tail = (size_t)(eof - off);
		size_t end = pground(tail);
		if (end > viewbytes) end = viewbytes;
		memset((char *)base + tail, 0, end - tail);
	}
	*base_inout = base;
	return 0;
}

int __plat_mem_unmap_view(void *base, size_t len)
{
	NTSTATUS st;
	(void)len; /* a view knows its own extent; see plat_mem.h */
	st = NtUnmapViewOfSection(NtCurrentProcess(), base);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_mem_flush_view(void *addr, size_t len, __plat_handle_t writeback)
{
	const void *p = addr;
	SIZE_T z = len;
	IO_STATUS_BLOCK io;
	NTSTATUS st;
	FILE_BASIC_INFORMATION bi;
	LARGE_INTEGER now;

	st = NtFlushVirtualMemory(NtCurrentProcess(), &p, &z, &io);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	/* The section flush above writes data but does not consistently
	 * advance the file times.  Preserve the attributes explicitly:
	 * Wine clears FILE_ATTRIBUTE_READONLY when FileAttributes is zero,
	 * unlike real NT (the same quirk is documented in utimensat.c). */
	st = NtQueryInformationFile(writeback, &io, &bi, sizeof bi, FileBasicInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	NtQuerySystemTime(&now);
	bi.CreationTime = bi.LastAccessTime = 0;
	bi.LastWriteTime = bi.ChangeTime = now;
	st = NtSetInformationFile(writeback, &io, &bi, sizeof bi, FileBasicInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}
