/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tendencia de la presion con observaciones de hora conocida (tiempo de internet: cada 10-120 min, con la hora de la observacion de
 * OpenWeatherMap / IQAir). La de PAGES_Core::PressureTrend supone una muestra cada 10 min del sensor; esta guarda la hora de cada una.
 * C++ puro, sin Arduino: se prueba en el PC (tools/test_press_trend.cpp).
 */

#ifndef PRESS_TREND_H_
#define PRESS_TREND_H_

#include <stdint.h>

class TimedPressureTrend {
public:
    static const int CAP = 16;                                  // 4 h a 15 min
    // tS: hora de la observacion (s, cualquier origen que crezca); se ignoran las repetidas (menos de 5 min tras la ultima)
    void add(float hpa, uint32_t tS) {
        if (!(hpa > 800.0f && hpa < 1100.0f)) return;
        if (n_ > 0) {
            const uint32_t last = t_[(head_ + n_ - 1) % CAP];
            if (tS < last + 300u) {                             // repetida o fuera de orden: se queda la ultima
                if (tS >= last) p_[(head_ + n_ - 1) % CAP] = hpa;
                return;
            }
        }
        if (n_ == CAP) { head_ = (head_ + 1) % CAP; n_--; }
        const int i = (head_ + n_) % CAP;
        p_[i] = hpa; t_[i] = tS; n_++;
    }
    void clear() { n_ = 0; head_ = 0; }
    int count() const { return n_; }
    // Cambio en 3 h: con la observacion mas cercana a 3 h antes de la ultima (entre 1 h y 3,5 h), llevado a 3 h
    bool delta3h(float& out) const {
        if (n_ < 2) return false;
        const int li = (head_ + n_ - 1) % CAP;
        const uint32_t tl = t_[li];
        int best = -1; uint32_t bestSpan = 0, bestErr = 0xFFFFFFFFu;
        for (int k = 0; k < n_ - 1; k++) {
            const int i = (head_ + k) % CAP;
            const uint32_t span = tl - t_[i];
            if (span < 3600u || span > 12600u) continue;
            const uint32_t err = span > 10800u ? span - 10800u : 10800u - span;
            if (err < bestErr) { bestErr = err; best = i; bestSpan = span; }
        }
        if (best < 0) return false;
        out = (p_[li] - p_[best]) * (10800.0f / (float)bestSpan);
        return true;
    }
    uint32_t lastS() const { return n_ ? t_[(head_ + n_ - 1) % CAP] : 0; }
private:
    float    p_[CAP];
    uint32_t t_[CAP];
    int      n_ = 0, head_ = 0;
};

#endif
