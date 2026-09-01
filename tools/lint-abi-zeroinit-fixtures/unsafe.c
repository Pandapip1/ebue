/* SPDX-FileCopyrightText: (C) 2026 Gavin John */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Minimal local stubs shaped like this project's src/internal/nt.h
 * declarations -- not the real header, just enough surface for the
 * checker to see real Nt* argument positions and struct shapes. */
typedef unsigned int ULONG; /* real ULONG is uint32_t (src/internal/nt.h);
                              * `unsigned long` is 8 bytes on a native
                              * x86_64 analysis host with no -target flag,
                              * which would mask the real LLP64 padding
                              * this fixture exists to demonstrate. */
typedef long NTSTATUS;
typedef void *PVOID;
typedef void *HANDLE, **PHANDLE;
typedef unsigned long ACCESS_MASK;

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

/* The InitializeObjectAttributes-macro footgun this checker exists to
 * catch: every field this project's real macro sets is set individually
 * through the pointer, with no whole-object initializer or memset ever
 * covering the struct, so any compiler-inserted padding between fields
 * (e.g. after `Length` before the pointer-sized `RootDirectory` on
 * LP64/LLP64) is never proven zeroed before it crosses into the kernel. */
int create_field_by_field(HANDLE *out)
{
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK io;

	oa.Length = sizeof(oa);
	oa.RootDirectory = 0;
	oa.Attributes = 0x40;
	oa.ObjectName = 0;
	oa.SecurityDescriptor = 0;
	oa.SecurityQualityOfService = 0;
	return (int)NtCreateFile(out, 0, &oa, &io, 0, 0, 0, 1, 0, 0, 0); /* abi-zeroinit-expect */
}

/* The OUT parameter this call fills is never read back before the
 * function returns -- a wasted round trip through the kernel, and the
 * symmetrical half of the same boundary obligation. */
int query_and_ignore(HANDLE h)
{
	PROCESS_BASIC_INFORMATION pbi;
	NTSTATUS st = NtQueryInformationProcess(h, 0, &pbi, sizeof pbi, 0);
	return (int)st; /* abi-zeroinit-expect */
}
