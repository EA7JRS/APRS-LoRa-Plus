/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "geo.h"

#include <cmath>

namespace plotter {

double norm360(double d) { d = std::fmod(d, 360.0); return d < 0 ? d + 360.0 : d; }
double norm180(double d) { d = norm360(d); return d > 180.0 ? d - 360.0 : d; }

static const double kRnm = kEarthRadiusM / kMetersPerNm;

double gcDistanceNm(LatLon a, LatLon b) {
    double p1 = deg2rad(a.lat), p2 = deg2rad(b.lat);
    double dp = p2 - p1, dl = deg2rad(b.lon - a.lon);
    double h = std::sin(dp / 2) * std::sin(dp / 2) + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return kRnm * 2 * std::atan2(std::sqrt(h), std::sqrt(1 - h));
}

double gcInitialBearing(LatLon a, LatLon b) {
    double p1 = deg2rad(a.lat), p2 = deg2rad(b.lat), dl = deg2rad(b.lon - a.lon);
    double y = std::sin(dl) * std::cos(p2);
    double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
    return norm360(rad2deg(std::atan2(y, x)));
}

LatLon gcDestination(LatLon from, double bearingDeg, double distNm) {
    double d = distNm / kRnm, t = deg2rad(bearingDeg), p1 = deg2rad(from.lat), l1 = deg2rad(from.lon);
    double p2 = std::asin(std::sin(p1) * std::cos(d) + std::cos(p1) * std::sin(d) * std::cos(t));
    double l2 = l1 + std::atan2(std::sin(t) * std::sin(d) * std::cos(p1), std::cos(d) - std::sin(p1) * std::sin(p2));
    return {rad2deg(p2), norm180(rad2deg(l2))};
}

double rhumbDistanceNm(LatLon a, LatLon b) {
    double p1 = deg2rad(a.lat), p2 = deg2rad(b.lat);
    double dp = p2 - p1, dl = deg2rad(norm180(b.lon - a.lon));
    double dpsi = std::log(std::tan(kPi / 4 + p2 / 2) / std::tan(kPi / 4 + p1 / 2));     // diferencia de latitudes aumentadas
    double q = std::fabs(dpsi) > 1e-12 ? dp / dpsi : std::cos(p1);
    return kRnm * std::sqrt(dp * dp + q * q * dl * dl);
}

double rhumbBearing(LatLon a, LatLon b) {
    double p1 = deg2rad(a.lat), p2 = deg2rad(b.lat);
    double dl = deg2rad(norm180(b.lon - a.lon));
    double dpsi = std::log(std::tan(kPi / 4 + p2 / 2) / std::tan(kPi / 4 + p1 / 2));
    return norm360(rad2deg(std::atan2(dl, dpsi)));
}

bool vincentyInverse(LatLon p1, LatLon p2, double& distM, double& brg) {
    const double a = 6378137.0, f = 1 / 298.257223563, b = a * (1 - f);
    double L = deg2rad(p2.lon - p1.lon);
    double U1 = std::atan((1 - f) * std::tan(deg2rad(p1.lat))), U2 = std::atan((1 - f) * std::tan(deg2rad(p2.lat)));
    double sU1 = std::sin(U1), cU1 = std::cos(U1), sU2 = std::sin(U2), cU2 = std::cos(U2);
    double lam = L, lamP, sS, cS, sig, sA, c2A, c2SM;
    int it = 0;
    do {
        double sL = std::sin(lam), cL = std::cos(lam);
        sS = std::sqrt((cU2 * sL) * (cU2 * sL) + (cU1 * sU2 - sU1 * cU2 * cL) * (cU1 * sU2 - sU1 * cU2 * cL));
        if (sS == 0) { distM = 0; brg = 0; return true; }      // puntos coincidentes
        cS = sU1 * sU2 + cU1 * cU2 * cL;
        sig = std::atan2(sS, cS);
        sA = cU1 * cU2 * sL / sS;
        c2A = 1 - sA * sA;
        c2SM = c2A != 0 ? cS - 2 * sU1 * sU2 / c2A : 0;        // linea ecuatorial
        double C = f / 16 * c2A * (4 + f * (4 - 3 * c2A));
        lamP = lam;
        lam = L + (1 - C) * f * sA * (sig + C * sS * (c2SM + C * cS * (-1 + 2 * c2SM * c2SM)));
    } while (std::fabs(lam - lamP) > 1e-12 && ++it < 200);
    if (it >= 200) return false;
    double u2 = c2A * (a * a - b * b) / (b * b);
    double A = 1 + u2 / 16384 * (4096 + u2 * (-768 + u2 * (320 - 175 * u2)));
    double B = u2 / 1024 * (256 + u2 * (-128 + u2 * (74 - 47 * u2)));
    double dS = B * sS * (c2SM + B / 4 * (cS * (-1 + 2 * c2SM * c2SM) - B / 6 * c2SM * (-3 + 4 * sS * sS) * (-3 + 4 * c2SM * c2SM)));
    distM = b * A * (sig - dS);
    brg = norm360(rad2deg(std::atan2(cU2 * std::sin(lam), cU1 * sU2 - sU1 * cU2 * std::cos(lam))));
    return true;
}

void crossTrack(LatLon a, LatLon b, LatLon p, double& xteNm, double& alongNm) {
    double d13 = gcDistanceNm(a, p) / kRnm;
    double t13 = deg2rad(gcInitialBearing(a, p)), t12 = deg2rad(gcInitialBearing(a, b));
    double dxt = std::asin(std::sin(d13) * std::sin(t13 - t12));
    double c = std::cos(d13) / std::cos(dxt);
    c = c > 1 ? 1 : (c < -1 ? -1 : c);
    double dat = std::acos(c);
    if (std::cos(t13 - t12) < 0) dat = -dat;                   // por detras del origen
    xteNm = dxt * kRnm;
    alongNm = dat * kRnm;
}

}  // namespace plotter
