/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "wind.h"

#include <cmath>

namespace plotter {

int beaufort(double kn) {
    static const int top[] = {0, 3, 6, 10, 16, 21, 27, 33, 40, 47, 55, 63};       // limite superior de cada fuerza, en nudos
    long k = std::lround(kn < 0 ? 0 : kn);
    for (int f = 0; f < 12; f++) if (k <= top[f]) return f;
    return 12;
}

const char* beaufortNameEs(int f) {
    static const char* n[] = {"Calma", "Ventolina", "Flojito", "Flojo", "Bonancible", "Fresquito", "Fresco", "Frescachón",
                              "Temporal", "Temporal fuerte", "Temporal duro", "Temporal muy duro", "Temporal huracanado"};
    return n[f < 0 ? 0 : (f > 12 ? 12 : f)];
}

PulseAnemometer::PulseAnemometer(double f, uint32_t deb, uint32_t win, uint32_t calm)
    : factor_(f), debounceUs_(deb), windowUs_(win), calmUs_(calm) {}

bool PulseAnemometer::pulse(uint64_t now) {
    if (any_ && now - last_ < debounceUs_) return false;
    last_ = now;
    any_ = true;
    total_++;
    t_[head_] = now;
    head_ = (head_ + 1) % kMax;
    if (n_ < kMax) n_++;
    return true;
}

double PulseAnemometer::knots(uint64_t now) {
    if (!any_ || now - last_ > calmUs_) return 0;
    // Pulsos dentro de la ventana: frecuencia = (pulsos - 1) / (ultimo - primero)
    int cnt = 0;
    uint64_t first = 0, lastT = 0;
    for (int i = 0; i < n_; i++) {
        uint64_t t = t_[(head_ - 1 - i + 2 * kMax) % kMax];
        if (now - t > windowUs_) break;
        if (cnt == 0) lastT = t;
        first = t;
        cnt++;
    }
    double hz = 0;
    if (cnt >= 2 && lastT > first) hz = (cnt - 1) * 1e6 / double(lastT - first);
    else if (n_ >= 2) {                                             // viento flojo: intervalo entre los dos ultimos pulsos
        uint64_t a = t_[(head_ - 1 + kMax) % kMax], b = t_[(head_ - 2 + kMax) % kMax];
        if (a > b) hz = 1e6 / double(a - b);
    }
    double kn = hz * factor_;
    if (kn > gust_) gust_ = kn;
    return kn;
}

}  // namespace plotter
