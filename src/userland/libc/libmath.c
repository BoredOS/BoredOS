// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include "math.h"
static double _pow_int(double b, int e) {
    if (e == 0) return 1.0;
    if (e < 0) { return 1.0 / _pow_int(b, -e); }
    double r = 1.0;
    while (e > 0) {
        if (e & 1) r *= b;
        b *= b;
        e >>= 1;
    }
    return r;
}
double fabs(double x) {
    return x < 0.0 ? -x : x;
}
double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    return x - (int)(x / y) * y;
}

double floor(double x) {
    int i = (int)x;
    return (x < 0.0 && (double)i != x) ? (double)(i - 1) : (double)i;
}

double ceil(double x) {
    int i = (int)x;
    return (x > 0.0 && (double)i != x) ? (double)(i + 1) : (double)i;
}

double sin(double x) {
    x = fmod(x, 2.0 * M_PI);
    if (x > M_PI)  x -= 2.0 * M_PI;
    if (x < -M_PI) x += 2.0 * M_PI;

    double x2 = x * x;
    double term = x;
    double sum  = x;
    for (int i = 1; i <= 8; i++) {
        term *= -x2 / ((2*i) * (2*i + 1));
        sum  += term;
    }
    return sum;
}

double cos(double x) {
    return sin(x + M_PI / 2.0);
}

double tan(double x) {
    double c = cos(x);
    if (fabs(c) < 1e-10) return 1e15;   
    return sin(x) / c;
}

double sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double g = x * 0.5;
    for (int i = 0; i < 25; i++) {
        g = (g + x / g) * 0.5;
    }
    return g;
}

double log(double x) {
    if (x <= 0.0) return -1e30;

    /* Normalize x to [1, 2) so the atanh-series converges quickly. */
    int e = 0;
    while (x >= 2.0) { x *= 0.5; e++; }
    while (x < 1.0)  { x *= 2.0; e--; }

    double y = (x - 1.0) / (x + 1.0);
    double y2 = y * y;
    double term = y;
    double sum = 0.0;
    for (int n = 1; n <= 31; n += 2) {
        sum += term / (double)n;
        term *= y2;
    }
    return 2.0 * sum + (double)e * M_LN2;
}

double log2(double x) {
    // log2(x) = ln(x) / ln(2)
    return log(x) / M_LN2;
}

double log10(double x) {
    return log(x) / 2.302585092994046;
}

double exp(double x) {
    if (x >  700.0) return 1e300;   
    if (x < -700.0) return 0.0;     

    int k = (int)(x / M_LN2);
    if (x < 0.0 && (double)k * M_LN2 > x) k--;
    double r = x - (double)k * M_LN2;

    double sum = 1.0, term = 1.0;
    for (int i = 1; i <= 20; i++) {
        term *= r / (double)i;
        sum  += term;
    }

    double result = sum;
    if (k >= 0) { for (int i = 0; i < k;  i++) result *= 2.0; }
    else        { for (int i = 0; i < -k; i++) result /= 2.0; }
    return result;
}
double pow(double base, double exponent) {
    if (base == 0.0)  return 0.0;
    if (exponent == 0.0) return 1.0;
    int ie = (int)exponent;
    if ((double)ie == exponent) return _pow_int(base, ie);

    if (base < 0.0) return 0.0;  
    return exp(exponent * log(base));
}

double sinh(double x) {
    double ep = exp(x);
    double em = exp(-x);
    return (ep - em) * 0.5;
}

double cosh(double x) {
    double ep = exp(x);
    double em = exp(-x);
    return (ep + em) * 0.5;
}

double tanh(double x) {
    double e2 = exp(2.0 * x);
    return (e2 - 1.0) / (e2 + 1.0);
}

double hypot(double x, double y) {
    return sqrt(x*x + y*y);
}

double fmin(double a, double b) {
    return a < b ? a : b;
}

double fmax(double a, double b) {
    return a > b ? a : b;
}

double fclamp(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

double ldexp(double x, int exp) {
    if (exp >= 0) { while (exp--) x *= 2.0; }
    else { while (exp++) x /= 2.0; }
    return x;
}

long double ldexpl(long double x, int exp) {
    if (exp >= 0) { while (exp--) x *= 2.0L; }
    else { while (exp++) x /= 2.0L; }
    return x;
}
