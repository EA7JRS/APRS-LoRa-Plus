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

#include "ais_vessel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

    // Simbolos APRS: https://www.aprs.org/symbols/symbolsX.txt (/Y yate SSID-5, /s buque SSID-8) y
    // https://www.aprs.org/symbols/symbols-new.txt (bloque SHIPS, tabla alternativa '\' con overlay)
    // Tipos AIS: ITU-R M.1371-6, Cuadro 53 (30 pesca, 31/32 remolque, 34 buceo, 35 militar, 36 vela, 37 recreo,
    // 40 alta velocidad, 50 practico, 51 SAR, 52 remolcador, 55 autoridad, 58 sanitario, 60 pasaje, 70 carga, 80 tanque)
    const AIS_Vessel::VesselType TYPES[] = {
        {  0, "Sailing yacht (recreational)",    AIS_Vessel::CLASS_B_CS, 5, '/',  'Y', 36 },
        {  1, "Power boat (recreational)",       AIS_Vessel::CLASS_B_SO, 8, '/',  's', 37 },
        {  2, "Cargo ship",                      AIS_Vessel::CLASS_A,    8, 'C',  's', 70 },
        {  3, "Tanker",                          AIS_Vessel::CLASS_A,    8, '\\', 's', 80 },
        {  4, "Passenger ship / ferry",          AIS_Vessel::CLASS_A,    8, 'X',  's', 60 },
        {  5, "Fishing vessel",                  AIS_Vessel::CLASS_A,    8, 'F',  's', 30 },
        {  6, "Tug",                             AIS_Vessel::CLASS_A,    8, 'T',  's', 52 },
        {  7, "Pilot vessel",                    AIS_Vessel::CLASS_A,    8, 'P',  's', 50 },
        {  8, "High-speed craft",                AIS_Vessel::CLASS_A,    8, 'H',  's', 40 },
        {  9, "Law enforcement",                 AIS_Vessel::CLASS_A,    8, 'L',  's', 55 },
        { 10, "Search and rescue",               AIS_Vessel::CLASS_A,    8, 'S',  's', 51 },
        { 11, "Emergency / medical transport",   AIS_Vessel::CLASS_A,    8, 'E',  's', 58 },
        { 12, "Military",                        AIS_Vessel::CLASS_A,    8, 'M',  's', 35 },
        { 13, "Diving vessel",                   AIS_Vessel::CLASS_A,    8, 'D',  's', 34 },
        { 14, "Sailing ship (large)",            AIS_Vessel::CLASS_A,    8, 'Y',  's', 36 },
        { 15, "Wing-in-ground / hovercraft",     AIS_Vessel::CLASS_A,    8, 'W',  's', 20 },
        { 16, "Other ship",                      AIS_Vessel::CLASS_A,    8, '\\', 's', 90 }
    };
    constexpr uint8_t TYPE_COUNT = sizeof(TYPES) / sizeof(TYPES[0]);

    bool nameCharAllowed(char c) {
        if (c >= 'A' && c <= 'Z') return true;
        if (c >= '0' && c <= '9') return true;
        return c == ' ' || c == '-' || c == '.' || c == '/' || c == '\'' || c == '&' || c == '(' || c == ')' || c == '+' || c == '#';
    }

}

namespace AIS_Vessel {

    uint8_t smartBeaconSettingFor(char aisClass) {
        switch (aisClass) {
            case CLASS_A:       return 3;
            case CLASS_B_SO:    return 4;
            default:            return 5;
        }
    }

    uint8_t typeCount() {
        return TYPE_COUNT;
    }

    const VesselType& vesselType(uint8_t id) {
        return id < TYPE_COUNT ? TYPES[id] : TYPES[0];
    }

    size_t baseCallsign(const char* callsign, char* out, size_t size) {
        if (size == 0) return 0;
        size_t used = 0;
        for (const char* p = callsign ? callsign : ""; *p && *p != '-' && used + 1 < size; p++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c = (char) (c - 32);
            if (c == ' ') continue;
            out[used++] = c;
        }
        out[used] = 0;
        return used;
    }

    size_t forceCallsign(const char* callsign, int8_t ssid, char* out, size_t size) {
        char base[12];
        baseCallsign(callsign, base, sizeof(base));
        int n = snprintf(out, size, "%s-%d", base, (int) ssid);
        return n > 0 && (size_t) n < size ? (size_t) n : 0;
    }

    size_t sanitizeName(const char* name, char* out, size_t size) {
        if (size == 0) return 0;
        size_t used = 0;
        bool   lastSpace = true;                    // sin espacios al principio ni dobles
        for (const char* p = name ? name : ""; *p && used < 20 && used + 1 < size; p++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c = (char) (c - 32);
            if (!nameCharAllowed(c)) c = ' ';
            if (c == ' ' && lastSpace) continue;
            lastSpace = (c == ' ');
            out[used++] = c;
        }
        while (used > 0 && out[used - 1] == ' ') used--;
        out[used] = 0;
        return used;
    }

    bool validMmsi(uint32_t mmsi) {
        return mmsi >= 200000000UL && mmsi <= 799999999UL;
    }

    uint16_t clampLength(char aisClass, uint16_t length) {
        if (length == 0) return 0;
        if (aisClass == CLASS_A) return length > 999 ? 999 : length;
        return length < 6 ? 6 : (length > 24 ? 24 : length);
    }

    size_t buildTag(const Info& info, char* out, size_t size) {
        if (!validMmsi(info.mmsi)) return 0;
        char name[21];
        sanitizeName(info.name, name, sizeof(name));
        char cls = (info.aisClass == CLASS_A || info.aisClass == CLASS_B_SO) ? info.aisClass : (char) CLASS_B_CS;
        uint16_t length = info.length > 999 ? 999 : info.length;
        uint16_t beam   = info.beam > 126 ? 126 : info.beam;
        int n = snprintf(out, size, "AIS:%c%lu,%ux%ux%u.%u,%u,%s;", cls, (unsigned long) info.mmsi, (unsigned) length, (unsigned) beam,
                         (unsigned) (info.draughtDm / 10), (unsigned) (info.draughtDm % 10), (unsigned) info.shipType, name);
        return n > 0 && (size_t) n < size ? (size_t) n : 0;
    }

    bool parseTag(const char* text, Info& info) {
        if (text == nullptr) return false;
        const char* start = strstr(text, "AIS:");
        if (start == nullptr) return false;
        const char* p = start + 4;
        char cls = *p++;
        if (cls != CLASS_A && cls != CLASS_B_SO && cls != CLASS_B_CS) return false;
        char* end = nullptr;
        unsigned long mmsi = strtoul(p, &end, 10);
        if (end == p || !validMmsi((uint32_t) mmsi) || *end != ',') return false;
        p = end + 1;
        unsigned long length = strtoul(p, &end, 10);
        if (end == p || *end != 'x') return false;
        p = end + 1;
        unsigned long beam = strtoul(p, &end, 10);
        if (end == p || *end != 'x') return false;
        p = end + 1;
        double draught = strtod(p, &end);
        if (end == p || *end != ',') return false;
        p = end + 1;
        unsigned long shipType = strtoul(p, &end, 10);
        if (end == p || *end != ',' || shipType > 99) return false;
        p = end + 1;
        Info parsed;
        parsed.aisClass  = cls;
        parsed.mmsi      = (uint32_t) mmsi;
        parsed.length    = (uint16_t) (length > 999 ? 999 : length);
        parsed.beam      = (uint16_t) (beam > 126 ? 126 : beam);
        long dm          = (long) (draught * 10.0 + 0.5);
        parsed.draughtDm = (uint8_t) (dm < 0 ? 0 : (dm > 255 ? 255 : dm));
        parsed.shipType  = (uint8_t) shipType;
        size_t used = 0;
        while (*p && *p != ';' && used < 20) parsed.name[used++] = *p++;
        parsed.name[used] = 0;
        info = parsed;
        return true;
    }

    void splitDimensions(uint16_t length, uint16_t beam, uint16_t& toBow, uint16_t& toStern, uint8_t& toPort, uint8_t& toStarboard) {
        if (length > 1022) length = 1022;
        if (beam > 126) beam = 126;
        toBow       = (uint16_t) ((length + 1) / 2);
        toStern     = (uint16_t) (length - toBow);
        toPort      = (uint8_t) (beam / 2);
        toStarboard = (uint8_t) (beam - toPort);
    }

}
