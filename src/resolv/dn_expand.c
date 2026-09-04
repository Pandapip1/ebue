/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * dn_expand(): decode one RFC 1035 sec 4.1.4 compressed domain name.
 * A name in a DNS message is a sequence of length-prefixed labels
 * (1-63 bytes each) ending in a zero-length "root" label, except that
 * at any label boundary a 2-byte, top-two-bits-set pointer (0xC0 mask)
 * may appear instead, redirecting the walk to another offset in the
 * same message so a repeated suffix (e.g. a domain shared by every
 * answer RR) need not be spelled out twice.
 *
 * Return value: the number of bytes dn_expand() itself consumed
 * starting at comp_dn, i.e. up to and including the first pointer
 * encountered (or the terminating root label, if the name has no
 * pointer) -- NOT counting anything read after following a pointer.
 * That is exactly how many bytes of the message comp_dn's caller must
 * skip to reach whatever field follows this name; bytes reached via a
 * pointer jump belong to some earlier occurrence already accounted
 * for. Returns -1 on any malformed input or if exp_dn/length is too
 * small to hold the decoded name.
 */
#include <resolv.h>
#include "unsafe_pointer.h"

/* RFC 1035 sec 4.1.4 itself flags the hazard this guards against: a
 * pointer's target is not required to lie strictly before its own
 * position (dn_expand-ptr-0.c below exercises exactly such a forward
 * pointer, deliberately), so nothing here can assume offsets strictly
 * decrease. Bounding the number of jumps instead of the offsets
 * themselves is what keeps a cyclic chain of pointers from looping
 * forever while still accepting the legitimate forward case. No real
 * message needs more than a handful of jumps; this cap is generous. */
#define DN_EXPAND_MAX_JUMPS 128

int dn_expand(const unsigned char *msg, const unsigned char *eomorig,
              const unsigned char *comp_dn, char *exp_dn, int length)
{
	const unsigned char *p = comp_dn;
	char *dst = exp_dn;
	char *dst_end = exp_dn + length;
	int consumed = -1; /* bytes read at comp_dn itself; set once, before any jump */
	int jumps = 0;
	int first_label = 1;

	if (length < 0) return -1;

	for (;;) {
		unsigned int b;
		int i;

		/* p (comp_dn, or a later msg+offset after a compression jump)
		 * and msg/eomorig are only related by this function's
		 * documented caller contract -- every caller passes offsets
		 * into the SAME DNS message buffer bounded by [msg, eomorig) --
		 * not by any pointer derivation this loop's own body performs.
		 * (Genuinely needed on both operands: the analyzer's region
		 * tracking does not survive this loop's own back-edge, so even
		 * comp_dn-derived reads of p need it here, not just msg-derived
		 * ones.) */
		if (unsafe_assume_shared_provenance(p < msg) ||
		    unsafe_assume_shared_provenance(p >= eomorig)) return -1;
		b = p[0];

		if ((b & 0xC0) == 0xC0) {
			unsigned int offset;

			/* Same message-buffer contract as the loop's own bounds
			 * check above. */
			if (unsafe_assume_shared_provenance(p + 1 >= eomorig)) return -1;
			offset = ((b & 0x3F) << 8) | p[1];

			/* p and comp_dn: same message-buffer contract as above --
			 * the caller's own comp_dn parameter and this loop's
			 * cursor p are asserted, not proven, to index the same
			 * buffer. */
			if (consumed < 0)
				consumed = (int)unsafe_assume_shared_provenance(p - comp_dn) + 2;
			if (++jumps > DN_EXPAND_MAX_JUMPS) return -1;

			p = msg + offset;
			continue;
		}

		if (b & 0xC0) return -1; /* reserved label-length bits (0x40/0x80) */

		if (b == 0) {
			/* Same message-buffer contract as the compression-pointer
			 * case above. */
			if (consumed < 0)
				consumed = (int)unsafe_assume_shared_provenance(p - comp_dn) + 1;
			break;
		}

		/* ordinary label: b (1-63) label bytes follow p[0]. Same
		 * message-buffer contract as the loop's own bounds check
		 * above. */
		if (unsafe_assume_shared_provenance(p + 1 + b > eomorig)) return -1;

		if (!first_label) {
			if (dst + 1 >= dst_end) return -1; /* leave room for a terminator */
			*dst = '.';
			dst += 1;
		}
		if (dst + (int)b >= dst_end) return -1; /* leave room for a terminator */
		for (i = 0; i < (int)b; i++) dst[i] = (char)p[1 + i];
		dst += b;
		first_label = 0;

		p += 1 + b;
	}

	if (dst >= dst_end) return -1;
	*dst = '\0';

	return consumed;
}
