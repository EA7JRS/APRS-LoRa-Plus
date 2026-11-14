/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Receptor AIS del tracker: junta los mensajes de la radio (modo AIS de la T-TWR, SA868), de un modulo AIS externo por UART
 * (!AIVDM: dAISy, Quark-elec...) y de la app (receptor RTL-SDR del movil, orden BLE "aisin"); lleva la lista de barcos, la
 * distancia / rumbo / CPA / TCPA respecto a la estacion propia, las alarmas (riesgo de colision, zona de guardia, AIS-SART / MOB /
 * EPIRB), reenvia las frases !AIVDM a la salida NMEA y a la app, y alterna los canales A / B en la T-TWR. Nucleo: include/ais_core.h.
 */

#ifndef AIS_UTILS_H_
#define AIS_UTILS_H_

#include <Arduino.h>
#include "ais_core.h"

namespace AIS_Utils {

    enum Source : uint8_t { SRC_RADIO = 0, SRC_MODULE = 1, SRC_APP = 2, SRC_APRS = 3 };

    void        begin();                                     // al arrancar (modulo externo por UART)
    void        loop();                                      // bucle principal
    bool        active();                                    // modo AIS, modulo externo o barcos recibidos de la app
    void        onMessage(const uint8_t* msg, size_t nbits, char channel, Source src);
    bool        feedNmea(const char* line, Source src);      // una frase !AIVDM (modulo o app)
    // Baliza APRS LoRa del perfil nautico AIS (etiqueta "AIS:..." o perfil LoRa AIS en uso): barco en la tabla AIS como uno de
    // verdad (clase, MMSI, nombre, dimensiones), canal 'L'. Sin etiqueta todavia, con el MMSI ficticio del indicativo
    void        onAprsVessel(const char* callsign, double lat, double lon, double speedKmh, double courseDeg, const char* packetText);

    AisCore::Table& table();
    bool        own(AisCore::Own& out);                      // posicion y movimiento propios (GPS)
    int         sorted(int* idx, int max);                   // indices de table() por distancia
    uint32_t    messages();
    uint32_t    lastRxMs();
    const char* sourceName();                                // "SA868 A", "module", "app"...
    char        channel();                                   // canal que escucha la T-TWR ('A' / 'B'; 0 sin radio)
    void        setChannelMode(int mode);                    // 0 A, 1 B, 2 alternar
    bool        dangerous(const AisCore::Vessel& v, double* cpaNm = nullptr, double* tcpaMin = nullptr, double* rangeNm = nullptr);
    void        clear();
    void        goTo(uint32_t mmsi);                         // brujula (y rotor) hacia el barco: lo anade a la lista de estaciones
    void        stationId(const AisCore::Vessel& v, char* out, size_t n);   // nombre corto del barco (o el MMSI)
}

#endif
