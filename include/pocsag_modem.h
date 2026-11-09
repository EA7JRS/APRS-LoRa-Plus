/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Demodulador POCSAG de banda base para el modo DAPNET de la T-TWR: el SA868 en FM con audio plano da a la salida la senal NRZ
 * (512 / 1200 / 2400 baudios, +-4,5 kHz). La entrada de audio de la placa tiene un acoplo de alterna (100 nF con 10 k / 10 k:
 * unos 320 Hz) que deforma los bits largos: se compensa con su inversa (integrador con fuga). Despues, paso bajo, umbral
 * adaptativo (punto medio de las envolventes) y PLL de reloj de bit sobre los cruces. Los bits salen agrupados en bytes, el
 * primero en el bit de mayor peso, como los da el SX126x al decodificador POCSAG (include/pocsag.h), que busca solo la
 * sincronia y la polaridad. Sin hardware: se prueba en el PC con tools/afsk/test_pocsag_audio.cpp.
 */

#ifndef POCSAG_MODEM_H_
#define POCSAG_MODEM_H_

#include <math.h>
#include <stdint.h>

namespace PocsagModem {

    class BaseRx {
    public:
        typedef void (*ByteFn)(uint8_t byte, void* ctx);

        void begin(float fs, int baud, float hpfHz) {
            this->fs = fs; this->baud = baud > 0 ? baud : 1200;
            step = (float)this->baud / fs;
            hpfW = hpfHz > 0 ? 2.0f * (float)M_PI * hpfHz / fs : 0.0f;
            leak = 1.0f - 1.0f / (0.05f * fs);              // 50 ms: deja pasar el preambulo y los bits largos, no deriva
            const float fc = 0.75f * (float)this->baud;     // paso bajo de dos polos
            lpK = 1.0f - expf(-2.0f * (float)M_PI * fc / fs);
            envK = 1.0f / (16.0f * fs / (float)this->baud);   // envolventes: ~16 bits
            reset();
        }
        void reset() { integ = 0; dcIn = 0; l1 = l2 = 0; hi = 0.01f; lo = -0.01f; phase = 0; lastAbove = false; acc = 0; nbits = 0; edges = 0; edgeAge = 0; }

        void feed(float x, ByteFn fn, void* ctx) {
            dcIn += (x - dcIn) * 0.0005f;                   // continua del ADC (polarizacion)
            float y = x - dcIn;
            if (hpfW > 0) { integ = integ * leak + y; y += hpfW * integ; }     // inversa del acoplo de alterna
            l1 += (y - l1) * lpK; l2 += (l1 - l2) * lpK;
            const float v = l2;
            if (v > hi) hi = v; else hi += (lo - hi) * envK * 0.5f;     // envolventes con fuga hacia el centro
            if (v < lo) lo = v; else lo += (hi - lo) * envK * 0.5f;
            const float thr = 0.5f * (hi + lo);
            const bool above = v > thr;
            if (above != lastAbove) {                       // cruce: el centro del bit queda medio bit despues
                lastAbove = above;
                const float err = phase - 0.5f;
                phase -= err * (edgeAge < 64 ? 0.35f : 0.12f);
                if (edges < 1000) edges++;
                edgeAge = 0;
            }
            phase += step;
            if (phase >= 1.0f) {
                phase -= 1.0f;
                if (edgeAge < 1000) edgeAge++;
                acc = (uint8_t)((acc << 1) | (above ? 1 : 0));
                if (++nbits == 8) { fn(acc, ctx); nbits = 0; acc = 0; }
            }
        }
        // hay transiciones recientes con amplitud: probable senal de datos (para el canal ocupado)
        bool carrier() const { return edgeAge < 32 && (hi - lo) > 0.02f; }

    private:
        float fs = 48000, step = 0.025f, hpfW = 0, leak = 0.999f, lpK = 0.1f, envK = 0.001f;
        float integ = 0, dcIn = 0, l1 = 0, l2 = 0, hi = 0.01f, lo = -0.01f, phase = 0;
        bool  lastAbove = false;
        uint8_t acc = 0, nbits = 0;
        int   baud = 1200, edges = 0, edgeAge = 0;
    };
}

#endif
