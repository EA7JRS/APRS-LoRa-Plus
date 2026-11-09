/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pruebas en el PC de src/baro_utils.cpp: QNH aprendido del GPS, manual y de internet (la app).
 *   g++ -std=c++17 -Wall -Wextra -Werror -Iinclude tools/test_baro.cpp src/baro_utils.cpp -o /tmp/test_baro && /tmp/test_baro
 */
#include <cmath>
#include <cstdio>
#include "baro_utils.h"

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FALLO %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, t) CHECK(std::fabs((a) - (b)) <= (t))

int main() {
    using namespace BARO_Utils;
    // Estacion a 500 m con 1020 hPa al nivel del mar: presion de la estacion
    const float alt = 500, t = 15, slp = 1020;
    const float st = slp / powf(1.0f - (0.0065f * alt) / (t + 0.0065f * alt + 273.15f), -5.257f);
    NEAR(seaLevelPressure(st, alt, t), slp, 0.05);

    // Sin GPS y sin internet: no hay QNH ni altitud
    reset();
    uint32_t now = 1000;
    feed(st, t, false, 0, now);
    CHECK(getQnh() == 0.0f && !altitudeValid() && !isNetQnh());

    // Llega el QNH de internet: altitud barometrica sin GPS
    setNetQnh(1020.0f, now);
    feed(st, t, false, 0, now += 1000);
    CHECK(isNetQnh() && altitudeValid());
    NEAR(getQnh(), 1020.0f, 0.01);
    NEAR(getAltitude(), alt, 1.0);
    NEAR(reducedPressure(st, t), st, 0.01);                 // nunca como presion reducida de la estacion (balizas)

    // Llega el GPS: manda el GPS (QNH local 1021 por el desfase del sensor), suave
    const float st2 = 1021.0f / powf(1.0f - (0.0065f * alt) / (t + 0.0065f * alt + 273.15f), -5.257f);
    for (int i = 0; i < 40; i++) feed(st2, t, true, alt, now += 30000);
    CHECK(!isNetQnh());
    NEAR(getQnh(), 1021.0f, 0.1);
    // Internet no pisa al GPS reciente
    setNetQnh(1005.0f, now);
    feed(st2, t, false, 0, now += 1000);
    CHECK(!isNetQnh());
    NEAR(getQnh(), 1021.0f, 0.1);
    // Sin GPS mas de 20 min: vuelve el de internet (el ultimo recibido)
    setNetQnh(1019.0f, now += 21 * 60000);
    feed(st2, t, false, 0, now += 1000);
    CHECK(isNetQnh());
    NEAR(getQnh(), 1019.0f, 0.01);
    // El manual manda sobre todo
    setManualQnh(1012.0f);
    feed(st2, t, false, 0, now += 1000);
    CHECK(isManualQnh() && !isNetQnh());
    NEAR(getQnh(), 1012.0f, 0.01);
    clearManualQnh();
    feed(st2, t, false, 0, now += 1000);
    CHECK(isNetQnh());
    // El de internet caduca a las 2 h
    feed(st2, t, false, 0, now += 2 * 3600000UL);
    CHECK(!isNetQnh() && getQnh() == 0.0f && !altitudeValid());
    // Valores imposibles se ignoran
    setNetQnh(500.0f, now);
    feed(st2, t, false, 0, now += 1000);
    CHECK(!isNetQnh());

    printf("%d comprobaciones del barometro, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
