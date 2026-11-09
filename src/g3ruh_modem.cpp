/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Modem de banda base G3RUH 9600. Ver include/g3ruh_modem.h
 */

#include <math.h>
#include <string.h>
#include "g3ruh_modem.h"

namespace G3RUH {

    static constexpr float PI_F = 3.14159265358979f;
    #ifndef G3RUH_TUNE
    static constexpr float LPF_BAUD = 1.0f, LPF_SYM = 1.0f, AGC_FAST = 0.08f, AGC_SLOW = 0.00012f;
    static constexpr double PLL_LOCKED = 0.89, PLL_SEARCH = 0.67;
    #endif

    // ---- Emision ----------------------------------------------------------------------------------------------------

    void BaseTx::start(const uint8_t* bytes, size_t nbytes, uint32_t sampleRate, float amplitude) {
        data = bytes; nbits = nbytes * 8; n = 0;
        fs = sampleRate ? sampleRate : 48000;
        amp = amplitude < 0 ? 0 : (amplitude > 1 ? 1 : amplitude);
        total = (size_t)(((uint64_t)(nbits + SPAN) * fs) / BAUD);
        const float beta = 0.5f;                            // coseno alzado: el espectro cabe en 0,75 x 9600 = 7,2 kHz
        for (int i = 0; i <= SPAN * RES; i++) {
            const float t = (float)i / RES - SPAN / 2.0f;   // en simbolos, centrado
            float h;
            if (fabsf(t) < 1e-6f) h = 1.0f;
            else if (fabsf(fabsf(2 * beta * t) - 1) < 1e-4f) h = (PI_F / 4) * sinf(PI_F / (2 * beta)) / (PI_F / (2 * beta));
            else h = (sinf(PI_F * t) / (PI_F * t)) * cosf(PI_F * beta * t) / (1 - (2 * beta * t) * (2 * beta * t));
            pulse[i] = h;
        }
    }

    float BaseTx::level(long k) const {
        if (k < 0 || (size_t)k >= nbits) return 0.0f;
        return ((data[k >> 3] >> (7 - (k & 7))) & 1) ? 1.0f : -1.0f;
    }

    size_t BaseTx::fill(int16_t* out, size_t maxSamples) {
        size_t w = 0;
        while (w < maxSamples && n < total) {
            const float u = (float)((double)n * BAUD / fs) - SPAN / 2.0f;    // tiempo en simbolos (retrasado medio pulso)
            const long k0 = (long)floorf(u);
            float s = 0;
            for (long k = k0 - SPAN / 2; k <= k0 + SPAN / 2 + 1; k++) {
                const float d = u - (float)k + SPAN / 2.0f;                   // posicion dentro del pulso del bit k
                if (d < 0 || d > SPAN) continue;
                const float pos = d * RES;
                const int i = (int)pos;
                const float fr = pos - i;
                const float h = (i >= SPAN * RES) ? pulse[SPAN * RES] : pulse[i] + (pulse[i + 1] - pulse[i]) * fr;
                s += level(k) * h;
            }
            float v = s * amp * 0.8f;                                         // margen: los picos del coseno alzado pasan de 1
            if (v > 1) v = 1;
            if (v < -1) v = -1;
            out[w++] = (int16_t)lrintf(v * 32767.0f);
            n++;
        }
        return w;
    }

    // ---- Recepcion --------------------------------------------------------------------------------------------------

    bool BaseRx::begin(uint32_t sampleRate) {
        if (sampleRate < 19200 || sampleRate > 96000) return false;
        fs = sampleRate;
        // paso bajo: corte en LPF_BAUD x baudios, LPF_SYM simbolos, ventana de coseno, ganancia 1 en continua
        int len = (int)lrintf(LPF_SYM * fs / BAUD) | 1;
        if (len > MAX_TAPS - 1) len = MAX_TAPS - 1;
        lpLen = len;
        const float fc = LPF_BAUD * BAUD, mid = (len - 1) * 0.5f;
        float sum = 0;
        for (int i = 0; i < len; i++) {
            const float k = i - mid;
            const float h = (fabsf(k) < 1e-6f) ? 2.0f * fc / fs : sinf(2.0f * PI_F * fc * k / fs) / (PI_F * k);
            lp[i] = h * (len > 1 ? sinf(PI_F * (i + 0.5f) / len) : 1.0f);
            sum += lp[i];
        }
        for (int i = 0; i < len; i++) lp[i] /= sum;
        pllStep = (uint32_t)(((uint64_t)BAUD << 32) / fs);
        agcFast = AGC_FAST;
        agcSlow = AGC_SLOW * 48000.0f / (float)fs;
        reset();
        return true;
    }

    void BaseRx::reset() {
        memset(hist, 0, sizeof(hist)); hpos = 0;
        dcAvg = 0; peak = 0; valley = 0; prevDemod = 0;
        pll = 0; prevData = 0; byteAcc = 0; byteBits = 0; flagReg = 0; lockedBits = 0;
        decoder.reset();
    }

    void BaseRx::onFrame(const uint8_t* f, size_t len, void* self) {
        BaseRx* r = (BaseRx*)self;
        r->frames++;
        if (r->userCb) r->userCb(f, len, r->userCtx);
    }

    // bits en el aire (aun aleatorizados) -> bytes MSB primero para el decodificador
    void BaseRx::bitOut(uint8_t b) {
        byteAcc = (uint8_t)((byteAcc << 1) | b);
        if (++byteBits == 8) {
            decoder.feed(&byteAcc, 1, onFrame, this);
            byteBits = 0;
        }
        // DCD aproximado: transiciones regulares (el aleatorizador garantiza que hay muchas); se mantiene 64 bits
        flagReg = (flagReg << 1) | b;
        const uint32_t x = flagReg ^ (flagReg >> 1);
        if ((x & 0xFFFF) != 0 && (x & 0xFFFF) != 0xFFFF) lockedBits = 64; else if (lockedBits > 0) lockedBits--;
    }

    void BaseRx::feed(float x, AX25G3::FrameCallback cb, void* ctx) {
        userCb = cb; userCtx = ctx;
        dcAvg += (x - dcAvg) * 0.0005f;
        x -= dcAvg;
        hpos = (hpos == 0) ? lpLen - 1 : hpos - 1;
        hist[hpos] = x; hist[hpos + lpLen] = x;
        float y = 0;
        for (int i = 0; i < lpLen; i++) y += lp[i] * hist[hpos + 1 + i];

        // CAG pico / valle: centra el umbral aunque quede algo de continua o la senal sea asimetrica
        peak   = (y >= peak)   ? y * agcFast + peak * (1 - agcFast)   : y * agcSlow + peak * (1 - agcSlow);
        valley = (y <= valley) ? y * agcFast + valley * (1 - agcFast) : y * agcSlow + valley * (1 - agcSlow);
        const float demod = (peak > valley) ? (y - 0.5f * (peak + valley)) / (peak - valley) : 0.0f;
        const uint8_t data = demod > 0 ? 1 : 0;

        const int32_t prev = pll;
        pll = (int32_t)((uint32_t)pll + pllStep);
        if (pll < 0 && prev > 0) {
            const float after = (float)((uint32_t)pll - 0x80000000u) / (float)pllStep;
            const float atCross = demod - (demod - prevDemod) * after;
            bitOut(atCross > 0 ? 1 : 0);
        }
        if (data != prevData) {
            float t0 = (prevDemod != demod) ? prevDemod / (prevDemod - demod) : 1.0f;
            if (t0 < 0) t0 = 0;
            if (t0 > 1) t0 = 1;
            const double back = (1.0 - t0) * (double)pllStep;
            double nudged = ((double)pll - back) * (lockedBits > 0 ? PLL_LOCKED : PLL_SEARCH) + back;
            if (nudged > 2147483647.0) nudged -= 4294967296.0;
            if (nudged < -2147483648.0) nudged += 4294967296.0;
            pll = (int32_t)nudged;
            prevData = data;
        }
        prevDemod = demod;
    }

    void BaseRx::feed(const int16_t* x, size_t n, AX25G3::FrameCallback cb, void* ctx) {
        for (size_t i = 0; i < n; i++) feed(x[i] / 32768.0f, cb, ctx);
    }
}
