/* SPDX-FileCopyrightText: (C) 2026 Gavin John */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Minimal local stubs shaped like this project's src/internal/nt.h
 * declarations -- not the real header, just enough surface for the
 * checker to see real Nt* argument positions and struct shapes. */
typedef unsigned int ULONG; /* real ULONG is uint32_t (src/internal/nt.h);
                              * `unsigned long` is 8 bytes on a native
                              * x86_64 analysis host with no -target flag,
                              * which would mask the real LLP64 padding
                              * the sibling unsafe.c fixture demonstrates. */
typedef long NTSTATUS;
typedef void *PVOID;
typedef void *HANDLE, **PHANDLE;
typedef unsigned long ACCESS_MASK;
typedef unsigned long SIZE_T;

typedef struct _OBJECT_ATTRIBUTES {
	ULONG Length;
	HANDLE RootDirectory;
	PVOID ObjectName;
	ULONG Attributes;
	PVOID SecurityDescriptor;
	PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES;

typedef struct _IO_STATUS_BLOCK {
	long Status;
	ULONG Information;
} IO_STATUS_BLOCK;

typedef struct _PROCESS_PRIORITY_CLASS {
	unsigned char Foreground;
	unsigned char PriorityClass;
} PROCESS_PRIORITY_CLASS;

typedef struct _PROCESS_BASIC_INFORMATION {
	NTSTATUS ExitStatus;
	PVOID PebBaseAddress;
	long BasePriority;
	ULONG UniqueProcessId;
} PROCESS_BASIC_INFORMATION;

typedef struct _FILE_BASIC_INFORMATION {
	long long CreationTime;
	long long LastAccessTime;
	ULONG FileAttributes;
} FILE_BASIC_INFORMATION;

typedef struct _FILE_STANDARD_INFORMATION {
	long long EndOfFile;
} FILE_STANDARD_INFORMATION;

NTSTATUS NtCreateFile(PHANDLE, ACCESS_MASK, OBJECT_ATTRIBUTES *,
                      IO_STATUS_BLOCK *, PVOID, ULONG, ULONG, ULONG, ULONG,
                      PVOID, ULONG);
NTSTATUS NtSetInformationProcess(HANDLE, int, PVOID, ULONG);
NTSTATUS NtQueryInformationProcess(HANDLE, int, PVOID, ULONG, ULONG *);
NTSTATUS NtQueryInformationFile(HANDLE, IO_STATUS_BLOCK *, PVOID, ULONG, int);
NTSTATUS NtSetInformationFile(HANDLE, IO_STATUS_BLOCK *, PVOID, ULONG, int);
void *memset(void *, int, unsigned long);

/* Declaration-time aggregate init proves the whole object, including
 * padding -- InitializeObjectAttributes-style field tweaks afterward
 * do not undo that proof. */
int create_with_full_init(HANDLE *out)
{
	OBJECT_ATTRIBUTES oa = {0};
	IO_STATUS_BLOCK io;
	NTSTATUS st;

	oa.Length = sizeof(oa);
	oa.Attributes = 0x40;
	st = NtCreateFile(out, 0, &oa, &io, 0, 0, 0, 1, 0, 0, 0);
	return (int)st;
}

/* A whole-object memset before any field write also proves the object,
 * the same way src/misc/resource.c's apply_job_limits() does for its
 * JOBOBJECT_EXTENDED_LIMIT_INFORMATION. */
int set_priority_after_memset(HANDLE h, int value)
{
	PROCESS_PRIORITY_CLASS pc;

	memset(&pc, 0, sizeof pc);
	pc.Foreground = 0;
	pc.PriorityClass = (unsigned char)value;
	return (int)NtSetInformationProcess(h, 0, &pc, sizeof pc);
}

/* An OUT-only IO_STATUS_BLOCK is never a caller-populated IN slot for
 * NtCreateFile, so leaving it untouched before the call is not a
 * partial-initialization footgun -- the kernel writes the whole thing. */
int create_with_untouched_iosb(HANDLE *out)
{
	OBJECT_ATTRIBUTES oa = {0};
	IO_STATUS_BLOCK io;

	return (int)NtCreateFile(out, 0, &oa, &io, 0, 0, 0, 1, 0, 0, 0);
}

/* The OUT parameter is read back afterward, so it is proven consumed. */
int query_and_consume(HANDLE h)
{
	PROCESS_BASIC_INFORMATION pbi;
	NTSTATUS st = NtQueryInformationProcess(h, 0, &pbi, sizeof pbi, 0);
	if (st < 0)
		return -1;
	return (int)pbi.ExitStatus;
}

/* Field-by-field with no memset and no declaration-time initializer is
 * still fully proven when the fields written, walked against the real
 * target's own computed layout, leave no gap: PROCESS_PRIORITY_CLASS's
 * two adjacent UCHAR fields can never have compiler-inserted padding
 * between or after them on any target, unlike OBJECT_ATTRIBUTES's
 * pointer-sized RootDirectory after a 4-byte Length on LLP64. */
int set_priority_field_by_field_no_gap(HANDLE h, int value)
{
	PROCESS_PRIORITY_CLASS pc;

	pc.Foreground = 0;
	pc.PriorityClass = (unsigned char)value;
	return (int)NtSetInformationProcess(h, 0, &pc, sizeof pc);
}

/* A successful Nt* OUT query fills the whole buffer under the same
 * contract a local memset would -- src/stat/utimensat.c's
 * set_times_handle() round-trips exactly this way: query the current
 * attributes, zero and selectively re-set only the timestamps that are
 * changing, and cross back into the kernel with FileAttributes left
 * exactly as the query filled it. */
int query_then_reuse_as_in(HANDLE h)
{
	IO_STATUS_BLOCK io;
	FILE_BASIC_INFORMATION bi;
	NTSTATUS st = NtQueryInformationFile(h, &io, &bi, sizeof bi, 0);
	if (st < 0)
		return -1;
	bi.LastAccessTime = 0;
	st = NtSetInformationFile(h, &io, &bi, sizeof bi, 0);
	return (int)st;
}

/* Two OUT queries, the second of which can fail and return early before
 * the first's result is ever read -- src/ioctl/ioctl.c's
 * fionread_file() shape.  The first buffer is not a wasted round trip:
 * it really is read, just not on the path where the second query fails
 * first and the whole operation is abandoned instead. */
int query_two_then_use_both(HANDLE h, int *out)
{
	IO_STATUS_BLOCK io;
	FILE_STANDARD_INFORMATION si;
	FILE_BASIC_INFORMATION bi;
	NTSTATUS st = NtQueryInformationFile(h, &io, &si, sizeof si, 0);
	if (st < 0)
		return -1;
	st = NtQueryInformationFile(h, &io, &bi, sizeof bi, 0);
	if (st < 0)
		return -1;
	*out = (int)(si.EndOfFile + bi.FileAttributes);
	return 0;
}

/* A shared, locally-defined setup helper that unconditionally memsets
 * its own by-address out-parameter -- shaped exactly like
 * src/thread/semaphore.c's and src/thread/mqueue.c's own per-file
 * object_attributes(), whose only store to its OBJECT_ATTRIBUTES*
 * parameter is a call to the real InitializeObjectAttributes(), which
 * itself opens with exactly this memset, one level down inside a
 * `do { ... } while (0)` -- proves the caller's local exactly as
 * completely as if that memset were written here directly, even though
 * this checker analyzes one function frame at a time and does not
 * itself see across the call. */
static void setup(OBJECT_ATTRIBUTES *oa)
{
	do {
		memset(oa, 0, sizeof(*oa));
		oa->Length = sizeof(*oa);
		oa->Attributes = 0x40;
	} while (0);
}

int create_via_local_helper(HANDLE *out)
{
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK io;

	setup(&oa);
	return (int)NtCreateFile(out, 0, &oa, &io, 0, 0, 0, 1, 0, 0, 0);
}
