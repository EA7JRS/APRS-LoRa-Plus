/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Audio del modo FM de la T-TWR. Ver include/fm_dsp.h
 */

#include <math.h>
#include <string.h>
#include "fm_dsp.h"

namespace FmDsp {

    // ---- Detector de ruido ------------------------------------------------------------------------------------------

    void NoiseDetector::begin(float fs) {
        blockLen = (int)(fs * 0.02f);
        if (blockLen < 32) blockLen = 32;
        n = 0; noiseCount = 0; dc = 0; prev = 0; e = 0; ed = 0; hf = 0; lastRms = 0; hiss = 0;
    }

    bool NoiseDetector::feed(float x) {
        dc += (x - dc) * 0.002f;
        const float v = x - dc;
        const float d = v - prev;
        prev = v;
        e += v * v;
        ed += d * d;
        if (++n < blockLen) return false;
        lastRms = sqrtf(e / (float)n);
        hf = e > 1e-9f ? ed / e : 0.0f;
        // soplido: mucha alta frecuencia y un nivel cercano al del canal sin portadora (con portadora, el ruido baja: "quieting")
        const bool hfLike = lastRms > 0.004f && hf > HF_THRESHOLD;
        if (hfLike && lastRms > hiss) hiss = lastRms;
        else hiss *= 0.9995f;                               // olvida despacio (~2 dB por minuto)
        const bool looksNoise = hfLike && lastRms > 0.3f * hiss;
        noiseCount = looksNoise ? (noiseCount < 100 ? noiseCount + 1 : 100) : 0;
        n = 0; e = 0; ed = 0;
        return true;
    }

    // ---- Silenciador automatico -------------------------------------------------------------------------------------

    bool AutoSquelch::softUpdate(const NoiseDetector& d, int rssiDbm, bool haveRssi) {
        // abre con dos bloques sin aspecto de soplido; cierra con tres seguidos que lo tienen (silencio = portadora sin modular)
        bool signal = isOpen ? !d.noise() : !d.noiseNow();
        if (haveRssi) {
            const float r = (float)rssiDbm;
            if (!floorInit) { floorDb = r; floorInit = true; }
            if (r < floorDb) floorDb = r;                   // suelo de ruido: sigue a los minimos y sube despacio (1 dB/s)
            else if (!isOpen) floorDb += 0.02f;
            const float thr = floorDb + RSSI_MARGIN_DB + (isOpen ? -2.0f : 0.0f);      // histeresis
            signal = signal && r >= thr;
        }
        if (signal) { openCount++; closeCount = 0; } else { closeCount++; openCount = 0; }
        if (!isOpen && openCount >= 2) isOpen = true;       // 40 ms
        if (isOpen && closeCount >= 1) isOpen = false;      // ya son 60 ms de soplido (noise())
        return isOpen;
    }

    int AutoSquelch::levelUpdate(uint32_t now, bool busy, bool noisy, int level) {
        if (level < 1) level = 1;
        if ((int32_t)(now - coolUntil) < 0) return -1;
        if (busy && noisy) {
            if (noisySince == 0) noisySince = now ? now : 1;
            if (now - noisySince >= 1000 && level < 8) {    // se abre con soplido: un nivel mas
                noisySince = 0; freeSince = 0;
                coolUntil = now + 3000;
                if (probedFrom >= 0) { nextProbe = now + 300000UL; probedFrom = -1; }   // la prueba de bajar fallo: 5 min sin probar
                return level + 1;
            }
            return -1;
        }
        noisySince = 0;
        if (busy) { freeSince = 0; return -1; }
        if (freeSince == 0) freeSince = now ? now : 1;
        if (probedFrom >= 0 && now - probeAt >= 10000) probedFrom = -1;         // la prueba aguanto 10 s: se queda
        if (level > 1 && now - freeSince >= 60000 && (int32_t)(now - nextProbe) >= 0 && probedFrom < 0) {
            probedFrom = level; probeAt = now; freeSince = now;
            coolUntil = now + 1500;
            return level - 1;                               // canal libre un minuto: probar un nivel menos
        }
        return -1;
    }

    // ---- FFT --------------------------------------------------------------------------------------------------------

    void fft(float* re, float* im, int n, bool inverse) {
        for (int i = 1, j = 0; i < n; i++) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
        }
        for (int len = 2; len <= n; len <<= 1) {
            const float ang = 2.0f * (float)M_PI / (float)len * (inverse ? 1.0f : -1.0f);
            const float wr = cosf(ang), wi = sinf(ang);
            for (int i = 0; i < n; i += len) {
                float cr = 1.0f, ci = 0.0f;
                for (int k = 0; k < len / 2; k++) {
                    const int a = i + k, b = i + k + len / 2;
                    const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr; im[a] += ti;
                    const float nr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr; cr = nr;
                }
            }
        }
        if (inverse) for (int i = 0; i < n; i++) { re[i] /= (float)n; im[i] /= (float)n; }
    }

    // ---- Reduccion de ruido -----------------------------------------------------------------------------------------

    void NoiseReducer::begin() {
        for (int i = 0; i < N; i++) win[i] = sqrtf(0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)i / (float)N));   // raiz de Hann periodica
        reset();
    }

    void NoiseReducer::reset() {
        memset(inBuf, 0, sizeof(inBuf)); memset(outBuf, 0, sizeof(outBuf)); memset(ready, 0, sizeof(ready));
        for (int k = 0; k <= N / 2; k++) { noisePsd[k] = 0; smoothPsd[k] = 0; gain[k] = 1; }
        pos = 0; outPos = 0; primed = false; frames = 0;
    }

    void NoiseReducer::setStrength(float s) {
        if (s < 0) s = 0;
        if (s > 1) s = 1;
        beta = 0.5f + 2.0f * s;                             // sobrerresta: 0,5 .. 2,5
        gmin = 0.35f - 0.27f * s;                           // suelo de ganancia: -9 dB .. -21 dB
    }

    void NoiseReducer::frame() {
        for (int i = 0; i < N; i++) { re[i] = inBuf[i] * win[i]; im[i] = 0; }
        fft(re, im, N, false);
        frames++;
        for (int k = 0; k <= N / 2; k++) {
            const float p = re[k] * re[k] + im[k] * im[k];
            smoothPsd[k] = frames == 1 ? p : 0.7f * smoothPsd[k] + 0.3f * p;
            float& nz = noisePsd[k];
            if (frames == 1 || smoothPsd[k] < nz) nz = smoothPsd[k];             // minimos: el ruido es lo que queda en las pausas
            else nz *= 1.004f;                                                     // sube despacio (~+2 dB/s) si el ruido aumenta
            float g = smoothPsd[k] > 1e-12f ? 1.0f - beta * 2.2f * nz / smoothPsd[k] : gmin;      // 2,2: el minimo queda ~3,5 dB por debajo de la media del ruido
            if (g < gmin) g = gmin;
            gain[k] = 0.6f * gain[k] + 0.4f * g;                                   // suavizado: menos "ruido musical"
            const float gg = frames < 8 ? 1.0f : gain[k];                          // primeros 60 ms: aprende sin tocar
            re[k] *= gg; im[k] *= gg;
            if (k > 0 && k < N / 2) { re[N - k] = re[k]; im[N - k] = -im[k]; }
        }
        fft(re, im, N, true);
        for (int i = 0; i < N; i++) outBuf[i] += re[i] * win[i];
    }

    float NoiseReducer::process(float x) {
        // entrada: ventana deslizante de N con salto HOP; salida: suma solapada, ya completa, del salto anterior (retardo N)
        inBuf[N - HOP + pos] = x;
        const float y = ready[pos];
        pos++;
        if (pos == HOP) {
            frame();
            memcpy(ready, outBuf, sizeof(float) * HOP);
            memmove(inBuf, inBuf + HOP, sizeof(float) * (N - HOP));
            memmove(outBuf, outBuf + HOP, sizeof(float) * (N - HOP));
            memset(outBuf + N - HOP, 0, sizeof(float) * HOP);
            pos = 0;
        }
        return y;
    }

    // ---- CW por el silenciamiento del ruido -------------------------------------------------------------------------

    void QuietingDetector::begin(float fs) {
        blockLen = (int)(fs * 0.002f);
        if (blockLen < 8) blockLen = 8;
        n = 0; prev = 0; e = 0; ref = -200; level = 0; smooth = 0; isOn = false;
    }

    bool QuietingDetector::feed(float x) {
        const float d = x - prev;
        prev = x;
        e += d * d;
        if (++n < blockLen) return false;
        const float db = 10.0f * log10f(e / (float)n + 1e-12f);
        n = 0; e = 0;
        // referencia: el soplido sin portadora (lo mas alto oido, olvidando despacio: ~1 dB/s)
        if (db > ref) ref = db; else ref -= 0.002f;
        const float q = ref - db;
        smooth += (q - smooth) * 0.5f;                      // dos bloques: ~4 ms
        level = smooth < 0 ? 0 : smooth;
        if (!isOn && level > ON_DB) isOn = true;
        else if (isOn && level < OFF_DB) isOn = false;
        return true;
    }
}
