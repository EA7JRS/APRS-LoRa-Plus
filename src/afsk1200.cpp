/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Modem AFSK 1200 (Bell 202). Ver include/afsk1200.h
 */

#include <math.h>
#include <string.h>
#include "afsk1200.h"

namespace AFSK {

    static constexpr float PI_F = 3.14159265358979f;

    // ---- Emision ----------------------------------------------------------------------------------------------------

    size_t encodeBits(const uint8_t* frame, size_t len, uint8_t* out, size_t maxOutBytes, size_t leadFlags, size_t tailFlags, int fec) {
        if (!frame || !out || len == 0) return 0;
        size_t n = 0;
        const size_t tail = tailFlags ? tailFlags : 1;
        if (fec >= 1 && fec <= 3) n = AX25G3::g3ruhEncodeFx25(frame, len, out, maxOutBytes, leadFlags, 16 << (fec - 1), false, tail);
        if (fec == 4)             n = AX25G3::g3ruhEncodeIl2p(frame, len, out, maxOutBytes, leadFlags, false, tail);
        if (n == 0)               n = AX25G3::g3ruhEncode(frame, len, out, maxOutBytes, leadFlags, false, tail);
        return n * 8;
    }

    void ToneGen::start(const uint8_t* b, size_t n, uint32_t sampleRate, float amplitude) {
        bits = b; nbits = n; idx = 0; fs = sampleRate ? sampleRate : 48000; clk = 0; phase = 0;
        if (amplitude < 0) amplitude = 0;
        if (amplitude > 1) amplitude = 1;
        for (int i = 0; i < 256; i++) table[i] = (int16_t)lrintf(32767.0f * amplitude * sinf(2.0f * PI_F * i / 256.0f));
        stepMark  = (uint32_t)(((uint64_t)MARK_HZ  << 32) / fs);
        stepSpace = (uint32_t)(((uint64_t)SPACE_HZ << 32) / fs);
    }

    size_t ToneGen::fill(int16_t* out, size_t maxSamples) {
        size_t n = 0;
        while (n < maxSamples && idx < nbits) {
            out[n++] = table[phase >> 24];
            phase += bitAt(bits, idx) ? stepMark : stepSpace;
            clk += BAUD;
            if (clk >= fs) { clk -= fs; idx++; }
        }
        return n;
    }

    // ---- Recepcion --------------------------------------------------------------------------------------------------

    // Paso banda sin ventana (truncado), ganancia 1 en el centro de la banda (1700 Hz)
    static int designBandpass(float* taps, int len, float fs, float lowHz, float highHz, int maxLen) {
        if (len > maxLen) len = maxLen;
        if ((len & 1) == 0) len--;
        const float mid = (len - 1) * 0.5f;
        for (int i = 0; i < len; i++) {
            const float k = i - mid;
            auto sinc = [&](float fc) { return k == 0 ? 2.0f * fc / fs : sinf(2.0f * PI_F * fc * k / fs) / (PI_F * k); };
            taps[i] = sinc(highHz) - sinc(lowHz);
        }
        float re = 0, im = 0;
        for (int i = 0; i < len; i++) { re += taps[i] * cosf(2.0f * PI_F * 1700.0f * i / fs); im += taps[i] * sinf(2.0f * PI_F * 1700.0f * i / fs); }
        const float g = sqrtf(re * re + im * im);
        if (g > 0) for (int i = 0; i < len; i++) taps[i] /= g;
        return len;
    }

    // Coseno alzado en raiz (RRC) de ancho widthSym simbolos y caida rolloff; ganancia 1 en continua
    static int designRrc(float* taps, float spb, float widthSym, float rolloff, int maxLen) {
        int len = (int)lrintf(spb * widthSym) | 1;
        if (len > maxLen) len = (maxLen & 1) ? maxLen : maxLen - 1;
        const float mid = (len - 1) * 0.5f, a = rolloff;
        float sum = 0;
        for (int i = 0; i < len; i++) {
            const float t = (i - mid) / spb;
            float h;
            if (fabsf(t) < 1e-6f) h = 1 - a + 4 * a / PI_F;
            else if (fabsf(fabsf(4 * a * t) - 1) < 1e-4f)
                h = a / sqrtf(2.0f) * ((1 + 2 / PI_F) * sinf(PI_F / (4 * a)) + (1 - 2 / PI_F) * cosf(PI_F / (4 * a)));
            else h = (sinf(PI_F * t * (1 - a)) + 4 * a * t * cosf(PI_F * t * (1 + a))) / (PI_F * t * (1 - (4 * a * t) * (4 * a * t)));
            taps[i] = h; sum += h;
        }
        if (sum != 0) for (int i = 0; i < len; i++) taps[i] /= sum;
        return len;
    }

    // Convolucion sobre un historial duplicado (escrito en pos y en pos + len) para no partir la suma en dos
    static inline float convolve(const float* taps, int len, const float* hist, int pos) {
        const float* h = hist + pos + 1;
        float acc = 0;
        for (int i = 0; i < len; i++) acc += taps[i] * h[i];
        return acc;
    }

    static inline void push(float* hist, int len, int& pos, float v) {
        pos = (pos == 0) ? len - 1 : pos - 1;
        hist[pos] = v; hist[pos + len] = v;
    }

    bool Demod::begin(uint32_t sampleRate) {
        if (sampleRate < 8000 || sampleRate > 48000) return false;
        fs = sampleRate;
        const float spb = (float)fs / BAUD;
        // paso banda estrecho y largo: de marca - 0,155 x baudios a espacio + 0,155 x baudios, 8 simbolos (perfil A de Dire Wolf)
        bpLen = designBandpass(bp, (int)lrintf(spb * 8.163f) | 1, (float)fs, MARK_HZ - 0.155f * BAUD, SPACE_HZ + 0.155f * BAUD, MAX_TAPS);
        // filtro de los correladores: coseno alzado en raiz de 2,8 simbolos y caida 0,2
        lpLen = designRrc(lp, spb, 2.80f, 0.20f, MAX_TAPS);
        for (int i = 0; i < 256; i++) { cosT[i] = cosf(2.0f * PI_F * i / 256.0f); sinT[i] = sinf(2.0f * PI_F * i / 256.0f); }
        stepM   = (uint32_t)(((uint64_t)MARK_HZ  << 32) / fs);
        stepS   = (uint32_t)(((uint64_t)SPACE_HZ << 32) / fs);
        pllStep = (uint32_t)(((uint64_t)BAUD << 32) / fs);
        // CAG: subida rapida, bajada lenta (0,000090 por muestra a 44100 Hz en Dire Wolf; mismo tiempo a otra frecuencia)
        agcSlow   = 0.000090f * 44100.0f / (float)fs;
        peakDecay = 1.0f - 1.0f / (0.5f * (float)fs);           // nivel de entrada: medio segundo
        reset();
        return true;
    }

    void Demod::reset() {
        memset(hist, 0, sizeof(hist)); hpos = 0;
        memset(mI, 0, sizeof(mI)); memset(mQ, 0, sizeof(mQ)); memset(sI, 0, sizeof(sI)); memset(sQ, 0, sizeof(sQ)); lpos = 0;
        phM = phS = 0;
        mPeak = mValley = sPeak = sValley = 0;
        dcAvg = 0; peakIn = 0;
        pll = 0; prevDemod = 0; prevData = 0; lastBit = 0; flagReg = 0; lockedBits = 0; byteAcc = 0; byteBits = 0;
        decoder.reset();
        decoder.setScrambled(false);
    }

    void Demod::onFrame(const uint8_t* f, size_t len, void* self) {
        Demod* d = (Demod*)self;
        d->frames++;
        if (d->userCb) d->userCb(f, len, d->userCtx);
    }

    void Demod::bitOut(uint8_t b) {
        const uint8_t decoded = (b == lastBit) ? 1 : 0;     // NRZI (solo para el DCD; el decodificador hace el suyo)
        lastBit = b;
        flagReg = (uint8_t)((flagReg >> 1) | (decoded << 7));
        if (flagReg == 0x7E) lockedBits = 64;              // bandera: hay datos durante al menos 8 bytes mas
        else if (lockedBits > 0) lockedBits--;
        byteAcc = (uint8_t)((byteAcc << 1) | b);           // bits de tono, MSB primero, al decodificador (HDLC, FX.25, IL2P)
        if (++byteBits == 8) { decoder.feed(&byteAcc, 1, onFrame, this); byteBits = 0; }
    }

    void Demod::feed(float x, AX25G3::FrameCallback cb, void* ctx) {
        userCb = cb; userCtx = ctx;
        // continua fuera (el ADC de la placa tiene la senal centrada en media escala)
        dcAvg += (x - dcAvg) * 0.002f;
        x -= dcAvg;
        const float ax = fabsf(x);
        peakIn = (ax > peakIn) ? ax : peakIn * peakDecay;

        push(hist, bpLen, hpos, x);
        const float y = convolve(bp, bpLen, hist, hpos);

        push(mI, lpLen, lpos, y * cosT[phM >> 24]);
        mQ[lpos] = mQ[lpos + lpLen] = y * sinT[phM >> 24];
        sI[lpos] = sI[lpos + lpLen] = y * cosT[phS >> 24];
        sQ[lpos] = sQ[lpos + lpLen] = y * sinT[phS >> 24];
        phM += stepM; phS += stepS;

        const float mi = convolve(lp, lpLen, mI, lpos), mq = convolve(lp, lpLen, mQ, lpos);
        const float si = convolve(lp, lpLen, sI, lpos), sq = convolve(lp, lpLen, sQ, lpos);
        const float m = sqrtf(mi * mi + mq * mq);
        const float s = sqrtf(si * si + sq * sq);

        // CAG de cada tono por separado: compensa la inclinacion (pre/desenfasis) entre 1200 y 2200 Hz
        auto agc = [&](float in, float& peak, float& valley) {
            peak   = (in >= peak)   ? in * agcFast + peak * (1 - agcFast)   : in * agcSlow + peak * (1 - agcSlow);
            valley = (in <= valley) ? in * agcFast + valley * (1 - agcFast) : in * agcSlow + valley * (1 - agcSlow);
            return (peak > valley) ? (in - 0.5f * (peak + valley)) / (peak - valley) : 0.0f;
        };
        const float demod = agc(m, mPeak, mValley) - agc(s, sPeak, sValley);
        const uint8_t data = demod > 0 ? 1 : 0;

        // PLL del reloj de bit: se toma el bit cuando el acumulador de 32 bits desborda; cada cambio de tono lo acerca a cero.
        // Con pocas muestras por bit (11 a 13,2 kHz) el instante de decision y el de cada cruce caen entre dos muestras: se
        // interpolan linealmente, lo que da casi el mismo resultado que muestrear a 40 kHz.
        const int32_t prev = pll;
        pll = (int32_t)((uint32_t)pll + pllStep);
        if (pll < 0 && prev > 0) {
            const float after = (float)((uint32_t)pll - 0x80000000u) / (float)pllStep;    // fraccion de muestra pasada desde el desborde
            const float atCross = demod - (demod - prevDemod) * after;
            bitOut(atCross > 0 ? 1 : 0);
        }
        if (data != prevData) {
            // momento del cruce por cero entre la muestra anterior y esta (0 = en la anterior, 1 = en esta)
            float t0 = (prevDemod != demod) ? prevDemod / (prevDemod - demod) : 1.0f;
            if (t0 < 0) t0 = 0;
            if (t0 > 1) t0 = 1;
            const double back = (1.0 - t0) * (double)pllStep;                                 // valor del PLL en el cruce
            const double atX  = (double)pll - back;
            double nudged = atX * (lockedBits > 0 ? 0.74 : 0.50) + back;
            if (nudged > 2147483647.0) nudged -= 4294967296.0;
            if (nudged < -2147483648.0) nudged += 4294967296.0;
            pll = (int32_t)nudged;
            prevData = data;
        }
        prevDemod = demod;
    }

    void Demod::feed(const int16_t* x, size_t n, AX25G3::FrameCallback cb, void* ctx) {
        for (size_t i = 0; i < n; i++) feed(x[i] / 32768.0f, cb, ctx);
    }
}
