/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The AFD-endpoint-creation and sockaddr<->TDI-address helpers every
 * src/socket/ (every .c there) file shares.  See src/internal/afd.h's banner for the
 * two sources this is checked against.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "libc.h"
#include "afd.h"

/* Open a fresh \Device\Afd\Endpoint handle carrying the AF_INET/
 * SOCK_STREAM transport ("\Device\Tcp") -- the EA-buffer recipe from
 * ReactOS's WSPSocket (dll/win32/msafd/misc/dllmain.c, around its own
 * lines 240-267 and 347): a FILE_FULL_EA_INFORMATION named
 * "AfdOpenPacketXX" whose value is an AFD_CREATE_PACKET naming the
 * transport device.  Every socket() call and every accept()ed
 * connection needs one of these; see those two files for the two call
 * sites. */
int __afd_open(HANDLE *out)
{
	static const WCHAR transport[] = AFD_TRANSPORT_TCP;
	enum { transport_wchars = (sizeof(transport) / sizeof(WCHAR)) - 1 }; /* not counting the NUL */
	unsigned long transport_len_bytes = transport_wchars * sizeof(WCHAR);
	unsigned long packet_size = transport_len_bytes + sizeof(AFD_CREATE_PACKET) + sizeof(WCHAR);
	unsigned long ea_size = packet_size + sizeof(FILE_FULL_EA_INFORMATION) + AFD_EA_NAME_LEN;
	char *buf;
	FILE_FULL_EA_INFORMATION *ea;
	AFD_CREATE_PACKET *pkt;
	UNICODE_STRING devname;
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;

	buf = malloc(ea_size);
	if (!buf) { errno = ENOMEM; return -1; }
	memset(buf, 0, ea_size);

	ea = (FILE_FULL_EA_INFORMATION *)buf;
	ea->NextEntryOffset = 0;
	ea->Flags = 0;
	ea->EaNameLength = AFD_EA_NAME_LEN;
	memcpy(ea->EaName, AFD_EA_NAME, AFD_EA_NAME_LEN + 1); /* +1: the NUL AfdCommand's own literal carries */
	ea->EaValueLength = (unsigned short)packet_size;

	pkt = (AFD_CREATE_PACKET *)(ea->EaName + ea->EaNameLength + 1);
	pkt->EndpointFlags = 0; /* connection-oriented, not AFD_ENDPOINT_CONNECTIONLESS: SOCK_STREAM only */
	pkt->GroupID = 0;
	pkt->SizeOfTransportName = transport_len_bytes;
	memcpy(pkt->TransportName, transport, transport_len_bytes + sizeof(WCHAR));

	/* \Device\Afd\Endpoint (dllmain.c's DevName; confirmed independently
	 * by leftarcode's reverse-engineering series -- see afd.h banner). */
	{
		static const WCHAR endpoint[] = AFD_ENDPOINT_DEVICE;
		devname.Length = (unsigned short)((sizeof(endpoint) / sizeof(WCHAR) - 1) * sizeof(WCHAR));
		devname.MaximumLength = devname.Length + sizeof(WCHAR);
		devname.Buffer = (WCHAR *)endpoint;
	}
	InitializeObjectAttributes(&oa, &devname, OBJ_CASE_INSENSITIVE, 0, 0);

	/* FILE_SYNCHRONOUS_IO_NONALERT: this project's own house style
	 * (src/fcntl/open.c's banner) for every handle read()/write()/the
	 * AFD ioctls below wait on synchronously, rather than ReactOS's
	 * per-call-event scheme (dllmain.c creates a fresh NtCreateEvent
	 * for every ioctl) -- both are legitimate ways to drive AFD's
	 * asynchronous ioctls to completion; this one reuses the
	 * NtWaitForSingleObject(f->h,...)-on-STATUS_PENDING pattern every
	 * other __FD_* type here already relies on (src/unistd/read.c). */
	st = NtCreateFile(&h, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &io, 0, 0,
	                  FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF,
	                  FILE_SYNCHRONOUS_IO_NONALERT, buf, ea_size);
	free(buf);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	*out = h;
	return 0;
}

/* See afd.h for the contract. */
NTSTATUS __afd_ioctl(HANDLE h, ULONG code, void *in, ULONG inlen, void *out, ULONG outlen, IO_STATUS_BLOCK *io_out)
{
	IO_STATUS_BLOCK io;
	NTSTATUS st;

	io.Status = 0; io.Information = 0;
	st = NtDeviceIoControlFile(h, 0, 0, 0, &io, code, in, inlen, out, outlen);
	if (st == STATUS_PENDING) {
		NtWaitForSingleObject(h, 0, 0);
		st = io.Status;
	}
	if (io_out) *io_out = io;
	return st;
}

/* sockaddr_in -> TRANSPORT_ADDRESS.  bind.html/connect.html both take
 * (address, address_len); AF_INET/SOCK_STREAM is this project's only
 * supported pair, so anything else is EAFNOSUPPORT. */
int __afd_addr_from_sockaddr(const struct sockaddr *addr, socklen_t len, TRANSPORT_ADDRESS *out)
{
	const struct sockaddr_in *sin;
	TDI_ADDRESS_IP *ip;

	if (!addr || len < (socklen_t)sizeof(struct sockaddr_in)) { errno = EINVAL; return -1; }
	if (addr->sa_family != AF_INET) { errno = EAFNOSUPPORT; return -1; }

	sin = (const struct sockaddr_in *)addr;
	out->TAAddressCount = 1;
	out->Address[0].AddressLength = sizeof(TDI_ADDRESS_IP);
	out->Address[0].AddressType = TDI_ADDRESS_TYPE_IP;
	ip = (TDI_ADDRESS_IP *)out->Address[0].Address;
	ip->sin_port = sin->sin_port;
	ip->in_addr = sin->sin_addr.s_addr;
	memset(ip->sin_zero, 0, sizeof(ip->sin_zero));
	return 0;
}

/* TA_ADDRESS -> sockaddr_in, truncating into *addr and *len the way
 * accept.html specifies ("If...address_len is not large enough...
 * stored address shall be truncated"). */
void __afd_addr_to_sockaddr(const TA_ADDRESS *ta, struct sockaddr *addr, socklen_t *len)
{
	struct sockaddr_in sin;
	const TDI_ADDRESS_IP *ip = (const TDI_ADDRESS_IP *)ta->Address;
	socklen_t n;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = ip->sin_port;
	sin.sin_addr.s_addr = ip->in_addr;

	if (!addr || !len) return;
	n = *len < (socklen_t)sizeof(sin) ? *len : (socklen_t)sizeof(sin);
	memcpy(addr, &sin, n);
	*len = sizeof(sin);
}
