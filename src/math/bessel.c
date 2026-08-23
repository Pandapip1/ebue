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
 *  - J_n, |n| >= 2: the three-term recurrence
 *      J_{k-1}(x) = (2k/x) J_k(x) - J_{k+1}(x)
 *    is numerically unstable computed upward when n exceeds x, so
 *    this file always uses Miller's algorithm instead: recur the same
 *    relation *downward* from an order well above n, starting from an
 *    arbitrary seed (unstable in the downward direction becomes
 *    stable), and normalize the whole run by comparing the downward
 *    run's own J_0 against this file's j0(x).
 *
 *  - Y_n, |n| >= 2: the same recurrence
 *      Y_{k+1}(x) = (2k/x) Y_k(x) - Y_{k-1}(x)
 *    is stable computed *upward*, so a plain upward recurrence from
 *    y0(x)/y1(x) is used directly.
 *
 * None of this is derived from any specific library's source (Cephes,
 * musl, glibc, Boost, ...) -- it is written from the public-domain
 * mathematical definitions above.
 */
#include <math.h>
#include <fenv.h>

static double j_series(int n, double x)
{
	double half = x * 0.5;
	double term = 1.0;
	double sum;
	int k;

	for (k = 1; k <= n; k++) term *= half / (double)k;
	sum = term;
	for (k = 1; k <= 60; k++) {
		term *= -(half * half) / ((double)k * (double)(n + k));
		sum += term;
		if (k > 3 && fabs(term) < 1e-17 * fabs(sum)) break;
	}
	return sum;
}

static void asymp_pq(int n, double x, double *p, double *q)
{
	double mu = 4.0 * (double)n * (double)n;
	double t = 1.0 / (8.0 * x);
	double t2 = t * t;

	*p = 1.0 - (mu - 1.0) * (mu - 9.0) * t2 * 0.5;
	*q = (mu - 1.0) * t - (mu - 1.0) * (mu - 9.0) * (mu - 25.0) * t2 * t / 6.0;
}

static double j_asymp(int n, double x)
{
	double p, q, chi, s, c;
	asymp_pq(n, x, &p, &q);
	chi = x - (double)n * M_PI_2 - M_PI_4;
	s = sin(chi);
	c = cos(chi);
	return sqrt(2.0 / (M_PI * x)) * (p * c - q * s);
}

static double y_asymp(int n, double x)
{
	double p, q, chi, s, c;
	asymp_pq(n, x, &p, &q);
	chi = x - (double)n * M_PI_2 - M_PI_4;
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
static double jn_miller(int n, double x)
{
	int nstart, k;
	double jkp1, jk, jkm1, j_target;

	nstart = n + (int)sqrt(40.0 * (double)n) + 10;
	if (nstart < n + 20) nstart = n + 20;

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

static double jn_recur(int n, double x)
{
	double ax = fabs(x);
	int neg = x < 0.0 && (n % 2);
	double r;

	if (isinf(ax)) return 0.0;
	if (ax == 0.0) return 0.0;
	r = jn_miller(n, ax);
	return neg ? -r : r;
}

double jn(int n, double x)
{
	if (x != x) return x;
	if (n < 0) {
		int m = -n;
		double r = jn(m, x);
		return (m % 2) ? -r : r;   /* J_{-n} = (-1)^n J_n */
	}
	if (n == 0) return j0(x);
	if (n == 1) return j1(x);
	return jn_recur(n, x);
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

double yn(int n, double x)
{
	double ym1, y, ynext;
	int k;

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

	if (n < 0) {
		int m = -n;
		double r = yn(m, x);
		return (m % 2) ? -r : r;   /* Y_{-n} = (-1)^n Y_n */
	}
	if (n == 0) return y0(x);
	if (n == 1) return y1(x);

	/* Stable upward recurrence. */
	ym1 = y0(x);
	y = y1(x);
	for (k = 1; k < n; k++) {
		ynext = (2.0 * (double)k / x) * y - ym1;
		ym1 = y;
		y = ynext;
	}
	return y;
}
