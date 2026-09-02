/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * crypt()/encrypt()/setkey(): the traditional Unix DES-based password
 * hashing family. Previously marked undefined-ok on the theory that "DES
 * password hashing is not something this library implements from
 * scratch" -- but DES is fully specified (FIPS 46-3) and standalone from
 * scratch is exactly what other from-scratch libcs do (musl's own
 * src/crypt/crypt_des.c). This is a plain, portable ISO C implementation
 * of the algorithm: no platform dependency, so it is one of the few
 * src/unistd/*.c files with no nt/ or linux/ backend at all.
 *
 * crypt.html: "The algorithm is implementation-defined" -- POSIX does
 * not mandate DES, only that "the first two bytes of [salt] may be used
 * to perturb the encoding algorithm". This implements the traditional,
 * de facto standard algorithm every historical crypt(3) (V7, BSD,
 * glibc's descrypt) agrees on, since that is the one with real
 * interoperability value (matching real /etc/passwd-style hashes):
 *
 *   1. Up to the first 8 bytes of the password, each truncated to its
 *      low-order 7 bits, become a 56-bit DES key (the 8th/parity bit of
 *      each byte is left 0; DES's own key schedule (PC1) never reads
 *      it, so its value is moot).
 *   2. The 2-character salt is decoded to a 12-bit integer (6 bits per
 *      character, first character low). For i in 0..11, if salt bit i
 *      is set, DES's 48-bit E-expansion output has bits i and i+24
 *      swapped, every round -- this is the "perturb the encoding
 *      algorithm" the salt exists for, and specifically what stops
 *      off-the-shelf DES hardware/software from being reused to attack
 *      it. (Independently confirmed against
 *      https://www.usenix.org/legacyurl/traditional-crypt, "if bit i of
 *      the salt is set, then bits i and i+24 are swapped in the DES
 *      E-box output".)
 *   3. The all-zero 64-bit block is DES-encrypted, with that modified
 *      E-expansion, 25 times in a row (each iteration's ciphertext is
 *      the next iteration's plaintext).
 *   4. The salt (verbatim) followed by the final 64-bit result -- padded
 *      to 66 bits with two trailing zero bits, then regrouped into 11
 *      six-bit groups MSB-first -- each mapped through the crypt64
 *      alphabet ("./0-9A-Za-z") is the 13-character result.
 *
 * The DES core (permute()/key_schedule()/des_block(), and every table)
 * was verified, before this file was written, against the official
 * FIPS 46-3 known-answer test (key 0x133457799BBCDFF1, plaintext
 * 0x0123456789ABCDEF, ciphertext 0x85E813540F0AB405) in a standalone
 * driver, and the IP/E/P/PC1/PC2/S1 tables independently cross-checked
 * entry-for-entry against Openwall John the Ripper's DES_std.c.
 * crypt()'s own salt/chaining/encoding wrapper was verified the same
 * way against two known-answer crypt(3) outputs from mogest/unix-
 * crypt's test suite, which test/posix-unistd.c's test_crypt() also
 * pins: crypt("test","PQ") == "PQl1.p7BcJRuM" and
 * crypt("much longer password here","xx") == "xxtHrOGVa3182".
 *
 * Extended (glibc _crypt_blowfish-style "$1$"/"$2a$"/"$5$"/"$6$")
 * variants are deliberately not implemented -- traditional 2-character-
 * salt DES is the POSIX-mandatory minimum, the others are a much larger
 * surface for no required-behaviour gain, and crypt() already has the
 * right failure mode for a salt it does not understand: since none of
 * '$', '_' etc. are in the traditional salt alphabet, ascii_to_bin()-
 * style validation below rejects them with EINVAL, same as a real
 * multi-algorithm crypt() rejects a salt naming a hash it was built
 * without.
 *
 * encrypt()/setkey() (crypt.html's less-used siblings) are the same DES
 * core with no salt and no 25x chaining: setkey() loads a caller-given
 * 64-bit key (as 64 ASCII '0'/'1' characters, bit 1 first) into
 * module-static subkeys; encrypt() runs a single forward (edflag == 0)
 * or reverse (edflag != 0) DES pass over a caller-given 64-bit block (in
 * the same 64-character representation), in place. Like rand()/srand()
 * (src/stdlib/rand.c), the shared state is a plain static, not
 * thread-local: POSIX does not require either family to be thread-safe,
 * and setkey() explicitly documents "The effect ... when ... called
 * from more than one thread ... is unspecified."
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include "libc.h"

/* Every permutation/selection table below lists 1-indexed bit positions,
 * bit 1 being the most significant bit of its input -- the standard
 * FIPS 46-3 convention. */
static const unsigned char IP[64] = {
	58,50,42,34,26,18,10,2, 60,52,44,36,28,20,12,4,
	62,54,46,38,30,22,14,6, 64,56,48,40,32,24,16,8,
	57,49,41,33,25,17,9,1,  59,51,43,35,27,19,11,3,
	61,53,45,37,29,21,13,5, 63,55,47,39,31,23,15,7
};
static const unsigned char FP[64] = {
	40,8,48,16,56,24,64,32, 39,7,47,15,55,23,63,31,
	38,6,46,14,54,22,62,30, 37,5,45,13,53,21,61,29,
	36,4,44,12,52,20,60,28, 35,3,43,11,51,19,59,27,
	34,2,42,10,50,18,58,26, 33,1,41,9,49,17,57,25
};
static const unsigned char Etab[48] = {
	32,1,2,3,4,5, 4,5,6,7,8,9, 8,9,10,11,12,13, 12,13,14,15,16,17,
	16,17,18,19,20,21, 20,21,22,23,24,25, 24,25,26,27,28,29, 28,29,30,31,32,1
};
static const unsigned char Ptab[32] = {
	16,7,20,21, 29,12,28,17, 1,15,23,26, 5,18,31,10,
	2,8,24,14, 32,27,3,9, 19,13,30,6, 22,11,4,25
};
static const unsigned char PC1[56] = {
	57,49,41,33,25,17,9, 1,58,50,42,34,26,18,
	10,2,59,51,43,35,27, 19,11,3,60,52,44,36,
	63,55,47,39,31,23,15, 7,62,54,46,38,30,22,
	14,6,61,53,45,37,29, 21,13,5,28,20,12,4
};
static const unsigned char PC2[48] = {
	14,17,11,24,1,5, 3,28,15,6,21,10, 23,19,12,4,26,8, 16,7,27,20,13,2,
	41,52,31,37,47,55, 30,40,51,45,33,48, 44,49,39,56,34,53, 46,42,50,36,29,32
};
static const unsigned char SHIFTS[16] = {1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1};

static const unsigned char Sbox[8][64] = {
{14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7, 0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,
 4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0, 15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13},
{15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10, 3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,
 0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15, 13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9},
{10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8, 13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,
 13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7, 1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12},
{7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15, 13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,
 10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4, 3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14},
{2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9, 14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,
 4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14, 11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3},
{12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11, 10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,
 9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6, 4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13},
{4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1, 13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,
 1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2, 6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12},
{13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7, 1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,
 7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8, 2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11}
};

/* Extract table[i]'s 1-indexed bit (MSB-first) from `in` (`inbits` wide,
 * right-justified) for i in 0..outbits-1, and pack the results MSB-first
 * into an outbits-wide, right-justified result. Used for every fixed
 * permutation/selection/compression table above (IP, FP, E, P, PC1,
 * PC2), which differ only in table contents and in/out widths. */
static uint64_t permute(uint64_t in, const unsigned char *table, int outbits, int inbits)
{
	uint64_t out = 0;
	int i;
	for (i = 0; i < outbits; i++) {
		int frombit = table[i];
		int shift = inbits - frombit;
		out = (out << 1) | ((in >> shift) & 1ULL);
	}
	return out;
}

static uint32_t rotl28(uint32_t v, int n)
{
	v &= 0x0FFFFFFFu;
	return ((v << n) | (v >> (28 - n))) & 0x0FFFFFFFu;
}

/* Standard DES key schedule: PC1 selects 56 of the 64 key bits (the
 * other 8 are the traditional per-byte parity bits, never read), split
 * into two 28-bit halves that are independently left-rotated by
 * SHIFTS[round] before each round's subkey is PC2's 48-bit selection of
 * the concatenated result. */
static void key_schedule(uint64_t key, uint64_t subkeys[16])
{
	uint64_t cd = permute(key, PC1, 56, 64);
	uint32_t c = (uint32_t)(cd >> 28) & 0x0FFFFFFFu;
	uint32_t d = (uint32_t)cd & 0x0FFFFFFFu;
	int i;
	for (i = 0; i < 16; i++) {
		c = rotl28(c, SHIFTS[i]);
		d = rotl28(d, SHIFTS[i]);
		subkeys[i] = permute(((uint64_t)c << 28) | d, PC2, 48, 56);
	}
}

/* One full DES pass (IP, 16 Feistel rounds, FP) over a 64-bit block.
 * `decrypt` runs the round keys in reverse order, the standard way DES
 * decryption reuses the same Feistel network as encryption. `salt12`,
 * if nonzero, is crypt()'s salt: for i in 0..11, bit i set swaps the
 * 48-bit E-expansion's bit i and bit i+24 every round (see this file's
 * banner); 0 leaves E unmodified, which is what encrypt()/setkey() want
 * -- plain DES has no salt. */
static uint64_t des_block(uint64_t block, const uint64_t subkeys[16], unsigned salt12, int decrypt)
{
	uint64_t ip = permute(block, IP, 64, 64);
	uint32_t L = (uint32_t)(ip >> 32);
	uint32_t R = (uint32_t)ip;
	int round;
	for (round = 0; round < 16; round++) {
		uint64_t E = permute(R, Etab, 48, 32);
		uint64_t subkey = subkeys[decrypt ? 15 - round : round];
		uint32_t f;
		uint32_t newR;
		int j;
		if (salt12) {
			for (j = 0; j < 12; j++) {
				if (salt12 & (1u << j)) {
					uint64_t b1 = (E >> (47 - j)) & 1;
					uint64_t b2 = (E >> (47 - (j + 24))) & 1;
					if (b1 != b2) {
						E ^= (1ULL << (47 - j));
						E ^= (1ULL << (47 - (j + 24)));
					}
				}
			}
		}
		E ^= subkey;
		f = 0;
		for (j = 0; j < 8; j++) {
			unsigned six = (unsigned)((E >> (42 - 6 * j)) & 0x3Fu);
			unsigned row = ((six & 0x20u) >> 4) | (six & 1u);
			unsigned col = (six >> 1) & 0xFu;
			f = (f << 4) | Sbox[j][row * 16 + col];
		}
		f = (uint32_t)permute(f, Ptab, 32, 32);
		newR = L ^ f;
		L = R;
		R = newR;
	}
	/* Pre-output is R||L (the last round's halves are not swapped
	 * back), then FP. */
	return permute(((uint64_t)R << 32) | L, FP, 64, 64);
}

static const char crypt64[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/* Inverse of crypt64[]: -1 for a byte outside the traditional salt/
 * output alphabet. Same mapping crypt(3) has always used, and the one
 * this file's banner cites being cross-checked against musl's
 * src/crypt/crypt_des.c ascii_to_bin(). */
static int crypt64_val(unsigned char c)
{
	if (c == '.') return 0;
	if (c == '/') return 1;
	if (c >= '0' && c <= '9') return 2 + (c - '0');
	if (c >= 'A' && c <= 'Z') return 12 + (c - 'A');
	if (c >= 'a' && c <= 'z') return 38 + (c - 'a');
	return -1;
}

/* crypt.html DESCRIPTION/RETURN VALUE: see this file's banner for the
 * algorithm. Returns a pointer to a static buffer, overwritten by the
 * next call -- crypt() has never been required to be reentrant
 * (crypt_r() is the POSIX answer for that, not implemented here: no
 * caller in this tree needs it, and it is a thin reentrant wrapper
 * around exactly this algorithm, not a new one).
 *
 * key/salt are required (include/unistd.h marks both nonnull): every
 * real caller (test/posix-unistd.c, third_party/libc-test's crypt.c)
 * passes real strings, and there is no defined behaviour to fall back
 * to for a NULL password or salt. */
char *crypt(const char *key, const char *salt)
{
	static char out[14];
	uint64_t keybits = 0;
	uint64_t subkeys[16];
	uint64_t block;
	unsigned salt12;
	int s0, s1, ended, i;

	s0 = crypt64_val((unsigned char)salt[0]);
	s1 = crypt64_val((unsigned char)salt[1]);
	if (s0 < 0 || s1 < 0) { errno = EINVAL; return NULL; }
	salt12 = (unsigned)(s0 | (s1 << 6));

	ended = 0;
	for (i = 0; i < 8; i++) {
		unsigned char c;
		if (!ended && key[i] == 0) ended = 1;
		c = ended ? 0 : (unsigned char)key[i];
		keybits = (keybits << 8) | (uint64_t)((c & 0x7Fu) << 1);
	}
	key_schedule(keybits, subkeys);

	block = 0;
	for (i = 0; i < 25; i++)
		block = des_block(block, subkeys, salt12, 0);

	out[0] = (char)salt[0];
	out[1] = (char)salt[1];
	for (i = 0; i < 11; i++) {
		unsigned grp = 0;
		int b;
		for (b = 0; b < 6; b++) {
			int bitpos = i * 6 + b; /* 0-indexed from the MSB of the 66-bit padded value */
			unsigned bit = bitpos < 64 ? (unsigned)((block >> (63 - bitpos)) & 1) : 0;
			grp = (grp << 1) | bit;
		}
		out[2 + i] = crypt64[grp & 0x3Fu];
	}
	out[13] = 0;
	return out;
}

/* setkey.html/encrypt.html DESCRIPTION: both represent a 64-bit
 * quantity as 64 ASCII '0'/'1' bytes, most significant bit (DES bit 1)
 * first -- the array-of-bits interface XSI defines instead of crypt()'s
 * packed password/salt strings. Shared, plain-static (not per-thread)
 * subkey state: see this file's banner for why. */
static uint64_t despriv_subkeys[16];

/* block/key are required (include/stdlib.h and include/unistd.h mark
 * both nonnull): both pages describe them as fixed 64-element arrays
 * with no null-pointer provision, and every element is read (or written
 * back, for encrypt()) unconditionally. */
void setkey(const char *key)
{
	uint64_t keybits = 0;
	int i;
	for (i = 0; i < 64; i++)
		keybits = (keybits << 1) | (uint64_t)(key[i] == '1');
	key_schedule(keybits, despriv_subkeys);
}

void encrypt(char *block, int edflag)
{
	uint64_t in = 0;
	uint64_t out;
	int i;
	for (i = 0; i < 64; i++)
		in = (in << 1) | (uint64_t)(block[i] == '1');
	out = des_block(in, despriv_subkeys, 0, edflag != 0);
	for (i = 0; i < 64; i++)
		block[i] = (char)('0' + ((out >> (63 - i)) & 1));
}

// NOLINTEND(misc-include-cleaner)
