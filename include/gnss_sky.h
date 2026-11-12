/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+.
 * Licensed under the GNU General Public License, version 3 or later.
 */

#ifndef GNSS_SKY_H_
#define GNSS_SKY_H_

#include <Arduino.h>

// Cielo del receptor GNSS para la app (evento "sky"): satelites a la vista de las frases GSV (sistema, numero, elevacion,
// azimut, SNR), los usados en el fix de GSA, PDOP / HDOP / VDOP y el error estimado de GST (si el receptor la envia).
// Se alimenta con los mismos caracteres que TinyGPS; no envia nada al receptor.
namespace GNSS_Sky {

    struct Sat {
        char     sys;        // G GPS, R GLONASS, E Galileo, C BeiDou, J QZSS, S SBAS, N otro
        uint16_t svid;
        int8_t   elev;       // grados, -1 desconocida
        int16_t  azim;       // grados, -1 desconocido
        int8_t   snr;        // dB-Hz, -1 sin senal (la mejor de sus bandas)
        bool     used;
        uint32_t seenMs;
        int8_t   bandSnr[3]; // por banda (BAND_L1, BAND_L5, BAND_L2): SNR de la ultima frase GSV de esa senal (NMEA 4.10+)
        uint32_t bandMs[3];
    };

    // Bandas por el identificador de senal de GSV (NMEA 4.10/4.11): L1 = L1 / E1 / B1 / G1; L5 = L5 / E5a / B2a / NavIC L5;
    // L2 = el resto (L2, G2, E5b, E6, B2I, B3...). Frases sin identificador de senal: L1.
    enum Band : uint8_t { BAND_L1, BAND_L5, BAND_L2, BAND_COUNT };
    bool        hasBand(const Sat& s, uint8_t band);     // senal en esa banda (SNR > 0) en los ultimos 12 s

    void        feed(char c);
    uint8_t     count();                    // satelites recientes (vistos en los ultimos 12 s)
    const Sat*  sat(uint8_t index);         // nullptr fuera de rango o caducado
    float       pdop();                     // NAN si no se sabe
    float       hdop();
    float       vdop();
    float       errorM();                   // de GST: raiz de (sigma lat^2 + sigma lon^2); NAN si no hay GST reciente

}

#endif
