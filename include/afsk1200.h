/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Modem AFSK 1200 baudios (Bell 202: marca 1200 Hz, espacio 2200 Hz), el del APRS analogico de VHF (144,800 / 144,390 MHz).
 * Codigo C++ puro, sin Arduino: se prueba en el PC con tools/afsk (frente a gen_packets y atest de Dire Wolf).
 *
 *  - Emision: trama AX.25 (sin FCS) -> banderas + HDLC (relleno de bits, FCS) -> NRZI -> tonos con fase continua.
 *  - Recepcion: muestras de audio -> paso banda -> correladores de marca y espacio -> CAG de cada tono -> PLL digital
 *    del reloj de bit -> NRZI -> HDLC (AX25G3::Hdlc, el mismo del G3RUH). La idea del demodulador es la del perfil "A"
 *    de Dire Wolf (WB2OSZ), reescrita desde cero.
 */

#ifndef AFSK1200_H_
#define AFSK1200_H_

#include <stddef.h>
#include <stdint.h>
#include "ax25_g3ruh.h"

namespace AFSK {

    constexpr int BAUD       = 1200;
    constexpr int MARK_HZ    = 1200;
    constexpr int SPACE_HZ   = 2200;

    // Trama AX.25 sin FCS -> bits de tono ya en NRZI (1 = marca, 0 = espacio), empaquetados LSB primero en out.
    // leadFlags banderas delante (TXDELAY: 1 bandera = 6,67 ms) y tailFlags detras. Devuelve el numero de bits o 0 si no cabe
    size_t encodeBits(const uint8_t* frame, size_t len, uint8_t* out, size_t maxOutBytes, size_t leadFlags, size_t tailFlags);

    inline uint8_t bitAt(const uint8_t* bits, size_t i) { return (uint8_t)((bits[i >> 3] >> (i & 7)) & 1); }

    // Generador de tonos con fase continua a partir de los bits de encodeBits. amplitude 0..1 sobre la escala de int16
    class ToneGen {
    public:
        void   start(const uint8_t* bits, size_t nbits, uint32_t sampleRate, float amplitude);
        size_t fill(int16_t* out, size_t maxSamples);       // muestras escritas; 0 cuando ha terminado
        bool   done() const { return idx >= nbits; }
        static size_t samplesFor(size_t nbits, uint32_t sampleRate) { return (size_t)(((uint64_t)nbits * sampleRate + BAUD - 1) / BAUD); }
    private:
        const uint8_t* bits = nullptr;
        size_t   nbits = 0, idx = 0;
        uint32_t fs = 0, clk = 0, phase = 0, stepMark = 0, stepSpace = 0;
        int16_t  table[256];
    };

    // Demodulador. sampleRate de 8000 a 48000 (en la placa 13200: 11 muestras por bit). Las tramas buenas (FCS correcto)
    // llegan sin FCS por la llamada de vuelta
    class Demod {
    public:
        Demod() {}
        bool  begin(uint32_t sampleRate);
        void  reset();
        void  feed(const int16_t* x, size_t n, AX25G3::FrameCallback cb, void* ctx);
        void  feed(float x, AX25G3::FrameCallback cb, void* ctx);
        bool  dataCarrier() const { return lockedBits > 0; }     // hay banderas o datos HDLC recientes (DCD)
        float level() const { return peakIn; }                   // nivel de pico de la entrada (0..1 de la escala)
        uint32_t frames = 0;                                     // tramas con FCS correcto
    private:
        static constexpr int MAX_TAPS = 320;          // 8 simbolos de paso banda a 48 kHz
        uint32_t fs = 0;
        // paso banda
        float    bp[MAX_TAPS]; int bpLen = 0;
        float    hist[MAX_TAPS * 2]; int hpos = 0;
        // osciladores locales y filtros paso bajo de I/Q
        float    lp[MAX_TAPS]; int lpLen = 0;
        float    mI[MAX_TAPS * 2], mQ[MAX_TAPS * 2], sI[MAX_TAPS * 2], sQ[MAX_TAPS * 2]; int lpos = 0;
        uint32_t phM = 0, phS = 0, stepM = 0, stepS = 0;
        float    cosT[256], sinT[256];
        // CAG de cada tono
        float    mPeak = 0, mValley = 0, sPeak = 0, sValley = 0, agcFast = 0.70f, agcSlow = 0;
        // entrada
        float    dcAvg = 0, peakIn = 0, peakDecay = 0;
        // PLL y bits
        int32_t  pll = 0; uint32_t pllStep = 0; float prevDemod = 0;
        uint8_t  prevData = 0, lastBit = 0;
        uint8_t  flagReg = 0;
        int      lockedBits = 0;
        AX25G3::Hdlc hdlc;
        AX25G3::FrameCallback userCb = nullptr; void* userCtx = nullptr;
        static void onFrame(const uint8_t* f, size_t len, void* self);
        void bitOut(uint8_t b);
    };
}

#endif
