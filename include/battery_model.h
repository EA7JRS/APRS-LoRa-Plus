/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (based on CA2RXU LoRa APRS Tracker).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Modelo de la bateria, sin Arduino (se prueba en el PC con tools/test_battery.cpp):
 *  - porcentaje de una celda Li-ion por su curva de tension en reposo (no lineal entre 3,0 y 4,2 V);
 *  - tendencia: una muestra por minuto durante una hora; pendiente (V/h y %/h) por minimos cuadrados;
 *  - estado sin PMU (cargando, llena / USB, descargando) por la tendencia, y horas que quedan.
 */
#ifndef BATTERY_MODEL_H_
#define BATTERY_MODEL_H_

#include <stdint.h>
#include <math.h>

namespace BatteryModel {

    // Curva tipica de una celda Li-ion / LiPo con poca carga (tension -> %), de 0 % a 100 % cada 5 %
    inline float liionPercent(float vCell) {
        static const float V[21] = { 3.27f, 3.61f, 3.69f, 3.71f, 3.73f, 3.75f, 3.77f, 3.79f, 3.80f, 3.82f, 3.84f,
                                     3.85f, 3.87f, 3.91f, 3.95f, 3.98f, 4.02f, 4.08f, 4.11f, 4.15f, 4.20f };
        if (!(vCell > V[0])) return 0.0f;
        if (vCell >= V[20]) return 100.0f;
        for (int i = 1; i < 21; i++) {
            if (vCell < V[i]) return 5.0f * (i - 1) + 5.0f * (vCell - V[i - 1]) / (V[i] - V[i - 1]);
        }
        return 100.0f;
    }

    enum State : uint8_t { ST_NONE, ST_UNKNOWN, ST_DISCHARGING, ST_CHARGING, ST_FULL };

    inline const char* stateName(State s) {
        switch (s) {
            case ST_NONE:        return "no battery";
            case ST_DISCHARGING: return "discharging";
            case ST_CHARGING:    return "charging";
            case ST_FULL:        return "full / USB";
            default:             return "...";
        }
    }

    class Trend {
    public:
        static const uint8_t N = 64;                    // una muestra por minuto: algo mas de una hora

        void clear() { n = 0; head = 0; }

        // tS: segundos (millis / 1000); vCell: tension por celda
        void add(uint32_t tS, float vCell) {
            t[head] = tS;
            v[head] = vCell;
            p[head] = liionPercent(vCell);
            head = (uint8_t)((head + 1) % N);
            if (n < N) n++;
        }

        uint8_t count() const { return n; }

        // Pendiente de las muestras de los ultimos windowS segundos: tension (V/h) y porcentaje (%/h).
        // Hace falta un minimo de 5 muestras que abarquen al menos minSpanS.
        bool slope(uint32_t nowS, uint32_t windowS, uint32_t minSpanS, float& voltsPerHour, float& percentPerHour) const {
            double st = 0, sv = 0, sp = 0, stt = 0, stv = 0, stp = 0;
            uint8_t k = 0;
            uint32_t first = 0, last = 0;
            for (uint8_t i = 0; i < n; i++) {
                const uint8_t j = (uint8_t)((head + N - n + i) % N);
                if (nowS - t[j] > windowS) continue;
                const double x = (double)(int32_t)(t[j] - nowS) / 3600.0;     // horas (negativas)
                st += x; sv += v[j]; sp += p[j]; stt += x * x; stv += x * v[j]; stp += x * p[j];
                if (k == 0) first = t[j];
                last = t[j];
                k++;
            }
            if (k < 5 || last - first < minSpanS) return false;
            const double d = k * stt - st * st;
            if (fabs(d) < 1e-12) return false;
            voltsPerHour   = (float)((k * stv - st * sv) / d);
            percentPerHour = (float)((k * stp - st * sp) / d);
            return true;
        }

    private:
        uint32_t t[N] = {};
        float    v[N] = {};
        float    p[N] = {};
        uint8_t  n = 0, head = 0;
    };

    // Estado sin PMU, por la tendencia de la tension (V/h): sube = cargando; arriba y quieta = llena o en USB; baja = descargando
    inline State classify(bool connected, float vCell, bool haveSlope, float voltsPerHour) {
        if (!connected) return ST_NONE;
        if (!haveSlope) return vCell >= 4.17f ? ST_FULL : ST_UNKNOWN;
        if (voltsPerHour > 0.03f) return ST_CHARGING;
        if (vCell >= 4.14f && voltsPerHour > -0.02f) return ST_FULL;
        if (voltsPerHour < -0.005f) return ST_DISCHARGING;
        return ST_UNKNOWN;
    }

    // Horas hasta vaciarse (descargando) o hasta llenarse (cargando); < 0 si no se puede estimar
    inline float hoursLeft(State s, float percent, float percentPerHour) {
        if (s == ST_DISCHARGING && percentPerHour < -0.2f) return percent / -percentPerHour;
        if (s == ST_CHARGING && percentPerHour > 0.2f) return (100.0f - percent) / percentPerHour;
        return -1.0f;
    }

}

#endif
