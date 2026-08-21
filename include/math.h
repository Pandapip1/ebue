#ifndef _MATH_H
#define _MATH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_float_t
#define __NEED_double_t
#include <bits/alltypes.h>

#define HUGE_VALF (1.0f/0.0f)
#define HUGE_VAL  (1.0/0.0)
#define HUGE_VALL (1.0L/0.0L)
#define INFINITY  HUGE_VALF
#define NAN       (0.0f/0.0f)

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

int __fpclassify(double);
int __fpclassifyf(float);
int __fpclassifyl(long double);
int __signbit(double);
int __signbitf(float);
int __signbitl(long double);

#define fpclassify(x) ( \
	sizeof(x) == sizeof(float) ? __fpclassifyf(x) : \
	sizeof(x) == sizeof(double) ? __fpclassify(x) : \
	__fpclassifyl(x) )

#define isinf(x) (fpclassify(x) == FP_INFINITE)
#define isnan(x) (fpclassify(x) == FP_NAN)
#define isnormal(x) (fpclassify(x) == FP_NORMAL)
#define isfinite(x) (fpclassify(x) > FP_INFINITE)

#define signbit(x) ( \
	sizeof(x) == sizeof(float) ? __signbitf(x) : \
	sizeof(x) == sizeof(double) ? __signbit(x) : \
	__signbitl(x) )

#define isunordered(x,y) (isnan((x)) ? ((void)(y),1) : isnan((y)))
#define isless(x,y) (!isunordered(x,y) && (x) < (y))
#define islessequal(x,y) (!isunordered(x,y) && (x) <= (y))
#define islessgreater(x,y) (!isunordered(x,y) && ((x) < (y) || (x) > (y)))
#define isgreater(x,y) (!isunordered(x,y) && (x) > (y))
#define isgreaterequal(x,y) (!isunordered(x,y) && (x) >= (y))

#define MATH_ERRNO  1
#define MATH_ERREXCEPT 2
#define math_errhandling 2

#define FP_ILOGBNAN (-1-0x7fffffff)
#define FP_ILOGB0 FP_ILOGBNAN

double      fabs(double);
float       fabsf(float);
long double fabsl(long double);
double      floor(double);
float       floorf(float);
long double floorl(long double);
double      ceil(double);
float       ceilf(float);
long double ceill(long double);
double      trunc(double);
float       truncf(float);
long double truncl(long double);
double      round(double);
float       roundf(float);
long double roundl(long double);
double      sqrt(double);
float       sqrtf(float);
long double sqrtl(long double);
double      fmod(double, double);
float       fmodf(float, float);
long double fmodl(long double, long double);
double      frexp(double, int *);
float       frexpf(float, int *);
long double frexpl(long double, int *);
double      ldexp(double, int);
float       ldexpf(float, int);
long double ldexpl(long double, int);
double      scalbn(double, int);
float       scalbnf(float, int);
long double scalbnl(long double, int);
double      modf(double, double *);
float       modff(float, float *);
long double modfl(long double, long double *);
double      pow(double, double);
float       powf(float, float);
long double powl(long double, long double);
double      exp(double);
float       expf(float);
long double expl(long double);
double      log(double);
float       logf(float);
long double logl(long double);
double      log10(double);
float       log10f(float);
long double log10l(long double);
double      log2(double);
float       log2f(float);
long double log2l(long double);
double      sin(double);
float       sinf(float);
long double sinl(long double);
double      cos(double);
float       cosf(float);
long double cosl(long double);
double      tan(double);
float       tanf(float);
long double tanl(long double);
double      atan(double);
float       atanf(float);
long double atanl(long double);
double      atan2(double, double);
float       atan2f(float, float);
long double atan2l(long double, long double);
double      fmax(double, double);
double      fmin(double, double);
double      copysign(double, double);
float       copysignf(float, float);
long double copysignl(long double, long double);
double      nan(const char *);
float       nanf(const char *);
long double nanl(const char *);
double      hypot(double, double);
double      rint(double);
long        lround(double);
long long   llround(double);
long        lrint(double);
long long   llrint(double);

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#define M_E             2.7182818284590452354
#define M_LOG2E         1.4426950408889634074
#define M_LOG10E        0.43429448190325182765
#define M_LN2           0.69314718055994530942
#define M_LN10          2.30258509299404568402
#define M_PI            3.14159265358979323846
#define M_PI_2          1.57079632679489661923
#define M_PI_4          0.78539816339744830962
#define M_1_PI          0.31830988618379067154
#define M_2_PI          0.63661977236758134308
#define M_2_SQRTPI      1.12837916709551257390
#define M_SQRT2         1.41421356237309504880
#define M_SQRT1_2       0.70710678118654752440
#endif

#ifdef __cplusplus
}
#endif

#endif
