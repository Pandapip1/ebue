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

/* The value is the open packet: the shape's header (NOT
 * sizeof(AFD_OPEN_PACKET), which is 28, nor sizeof(AFD_CREATE_PACKET),
 * which is 16 -- both count a TransportName[1] placeholder and pad),
 * the name, and the name's NUL.  The NUL is not counted by the packet's
 * own name-length field but is kept in the buffer, matching ReactOS's
 * WSPSocket, which copies TransportName.Length + sizeof(WCHAR). */
#define AFD_OPEN_PACKET_BYTES(hdr) ((hdr) + AFD_TRANSPORT_BYTES + sizeof(WCHAR))

/* See afd.h.  The header byte count for a shape, and the only place
 * that mapping is written down. */
static unsigned long afd_shape_header(int shape)
{
	return shape == AFD_SHAPE_NT4
	     ? (unsigned long)AFD_CREATE_PACKET_HEADER_SIZE
	     : (unsigned long)AFD_OPEN_PACKET_HEADER_SIZE;
}

/* See afd.h.
 *
 * Version-gated, not probed, and afd.h's socket-creation banner is
 * where the argument for that lives: handing either driver the other
 * one's layout *succeeds*, so there is no failure for a probe to learn
 * from.  src/internal/ntversion.c states the three conditions a
 * divergence has to meet before it may be settled this way.
 *
 * The threshold is NT 6.0, matching ReactOS's own apitest
 * (modules/rostests/apitests/afd/AfdHelpers.c, which branches on
 * `LOBYTE(LOWORD(GetVersion())) >= 6`).  A platform that cannot supply
 * a version at all is treated as modern -- see ntversion.c -- so the
 * shape CI verifies is also the shape any unrecognised platform gets. */
int __afd_open_shape(void)
{
	return __nt_version_at_least(6, 0) ? AFD_SHAPE_NT6 : AFD_SHAPE_NT4;
}

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
 * without having to special-case padding.  (ReactOS's *apitest* sizes
 * it exactly the way this does, with FIELD_OFFSET throughout.) */
unsigned long __afd_open_ea_size_for(int shape)
{
	return (unsigned long)(AFD_EA_HEADER_SIZE + AFD_EA_NAME_LEN + 1
	                       + AFD_OPEN_PACKET_BYTES(afd_shape_header(shape)));
}

unsigned long __afd_open_ea_size(void)
{
	return __afd_open_ea_size_for(__afd_open_shape());
}

/* See afd.h.
 *
 * The two shapes are written out separately and in full rather than
 * shared through a run of offsets.  They differ by three fields in the
 * middle, which is precisely the kind of difference that disappears
 * when it is expressed as arithmetic; each block below can be read
 * against its reference declaration one field at a time. */
void __afd_build_open_ea_for(int shape, void *buf)
{
	FILE_FULL_EA_INFORMATION *ea = (FILE_FULL_EA_INFORMATION *)buf;
	unsigned long hdr = afd_shape_header(shape);
	void *value;

	memset(buf, 0, __afd_open_ea_size_for(shape));

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
	ea->EaValueLength = (unsigned short)AFD_OPEN_PACKET_BYTES(hdr);

	/* The value starts immediately after the name's NUL.  With a
	 * 15-byte name that lands at offset 8 + 15 + 1 == 24, so the
	 * packet's own uint32_t fields stay naturally aligned -- true of
	 * both shapes, since only the header length differs. */
	value = (void *)(ea->EaName + AFD_EA_NAME_LEN + 1);

	if (shape == AFD_SHAPE_NT4) {
		/* ReactOS sdk/include/reactos/drivers/afd/shared.h's
		 * AFD_CREATE_PACKET, read back by AfdCreateSocket() in
		 * drivers/network/afd/afd/main.c. */
		AFD_CREATE_PACKET *pkt = (AFD_CREATE_PACKET *)value;
		pkt->EndpointFlags = 0; /* connection-oriented */
		pkt->GroupID = 0;
		pkt->SizeOfTransportName = (uint32_t)AFD_TRANSPORT_BYTES;
		memcpy(pkt->TransportName, afd_transport, AFD_TRANSPORT_BYTES + sizeof(WCHAR));
	} else {
		/* phnt ntafd.h's AFD_OPEN_PACKET. */
		AFD_OPEN_PACKET *pkt = (AFD_OPEN_PACKET *)value;
		pkt->EndpointFlags = 0; /* not CONNECTIONLESS/RAW/MESSAGE_ORIENTED */
		pkt->GroupID = 0;
		/* The three fields ReactOS's 12-byte AFD_CREATE_PACKET does
		 * not have, and whose absence is what made real Windows read
		 * the device name as a length -- see afd.h's socket-creation
		 * banner. */
		pkt->AddressFamily = AF_INET;
		pkt->SocketType = SOCK_STREAM;
		pkt->Protocol = IPPROTO_TCP;
		pkt->TransportDeviceNameLength = (uint32_t)AFD_TRANSPORT_BYTES;
		memcpy(pkt->TransportDeviceName, afd_transport, AFD_TRANSPORT_BYTES + sizeof(WCHAR));
	}
}

void __afd_build_open_ea(void *buf)
{
	__afd_build_open_ea_for(__afd_open_shape(), buf);
}

/* __afd_open() and __afd_ioctl() -- declared in src/internal/afd.h,
 * called by every file under src/socket/ -- have moved to
 * src/socket/nt/plat_socket.c: their bodies are entirely NtCreateFile/
 * NtDeviceIoControlFile/NtWaitForSingleObject marshaling, no different
 * in kind from src/mman/nt/plat_mem.c or src/unistd/nt/plat_fd.c, and
 * this file (afdsupport.c) is the pure byte-marshaling half of the AFD
 * support code -- the EA/request/reply builders below, none of which
 * issue a syscall of their own. See src/internal/plat_socket.h's banner
 * for why __afd_open()/__afd_ioctl() keep their existing afd.h-declared,
 * NT-shaped signatures rather than gaining POSIX-shaped __plat_ twins:
 * six other files under src/socket/ call them directly and are out of
 * scope for this conversion. */

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
 * puts Handles at +24 on x86_64, where the AFD driver's own source,
 * phnt, wepoll and libuv all put it at +16.  Everything here goes
 * through the named offsets so that no compiler's idea of ULONG_PTR
 * can move the array again.
 *
 * The header fields are at fixed offsets on both ABIs; only the
 * per-handle element size is pointer-sized. */

/* See afd.h. */
unsigned long __afd_poll_request_size(unsigned long nhandles)
{
	return (unsigned long)AFD_POLL_REQ_SIZE(nhandles);
}

/* See afd.h.  Timeout is a plain LONGLONG here (src/internal/nt.h has
 * no .QuadPart union), memcpy'd rather than stored through a cast so
 * the buffer needs no more than pointer alignment. */
void __afd_build_poll_request(void *buf, long long timeout, unsigned long nhandles)
{
	unsigned char *p = (unsigned char *)buf;
	uint32_t count = (uint32_t)nhandles;
	uint32_t exclusive = 0;

	memset(p, 0, AFD_POLL_REQ_SIZE(nhandles));
	memcpy(p + AFD_POLL_REQ_OFF_TIMEOUT, &timeout, sizeof(timeout));
	memcpy(p + AFD_POLL_REQ_OFF_HANDLE_COUNT, &count, sizeof(count));
	/* Four bytes, and always zero.  Microsoft's afd.h and phnt call
	 * it BOOLEAN Unique, wepoll and libuv call it ULONG Exclusive;
	 * they disagree about the type but not about the four bytes
	 * before an 8-aligned Handles, and zero is zero either way --
	 * see afd.h's poll banner.
	 *
	 * It must *stay* zero, and not merely because this project has
	 * no use for it: AfdPoll() reads it as Unique, and a non-zero
	 * Unique makes the request supersede any existing unique poll on
	 * the same first file object, cancelling that other IRP with
	 * STATUS_CANCELLED (the driver's poll.c walks AfdPollListHead
	 * and cancels the match).  A __fd_probe()-shaped poll setting it
	 * would silently break another thread's concurrent
	 * select()/poll() on the same socket. */
	memcpy(p + AFD_POLL_REQ_OFF_EXCLUSIVE, &exclusive, sizeof(exclusive));
}

/* See afd.h. */
void __afd_poll_set_handle(void *buf, unsigned long i, HANDLE h, uint32_t events)
{
	unsigned char *e = (unsigned char *)buf + AFD_POLL_REQ_OFF_HANDLES + (size_t)i * AFD_POLL_H_SIZE;
	uint32_t zero = 0;

	memcpy(e + AFD_POLL_H_OFF_HANDLE, &h, sizeof(h));
	memcpy(e + AFD_POLL_H_OFF_EVENTS, &events, sizeof(events));
	memcpy(e + AFD_POLL_H_OFF_STATUS, &zero, sizeof(zero));
}

/* See afd.h.  IOCTL_AFD_SELECT is METHOD_BUFFERED, so afd.sys writes
 * the events it actually observed back into these same slots. */
uint32_t __afd_poll_get_events(const void *buf, unsigned long i)
{
	const unsigned char *e = (const unsigned char *)buf + AFD_POLL_REQ_OFF_HANDLES + (size_t)i * AFD_POLL_H_SIZE;
	uint32_t events;

	memcpy(&events, e + AFD_POLL_H_OFF_EVENTS, sizeof(events));
	return events;
}

/* See afd.h.  The reply's own count -- the *only* thing that says how
 * many of the Handles[] slots afd.sys actually wrote. */
uint32_t __afd_poll_get_handle_count(const void *buf)
{
	uint32_t count;

	memcpy(&count, (const unsigned char *)buf + AFD_POLL_REQ_OFF_HANDLE_COUNT, sizeof(count));
	return count;
}

/* See afd.h.  Matching on the handle rather than indexing by request
 * position is not defensive padding: AfdPoll() *compacts* its output.
 * poll.c walks the requested endpoints and does
 *
 *     if ( found ) {
 *         pollInfo->NumberOfHandles++;
 *         pollHandleInfo++;
 *     }
 *
 * -- the output pointer advances only for an endpoint that fired, so
 * the entries that did fire are packed to the front of the array and
 * output slot i has nothing to do with request slot i.  With today's
 * single-handle probe the two coincide; written as an indexed read it
 * would silently become wrong the first time src/select/poll.c batches
 * several sockets into one ioctl. */
uint32_t __afd_poll_events_for(const void *buf, unsigned long nrequested, HANDLE h)
{
	uint32_t count = __afd_poll_get_handle_count(buf);
	unsigned long i;

	/* The reply cannot name more handles than were asked about; a
	 * count that says otherwise is not a reply this code understands,
	 * and reading past the buffer on its say-so would be worse than
	 * reporting nothing. */
	if ((unsigned long long)count > (unsigned long long)nrequested)
		count = (uint32_t)nrequested;

	for (i = 0; i < (unsigned long)count; i++) {
		const unsigned char *e = (const unsigned char *)buf + AFD_POLL_REQ_OFF_HANDLES
		                       + (size_t)i * AFD_POLL_H_SIZE;
		HANDLE eh;

		memcpy(&eh, e + AFD_POLL_H_OFF_HANDLE, sizeof(eh));
		if (eh == h) {
			uint32_t events;
			memcpy(&events, e + AFD_POLL_H_OFF_EVENTS, sizeof(events));
			return events;
		}
	}
	/* Not named in the reply: no event fired on it.  That is a real
	 * answer -- "nothing is ready" -- not a failure to obtain one. */
	return 0;
}

/* See afd.h. */
NTSTATUS __afd_poll_get_status(const void *buf, unsigned long i)
{
	const unsigned char *e = (const unsigned char *)buf + AFD_POLL_REQ_OFF_HANDLES + (size_t)i * AFD_POLL_H_SIZE;
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

/* See afd.h.  Two fields are checked, and each one is load-bearing.
 *
 * TAAddressCount is the only field in the reply that says how many
 * addresses are present.  accept.c read Address[0] without consulting
 * it, which is the same defect __afd_poll_events_for() exists to fix --
 * except that this buffer is out-only, so an unwritten Address[0] is
 * not the caller's own request read back but uninitialised stack.
 *
 * AddressLength stands in for a length check on IoStatus.Information.
 * The driver writes it as part of the TDI address it moves in, so over
 * a buffer zeroed before the ioctl a copy-back that stopped short of
 * the address leaves it zero, and rejecting zero rejects that reply --
 * with no arithmetic against Information, and no second source of truth
 * about how big the reply "should" be.  AfdWaitForListen() always
 * declares the whole address written or fails the IRP outright
 * (STATUS_BUFFER_TOO_SMALL), so a well-formed reply always passes; so
 * do the two name queries, which either move the whole TDI address in
 * or fail with STATUS_BUFFER_TOO_SMALL (AfdGetPeerName()) or the
 * transport's own query error (AfdGetSockName()).
 *
 * AddressType is deliberately not checked: it is the field that overlays
 * sa_family, and socket() admits AF_INET alone, so a connection accepted
 * on one of this library's listeners -- or an address AFD reports for
 * one of its endpoints -- has no other family to be.  It carries no
 * information AddressLength has not already given.
 *
 * The two field offsets are spelled +0 and +4 rather than through the
 * AFD_*_RSP_OFF_* names, because those differ per reply and this is the
 * one part that does not: a TRANSPORT_ADDRESS is TAAddressCount then
 * TA_ADDRESS, wherever the enclosing reply happens to put it. */
int __afd_transport_addr_out(const void *tap, struct sockaddr *addr, socklen_t *len)
{
	const unsigned char *p = (const unsigned char *)tap;
	TA_ADDRESS ta;
	int32_t count;
	unsigned short alen;

	memcpy(&count, p, sizeof(count));
	if (count < 1) return -1;

	memcpy(&alen, p + 4, sizeof(alen));
	if (alen < TDI_ADDRESS_LENGTH_IP) return -1;

	if (!addr || !len) return 0;

	/* Copied out by byte count rather than read through a
	 * TA_ADDRESS * aimed into the buffer: the caller's buffer need
	 * not be aligned for one, and this file's own tests hand it a
	 * plain unsigned char image. */
	memset(&ta, 0, sizeof(ta));
	memcpy(&ta, p + 4, (size_t)(2 + 2 + TDI_ADDRESS_LENGTH_IP));
	__afd_addr_to_sockaddr(&ta, addr, len);
	return 0;
}

/* See afd.h.  The wait-for-listen reply is a TRANSPORT_ADDRESS behind a
 * ULONG SequenceNumber, so the whole of this function is that offset. */
int __afd_accept_reply_addr(const void *reply, struct sockaddr *addr, socklen_t *len)
{
	return __afd_transport_addr_out((const unsigned char *)reply + AFD_ACCEPT_RSP_OFF_ADDR_COUNT,
	                                addr, len);
}

/* See afd.h.  These four exist so that the one thing that distinguishes
 * the two name replies -- 26 bytes with a ULONG ActivityCount in front
 * versus 22 bytes with nothing -- is reachable from a test with no
 * \Device\Afd, the same way __afd_build_bind_request() makes the bind
 * request's layout reachable.  Written as separate functions rather
 * than as an offset argument to one, because the offset is not a
 * parameter of anything: each ioctl has exactly one right answer, and a
 * caller that could pass the other one is the bug these are here to
 * make visible. */
unsigned long __afd_sockname_reply_size(void)
{
	return (unsigned long)AFD_SOCKNAME_RSP_SIZE;
}

int __afd_sockname_reply_addr(const void *reply, struct sockaddr *addr, socklen_t *len)
{
	return __afd_transport_addr_out((const unsigned char *)reply + AFD_SOCKNAME_RSP_OFF_ADDR,
	                                addr, len);
}

unsigned long __afd_peername_reply_size(void)
{
	return (unsigned long)AFD_PEERNAME_RSP_SIZE;
}

int __afd_peername_reply_addr(const void *reply, struct sockaddr *addr, socklen_t *len)
{
	return __afd_transport_addr_out((const unsigned char *)reply + AFD_PEERNAME_RSP_OFF_ADDR,
	                                addr, len);
}
