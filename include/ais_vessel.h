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

#ifndef AIS_VESSEL_H_
#define AIS_VESSEL_H_

#include <stddef.h>
#include <stdint.h>

// Perfil de baliza APRS "AIS nautico": tipos de embarcacion (clase AIS, SSID, simbolo APRS, tipo AIS) y la etiqueta
// "AIS:..." que viaja en el comentario APRS con los datos estaticos (MMSI, nombre, dimensiones, tipo).
// Modulo sin dependencias de Arduino, para poder probarlo en un PC.

namespace AIS_Vessel {

    enum AisClass : char {
        CLASS_A     = 'A',
        CLASS_B_SO  = 'S',      // Clase B "SO" (acceso por TDMA organizado en el tiempo)
        CLASS_B_CS  = 'C'       // Clase B "CS" (deteccion de portadora)
    };

    struct VesselType {
        uint8_t     id;
        const char* label;
        char        aisClass;   // AisClass
        int8_t      ssid;       // SSID fijo del indicativo
        char        overlay;    // '/' = tabla primaria; '\\' = tabla alternativa sin overlay; otro caracter = overlay
        char        symbol;     // caracter del simbolo APRS
        uint8_t     shipType;   // tipo de buque AIS (ITU-R M.1371-6, Cuadro 53)
    };

    // Ajustes de SmartBeacon que corresponden a cada clase (ver ais_profiles.h: 3 = clase A, 4 = clase B "SO", 5 = clase B "CS")
    uint8_t     smartBeaconSettingFor(char aisClass);

    uint8_t             typeCount();
    const VesselType&   vesselType(uint8_t id);         // id fuera de rango -> primer tipo

    // Indicativo con el SSID fijo del tipo de embarcacion ("ea7jrs-9" -> "EA7JRS-5")
    size_t      forceCallsign(const char* callsign, int8_t ssid, char* out, size_t size);
    // Indicativo sin SSID ("EA7JRS-5" -> "EA7JRS")
    size_t      baseCallsign(const char* callsign, char* out, size_t size);

    // Nombre en mayusculas con los caracteres que admite AIS y que no rompen la etiqueta (sin , ; :), maximo 20
    size_t      sanitizeName(const char* name, char* out, size_t size);
    bool        validMmsi(uint32_t mmsi);
    // Eslora admitida: clase B (recreo) 6 - 24 m; clase A sin limite practico (hasta 999 m). 0 = sin indicar
    uint16_t    clampLength(char aisClass, uint16_t length);               // MMSI de buque: 200000000 - 799999999

    struct Info {
        char        aisClass    = CLASS_B_CS;
        uint32_t    mmsi        = 0;
        uint16_t    length      = 0;        // eslora, m
        uint16_t    beam        = 0;        // manga, m
        uint8_t     draughtDm   = 0;        // calado, decimas de metro
        uint8_t     shipType    = 0;
        char        name[21]    = {0};
    };

    // "AIS:C224123456,12x4x1.8,37,SEA BREEZE;"  -> devuelve la longitud (0 si no cabe o los datos no son validos)
    size_t      buildTag(const Info& info, char* out, size_t size);
    // Busca la etiqueta en cualquier texto (comentario o trama completa)
    bool        parseTag(const char* text, Info& info);

    // Reparto de las dimensiones AIS (bow/stern/port/starboard) a partir de eslora y manga con la antena en el centro
    void        splitDimensions(uint16_t length, uint16_t beam, uint16_t& toBow, uint16_t& toStern, uint8_t& toPort, uint8_t& toStarboard);

}

#endif
