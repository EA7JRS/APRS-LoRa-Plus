/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "wxcalc.h"

#include <cmath>

namespace plotter {

namespace {
constexpr double kA = 17.625, kB = 243.04;
}

double dewPointC(double t, double rh) {
    if (!(rh >= 1 && rh <= 100) || !std::isfinite(t)) return NAN;
    double g = std::log(rh / 100.0) + kA * t / (kB + t);
    return kB * g / (kA - g);
}

double relHumidityPct(double t, double td) {
    double rh = 100.0 * std::exp(kA * td / (kB + td)) / std::exp(kA * t / (kB + t));
    return rh > 100 ? 100 : rh;
}

double windChillC(double t, double v) {
    if (!(t <= 10) || !(v > 4.8)) return t;
    double p = std::pow(v, 0.16);
    double w = 13.12 + 0.6215 * t - 11.37 * p + 0.3965 * t * p;
    return w < t ? w : t;
}

double heatIndexC(double t, double rh) {
    if (!(t >= 27) || !(rh >= 40 && rh <= 100)) return t;
    double f = t * 9 / 5 + 32, r = rh;
    double hi = -42.379 + 2.04901523 * f + 10.14333127 * r - 0.22475541 * f * r - 6.83783e-3 * f * f - 5.481717e-2 * r * r +
                1.22874e-3 * f * f * r + 8.5282e-4 * f * r * r - 1.99e-6 * f * f * r * r;
    if (r > 85 && f <= 87) hi += (r - 85) / 10.0 * (87 - f) / 5.0;     // ajuste de humedad alta del NWS
    double c = (hi - 32) * 5 / 9;
    return c > t ? c : t;
}

double feelsLikeC(double t, double rh, double v) {
    if (v >= 0 && t <= 10) return windChillC(t, v);
    if (rh >= 0 && t >= 27) return heatIndexC(t, rh);
    return t;
}

double seaLevelPressureHpa(double p, double h, double t) {
    double tk = t + 273.15 + 0.0065 * h / 2;          // temperatura media de la columna
    return p * std::exp(9.80665 * 0.0289644 * h / (8.31446 * tk));
}

}  // namespace plotter
