/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Structural invariants of the IOCTL_AFD_WAIT_FOR_LISTEN *reply*, and
 * the regression assertion for the way src/socket/accept.c used to read
 * one.  Fifth of the device-free set (test/posix-socket-ea.c, -bind.c,
 * -connect.c, -poll.c): it opens no socket and touches no device, so it
 * runs identically on a host with no \Device\Afd, under Wine, under
 * `make asan` natively, and on CI's real-Windows legs.
 *
 * That property is not a nicety here.  The live accept() path runs
 * nowhere local at all -- Wine's AFD rejects this project's portable
 * open path, so test/posix-socket.c and friends exit 77 in every local
 * environment -- and on real Windows it needs an actual inbound
 * connection to reach.  A synthetic reply is the only coverage this
 * defect can have before someone meets it in the field.
 *
 * *** The defect this is the regression assertion for. ***
 *
 * accept() did
 *
 *     AFD_RECEIVED_ACCEPT_DATA recvd;                    // uninitialised
 *     st = __afd_ioctl(f->h, IOCTL_AFD_WAIT_FOR_LISTEN, 0, 0,
 *                      &recvd, sizeof(recvd), 0);
 *     ...
 *     if (addr) __afd_addr_to_sockaddr(&recvd.Address.Address[0], addr, len);
 *
 * -- Address[0] indexed with no reference to Address.TAAddressCount, out
 * of a buffer nothing had written.
 *
 * IOCTL_AFD_WAIT_FOR_LISTEN is METHOD_BUFFERED and its buffer is
 * out-only.  The I/O manager copies back exactly IoStatus.Information
 * bytes and leaves the rest of the caller's buffer alone, and
 * AfdWaitForListen() (the AFD driver's own listen.c, Copyright (c) 1989
 * Microsoft Corporation) declares only what it moved in:
 *
 *     Irp->IoStatus.Information =
 *         sizeof(*listenResponse) - sizeof(TRANSPORT_ADDRESS) +
 *             connection->RemoteAddressLength;
 *
 * So the tail past the copy-back is whatever the caller left there.  For
 * IOCTL_AFD_SELECT (2edaa0d) that was the caller's own request -- wrong,
 * but the caller's own bytes.  Here there is no request underneath, and
 * it is uninitialised stack.
 *
 * *** Why the count check is not, by itself, the fix. ***
 *
 * Case 1 below is the point of this file.  Over an *unzeroed* buffer a
 * short copy-back leaves stack bytes in TAAddressCount and AddressLength
 * too, and stack bytes are not conveniently zero: any plausible junk
 * passes "count >= 1" and "AddressLength >= 14" and the address is
 * handed out anyway.  No field check can rescue an uninitialised buffer,
 * because the fields are part of what is uninitialised.  Zeroing before
 * the ioctl is what turns the tail into zeros; the count check is what
 * then rejects them.  Neither works alone.
 *
 * Only one of the two halves is *enforced* here, and the distinction
 * matters when reading a pass:
 *
 *   - The count/length checks are enforced.  Deleting them from
 *     __afd_accept_reply_addr() fails nine assertions in this file
 *     (cases 2, 3, 4, 5 and the exact-allocation case).
 *   - The memset in accept.c is only *demonstrated*, by case 1.  This
 *     file opens no device, so it never calls accept(); deleting that
 *     memset leaves every assertion here passing.  Case 1 shows what an
 *     unzeroed buffer would yield -- junk that clears every field check
 *     there is -- which is the argument for the memset, not a guard on
 *     it.  Nothing local can guard it: reaching accept() needs a real
 *     inbound connection on a real AFD.
 *
 * Both facts were measured, not assumed.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int fails;

#define CHECK(cond, what) do { \
	if (!(cond)) { fails++; printf("FAIL %s (%s:%d)\n", (what), __FILE__, __LINE__); } \
} while (0)
/* Return codes are compared with this rather than CHECK_EQ: 0 vs -1 is
 * the whole point of the file, and CHECK_EQ's unsigned long would print
 * the -1 as 4294967295. */
#define CHECK_RC(got, want, what) do { \
	int g_ = (int)(got), w_ = (int)(want); \
	if (g_ != w_) { fails++; \
		printf("FAIL %s = %d, want %d (%s:%d)\n", (what), g_, w_, __FILE__, __LINE__); } \
} while (0)
#define CHECK_EQ(got, want, what) do { \
	unsigned long g_ = (unsigned long)(got), w_ = (unsigned long)(want); \
	if (g_ != w_) { fails++; \
		printf("FAIL %s = %lu (0x%lx), want %lu (0x%lx) (%s:%d)\n", \
		       (what), g_, g_, w_, w_, __FILE__, __LINE__); } \
} while (0)

/* src/internal/afd.h's TA_ADDRESS.  Redeclared rather than included --
 * afd.h is a src/internal/ header and test/*.c are built against the
 * public include path only -- and redeclared as a struct rather than
 * passed as void *, so the two translation units really do agree on the
 * parameter type (C99 6.2.7: separately declared structs with the same
 * members are compatible).  The layout claim is asserted below. */
struct ta_address {
	unsigned short AddressLength;
	unsigned short AddressType;
	unsigned char Address[14];
};

/* src/socket/afdsupport.c */
int __afd_accept_reply_addr(const void *reply, struct sockaddr *addr, unsigned *len);
void __afd_addr_to_sockaddr(const struct ta_address *ta, struct sockaddr *addr, unsigned *len);

/* AFD_ACCEPT_RSP_OFF_* / TDI_* from src/internal/afd.h, restated here so
 * the test is an independent statement of the layout rather than a
 * paraphrase of the header it is checking. */
#define RSP_SEQUENCE     ((size_t)0)
#define RSP_ADDR_COUNT   ((size_t)4)
#define RSP_ADDR_LENGTH  ((size_t)8)
#define RSP_ADDR_TYPE    ((size_t)10)
#define RSP_ADDR         ((size_t)12)
#define TDI_LEN_IP       14
#define TDI_TYPE_IP      2
#define RSP_SIZE         (RSP_ADDR + TDI_LEN_IP)   /* 26 */

#define TDI_IP_OFF_PORT 0
#define TDI_IP_OFF_ADDR 2

/* What an uninitialised stack buffer might plausibly hold.  Chosen so
 * that read as a LONG it is a positive count and read as a USHORT it is
 * a length well past 14 -- i.e. junk that sails through every field
 * check there is.  That is the argument of case 1. */
#define POISON 0x5Au

static void put32(unsigned char *p, uint32_t v) { memcpy(p, &v, sizeof v); }
static void put16(unsigned char *p, unsigned short v) { memcpy(p, &v, sizeof v); }
static uint32_t get32(const unsigned char *p) { uint32_t v; memcpy(&v, p, sizeof v); return v; }
static unsigned short get16(const unsigned char *p) { unsigned short v; memcpy(&v, p, sizeof v); return v; }

/* A reply exactly as afd.sys completes one for an AF_INET peer. */
static void build_reply(unsigned char *b, uint32_t seq, int32_t count,
                        unsigned short alen, unsigned short atype,
                        unsigned short port_net, uint32_t addr_net)
{
	put32(b + RSP_SEQUENCE, seq);
	put32(b + RSP_ADDR_COUNT, (uint32_t)count);
	put16(b + RSP_ADDR_LENGTH, alen);
	put16(b + RSP_ADDR_TYPE, atype);
	put16(b + RSP_ADDR + TDI_IP_OFF_PORT, port_net);
	put32(b + RSP_ADDR + TDI_IP_OFF_ADDR, addr_net);
}

/* Everything from AddressType on is byte-for-byte a struct sockaddr_in --
 * the invariant phnt's AFD_ADDRESS diagram draws and the one
 * test/posix-socket-bind.c asserts for the request side. */
static void check_layout(void)
{
	struct sockaddr_in sin;
	unsigned char *p = (unsigned char *)&sin;

	CHECK_EQ(RSP_ADDR_TYPE + 2, RSP_ADDR, "AddressType is immediately before the address bytes");
	CHECK_EQ(RSP_SIZE, 26u, "the reply is 26 bytes on the wire");
	CHECK_EQ(sizeof(struct ta_address), (size_t)(2 + 2 + TDI_LEN_IP),
	         "TA_ADDRESS is 18 bytes, unpadded");
	CHECK_EQ(RSP_SIZE - RSP_ADDR_LENGTH, sizeof(struct ta_address),
	         "the reply's TA_ADDRESS runs to the end of the reply");

	memset(&sin, 0, sizeof sin);
	CHECK_EQ((size_t)((unsigned char *)&sin.sin_port - p), (size_t)2, "sockaddr_in.sin_port at +2");
	CHECK_EQ((size_t)((unsigned char *)&sin.sin_addr - p), (size_t)4, "sockaddr_in.sin_addr at +4");
	CHECK_EQ(TDI_LEN_IP, (int)(sizeof(struct sockaddr_in) - sizeof(sa_family_t)),
	         "AddressLength == sizeof(sockaddr_in) - sizeof(sa_family)");
	CHECK_EQ(TDI_TYPE_IP, AF_INET, "TDI_ADDRESS_TYPE_IP == AF_INET");
}

static void check_reply(void)
{
	unsigned char buf[RSP_SIZE + 16];
	struct sockaddr_in sin;
	struct sockaddr_in poison_sin;
	unsigned len;
	int rc;
	unsigned short port = htons(0x1234);
	uint32_t ip = htonl(0x01020304u);

	memset(&poison_sin, 0xCC, sizeof poison_sin);

	/* --- 1. THE defect, and the reason zeroing is the primary fix.
	 *
	 * The image an *unzeroed* stack buffer holds after a copy-back
	 * that delivered only the four bytes of SequenceNumber: byte 0..3
	 * from the driver, everything after it left over from whatever
	 * used that stack last.  This is bit-for-bit what METHOD_BUFFERED
	 * leaves in an out-only buffer at Information == 4. */
	memset(buf, POISON, sizeof buf);
	put32(buf + RSP_SEQUENCE, 0xDEADBEEFu);

	/* Every field check one could write passes on this image.  That is
	 * the finding, not an aside: junk is not zero. */
	CHECK(get32(buf + RSP_ADDR_COUNT) >= 1u, "poisoned tail: TAAddressCount looks like a valid count");
	CHECK(get16(buf + RSP_ADDR_LENGTH) >= TDI_LEN_IP, "poisoned tail: AddressLength looks valid too");

	/* ...and the pre-fix read hands the junk to the caller as a peer
	 * address.  The raw converter is still exactly what it always was;
	 * what changed is that accept() no longer calls it on a buffer
	 * nobody wrote.  Both halves of the disclosure, spelled out: */
	memset(&sin, 0, sizeof sin);
	len = sizeof sin;
	__afd_addr_to_sockaddr((const struct ta_address *)(buf + RSP_ADDR_LENGTH),
	                       (struct sockaddr *)&sin, &len);
	CHECK_EQ(ntohs(sin.sin_port), 0x5A5Au, "pre-fix read returns stack bytes as the peer port");
	CHECK_EQ(ntohl(sin.sin_addr.s_addr), 0x5A5A5A5Au, "pre-fix read returns stack bytes as the peer address");

	/* --- 2. The fix's first half: the same short copy-back over a
	 * buffer zeroed before the ioctl.  Now the tail is zeros, and the
	 * count check has something to catch. */
	memset(buf, 0, sizeof buf);
	put32(buf + RSP_SEQUENCE, 0xDEADBEEFu);
	CHECK_EQ(get32(buf + RSP_ADDR_COUNT), 0u, "zeroed buffer: a short copy-back leaves TAAddressCount == 0");

	sin = poison_sin;
	len = 7; /* a value the conversion would certainly overwrite */
	rc = __afd_accept_reply_addr(buf, (struct sockaddr *)&sin, &len);
	CHECK_RC(rc, -1, "a reply naming no address is rejected");
	CHECK(memcmp(&sin, &poison_sin, sizeof sin) == 0, "a rejected reply writes nothing to the caller's sockaddr");
	CHECK_EQ(len, 7u, "a rejected reply does not touch *address_len");

	/* --- 3. The IoStatus.Information check, obtained for free.  A
	 * copy-back that delivered the count but stopped before the
	 * address leaves AddressLength zero over a pre-zeroed buffer, and
	 * zero is rejected -- no arithmetic against Information needed. */
	memset(buf, 0, sizeof buf);
	build_reply(buf, 1, 1, 0, 0, 0, 0);
	CHECK_RC(__afd_accept_reply_addr(buf, 0, 0), -1,
	         "a reply truncated before the address is rejected");

	/* A partially-delivered address is caught the same way: the driver
	 * writes AddressLength as part of the address it moves in, so a
	 * length short of 14 never comes from a completed one. */
	memset(buf, 0, sizeof buf);
	build_reply(buf, 1, 1, TDI_LEN_IP - 1, TDI_TYPE_IP, port, ip);
	CHECK_RC(__afd_accept_reply_addr(buf, 0, 0), -1, "AddressLength short of 14 is rejected");

	/* --- 4. Fail closed: an all-zero buffer -- what the caller now
	 * hands the ioctl, and what it still holds if the driver writes
	 * nothing whatever -- is rejected rather than converted. */
	memset(buf, 0, sizeof buf);
	CHECK_RC(__afd_accept_reply_addr(buf, 0, 0), -1, "an all-zero reply is rejected");

	/* --- 5. A count of zero is the documented "nothing here", but the
	 * field is a signed LONG and a negative count is not a count. */
	memset(buf, 0, sizeof buf);
	build_reply(buf, 1, 0, TDI_LEN_IP, TDI_TYPE_IP, port, ip);
	CHECK_RC(__afd_accept_reply_addr(buf, 0, 0), -1, "TAAddressCount == 0 is rejected");
	build_reply(buf, 1, -1, TDI_LEN_IP, TDI_TYPE_IP, port, ip);
	CHECK_RC(__afd_accept_reply_addr(buf, 0, 0), -1, "a negative TAAddressCount is rejected");

	/* --- 6. A reply afd.sys actually completed is still read, and read
	 * correctly. */
	memset(buf, 0, sizeof buf);
	build_reply(buf, 0x11223344u, 1, TDI_LEN_IP, TDI_TYPE_IP, port, ip);
	sin = poison_sin;
	len = sizeof sin;
	rc = __afd_accept_reply_addr(buf, (struct sockaddr *)&sin, &len);
	CHECK_RC(rc, 0, "a well-formed reply is accepted");
	CHECK_EQ(sin.sin_family, AF_INET, "converted family");
	CHECK_EQ(ntohs(sin.sin_port), 0x1234u, "converted port");
	CHECK_EQ(ntohl(sin.sin_addr.s_addr), 0x01020304u, "converted address");
	CHECK_EQ(len, sizeof(struct sockaddr_in), "*address_len is set to the full length");

	/* Validation with no conversion asked for -- what accept() does
	 * before it opens an endpoint -- must not need a sockaddr. */
	CHECK_RC(__afd_accept_reply_addr(buf, 0, 0), 0, "a well-formed reply validates with addr == NULL");

	/* --- 7. accept.html: "If the actual length of the address is
	 * greater than the length of the supplied sockaddr structure, the
	 * stored address shall be truncated", and address_len still
	 * receives the full length. */
	{
		unsigned char small[sizeof(struct sockaddr_in) + 8];
		memset(small, 0xCC, sizeof small);
		len = 4;
		rc = __afd_accept_reply_addr(buf, (struct sockaddr *)small, &len);
		CHECK_RC(rc, 0, "truncating conversion still succeeds");
		CHECK_EQ(len, sizeof(struct sockaddr_in), "truncated: *address_len is the untruncated length");
		CHECK_EQ(small[4], 0xCCu, "truncated: nothing written past the caller's length");
	}

	/* --- 8. A count larger than the one address the reply can hold is
	 * not believed into reading further: only Address[0] is ever read,
	 * so an absurd count is harmless, and the guard bytes past the
	 * 26-byte reply prove nothing was touched beyond it. */
	memset(buf, 0, sizeof buf);
	memset(buf + RSP_SIZE, POISON, sizeof buf - RSP_SIZE);
	build_reply(buf, 1, 0x7FFFFFFF, TDI_LEN_IP, TDI_TYPE_IP, port, ip);
	sin = poison_sin;
	len = sizeof sin;
	CHECK_RC(__afd_accept_reply_addr(buf, (struct sockaddr *)&sin, &len), 0,
	         "an absurd count with a valid first address is still read");
	CHECK_EQ(ntohl(sin.sin_addr.s_addr), 0x01020304u, "absurd count: the first address, not some other slot");
	{
		size_t i;
		for (i = RSP_SIZE; i < sizeof buf; i++)
			CHECK_EQ(buf[i], POISON, "guard byte past the 26-byte reply");
	}
}

/* The same well-formed reply in a heap block sized to exactly the 26
 * bytes the driver declares.  Under `make asan` this is the real check
 * that the interpreter reads no further than the reply: a read at +26
 * is a heap-buffer-overflow, not a quietly-passing guard byte.  Under
 * the cross build it is simply another pass. */
static void check_exact_allocation(void)
{
	unsigned char *b = malloc(RSP_SIZE);
	struct sockaddr_in sin;
	unsigned len = sizeof sin;

	if (!b) { CHECK(0, "malloc"); return; }
	memset(b, 0, RSP_SIZE);
	build_reply(b, 7, 1, TDI_LEN_IP, TDI_TYPE_IP, htons(80), htonl(0x7F000001u));
	CHECK_RC(__afd_accept_reply_addr(b, (struct sockaddr *)&sin, &len), 0,
	         "exact-size reply: accepted");
	CHECK_EQ(ntohs(sin.sin_port), 80u, "exact-size reply: port");
	CHECK_EQ(ntohl(sin.sin_addr.s_addr), 0x7F000001u, "exact-size reply: address");

	memset(b, 0, RSP_SIZE);
	CHECK_RC(__afd_accept_reply_addr(b, 0, 0), -1, "exact-size zeroed reply: rejected");
	free(b);
}

int main(void)
{
	check_layout();
	check_reply();
	check_exact_allocation();

	if (!fails) printf("posix-socket-accept: all tests passed\n");
	return fails != 0;
}
