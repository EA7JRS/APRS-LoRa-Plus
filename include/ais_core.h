/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Receptor AIS (VHF marino, 161,975 MHz canal A / 162,025 MHz canal B; ITU-R M.1371): C++ puro, sin Arduino, se prueba en el PC
 * (tools/ais/test_ais.cpp).
 *
 *  - Demod: GMSK 9600 baudios desde el discriminador de FM (audio plano del SA868 de la T-TWR, o el de la app con un RTL-SDR):
 *    compensacion del acoplo de alterna de la entrada, paso bajo, CAG, umbral, PLL del reloj de bit, NRZI, HDLC (banderas,
 *    bits de relleno, octetos con el primer bit recibido como el de menor peso) y FCS CRC-16 X.25. Las tramas buenas salen como
 *    los octetos del mensaje (bits del mensaje de mayor a menor peso).
 *  - Mensajes 1, 2, 3 (posicion clase A), 4 (estacion base), 5 (datos estaticos y viaje), 9 (aeronave SAR), 14 (seguridad:
 *    SART / MOB / EPIRB), 18 y 19 (clase B), 21 (ayuda a la navegacion) y 24 (datos estaticos clase B).
 *  - NMEA 0183: frases !AIVDM de un mensaje (para OpenCPN, la app...) y su lectura (modulo AIS externo por UART: dAISy,
 *    Quark-elec...), con mensajes de varias frases.
 *  - Tabla de barcos con distancia, rumbo, CPA y TCPA respecto a la estacion propia.
 */

#ifndef AIS_CORE_H_
#define AIS_CORE_H_

#include <stddef.h>
#include <stdint.h>

namespace AisCore {

    constexpr int    BAUD = 9600;
    constexpr long   FREQ_A = 161975000L, FREQ_B = 162025000L;
    constexpr size_t MAX_MSG_BYTES = 128;          // 1008 bits como mucho (mensaje de 5 ranuras)

    // ---- Bits del mensaje (de mayor a menor peso) ----
    uint32_t    bitsU(const uint8_t* d, size_t nbits, size_t start, size_t len);
    int32_t     bitsS(const uint8_t* d, size_t nbits, size_t start, size_t len);
    void        bitsText(const uint8_t* d, size_t nbits, size_t start, size_t nchars, char* out, size_t outMax);   // 6 bits por caracter; quita @ y espacios finales
    uint16_t    crcX25(const uint8_t* d, size_t n);                    // CRC-16 de HDLC (inicio 0xFFFF, polinomio 0x8408 reflejado, final ^0xFFFF)

    // ---- Mensajes ----
    enum Class : char { CL_UNKNOWN = '?', CL_A = 'A', CL_B = 'B', CL_BASE = 'S', CL_ATON = 'N', CL_SAR = 'R', CL_SART = 'T' };

    struct Report {                                 // lo que trae un mensaje (los has* dicen que campos lleva)
        uint8_t  type;
        uint32_t mmsi;
        char     cls;
        bool     hasPos, hasSog, hasCog, hasHdg, hasStatus, hasName, hasCall, hasShip, hasDim, hasDest, hasEta, hasDraught, hasImo, hasAlt, hasText;
        double   lat, lon;
        float    sogKn, cogDeg;
        int16_t  hdg;
        uint8_t  status;                            // estado de navegacion (0 en marcha con motor... 14 AIS-SART activo, 15 sin definir)
        int8_t   rot;                               // -128 = sin dato
        char     name[21];
        char     call[8];
        char     dest[21];
        char     text[64];                          // mensaje 14
        uint8_t  shipType;
        uint16_t toBow, toStern;
        uint8_t  toPort, toStar;
        uint8_t  etaMonth, etaDay, etaHour, etaMinute;
        float    draughtM;
        uint32_t imo;
        int16_t  altM;                              // mensaje 9
        uint8_t  part;                              // mensaje 24: 0 = A, 1 = B
        uint8_t  aidType;                           // mensaje 21
    };

    bool        decode(const uint8_t* d, size_t nbits, Report& out);  // false si el tipo no se conoce o es demasiado corto
    const char* shipTypeName(uint8_t t);            // "Cargo", "Tanker", "Passenger", "Fishing", "Sailing", "Pleasure", "Tug", "SAR"...
    const char* statusName(uint8_t s);              // "Under way", "At anchor", "Moored", "Fishing", "Sailing", "SART"...
    bool        isDistress(uint32_t mmsi);          // 970 SART, 972 MOB, 974 EPIRB-AIS
    const char* distressName(uint32_t mmsi);        // "AIS-SART", "MOB", "EPIRB" o ""

    // ---- NMEA ----
    // Frases !AIVDM (o !AIVDO) de un mensaje; channel 'A' / 'B'; seqId 0..9 (solo con varias frases). Devuelve cuantas frases escribe en
    // out (separadas por "\r\n"), 0 si no caben.
    int         toNmea(const uint8_t* d, size_t nbits, char channel, int seqId, char* out, size_t outMax, bool own = false);
    uint8_t     nmeaChecksum(const char* s, size_t n);                 // XOR de los caracteres entre '!' y '*'

    class NmeaReader {                              // frases de un modulo externo -> mensajes completos
    public:
        // una linea (con o sin "\r\n"); true cuando completa un mensaje (bits en data / nbits, canal en channel)
        bool     feed(const char* line);
        uint8_t  data[MAX_MSG_BYTES];
        size_t   nbits = 0;
        char     channel = 'A';
        uint32_t bad = 0;                           // frases con checksum o formato malos
    private:
        uint8_t  acc[MAX_MSG_BYTES];
        size_t   accBits = 0;
        int      expectTotal = 0, expectNext = 0;
        char     seq = 0;
        bool     add(const char* payload, size_t len, int fill);
    };

    // ---- Demodulador GMSK 9600 + HDLC ----
    typedef void (*FrameCallback)(const uint8_t* msg, size_t nbytes, void* ctx);

    class Demod {
    public:
        bool     begin(float sampleRate, float hpfHz);   // 38400..96000; hpfHz = acoplo de alterna a compensar (0 = nada)
        void     reset();
        void     feed(float x, FrameCallback cb, void* ctx);
        bool     dataCarrier() const { return lockedBits > 0; }
        uint32_t frames = 0, crcErrors = 0;
    private:
        static constexpr int MAX_TAPS = 64;
        float    fs = 48000;
        float    lp[MAX_TAPS]; int lpLen = 0;
        float    hist[MAX_TAPS * 2]; int hpos = 0;
        float    hpfW = 0, integ = 0, leak = 0.999f;
        float    dcAvg = 0, peak = 0, valley = 0, agcFast = 0.08f, agcSlow = 0.0001f, prevDemod = 0;
        int32_t  pll = 0; uint32_t pllStep = 0;
        uint8_t  prevData = 0, prevBit = 0;
        int      lockedBits = 0;
        uint32_t shift = 0;
        // HDLC
        bool     inFrame = false;
        int      ones = 0;
        uint8_t  buf[MAX_MSG_BYTES + 4];
        size_t   nbuf = 0;
        uint8_t  cur = 0; int curBits = 0;
        void     hdlcBit(uint8_t b, FrameCallback cb, void* ctx);    // bit tras NRZI
        void     endFrame(FrameCallback cb, void* ctx);
    };

    // Emisor de pruebas: mensaje -> trama HDLC (entrenamiento, banderas, relleno, FCS) -> NRZI -> niveles +-1 (antes del filtro gaussiano)
    size_t      frameBits(const uint8_t* msg, size_t nbytes, uint8_t* bitsOut, size_t maxBits);

    // ---- Barcos ----
    struct Vessel {
        uint32_t mmsi;
        char     cls;
        char     name[21];
        char     call[8];
        char     dest[21];
        uint8_t  shipType, status;
        bool     hasPos, hasSog, hasCog;
        double   lat, lon;
        float    sogKn, cogDeg;
        int16_t  hdg;                               // 511 = sin dato
        uint16_t length;                            // m (proa + popa)
        uint8_t  beam;
        float    draughtM;
        uint8_t  etaMonth, etaDay, etaHour, etaMinute;
        uint32_t imo;
        int16_t  altM;                              // aeronave SAR
        uint32_t lastMs, posMs;
        uint32_t msgs;
        char     lastText[40];                      // ultimo mensaje 14
        char     channel;
    };

    struct Own { double lat, lon; float sogKn, cogDeg; bool valid; };

    // Distancia (millas nauticas), rumbo verdadero y CPA (millas) / TCPA (minutos; negativo = ya paso) con el movimiento propio
    void        rangeBearing(const Own& o, double lat, double lon, double& nm, double& bearingDeg);
    bool        cpa(const Own& o, const Vessel& v, double& cpaNm, double& tcpaMin);

    class Table {
    public:
        static constexpr int CAP = 80;
        Vessel*  update(const Report& r, uint32_t nowMs, char channel);   // la crea si no existe (sustituye la mas antigua)
        void     purge(uint32_t nowMs, uint32_t maxAgeMs);
        int      count() const { return n; }
        Vessel*  at(int i) { return (i >= 0 && i < n) ? &v[i] : nullptr; }
        Vessel*  find(uint32_t mmsi);
        bool     remove(uint32_t mmsi);
        // indices ordenados por distancia (los que no tienen posicion al final)
        int      sorted(const Own& o, int* idx, int max);
        void     clear() { n = 0; }
    private:
        Vessel   v[CAP];
        int      n = 0;
    };
}

#endif
