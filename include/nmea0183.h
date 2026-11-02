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

#ifndef NMEA0183_H_
#define NMEA0183_H_

#include <stddef.h>
#include <stdint.h>
#include "ais_vessel.h"

// Generacion de frases NMEA 0183 (GNSS, meteorologia) y de mensajes AIS (VDM) para estaciones APRS.
// Modulo sin dependencias de Arduino, para poder probarlo en un PC.

namespace NMEA0183 {

    struct Gnss {
        bool    valid       = false;
        double  lat         = 0;        // grados, + norte
        double  lon         = 0;        // grados, + este
        double  altitudeM   = 0;
        bool    hasAltitude = false;
        double  sogKnots    = 0;
        double  cogDegrees  = 0;
        double  hdop        = 0;
        int     satellites  = 0;
        bool    hasTime     = false;
        int     hour = 0, minute = 0, second = 0, centis = 0;
        bool    hasDate     = false;
        int     day = 1, month = 1, year = 2000;
    };

    struct Environment {
        bool    hasTemperature = false;
        bool    hasHumidity    = false;
        bool    hasPressure    = false;
        double  temperatureC   = 0;
        double  humidityPct    = 0;
        double  pressureHpa    = 0;     // a nivel del mar
    };

    // Cada funcion escribe la frase completa con su terminador "\r\n" y devuelve su longitud (0 si no cabe)
    size_t gga(const Gnss& g, char* buffer, size_t size);
    size_t rmc(const Gnss& g, char* buffer, size_t size);
    size_t vtg(const Gnss& g, char* buffer, size_t size);
    size_t gll(const Gnss& g, char* buffer, size_t size);
    size_t zda(const Gnss& g, char* buffer, size_t size);
    size_t mda(const Environment& e, char* buffer, size_t size);
    size_t xdr(const Environment& e, char* buffer, size_t size);

    uint8_t checksum(const char* body);
    size_t  finish(char lead, const char* body, char* buffer, size_t size);       // lead + body + *CS + CRLF     // XOR de los caracteres entre '$' (o '!') y '*'

    // AIS: MMSI ficticio "8 MID nnnnn" derivado del indicativo (formato de equipo portatil VHF; evita los rangos de
    // emergencia 970/972/974 que dispararian alarmas SART/MOB/EPIRB en el plotter)
    uint32_t mmsiFromCallsign(const char* callsign, uint16_t mid, uint8_t attempt);

    // sogKnots < 0 y cogDegrees < 0 = no disponible; second < 0 = no disponible
    // carrierSense = true: clase B "CS"; false: clase B "SO"
    size_t aisType18(uint32_t mmsi, double lat, double lon, double sogKnots, double cogDegrees, int second, char* buffer, size_t size, bool carrierSense = true);
    size_t aisType24A(uint32_t mmsi, const char* name, char* buffer, size_t size);
    size_t aisType24B(uint32_t mmsi, uint8_t shipType, const char* callsign, char* buffer, size_t size);
    size_t aisType24B(uint32_t mmsi, uint8_t shipType, const char* callsign, uint16_t toBow, uint16_t toStern, uint8_t toPort, uint8_t toStarboard, char* buffer, size_t size);
    // Clase A: informe de posicion (tipo 1) y datos estaticos y de viaje (tipo 5, dos frases VDM seguidas en el mismo buffer)
    size_t aisType1(uint32_t mmsi, uint8_t navStatus, double lat, double lon, double sogKnots, double cogDegrees, int second, char* buffer, size_t size, double headingDegrees = -1.0);
    size_t aisType5(uint32_t mmsi, const char* callsign, const char* name, uint8_t shipType, uint16_t toBow, uint16_t toStern, uint8_t toPort,
                    uint8_t toStarboard, uint8_t draughtDm, uint8_t sequence, char* buffer, size_t size);

    // Tabla de estaciones APRS convertidas en objetivos AIS
    struct AisStation {
        bool        used        = false;
        char        callsign[12] = {0};
        uint32_t    mmsi        = 0;
        double      lat         = 0;
        double      lon         = 0;
        double      sogKnots    = -1;       // < 0 = no disponible
        double      cogDeg      = -1;
        uint32_t    heardAt     = 0;
        uint32_t    emittedAt   = 0;
        uint32_t    staticAt    = 0;
        bool        staticSent  = false;
        bool        hasInfo     = false;    // datos reales de la embarcacion recibidos en la etiqueta "AIS:..." (perfil nautico AIS)
        AIS_Vessel::Info info;
    };

    class AisTable {
        public:
            static const int MAX_STATIONS = 24;
            explicit AisTable(uint16_t mid = 224) : mid_(mid) {}
            // Registra una recepcion. Devuelve el indice de la estacion; isNew = primera vez que se oye
            int  track(const char* callsign, double lat, double lon, uint32_t nowMs, bool& isNew) { return track(callsign, lat, lon, -1, -1, nowMs, isNew); }
            int  track(const char* callsign, double lat, double lon, double sogKnots, double cogDeg, uint32_t nowMs, bool& isNew);
            // Siguiente estacion cuyo informe de posicion hay que repetir (oida hace menos de ttlMs y sin emitir desde refreshMs); -1 si ninguna
            int  nextRefresh(uint32_t nowMs, uint32_t refreshMs, uint32_t ttlMs);
            // Guarda los datos de la etiqueta AIS (MMSI real, nombre, dimensiones, tipo); si cambian, los datos estaticos se vuelven a enviar
            void setInfo(int index, const AIS_Vessel::Info& info);
            bool staticDue(int index, uint32_t nowMs, uint32_t periodMs) const;
            void markEmitted(int index, uint32_t nowMs);
            void markStatic(int index, uint32_t nowMs);
            const AisStation& at(int index) const { return stations_[index]; }
        private:
            uint16_t    mid_;
            AisStation  stations_[MAX_STATIONS];
    };

}

#endif
