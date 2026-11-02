/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of LoRa APRS Tracker.
 *
 * LoRa APRS Tracker is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LoRa APRS Tracker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef NMEA_CORE_H_
#define NMEA_CORE_H_

#include <stddef.h>
#include <stdint.h>
#include "nmea0183.h"

// Motor de salida NMEA sin dependencias de Arduino: decide que frases se emiten y cuando, y las entrega a un receptor
// (USB, TCP, UART...) mediante una funcion de retorno. Las estaciones APRS oidas se convierten en objetivos AIS.
namespace NMEA_Core {

    typedef void (*SinkFn)(const char* text, size_t length, void* context);

    struct Options {
        bool        gnss                = true;         // RMC, GGA, VTG, GLL y ZDA
        bool        environment         = true;         // MDA y XDR
        bool        ais                 = true;         // !AIVDM de las estaciones APRS oidas
        bool        seaSmart            = false;        // NMEA 2000 como $PCDIN
        bool        actisense           = false;        // NMEA 2000 como Actisense N2K ASCII
        uint16_t    mid                 = 224;          // MID del MMSI sintetico (224 = Espana)
        uint8_t     n2kSource           = 0x42;         // direccion de origen declarada en NMEA 2000
        uint32_t    gnssPeriodMs        = 1000;
        uint32_t    zdaPeriodMs         = 10000;
        uint32_t    environmentPeriodMs = 10000;
        uint32_t    aisRefreshMs        = 30000;        // repeticion del informe de posicion de cada estacion
        uint32_t    aisTtlMs            = 1800000;      // se deja de repetir a los 30 min sin oir a la estacion
        uint32_t    aisStaticPeriodMs   = 360000;       // datos estaticos (tipo 24 A/B) cada 6 min
    };

    class Engine {
        public:
            Engine();
            void configure(const Options& options);
            void setSink(SinkFn sink, void* context);
            void update(uint32_t nowMs, const NMEA0183::Gnss& own, const NMEA0183::Environment& environment);
            // info != nullptr: datos reales de la embarcacion (etiqueta "AIS:..." del perfil nautico AIS)
            void onAprsStation(uint32_t nowMs, const char* callsign, double lat, double lon, double sogKnots, double cogDeg, const NMEA0183::Gnss& own,
                               const AIS_Vessel::Info* info = nullptr);
            int  stationCount() const;
        private:
            void emit(const char* text, size_t length);
            void emitPgn(uint32_t nowMs, const NMEA0183::Gnss& own, uint32_t pgn, const uint8_t* data, size_t length);
            void emitOwnGnss(uint32_t nowMs, const NMEA0183::Gnss& g);
            void emitEnvironment(uint32_t nowMs, const NMEA0183::Gnss& g, const NMEA0183::Environment& e);
            void emitAisPosition(uint32_t nowMs, int index, const NMEA0183::Gnss& own);
            void emitAisStatic(uint32_t nowMs, int index);

            Options             options_;
            SinkFn              sink_;
            void*               context_;
            NMEA0183::AisTable  ais_;
            uint32_t            lastGnss_, lastZda_, lastEnvironment_;
            bool                startedGnss_, startedZda_, startedEnvironment_;
            uint8_t             sid_;
            uint8_t             vdmSequence_ = 0;
    };

}

#endif
