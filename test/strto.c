/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <float.h>
#include <inttypes.h>
#include <wchar.h>

static uint64_t dbits(double d) { union { double d; uint64_t u; } v; v.d = d; return v.u; }
static uint32_t fbits(float f) { union { float f; uint32_t u; } v; v.f = f; return v.u; }

/* Expected bit patterns for correctly rounded strtod, produced by
 * glibc (which is correctly rounded) on the host; C99 7.20.1.3p9 lets
 * an implementation be off by one ulp, but there is no reason to be.
 * Cases: 86 cases */
static const struct { const char *s; uint64_t bits; } rt[] = {
	{ "24004371741093512538999e-219", 0x171cb5a9db912fceULL },
	{ "1.00000000000000011102230246251565404236316680908203125", 0x3ff0000000000000ULL },
	{ "1.00000000000000011102230246251565404236316680908203124", 0x3ff0000000000000ULL },
	{ "1.00000000000000011102230246251565404236316680908203126", 0x3ff0000000000001ULL },
	{ "2.2250738585072011e-308", 0x000fffffffffffffULL },
	{ "7.4109846876186981e-323", 0x000000000000000fULL },
	{ "2.4703282292062327e-324", 0x0000000000000000ULL },
	{ "2.4703282292062328e-324", 0x0000000000000001ULL },
	{ "9.881312916824931e-324", 0x0000000000000002ULL },
	{ "1.7976931348623158e308", 0x7fefffffffffffffULL },
	{ "5e-324", 0x0000000000000001ULL },
	{ "2.5e-324", 0x0000000000000001ULL },
	{ "0.500000000000000166533453693773481063544750213623046875", 0x3fe0000000000002ULL },
	{ "9.019375872403209e-318", 0x00000000001bdb06ULL },
	{ "3.5041689370767125e-306", 0x0083af894e7291feULL },
	{ "6.0600602825971518e-306", 0x009105a6c9257b45ULL },
	{ "6.9611910407773577e-312", 0x000001480c9aeae4ULL },
	{ "4.7480082177093486e-312", 0x000000dfc083ad02ULL },
	{ "2.5932246517395485e-300", 0x01bbc960475bd9b4ULL },
	{ "9.940322750512624e-295", 0x02e450c0ac55104cULL },
	{ "2.0273768101659582e-297", 0x025536e116df5c2fULL },
	{ "8.52254987462981e-291", 0x03b543166962669eULL },
	{ "6.3259555491961244e-300", 0x01d0f21f438e4c42ULL },
	{ "5.1634774195379458e-82", 0x2f0f58bdd75458a5ULL },
	{ "8.4293824066782749e-21", 0x3bc3e73fbf9b082dULL },
	{ "4.4754165479674067e-82", 0x2f0b2b66e9dd576aULL },
	{ "7.3264478854360155e-92", 0x2d031a5fd0f3399cULL },
	{ "8.4560166086585966e-67", 0x3236cc20cffe33d1ULL },
	{ "3.9106705021144648e-19", 0x3c1cdb0c3780817dULL },
	{ "1.718993899867552e-17", 0x3c73d193117d55acULL },
	{ "3.7285996904268285e-07", 0x3e9905af7c69168cULL },
	{ "2048362721104019.5", 0x431d1be67173424eULL },
	{ "26190084955182.477", 0x42b7d1dab5dc2e7aULL },
	{ "5.8093544970730083e+72", 0x4f0a4dc7797d1d54ULL },
	{ "4.5079187887798378e+25", 0x4542a4f1d607197eULL },
	{ "5.3933067054631152e+70", 0x4e9f41f45e2926d1ULL },
	{ "1.187827184450701e+26", 0x4558904e04743a2fULL },
	{ "7.0365286638514008e+94", 0x53a0dddf429e3c37ULL },
	{ "1.2527839646917003e+283", 0x7ab591491ac1f321ULL },
	{ "8.9553151856120348e+296", 0x7d95e8bcbf7738a3ULL },
	{ "8.0333099067344381e+297", 0x7dc8911a8ddaf6d9ULL },
	{ "6.786566273602434e+292", 0x7cbb33ed816cb38bULL },
	{ "2.1034353680340007e+288", 0x7bcba0aaff3b912bULL },
	{ "1.6036034711544702e+304", 0x7f176256d01442ceULL },
	{ "7.2497679748189915e+307", 0x7fd9cf5e2c621cdbULL },
	{ "2.8516051762006753e+303", 0x7ef0a21438533d75ULL },
	{ "5.173623214007909e+303", 0x7efe2d5dee198843ULL },
	{ "2.7897502494954276e-08", 0x3e5df468375d0618ULL },
	{ "50.52019848375069", 0x40494295dd2991aaULL },
	{ "0.00240406344672005", 0x3f63b1afbc328ee5ULL },
	{ "857.89712128286806", 0x408acf2d4dec53b6ULL },
	{ "55.599617173713725", 0x404bccc0416b9c36ULL },
	{ "6.6200792176012144e-298", 0x023bb578cbf14392ULL },
	{ "1.1440222713416599e-10", 0x3ddf72574f5a01a6ULL },
	{ "1.7046637658793344e+164", 0x6207ae7d75f078c7ULL },
	{ "5.9989130423909919e-180", 0x1ab8e48052e46c33ULL },
	{ "8.589506554851781e-182", 0x1a56cfa20b69f83aULL },
	{ "4.992227153588628e+277", 0x799687a44fdd7f79ULL },
	{ "1.8543792830760447e-83", 0x2ec20321291255b7ULL },
	{ "1.6101808856506569e+152", 0x5f889851f7366a68ULL },
	{ "3.0186701888292657e-73", 0x30e1113a025c98e7ULL },
	{ "2.8003602368538021e+38", 0x47ea559f3e47235fULL },
	{ "2.0653296834263506e+51", 0x4a96149fa827a496ULL },
	{ "2.2645695523115691e-68", 0x31e3896f619b518bULL },
	{ "2.017946231494267e-81", 0x2f2ea06719fb98f4ULL },
	{ "1.7853620606688857e-245", 0x0d1f35308027cf9cULL },
	{ "1.4712380124962395e-112", 0x28b6a4f911d72c95ULL },
	{ "8.1894887687686795e-299", 0x020b6c160ecd2e12ULL },
	{ "7.8365603037553884e-138", 0x23775499ac488ca0ULL },
	{ "1.5984293224405815e-86", 0x2e1fcc252486c1ceULL },
	{ "1.1827806772671172e-146", 0x21a2e7a102e18292ULL },
	{ "1.2510568451206708e-163", 0x1e1cd1409c23bf99ULL },
	{ "8.001214642542224e-309", 0x0005c0e4d0de18aaULL },
	{ "2.4545321026353112e-309", 0x0001c3d6ec1c08a6ULL },
	{ "3.7057047290982852e-309", 0x0002aa28fdd26c7aULL },
	{ "1.3123351620624869e-309", 0x0000f194618a1fb6ULL },
	{ "1.2440771122137459e-308", 0x0008f224e62ae775ULL },
	{ "8.5976505982905372e-309", 0x00062eb02079dc88ULL },
	{ "1.0128600925284752e-308", 0x00074882d8608911ULL },
	{ "1.5432554415085929e-308", 0x000b18e2061aa095ULL },
	{ "86278304856260049255527101499745207260319258425708461551627431120692203016401428094805983088260263411169678790255945189916624620295130232673061206038255887497220408784372191989190412778380502227212964273631391489148430828102034389063302182111128858973782978246562931950176630827415780024047079186197711053253377814541621170600265998970819971998330461337163280790742904206602810250467802805062116838873709412263640377553606497082691867693652945920288070181978332085542295809952361925686998993557175670e-30", 0x7ff0000000000000ULL },
	{ "2214905190395103057741812828121533152127435661261203004118267454041823009981058133231573845938193240733666265664230333189819104250468924348962785173094585504113055345549579133083414803468445089111273561780261223049183306776290835057131268066888420349254328164844228528916874367862049189417565078754459635691843910615987499756083309655964949690849640501630220514716978244304132304610613252724858423549751484990688869401607347392259681730613191519942718924540376122496880368516183671179838380218846177508109639601245554308e12", 0x7ff0000000000000ULL },
	{ "2415575877780419482795789895289583799392588270579608862893808636041153768716293811617091933854446007269421147120723058360184404835943862123272333847008258557116550539883263638685339997282105995348820990686940406136157492898081598129442656041657222473216097560756547415024037875057934204753084953090463344283915611624184689524297619533031700267618381540512606066519382310673850201455125245579801845388944993173325844830705687497746705335743123175540513295830857964014563742699948061904044625920179185620815665723118602382957357116506113438791005619758678467113973188822605690548846407425715163111078340334765949544647534593271205898552499706e-300", 0x7ff0000000000000ULL },
	{ "1798551392016694264440067638305944314110070252178923739303823580417486201622124353092010297701472456356284107747052954869136413155080205377894548916907243347571487120991955668229633698781389818817821404823754494807816038751342468772179303741003247901911586626309280962835721419680382409960878206891446995440663811398560213802707399435789419810545698515788041006014702394179278057406846397620116899157270975306935748342862497656034897201980437536692300086217513580088088535553710992831356321355288615659972771483830876202282513707497140640415892072445994690652782313735522705268197213469001993759691794207904384762625678058678169733705526034581617207220636387879782065752630086658243371296004660565005e250", 0x7ff0000000000000ULL },
	{ "8069304876952700293072280677625412891262073854807897281807658852502942940331054515515573528029820900719360954481436873951211082733891562298855811742501191345701651315261267881659207868860623298386097519474177607083596669436995382876367415435523938444706193885874413232084461836387158263167471336471122679524328962315503922215090990722846362262323503608444037562818972344946606240208530903007080290048218872918045979344572441434113778916405175133145153446343357804660161836836920974957773044385904607688252836205189665931823027126630490556502779193922597178274115404476027271753017083934965358662108760802901823858494329730462244549871660684047157548291380312569186635436053277871144062195370964566562809847908468455273652154484895077478603313776671997672381993246338654088513720052920959052295496929e0", 0x7ff0000000000000ULL },
	{ "593553203440875361765090712954892657820339790200528185762594549083287324839945364447633152249404477486127761700365068206619742381443866185904657430674369018352254738930420742551246055161932367770610315479936074721256275142195834154961146452866191891940599646035574140277325593174646737233435313489148111636072782831428542054955019595593646048266213234931651836641451321881262852152786698458057763755210720652461538636188991423216196679564180683564506559947306852523162290035875415415129278373617993465942688543779849859231220768264120810791448818322691300053383084010917712903571229844098290618647940169304597837154955395464290674908879493410809394218007732426526869099654400254838855123233647817478177075670994759647371780874682742673001523614854468797842848405610332812623881644133287738468650237191114241194293509280225308971859527209168086696956841338090356052985341540402941441294239906796932803e-5", 0x7ff0000000000000ULL },
};

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* An L"..." literal's element type is the *compiler's own* wchar_t, not
 * include/wchar.h's typedef -- 4-byte int on the native host asan builds
 * against, vs. this header's `unsigned short` (matching the NT target).
 * wcstoimax()/wcstoumax() below are compiled against the header's, so an
 * L"..." argument is a silent type mismatch natively: every other unit
 * reads as the high half of a 4-byte int. Same fix, same reasoning, as
 * test/posix-wchar.c's W() -- see its longer comment -- kept as its own
 * copy rather than a shared header for one static helper two files use. */
static const wchar_t *W(const char *s)
{
	static wchar_t pool[4][24];
	static int slot;
	wchar_t *b = pool[slot++ % 4];
	size_t i;
	for (i = 0; s[i] && i < 23; i++) b[i] = (wchar_t)(unsigned char)s[i];
	b[i] = 0;
	return b;
}

/* `long` is 32-bit (LLP64) on both PE arches this library targets, but
 * 64-bit under the native ASan build's host ABI -- see
 * test/posix-limits.c's own header for the same fact about this file's
 * neighbour.  The "one past the boundary" strings below used to be
 * hardcoded assuming the 32-bit width, which made every one of these
 * checks silently wrong (not just skipped) once compiled with a 64-bit
 * long: strtol("2147483648",...) no longer overflows a 64-bit long at
 * all, so what should be an ERANGE clamping test would instead just
 * parse the number.  Deriving "boundary +/- 1" from LONG_MAX/LONG_MIN/
 * ULONG_MAX themselves, the way test/posix-limits.c derives its own
 * checks from sizeof(long), makes the test correct at either width
 * instead of merely not-wrong-looking.
 *
 * A decimal-digit-string increment, rather than integer arithmetic, is
 * required here specifically because "one past the boundary" is by
 * definition not representable in *any* fixed-width integer type the
 * boundary itself came from: on a 64-bit long, LONG_MAX+1 does not fit
 * in a long, and does not reliably fit in intmax_t/uintmax_t either
 * (they are the same 64-bit width here), so computing it with arithmetic
 * would itself be exactly the overflow this test is trying to provoke
 * safely, in the string domain instead. */
static void incr_digits(char *out, size_t outsz, const char *digits)
{
	char tmp[24];
	size_t n = strlen(digits);
	int i, len, carry = 1;

	if (n >= sizeof tmp) n = sizeof tmp - 1;         /* not reachable at 64-bit widths */
	for (i = 0; i < (int)n; i++) tmp[i] = digits[n - 1 - (size_t)i];
	/* Every digit above the lowest has to be copied through even once
	 * the carry stops propagating -- only the loop bound was the bug
	 * here, not the arithmetic: len (the real digit count) must stay at
	 * n regardless of where the carry loop itself stops. */
	for (i = 0; i < (int)n && carry; i++) {
		int d = (tmp[i] - '0') + carry;
		tmp[i] = (char)('0' + d % 10);
		carry = d / 10;
	}
	len = (int)n;
	if (carry) tmp[len++] = (char)('0' + carry);
	if ((size_t)len >= outsz) len = (int)outsz - 1;
	{
		int j;
		for (j = 0; j < len; j++) out[j] = tmp[len - 1 - j];
		out[len] = 0;
	}
}

/* "one more negative than LONG_MIN", as a decimal string: LONG_MIN's own
 * magnitude does not fit in a positive long (two's complement), so this
 * works on the "-" plus the *digits* of %ld's own output, never negating
 * LONG_MIN as an integer. */
static void one_below(char *out, size_t outsz, long v)
{
	char buf[24];
	snprintf(buf, sizeof buf, "%ld", v);
	out[0] = '-';
	incr_digits(out + 1, outsz - 1, buf[0] == '-' ? buf + 1 : buf);
}

static void one_above_ul(char *out, size_t outsz, unsigned long v)
{
	char buf[24];
	snprintf(buf, sizeof buf, "%lu", v);
	incr_digits(out, outsz, buf);
}

static int near(double a, double b, double rel)
{
	if (a == b) return 1;
	if (b == 0) return fabs(a) < rel;
	return fabs(a - b) <= rel * fabs(b);
}

int main(void)
{
	char *end;

	/* strtol basics */
	CHECK(strtol("123", 0, 10) == 123);
	CHECK(strtol("  -456xyz", &end, 10) == -456 && !strcmp(end, "xyz"));
	CHECK(strtol("0x1A", &end, 16) == 26 && *end == 0);
	CHECK(strtol("0x1A", &end, 0) == 26 && *end == 0);
	CHECK(strtol("017", 0, 0) == 15);
	CHECK(strtol("z", 0, 36) == 35);
	CHECK(strtol("101", 0, 2) == 5);
	CHECK(strtol("10", 0, 8) == 8);
	CHECK(strtol("+42", 0, 10) == 42);

	/* no digits: endptr = nptr */
	{ const char *s = "abc"; CHECK(strtol(s, &end, 10) == 0 && end == (char *)s); }
	{ const char *s = "0x"; CHECK(strtol(s, &end, 16) == 0 && end == (char *)s + 1); }
	{ const char *s = "  +"; CHECK(strtol(s, &end, 10) == 0 && end == (char *)s); }

	/* clamping at LONG_MIN/LONG_MAX/ULONG_MAX and one past each --
	 * derived from the actual width of `long` here (32-bit LLP64 on the
	 * PE targets, 64-bit under the native ASan build), not hardcoded to
	 * either; see incr_digits()'s comment above. */
	{
		char lmax[24], lmax1[25], lmin1[26], ulmax[24], ulmax1[25];

		snprintf(lmax, sizeof lmax, "%ld", LONG_MAX);
		incr_digits(lmax1, sizeof lmax1, lmax);
		one_below(lmin1, sizeof lmin1, LONG_MIN);
		snprintf(ulmax, sizeof ulmax, "%lu", ULONG_MAX);
		one_above_ul(ulmax1, sizeof ulmax1, ULONG_MAX);

		errno = 0; CHECK(strtol(lmax1, 0, 10) == LONG_MAX && errno == ERANGE);
		errno = 0; CHECK(strtol(lmin1, 0, 10) == LONG_MIN && errno == ERANGE);
		errno = 0; CHECK(strtol(lmax, 0, 10) == LONG_MAX && errno == 0);
		errno = 0; { char lmin[26]; snprintf(lmin, sizeof lmin, "%ld", LONG_MIN);
		             CHECK(strtol(lmin, 0, 10) == LONG_MIN && errno == 0); }
		errno = 0; CHECK(strtoul(ulmax, 0, 10) == ULONG_MAX && errno == 0);
		errno = 0; CHECK(strtoul(ulmax1, 0, 10) == ULONG_MAX && errno == ERANGE);
	}
	CHECK(strtoul("-1", 0, 10) == ULONG_MAX);
	errno = 0; CHECK(strtoll("9223372036854775807", 0, 10) == LLONG_MAX && errno == 0);
	errno = 0; CHECK(strtoll("9223372036854775808", 0, 10) == LLONG_MAX && errno == ERANGE);
	errno = 0; CHECK(strtoll("-9223372036854775808", 0, 10) == LLONG_MIN && errno == 0);
	errno = 0; CHECK(strtoll("-9223372036854775809", 0, 10) == LLONG_MIN && errno == ERANGE);
	errno = 0; CHECK(strtoull("18446744073709551615", 0, 10) == ULLONG_MAX && errno == 0);
	errno = 0; CHECK(strtoull("18446744073709551616", 0, 10) == ULLONG_MAX && errno == ERANGE);
	CHECK(strtoimax("-42", 0, 0) == -42);
	CHECK(strtoumax("0xff", 0, 0) == 255);

	/* wcstoimax/wcstoumax: the wide mirror of strtoimax/strtoumax */
	{
		wchar_t *wend;
		CHECK(wcstoimax(W("-42"), 0, 0) == -42);
		CHECK(wcstoumax(W("0xff"), 0, 0) == 255);
		CHECK(wcstoimax(W("  +123abc"), &wend, 10) == 123 && *wend == L'a');
		errno = 0;
		CHECK(wcstoimax(W("9223372036854775808"), 0, 10) == INTMAX_MAX && errno == ERANGE);
		errno = 0;
		CHECK(wcstoumax(W("-1"), 0, 10) == UINTMAX_MAX && errno == 0);
		CHECK(wcstoimax(W("777"), &wend, 8) == 511 && *wend == 0);
	}

	/* atoi family */
	CHECK(atoi("-17") == -17);
	CHECK(atol("100000") == 100000L);
	CHECK(atoll("123456789012345") == 123456789012345LL);
	CHECK(atof("2.5") == 2.5);

	/* strtod */
	CHECK(strtod("0", &end) == 0.0 && *end == 0);
	CHECK(strtod("1.5", 0) == 1.5);
	CHECK(strtod("-3.25e2", 0) == -325.0);
	CHECK(strtod("  .5", 0) == 0.5);
	CHECK(strtod("5.", 0) == 5.0);
	CHECK(near(strtod("3.14159265358979", 0), 3.14159265358979, 1e-15));
	CHECK(near(strtod("1e100", 0), 1e100, 1e-15));
	CHECK(near(strtod("1e-100", 0), 1e-100, 1e-15));
	CHECK(near(strtod("2.2250738585072014e-308", 0), 2.2250738585072014e-308, 1e-15));
	CHECK(near(strtod("1.7976931348623157e308", 0), 1.7976931348623157e308, 1e-15));
	CHECK(near(strtod("123456789012345678901234567890", 0), 1.2345678901234568e29, 1e-14));

	/* hex floats are exact */
	CHECK(strtod("0x1.8p3", 0) == 12.0);
	CHECK(strtod("0x10", 0) == 16.0);
	CHECK(strtod("0x.8p1", 0) == 1.0);
	CHECK(strtod("0x1p-1074", 0) == 4.9406564584124654e-324);
	CHECK(strtod("-0x1.fffffffffffffp1023", 0) == -1.7976931348623157e308);

	/* inf/nan */
	CHECK(strtod("inf", &end) == HUGE_VAL && *end == 0);
	CHECK(strtod("-Infinity", &end) == -HUGE_VAL && *end == 0);
	CHECK(strtod("infx", &end) == HUGE_VAL && !strcmp(end, "x"));
	{ double d = strtod("nan", &end); CHECK(d != d && *end == 0); }
	{ double d = strtod("NaN(abc123)", &end); CHECK(d != d && *end == 0); }
	{ double d = strtod("nan(", &end); CHECK(d != d && !strcmp(end, "(")); }

	/* nothing parsed */
	{ const char *s = "xyz"; CHECK(strtod(s, &end) == 0 && end == (char *)s); }
	{ const char *s = "e5"; CHECK(strtod(s, &end) == 0 && end == (char *)s); }
	{ const char *s = "0x"; strtod(s, &end); CHECK(end == (char *)s + 1); }

	/* ERANGE */
	errno = 0; CHECK(strtod("1e400", 0) == HUGE_VAL && errno == ERANGE);
	errno = 0; CHECK(strtod("-1e400", 0) == -HUGE_VAL && errno == ERANGE);
	errno = 0; CHECK(strtod("1e-400", 0) == 0 && errno == ERANGE);
	errno = 0; CHECK(strtof("1e39", 0) == HUGE_VALF && errno == ERANGE);
	errno = 0; CHECK(strtof("3.4e38", 0) < HUGE_VALF && errno == 0);

	/* strtof / strtold */
	CHECK(strtof("0.25", 0) == 0.25f);
	CHECK(strtold("0x1p100", 0) == 1267650600228229401496703205376.0L);
	CHECK(near((double)strtold("1e300", 0), 1e300, 1e-14));


	/* --- exact rounding and range, C99 7.20.1.3 --- */

	/* Overflow saturates to HUGE_VAL with ERANGE.  It must never come
	 * back as a NaN, which is what an approximated 10^e does once its
	 * own intermediate steps overflow (inf - inf). */
	errno = 0; CHECK(strtod("1e441", 0) == HUGE_VAL && errno == ERANGE);
	errno = 0; CHECK(strtod("1e442", 0) == HUGE_VAL && errno == ERANGE);
	errno = 0; CHECK(strtod("-1e442", 0) == -HUGE_VAL && errno == ERANGE);
	errno = 0; CHECK(strtod("1e5000", 0) == HUGE_VAL && errno == ERANGE);
	errno = 0; CHECK(strtod("-1e5000", 0) == -HUGE_VAL && errno == ERANGE);
	errno = 0; CHECK(strtod("1e99999", 0) == HUGE_VAL && errno == ERANGE);
	errno = 0; CHECK(strtod("1e-442", 0) == 0.0 && errno == ERANGE);
	errno = 0; CHECK(strtod("1e-5000", 0) == 0.0 && errno == ERANGE);
	errno = 0; CHECK(strtod("-1e-5000", 0) == 0.0 && errno == ERANGE);
	errno = 0; CHECK(strtod("1e-99999", 0) == 0.0 && errno == ERANGE);

	/* The last decimal exponents that stay finite / nonzero. */
	errno = 0; CHECK(strtod("1e308", 0) == 1e308 && errno == 0);
	errno = 0; CHECK(strtod("1e309", 0) == HUGE_VAL && errno == ERANGE);
	errno = 0; CHECK(dbits(strtod("1e-323", 0)) == 0x0000000000000002ULL && errno == ERANGE);
	errno = 0; CHECK(strtod("1e-324", 0) == 0.0 && errno == ERANGE);

	/* Hundreds of digits, with and without an exponent. */
	{
		char big[1024];
		memset(big, '9', 600); big[600] = 0;
		errno = 0; CHECK(strtod(big, 0) == HUGE_VAL && errno == ERANGE);
		big[0] = '.'; memset(big + 1, '0', 598); big[599] = '1'; big[600] = 0;
		errno = 0; CHECK(strtod(big, 0) == 0.0 && errno == ERANGE);
		/* 600 significant digits naming exactly 1.0 */
		big[0] = '1'; memset(big + 1, '0', 599); memcpy(big + 600, "e-599", 6);
		errno = 0; CHECK(strtod(big, 0) == 1.0 && errno == 0);
		/* 900 significant digits, past the digits kept exactly: the
		 * discarded tail is sticky, so this is 1 ulp above 1.0, not 1.0 */
		/* An exact rounding boundary (the midpoint between 1.0 and the
		 * next double) ties to even, i.e. down to 1.0.  The same
		 * boundary with one more nonzero digit hundreds of places
		 * further down is above it, and must round up - so the digits
		 * past the ones kept exactly still have to be felt. */
		strcpy(big, "1.00000000000000011102230246251565404236316680908203125");
		errno = 0; CHECK(dbits(strtod(big, 0)) == 0x3ff0000000000000ULL && errno == 0);
		memset(big + 55, '0', 900); big[955] = '1'; big[956] = 0;
		errno = 0; CHECK(dbits(strtod(big, 0)) == 0x3ff0000000000001ULL && errno == 0);
	}

	/* "inf"/"nan" are spellings, not range errors: errno must be left
	 * alone (C99 7.20.1.3p10 only speaks of values that are out of
	 * range, and nothing was converted here). */
	errno = 0; CHECK(strtod("inf", 0) == HUGE_VAL && errno == 0);
	errno = 0; CHECK(strtod("INF", 0) == HUGE_VAL && errno == 0);
	errno = 0; CHECK(strtod("Infinity", 0) == HUGE_VAL && errno == 0);
	errno = 0; CHECK(strtod("-INFINITY", 0) == -HUGE_VAL && errno == 0);
	errno = 0; { double d = strtod("nan", 0); CHECK(d != d && errno == 0); }
	errno = 0; { double d = strtod("NAN(1_x)", 0); CHECK(d != d && errno == 0); }
	errno = 0; CHECK(strtof("inf", 0) == HUGE_VALF && errno == 0);
	errno = 0; { float f = strtof("nan", 0); CHECK(f != f && errno == 0); }
	errno = 0; CHECK(strtold("inf", 0) == HUGE_VALL && errno == 0);

	/* The overflow decision is a rounding decision: a value above
	 * DBL_MAX but below the midpoint to 2^1024 rounds down to DBL_MAX
	 * and is not a range error at all. */
	errno = 0; CHECK(strtod("1.7976931348623157e308", 0) == DBL_MAX && errno == 0);
	errno = 0; CHECK(strtod("1.7976931348623158e308", 0) == DBL_MAX && errno == 0);
	errno = 0; CHECK(strtod("1.7976931348623159e308", 0) == HUGE_VAL && errno == ERANGE);
	errno = 0; CHECK(strtof("3.4028234663852886e38", 0) == FLT_MAX && errno == 0);
	errno = 0; CHECK(strtof("3.4028235677973366e38", 0) == FLT_MAX && errno == 0);
	errno = 0; CHECK(strtof("3.4028235677973367e38", 0) == HUGE_VALF && errno == ERANGE);

	/* strtof rounds once, to float: going via a double first would give
	 * 1.0f here (the double is exactly float's midpoint, ties to even). */
	CHECK(fbits(strtof("0x1.00000100000000000001p0", 0)) == 0x3f800001UL);
	CHECK(fbits(strtof("1.00000005960464477539062510", 0)) == 0x3f800001UL);
	CHECK(fbits(strtof("1.0000000596046447753906250", 0)) == 0x3f800000UL);

	/* %.17g round-trips and other known-hard conversions, against
	 * glibc's correctly rounded answers. */
	{
		size_t i;
		for (i = 0; i < sizeof rt / sizeof rt[0]; i++) {
			uint64_t got;
			errno = 0;
			got = dbits(strtod(rt[i].s, &end));
			if (got != rt[i].bits || *end != 0) {
				fails++;
				printf("FAIL %s:%d: strtod(\"%.32s...\") = %016" PRIx64 ", want %016" PRIx64 "\n",
					__FILE__, __LINE__, rt[i].s, got, rt[i].bits);
			}
		}
	}

	if (!fails) printf("strto: all tests passed\n");
	return fails != 0;
}
