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

    // Etiqueta legible (aprs.fi y cualquier programa APRS la muestran tal cual en el comentario):
    //   "AIS B-CS: SEA BREEZE, MMSI 224123456, 12x4 m, draft 1.8 m, sailing;"
    // Clase "A", "B-SO" o "B-CS"; tipo "sailing" (36), "pleasure" (37) u otro como "type 70". Sin eslora ni manga: "?x? m".
    // parseTag lee tambien la forma compacta anterior ("AIS:C224123456,12x4x1.8,37,SEA BREEZE;").
    size_t buildTag(const Info& info, char* out, size_t size) {
        if (!validMmsi(info.mmsi)) return 0;
        char name[21];
        sanitizeName(info.name, name, sizeof(name));
        const char* cls = info.aisClass == CLASS_A ? "A" : (info.aisClass == CLASS_B_SO ? "B-SO" : "B-CS");
        uint16_t length = info.length > 999 ? 999 : info.length;
        uint16_t beam   = info.beam > 126 ? 126 : info.beam;
        char dims[24], type[16];
        if (length || beam) snprintf(dims, sizeof(dims), "%ux%u m", (unsigned) length, (unsigned) beam);
        else snprintf(dims, sizeof(dims), "?x? m");
        if (info.shipType == 36)      snprintf(type, sizeof(type), "sailing");
        else if (info.shipType == 37) snprintf(type, sizeof(type), "pleasure");
        else                          snprintf(type, sizeof(type), "type %u", (unsigned) info.shipType);
        int n = snprintf(out, size, "AIS %s: %s, MMSI %lu, %s, draft %u.%u m, %s;", cls, name[0] ? name : "-", (unsigned long) info.mmsi, dims,
                         (unsigned) (info.draughtDm / 10), (unsigned) (info.draughtDm % 10), type);
        return n > 0 && (size_t) n < size ? (size_t) n : 0;
    }

    namespace {
        bool parseCompact(const char* p, Info& info) {              // "C224123456,12x4x1.8,37,SEA BREEZE;"
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

        bool skip(const char*& p, const char* lit) {
            size_t n = strlen(lit);
            if (strncmp(p, lit, n) != 0) return false;
            p += n;
            return true;
        }

        bool parseReadable(const char* p, Info& info) {             // "B-CS: SEA BREEZE, MMSI 224123456, 12x4 m, draft 1.8 m, sailing;"
            Info parsed;
            if (skip(p, "A:"))         parsed.aisClass = CLASS_A;
            else if (skip(p, "B-SO:")) parsed.aisClass = CLASS_B_SO;
            else if (skip(p, "B-CS:")) parsed.aisClass = CLASS_B_CS;
            else return false;
            if (*p == ' ') p++;
            size_t used = 0;
            while (*p && *p != ',' && *p != ';' && used < 20) parsed.name[used++] = *p++;
            parsed.name[used] = 0;
            if (strcmp(parsed.name, "-") == 0) parsed.name[0] = 0;
            if (!skip(p, ", MMSI ")) return false;
            char* end = nullptr;
            unsigned long mmsi = strtoul(p, &end, 10);
            if (end == p || !validMmsi((uint32_t) mmsi)) return false;
            parsed.mmsi = (uint32_t) mmsi;
            p = end;
            if (!skip(p, ", ")) return false;
            if (skip(p, "?x? m")) {
                parsed.length = parsed.beam = 0;
            } else {
                unsigned long length = strtoul(p, &end, 10);
                if (end == p || *end != 'x') return false;
                p = end + 1;
                unsigned long beam = strtoul(p, &end, 10);
                if (end == p) return false;
                p = end;
                if (!skip(p, " m")) return false;
                parsed.length = (uint16_t) (length > 999 ? 999 : length);
                parsed.beam   = (uint16_t) (beam > 126 ? 126 : beam);
            }
            if (!skip(p, ", draft ")) return false;
            double draught = strtod(p, &end);
            if (end == p) return false;
            p = end;
            long dm = (long) (draught * 10.0 + 0.5);
            parsed.draughtDm = (uint8_t) (dm < 0 ? 0 : (dm > 255 ? 255 : dm));
            if (!skip(p, " m, ")) return false;
            if (skip(p, "sailing"))       parsed.shipType = 36;
            else if (skip(p, "pleasure")) parsed.shipType = 37;
            else if (skip(p, "type ")) {
                unsigned long t = strtoul(p, &end, 10);
                if (end == p || t > 99) return false;
                parsed.shipType = (uint8_t) t;
                p = end;
            } else return false;
            if (*p != ';') return false;
            info = parsed;
            return true;
        }
    }

    bool parseTag(const char* text, Info& info) {
        if (text == nullptr) return false;
        for (const char* start = strstr(text, "AIS"); start != nullptr; start = strstr(start + 3, "AIS")) {
            if (start[3] == ':' && parseCompact(start + 4, info)) return true;
            if (start[3] == ' ' && parseReadable(start + 4, info)) return true;
        }
        return false;
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
