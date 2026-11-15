/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "celnav.h"

#include <cctype>
#include <cmath>

#include "geo.h"

namespace plotter {

namespace {

const double kAs = kPi / (180.0 * 3600.0);       // un segundo de arco en radianes
const double kDeltaT = 69.2;                     // TT - UTC en segundos (2026; cambia ~0,1 s al anio: 0,0004' en el Sol)

const NavStar kStars[] = {
#include "celnav_stars.inc"
};
const int kStarCount = int(sizeof(kStars) / sizeof(kStars[0]));

struct Term { double a, b, c; };
// Longitud heliocentrica de la Tierra, VSOP87 (Meeus, apendice III), amplitudes en 1e-8 rad
const Term kL0[] = {
    {175347046, 0, 0}, {3341656, 4.6692568, 6283.07585}, {34894, 4.6261, 12566.1517}, {3497, 2.7441, 5753.3849},
    {3418, 2.8289, 3.5231}, {3136, 3.6277, 77713.7715}, {2676, 4.4181, 7860.4194}, {2343, 6.1352, 3930.2097},
    {1324, 0.7425, 11506.7698}, {1273, 2.0371, 529.691}, {1199, 1.1096, 1577.3435}, {990, 5.233, 5884.927},
    {902, 2.045, 26.298}, {857, 3.508, 398.149}, {780, 1.179, 5223.694}, {753, 2.533, 5507.553}, {505, 4.583, 18849.228},
    {492, 4.205, 775.523}, {357, 2.92, 0.067}, {317, 5.849, 11790.629}, {284, 1.899, 796.298}, {271, 0.315, 10977.079},
    {243, 0.345, 5486.778}, {206, 4.806, 2544.314}, {205, 1.869, 5573.143}, {202, 2.458, 6069.777}, {156, 0.833, 213.299},
    {132, 3.411, 2942.463}, {126, 1.083, 20.775}, {115, 0.645, 0.98}, {103, 0.636, 4694.003}, {102, 0.976, 15720.839},
    {102, 4.267, 7.114}, {99, 6.21, 2146.17}, {98, 0.68, 155.42}, {86, 5.98, 161000.69}, {85, 1.3, 6275.96},
    {85, 3.67, 71430.7}, {80, 1.81, 17260.15}};
const Term kL1[] = {
    {628331966747.0, 0, 0}, {206059, 2.678235, 6283.07585}, {4303, 2.6351, 12566.1517}, {425, 1.59, 3.523},
    {119, 5.796, 26.298}, {109, 2.966, 1577.344}, {93, 2.59, 18849.23}, {72, 1.14, 529.69}, {68, 1.87, 398.15},
    {67, 4.41, 5507.55}, {59, 2.89, 5223.69}, {56, 2.17, 155.42}, {45, 0.4, 796.3}, {36, 0.47, 775.52}, {29, 2.65, 7.11},
    {21, 5.34, 0.98}, {19, 1.85, 5486.78}, {19, 4.97, 213.3}, {17, 2.99, 6275.96}, {16, 0.03, 2544.31}};
const Term kL2[] = {{52919, 0, 0}, {8720, 1.0721, 6283.0758}, {309, 0.867, 12566.152}, {27, 0.05, 3.52}, {16, 5.19, 26.3},
                    {16, 3.68, 155.42}, {10, 0.76, 18849.23}, {9, 2.06, 77713.77}, {7, 0.83, 775.52}, {5, 4.66, 1577.34}};
const Term kL3[] = {{289, 5.844, 6283.076}, {35, 0, 0}, {17, 5.49, 12566.15}};

struct Series { const Term* t; int n; };
#define SER(x) {x, int(sizeof(x) / sizeof(x[0]))}
struct MoonLr { signed char d, m, mp, f; long l, r; };
struct MoonB { signed char d, m, mp, f; long b; };
#include "celnav_bodies.inc"
#undef SER
const Series kEarthL[] = {{kL0, int(sizeof(kL0) / sizeof(kL0[0]))}, {kL1, int(sizeof(kL1) / sizeof(kL1[0]))},
                          {kL2, int(sizeof(kL2) / sizeof(kL2[0]))}, {kL3, int(sizeof(kL3) / sizeof(kL3[0]))}};

// Suma de una coordenada VSOP87 (radianes o UA); tau en milenios julianos desde J2000
template <int N> double vsop(const Series (&s)[N], double tau) {
    double tot = 0, tp = 1;
    for (int k = 0; k < N; k++, tp *= tau) {
        double sum = 0;
        for (int i = 0; i < s[k].n; i++) sum += s[k].t[i].a * std::cos(s[k].t[i].b + s[k].t[i].c * tau);
        tot += sum * tp;
    }
    return tot * 1e-8;
}

template <int N> double series(const Term (&t)[N], double tau) {
    double s = 0;
    for (int i = 0; i < N; i++) s += t[i].a * std::cos(t[i].b + t[i].c * tau);
    return s;
}

double norm2pi(double a) { a = std::fmod(a, 2 * kPi); return a < 0 ? a + 2 * kPi : a; }

// Nutacion en longitud y en oblicuidad (radianes) y oblicuidad verdadera; T en siglos julianos (TT) desde J2000
void nutation(double T, double& dpsi, double& deps, double& eps) {
    const double om = deg2rad(125.04452 - 1934.136261 * T);
    const double L = deg2rad(280.4665 + 36000.7698 * T), Lm = deg2rad(218.3165 + 481267.8813 * T);
    dpsi = (-17.20 * std::sin(om) - 1.32 * std::sin(2 * L) - 0.23 * std::sin(2 * Lm) + 0.21 * std::sin(2 * om)) * kAs;
    deps = (9.20 * std::cos(om) + 0.57 * std::cos(2 * L) + 0.10 * std::cos(2 * Lm) - 0.09 * std::cos(2 * om)) * kAs;
    eps = (84381.448 - 46.8150 * T - 0.00059 * T * T + 0.001813 * T * T * T) * kAs + deps;
}

double centuriesTT(double jd) { return (jd + kDeltaT / 86400.0 - 2451545.0) / 36525.0; }

// Longitud geometrica del Sol (equinoccio de la fecha, sin nutacion ni aberracion) y distancia en UA
void sunGeometric(double T, double& lon, double& R) {
    const double tau = T / 10.0;
    const double L = (series(kL0, tau) + tau * (series(kL1, tau) + tau * (series(kL2, tau) + tau * (series(kL3, tau) + tau * -114.0)))) * 1e-8;
    lon = norm2pi(L + kPi) - 0.09033 * kAs;                         // heliocentrica -> geocentrica, y al sistema FK5
    const double M = deg2rad(357.52911 + 35999.05029 * T), e = 0.016708634 - 0.000042037 * T;
    const double C = deg2rad((1.914602 - 0.004817 * T) * std::sin(M) + 0.019993 * std::sin(2 * M) + 0.000289 * std::sin(3 * M));
    R = 1.000001018 * (1 - e * e) / (1 + e * std::cos(M + C));
}

void sunRaDec(double jd, double& ra, double& dec, double& R) {
    const double T = centuriesTT(jd);
    double lon, dpsi, deps, eps;
    sunGeometric(T, lon, R);
    nutation(T, dpsi, deps, eps);
    const double lam = lon + dpsi - 20.4898 * kAs / R;              // nutacion y aberracion
    ra = norm2pi(std::atan2(std::cos(eps) * std::sin(lam), std::cos(lam)));
    dec = std::asin(std::sin(eps) * std::sin(lam));
}

double gastRad(double jd) {
    const double d = jd - 2451545.0, Tu = d / 36525.0;
    const double gmst = 280.46061837 + 360.98564736629 * d + 0.000387933 * Tu * Tu - Tu * Tu * Tu / 38710000.0;
    double dpsi, deps, eps;
    nutation(centuriesTT(jd), dpsi, deps, eps);
    return norm2pi(deg2rad(std::fmod(gmst, 360.0)) + dpsi * std::cos(eps));
}

void eclToEq(double lam, double bet, double eps, double& ra, double& dec) {
    ra = norm2pi(std::atan2(std::sin(lam) * std::cos(eps) - std::tan(bet) * std::sin(eps), std::cos(lam)));
    dec = std::asin(std::sin(bet) * std::cos(eps) + std::cos(bet) * std::sin(eps) * std::sin(lam));
}

void planetLbr(Planet p, double tau, double& L, double& B, double& R) {
    switch (p) {
        case Planet::Venus: L = vsop(kVenusL, tau); B = vsop(kVenusB, tau); R = vsop(kVenusR, tau); break;
        case Planet::Mars: L = vsop(kMarsL, tau); B = vsop(kMarsB, tau); R = vsop(kMarsR, tau); break;
        case Planet::Jupiter: L = vsop(kJupiterL, tau); B = vsop(kJupiterB, tau); R = vsop(kJupiterR, tau); break;
        default: L = vsop(kSaturnL, tau); B = vsop(kSaturnB, tau); R = vsop(kSaturnR, tau); break;
    }
}

}  // namespace

void moonGhaDec(double jd, double& gha, double& decDeg, double& distKm) {
    const double T = centuriesTT(jd), T2 = T * T, T3 = T2 * T, T4 = T3 * T;
    const double Lp = 218.3164477 + 481267.88123421 * T - 0.0015786 * T2 + T3 / 538841.0 - T4 / 65194000.0;
    const double D = 297.8501921 + 445267.1114034 * T - 0.0018819 * T2 + T3 / 545868.0 - T4 / 113065000.0;
    const double M = 357.5291092 + 35999.0502909 * T - 0.0001536 * T2 + T3 / 24490000.0;
    const double Mp = 134.9633964 + 477198.8675055 * T + 0.0087414 * T2 + T3 / 69699.0 - T4 / 14712000.0;
    const double F = 93.2720950 + 483202.0175233 * T - 0.0036539 * T2 - T3 / 3526000.0 + T4 / 863310000.0;
    const double A1 = 119.75 + 131.849 * T, A2 = 53.09 + 479264.290 * T, A3 = 313.45 + 481266.484 * T;
    const double E = 1 - 0.002516 * T - 0.0000074 * T2;
    double sl = 0, sr = 0, sb = 0;
    for (const auto& t : kMoonLr) {
        const double arg = deg2rad(t.d * D + t.m * M + t.mp * Mp + t.f * F);
        const double e = t.m == 0 ? 1 : (t.m == 1 || t.m == -1 ? E : E * E);
        sl += e * t.l * std::sin(arg);
        sr += e * t.r * std::cos(arg);
    }
    for (const auto& t : kMoonB) {
        const double arg = deg2rad(t.d * D + t.m * M + t.mp * Mp + t.f * F);
        const double e = t.m == 0 ? 1 : (t.m == 1 || t.m == -1 ? E : E * E);
        sb += e * t.b * std::sin(arg);
    }
    sl += 3958 * std::sin(deg2rad(A1)) + 1962 * std::sin(deg2rad(Lp - F)) + 318 * std::sin(deg2rad(A2));
    sb += -2235 * std::sin(deg2rad(Lp)) + 382 * std::sin(deg2rad(A3)) + 175 * std::sin(deg2rad(A1 - F)) +
          175 * std::sin(deg2rad(A1 + F)) + 127 * std::sin(deg2rad(Lp - Mp)) - 115 * std::sin(deg2rad(Lp + Mp));
    double dpsi, deps, eps, ra, dec;
    nutation(T, dpsi, deps, eps);
    eclToEq(deg2rad(Lp + sl / 1e6) + dpsi, deg2rad(sb / 1e6), eps, ra, dec);
    distKm = 385000.56 + sr / 1000.0;
    gha = rad2deg(norm2pi(gastRad(jd) - ra));
    decDeg = rad2deg(dec);
}

double moonHpArcmin(double distKm) { return rad2deg(std::asin(6378.14 / distKm)) * 60.0; }
double moonSdArcmin(double distKm) { return 0.2725 * moonHpArcmin(distKm); }

const char* planetNameEs(Planet p) {
    static const char* n[] = {"Venus", "Marte", "Jupiter", "Saturno"};
    return n[int(p) & 3];
}
const char* planetNameEn(Planet p) {
    static const char* n[] = {"Venus", "Mars", "Jupiter", "Saturn"};
    return n[int(p) & 3];
}

void planetGhaDec(Planet p, double jd, double& gha, double& decDeg, double& distAu) {
    const double T = centuriesTT(jd), tau = T / 10.0;
    const double L0 = vsop(kEarthL, tau), B0 = vsop(kEarthB, tau), R0 = vsop(kEarthR, tau);
    const double ex = R0 * std::cos(B0) * std::cos(L0), ey = R0 * std::cos(B0) * std::sin(L0), ez = R0 * std::sin(B0);
    double x = 0, y = 0, z = 0, lt = 0;
    for (int i = 0; i < 3; i++) {                                   // tiempo de luz: el planeta, donde estaba
        double L, B, R;
        planetLbr(p, tau - lt / 365250.0, L, B, R);
        x = R * std::cos(B) * std::cos(L) - ex;
        y = R * std::cos(B) * std::sin(L) - ey;
        z = R * std::sin(B) - ez;
        distAu = std::sqrt(x * x + y * y + z * z);
        lt = 0.0057755183 * distAu;
    }
    double lam = std::atan2(y, x), bet = std::atan2(z, std::hypot(x, y));
    // Aberracion anual (Meeus 23.2) y paso al sistema FK5
    const double th = L0 + kPi, e = 0.016708634 - 0.000042037 * T, per = deg2rad(102.93735 + 1.71946 * T), k = 20.49552 * kAs;
    const double dl = (-k * std::cos(th - lam) + e * k * std::cos(per - lam)) / std::cos(bet);
    const double db = -k * std::sin(bet) * (std::sin(th - lam) - e * std::sin(per - lam));
    lam += dl - 0.09033 * kAs;
    bet += db;
    double dpsi, deps, eps, ra, dec;
    nutation(T, dpsi, deps, eps);
    eclToEq(lam + dpsi, bet, eps, ra, dec);
    gha = rad2deg(norm2pi(gastRad(jd) - ra));
    decDeg = rad2deg(dec);
}

double planetHpArcmin(double distAu) { return 8.794 / 60.0 / distAu; }

int navStarCount() { return kStarCount; }
const NavStar& navStar(int i) { return kStars[i < 0 ? 0 : (i >= kStarCount ? kStarCount - 1 : i)]; }

int navStarFind(const char* name) {
    for (int i = 0; i < kStarCount; i++) {
        const char *a = kStars[i].name, *b = name;
        while (*a && *b && std::tolower((unsigned char)*a) == std::tolower((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return -1;
}

double ariesGha(double jd) { return rad2deg(gastRad(jd)); }

void sunGhaDec(double jd, double& gha, double& decDeg) {
    double ra, dec, R;
    sunRaDec(jd, ra, dec, R);
    gha = rad2deg(norm2pi(gastRad(jd) - ra));
    decDeg = rad2deg(dec);
}

double sunSemiDiameterArcmin(double jd) {
    double lon, R;
    sunGeometric(centuriesTT(jd), lon, R);
    return 959.63 / 60.0 / R;
}

double equationOfTimeMin(double jd) {
    double gha, dec;
    sunGhaDec(jd, gha, dec);
    const double utcHours = std::fmod(jd + 0.5, 1.0) * 24.0;
    double e = gha - (utcHours * 15.0 - 180.0);                     // angulo horario verdadero - medio
    e = std::fmod(e + 540.0, 360.0) - 180.0;
    return e * 4.0;
}

void starShaDec(const NavStar& s, double jd, double& sha, double& decDeg) {
    const double T = centuriesTT(jd), years = T * 100.0;
    // movimiento propio
    double d0 = deg2rad(s.decDeg + s.pmDecMas / 3.6e6 * years);
    double a0 = deg2rad(s.raHours * 15.0) + (s.pmRaMas / 3.6e6 * years) * kPi / 180.0 / std::cos(deg2rad(s.decDeg));
    // precesion de J2000 a la fecha (Meeus 21.2 y 21.4)
    const double zeta = (2306.2181 * T + 0.30188 * T * T + 0.017998 * T * T * T) * kAs;
    const double z = (2306.2181 * T + 1.09468 * T * T + 0.018203 * T * T * T) * kAs;
    const double th = (2004.3109 * T - 0.42665 * T * T - 0.041833 * T * T * T) * kAs;
    const double A = std::cos(d0) * std::sin(a0 + zeta);
    const double B = std::cos(th) * std::cos(d0) * std::cos(a0 + zeta) - std::sin(th) * std::sin(d0);
    const double C = std::sin(th) * std::cos(d0) * std::cos(a0 + zeta) + std::cos(th) * std::sin(d0);
    double a = std::atan2(A, B) + z, d = std::asin(C);
    // nutacion (23.1) y aberracion anual (23.3)
    double dpsi, deps, eps, sun, R;
    nutation(T, dpsi, deps, eps);
    sunGeometric(T, sun, R);
    const double e = 0.016708634 - 0.000042037 * T, peri = deg2rad(102.93735 + 1.71946 * T), k = 20.49552 * kAs;
    const double ca = std::cos(a), sa = std::sin(a), cd = std::cos(d), sd = std::sin(d), ce = std::cos(eps), te = std::tan(eps);
    const double da = (ce + std::sin(eps) * sa * sd / cd) * dpsi - ca * sd / cd * deps
                      - k * (ca * std::cos(sun) * ce + sa * std::sin(sun)) / cd + e * k * (ca * std::cos(peri) * ce + sa * std::sin(peri)) / cd;
    const double dd = std::sin(eps) * ca * dpsi + sa * deps
                      - k * (std::cos(sun) * ce * (te * cd - sa * sd) + ca * sd * std::sin(sun))
                      + e * k * (std::cos(peri) * ce * (te * cd - sa * sd) + ca * sd * std::sin(peri));
    sha = rad2deg(norm2pi(-(a + da)));
    decDeg = rad2deg(d + dd);
}

void bodyAltAz(double gha, double decDeg, double lat, double lon, double& alt, double& az) {
    const double ha = deg2rad(gha + lon), dec = deg2rad(decDeg), la = deg2rad(lat);
    alt = rad2deg(std::asin(std::sin(la) * std::sin(dec) + std::cos(la) * std::cos(dec) * std::cos(ha)));
    az = norm360(rad2deg(std::atan2(-std::sin(ha) * std::cos(dec), std::sin(dec) * std::cos(la) - std::cos(dec) * std::sin(la) * std::cos(ha))));
}

double dipArcmin(double h) { return h > 0 ? 1.76 * std::sqrt(h) : 0.0; }

double refractionArcmin(double h) {
    if (h < -1.0) h = -1.0;
    return 1.0 / std::tan(deg2rad(h + 7.31 / (h + 4.4)));
}

double observedAltitude(double hs, double ie, double eye, Limb limb, double sd, double hp) {
    const double ha = hs - ie / 60.0 - dipArcmin(eye) / 60.0;            // altura aparente
    double ho = ha - refractionArcmin(ha) / 60.0 + hp * std::cos(deg2rad(ha)) / 60.0;
    if (limb == Limb::Lower) ho += sd / 60.0;
    else if (limb == Limb::Upper) ho -= sd / 60.0;
    return ho;
}

Sight reduceSight(double lat, double lon, double gha, double dec, double ho) {
    Sight s;
    bodyAltAz(gha, dec, lat, lon, s.hcDeg, s.znDeg);
    s.interceptNm = (ho - s.hcDeg) * 60.0;
    const LatLon p = gcDestination({lat, lon}, s.interceptNm >= 0 ? s.znDeg : norm360(s.znDeg + 180.0), std::fabs(s.interceptNm));
    s.lat = p.lat;
    s.lon = p.lon;
    return s;
}

Sight advanceSight(const Sight& s, double fromLat, double fromLon, double toLat, double toLon) {
    Sight r = s;
    const double mid = deg2rad((fromLat + toLat) / 2.0);
    r.lat = s.lat + (toLat - fromLat);
    r.lon = s.lon + (toLon - fromLon) * std::cos(mid) / std::cos(deg2rad((s.lat + r.lat) / 2.0));
    return r;
}

bool fixFromSights(double lat, double lon, const Sight* sg, int n, double& outLat, double& outLon) {
    if (n < 2) return false;
    // determinante_i = dN cos Zn_i + dE sin Zn_i (millas al norte y al este de la estima)
    double a = 0, b = 0, c = 0, u = 0, v = 0;
    for (int i = 0; i < n; i++) {
        const double cz = std::cos(deg2rad(sg[i].znDeg)), sz = std::sin(deg2rad(sg[i].znDeg));
        a += cz * cz; b += cz * sz; c += sz * sz; u += cz * sg[i].interceptNm; v += sz * sg[i].interceptNm;
    }
    const double det = a * c - b * b;
    if (det < 0.03) return false;                                       // rectas casi paralelas (menos de ~10 grados)
    const double dN = (u * c - v * b) / det, dE = (v * a - u * b) / det;
    outLat = lat + dN / 60.0;
    outLon = lon + dE / 60.0 / std::cos(deg2rad(lat));
    return true;
}

}  // namespace plotter
