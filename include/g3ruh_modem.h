/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Modem de banda base G3RUH 9600 baudios por audio (LilyGO T-TWR: radio FM SA868 con los filtros de voz puenteados).
 * Codigo C++ puro, sin Arduino: se prueba en el PC con tools/afsk (frente a gen_packets -B 9600 y atest de Dire Wolf).
 *
 *  - Emision: los bytes en el aire de AX25G3::g3ruhEncode (NRZI + aleatorizador, MSB primero) -> niveles +-1 con pulsos de
 *    coseno alzado (caida 0,5, 4 simbolos) a la frecuencia de muestreo que se pida.
 *  - Recepcion: continua fuera -> paso bajo -> CAG (pico / valle) -> umbral -> PLL del reloj de bit con interpolacion ->
 *    bytes al AX25G3::Decoder (desaleatorizador, NRZI, HDLC, FX.25, IL2P). La polaridad da igual (NRZI tras el aleatorizador).
 */

#ifndef G3RUH_MODEM_H_
#define G3RUH_MODEM_H_

#include <stddef.h>
#include <stdint.h>
#include "ax25_g3ruh.h"

namespace G3RUH {

    constexpr int BAUD = 9600;

    class BaseTx {
    public:
        void   start(const uint8_t* bytes, size_t nbytes, uint32_t sampleRate, float amplitude);
        size_t fill(int16_t* out, size_t maxSamples);       // muestras escritas; 0 al terminar
        bool   done() const { return n >= total; }
    private:
        static constexpr int SPAN = 4, RES = 64;            // pulso de 4 simbolos, 64 puntos por simbolo
        float    pulse[SPAN * RES + 1];
        const uint8_t* data = nullptr;
        size_t   nbits = 0, n = 0, total = 0;
        uint32_t fs = 0;
        float    amp = 0;
        float    level(long k) const;                       // +-1 del bit k (0 fuera de la trama)
    };

    class BaseRx {
    public:
        bool  begin(uint32_t sampleRate);                   // 19200..96000 (en la placa 48000: 5 muestras por bit)
        void  reset();
        void  feed(float x, AX25G3::FrameCallback cb, void* ctx);
        void  feed(const int16_t* x, size_t n, AX25G3::FrameCallback cb, void* ctx);
        bool  dataCarrier() const { return lockedBits > 0; }
        uint32_t frames = 0;
        AX25G3::Decoder decoder;
    private:
        static constexpr int MAX_TAPS = 48;
        uint32_t fs = 0;
        float    lp[MAX_TAPS]; int lpLen = 0;
        float    hist[MAX_TAPS * 2]; int hpos = 0;
        float    dcAvg = 0, peak = 0, valley = 0, agcFast = 0.3f, agcSlow = 0;
        float    prevDemod = 0;
        int32_t  pll = 0; uint32_t pllStep = 0;
        uint8_t  prevData = 0, byteAcc = 0, byteBits = 0;
        uint32_t flagReg = 0;
        int      lockedBits = 0;
        AX25G3::FrameCallback userCb = nullptr; void* userCtx = nullptr;
        static void onFrame(const uint8_t* f, size_t len, void* self);
        void bitOut(uint8_t b);
    };
}

#endif
