// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#ifndef MATH_H
#define MATH_H

#define M_PI        3.14159265358979323846
#define M_E         2.71828182845904523536
#define M_LN2       0.69314718055994530942
#define M_SQRT2     1.41421356237309504880
#define HUGE_VAL    (1e300 * 1e300)  

double fabs(double x);

double fmod(double x, double y);

double floor(double x);

double ceil(double x);

double sin(double x);
double cos(double x);
double tan(double x);
double sqrt(double x);

double log(double x);
double log2(double x);
double log10(double x);
double exp(double x);
double ldexp(double x, int expn);
long double ldexpl(long double x, int expn);
double frexp(double x, int *expn);
double pow(double base, double exponent);
double atan2(double y, double x);
double asin(double x);
double acos(double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double hypot(double x, double y);
double fmin(double a, double b);
double fmax(double a, double b);
double fclamp(double x, double lo, double hi);

#endif /* MATH_H */
