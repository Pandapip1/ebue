/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The WSL mode stored on NTFS as the $LXMOD extended attribute.
 *
 * Microsoft documents $LXMOD as the file-mode member of WSL's NTFS
 * metadata, and Linux ntfs3 stores it as one little-endian 32-bit word.
 * Deliberately do not create $LXUID or $LXGID here: those are literal IDs
 * in a Linux distribution's user namespace, while ntlibc's getuid() is a
 * Windows-SID-derived process identity and is not a WSL UID mapping.
 */
#include <string.h>
#include "libc.h"

#define LXMOD_NAME "$LXMOD"
#define LXMOD_NAME_LEN 6u
#define LXMOD_VALUE_LEN 4u
#define LXMOD_EA_LEN (8u + LXMOD_NAME_LEN + 1u + LXMOD_VALUE_LEN)

static void putle32(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

static unsigned getle32(const unsigned char *p)
{
	return (unsigned)p[0] | (unsigned)p[1] << 8 |
	       (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

unsigned __lxmod_create_buffer(void *buffer, unsigned mode)
{
	unsigned char *b = buffer;
	__NT_FILE_FULL_EA_INFORMATION *ea = (__NT_FILE_FULL_EA_INFORMATION *)b;
	memset(b, 0, LXMOD_EA_LEN);
	ea->EaNameLength = LXMOD_NAME_LEN;
	ea->EaValueLength = LXMOD_VALUE_LEN;
	memcpy(ea->EaName, LXMOD_NAME, LXMOD_NAME_LEN + 1);
	putle32((unsigned char *)ea->EaName + LXMOD_NAME_LEN + 1, mode);
	return LXMOD_EA_LEN;
}

int __lxmod_get(HANDLE h, unsigned *mode)
{
	unsigned char request[12] = { 0 };
	unsigned char reply[32] = { 0 };
	__NT_FILE_GET_EA_INFORMATION *get = (__NT_FILE_GET_EA_INFORMATION *)request;
	__NT_FILE_FULL_EA_INFORMATION *ea = (__NT_FILE_FULL_EA_INFORMATION *)reply;
	IO_STATUS_BLOCK io;
	NTSTATUS st;

	get->EaNameLength = LXMOD_NAME_LEN;
	memcpy(get->EaName, LXMOD_NAME, LXMOD_NAME_LEN + 1);
	st = NtQueryEaFile(h, &io, reply, sizeof reply, 1, request, sizeof request,
	                   0, 1);
	if (!NT_SUCCESS(st) || io.Information < LXMOD_EA_LEN ||
	    ea->EaNameLength != LXMOD_NAME_LEN ||
	    ea->EaValueLength != LXMOD_VALUE_LEN ||
	    memcmp(ea->EaName, LXMOD_NAME, LXMOD_NAME_LEN))
		return 0;
	*mode = getle32((unsigned char *)ea->EaName + LXMOD_NAME_LEN + 1);
	return 1;
}

int __lxmod_set(HANDLE h, unsigned mode)
{
	unsigned char buffer[LXMOD_EA_LEN];
	IO_STATUS_BLOCK io;
	NTSTATUS st;
	unsigned len = __lxmod_create_buffer(buffer, mode);
	st = NtSetEaFile(h, &io, buffer, len);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}
