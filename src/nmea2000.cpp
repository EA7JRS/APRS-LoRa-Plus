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

#include "nmea2000.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

    void put16(uint8_t* p, uint16_t v)  { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
    void put32(uint8_t* p, uint32_t v)  { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF; }
    void put64(uint8_t* p, uint64_t v)  { for (int i = 0; i < 8; i++) p[i] = (v >> (8 * i)) & 0xFF; }

    uint16_t scaledU16(double value, double resolution, bool available) {
        if (!available || value < 0) return 0xFFFF;
        double v = round(value / resolution);
        return v > 65534.0 ? 0xFFFE : (uint16_t) v;
    }

    int16_t scaledS16(double value, double resolution, bool available) {
        if (!available) return 0x7FFF;
        double v = round(value / resolution);
        if (v > 32766.0)  v = 32766.0;
        if (v < -32767.0) v = -32767.0;
        return (int16_t) v;
    }

    size_t toHex(const uint8_t* data, size_t length, char* out, size_t size) {
        if (size < length * 2 + 1) return 0;
        for (size_t i = 0; i < length; i++) snprintf(out + 2 * i, 3, "%02X", data[i]);
        return length * 2;
    }

}

namespace NMEA2000 {

    int32_t daysSince1970(int year, int month, int day) {
        // algoritmo de dias civiles (Howard Hinnant)
        year -= month <= 2;
        int era = (year >= 0 ? year : year - 399) / 400;
        unsigned yoe = (unsigned) (year - era * 400);
        unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + (int) doe - 719468;
    }

    size_t pgn129025(uint8_t* out, size_t size, double lat, double lon) {
        if (size < 8) return 0;
        put32(out,     (uint32_t) (int32_t) lround(lat * 1e7));
        put32(out + 4, (uint32_t) (int32_t) lround(lon * 1e7));
        return 8;
    }

    size_t pgn129026(uint8_t* out, size_t size, uint8_t sid, double cogDeg, double sogKnots) {
        if (size < 8) return 0;
        out[0] = sid;
        out[1] = 0xFC;                                                          // referencia 0 = verdadero, bits reservados a 1
        put16(out + 2, scaledU16(cogDeg * M_PI / 180.0, 0.0001, cogDeg >= 0));
        put16(out + 4, scaledU16(sogKnots * 0.514444, 0.01, sogKnots >= 0));
        out[6] = out[7] = 0xFF;
        return 8;
    }

    size_t pgn129029(uint8_t* out, size_t size, uint8_t sid, int year, int month, int day, double secondsOfDay,
                     double lat, double lon, double altitudeM, bool hasAltitude, int satellites, double hdop) {
        if (size < 43) return 0;
        memset(out, 0xFF, 43);
        out[0] = sid;
        put16(out + 1, (uint16_t) daysSince1970(year, month, day));
        put32(out + 3, (uint32_t) lround(secondsOfDay * 10000.0));
        put64(out + 7,  (uint64_t) (int64_t) llround(lat * 1e7) * 1000000000ULL);     // 1e-16 grados
        put64(out + 15, (uint64_t) (int64_t) llround(lon * 1e7) * 1000000000ULL);
        put64(out + 23, hasAltitude ? (uint64_t) (int64_t) llround(altitudeM * 1e6) : 0x7FFFFFFFFFFFFFFFULL);   // 1e-6 m
        out[31] = 0x10;                                                         // tipo GPS (bits 0-3), metodo "GNSS fix" (bits 4-7)
        out[32] = 0xFC;                                                         // sin integridad, reservado a 1
        out[33] = (uint8_t) (satellites < 0 ? 0xFF : (satellites > 254 ? 254 : satellites));
        put16(out + 34, (uint16_t) scaledS16(hdop, 0.01, hdop >= 0));
        put16(out + 36, 0x7FFF);                                                // PDOP no disponible
        put32(out + 38, 0x7FFFFFFFUL);                                          // separacion del geoide no disponible
        out[42] = 0;                                                            // sin estaciones de referencia
        return 43;
    }

    size_t pgn130311(uint8_t* out, size_t size, uint8_t sid, bool hasT, double tempC, bool hasH, double humidityPct, bool hasP, double pressureHpa) {
        if (size < 8) return 0;
        out[0] = sid;
        out[1] = 0x41;                                                          // fuente de temperatura 1 (exterior) | fuente de humedad 1 (exterior)
        put16(out + 2, scaledU16(tempC + 273.15, 0.01, hasT));
        put16(out + 4, (uint16_t) scaledS16(humidityPct, 0.004, hasH));
        put16(out + 6, scaledU16(pressureHpa, 1.0, hasP));                      // resolucion 100 Pa = 1 hPa
        return 8;
    }

    size_t pgn130312(uint8_t* out, size_t size, uint8_t sid, uint8_t instance, uint8_t source, double tempC) {
        if (size < 8) return 0;
        out[0] = sid;
        out[1] = instance;
        out[2] = source;                                                        // 0 mar, 1 exterior, 2 interior...
        put16(out + 3, scaledU16(tempC + 273.15, 0.01, true));
        put16(out + 5, 0xFFFF);                                                 // temperatura de consigna no disponible
        out[7] = 0xFF;
        return 8;
    }

    size_t pgn130313(uint8_t* out, size_t size, uint8_t sid, uint8_t instance, uint8_t source, double humidityPct) {
        if (size < 8) return 0;
        out[0] = sid;
        out[1] = instance;
        out[2] = source;                                                        // 0 interior, 1 exterior
        put16(out + 3, (uint16_t) scaledS16(humidityPct, 0.004, true));
        put16(out + 5, 0x7FFF);
        out[7] = 0xFF;
        return 8;
    }

    size_t pgn130314(uint8_t* out, size_t size, uint8_t sid, uint8_t instance, uint8_t source, double pressureHpa) {
        if (size < 8) return 0;
        out[0] = sid;
        out[1] = instance;
        out[2] = source;                                                        // 0 = presion atmosferica
        put32(out + 3, (uint32_t) lround(pressureHpa * 1000.0));                // resolucion 0,1 Pa
        out[7] = 0xFF;
        return 8;
    }

    size_t seaSmart(char* out, size_t size, uint32_t pgn, uint32_t timestampMs, uint8_t source, const uint8_t* data, size_t length) {
        char hex[2 * 223 + 1];
        if (length > 223 || (length > 0 && toHex(data, length, hex, sizeof(hex)) == 0)) return 0;
        if (length == 0) hex[0] = 0;
        char body[2 * 223 + 48];
        int n = snprintf(body, sizeof(body), "PCDIN,%06X,%08X,%02X,%s", (unsigned) pgn, (unsigned) timestampMs, source, hex);
        if (n < 0 || (size_t) n >= sizeof(body)) return 0;
        uint8_t cs = 0;
        for (int i = 0; i < n; i++) cs ^= (uint8_t) body[i];
        int total = snprintf(out, size, "$%s*%02X\r\n", body, cs);
        return (total < 0 || (size_t) total >= size) ? 0 : (size_t) total;
    }

    size_t actisense(char* out, size_t size, int hh, int mm, int ss, int ms, uint8_t source, uint8_t destination, uint8_t priority,
                     uint32_t pgn, const uint8_t* data, size_t length) {
        char hex[2 * 223 + 1];
        if (length > 223 || (length > 0 && toHex(data, length, hex, sizeof(hex)) == 0)) return 0;
        if (length == 0) hex[0] = 0;
        int total = snprintf(out, size, "A%02d%02d%02d.%03d %02X%02X%X %05X %s\r\n",
                             hh % 100, mm % 60, ss % 60, ms % 1000, source, destination, priority & 0x0F, (unsigned) pgn, hex);
        return (total < 0 || (size_t) total >= size) ? 0 : (size_t) total;
    }

}
