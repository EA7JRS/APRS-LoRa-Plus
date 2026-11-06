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
#include <stdint.h>

// Version propia de sin, cos, atan2, sqrt y floor en doble precision (nucleos de fdlibm). La libm de doble del ESP32 original va a la IRAM
// por la correccion de cache de la PSRAM y la desborda; estas van en flash. Exactitud ~1e-15 (se comprueba en tools/test_sat_core.cpp).

static inline double satFloor(double x) {
    if (!(fabs(x) < 4.0e15)) return x;
    double t = (double)(int64_t)x;
    if (t > x) t -= 1.0;
    return t;
}

static inline double satSqrt(double x) {
    if (!(x > 0.0)) return x == 0.0 ? 0.0 : NAN;
    double r = (double)sqrtf((float)x);
    if (r == 0.0 || !(r < 1e300)) r = x < 1.0 ? x : x * 0.5;
    for (int i = 0; i < 3; i++) r = 0.5 * (r + x / r);
    return r;
}

// sin y cos en [-pi/4, pi/4]
static inline double satKSin(double x) {
    const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03, S3 = -1.98412698298579493134e-04,
                 S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
    const double z = x * x;
    return x + x * z * (S1 + z * (S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)))));
}
static inline double satKCos(double x) {
    const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05,
                 C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
    const double z = x * x;
    return 1.0 - 0.5 * z + z * z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
}

// Reduce x a r en [-pi/4, pi/4] y devuelve el cuadrante (0..3). Valido para |x| < ~1e5 rad (SGP4 trabaja con angulos reducidos).
static inline int satReduce(double x, double& r) {
    const double INV_PIO2 = 6.36619772367581382433e-01;
    const double PIO2_1 = 1.57079632673412561417e+00, PIO2_1T = 6.07710050650619224932e-11;       // pi/2 en dos trozos (Cody-Waite)
    const double PIO2_2T = 2.02226624879595063154e-21;
    const double k = satFloor(x * INV_PIO2 + 0.5);
    r = (x - k * PIO2_1) - k * PIO2_1T;
    r -= k * PIO2_2T * 0.0;                                                                      // (el tercer trozo no hace falta con |k| < 1e5)
    long q = (long)k;
    return (int)(q & 3);
}

static inline double satSin(double x) {
    double r; const int q = satReduce(x, r);
    switch (q) { case 0: return satKSin(r); case 1: return satKCos(r); case 2: return -satKSin(r); default: return -satKCos(r); }
}
static inline double satCos(double x) {
    double r; const int q = satReduce(x, r);
    switch (q) { case 0: return satKCos(r); case 1: return -satKSin(r); case 2: return -satKCos(r); default: return satKSin(r); }
}

static inline double satAtan(double x) {
    const bool neg = x < 0; if (neg) x = -x;
    const bool inv = x > 1.0; if (inv) x = 1.0 / x;
    x = x / (1.0 + satSqrt(1.0 + x * x));                 // atan(x) = 2 atan(x / (1 + sqrt(1 + x^2))), dos veces
    x = x / (1.0 + satSqrt(1.0 + x * x));
    const double z = x * x;
    double term = x, sum = x;
    for (int n = 1; n < 12; n++) { term *= -z; sum += term / (2 * n + 1); }
    double r = 4.0 * sum;
    if (inv) r = 1.57079632679489661923 - r;
    return neg ? -r : r;
}

static inline double satAtan2(double y, double x) {
    const double SAT_PI = 3.14159265358979323846;
    if (x == 0.0) return y > 0 ? SAT_PI / 2 : (y < 0 ? -SAT_PI / 2 : 0.0);
    const double a = satAtan(y / x);
    if (x > 0) return a;
    return y >= 0 ? a + SAT_PI : a - SAT_PI;
}

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
    const double yi = satFloor(y + 0.5);
    if (fabs(y - yi) < 1e-12 && fabs(yi) < 1000.0) return satIntPow(x, (long)yi);
    const double y2 = y * 2.0, y2i = satFloor(y2 + 0.5);
    if (fabs(y2 - y2i) < 1e-12 && fabs(y2i) < 1000.0) return satIntPow(satSqrt(x), (long)y2i);
    const double y3 = y * 3.0, y3i = satFloor(y3 + 0.5);
    if (fabs(y3 - y3i) < 1e-9 && fabs(y3i) < 1000.0) return satIntPow(satCbrt(x), (long)y3i);
    return NAN;                                         // exponente que SGP4 no usa
}

static inline double satFmod(double x, double y) {
    const double q = x / y;
    return x - y * (q < 0 ? -satFloor(-q) : satFloor(q));
}

static inline double satAsin(double x) { return satAtan2(x, satSqrt(1.0 - x * x > 0.0 ? 1.0 - x * x : 0.0)); }
static inline double satAcos(double x) { return satAtan2(satSqrt(1.0 - x * x > 0.0 ? 1.0 - x * x : 0.0), x); }
static inline double satTan(double x)  { return satSin(x) / satCos(x); }

#endif
