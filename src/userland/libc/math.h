// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#ifndef MATH_H
#define MATH_H

#define M_PI        3.14159265358979323846
#define M_E         2.71828182845904523536
#define M_LN2       0.69314718055994530942
#define M_SQRT2     1.41421356237309504880
#define HUGE_VAL    (1e300 * 1e300)  
#define INFINITY    (__builtin_inff())
#define NAN         (__builtin_nanf(""))

#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4

typedef float float_t;
typedef double double_t;

#define signbit(x) __builtin_signbit(x)
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, (x))
#define isfinite(x) __builtin_isfinite(x)
#define isinf(x) __builtin_isinf(x)
#define isnan(x) __builtin_isnan(x)
#define isnormal(x) __builtin_isnormal(x)
#define isgreater(x, y) __builtin_isgreater((x), (y))
#define isgreaterequal(x, y) __builtin_isgreaterequal((x), (y))
#define isless(x, y) __builtin_isless((x), (y))
#define islessequal(x, y) __builtin_islessequal((x), (y))
#define islessgreater(x, y) __builtin_islessgreater((x), (y))
#define isunordered(x, y) __builtin_isunordered((x), (y))

double fabs(double x);
float fabsf(float x);

double fmod(double x, double y);
float fmodf(float x, float y);

double floor(double x);
float floorf(float x);

double ceil(double x);
float ceilf(float x);

double sin(double x);
double cos(double x);
double tan(double x);
float sinf(float x);
float cosf(float x);
float tanf(float x);
double sqrt(double x);
float sqrtf(float x);

double log(double x);
double log2(double x);
double log10(double x);
double exp(double x);
float logf(float x);
float log2f(float x);
float log10f(float x);
float expf(float x);
double ldexp(double x, int expn);
float ldexpf(float x, int expn);
long double ldexpl(long double x, int expn);
double frexp(double x, int *expn);
float frexpf(float x, int *expn);
double pow(double base, double exponent);
float powf(float base, float exponent);
double atan2(double y, double x);
double asin(double x);
double acos(double x);
float atan2f(float y, float x);
float asinf(float x);
float acosf(float x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
float sinhf(float x);
float coshf(float x);
float tanhf(float x);
double hypot(double x, double y);
float hypotf(float x, float y);
double fmin(double a, double b);
float fminf(float a, float b);
double fmax(double a, double b);
float fmaxf(float a, float b);
double fclamp(double x, double lo, double hi);

int abs(int x);
double atan(double x);
float atanf(float x);
double modf(double x, double *iptr);
float modff(float x, float *iptr);
double acosh(double x);
float acoshf(float x);
double asinh(double x);
float asinhf(float x);
double atanh(double x);
float atanhf(float x);
double cbrt(double x);
float cbrtf(float x);
double copysign(double x, double y);
float copysignf(float x, float y);
double erf(double x);
float erff(float x);
double erfc(double x);
float erfcf(float x);
double exp2(double x);
float exp2f(float x);
double expm1(double x);
float expm1f(float x);
double fdim(double x, double y);
float fdimf(float x, float y);
double fma(double x, double y, double z);
float fmaf(float x, float y, float z);
int ilogb(double x);
int ilogbf(float x);
double lgamma(double x);
float lgammaf(float x);
long long llrint(double x);
long long llrintf(float x);
long long llround(double x);
long long llroundf(float x);
double log1p(double x);
float log1pf(float x);
double logb(double x);
float logbf(float x);
long lrint(double x);
long lrintf(float x);
long lround(double x);
long lroundf(float x);
double nan(const char *tagp);
float nanf(const char *tagp);
double nearbyint(double x);
float nearbyintf(float x);
double nextafter(double x, double y);
float nextafterf(float x, float y);
double nexttoward(double x, long double y);
float nexttowardf(float x, long double y);
double remainder(double x, double y);
float remainderf(float x, float y);
double remquo(double x, double y, int *quo);
float remquof(float x, float y, int *quo);
double rint(double x);
float rintf(float x);
double round(double x);
float roundf(float x);
double scalbln(double x, long n);
float scalblnf(float x, long n);
double scalbn(double x, int n);
float scalbnf(float x, int n);
double tgamma(double x);
float tgammaf(float x);
double trunc(double x);
float truncf(float x);

long double acosl(long double x);
long double asinl(long double x);
long double atanl(long double x);
long double atan2l(long double y, long double x);
long double ceill(long double x);
long double cosl(long double x);
long double coshl(long double x);
long double expl(long double x);
long double fabsl(long double x);
long double floorl(long double x);
long double fmodl(long double x, long double y);
long double frexpl(long double x, int *expn);
long double logl(long double x);
long double log10l(long double x);
long double modfl(long double x, long double *iptr);
long double powl(long double base, long double exponent);
long double sinl(long double x);
long double sinhl(long double x);
long double sqrtl(long double x);
long double tanl(long double x);
long double tanhl(long double x);
long double acoshl(long double x);
long double asinhl(long double x);
long double atanhl(long double x);
long double cbrtl(long double x);
long double copysignl(long double x, long double y);
long double erfl(long double x);
long double erfcl(long double x);
long double exp2l(long double x);
long double expm1l(long double x);
long double fdiml(long double x, long double y);
long double fmal(long double x, long double y, long double z);
long double fmaxl(long double x, long double y);
long double fminl(long double x, long double y);
long double hypotl(long double x, long double y);
int ilogbl(long double x);
long double lgammal(long double x);
long long llrintl(long double x);
long long llroundl(long double x);
long double log1pl(long double x);
long double log2l(long double x);
long double logbl(long double x);
long lrintl(long double x);
long lroundl(long double x);
long double nanl(const char *tagp);
long double nearbyintl(long double x);
long double nextafterl(long double x, long double y);
long double nexttowardl(long double x, long double y);
long double remainderl(long double x, long double y);
long double remquol(long double x, long double y, int *quo);
long double rintl(long double x);
long double roundl(long double x);
long double scalblnl(long double x, long n);
long double scalbnl(long double x, int n);
long double tgammal(long double x);
long double truncl(long double x);

#endif /* MATH_H */
