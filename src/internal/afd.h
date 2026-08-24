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
/* ---- TDI address structures (tdi.h) ---------------------------------
 *
 * tdi.h declares TA_ADDRESS and TRANSPORT_ADDRESS at default alignment:
 *
 *      typedef struct _TA_ADDRESS {
 *        USHORT AddressLength;   +0
 *        USHORT AddressType;     +2
 *        UCHAR  Address[1];      +4
 *      } TA_ADDRESS;
 *      typedef struct _TRANSPORT_ADDRESS {
 *        LONG       TAAddressCount;  +0
 *        TA_ADDRESS Address[1];      +4
 *      } TRANSPORT_ADDRESS;
 *
 * so the address bytes of the first (and here only) TA_ADDRESS begin at
 * +12 of an AFD_BIND_DATA and are TDI_ADDRESS_LENGTH_IP long.
 *
 * *** TDI_ADDRESS_IP is deliberately NOT declared as a struct here. ***
 *
 * In tdi.h it sits inside `#include "pshpack1.h"` ... `#include
 * "poppack.h"` -- /usr/share/mingw-w64/include/tdi.h lines 388 and 582
 * bracket it -- so it is packed to 1 and
 * TDI_ADDRESS_LENGTH_IP == sizeof(TDI_ADDRESS_IP) == 14:
 *
 *      USHORT sin_port;     +0
 *      ULONG  in_addr;      +2   <- unaligned; only pack(1) puts it here
 *      UCHAR  sin_zero[8];  +6
 *
 * Transcribing those three fields as an ordinary C struct (which this
 * header did until this commit) makes the compiler insert two bytes of
 * padding after sin_port to align in_addr, giving in_addr at +4,
 * sin_zero at +8 and sizeof() == 16.  Every byte from +2 on is then
 * wrong on the wire, and AddressLength is declared as 16 rather than
 * 14.  That is the same defect class as the 12-vs-24-byte open packet
 * this header carried one commit ago, and it is what bind() was
 * tripping over -- see the AFD_BIND_DATA banner below.
 *
 * Rather than re-introduce the struct with an __attribute__((packed))
 * that four different compilers (mingw gcc, tcc, and the native gcc and
 * clang of `make lint`/`make asan`) would each have to honour, the
 * address payload is written and read through named byte offsets, the
 * way this header already handles the EA buffer.  That is also what
 * both reference clients do: neither ReactOS's WSPBind
 * (dll/win32/msafd/misc/dllmain.c) nor the layout phnt documents ever
 * instantiates a TDI_ADDRESS_IP -- they copy the 14 bytes of
 * `sockaddr.sa_data` verbatim.
 *
 * The rule they both encode, and which the offsets below implement:
 * TA_ADDRESS.AddressType overlays sockaddr.sa_family and
 * TA_ADDRESS.Address overlays sockaddr.sa_data, so
 *
 *      AddressLength == sockaddr length - sizeof(sa_family) == 14
 *
 * for a sockaddr_in.  Sources, in agreement for once:
 *
 *   - ReactOS dll/win32/msafd/misc/dllmain.c, WSPBind():
 *       BindData->Address.Address[0].AddressLength =
 *           SocketAddressLength - sizeof(SocketAddress->sa_family);
 *       BindData->Address.Address[0].AddressType = SocketAddress->sa_family;
 *       RtlCopyMemory(BindData->Address.Address[0].Address,
 *                     SocketAddress->sa_data, ...);
 *   - System Informer phnt, ntafd.h, the AFD_ADDRESS union: its
 *     `TdiAddressUnpacked` arm is `UCHAR Padding[10]` followed by a
 *     SOCKADDR_STORAGE, with the comment
 *     "RTL_SIZEOF_THROUGH_FIELD(TDI_ADDRESS_INFO, Address.Address[0].AddressLength)"
 *     and an ASCII diagram showing SOCKADDR's sa_family sitting exactly
 *     on TA_ADDRESS's AddressType.  10 == 4 (ActivityCount) + 4
 *     (TAAddressCount) + 2 (AddressLength), i.e. the embedded sockaddr
 *     starts at AddressType.
 *   - mingw-w64 tdi.h, for the pack(1) that makes the count 14.
 */
#define TDI_ADDRESS_TYPE_IP 2

/* sizeof(TDI_ADDRESS_IP) with tdi.h's pack(1) in force, which is what
 * TA_ADDRESS.AddressLength must carry for an AF_INET address -- and,
 * equivalently, sizeof(struct sockaddr_in) - sizeof(sa_family_t). */
#define TDI_ADDRESS_LENGTH_IP 14

/* Field offsets within those 14 bytes.  Network byte order throughout;
 * they are copied out of sockaddr_in unchanged. */
#define TDI_IP_OFF_PORT 0
#define TDI_IP_OFF_ADDR 2
#define TDI_IP_OFF_ZERO 6
#define TDI_IP_ZERO_LEN 8

typedef struct _TA_ADDRESS {
	unsigned short AddressLength;
	unsigned short AddressType;
	unsigned char Address[TDI_ADDRESS_LENGTH_IP]; /* exactly one packed TDI_ADDRESS_IP */
} TA_ADDRESS;

typedef struct _TRANSPORT_ADDRESS {
	int32_t TAAddressCount;
	TA_ADDRESS Address[1];
} TRANSPORT_ADDRESS;

/* TDI_ADDRESS_INFO (tdi.h): what IOCTL_AFD_BIND writes *back* in TDI
 * mode -- a ULONG ActivityCount followed by a TRANSPORT_ADDRESS.  For
 * one AF_INET address that is 4 + 4 + 2 + 2 + 14 == 26 bytes, which is
 * two bytes *more* than sizeof(AFD_BIND_DATA)'s TRANSPORT_ADDRESS
 * payload, so the reply cannot be read back into the request buffer the
 * way ReactOS does it (its
 * FIELD_OFFSET(AFD_BIND_DATA, Address.Address[SocketAddressLength])
 * indexes the TA_ADDRESS *array*, wildly over-allocating and hiding the
 * question).  src/socket/bind.c gives the reply its own buffer. */
#define AFD_TDI_ADDRESS_INFO_SIZE_IP (4 + 4 + 2 + 2 + TDI_ADDRESS_LENGTH_IP)

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

/* The header size that matters on the wire is offsetof(..., EaName) == 8,
 * *not* sizeof(FILE_FULL_EA_INFORMATION).  sizeof() is 12: it counts the
 * EaName[1] placeholder byte and then rounds the whole thing up to the
 * 4-byte alignment NextEntryOffset forces.  Sizing a buffer with sizeof()
 * therefore overstates the entry by 3 bytes and leaves the declared total
 * length 4-misaligned -- see __afd_open()'s comment and
 * test/posix-socket-ea.c, which asserts the exact-fit arithmetic.
 *
 * The invariants NT's own validator (ReactOS ntoskrnl/io/iomgr/util.c,
 * IoCheckEaBufferValidity(), an @implemented reimplementation of the
 * kernel's) enforces on this buffer, transcribed here because
 * test/posix-socket-ea.c checks each one by name:
 *
 *   ComputedLength = EaValueLength + EaNameLength
 *                    + FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + 1
 *
 *   - EaLength must be >= FIELD_OFFSET(..., EaName), and >= ComputedLength;
 *   - EaName[EaNameLength] must be ANSI_NULL -- so EaNameLength excludes
 *     the terminator but the terminator must still be in the buffer;
 *   - for a non-final entry, NextEntryOffset must equal
 *     ALIGN_UP_BY(ComputedLength, sizeof(ULONG));
 *   - for the final entry, NextEntryOffset must be 0. */
#define AFD_EA_HEADER_SIZE 8 /* offsetof(FILE_FULL_EA_INFORMATION, EaName) */

/* ---- socket creation (shared.h: AFD_CREATE_PACKET, AfdCommand;
 * dllmain.c's WSPSocket around ReactOS's own line 243/347 for the
 * NtCreateFile/EA recipe) ---------------------------------------------
 *
 * The transport device name for AF_INET/SOCK_STREAM: "\Device\Tcp".
 * Confirmed independently of ReactOS by Mateusz Lewczak's "Under the
 * Hood of AFD.sys" reverse-engineering series
 * (https://leftarcode.com/posts/afd-reverse-engineering-part1/, which
 * also confirms the "\Device\Afd\Endpoint" open path and the
 * "AfdOpenPacketXX" EA name below).
 *
 * *** The two sources disagree on the EA value's own layout, and only
 * one of them describes the driver CI actually runs against. ***
 *
 * ReactOS's client (dll/win32/msafd/misc/dllmain.c) fills an
 * AFD_CREATE_PACKET (sdk/include/reactos/drivers/afd/shared.h):
 *
 *      +0  DWORD EndpointFlags
 *      +4  DWORD GroupID
 *      +8  DWORD SizeOfTransportName
 *     +12  WCHAR TransportName[]
 *
 * -- a 12-byte header, no address-family/type/protocol fields, because
 * ReactOS's *own* afd.sys (drivers/network/afd/afd/main.c, AfdCreate())
 * reads only those three fields back out.  That is the NT4/2000-era
 * shape, and it is self-consistent for ReactOS.
 *
 * Real Windows' afd.sys does not use it.  Since Vista the EA value is
 * an AFD_OPEN_PACKET with a *24-byte* header carrying three extra LONGs
 * between GroupID and the name length:
 *
 *      +0  ULONG EndpointFlags          (AFD_ENDPOINT_FLAGS)
 *      +4  ULONG GroupID                (GROUP)
 *      +8  LONG  AddressFamily          AF_*
 *     +12  LONG  SocketType             SOCK_*
 *     +16  LONG  Protocol               IPPROTO_*
 *     +20  ULONG TransportDeviceNameLength   -- in BYTES, not characters
 *     +24  WCHAR TransportDeviceName[]  UTF-16, not NUL-counted
 *
 * Two independent reverse-engineering efforts agree on that 24-byte
 * shape, against ReactOS's 12:
 *
 *   - System Informer's phnt headers, ntafd.h: `AFD_OPEN_PACKET` and
 *     the `AFD_OPEN_PACKET_FULL_EA` convenience wrapper right below it
 *     (which also pins EaName to `CHAR EaName[sizeof(AfdOpenPacket)]`,
 *     i.e. 16 bytes -- 15 plus the NUL -- putting the value at +24 of
 *     the EA entry).  `TransportDeviceNameLength` is annotated
 *     `_Field_size_bytes_opt_()`, which is what settles bytes-vs-chars.
 *   - Lewczak's series (part 1, "AFD_OPEN_PACKET_EA"), which lists
 *     endpointFlags, groupID, addressFamily, socketType, protocol,
 *     sizeOfTransportName in exactly that order after a 0x10-byte
 *     eaName.
 *
 * Using ReactOS's 12-byte layout against real Windows puts the UTF-16
 * device name where afd.sys expects SocketType/Protocol/
 * TransportDeviceNameLength.  afd.sys then reads a *name length* out of
 * the middle of the name text -- for "\Device\Tcp" the WCHARs at +20
 * are 'i','c', so the length reads back as 0x00630069 == 6488169
 * (~6.2 MB) -- and walks that far past the end of a 67-byte buffer
 * (observed directly: reintroducing the old layout makes
 * test/posix-socket-ea.c print exactly that number).  That is the
 * STATUS_ACCESS_VIOLATION (-> EFAULT) socket() returned on the CI
 * windows-test legs while this header carried the ReactOS shape.  Wine
 * cannot see it: Wine's AFD is its own implementation and never parses
 * this packet.
 *
 * phnt notes that leaving TransportDeviceNameLength at 0 (no device
 * name) selects "TLI" transport mode, in which AFD's bind/connect/
 * accept ioctls take SOCKADDR rather than the TDI_ADDRESS_INFO /
 * TRANSPORT_ADDRESS structures the rest of this header is built on.
 * This code therefore keeps naming "\Device\Tcp", which selects TDI
 * (or hybrid) mode and keeps the structures below correct.  If Server
 * 2025 has finally retired the \Device\Tcp TDI stub, the open will
 * fail with a name-lookup status (ENOENT) rather than EFAULT -- a
 * distinguishable next signal, and the point at which converting the
 * whole file to the _TL structures becomes the fix. */
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

/* Every field spelled as a fixed-width type, for the LP64-vs-LLP64
 * reason in this header's banner: `make asan` compiles it natively. */
typedef struct _AFD_OPEN_PACKET {
	uint32_t EndpointFlags;
	uint32_t GroupID;
	int32_t AddressFamily;
	int32_t SocketType;
	int32_t Protocol;
	uint32_t TransportDeviceNameLength; /* bytes, excluding any NUL */
	WCHAR TransportDeviceName[1];
} AFD_OPEN_PACKET;

/* offsetof(AFD_OPEN_PACKET, TransportDeviceName).  Named separately for
 * the same reason AFD_EA_HEADER_SIZE is: sizeof(AFD_OPEN_PACKET) is 28,
 * not 24 -- it counts the TransportDeviceName[1] placeholder and rounds
 * up -- so sizeof() would overstate the value by 4 bytes. */
#define AFD_OPEN_PACKET_HEADER_SIZE 24

/* ---- bind (shared.h: AFD_BIND_DATA, AFD_SHARE_*) --------------------- */
#define AFD_SHARE_UNIQUE    0
#define AFD_SHARE_REUSE     1
#define AFD_SHARE_WILDCARD  2
#define AFD_SHARE_EXCLUSIVE 3

typedef struct _AFD_BIND_DATA {
	uint32_t ShareType;
	TRANSPORT_ADDRESS Address;
} AFD_BIND_DATA;

/* The IOCTL_AFD_BIND *request* as it appears on the wire, by offset.
 * Named separately from AFD_BIND_DATA for the same reason
 * AFD_EA_HEADER_SIZE is named separately from
 * sizeof(FILE_FULL_EA_INFORMATION): sizeof(AFD_BIND_DATA) is 28, two
 * bytes more than the 26 the request actually occupies, because
 * TAAddressCount's 4-byte alignment rounds the tail up.  Declaring 28
 * would declare two bytes the request does not describe.
 *
 *      +0   ULONG  ShareType         AFD_SHARE_*
 *      +4   LONG   TAAddressCount    == 1
 *      +8   USHORT AddressLength     == TDI_ADDRESS_LENGTH_IP == 14
 *      +10  USHORT AddressType       == TDI_ADDRESS_TYPE_IP == AF_INET == 2
 *      +12  USHORT sin_port          network order
 *      +14  ULONG  in_addr           network order  <- NOT +16; see the
 *      +18  UCHAR  sin_zero[8]                         TDI banner above
 *      == 26
 *
 * Note +10 onwards is byte-for-byte a `struct sockaddr_in`: AddressType
 * is its sa_family and the 14 address bytes are its sa_data.  That is
 * the invariant phnt's AFD_ADDRESS diagram draws and the one
 * test/posix-socket-bind.c asserts.
 *
 * IOCTL_AFD_BIND is METHOD_NEITHER (phnt ntafd.h: 0x12003), so afd.sys
 * sees the caller's buffer and its declared length directly; the length
 * is the only thing bounding how much of the address it reads. */
#define AFD_BIND_REQ_OFF_SHARE_TYPE   0
#define AFD_BIND_REQ_OFF_ADDR_COUNT   4
#define AFD_BIND_REQ_OFF_ADDR_LENGTH  8
#define AFD_BIND_REQ_OFF_ADDR_TYPE   10
#define AFD_BIND_REQ_OFF_ADDR        12
#define AFD_BIND_REQ_SIZE (AFD_BIND_REQ_OFF_ADDR + TDI_ADDRESS_LENGTH_IP)

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
/* socklen_t is `unsigned` (include/alltypes.h.in) -- spelled that way
 * here, not with <sys/socket.h>'s typedef, so this header (pulled into
 * src/select/select.c, which has no reason to include <sys/socket.h>)
 * stays self-contained. */
struct sockaddr;
/* Open a fresh AFD endpoint handle for AF_INET/SOCK_STREAM: the guts of
 * socket() and of the new handle accept() installs for an incoming
 * connection.  Returns 0, or -1 with errno. */
int __afd_open(HANDLE *out);
/* The AfdOpenPacketXX EA buffer __afd_open() hands NtCreateFile, split
 * out so it can be inspected without a device: __afd_open_ea_size()
 * returns the exact byte count (no slack -- see AFD_EA_HEADER_SIZE),
 * and __afd_build_open_ea() fills that many bytes at `buf`.  buf must
 * be at least 4-byte aligned, which is what NT's own EA validator
 * requires of the whole entry.  test/posix-socket-ea.c re-parses the
 * result and asserts every invariant NT checks; it is the only reason
 * these are separate functions, and it runs on hosts with no working
 * \Device\Afd at all. */
unsigned long __afd_open_ea_size(void);
void __afd_build_open_ea(void *buf);
/* The IOCTL_AFD_BIND request body, split out for exactly the same
 * reason and inspected exactly the same way: __afd_bind_request_size()
 * returns the byte count the ioctl declares (26, not
 * sizeof(AFD_BIND_DATA)), and __afd_build_bind_request() fills that
 * many bytes at `buf` from a sockaddr, returning 0, or -1 with
 * errno=EINVAL/EAFNOSUPPORT for an address this project cannot express.
 * buf must be 4-byte aligned and at least sizeof(AFD_BIND_DATA) bytes
 * (the two bytes of tail slack are not written).
 * test/posix-socket-bind.c re-parses the result by offset with no
 * reference to this header, and runs with no \Device\Afd at all. */
unsigned long __afd_bind_request_size(void);
int __afd_build_bind_request(void *buf, unsigned long share_type,
                             const struct sockaddr *addr, unsigned len);
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
