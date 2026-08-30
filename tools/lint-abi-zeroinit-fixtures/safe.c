/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Minimal local stubs shaped like this project's src/internal/nt.h
 * declarations -- not the real header, just enough surface for the
 * checker to see real Nt* argument positions and struct shapes. */
typedef unsigned long ULONG;
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

NTSTATUS NtCreateFile(PHANDLE, ACCESS_MASK, OBJECT_ATTRIBUTES *,
                      IO_STATUS_BLOCK *, PVOID, ULONG, ULONG, ULONG, ULONG,
                      PVOID, ULONG);
NTSTATUS NtSetInformationProcess(HANDLE, int, PVOID, ULONG);
NTSTATUS NtQueryInformationProcess(HANDLE, int, PVOID, ULONG, ULONG *);
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
