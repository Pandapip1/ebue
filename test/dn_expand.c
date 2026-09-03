/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * dn_expand(): not POSIX (historical BSD resolver API -- see
 * include/resolv.h's own banner). Checked against RFC 1035 sec 4.1.4's
 * own wire format directly: a name is length-prefixed labels (1-63
 * bytes each) ending in a zero-length root label, or a 2-byte
 * 0xC0-masked pointer redirecting the walk to an earlier point in the
 * same message.  Every message below is a hand-built byte array, not
 * anything sourced from a real resolver, since dn_expand() itself
 * (src/resolv/dn_expand.c) is pure buffer-walking with no OS
 * dependency to exercise.
 */
#include <resolv.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* A name with no compression at all: three ordinary labels and the
 * root label.  RETURN VALUE (this file's own header comment): "the
 * number of bytes dn_expand() itself consumed" -- with no pointer,
 * that is the whole encoded name, terminator included. */
static void test_plain_name(void)
{
	static const unsigned char msg[] = {
		3, 'w', 'w', 'w',
		7, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
		3, 'c', 'o', 'm',
		0
	};
	char out[256];
	int n;

	n = dn_expand(msg, msg + sizeof msg, msg, out, sizeof out);
	CHECK(n == (int)sizeof msg);
	CHECK(strcmp(out, "www.example.com") == 0);
}

/* A backward-pointing compression pointer: "example.com" spelled out
 * once at offset 0, and a second name "www" + a pointer back to it,
 * the exact same-suffix-shared-by-every-answer-RR case RFC 1035 sec
 * 4.1.4 introduces compression for.  The consumed count must cover
 * only the second name's own bytes (the label plus the 2-byte
 * pointer), per this file's header comment: "NOT counting anything
 * read after following a pointer". */
static void test_backward_pointer(void)
{
	static const unsigned char msg[] = {
		/* offset 0: "example.com" */
		7, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
		3, 'c', 'o', 'm',
		0,
		/* offset 13: "www" + pointer to offset 0 */
		3, 'w', 'w', 'w',
		0xC0, 0x00
	};
	const unsigned char *second = msg + 13;
	char out[256];
	int n;

	n = dn_expand(msg, msg + sizeof msg, second, out, sizeof out);
	CHECK(n == 6);   /* the "www" label (4 bytes) plus the pointer (2) */
	CHECK(strcmp(out, "www.example.com") == 0);

	/* The pointer's own target, decoded directly, is unaffected. */
	n = dn_expand(msg, msg + sizeof msg, msg, out, sizeof out);
	CHECK(n == 13);
	CHECK(strcmp(out, "example.com") == 0);
}

/* RFC 1035 sec 4.1.4's own stated hazard, and src/resolv/dn_expand.c's
 * own comment naming it directly: "a pointer's target is not required
 * to lie strictly before its own position".  A pointer at offset 4
 * that jumps forward to offset 7 must be followed correctly, not
 * rejected merely for pointing ahead of itself. */
static void test_forward_pointer(void)
{
	static const unsigned char msg[] = {
		3, 'f', 'o', 'o',        /* offset 0: label "foo" */
		0xC0, 0x07,               /* offset 4: pointer -> offset 7 */
		0,                         /* offset 6: unread filler */
		3, 'b', 'a', 'r',          /* offset 7: label "bar" */
		0                           /* offset 11: root label */
	};
	char out[256];
	int n;

	n = dn_expand(msg, msg + sizeof msg, msg, out, sizeof out);
	CHECK(n == 6);   /* the "foo" label (4 bytes) plus the pointer (2) */
	CHECK(strcmp(out, "foo.bar") == 0);
}

/* A self-referential pointer chain must terminate with an error rather
 * than loop forever -- src/resolv/dn_expand.c's own comment: "Bounding
 * the number of jumps ... is what keeps a cyclic chain of pointers
 * from looping forever". */
static void test_pointer_loop_rejected(void)
{
	static const unsigned char msg[] = { 0xC0, 0x00 };  /* points at itself */
	char out[256];

	CHECK(dn_expand(msg, msg + sizeof msg, msg, out, sizeof out) == -1);
}

/* Malformed input, each rejected with -1 per this file's own header
 * comment ("Returns -1 on any malformed input"): the two reserved
 * label-length bits (0x40/0x80, RFC 1035's "the rest of the octet is
 * reserved"), a label claiming more bytes than remain before eomorig,
 * and a truncated pointer with only one byte left in the message. */
static void test_malformed(void)
{
	char out[256];

	{
		static const unsigned char msg[] = { 0x40, 'x', 0 };
		CHECK(dn_expand(msg, msg + sizeof msg, msg, out, sizeof out) == -1);
	}
	{
		static const unsigned char msg[] = { 0x80, 'x', 0 };
		CHECK(dn_expand(msg, msg + sizeof msg, msg, out, sizeof out) == -1);
	}
	{
		/* label claims 10 bytes but only 2 remain in the message */
		static const unsigned char msg[] = { 10, 'a', 'b' };
		CHECK(dn_expand(msg, msg + sizeof msg, msg, out, sizeof out) == -1);
	}
	{
		/* a pointer's second byte is past eomorig */
		static const unsigned char msg[] = { 0xC0 };
		CHECK(dn_expand(msg, msg + sizeof msg, msg, out, sizeof out) == -1);
	}
	{
		/* comp_dn itself already at or past eomorig */
		static const unsigned char msg[] = { 0 };
		CHECK(dn_expand(msg, msg, msg, out, sizeof out) == -1);
	}
}

/* A destination buffer too small for the decoded name must fail
 * cleanly (-1) rather than overflow it -- this file's own header
 * comment: "-1 ... if exp_dn/length is too small to hold the decoded
 * name." */
static void test_output_buffer_too_small(void)
{
	static const unsigned char msg[] = {
		3, 'w', 'w', 'w', 3, 'c', 'o', 'm', 0
	};
	char small[3]; /* "www" alone needs 4 bytes (3 + NUL); "www.com" more */

	CHECK(dn_expand(msg, msg + sizeof msg, msg, small, sizeof small) == -1);
	CHECK(dn_expand(msg, msg + sizeof msg, msg, small, 0) == -1);
}

int main(void)
{
	test_plain_name();
	test_backward_pointer();
	test_forward_pointer();
	test_pointer_loop_rejected();
	test_malformed();
	test_output_buffer_too_small();

	if (!fails) printf("dn_expand: all tests passed\n");
	return fails != 0;
}
