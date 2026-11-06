/*
 * Sustitutos de pow / fmod / asin / acos / tan para el calculo orbital.
 * En el ESP32 original con PSRAM el enlazador mete la libm entera en la IRAM (que va justa): estas versiones, que solo usan
 * sin, cos, atan2, sqrt y floor, evitan enlazar pow, e_fmod, asin, acos y tan. Solo cubren los exponentes que usa SGP4
 * (enteros, mitades y tercios).
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef SAT_MATH_H_
#define SAT_MATH_H_

#include <math.h>

static inline double satCbrt(double x) {
    if (x == 0.0) return 0.0;
    const double a = x < 0 ? -x : x;
    double r = a > 1.0 ? a / 3.0 : 1.0;
    for (int i = 0; i < 60; i++) {
        const double n = (2.0 * r + a / (r * r)) / 3.0;
        if (fabs(n - r) <= 1e-15 * n) { r = n; break; }
        r = n;
    }
    return x < 0 ? -r : r;
}

static inline double satIntPow(double x, long n) {
    const bool neg = n < 0;
    unsigned long k = neg ? (unsigned long)(-n) : (unsigned long)n;
    double r = 1.0, b = x;
    while (k) { if (k & 1UL) r *= b; b *= b; k >>= 1; }
    return neg ? 1.0 / r : r;
}

static inline double satPow(double x, double y) {
    const double yi = floor(y + 0.5);
    if (fabs(y - yi) < 1e-12 && fabs(yi) < 1000.0) return satIntPow(x, (long)yi);
    const double y2 = y * 2.0, y2i = floor(y2 + 0.5);
    if (fabs(y2 - y2i) < 1e-12 && fabs(y2i) < 1000.0) return satIntPow(sqrt(x), (long)y2i);
    const double y3 = y * 3.0, y3i = floor(y3 + 0.5);
    if (fabs(y3 - y3i) < 1e-9 && fabs(y3i) < 1000.0) return satIntPow(satCbrt(x), (long)y3i);
    return NAN;                                         // exponente que SGP4 no usa
}

static inline double satFmod(double x, double y) {
    const double q = x / y;
    return x - y * (q < 0 ? ceil(q) : floor(q));
}

static inline double satAsin(double x) { return atan2(x, sqrt(1.0 - x * x > 0.0 ? 1.0 - x * x : 0.0)); }
static inline double satAcos(double x) { return atan2(sqrt(1.0 - x * x > 0.0 ? 1.0 - x * x : 0.0), x); }
static inline double satTan(double x)  { return sin(x) / cos(x); }

#endif
