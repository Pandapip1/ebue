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
#include <stddef.h>
#include "libc.h"
#include "afd.h"

/* The "\Device\Tcp" transport name this project's one supported socket
 * kind (AF_INET/SOCK_STREAM) names in its open packet, and its length
 * in *bytes* -- AFD_OPEN_PACKET.TransportDeviceNameLength is a byte
 * count, not a character count (phnt ntafd.h annotates it
 * _Field_size_bytes_opt_; ReactOS passes UNICODE_STRING.Length, which
 * is also bytes).  Getting this wrong by a factor of two is the classic
 * UTF-16 length bug, so it is computed once, here. */
static const WCHAR afd_transport[] = AFD_TRANSPORT_TCP;
#define AFD_TRANSPORT_WCHARS ((sizeof(afd_transport) / sizeof(WCHAR)) - 1) /* excludes the NUL */
#define AFD_TRANSPORT_BYTES (AFD_TRANSPORT_WCHARS * sizeof(WCHAR))

/* The value is the open packet: its 24-byte header (NOT
 * sizeof(AFD_OPEN_PACKET), which is 28), the name, and the name's NUL.
 * The NUL is not counted by TransportDeviceNameLength but is kept in
 * the buffer, matching ReactOS's WSPSocket, which copies
 * TransportName.Length + sizeof(WCHAR). */
#define AFD_OPEN_PACKET_BYTES \
	(AFD_OPEN_PACKET_HEADER_SIZE + AFD_TRANSPORT_BYTES + sizeof(WCHAR))

/* See afd.h.  Exact fit, deliberately: this is
 *
 *     FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName)
 *       + EaNameLength + 1 (the NUL) + EaValueLength
 *
 * which is exactly the `ComputedLength` NT's IoCheckEaBufferValidity()
 * computes.  ReactOS's WSPSocket instead writes
 * `SizeOfPacket + sizeof(FILE_FULL_EA_INFORMATION) + AFD_PACKET_COMMAND_LENGTH`,
 * which is 3 bytes larger (sizeof() counts the EaName[1] placeholder
 * and pads to 4) and leaves the declared total 4-misaligned.  The
 * validator tolerates trailing slack on a final entry, but there is no
 * reason to declare bytes the entry does not describe -- and an exact,
 * 4-aligned total is an invariant test/posix-socket-ea.c can assert
 * without having to special-case padding. */
unsigned long __afd_open_ea_size(void)
{
	return (unsigned long)(AFD_EA_HEADER_SIZE + AFD_EA_NAME_LEN + 1 + AFD_OPEN_PACKET_BYTES);
}

/* See afd.h. */
void __afd_build_open_ea(void *buf)
{
	FILE_FULL_EA_INFORMATION *ea = (FILE_FULL_EA_INFORMATION *)buf;
	AFD_OPEN_PACKET *pkt;

	memset(buf, 0, __afd_open_ea_size());

	/* Single, and therefore final, entry: NextEntryOffset is 0.  A
	 * non-zero value would have to equal ALIGN_UP(ComputedLength, 4)
	 * *and* be followed by another entry. */
	ea->NextEntryOffset = 0;
	ea->Flags = 0;
	/* EaNameLength excludes the terminator; the terminator must still
	 * be present, because the validator checks EaName[EaNameLength]
	 * == '\0'.  Hence AFD_EA_NAME_LEN here but +1 in the copy. */
	ea->EaNameLength = AFD_EA_NAME_LEN;
	memcpy(ea->EaName, AFD_EA_NAME, AFD_EA_NAME_LEN + 1);
	ea->EaValueLength = (unsigned short)AFD_OPEN_PACKET_BYTES;

	/* The value starts immediately after the name's NUL.  With a
	 * 15-byte name that lands at offset 8 + 15 + 1 == 24, so the
	 * packet's own uint32_t fields stay naturally aligned. */
	pkt = (AFD_OPEN_PACKET *)(void *)(ea->EaName + AFD_EA_NAME_LEN + 1);
	pkt->EndpointFlags = 0; /* connection-oriented: not CONNECTIONLESS/RAW/MESSAGE_ORIENTED */
	pkt->GroupID = 0;
	/* The three fields ReactOS's 12-byte AFD_CREATE_PACKET does not
	 * have, and whose absence is what made real Windows read the
	 * device name as a length -- see afd.h's socket-creation banner. */
	pkt->AddressFamily = AF_INET;
	pkt->SocketType = SOCK_STREAM;
	pkt->Protocol = IPPROTO_TCP;
	pkt->TransportDeviceNameLength = (uint32_t)AFD_TRANSPORT_BYTES;
	memcpy(pkt->TransportDeviceName, afd_transport, AFD_TRANSPORT_BYTES + sizeof(WCHAR));
}

/* Open a fresh \Device\Afd\Endpoint handle carrying the AF_INET/
 * SOCK_STREAM transport ("\Device\Tcp") -- a FILE_FULL_EA_INFORMATION
 * named "AfdOpenPacketXX" whose value is an AFD_OPEN_PACKET naming the
 * transport device.  See src/internal/afd.h's socket-creation banner
 * for the layout and the two sources it is taken from.  Every socket()
 * call and every accept()ed connection needs one of these. */
int __afd_open(HANDLE *out)
{
	unsigned long ea_size = __afd_open_ea_size();
	char *buf;
	UNICODE_STRING devname;
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;

	buf = malloc(ea_size);
	if (!buf) { errno = ENOMEM; return -1; }
	__afd_build_open_ea(buf);

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
 * supported pair, so anything else is EAFNOSUPPORT.
 *
 * The 14 address bytes are written through src/internal/afd.h's
 * TDI_IP_OFF_* offsets rather than through a TDI_ADDRESS_IP struct.
 * tdi.h packs that struct to 1 (it sits between pshpack1.h and
 * poppack.h), so in_addr is at +2, not at the +4 an ordinary C struct
 * would put it; see the TDI banner in afd.h.  ReactOS's WSPBind
 * (dll/win32/msafd/misc/dllmain.c) writes the same 14 bytes as a plain
 * RtlCopyMemory of sockaddr.sa_data, which is the identical image. */
int __afd_addr_from_sockaddr(const struct sockaddr *addr, socklen_t len, TRANSPORT_ADDRESS *out)
{
	const struct sockaddr_in *sin;
	unsigned char *a;

	if (!addr || len < (socklen_t)sizeof(struct sockaddr_in)) { errno = EINVAL; return -1; }
	if (addr->sa_family != AF_INET) { errno = EAFNOSUPPORT; return -1; }

	sin = (const struct sockaddr_in *)(const void *)addr;
	out->TAAddressCount = 1;
	/* Length of the *address*, i.e. the sockaddr minus its family --
	 * 14 for sockaddr_in, never sizeof() of a padded struct. */
	out->Address[0].AddressLength = TDI_ADDRESS_LENGTH_IP;
	/* AddressType overlays sa_family, and AF_INET == TDI_ADDRESS_TYPE_IP == 2. */
	out->Address[0].AddressType = TDI_ADDRESS_TYPE_IP;
	a = out->Address[0].Address;
	memset(a, 0, TDI_ADDRESS_LENGTH_IP);
	memcpy(a + TDI_IP_OFF_PORT, &sin->sin_port, sizeof(sin->sin_port));
	memcpy(a + TDI_IP_OFF_ADDR, &sin->sin_addr.s_addr, sizeof(sin->sin_addr.s_addr));
	/* sin_zero is already zeroed by the memset above. */
	return 0;
}

/* See afd.h.  26, not sizeof(AFD_BIND_DATA) (28). */
unsigned long __afd_bind_request_size(void)
{
	return (unsigned long)AFD_BIND_REQ_SIZE;
}

/* See afd.h. */
int __afd_build_bind_request(void *buf, unsigned long share_type,
                             const struct sockaddr *addr, socklen_t len)
{
	AFD_BIND_DATA *bd = (AFD_BIND_DATA *)buf;

	if (__afd_addr_from_sockaddr(addr, len, &bd->Address) < 0) return -1;
	bd->ShareType = (uint32_t)share_type;
	return 0;
}

/* See afd.h.  46 on x86_64, 34 on i386 -- and in neither case
 * sizeof(AFD_CONNECT_INFO), which rounds the tail up for
 * TAAddressCount's alignment and would declare bytes the request does
 * not describe.  IOCTL_AFD_CONNECT is METHOD_NEITHER, so this is the
 * only bound afd.sys has on its read of the address. */
unsigned long __afd_connect_request_size(void)
{
	return (unsigned long)AFD_CONNECT_REQ_SIZE;
}

/* See afd.h.  Written through the AFD_CONNECT_REQ_OFF_* byte offsets
 * rather than through AFD_CONNECT_INFO's members, for the reason the
 * header's connect banner gives: the position of RemoteAddress is the
 * one thing this project's two reference sources disagree about, it
 * differs only on x86_64, and expressing it as arithmetic on
 * sizeof(HANDLE) keeps the disagreement visible instead of hiding it
 * inside a compiler's padding rules.
 *
 * SanActive, RootEndpoint and ConnectEndpoint are all zero for an
 * ordinary connect(): no Winsock SAN provider, and no multipoint
 * root/leaf endpoints (those are what WSAJoinLeaf fills in -- phnt
 * ntafd.h shares this structure between AFD_CONNECT and
 * AFD_JOIN_LEAF).  ReactOS's WSPConnect (dll/win32/msafd/misc/
 * dllmain.c) likewise sets UseSAN/Root/Unknown to 0/0/0. */
int __afd_build_connect_request(void *buf, const struct sockaddr *addr, socklen_t len)
{
	unsigned char *p = (unsigned char *)buf;
	TRANSPORT_ADDRESS ta;

	/* Validate before writing anything, so a rejected address leaves
	 * the caller's buffer untouched. */
	if (__afd_addr_from_sockaddr(addr, len, &ta) < 0) return -1;

	memset(p, 0, (size_t)AFD_CONNECT_REQ_SIZE);
	/* SanActive / RootEndpoint / ConnectEndpoint: already zero. */
	{
		uint32_t count = (uint32_t)ta.TAAddressCount;
		unsigned short l = ta.Address[0].AddressLength;
		unsigned short t = ta.Address[0].AddressType;
		memcpy(p + AFD_CONNECT_REQ_OFF_ADDR_COUNT, &count, sizeof(count));
		memcpy(p + AFD_CONNECT_REQ_OFF_ADDR_LENGTH, &l, sizeof(l));
		memcpy(p + AFD_CONNECT_REQ_OFF_ADDR_TYPE, &t, sizeof(t));
		memcpy(p + AFD_CONNECT_REQ_OFF_ADDR, ta.Address[0].Address, TDI_ADDRESS_LENGTH_IP);
	}
	return 0;
}

/* ---- IOCTL_AFD_SELECT request/reply, by offset -----------------------
 *
 * See src/internal/afd.h's poll section: ReactOS's ULONG_PTR Exclusive
 * puts Handles at +24 on x86_64, where phnt, wepoll and libuv all put
 * it at +16.  Everything here goes through the named offsets so that
 * no compiler's idea of ULONG_PTR can move the array again.
 *
 * The header fields are at fixed offsets on both ABIs; only the
 * per-handle element size is pointer-sized. */

/* See afd.h. */
unsigned long __afd_poll_request_size(unsigned long nhandles)
{
	return AFD_POLL_REQ_SIZE(nhandles);
}

/* See afd.h.  Timeout is a plain LONGLONG here (src/internal/nt.h has
 * no .QuadPart union), memcpy'd rather than stored through a cast so
 * the buffer needs no more than pointer alignment. */
void __afd_build_poll_request(void *buf, long long timeout, unsigned long nhandles)
{
	unsigned char *p = (unsigned char *)buf;
	uint32_t count = (uint32_t)nhandles;
	uint32_t exclusive = 0;

	memset(p, 0, (size_t)AFD_POLL_REQ_SIZE(nhandles));
	memcpy(p + AFD_POLL_REQ_OFF_TIMEOUT, &timeout, sizeof(timeout));
	memcpy(p + AFD_POLL_REQ_OFF_HANDLE_COUNT, &count, sizeof(count));
	/* Four bytes, and always zero.  phnt calls it BOOLEAN Unique and
	 * wepoll/libuv call it ULONG Exclusive; they disagree about the
	 * type but not about the four bytes, and zero is zero either
	 * way -- see afd.h's poll banner. */
	memcpy(p + AFD_POLL_REQ_OFF_EXCLUSIVE, &exclusive, sizeof(exclusive));
}

/* See afd.h. */
void __afd_poll_set_handle(void *buf, unsigned long i, HANDLE h, uint32_t events)
{
	unsigned char *e = (unsigned char *)buf + AFD_POLL_REQ_OFF_HANDLES + i * AFD_POLL_H_SIZE;
	uint32_t zero = 0;

	memcpy(e + AFD_POLL_H_OFF_HANDLE, &h, sizeof(h));
	memcpy(e + AFD_POLL_H_OFF_EVENTS, &events, sizeof(events));
	memcpy(e + AFD_POLL_H_OFF_STATUS, &zero, sizeof(zero));
}

/* See afd.h.  IOCTL_AFD_SELECT is METHOD_BUFFERED, so afd.sys writes
 * the events it actually observed back into these same slots. */
uint32_t __afd_poll_get_events(const void *buf, unsigned long i)
{
	const unsigned char *e = (const unsigned char *)buf + AFD_POLL_REQ_OFF_HANDLES + i * AFD_POLL_H_SIZE;
	uint32_t events;

	memcpy(&events, e + AFD_POLL_H_OFF_EVENTS, sizeof(events));
	return events;
}

/* See afd.h. */
NTSTATUS __afd_poll_get_status(const void *buf, unsigned long i)
{
	const unsigned char *e = (const unsigned char *)buf + AFD_POLL_REQ_OFF_HANDLES + i * AFD_POLL_H_SIZE;
	NTSTATUS st;

	memcpy(&st, e + AFD_POLL_H_OFF_STATUS, sizeof(st));
	return st;
}

/* TA_ADDRESS -> sockaddr_in, truncating into *addr and *len the way
 * accept.html specifies ("If...address_len is not large enough...
 * stored address shall be truncated").  Reads the same packed offsets
 * __afd_addr_from_sockaddr() writes. */
void __afd_addr_to_sockaddr(const TA_ADDRESS *ta, struct sockaddr *addr, socklen_t *len)
{
	struct sockaddr_in sin;
	const unsigned char *a = ta->Address;
	socklen_t n;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	memcpy(&sin.sin_port, a + TDI_IP_OFF_PORT, sizeof(sin.sin_port));
	memcpy(&sin.sin_addr.s_addr, a + TDI_IP_OFF_ADDR, sizeof(sin.sin_addr.s_addr));

	if (!addr || !len) return;
	n = *len < (socklen_t)sizeof(sin) ? *len : (socklen_t)sizeof(sin);
	memcpy(addr, &sin, n);
	*len = sizeof(sin);
}
