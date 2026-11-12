/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Modo FM de la T-TWR: tratamiento del audio recibido, sin hardware (probado en el PC: tools/fm/test_fm_dsp.cpp).
 *  - NoiseDetector: distingue el soplido de FM (sin senal) de la voz por bloques de 20 ms: proporcion de energia de alta
 *    frecuencia (primera diferencia) y regularidad de la envolvente.
 *  - AutoSquelch: silenciador automatico. Por software (Rev2.1: el ESP32 gobierna el altavoz) con el detector de ruido y, si
 *    hay RSSI (firmware de OpenRTX), con el suelo de ruido medido + un margen. Por nivel (Rev2.0: solo el silenciador del modulo):
 *    sube el nivel si se abre con ruido y lo prueba a bajar cuando el canal lleva tiempo libre.
 *  - NoiseReducer: reduccion de ruido por resta espectral (STFT de 256 puntos con solape del 50 %, ventana raiz de Hann,
 *    estimacion del ruido por minimos, ganancia suavizada para evitar el "ruido musical").
 *  - VoiceFilter: banda de voz para el altavoz (paso alto de 300 Hz: corrientes continuas, zumbido y tonos CTCSS; paso bajo de
 *    3 kHz de cuarto orden: soplido por encima de la voz) y limitador suave (sin recortes duros que distorsionen).
 */

#ifndef FM_DSP_H_
#define FM_DSP_H_

#include <stddef.h>
#include <stdint.h>

namespace FmDsp {

    class NoiseDetector {
    public:
        void  begin(float fs);
        bool  feed(float x);                    // true al terminar un bloque de 20 ms
        float hfRatio() const { return hf; }    // energia de la diferencia / energia (soplido ~0,3..0,6; voz ~0,05..0,15)
        float rms() const { return lastRms; }
        float hissLevel() const { return hiss; }         // nivel del soplido sin portadora (aprendido)
        bool  noise() const { return noiseCount >= 3; }     // tres bloques seguidos con aspecto de soplido
        bool  noiseNow() const { return noiseCount > 0; }   // el ultimo bloque lo parece
        bool  quiet() const { return lastRms < 0.004f; }    // practicamente silencio (silenciador del modulo cerrado)
        static constexpr float HF_THRESHOLD = 0.22f;
    private:
        int   blockLen = 264, n = 0, noiseCount = 0;
        float dc = 0, prev = 0, e = 0, ed = 0, hf = 0, lastRms = 0, hiss = 0;
    };

    class AutoSquelch {
    public:
        // Software: decide por bloque si el altavoz debe sonar. rssiDbm solo cuenta si haveRssi
        bool  softUpdate(const NoiseDetector& d, int rssiDbm, bool haveRssi);
        bool  open() const { return isOpen; }
        float rssiFloor() const { return floorDb; }
        static constexpr float RSSI_MARGIN_DB = 6.0f;
        // Por nivel: busy = silenciador del modulo abierto; noisy = abierto con soplido. Devuelve el nivel nuevo o -1
        int   levelUpdate(uint32_t now, bool busy, bool noisy, int level);
    private:
        bool     isOpen = false;
        int      openCount = 0, closeCount = 0;
        float    floorDb = 0; bool floorInit = false;
        uint32_t noisySince = 0, freeSince = 0, coolUntil = 0, probeAt = 0, nextProbe = 0;
        int      probedFrom = -1;
    };

    class NoiseReducer {
    public:
        static constexpr int N = 256, HOP = N / 2;
        void  begin();
        void  setStrength(float s);             // 0 (nada) .. 1 (fuerte)
        float process(float x);                 // una muestra dentro, una fuera (retardo de N muestras)
        void  reset();
    private:
        void  frame();
        float win[N], inBuf[N], outBuf[N], ready[HOP], noisePsd[N / 2 + 1], smoothPsd[N / 2 + 1], gain[N / 2 + 1];
        float re[N], im[N];
        int   pos = 0, outPos = 0;
        float beta = 1.5f, gmin = 0.12f;
        bool  primed = false;
        int   frames = 0;
    };

    // CW (A1A) en un receptor de FM: con portadora el soplido desaparece ("quieting"). Mide la energia de alta frecuencia del
    // audio en bloques de 2 ms; levelDb() = dB por debajo del soplido sin portadora (aprendido); on() con histeresis
    class QuietingDetector {
    public:
        void  begin(float fs);
        bool  feed(float x);                    // true al terminar un bloque
        float levelDb() const { return level; } // 0 = soplido, 20..40 = portadora
        bool  on() const { return isOn; }
        static constexpr float ON_DB = 9.0f, OFF_DB = 5.0f;
    private:
        int   blockLen = 26, n = 0;
        float prev = 0, e = 0, ref = -200, level = 0, smooth = 0;
        bool  isOn = false;
    };

    class VoiceFilter {
    public:
        void  begin(float fs, float lowHz = 300.0f, float highHz = 3000.0f);
        float process(float x);                 // banda de voz
        void  reset();
        static float limit(float x);            // limitador suave: lineal hasta 0,6 y luego curva hasta +-1 (nunca lo pasa)
    private:
        struct Biquad {
            float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            float run(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        };
        Biquad hp1, hp2, lp1, lp2;
    };

    void fft(float* re, float* im, int n, bool inverse);    // radix 2, n potencia de 2 (<= 1024)
}

#endif
