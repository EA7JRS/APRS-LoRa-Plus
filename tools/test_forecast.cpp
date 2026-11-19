/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pronostico de TELEMETRY con el tiempo de internet (viento, nubes y lluvia) y propagacion en HF (formulas de sat-aprs):
 *   g++ -std=c++17 -Wall -Wextra -Iinclude -Ilib/aprs_formulas/src tools/test_forecast.cpp src/pages_core.cpp lib/aprs_formulas/src/[a-z]*.cpp -o /tmp/tfc && /tmp/tfc
 */
#include <cmath>
#include <cstdio>
#include <cstring>
#include "pages_core.h"

using namespace PAGES_Core;
static int checks = 0, fails = 0;
#define EQ(a, b) do { checks++; if (strcmp((a), (b)) != 0) { fails++; printf("FALLO %s:%d \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

static const char* fc(float p, Trend t, float delta, const ForecastExtra& ex) {
    static char out[24];
    forecastText(p, t != TREND_UNKNOWN, t, 15.0f, 70.0f, 9.6f, ex, out, sizeof(out), t != TREND_UNKNOWN ? delta : NAN);
    return out;
}

int main() {
    ForecastExtra none;
    EQ(fc(1012, TREND_FALLING, -1.6f, none), "Clouding over");               // sin internet: lo de siempre
    ForecastExtra sw; sw.windDeg = 230; sw.windMs = 6;
    EQ(fc(1012, TREND_FALLING, -1.6f, sw), "Rain likely");                    // cae con viento del SW
    EQ(fc(1015, TREND_FALLING_SLOW, -0.8f, sw), "Showers possible");
    ForecastExtra ne; ne.windDeg = 45; ne.windMs = 6;
    EQ(fc(1012, TREND_FALLING, -1.6f, ne), "Clouding over");                  // del NE no cambia
    ForecastExtra south = sw; south.north = false;
    EQ(fc(1012, TREND_FALLING, -1.6f, south), "Clouding over");               // en el sur el lado humedo es el NW
    south.windDeg = 300;
    EQ(fc(1012, TREND_FALLING, -1.6f, south), "Rain likely");
    ForecastExtra gale; gale.windDeg = 200; gale.windMs = 14;
    EQ(fc(1005, TREND_FALLING, -2.0f, gale), "Gale, rain soon");
    EQ(fc(1015, TREND_FALLING_SLOW, -0.8f, gale), "Windy, rain soon");
    ForecastExtra rain; rain.rainMmH = 1.2f;
    EQ(fc(1008, TREND_FALLING, -1.0f, rain), "Rain now");
    EQ(fc(1008, TREND_RISING, 1.0f, rain), "Rain easing");
    rain.rainMmH = 6; EQ(fc(1008, TREND_STEADY, 0, rain), "Heavy rain now");
    ForecastExtra cloudy; cloudy.clouds = 95;
    EQ(fc(1013, TREND_STEADY, 0.1f, cloudy), "Overcast");
    ForecastExtra clear; clear.clouds = 5;
    EQ(fc(1013, TREND_STEADY, 0.1f, clear), "Clear skies");
    EQ(fc(1013, TREND_UNKNOWN, 0, clear), "Clear skies");                    // sin tendencia todavia: el cielo de ahora
    ForecastExtra half; half.clouds = 60;
    EQ(fc(1013, TREND_UNKNOWN, 0, half), "Cloudy");
    // HF: mismos valores que las formulas de sat-aprs (server.ts)
    #define NEARF(a, b) do { checks++; if (std::fabs((a) - (b)) > 0.01) { fails++; printf("FALLO %s:%d %f != %f\n", __FILE__, __LINE__, (double)(a), (double)(b)); } } while (0)
    HfEstimate n = hfEstimate(150, -20, 0);                  // de noche
    NEARF(n.muf, 12.1 * 0.85);                               // base 7 + 85 * 0,06 = 12,1; de noche el 85 %
    NEARF(n.luf, 1.925 * 0.7);
    HfEstimate d = hfEstimate(150, 90, 3.1e-6);              // sol en el cenit, rayos X C3.1
    NEARF(d.muf, 12.1 + 21.6);
    NEARF(d.luf, 1.925 + 3.2 + 1.5);
    EQ(hfStateShort(hfBandState(28.5f, d, 2)), "open");     // 28,5 <= 0,85 * 33,7
    EQ(hfStateShort(hfBandState(3.65f, d, 2)), "abs");
    EQ(hfStateShort(hfBandState(14.15f, n, 2)), "shut");
    NEARF(hfEstimate(150, -20, 3.1e-6).luf, 1.925 * 0.7);          // de noche los rayos X no absorben (capa D sin sol)
    NEARF(hfEstimate(150, 30, 3.1e-6).luf, 1.925 + 0.5 * 3.2 + 1.5 * pow(0.5, 0.75));   // sol a 30 grados: cos(zenit)^0,75
    EQ(hfStateShort(hfBandState(7.1f, d, 5)), "poor");       // tormenta
    NEARF(xrayFluxFromClass("M2.4") * 1e6, 24.0);
    NEARF(xrayFluxFromClass("x") * 1e4, 1.0);
    printf("%d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
