/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The `\Device\Afd` wire protocol: ioctl codes and request/reply
 * structures for AF_INET/SOCK_STREAM sockets, driven directly through
 * NtCreateFile/NtDeviceIoControlFile per test/networking-audit.md sec 1
 * ("Decision: drive AFD directly through ntdll").
 *
 * None of this is documented by Microsoft.  It is cross-checked against
 * two independent sources that agree on it, per the audit:
 *
 *   - ReactOS's own AFD client (dll/win32/msafd/misc/{dllmain,sndrcv,
 *     helpers}.c) and the ioctl-code/structure header its driver and
 *     client share (sdk/include/reactos/drivers/afd/shared.h) -- both
 *     read directly from a checkout at /tmp/claude/repos/reactos while
 *     writing this file.  This is the *primary* source: it is real,
 *     compilable, driver-and-client-agreeing code, and it is what
 *     test/networking-audit.md and CONTRIBUTING.md's task both point at
 *     for "how Windows itself does it" (NtCreateFile + an EA buffer, as
 *     opposed to Wine's invented IOCTL_AFD_WINE_* codes).
 *   - mingw-w64's vendored copies of Microsoft's own DDK headers,
 *     ntstatus.h and tdi.h (found on this machine under
 *     /usr/share/mingw-w64/include/ -- the same headers this project's
 *     cross toolchain ships), for TRANSPORT_ADDRESS/TA_ADDRESS/
 *     TDI_ADDRESS_IP's exact field layout and the TDI_RECEIVE_* flags.
 *     ReactOS's own client code uses these types but does not vendor a
 *     copy of tdi.h itself; mingw-w64's is the SDK original.
 *
 * The ioctl code arithmetic is independently confirmed a third way:
 * test/networking-audit.md sec 1 shows Wine's IOCTL_AFD_BIND/LISTEN/
 * RECV/POLL (CTL_CODE(FILE_DEVICE_BEEP, 0x800+op, method,
 * FILE_ANY_ACCESS)) numerically equal ReactOS's
 * _AFD_CONTROL_CODE(op, method) for the same operations -- two
 * independent reverse-engineering efforts landed on the same numbers.
 *
 * Only what AF_INET/SOCK_STREAM (this project's declared scope) needs
 * is transcribed: bind, connect, listen, accept (wait-for-listen +
 * accept), send, recv, select/poll, disconnect, and socket creation
 * itself.  UDP-only, AF_UNIX-only and multipoint-only pieces of the
 * real structures are left out.
 */
#ifndef _NTLIBC_AFD_H
#define _NTLIBC_AFD_H

#include <stdint.h>
#include "nt.h"

/* ---- ioctl code generation (shared.h: FSCTL_AFD_BASE/_AFD_CONTROL_CODE,
 * confirmed against Wine's independently-derived numbers -- see the file
 * banner) ------------------------------------------------------------- */
#define AFD_FILE_DEVICE_NETWORK 0x00000012UL
#define _AFD_CONTROL_CODE(op, method) ((AFD_FILE_DEVICE_NETWORK) << 12 | ((op) << 2) | (method))

#define METHOD_BUFFERED_  0
#define METHOD_NEITHER_   3

#define AFD_BIND                    0
#define AFD_CONNECT                 1
#define AFD_START_LISTEN            2
#define AFD_WAIT_FOR_LISTEN         3
#define AFD_ACCEPT                  4
#define AFD_RECV                    5
#define AFD_SEND                    7
#define AFD_SELECT                  9
#define AFD_DISCONNECT              10

#define IOCTL_AFD_BIND               _AFD_CONTROL_CODE(AFD_BIND, METHOD_NEITHER_)
#define IOCTL_AFD_CONNECT            _AFD_CONTROL_CODE(AFD_CONNECT, METHOD_NEITHER_)
#define IOCTL_AFD_START_LISTEN       _AFD_CONTROL_CODE(AFD_START_LISTEN, METHOD_NEITHER_)
#define IOCTL_AFD_WAIT_FOR_LISTEN    _AFD_CONTROL_CODE(AFD_WAIT_FOR_LISTEN, METHOD_BUFFERED_)
#define IOCTL_AFD_ACCEPT             _AFD_CONTROL_CODE(AFD_ACCEPT, METHOD_BUFFERED_)
#define IOCTL_AFD_RECV               _AFD_CONTROL_CODE(AFD_RECV, METHOD_NEITHER_)
#define IOCTL_AFD_SEND               _AFD_CONTROL_CODE(AFD_SEND, METHOD_NEITHER_)
#define IOCTL_AFD_SELECT             _AFD_CONTROL_CODE(AFD_SELECT, METHOD_BUFFERED_)
#define IOCTL_AFD_DISCONNECT         _AFD_CONTROL_CODE(AFD_DISCONNECT, METHOD_NEITHER_)

/* Every wire-format struct below spells a 32-bit field as `uint32_t`
 * (and TAAddressCount as `int32_t`, matching TDI's own LONG), not
 * src/internal/nt.h's `ULONG` (`unsigned long`).  On the real target
 * (mingw's LLP64 ABI) the two are identical, always 4 bytes; but this
 * header is also compiled natively for `make asan` (fuzz/ntstubs.c),
 * where the host's LP64 ABI makes `unsigned long` 8 bytes.  These
 * structs are built with hand-computed byte offsets (__afd_open()'s EA
 * buffer, in particular), not sizeof()/member access alone, so a wider
 * ULONG there silently shifts every offset after it and misaligns the
 * next struct -- confirmed the hard way, as a real UBSan misaligned-
 * access finding under `make asan` before this fix. */
/* ---- TDI address structures (tdi.h) --------------------------------- */
#define TDI_ADDRESS_TYPE_IP 2

typedef struct _TDI_ADDRESS_IP {
	unsigned short sin_port;
	uint32_t in_addr;
	unsigned char sin_zero[8];
} TDI_ADDRESS_IP;

typedef struct _TA_ADDRESS {
	unsigned short AddressLength;
	unsigned short AddressType;
	unsigned char Address[14]; /* big enough for one TDI_ADDRESS_IP */
} TA_ADDRESS;

typedef struct _TRANSPORT_ADDRESS {
	int32_t TAAddressCount;
	TA_ADDRESS Address[1];
} TRANSPORT_ADDRESS;

/* ---- the EA buffer NtCreateFile takes (a generic NT structure, not
 * AFD-specific, but this is the only place ntlibc needs it -- field
 * layout confirmed by ReactOS's WSPSocket use of EaName/EaNameLength/
 * EaValueLength, dllmain.c around line 256). ------------------------- */
typedef struct _FILE_FULL_EA_INFORMATION {
	uint32_t NextEntryOffset;
	unsigned char Flags;
	unsigned char EaNameLength;
	unsigned short EaValueLength;
	char EaName[1]; /* EaNameLength bytes + a NUL, then EaValueLength bytes of value */
} FILE_FULL_EA_INFORMATION;

/* ---- socket creation (shared.h: AFD_CREATE_PACKET, AfdCommand;
 * dllmain.c's WSPSocket around ReactOS's own line 243/347 for the
 * NtCreateFile/EA recipe) ---------------------------------------------
 *
 * The transport device name for AF_INET/SOCK_STREAM: "\Device\Tcp".
 * Confirmed independently of ReactOS by Mateusz Lewczak's "Under the
 * Hood of AFD.sys" reverse-engineering series
 * (https://leftarcode.com/posts/afd-reverse-engineering-part1/, which
 * also confirms the "\Device\Afd\Endpoint" open path and the
 * "AfdOpenPacketXX" EA name below) -- ReactOS's driver-side handler
 * (drivers/network/afd/afd/main.c) reads only EndpointFlags,
 * SizeOfTransportName and TransportName out of the EA payload, nothing
 * else, so no separate AF/type/protocol fields are needed here: which
 * protocol stack answers is selected entirely by which transport
 * device name is opened. */
/* Spelled as WCHAR-array initializer lists, not L"..." literals: a wide
 * string literal's element type is the compiler's native wchar_t,
 * which is a 32-bit int on a non-Windows-targeting compiler (confirmed
 * against this project's own `make lint` native-gcc/clang pass, which
 * is not a Windows-targeting compiler on purpose -- src/internal/nt.h's
 * WCHAR is `unsigned short`, so `static const WCHAR x[] = L"..."` is a
 * real array-element type mismatch there, not just a lint nit).  This
 * project's few existing L"..." uses (src/signal/signal.c etc.) all
 * hand a wide literal straight to RtlInitUnicodeString()/similar as a
 * pointer, which only warns on the pointee-type mismatch; this file
 * needs an actual WCHAR[], so it sidesteps the literal instead. */
#define AFD_TRANSPORT_TCP { '\\','D','e','v','i','c','e','\\','T','c','p', 0 }
#define AFD_ENDPOINT_DEVICE { '\\','D','e','v','i','c','e','\\','A','f','d','\\','E','n','d','p','o','i','n','t', 0 }

#define AFD_EA_NAME "AfdOpenPacketXX"
#define AFD_EA_NAME_LEN 15 /* strlen(AFD_EA_NAME), not counting the NUL EaName itself is stored with */

typedef struct _AFD_CREATE_PACKET {
	uint32_t EndpointFlags;
	uint32_t GroupID;
	uint32_t SizeOfTransportName;
	WCHAR TransportName[1];
} AFD_CREATE_PACKET;

/* ---- bind (shared.h: AFD_BIND_DATA, AFD_SHARE_*) --------------------- */
#define AFD_SHARE_UNIQUE    0
#define AFD_SHARE_REUSE     1
#define AFD_SHARE_WILDCARD  2
#define AFD_SHARE_EXCLUSIVE 3

typedef struct _AFD_BIND_DATA {
	uint32_t ShareType;
	TRANSPORT_ADDRESS Address;
} AFD_BIND_DATA;

/* ---- listen / accept -------------------------------------------------- */
typedef struct _AFD_LISTEN_DATA {
	unsigned char UseSAN;
	uint32_t Backlog;
	unsigned char UseDelayedAcceptance;
} AFD_LISTEN_DATA;

typedef struct _AFD_RECEIVED_ACCEPT_DATA {
	uint32_t SequenceNumber;
	TRANSPORT_ADDRESS Address;
} AFD_RECEIVED_ACCEPT_DATA;

typedef struct _AFD_ACCEPT_DATA {
	uint32_t UseSAN;
	uint32_t SequenceNumber;
	HANDLE ListenHandle; /* the *new* socket handle the connection lands on */
} AFD_ACCEPT_DATA;

/* ---- connect (shared.h: AFD_CONNECT_INFO) ---------------------------- */
typedef struct _AFD_CONNECT_INFO {
	unsigned char UseSAN;
	uint32_t Root;
	uint32_t Unknown;
	TRANSPORT_ADDRESS RemoteAddress;
} AFD_CONNECT_INFO;

/* ---- send / recv (shared.h: AFD_WSABUF, AFD_RECV_INFO, AFD_SEND_INFO,
 * AFD_SKIP_FIO/AFD_OVERLAPPED/AFD_IMMEDIATE; tdi.h: TDI_RECEIVE_*) ----- */
typedef struct _AFD_WSABUF {
	unsigned int len;
	char *buf;
} AFD_WSABUF;

#define AFD_IMMEDIATE 0x4UL

#define TDI_RECEIVE_PARTIAL   0x00000010UL
#define TDI_RECEIVE_NORMAL    0x00000020UL
#define TDI_RECEIVE_EXPEDITED 0x00000040UL
#define TDI_RECEIVE_PEEK      0x00000080UL
#define TDI_SEND_EXPEDITED    0x00000020UL

typedef struct _AFD_RECV_INFO {
	AFD_WSABUF *BufferArray;
	uint32_t BufferCount;
	uint32_t AfdFlags;
	uint32_t TdiFlags;
} AFD_RECV_INFO;

typedef struct _AFD_SEND_INFO {
	AFD_WSABUF *BufferArray;
	uint32_t BufferCount;
	uint32_t AfdFlags;
	uint32_t TdiFlags;
} AFD_SEND_INFO;

/* ---- disconnect (shutdown()) ------------------------------------------ */
#define AFD_DISCONNECT_SEND 0x01UL
#define AFD_DISCONNECT_RECV 0x02UL
#define AFD_DISCONNECT_ABORT 0x04UL

typedef struct _AFD_DISCONNECT_INFO {
	uint32_t DisconnectType;
	LARGE_INTEGER Timeout;
} AFD_DISCONNECT_INFO;

/* ---- select/poll (shared.h: AFD_POLL_INFO/AFD_HANDLE, AFD_EVENT_*;
 * dllmain.c's WSPSelect for the fd_set-bit -> AFD_EVENT_* mapping this
 * project's __fd_probe()/select()/poll() reuse) ------------------------ */
#define AFD_EVENT_RECEIVE      0x0001UL
#define AFD_EVENT_OOB_RECEIVE  0x0002UL
#define AFD_EVENT_SEND         0x0004UL
#define AFD_EVENT_DISCONNECT   0x0008UL
#define AFD_EVENT_ABORT        0x0010UL
#define AFD_EVENT_CLOSE        0x0020UL
#define AFD_EVENT_CONNECT      0x0040UL
#define AFD_EVENT_ACCEPT       0x0080UL
#define AFD_EVENT_CONNECT_FAIL 0x0100UL

/* readfds: a read, a hangup or a pending accept would all not block. */
#define AFD_POLL_READ_BITS (AFD_EVENT_RECEIVE | AFD_EVENT_DISCONNECT | AFD_EVENT_ABORT | AFD_EVENT_CLOSE | AFD_EVENT_ACCEPT)
/* writefds: a send, or an in-progress connect() finishing, would not block. */
#define AFD_POLL_WRITE_BITS (AFD_EVENT_SEND | AFD_EVENT_CONNECT | AFD_EVENT_CONNECT_FAIL)

typedef struct _AFD_HANDLE {
	HANDLE Handle;
	uint32_t Events;
	NTSTATUS Status;
} AFD_HANDLE;

typedef struct _AFD_POLL_INFO {
	LARGE_INTEGER Timeout;
	uint32_t HandleCount;
	ULONG_PTR Exclusive;
	AFD_HANDLE Handles[1];
} AFD_POLL_INFO;

/* ---- helpers shared by src/socket/ (every .c there) (src/socket/afdsupport.c) ------ */
/* Open a fresh AFD endpoint handle for AF_INET/SOCK_STREAM: the guts of
 * socket() and of the new handle accept() installs for an incoming
 * connection.  Returns 0, or -1 with errno. */
int __afd_open(HANDLE *out);
/* Issue one AFD ioctl on socket handle h and wait for it to finish --
 * every AFD request (src/socket/ (every .c there)) goes through this.  STATUS_PENDING
 * is waited out on the handle itself (see __afd_open()'s comment on why
 * that works here instead of ReactOS's per-call NtCreateEvent).
 * *io_out, if not NULL, receives the final IO_STATUS_BLOCK (its
 * Information field is what bind()'s TdiAddressHandle-style results and
 * recv()/send()'s byte counts come from). */
NTSTATUS __afd_ioctl(HANDLE h, ULONG code, void *in, ULONG inlen, void *out, ULONG outlen, IO_STATUS_BLOCK *io_out);
/* sockaddr_in -> TRANSPORT_ADDRESS, validating family/length.  Returns
 * 0, or -1 with errno=EINVAL/EAFNOSUPPORT. */
/* socklen_t is `unsigned` (include/alltypes.h.in) -- spelled that way
 * here, not with <sys/socket.h>'s typedef, so this header (pulled into
 * src/select/select.c, which has no reason to include <sys/socket.h>)
 * stays self-contained. */
struct sockaddr;
int __afd_addr_from_sockaddr(const struct sockaddr *addr, unsigned len, TRANSPORT_ADDRESS *out);
/* TA_ADDRESS (as embedded in a TRANSPORT_ADDRESS) -> sockaddr_in,
 * truncating into *addr and *len the way accept()/recvfrom() are specified
 * to. */
void __afd_addr_to_sockaddr(const TA_ADDRESS *ta, struct sockaddr *addr, unsigned *len);

/* Per-socket state bits, stashed in struct __fd's otherwise-unused `pad`
 * byte for __FD_SOCKET descriptors only (test/networking-audit.md sec 2:
 * "dirflag and pos are meaningless for a socket and can stay zero/-1" --
 * true of pos, but bind()/listen()/connect() do need to remember a
 * little state across calls, and pad is the other field that sits
 * unused for every non-directory type already). */
#define AFD_ST_BOUND      0x01
#define AFD_ST_LISTENING  0x02
#define AFD_ST_CONNECTED  0x04
#define AFD_ST_REUSEADDR  0x08

#endif
