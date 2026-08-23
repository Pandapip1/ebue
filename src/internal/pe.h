/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Minimal PE/COFF *image format* structures -- as opposed to nt.h, which
 * is the native NT *API* (ntdll calling conventions, PEB/TEB, NTSTATUS).
 * The one thing this file exists for is ntlibc_pe_find_export(): given
 * the base address of an already-mapped PE image, walk its own export
 * directory by hand and return a named export's address, using nothing
 * but memory reads -- no LdrGetProcedureAddress, no any other imported
 * call. See src/internal/delayload2.c's header comment for why that
 * matters: it is the one piece that lets __delayLoadHelper2 resolve
 * ntdll's own (delay-loaded) imports without calling through an
 * unresolved ntdll import to do it.
 *
 * Field layouts are PE/COFF spec ones, cross-checked against tinycc's
 * own copy (tccpe.c, IMAGE_DOS_HEADER/IMAGE_FILE_HEADER/
 * IMAGE_OPTIONAL_HEADER/IMAGE_EXPORT_DIRECTORY, ~line 122 on) since that
 * is the linker that actually writes the images this reads. Only the
 * fields needed to reach DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]
 * and then walk the export directory are declared.
 */
#ifndef NTLIBC_PE_H
#define NTLIBC_PE_H

#include "nt.h"

#define IMAGE_NT_SIGNATURE (0x00004550) /* "PE\0\0" */
#define IMAGE_DIRECTORY_ENTRY_EXPORT 0

#pragma pack(push, 1)

typedef struct _IMAGE_DOS_HEADER {
	USHORT e_magic;
	/* e_cblp..e_ovno (13 words) + e_res[4] + e_oemid/e_oeminfo (2 words)
	 * + e_res2[10] = 29 words, none of them needed here; e_lfanew sits
	 * at byte offset 0x3C (14+4+2+10 = 30 words in, including e_magic). */
	USHORT e_ignore1[29];
	ULONG  e_lfanew;      /* file offset of IMAGE_NT_HEADERS */
} IMAGE_DOS_HEADER;

typedef struct _IMAGE_FILE_HEADER {
	USHORT Machine;
	USHORT NumberOfSections;
	ULONG  TimeDateStamp;
	ULONG  PointerToSymbolTable;
	ULONG  NumberOfSymbols;
	USHORT SizeOfOptionalHeader;
	USHORT Characteristics;
} IMAGE_FILE_HEADER;

typedef struct _IMAGE_DATA_DIRECTORY {
	ULONG VirtualAddress;
	ULONG Size;
} IMAGE_DATA_DIRECTORY;

/* PE32 (i386): ImageBase and the four Size* fields are 32-bit, and there
 * is a BaseOfData field PE32+ drops. */
typedef struct _IMAGE_OPTIONAL_HEADER32 {
	USHORT Magic;
	UCHAR  MajorLinkerVersion, MinorLinkerVersion;
	ULONG  SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData;
	ULONG  AddressOfEntryPoint, BaseOfCode, BaseOfData;
	ULONG  ImageBase, SectionAlignment, FileAlignment;
	USHORT MajorOperatingSystemVersion, MinorOperatingSystemVersion;
	USHORT MajorImageVersion, MinorImageVersion;
	USHORT MajorSubsystemVersion, MinorSubsystemVersion;
	ULONG  Win32VersionValue, SizeOfImage, SizeOfHeaders, CheckSum;
	USHORT Subsystem, DllCharacteristics;
	ULONG  SizeOfStackReserve, SizeOfStackCommit, SizeOfHeapReserve, SizeOfHeapCommit;
	ULONG  LoaderFlags, NumberOfRvaAndSizes;
	IMAGE_DATA_DIRECTORY DataDirectory[16];
} IMAGE_OPTIONAL_HEADER32;

/* PE32+ (x86_64): no BaseOfData; ImageBase and the four Size* fields
 * widen to 64 bits. */
typedef struct _IMAGE_OPTIONAL_HEADER64 {
	USHORT     Magic;
	UCHAR      MajorLinkerVersion, MinorLinkerVersion;
	ULONG      SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData;
	ULONG      AddressOfEntryPoint, BaseOfCode;
	ULONGLONG  ImageBase;
	ULONG      SectionAlignment, FileAlignment;
	USHORT     MajorOperatingSystemVersion, MinorOperatingSystemVersion;
	USHORT     MajorImageVersion, MinorImageVersion;
	USHORT     MajorSubsystemVersion, MinorSubsystemVersion;
	ULONG      Win32VersionValue, SizeOfImage, SizeOfHeaders, CheckSum;
	USHORT     Subsystem, DllCharacteristics;
	ULONGLONG  SizeOfStackReserve, SizeOfStackCommit, SizeOfHeapReserve, SizeOfHeapCommit;
	ULONG      LoaderFlags, NumberOfRvaAndSizes;
	IMAGE_DATA_DIRECTORY DataDirectory[16];
} IMAGE_OPTIONAL_HEADER64;

#if defined(__x86_64__) || defined(_WIN64)
typedef IMAGE_OPTIONAL_HEADER64 IMAGE_OPTIONAL_HEADER;
#else
typedef IMAGE_OPTIONAL_HEADER32 IMAGE_OPTIONAL_HEADER;
#endif

typedef struct _IMAGE_NT_HEADERS {
	ULONG Signature;
	IMAGE_FILE_HEADER FileHeader;
	IMAGE_OPTIONAL_HEADER OptionalHeader;
} IMAGE_NT_HEADERS;

typedef struct _IMAGE_EXPORT_DIRECTORY {
	ULONG  Characteristics;
	ULONG  TimeDateStamp;
	USHORT MajorVersion, MinorVersion;
	ULONG  Name;
	ULONG  Base;
	ULONG  NumberOfFunctions;
	ULONG  NumberOfNames;
	ULONG  AddressOfFunctions;    /* RVA to ULONG[NumberOfFunctions] */
	ULONG  AddressOfNames;        /* RVA to ULONG[NumberOfNames], each an RVA to a name string */
	ULONG  AddressOfNameOrdinals; /* RVA to USHORT[NumberOfNames] */
} IMAGE_EXPORT_DIRECTORY;

#pragma pack(pop)

/* Resolves `name` in the export table of the PE image already mapped at
 * `base` (a module base as found in the PEB's loader lists, or returned
 * by LdrLoadDll) by hand-walking IMAGE_DOS_HEADER -> IMAGE_NT_HEADERS ->
 * OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] ->
 * IMAGE_EXPORT_DIRECTORY -> AddressOfNames/AddressOfNameOrdinals/
 * AddressOfFunctions, exactly the way the loader itself would, but
 * without calling the loader. Returns NULL if `base` is not a valid PE
 * image, has no export directory, or has no export by that name --
 * never partial/garbage. No ordinal-only lookup: nothing here needs it. */
void *ntlibc_pe_find_export(void *base, const char *name);

/* Reads [base, base+SizeOfImage) out of the mapped image's own PE
 * header (OptionalHeader.SizeOfImage) and returns it as [*start, *end).
 * Used by delayload2.c to tell "this pointer lands inside the DLL that
 * owns it" (already resolved) from "this pointer is the delay-load
 * thunk's own address" (not yet resolved) without needing to remember
 * the thunk's address separately. Returns 0 (leaving *start and *end
 * untouched) if `base` is not a valid PE image. */
int ntlibc_pe_dll_range(void *base, void **start, void **end);

#endif
