/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * iconv_open(), iconv() and iconv_close(), added after the original
 * fuzzing inventory was completed.  All four supported UTF-8/UTF-16LE
 * directions are driven here.  Each conversion first gets a fuzzer-sized
 * output buffer, so every character boundary can become the E2BIG edge,
 * and then a large output buffer, so valid inputs reach the whole decoder.
 *
 * The checks are properties of iconv's contract rather than a second
 * converter: input and output pointers must agree with their remaining
 * byte counts, a failed conversion must stop inside the supplied buffers,
 * an identity conversion must copy its valid input exactly, and a complete
 * conversion through the other encoding and back must reproduce the
 * original canonical byte sequence.  Guards make writes past the caller's
 * advertised output size visible in UBSan-only mode as well as ASan mode.
 */
#include <iconv.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define INMAX  256
#define OUTMAX 1024
#define PAD    16
#define GUARD  0xa7

struct guarded {
	unsigned char mem[PAD + OUTMAX + PAD];
};

static unsigned char *guarded_out(struct guarded *g)
{
	memset(g->mem, GUARD, sizeof g->mem);
	return g->mem + PAD;
}

static void check_guard(const char *what, const struct guarded *g, size_t cap)
{
	size_t i;

	for (i = 0; i < PAD; i++)
		if (g->mem[i] != GUARD)
			oracle_mismatch_i(what, "output underrun", (long long)i, GUARD);
	for (i = PAD + cap; i < sizeof g->mem; i++)
		if (g->mem[i] != GUARD)
			oracle_mismatch_i(what, "output overrun",
			                  (long long)(i - PAD - cap), GUARD);
}

static size_t run(iconv_t cd, const char *what, const unsigned char *in,
                  size_t inlen, unsigned char *out, size_t cap,
                  size_t *produced, int *err)
{
	char *ip = (char *)in, *op = (char *)out;
	size_t il = inlen, ol = cap, rc;
	uintptr_t ibase = (uintptr_t)(const void *)in;
	uintptr_t obase = (uintptr_t)(void *)out;
	uintptr_t ipos, opos;
	size_t consumed = 0;

	errno = 0;
	rc = iconv(cd, &ip, &il, &op, &ol);
	*err = errno;
	ipos = (uintptr_t)(void *)ip;
	opos = (uintptr_t)(void *)op;
	*produced = 0;

	/* Compare integer representations first: subtracting or ordering
	 * pointers outside the same object would itself make a corrupt
	 * pointer reported by the implementation undefined in the harness. */
	if (ipos < ibase || ipos - ibase > inlen)
		oracle_mismatch_i(what, "input pointer escaped its buffer",
		                  (long long)(ipos - ibase), (long long)inlen);
	else consumed = (size_t)(ipos - ibase);
	if (il != inlen - consumed)
		oracle_mismatch_i(what, "input pointer/count disagree",
		                  (long long)consumed + (long long)il,
		                  (long long)inlen);
	if (opos < obase || opos - obase > cap)
		oracle_mismatch_i(what, "output pointer escaped its buffer",
		                  (long long)(opos - obase), (long long)cap);
	else *produced = (size_t)(opos - obase);
	if (ol != cap - *produced)
		oracle_mismatch_i(what, "output pointer/count disagree",
		                  (long long)*produced + (long long)ol,
		                  (long long)cap);
	if (rc == 0 && il != 0)
		oracle_mismatch_i(what, "success left input unconverted",
		                  (long long)il, 0);
	if (rc == (size_t)-1 && *err != E2BIG && *err != EILSEQ && *err != EINVAL)
		oracle_mismatch_i(what, "unexpected failure errno", *err, EILSEQ);
	return rc;
}

static void exercise(const char *from, const char *to, int identity,
                     const unsigned char *in, size_t inlen, size_t smallcap)
{
	struct guarded small, full, back;
	unsigned char *sout = guarded_out(&small);
	unsigned char *fout = guarded_out(&full);
	unsigned char *bout = guarded_out(&back);
	iconv_t cd, reverse;
	size_t made, fullmade, backmade;
	int err;
	size_t rc;

	cd = iconv_open(to, from);
	if (cd == (iconv_t)-1)
		oracle_mismatch_i("iconv_open rejected a supported pair", from, 0, 1);
	if (cd == (iconv_t)-1) return;

	(void)run(cd, from, in, inlen, sout, smallcap, &made, &err);
	check_guard(from, &small, smallcap);

	/* Neither supported codeset has shift state, but the specified reset
	 * call must still succeed and must not consume output space. */
	{
		char *op = (char *)fout;
		size_t ol = OUTMAX;
		if (iconv(cd, 0, 0, &op, &ol) == (size_t)-1 ||
		    op != (char *)fout || ol != OUTMAX)
			oracle_mismatch_i("iconv reset changed a stateless descriptor",
			                  from, (long long)ol, OUTMAX);
	}

	rc = run(cd, from, in, inlen, fout, OUTMAX, &fullmade, &err);
	check_guard(from, &full, OUTMAX);
	if (rc == 0 && identity &&
	    (fullmade != inlen || memcmp(fout, in, inlen) != 0))
		oracle_mismatch_i("iconv identity conversion changed its input",
		                  from, (long long)fullmade, (long long)inlen);

	if (rc == 0) {
		reverse = iconv_open(from, to);
		if (reverse == (iconv_t)-1)
			oracle_mismatch_i("iconv_open rejected the reverse pair", to, 0, 1);
		else {
			size_t brc = run(reverse, to, fout, fullmade, bout, OUTMAX,
			                 &backmade, &err);
			check_guard(to, &back, OUTMAX);
			if (brc != 0 || backmade != inlen || memcmp(bout, in, inlen) != 0)
				oracle_mismatch_i("iconv round trip changed a valid sequence",
				                  from, (long long)backmade, (long long)inlen);
			if (iconv_close(reverse) != 0)
				oracle_mismatch_i("iconv_close(reverse) failed", to, errno, 0);
		}
	}

	if (iconv_close(cd) != 0)
		oracle_mismatch_i("iconv_close failed", from, errno, 0);
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	static const char *const utf8[] = { "UTF-8", "utf8", "Ut_F-8" };
	static const char *const utf16[] = { "UTF-16LE", "utf16le", "Ut_F-16_LE" };
	unsigned char in[INMAX];
	size_t inlen, cap;
	unsigned sel;

	if (size < 2) return 0;
	sel = data[0];
	cap = (size_t)data[1] * 2;
	if (cap > OUTMAX) cap = OUTMAX;
	data += 2;
	size -= 2;
	inlen = size < sizeof in ? size : sizeof in;
	memcpy(in, data, inlen);

	exercise(utf8[sel % 3], utf8[(sel / 3) % 3], 1, in, inlen, cap);
	exercise(utf8[sel % 3], utf16[(sel / 3) % 3], 0, in, inlen, cap);
	exercise(utf16[sel % 3], utf8[(sel / 3) % 3], 0, in, inlen, cap);
	exercise(utf16[sel % 3], utf16[(sel / 3) % 3], 1, in, inlen, cap);

	/* Drive the codeset-name normalizer with arbitrary bounded strings too.
	 * An accepted name denotes one of the supported pairs and must close. */
	{
		char a[34], b[34];
		size_t split = inlen ? in[0] % (inlen + 1) : 0;
		size_t alen = split < sizeof a - 1 ? split : sizeof a - 1;
		size_t blen = inlen - split < sizeof b - 1 ? inlen - split : sizeof b - 1;
		iconv_t cd;
		memcpy(a, in, alen); a[alen] = 0;
		memcpy(b, in + split, blen); b[blen] = 0;
		cd = iconv_open(a, b);
		if (cd != (iconv_t)-1) (void)iconv_close(cd);
	}
	return 0;
}
