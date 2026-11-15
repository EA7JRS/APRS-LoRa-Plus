/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "astro.h"

#include <cmath>

#include "celnav.h"
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
    // Lugar aparente con precision de almanaque (celnav: VSOP87 recortada, nutacion y aberracion; 0,1')
    double gha, dec;
    sunGhaDec(julianDay(y, m, d, utc), gha, dec);
    bodyAltAz(gha, dec, lat, lon, alt, az);
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

void moonPosition(int y, int mo, int d, double utc, double lat, double lon, double& alt, double& az, double& dist) {
    double gha, decDeg;
    moonGhaDec(julianDay(y, mo, d, utc), gha, decDeg, dist);          // Meeus 47 completo, en celnav
    const double H = deg2rad(gha + lon), la = deg2rad(lat), dec = deg2rad(decDeg);
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
