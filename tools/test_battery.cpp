// Copyright (C) 2026 Manuel - EA7JRS
// SPDX-License-Identifier: GPL-3.0-or-later
// Pruebas de include/battery_model.h en el PC: g++ -std=c++17 -I include tools/test_battery.cpp -o /tmp/tb && /tmp/tb
#include <cstdio>
#include <cmath>
#include "battery_model.h"
using namespace BatteryModel;
static int fails = 0, checks = 0;
static void ok(bool c, const char* what) { checks++; if (!c) { fails++; printf("FAIL %s\n", what); } }
int main() {
    ok(liionPercent(4.25f) == 100.0f, "100% arriba");
    ok(liionPercent(3.0f) == 0.0f, "0% abajo");
    ok(fabsf(liionPercent(3.84f) - 50.0f) < 0.01f, "3,84 V = 50%");
    ok(liionPercent(3.70f) > 10.0f && liionPercent(3.70f) < 15.0f, "3,70 V entre 10 y 15%");
    float last = -1; bool mono = true;
    for (float v = 3.0f; v <= 4.3f; v += 0.005f) { float p = liionPercent(v); if (p < last) mono = false; last = p; }
    ok(mono, "curva creciente");
    Trend t; float vph, pph;
    ok(!t.slope(0, 1800, 600, vph, pph), "sin muestras");
    for (int m = 0; m <= 30; m++) t.add(1000 + m * 60, 3.95f - 0.2f * m / 60.0f);     // -0,2 V/h
    ok(t.slope(1000 + 30 * 60, 1800, 600, vph, pph), "pendiente con 31 muestras");
    ok(fabsf(vph + 0.2f) < 0.005f, "-0,2 V/h");
    ok(pph < -5.0f, "%/h negativo");
    State s = classify(true, 3.85f, true, vph);
    ok(s == ST_DISCHARGING, "descargando");
    float h = hoursLeft(s, 50.0f, pph);
    ok(h > 0 && h < 50, "horas que quedan");
    ok(classify(true, 3.9f, true, 0.1f) == ST_CHARGING, "cargando");
    ok(classify(true, 4.18f, true, 0.0f) == ST_FULL, "llena / USB");
    ok(classify(true, 4.18f, false, 0.0f) == ST_FULL, "llena sin tendencia");
    ok(classify(false, 0, false, 0) == ST_NONE, "sin bateria");
    ok(hoursLeft(ST_CHARGING, 80.0f, 20.0f) == 1.0f, "1 h para llenarse");
    Trend w;
    for (int m = 0; m < 200; m++) w.add(m * 60, 4.0f);                                  // da la vuelta al anillo
    ok(w.count() == Trend::N, "anillo lleno");
    ok(w.slope(199 * 60, 1800, 600, vph, pph) && fabsf(vph) < 1e-4f, "plana tras dar la vuelta");
    printf("%d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
