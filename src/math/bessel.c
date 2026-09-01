/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bessel functions of the first kind (j0/j1/jn) and second kind, a.k.a.
 * Neumann functions (y0/y1/yn).  Not asserted for accuracy by POSIX,
 * only for the special values below, but implemented to a genuinely
 * reasonable accuracy anyway via textbook methods:
 *
 *  - J_n, small-to-moderate |x| (< 8): the defining power series
 *      J_n(x) = sum_{k=0}^inf (-1)^k / (k! (n+k)!) * (x/2)^(n+2k)
 *    evaluated by a running term ratio (no direct factorials, which
 *    would overflow), truncated once a term is negligible relative to
 *    the running sum.
 *
 *  - J_n, large |x| (>= 8): the standard asymptotic expansion (with
 *    the first Hankel P/Q correction terms, not just the leading
 *    order)
 *      J_n(x) ~ sqrt(2/(pi x)) [P(n,x) cos(chi) - Q(n,x) sin(chi)]
 *      Y_n(x) ~ sqrt(2/(pi x)) [P(n,x) sin(chi) + Q(n,x) cos(chi)]
 *      chi = x - n*pi/2 - pi/4
 *      P(n,x) ~ 1 - (mu-1)(mu-9)/(2 (8x)^2),  mu = 4 n^2
 *      Q(n,x) ~ (mu-1)/(8x) - (mu-1)(mu-9)(mu-25)/(6 (8x)^3)
 *
 *  - Y_0, Y_1, small-to-moderate x: the standard log-singular series
 *    (Abramowitz & Stegun-style; a well known public-domain form of
 *    the general solution to Bessel's equation, not any particular
 *    library's code):
 *      Y_0(x) = (2/pi)[(ln(x/2)+gamma) J_0(x)
 *                       + sum_{k=1}^inf (-1)^(k+1) H_k / (k!)^2 (x/2)^2k]
 *      Y_1(x) = -2/(pi x) + (2/pi)(ln(x/2)+gamma) J_1(x)
 *               - (1/pi) sum_{k=0}^inf (-1)^k (H_k+H_{k+1})
 *                                       / (k! (k+1)!) (x/2)^(2k+1)
 *    where H_k is the k-th harmonic number (H_0 = 0) and
 *    gamma is the Euler-Mascheroni constant.
 *
 *  - J_n, 2 <= |n| <= 10000: the three-term recurrence
 *      J_{k-1}(x) = (2k/x) J_k(x) - J_{k+1}(x)
 *    is numerically unstable computed upward when n exceeds x, so
 *    this file always uses Miller's algorithm instead: recur the same
 *    relation *downward* from an order well above n, starting from an
 *    arbitrary seed (unstable in the downward direction becomes
 *    stable), and normalize the whole run by comparing the downward
 *    run's own J_0 against this file's j0(x).
 *
 *  - Y_n, 2 <= |n| <= 10000: the same recurrence
 *      Y_{k+1}(x) = (2k/x) Y_k(x) - Y_{k-1}(x)
 *    is stable computed *upward*, so a plain upward recurrence from
 *    y0(x)/y1(x) is used directly.
 *
 *  - Larger orders: a bounded uniform Airy expansion covers both sides
 *    of the x=n turning point.  When x is far beyond n, the ordinary
 *    Hankel expansion above is used instead.  This avoids making runtime
 *    proportional to an arbitrary caller-supplied int order.
 *
 * None of this is derived from any specific library's source (Cephes,
 * musl, glibc, Boost, ...) -- it is written from the public-domain
 * mathematical definitions above.
 */
#include <math.h>
#include <fenv.h>
#include <float.h>
#include <stdint.h>

static double j_series(int n, double x) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	double half = x * 0.5;
	double term = 1.0;
	double sum;
	int k;

	for (k = 1; k < n + 1; k++) term *= half / (double)k;
	sum = term;
	for (k = 1; k <= 60; k++) {
		term *= -(half * half) / ((double)k * (double)(n + k));
		sum += term;
		if (k > 3 && fabs(term) < 1e-17 * fabs(sum)) break;
	}
	return sum;
}

static void asymp_pq(double n, double x, double *p, double *q) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	double mu = 4.0 * n * n;
	double t = 1.0 / (8.0 * x);
	double t2 = t * t;

	*p = 1.0 - (mu - 1.0) * (mu - 9.0) * t2 * 0.5;
	*q = (mu - 1.0) * t - (mu - 1.0) * (mu - 9.0) * (mu - 25.0) * t2 * t / 6.0;
}

static double j_asymp(double n, double x)
{
	double p, q, chi, s, c;
	asymp_pq(n, x, &p, &q);
	chi = x - n * M_PI_2 - M_PI_4;
	s = sin(chi);
	c = cos(chi);
	return sqrt(2.0 / (M_PI * x)) * (p * c - q * s);
}

static double y_asymp(double n, double x)
{
	double p, q, chi, s, c;
	asymp_pq(n, x, &p, &q);
	chi = x - n * M_PI_2 - M_PI_4;
	s = sin(chi);
	c = cos(chi);
	return sqrt(2.0 / (M_PI * x)) * (p * s + q * c);
}

double j0(double x)
{
	if (x != x) return x;
	x = fabs(x);
	if (isinf(x)) return 0.0;
	if (x >= 8.0) return j_asymp(0, x);
	return j_series(0, x);
}

double j1(double x)
{
	double ax, r;

	if (x != x) return x;
	ax = fabs(x);
	if (isinf(ax)) return 0.0;
	r = ax >= 8.0 ? j_asymp(1, ax) : j_series(1, ax);
	return x < 0.0 ? -r : r;
}

/* Downward (Miller's algorithm) evaluation of J_n(x) for n >= 2,
 * x >= 0.  Recurs from an arbitrary seed at an order well above n
 * down to 0, tracking the (unnormalized) value at k == n along the
 * way, then rescales the whole run by matching the run's own J_0
 * against j0(x). */
static double jn_miller(unsigned int n, double x) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	uint64_t nstart, k;
	double jkp1, jk, jkm1, j_target;

	nstart = (uint64_t)n + (uint64_t)sqrt(40.0 * (double)n) + 10;
	if (nstart < (uint64_t)n + 20) nstart = (uint64_t)n + 20;

	jkp1 = 0.0;
	jk = 1e-30;
	j_target = 0.0;

	for (k = nstart; k > 0; k--) {
		jkm1 = (2.0 * (double)k / x) * jk - jkp1;
		if (k - 1 == n) j_target = jkm1;
		jkp1 = jk;
		jk = jkm1;
		if (fabs(jk) > 1e250) {
			jkp1 *= 1e-250;
			jk *= 1e-250;
			j_target *= 1e-250;
		}
	}
	/* jk now holds the run's own (unnormalized) J_0(x). */
	if (jk == 0.0) return 0.0;
	return j_target * (j0(x) / jk);
}

static int jn_underflows(unsigned int n, double x) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	double nd = (double)n;
	double log_bound;

	/* From the absolute-value sum of J_n's defining series:
	 *
	 * |J_n(x)| <= (x/2)^n / n! * exp(x^2 / (4(n+1))).
	 *
	 * If even that upper bound is below the smallest positive double,
	 * zero is the only representable result.  Besides avoiding pointless
	 * work, this gives extreme int orders a finite path into Miller's
	 * algorithm without approximating the transition region n ~= x. */
	if (x == 0.0) return 1;
	if (x * x > 2.0 * (nd + 1.0)) return 0;
	log_bound = nd * log(x * 0.5) - lgamma(nd + 1.0)
		+ x * x / (4.0 * (nd + 1.0));
	return log_bound < log(DBL_MIN) + log(DBL_EPSILON);
}

/* Linear recurrences are both unnecessary and an effective denial of
 * service once the order itself is enormous.  The leading uniform Airy
 * expansion is valid on both sides of the turning point x=n and, unlike
 * separate Debye expansions, stays finite at that point.  It is therefore
 * the bounded fallback for orders above this file's practical recurrence
 * limit.  Far beyond the turning point the ordinary Hankel expansion above
 * is more accurate and cheaper still. */
#define RECURRENCE_ORDER_LIMIT 10000u

static void airy_pair(double x, double *ai, double *bi)
{
	const double ai0 = 0.35502805388781723926;
	const double aip0 = -0.25881940379280679841;
	const double bi0 = 0.61492662744600073515;
	const double bip0 = 0.44828835735382635791;
	double a, amp, phase;

	if (x > 5.0) {
		phase = (2.0 / 3.0) * x * sqrt(x);
		amp = 1.0 / (sqrt(M_PI) * sqrt(sqrt(x)));
		*ai = 0.5 * amp * exp(-phase);
		*bi = amp * exp(phase);
		return;
	}
	if (x < -5.0) {
		a = -x;
		phase = (2.0 / 3.0) * a * sqrt(a) + M_PI_4;
		amp = 1.0 / (sqrt(M_PI) * sqrt(sqrt(a)));
		*ai = amp * sin(phase);
		*bi = amp * cos(phase);
		return;
	}
	{
		double ca[64] = { ai0, aip0, 0.0 };
		double cb[64] = { bi0, bip0, 0.0 };
		int i;
		for (i = 1; i < 62; i++) {
			ca[i + 2] = ca[i - 1] / ((double)(i + 2) * (i + 1));
			cb[i + 2] = cb[i - 1] / ((double)(i + 2) * (i + 1));
		}
		*ai = ca[63];
		*bi = cb[63];
		for (i = 62; i >= 0; i--) {
			*ai = *ai * x + ca[i];
			*bi = *bi * x + cb[i];
		}
	}
}

static void large_order_uniform(unsigned int n, double x, double *j, double *y) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	double nu = (double)n;
	double z = x / nu;
	double zeta, factor, eta, q, ai, bi;
	double nu13 = cbrt(nu);
	const double cbrt2 = 1.2599210498948731648;

	if (z == 0.0) {
		*j = 0.0;
		*y = -HUGE_VAL;
		return;
	}
	/* The exact zeta formula subtracts two nearly equal quantities at
	 * the turning point.  Its analytic limit avoids that cancellation
	 * and is the part that makes x==n itself well defined. */
	if (fabs(z - 1.0) < 1e-4) {
		zeta = cbrt2 * (1.0 - z);
		factor = cbrt2;
	} else if (z < 1.0) {
		q = sqrt(1.0 - z * z);
		eta = log((1.0 + q) / z) - q;
		zeta = pow(1.5 * eta, 2.0 / 3.0);
		factor = sqrt(sqrt(4.0 * zeta / (1.0 - z * z)));
	} else {
		q = sqrt(z * z - 1.0);
		eta = q - acos(1.0 / z);
		zeta = -pow(1.5 * eta, 2.0 / 3.0);
		factor = sqrt(sqrt(-4.0 * zeta / (z * z - 1.0)));
	}
	airy_pair(nu13 * nu13 * zeta, &ai, &bi);
	*j = factor * ai / nu13;
	*y = -factor * bi / nu13;
}

static void large_order(unsigned int n, double x, double *j, double *y)
{
	double nu = (double)n;
	if (x > 8.0 * nu * nu) {
		*j = j_asymp(nu, x);
		*y = y_asymp(nu, x);
	} else {
		large_order_uniform(n, x, j, y);
	}
}

static double jn_recur(unsigned int n, double x)
{
	double ax = fabs(x);
	int neg = x < 0.0 && (n % 2);
	double r, unused;

	if (isinf(ax)) return 0.0;
	if (ax == 0.0) return 0.0;
	if (jn_underflows(n, ax)) return neg ? -0.0 : 0.0;
	if (n > RECURRENCE_ORDER_LIMIT) large_order(n, ax, &r, &unused);
	else r = jn_miller(n, ax);
	return neg ? -r : r;
}

double jn(int n, double x) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	unsigned int order;
	int negate;
	double r;

	if (x != x) return x;
	/* n+1 is representable even at INT_MIN; negate that first, then add
	 * the final unit in unsigned space where INT_MAX+1 is representable. */
	order = n < 0 ? (unsigned int)(-(n + 1)) + 1u : (unsigned int)n;
	negate = n < 0 && (order % 2);       /* J_-n = (-1)^n J_n */
	if (order == 0) r = j0(x);
	else if (order == 1) r = j1(x);
	else r = jn_recur(order, x);
	return negate ? -r : r;
}

static double y0_series(double x)
{
	const double gamma = 0.5772156649015328606065121;
	double half = x * 0.5, half2 = half * half;
	double a = 1.0;
	double hk = 0.0;
	double sum = 0.0;
	double sign = 1.0;
	int k;

	for (k = 1; k <= 60; k++) {
		double term;
		a *= half2 / ((double)k * (double)k);
		hk += 1.0 / (double)k;
		term = sign * hk * a;
		sum += term;
		sign = -sign;
		if (k > 3 && fabs(term) < 1e-17 * fabs(sum)) break;
	}
	return (2.0 / M_PI) * ((log(half) + gamma) * j_series(0, x) + sum);
}

static double y1_series(double x)
{
	const double gamma = 0.5772156649015328606065121;
	double half = x * 0.5, half2 = half * half;
	double a = half;         /* a_0 = (x/2)^1 / (0! 1!) */
	double hk = 0.0, hk1 = 1.0;    /* H_0 = 0, H_1 = 1 */
	double sum = 0.0;
	double sign = 1.0;
	int k;

	for (k = 0; k <= 60; k++) {
		double term = sign * (hk + hk1) * a;
		sum += term;
		a *= half2 / ((double)(k + 1) * (double)(k + 2));
		hk = hk1;
		hk1 += 1.0 / (double)(k + 2);
		sign = -sign;
		if (k > 3 && fabs(term) < 1e-17 * fabs(sum)) break;
	}
	return -2.0 / (M_PI * x) + (2.0 / M_PI) * (log(half) + gamma) * j_series(1, x) - sum / M_PI;
}

double y0(double x)
{
	if (x != x) return x;
	if (x < 0.0) {
		feraiseexcept(FE_INVALID);
		return NAN;
	}
	if (x == 0.0) {
		feraiseexcept(FE_DIVBYZERO);
		return -HUGE_VAL;
	}
	if (isinf(x)) return 0.0;
	return x >= 8.0 ? y_asymp(0, x) : y0_series(x);
}

double y1(double x)
{
	if (x != x) return x;
	if (x < 0.0) {
		feraiseexcept(FE_INVALID);
		return NAN;
	}
	if (x == 0.0) {
		feraiseexcept(FE_DIVBYZERO);
		return -HUGE_VAL;
	}
	if (isinf(x)) return 0.0;
	return x >= 8.0 ? y_asymp(1, x) : y1_series(x);
}

double yn(int n, double x) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	double ym1, y, ynext, unused;
	unsigned int order, k;
	int negate;

	if (x != x) return x;
	if (x < 0.0) {
		feraiseexcept(FE_INVALID);
		return NAN;
	}
	if (x == 0.0) {
		feraiseexcept(FE_DIVBYZERO);
		return -HUGE_VAL;
	}
	if (isinf(x)) return 0.0;

	order = n < 0 ? (unsigned int)(-(n + 1)) + 1u : (unsigned int)n;
	negate = n < 0 && (order % 2);       /* Y_-n = (-1)^n Y_n */
	if (order == 0) return y0(x);
	if (order == 1) return negate ? -y1(x) : y1(x);
	if (order > RECURRENCE_ORDER_LIMIT) {
		large_order(order, x, &unused, &y);
		return negate ? -y : y;
	}

	/* Stable upward recurrence. */
	ym1 = y0(x);
	y = y1(x);
	for (k = 1; k < order; k++) {
		ynext = (2.0 * (double)k / x) * y - ym1;
		if (isinf(ynext)) return negate ? -ynext : ynext;
		ym1 = y;
		y = ynext;
	}
	return negate ? -y : y;
}
