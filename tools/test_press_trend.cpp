/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pruebas de include/press_trend.h:  g++ -std=c++17 -Wall -Wextra -Werror -Iinclude tools/test_press_trend.cpp -o /tmp/tpt && /tmp/tpt
 */
#include <cmath>
#include <cstdio>
#include "press_trend.h"

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FALLO %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, t) CHECK(std::fabs((a) - (b)) <= (t))

int main() {
    TimedPressureTrend t;
    float d = 0;
    CHECK(!t.delta3h(d));
    t.add(1015.0f, 1000);
    t.add(1015.2f, 1100);                                       // repetida (menos de 5 min): sustituye a la ultima
    CHECK(t.count() == 1);
    t.add(1014.0f, 1000 + 1800);                                // 30 min: aun no hay 1 h
    CHECK(!t.delta3h(d));
    t.add(1013.2f, 1000 + 3600);                                // 1 h, -2 hPa: se extrapola a -6 hPa / 3 h
    CHECK(t.delta3h(d));
    NEAR(d, -6.0f, 0.01);
    t.clear();
    for (int k = 0; k < 20; k++) t.add(1015.0f - 0.5f * k, 1000 + 3600u * k);   // cada hora, -0,5 hPa/h (se quedan las 16 ultimas)
    CHECK(t.delta3h(d));
    NEAR(d, -1.5f, 0.01);                                       // 3 h exactas: -1,5
    CHECK(t.count() == 16);
    t.add(900.0f, t.lastS() - 100);                             // fuera de orden: se ignora el tiempo y se corrige la ultima
    CHECK(t.count() == 16);
    t.add(2000.0f, t.lastS() + 3600);                           // imposible: se ignora
    CHECK(t.count() == 16);
    TimedPressureTrend u;                                       // cada 15 min durante 4 h, +0,1 hPa por muestra (+1,2 hPa / 3 h)
    for (int k = 0; k <= 16; k++) u.add(1010.0f + 0.1f * k, 50000 + 900u * k);
    CHECK(u.delta3h(d));
    NEAR(d, 1.2f, 0.01);
    TimedPressureTrend w;                                       // un hueco de 5 h: la historia vieja no sirve
    w.add(1010.0f, 0); w.add(1000.0f, 18000);
    CHECK(!w.delta3h(d));
    printf("%d comprobaciones de la tendencia, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
