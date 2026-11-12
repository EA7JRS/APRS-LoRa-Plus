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

// Hora del tracker, con las mismas reglas que el resto de equipos APRS LoRa+ (plotter, Display, rotor): manda el GPS; sin GPS,
// NTP en cuanto el tracker tiene WiFi como cliente (KISS por TCP, NMEA por TCP, rotor por la red...): ntp.roa.es, la hora
// oficial de Espana (Real Instituto y Observatorio de la Armada), y de reserva hora.roa.es y pool.ntp.org; luego el RTC
// (T-Beam Supreme) y la hora de DAPNET. La hora que usa el firmware es la de TimeLib; aqui se mantiene igual el reloj del
// sistema (time()), que es el que ajusta NTP.

#ifndef TIME_SYNC_H_
#define TIME_SYNC_H_

#include <Arduino.h>

namespace TIME_Utils {

    enum Source : uint8_t { SRC_NONE = 0, SRC_GPS, SRC_NTP, SRC_RTC, SRC_DAPNET };

    constexpr const char* NTP_SERVER_1 = "ntp.roa.es";
    constexpr const char* NTP_SERVER_2 = "hora.roa.es";
    constexpr const char* NTP_SERVER_3 = "pool.ntp.org";

    void        loop();                 // arranca NTP con WiFi cliente y aplica su hora si el GPS no la da
    void        gpsTimeSet();           // GPS_Utils::setDateFromData acaba de poner la hora (TimeLib)
    void        note(Source s);         // la puso otro (RTC, DAPNET)
    bool        gpsTimeOk();            // el GPS da hora ahora mismo
    Source      source();
    const char* sourceName();           // "GPS", "NTP", "RTC", "DAPNET", "NONE"
    uint32_t    sinceMs();              // desde que se puso (0 = nunca)
    bool        ntpStarted();
}

#endif
