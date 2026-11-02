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

#ifndef NMEA2000_H_
#define NMEA2000_H_

#include <stddef.h>
#include <stdint.h>

// Codificacion de PGN NMEA 2000 y de los formatos de texto que usan las pasarelas (SeaSmart $PCDIN y Actisense ASCII).
// Modulo sin dependencias de Arduino. No genera tramas CAN: para el bus fisico hace falta un transceptor externo.

namespace NMEA2000 {

    static const uint32_t PGN_POSITION_RAPID    = 129025;
    static const uint32_t PGN_COG_SOG_RAPID     = 129026;
    static const uint32_t PGN_GNSS_POSITION     = 129029;
    static const uint32_t PGN_ENVIRONMENT       = 130311;
    static const uint32_t PGN_TEMPERATURE       = 130312;
    static const uint32_t PGN_HUMIDITY          = 130313;
    static const uint32_t PGN_PRESSURE          = 130314;

    // Cada funcion escribe el campo de datos del PGN en out y devuelve su longitud (0 si no cabe)
    size_t pgn129025(uint8_t* out, size_t size, double lat, double lon);                                                        // 8 bytes
    size_t pgn129026(uint8_t* out, size_t size, uint8_t sid, double cogDeg, double sogKnots);                                   // 8 bytes (cog < 0 / sog < 0 = no disponible)
    size_t pgn129029(uint8_t* out, size_t size, uint8_t sid, int year, int month, int day, double secondsOfDay,
                     double lat, double lon, double altitudeM, bool hasAltitude, int satellites, double hdop);                  // 43 bytes
    size_t pgn130311(uint8_t* out, size_t size, uint8_t sid, bool hasT, double tempC, bool hasH, double humidityPct, bool hasP, double pressureHpa);   // 8 bytes
    size_t pgn130312(uint8_t* out, size_t size, uint8_t sid, uint8_t instance, uint8_t source, double tempC);                   // 8 bytes
    size_t pgn130313(uint8_t* out, size_t size, uint8_t sid, uint8_t instance, uint8_t source, double humidityPct);             // 8 bytes
    size_t pgn130314(uint8_t* out, size_t size, uint8_t sid, uint8_t instance, uint8_t source, double pressureHpa);             // 8 bytes

    // Formatos de texto. Devuelven la longitud de la linea (con \r\n) o 0 si no cabe.
    size_t seaSmart(char* out, size_t size, uint32_t pgn, uint32_t timestampMs, uint8_t source, const uint8_t* data, size_t length);
    size_t actisense(char* out, size_t size, int hh, int mm, int ss, int ms, uint8_t source, uint8_t destination, uint8_t priority,
                     uint32_t pgn, const uint8_t* data, size_t length);

    int32_t daysSince1970(int year, int month, int day);

}

#endif
