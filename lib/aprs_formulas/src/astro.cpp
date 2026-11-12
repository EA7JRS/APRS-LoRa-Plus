/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "astro.h"

#include <cmath>

#include "geo.h"

namespace plotter {

int dayOfYear(int y, int m, int d) {
    static const int cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return cum[m - 1] + d + (leap && m > 2 ? 1 : 0);
}

// Ecuacion del tiempo (minutos) y declinacion (radianes) para la fraccion de anio g
static void solar(int y, int m, int d, double utcHours, double& eqMin, double& dec) {
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    double g = 2 * kPi / (leap ? 366.0 : 365.0) * (dayOfYear(y, m, d) - 1 + (utcHours - 12) / 24.0);
    eqMin = 229.18 * (0.000075 + 0.001868 * std::cos(g) - 0.032077 * std::sin(g) - 0.014615 * std::cos(2 * g) - 0.040849 * std::sin(2 * g));
    dec = 0.006918 - 0.399912 * std::cos(g) + 0.070257 * std::sin(g) - 0.006758 * std::cos(2 * g) + 0.000907 * std::sin(2 * g) -
          0.002697 * std::cos(3 * g) + 0.00148 * std::sin(3 * g);
}

double solarNoonUtcMin(int y, int m, int d, double lon) {
    double eq, dec;
    solar(y, m, d, 12, eq, dec);
    return 720 - 4 * lon - eq;
}

bool sunTimes(int y, int m, int d, double lat, double lon, double alt, double& rise, double& set) {
    double eq, dec;
    solar(y, m, d, 12, eq, dec);
    double la = deg2rad(lat);
    double c = (std::sin(deg2rad(alt)) - std::sin(la) * std::sin(dec)) / (std::cos(la) * std::cos(dec));
    if (c < -1 || c > 1) return false;
    double ha = rad2deg(std::acos(c)), noon = 720 - 4 * lon - eq;
    rise = noon - 4 * ha;
    set = noon + 4 * ha;
    return true;
}

void sunPosition(int y, int m, int d, double utc, double lat, double lon, double& alt, double& az) {
    // Meeus, cap. 25 (baja precision, ~0,01 grados): mejor que la serie de NOAA para apuntar una antena al sol
    const double jd = julianDay(y, m, d, utc), T = (jd + 69.0 / 86400.0 - 2451545.0) / 36525.0;
    const double L0 = 280.46646 + 36000.76983 * T + 0.0003032 * T * T;
    const double M = deg2rad(357.52911 + 35999.05029 * T - 0.0001537 * T * T);
    const double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * std::sin(M) + (0.019993 - 0.000101 * T) * std::sin(2 * M) +
                     0.000289 * std::sin(3 * M);
    const double om = deg2rad(125.04 - 1934.136 * T);
    const double lam = deg2rad(L0 + C - 0.00569 - 0.00478 * std::sin(om));
    const double eps = deg2rad(23.439291 - 0.0130042 * T + 0.00256 * std::cos(om));
    const double ra = std::atan2(std::cos(eps) * std::sin(lam), std::cos(lam));
    const double dec = std::asin(std::sin(eps) * std::sin(lam));
    const double Tu = (jd - 2451545.0) / 36525.0;
    double gmst = std::fmod(280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * Tu * Tu, 360.0);
    const double ha = deg2rad(gmst + lon) - ra, la = deg2rad(lat);
    alt = rad2deg(std::asin(std::sin(la) * std::sin(dec) + std::cos(la) * std::cos(dec) * std::cos(ha)));
    az = norm360(rad2deg(std::atan2(-std::sin(ha) * std::cos(dec), std::sin(dec) * std::cos(la) - std::cos(dec) * std::sin(la) * std::cos(ha))));
}

double julianDay(int y, int m, int d, double utc) {
    if (m <= 2) { y -= 1; m += 12; }
    int a = y / 100, b = 2 - a + a / 4;
    return std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (m + 1)) + d + b - 1524.5 + utc / 24.0;
}

double moonAgeDays(int y, int m, int d, double utc) {
    const double syn = 29.530588853, newMoon = 2451550.09766;       // luna nueva del 6 de enero de 2000
    double a = std::fmod(julianDay(y, m, d, utc) - newMoon, syn);
    return a < 0 ? a + syn : a;
}

double moonIllumination(double age) { return (1 - std::cos(2 * kPi * age / 29.530588853)) / 2; }

int moonPhase8(double age) {
    int p = int(std::floor(age / 29.530588853 * 8 + 0.5)) % 8;
    return p < 0 ? p + 8 : p;
}

const char* moonPhaseNameEs(int p) {
    static const char* n[] = {"Luna nueva", "Creciente", "Cuarto creciente", "Gibosa creciente",
                              "Luna llena", "Gibosa menguante", "Cuarto menguante", "Menguante"};
    return n[((p % 8) + 8) % 8];
}

const char* moonPhaseNameEn(int p) {
    static const char* n[] = {"New moon", "Waxing crescent", "First quarter", "Waxing gibbous",
                              "Full moon", "Waning gibbous", "Last quarter", "Waning crescent"};
    return n[((p % 8) + 8) % 8];
}

namespace {
struct LunarTerm { signed char d, m, mp, f; long l, r; };
// Meeus, tabla 47.A (longitud en 1e-6 grados, distancia en metros), los 32 primeros terminos
const LunarTerm kLr[] = {
    {0, 0, 1, 0, 6288774, -20905355}, {2, 0, -1, 0, 1274027, -3699111}, {2, 0, 0, 0, 658314, -2955968},
    {0, 0, 2, 0, 213618, -569925},    {0, 1, 0, 0, -185116, 48888},     {0, 0, 0, 2, -114332, -3149},
    {2, 0, -2, 0, 58793, 246158},     {2, -1, -1, 0, 57066, -152138},   {2, 0, 1, 0, 53322, -170733},
    {2, -1, 0, 0, 45758, -204586},    {0, 1, -1, 0, -40923, -129620},   {1, 0, 0, 0, -34720, 108743},
    {0, 1, 1, 0, -30383, 104755},     {2, 0, 0, -2, 15327, 10321},      {0, 0, 1, 2, -12528, 0},
    {0, 0, 1, -2, 10980, 79661},      {4, 0, -1, 0, 10675, -34782},     {0, 0, 3, 0, 10034, -23210},
    {4, 0, -2, 0, 8548, -21636},      {2, 1, -1, 0, -7888, 24208},      {2, 1, 0, 0, -6766, 30824},
    {1, 0, -1, 0, -5163, -8379},      {1, 1, 0, 0, 4987, -16675},       {2, -1, 1, 0, 4036, -12831},
    {2, 0, 2, 0, 3994, -10445},       {4, 0, 0, 0, 3861, -11650},       {2, 0, -3, 0, 3665, 14403},
    {0, 1, -2, 0, -2689, -7003},      {2, 0, -1, 2, -2602, 0},          {2, -1, -2, 0, 2390, 10056},
    {1, 0, 1, 0, -2348, 6322},        {2, -2, 0, 0, 2236, -9884},
};
// Tabla 47.B (latitud en 1e-6 grados), los 20 primeros
const LunarTerm kB[] = {
    {0, 0, 0, 1, 5128122, 0}, {0, 0, 1, 1, 280602, 0},  {0, 0, 1, -1, 277693, 0}, {2, 0, 0, -1, 173237, 0},
    {2, 0, -1, 1, 55413, 0},  {2, 0, -1, -1, 46271, 0}, {2, 0, 0, 1, 32573, 0},   {0, 0, 2, 1, 17198, 0},
    {2, 0, 1, -1, 9266, 0},   {0, 0, 2, -1, 8822, 0},   {2, -1, 0, -1, 8216, 0},  {2, 0, -2, -1, 4324, 0},
    {2, 0, 1, 1, 4200, 0},    {2, 1, 0, -1, -3359, 0},  {2, -1, -1, 1, 2463, 0},  {2, -1, 0, 1, 2211, 0},
    {2, -1, -1, -1, 2065, 0}, {0, 1, -1, -1, -1870, 0}, {4, 0, -1, -1, 1828, 0},  {0, 1, 0, 1, -1794, 0},
};

double nrm(double a) { a = std::fmod(a, 360.0); return a < 0 ? a + 360 : a; }
}  // namespace

void moonPosition(int y, int mo, int d, double utc, double lat, double lon, double& alt, double& az, double& dist) {
    const double jd = julianDay(y, mo, d, utc) + 69.0 / 86400.0;    // TT ~ UTC + 69 s
    const double T = (jd - 2451545.0) / 36525.0;
    const double Lp = nrm(218.3164477 + 481267.88123421 * T), D = nrm(297.8501921 + 445267.1114034 * T),
                 M = nrm(357.5291092 + 35999.0502909 * T), Mp = nrm(134.9633964 + 477198.8675055 * T),
                 F = nrm(93.2720950 + 483202.0175233 * T), E = 1 - 0.002516 * T - 0.0000074 * T * T;
    const double A1 = nrm(119.75 + 131.849 * T), A2 = nrm(53.09 + 479264.290 * T), A3 = nrm(313.45 + 481266.484 * T);
    double sl = 0, sr = 0, sb = 0;
    for (const auto& t : kLr) {
        double arg = deg2rad(t.d * D + t.m * M + t.mp * Mp + t.f * F);
        double e = t.m == 0 ? 1 : (t.m == 1 || t.m == -1 ? E : E * E);
        sl += e * t.l * std::sin(arg);
        sr += e * t.r * std::cos(arg);
    }
    for (const auto& t : kB) {
        double arg = deg2rad(t.d * D + t.m * M + t.mp * Mp + t.f * F);
        double e = t.m == 0 ? 1 : (t.m == 1 || t.m == -1 ? E : E * E);
        sb += e * t.l * std::sin(arg);
    }
    sl += 3958 * std::sin(deg2rad(A1)) + 1962 * std::sin(deg2rad(Lp - F)) + 318 * std::sin(deg2rad(A2));
    sb += -2235 * std::sin(deg2rad(Lp)) + 382 * std::sin(deg2rad(A3)) + 175 * std::sin(deg2rad(A1 - F)) +
          175 * std::sin(deg2rad(A1 + F)) + 127 * std::sin(deg2rad(Lp - Mp)) - 115 * std::sin(deg2rad(Lp + Mp));
    // Nutacion en longitud (terminos principales) y oblicuidad
    const double Om = nrm(125.04452 - 1934.136261 * T), Ls = nrm(280.4665 + 36000.7698 * T);
    const double dpsi = -17.20 * std::sin(deg2rad(Om)) - 1.32 * std::sin(deg2rad(2 * Ls)) - 0.23 * std::sin(deg2rad(2 * Lp)) +
                        0.21 * std::sin(deg2rad(2 * Om));
    const double deps = 9.20 * std::cos(deg2rad(Om)) + 0.57 * std::cos(deg2rad(2 * Ls)) + 0.10 * std::cos(deg2rad(2 * Lp)) -
                        0.09 * std::cos(deg2rad(2 * Om));
    const double lam = deg2rad(Lp + sl / 1e6 + dpsi / 3600.0), bet = deg2rad(sb / 1e6);
    dist = 385000.56 + sr / 1000.0;
    const double eps = deg2rad(23.4392911 - 0.0130042 * T + deps / 3600.0);
    const double ra = std::atan2(std::sin(lam) * std::cos(eps) - std::tan(bet) * std::sin(eps), std::cos(lam));
    const double dec = std::asin(std::sin(bet) * std::cos(eps) + std::cos(bet) * std::sin(eps) * std::sin(lam));
    // Tiempo sidereo aparente de Greenwich (con UT ~ UTC)
    const double jdu = jd - 69.0 / 86400.0, Tu = (jdu - 2451545.0) / 36525.0;
    const double gmst = nrm(280.46061837 + 360.98564736629 * (jdu - 2451545.0) + 0.000387933 * Tu * Tu +
                            dpsi / 3600.0 * std::cos(eps));
    const double H = deg2rad(gmst + lon) - ra, la = deg2rad(lat);
    // Paralaje: coordenadas topocentricas (Meeus, cap. 40)
    const double sinPi = 6378.14 / dist;
    const double u = std::atan(0.99664719 * std::tan(la));
    const double rs = 0.99664719 * std::sin(u), rc = std::cos(u);
    const double dra = std::atan2(-rc * sinPi * std::sin(H), std::cos(dec) - rc * sinPi * std::cos(H));
    const double dec2 = std::atan2((std::sin(dec) - rs * sinPi) * std::cos(dra), std::cos(dec) - rc * sinPi * std::cos(H));
    const double H2 = H - dra;
    alt = rad2deg(std::asin(std::sin(la) * std::sin(dec2) + std::cos(la) * std::cos(dec2) * std::cos(H2)));
    az = norm360(rad2deg(std::atan2(-std::sin(H2) * std::cos(dec2), std::sin(dec2) * std::cos(la) - std::cos(dec2) * std::sin(la) * std::cos(H2))));
}

void moonTimes(int y, int m, int d, double lat, double lon, double& rise, bool& hasRise, double& set, bool& hasSet) {
    const double h0 = -0.833;                         // centro con la refraccion y el semidiametro (topocentrica)
    hasRise = hasSet = false;
    rise = set = 0;
    double az, dist, prev;
    moonPosition(y, m, d, 0, lat, lon, prev, az, dist);
    prev -= h0;
    for (int i = 1; i <= 144; i++) {                  // cada 10 min
        double a;
        moonPosition(y, m, d, i / 6.0, lat, lon, a, az, dist);
        a -= h0;
        if ((prev < 0) != (a < 0)) {
            double t = (i - 1 + prev / (prev - a)) * 10.0;
            if (prev < 0 && !hasRise) { rise = t; hasRise = true; }
            else if (prev >= 0 && !hasSet) { set = t; hasSet = true; }
        }
        prev = a;
    }
}

double refractionDeg(double h) {
    if (h < -1.9) h = -1.9;
    return 1.02 / std::tan(deg2rad(h + 10.3 / (h + 5.11))) / 60.0;
}

void unixToUtc(long long t, int& y, int& m, int& d, double& utc) {
    long long days = t / 86400, rem = t % 86400;
    if (rem < 0) { rem += 86400; days--; }
    utc = rem / 3600.0;
    // Algoritmo civil de Howard Hinnant
    long long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = unsigned(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long yy = (long long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    d = int(doy - (153 * mp + 2) / 5 + 1);
    m = int(mp < 10 ? mp + 3 : mp - 9);
    y = int(yy + (m <= 2));
}

}  // namespace plotter
