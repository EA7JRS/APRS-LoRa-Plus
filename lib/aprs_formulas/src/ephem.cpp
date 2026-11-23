/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ephem.h"

#include <cmath>

#include "astro.h"
#include "celnav.h"
#include "geo.h"

namespace plotter {

namespace {

constexpr double kWgsA = 6378137.0, kWgsF = 1.0 / 298.257223563;

double obliquityRad(double jd) {
    const double T = (jd - 2451545.0) / 36525.0;
    return deg2rad(23.4392911 - 0.0130042 * T);
}

// Longitud y latitud eclipticas de la fecha (grados) a partir del AHG y la declinacion aparentes de celnav
void eclipticOf(double jd, double ghaDeg, double decDeg, double& lonDeg, double& latDeg) {
    const double ra = deg2rad(ariesGha(jd) - ghaDeg), dec = deg2rad(decDeg), eps = obliquityRad(jd);
    lonDeg = norm360(rad2deg(std::atan2(std::sin(ra) * std::cos(eps) + std::tan(dec) * std::sin(eps), std::cos(ra))));
    latDeg = rad2deg(std::asin(std::sin(dec) * std::cos(eps) - std::cos(dec) * std::sin(eps) * std::sin(ra)));
}

struct SunMoon { double sunLon, moonLon, moonLat, moonKm, sunAu, sep; };

SunMoon sunMoon(double jd) {
    SunMoon s{};
    double gs, ds, gm, dm, sb;
    sunGhaDec(jd, gs, ds);
    moonGhaDec(jd, gm, dm, s.moonKm);
    eclipticOf(jd, gs, ds, s.sunLon, sb);
    eclipticOf(jd, gm, dm, s.moonLon, s.moonLat);
    s.sunAu = sunDistanceAu(jd);
    double c = std::sin(deg2rad(ds)) * std::sin(deg2rad(dm)) + std::cos(deg2rad(ds)) * std::cos(deg2rad(dm)) * std::cos(deg2rad(gs - gm));
    s.sep = rad2deg(std::acos(c > 1 ? 1 : (c < -1 ? -1 : c)));
    return s;
}

double elongation(double jd) {
    const SunMoon s = sunMoon(jd);
    return norm360(s.moonLon - s.sunLon);
}

// Extremo (minimo si minimum) de f a partir de jd: busca el primer cambio de tendencia en pasos de step dias y lo afina
// con la seccion aurea hasta ~1 minuto
template <typename F>
double nextExtremum(F f, double jd, double step, int maxSteps, bool minimum, double& value) {
    const double sgn = minimum ? 1.0 : -1.0;
    double a = jd, fa = sgn * f(a), b = a + step, fb = sgn * f(b);
    for (int i = 0; i < maxSteps; i++) {
        const double c = b + step, fc = sgn * f(c);
        if (fb <= fa && fb <= fc && !(i == 0 && fa < fb)) {
            double lo = a, hi = c;
            const double g = 0.6180339887498949;
            double x1 = hi - g * (hi - lo), x2 = lo + g * (hi - lo), f1 = sgn * f(x1), f2 = sgn * f(x2);
            while (hi - lo > 0.0005) {
                if (f1 < f2) { hi = x2; x2 = x1; f2 = f1; x1 = hi - g * (hi - lo); f1 = sgn * f(x1); }
                else         { lo = x1; x1 = x2; f1 = f2; x2 = lo + g * (hi - lo); f2 = sgn * f(x2); }
            }
            const double x = (lo + hi) / 2;
            value = f(x);
            return x;
        }
        a = b; fa = fb; b = c; fb = fc;
    }
    value = 0;
    return 0;
}

}  // namespace

// ---------------------------------------------------------------- Luna

MoonPhase moonPhaseTrue(double jd) {
    const SunMoon s = sunMoon(jd);
    MoonPhase p{};
    p.elongDeg = norm360(s.moonLon - s.sunLon);
    // angulo de fase (Meeus, cap. 48): desde la Luna, entre el Sol y la Tierra
    const double psi = deg2rad(s.sep), R = s.sunAu * kAuKm;
    const double i = std::atan2(R * std::sin(psi), s.moonKm - R * std::cos(psi));
    p.illum = (1 + std::cos(i)) / 2;
    p.waxing = p.elongDeg < 180;
    int e = int(std::floor(p.elongDeg / 45.0 + 0.5)) % 8;
    p.phase8 = e < 0 ? e + 8 : e;
    // ultima luna nueva: se retrocede con el movimiento medio y se afina
    double t = jd - p.elongDeg / 360.0 * kSynodicMonth;
    for (int k = 0; k < 6; k++) {
        const double d = norm180(elongation(t));
        t -= d / 360.0 * kSynodicMonth;
        if (std::fabs(d) < 1e-5) break;
    }
    if (t > jd) t -= kSynodicMonth;                 // justo en la luna nueva
    p.ageDays = jd - t;
    return p;
}

double nextMoonPhase(double jd, int quarter) {
    const double target = 90.0 * (((quarter % 4) + 4) % 4);
    double ahead = norm360(target - elongation(jd));
    if (ahead < 1e-6) ahead = 360;
    double t = jd + ahead / 360.0 * kSynodicMonth;
    for (int k = 0; k < 8; k++) {
        const double d = norm180(elongation(t) - target);
        t -= d / 360.0 * kSynodicMonth;
        if (std::fabs(d) < 1e-6) break;
    }
    if (t <= jd + 1e-6) return nextMoonPhase(jd + 1.0, quarter);     // se quedo en la fase que acaba de pasar
    return t;
}

double nextMoonApsis(double jd, bool perigee, double& distKm) {
    auto dist = [](double t) { double g, d, km; moonGhaDec(t, g, d, km); return km; };
    return nextExtremum(dist, jd, 1.0, 40, perigee, distKm);
}

double moonAngularDiameterArcmin(double distKm) { return 2 * rad2deg(std::asin(kMoonRadiusKm / distKm)) * 60.0; }

Eclipse eclipseAtSyzygy(double jdS, bool full) {
    Eclipse e;
    e.jd = jdS;
    const SunMoon s = sunMoon(jdS);
    const double hp = moonHpArcmin(s.moonKm), sdm = moonSdArcmin(s.moonKm), sds = sunSemiDiameterArcmin(jdS);
    const double hps = 8.794 / 60.0 / s.sunAu;                       // paralaje horizontal del Sol
    // minima distancia entre centros: la latitud de la Luna en la sicigia, corregida por la inclinacion de su trayectoria
    const double beta = std::fabs(s.moonLat) * 60.0 * 0.9963;
    if (full) {
        const double umbra = 1.02 * (0.99834 * hp - sds + hps), penumbra = 1.02 * (0.99834 * hp + sds + hps);
        const double mu = (umbra + sdm - beta) / (2 * sdm), mp = (penumbra + sdm - beta) / (2 * sdm);
        if (mu >= 1)      { e.kind = EclipseKind::LunarTotal;     e.magnitude = mu; }
        else if (mu > 0)  { e.kind = EclipseKind::LunarPartial;   e.magnitude = mu; }
        else if (mp > 0)  { e.kind = EclipseKind::LunarPenumbral; e.magnitude = mp; }
    } else {
        const double gamma = beta / (hp - hps);                      // eje de la sombra, en radios terrestres
        if (gamma < 0.9972) {
            e.kind = sdm >= sds ? EclipseKind::SolarTotal : EclipseKind::SolarAnnular;
            e.magnitude = gamma;
        } else if (gamma < 0.9972 + (sds + sdm) / (hp - hps)) {
            e.kind = EclipseKind::SolarPartial;
            e.magnitude = gamma;
        }
    }
    return e;
}

Eclipse nextEclipse(double jd, bool lunar, int maxLunations) {
    double t = jd;
    for (int i = 0; i < maxLunations; i++) {
        t = nextMoonPhase(t, lunar ? 2 : 0);
        const Eclipse e = eclipseAtSyzygy(t, lunar);
        if (e.kind != EclipseKind::None) return e;
        t += 20;                                                     // a por la siguiente lunacion
    }
    return Eclipse{};
}

const char* eclipseNameEs(EclipseKind k) {
    static const char* n[] = {"Sin eclipse", "Eclipse penumbral de Luna", "Eclipse parcial de Luna", "Eclipse total de Luna",
                              "Eclipse parcial de Sol", "Eclipse anular de Sol", "Eclipse total de Sol"};
    return n[int(k) < 7 ? int(k) : 0];
}

const char* eclipseNameEn(EclipseKind k) {
    static const char* n[] = {"No eclipse", "Penumbral lunar eclipse", "Partial lunar eclipse", "Total lunar eclipse",
                              "Partial solar eclipse", "Annular solar eclipse", "Total solar eclipse"};
    return n[int(k) < 7 ? int(k) : 0];
}

double tideForceIndex(double jd) {
    const SunMoon s = sunMoon(jd);
    // fuerza generadora ~ masa / distancia^3: la del Sol medio es 0,4595 de la de la Luna media
    const double m = std::pow(kMoonMeanKm / s.moonKm, 3), su = 0.4595 / std::pow(s.sunAu, 3);
    const double both = std::sqrt(m * m + su * su + 2 * m * su * std::cos(2 * deg2rad(s.sep)));
    return 100.0 * both / 1.4595;
}

EmeInfo emeInfo(double jd, double lat, double lon, double freqHz) {
    // distancia topocentrica: vectores geocentricos de la Luna y del observador (elipsoide WGS84) en ejes fijos a la Tierra
    auto range = [lat, lon](double t) {
        double gha, dec, km;
        moonGhaDec(t, gha, dec, km);
        const double la = deg2rad(lat), e2 = kWgsF * (2 - kWgsF);
        const double N = kWgsA / 1000.0 / std::sqrt(1 - e2 * std::sin(la) * std::sin(la));
        const double ox = N * std::cos(la) * std::cos(deg2rad(lon)), oy = N * std::cos(la) * std::sin(deg2rad(lon)), oz = N * (1 - e2) * std::sin(la);
        const double ml = deg2rad(-gha), md = deg2rad(dec);          // longitud geografica del punto sublunar = -AHG
        const double mx = km * std::cos(md) * std::cos(ml), my = km * std::cos(md) * std::sin(ml), mz = km * std::sin(md);
        return std::sqrt((mx - ox) * (mx - ox) + (my - oy) * (my - oy) + (mz - oz) * (mz - oz));
    };
    EmeInfo r{};
    r.rangeKm = range(jd);
    const double dt = 30.0 / 86400.0;
    const double rateKmS = (range(jd + dt) - range(jd - dt)) / 60.0;
    const double lambdaM = kLightKmS * 1000.0 / freqHz, d = r.rangeKm * 1000.0;
    const double sigma = 0.065 * kPi * (kMoonRadiusKm * 1000.0) * (kMoonRadiusKm * 1000.0);
    r.pathLossDb = 10 * std::log10(std::pow(4 * kPi, 3) * d * d * d * d / (sigma * lambdaM * lambdaM));
    r.echoDelayS = 2 * r.rangeKm / kLightKmS;
    r.dopplerHz = -2 * rateKmS / kLightKmS * freqHz;
    return r;
}

// ---------------------------------------------------------------- Sol

double sunDistanceAu(double jd) {
    // el semidiametro de celnav es 959,63" / R
    return (959.63 / 60.0) / sunSemiDiameterArcmin(jd);
}

double sunAngularDiameterArcmin(double jd) { return 2 * sunSemiDiameterArcmin(jd); }

double solarIrradianceToa(double jd) {
    const double r = sunDistanceAu(jd);
    return kSolarConstant / (r * r);
}

double clearSkyUvIndex(double sunAltDeg, double distAu, double ozoneDu) {
    if (sunAltDeg <= 0 || distAu <= 0 || ozoneDu <= 0) return 0;
    // Parametrizacion de Allaart, van Weele, Forster y Kelder (2004) en su forma de uso corriente:
    // 12,5 mu^2,42 (ozono / 300)^-1,23, corregida por la distancia al Sol
    const double mu0 = std::sin(deg2rad(sunAltDeg));
    return 12.5 * std::pow(mu0, 2.42) * std::pow(ozoneDu / 300.0, -1.23) / (distAu * distAu);
}

double carringtonRotation(double jd) { return (jd - 2398140.2270) / 27.2752316; }

double nextSeason(double jd, int& season) {
    auto sunLon = [](double t) { double g, d, lo, la; sunGhaDec(t, g, d); eclipticOf(t, g, d, lo, la); return lo; };
    const double lon = sunLon(jd);
    int k = (int(std::floor(lon / 90.0)) + 1) % 4;                   // 0 = 0 grados (marzo), 1 = 90 (junio)...
    double t = jd + norm360(k * 90.0 - lon) / 360.0 * 365.2422;
    for (int i = 0; i < 8; i++) {
        const double d = norm180(sunLon(t) - k * 90.0);
        t -= d / 360.0 * 365.2422;
        if (std::fabs(d) < 1e-7) break;
    }
    season = k;
    return t;
}

double nextEarthApsis(double jd, bool perihelion, double& distAu) {
    return nextExtremum([](double t) { return sunDistanceAu(t); }, jd, 8.0, 60, perihelion, distAu);
}

// ---------------------------------------------------------------- Tierra

double horizonKm(double h) { return h > 0 ? std::sqrt(2 * 6371.0088 * h / 1000.0 + (h / 1000.0) * (h / 1000.0)) : 0; }

double radioHorizonKm(double h) { return h > 0 ? std::sqrt(2 * (4.0 / 3.0) * 6371.0088 * h / 1000.0) : 0; }

double radioRangeKm(double h1, double h2) { return radioHorizonKm(h1) + radioHorizonKm(h2); }

double opticalRangeNm(double eye, double light) {
    return 2.03 * (std::sqrt(eye > 0 ? eye : 0) + std::sqrt(light > 0 ? light : 0));
}

double normalGravity(double latDeg, double h) {
    const double s = std::sin(deg2rad(latDeg)), s2 = s * s;
    const double g0 = 9.7803253359 * (1 + 0.00193185265241 * s2) / std::sqrt(1 - 0.00669437999013 * s2);
    return g0 - 3.086e-6 * h;
}

double earthRadiusAtM(double latDeg) {
    const double b = kWgsA * (1 - kWgsF), c = std::cos(deg2rad(latDeg)), s = std::sin(deg2rad(latDeg));
    const double n = (kWgsA * kWgsA * c) * (kWgsA * kWgsA * c) + (b * b * s) * (b * b * s);
    const double d = (kWgsA * c) * (kWgsA * c) + (b * s) * (b * s);
    return std::sqrt(n / d);
}

void jdToUtc(double jd, int& y, int& m, int& d, double& utcHours) {
    unixToUtc((long long)std::floor((jd - 2440587.5) * 86400.0 + 0.5), y, m, d, utcHours);
}

}  // namespace plotter
